#include "SphynxClient.h"

SphynxClient::SphynxClient()
{
    ServerTimeDeltaMsec = 0;
    SendingHandshakes = false;

    RPCHeartbeatTCP.CallSender = TCPCallSender;
    RPCHeartbeatUDP.CallSender = UDPCallSender;
    RPCHandshakeUDP.CallSender = UDPCallSender;

    Router.Set<S2CTCPHandshakeT>(S2CTCPHandshakeID, [this](u32 cookie, u16 udpPort)
    {
        LOG(INFO) << "Got TCP handshake: cookie=" << cookie << ", UDPport=" << udpPort;

        ConnectionCookie = cookie;
        PeerUDPAddress = asio::ip::udp::endpoint(ServerTCPAddr.address(), udpPort);

        asio::ip::udp::endpoint UDPEndpoint(asio::ip::udp::v4(), 0);
        UDPSocket = std::make_shared<asio::ip::udp::socket>(*Context, UDPEndpoint);

        // Set socket options
        asio::socket_base::send_buffer_size SendBufferSizeOption(kUDPSendBufferSizeBytes);
        UDPSocket->set_option(SendBufferSizeOption);
        asio::socket_base::receive_buffer_size RecvBufferSizeOption(kUDPRecvBufferSizeBytes);
        UDPSocket->set_option(RecvBufferSizeOption);
        asio::socket_base::reuse_address ReuseAddressOption(true);
        UDPSocket->set_option(ReuseAddressOption);

        DontFragment(UDPSocket, true);
        IgnoreUnreachable(UDPSocket, true);

        PostNextRecvFrom();

        SendingHandshakes = true;
    });
    Router.Set<S2CTimeSyncT>(S2CTimeSyncID, [this](u16 bestC2Sdelta)
    {
        if (SendingHandshakes)
        {
            SendingHandshakes = false;
            IsFullConnection = true;
            Interface->OnConnect(this);
        }

        const u64 nowMsec = GetTimeMsec();
        const u64 bestS2Cdelta = WinTimes.ComputeDelta(nowMsec);
        ServerTimeDeltaMsec = (bestC2Sdelta - (u16)bestS2Cdelta) >> 1;

        LOG(INFO) << "Got time sync: bestC2Sdelta=" << bestC2Sdelta << ", delta=" << ServerTimeDeltaMsec;
    });
    Router.Set<S2CHeartbeatT>(S2CHeartbeatID, [this]()
    {
        LOG(INFO) << "Got heartbeat";

        // no-op
    });
}

SphynxClient::~SphynxClient()
{
}

void SphynxClient::OnTimerTick()
{
    const u64 nowMsec = GetTimeMsec();

    if (SendingHandshakes)
    {
        if (nowMsec - LastHandshakeAttemptMsec >= kClientHandshakeIntervalMsec)
        {
            RPCHandshakeUDP(ConnectionCookie);
        }
    }
    else
    {
        if (nowMsec - LastReceiveLocalMsec > kS2CTimeoutMsec && LastReceiveLocalMsec != 0)
        {
            LOG(WARNING) << "Server timeout: Disconnecting";

            Disconnect();
        }

        if (!IsDisconnected() && IsFullConnection)
            Interface->OnTick(this, nowMsec);

        if (IsDisconnected())
        {
            LOG(WARNING) << "Server is disconnected: Stopping now";

            if (IsFullConnection)
                Interface->OnDisconnect(this);
            else
                Interface->OnConnectFail(this);

            Stop();
            return;
        }

        if (IsFullConnection &&
            (nowMsec - LastUDPTimeSyncMsec > static_cast<u64>(S2CUDPTimeSyncIntervalMsec)))
        {
            LastUDPTimeSyncMsec = nowMsec;
            LOG(DBUG) << "Sending UDP heartbeat " << nowMsec;

            RPCHeartbeatUDP(ToServerTime15(nowMsec));

            if (FastCount <= kS2CUDPTimeSyncFastCount)
            {
                if (FastCount == kS2CUDPTimeSyncFastCount)
                    S2CUDPTimeSyncIntervalMsec = kS2CUDPTimeSyncIntervalSlowMsec;
                ++FastCount;
            }
        }

        if (nowMsec - LastTCPHeartbeatMsec > static_cast<u64>(kS2CTCPHeartbeatIntervalMsec))
        {
            LastTCPHeartbeatMsec = nowMsec;
            LOG(DBUG) << "Sending TCP heartbeat " << nowMsec;

            RPCHeartbeatTCP(ToServerTime15(nowMsec));
        }
    }

    Flush();

    PostNextTimer();
}

void SphynxClient::OnTimerError(const asio::error_code& error)
{
    LOG(WARNING) << "Client timer thread: Tick error " << error.message();
}

void SphynxClient::PostNextTimer()
{
    Timer->expires_after(std::chrono::milliseconds(kClientWorkerTimerIntervalMsec));
    Timer->async_wait([this](const asio::error_code& error)
    {
        if (!!error)
            OnTimerError(error);
        else
            OnTimerTick();
    });
}

void SphynxClient::Loop()
{
    SetThreadName("ClientWorker");

    auto resolver = std::make_shared<asio::ip::tcp::resolver>(*Context);
    resolver->async_resolve(Settings->Host, std::to_string(Settings->TCPPort), asio::ip::tcp::resolver::flags::numeric_service,
        [this](const asio::error_code& error, const asio::ip::tcp::resolver::results_type& results)
    {
        if (!!error)
            OnResolveError(error);
        else if (results.empty())
            Interface->OnConnectFail(this);
        else
        {
            ServerAddrs.clear();
            ServerAddrs.reserve(results.size());
            for (auto& result : results)
                ServerAddrs.emplace_back(result.endpoint());

            ConnectAddrIndex = static_cast<int>(GetTimeUsec() % ServerAddrs.size());
            ConnectAddrEnd = ConnectAddrIndex;

            PostNextConnect();
        }
    });

    LOG(INFO) << "Client thread: Entering loop";

    while (!Terminated)
        Context->run();

    LOG(INFO) << "Client thread: Exiting loop";
}

void SphynxClient::OnUDPClose()
{
	LOG(WARNING) << "UDP: Closed";
}

void SphynxClient::OnUDPError(const asio::error_code& error)
{
    LOG(WARNING) << "UDP: Socket error: " << error.message();
}

void SphynxClient::PostNextRecvFrom()
{
    memset(&UDPReceiveBuffer[0], 0xfe, UDPReceiveBuffer.size());

    UDPSocket->async_receive_from(asio::buffer(UDPReceiveBuffer), FromEndpoint, [this](
		const asio::error_code& error, std::size_t bytes_transferred)
	{
		const u64 nowMsec = GetTimeMsec();

		if (!!error)
			OnUDPError(error);
        else if (bytes_transferred <= 0)
            OnUDPClose();
        else if (FromEndpoint == PeerUDPAddress)
		{
			Stream stream;
            stream.WrapRead(&UDPReceiveBuffer[0], bytes_transferred);

			//LOG(DBUG) << "UDP: Got data len=" << stream.GetRemaining();

			OnUDPData(nowMsec, stream);

			PostNextRecvFrom();
		}
	});
}

void SphynxClient::Start(const std::shared_ptr<ClientSettings>& settings)
{
    Settings = settings;
    Interface = Settings->Interface;

    LOG(INFO) << "Starting client for host=" << Settings->Host << " : " << Settings->TCPPort;

    IsFullConnection = false;
    Disconnected = false;

    Cipher.InitializeEncryption(0, EncryptionRole::Client);

    Context = std::make_shared<asio::io_context>();
    Context->restart();

    SphynxPeer::Start(Context);
    TCPSocket->open(asio::ip::tcp::v4());

    // Set socket options
    asio::socket_base::send_buffer_size SendBufferSizeOption(kTCPSendBufferSizeBytes);
    TCPSocket->set_option(SendBufferSizeOption);
    asio::socket_base::receive_buffer_size RecvBufferSizeOption(kTCPRecvBufferSizeBytes);
    TCPSocket->set_option(RecvBufferSizeOption);
    asio::socket_base::linger LingerOption(false, 0);
    TCPSocket->set_option(LingerOption);

    // Set TCP options
    asio::ip::tcp::no_delay NoDelayOption(true);
    TCPSocket->set_option(NoDelayOption);

    Terminated = false;
    Thread = std::make_unique<std::thread>(&SphynxClient::Loop, this);
}

void SphynxClient::OnResolveError(const asio::error_code& error)
{
    LOG(WARNING) << "Resolve error: " << error.message();
    Interface->OnConnectFail(this);
}

void SphynxClient::PostNextConnect()
{
    ConnectAddrIndex++;
    if (ConnectAddrIndex >= static_cast<int>(ServerAddrs.size()))
        ConnectAddrIndex = 0;

    const asio::ip::tcp::endpoint& addr = ServerAddrs[ConnectAddrIndex];

    LOG(INFO) << "Attempting connection to " << addr.address().to_string() << " : " << addr.port();

    TCPSocket->async_connect(addr, [this, addr](const asio::error_code& error)
    {
        if (!!error)
        {
            if (ConnectAddrIndex == ConnectAddrEnd)
            {
                LOG(INFO) << "All connection attempts failed";
                Interface->OnConnectFail(this);
            }
            else
                PostNextConnect();
        }
        else
        {
            ServerTCPAddr = addr;
            OnConnect();
        }
    });
}

void SphynxClient::OnConnect()
{
    LOG(INFO) << "Connection success";

    Timer = std::make_unique<asio::steady_timer>(*Context);
    PostNextTimer();

    PostNextTCPRead();
}

void SphynxClient::Stop()
{
    LOG(DBUG) << "Stopping client";

    Terminated = true;

    SphynxPeer::Stop();

    if (Timer)
        Timer->cancel();
    if (Thread)
        Thread->join();
    Thread = nullptr;
    Context = nullptr;
    Timer = nullptr;
}
