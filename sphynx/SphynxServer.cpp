#include "SphynxServer.h"


//-----------------------------------------------------------------------------
// Tools

#ifdef _MSC_VER
#include <nmmintrin.h>
#endif

#ifndef _ANDROID
    #define HAS_CRC32_INTRINSIC /* disable if it doesn't compile */
#endif

ALIGNED_TYPE(asio::ip::address_v4::bytes_type, 16) aligned_v4_t;
ALIGNED_TYPE(asio::ip::address_v6::bytes_type, 16) aligned_v6_t;

uint32_t hash_ip_addr(const asio::ip::udp::endpoint& addr)
{
    uint32_t key;

    if (addr.address().is_v4())
    {
        aligned_v4_t v4bytes = addr.address().to_v4().to_bytes();
        key = *(uint32_t*)&v4bytes[0];
#ifdef HAS_CRC32_INTRINSIC
        key = _mm_crc32_u32(addr.port(), key);
#else
        key = (key ^ 61) ^ (key >> 16);
        key = key + (key << 3) + addr.port();
        key = key ^ (key >> 4);
        key = key * 0x27d4eb2d;
        key = key ^ (key >> 15);
#endif
    }
    else
    {
        aligned_v6_t v6bytes = addr.address().to_v6().to_bytes();
        uint32_t* keywords = (uint32_t*)&v6bytes[0];
#ifdef HAS_CRC32_INTRINSIC
        key = _mm_crc32_u32(addr.port(), keywords[0]);
        key = _mm_crc32_u32(key, keywords[1]);
        key = _mm_crc32_u32(key, keywords[2]);
        key = _mm_crc32_u32(key, keywords[3]);
#else
        key = keywords[0] + keywords[1] + keywords[2] + keywords[3];
        key = (key ^ 61) ^ (key >> 16);
        key = key + (key << 3) + addr.port();
        key = key ^ (key >> 4);
        key = key * 0x27d4eb2d;
        key = key ^ (key >> 15);
#endif
    }

    return key;
}


//-----------------------------------------------------------------------------
// ServerWorker

ServerWorker::ServerWorker()
{
    Terminated = false;
}

ServerWorker::~ServerWorker()
{
}

void ServerWorker::AddNewConnection(const std::shared_ptr<Connection>& connection)
{
    ConnectionCount++;

    Locker locker(NewConnectionsLock);
    NewConnections.push_back(connection);
}

void ServerWorker::RemoveConnection(const std::shared_ptr<Connection>& connection)
{
    ConnectionCount--;

    Locker locker(NewConnectionsLock);
    NewConnections.remove(connection);
    Connections.remove(connection);
}

void ServerWorker::Start(std::shared_ptr<asio::io_context>& context, unsigned threadId,
    std::shared_ptr<ServerSettings>& settings)
{
    Context = context;
    ThreadId = threadId;
    Settings = settings;

    LOG(DEBUG) << "Thread " << ThreadId << ": Starting";

    Timer = std::make_unique<asio::steady_timer>(*Context);
    PostNextTimer();

    Terminated = false;
    Thread = std::make_unique<std::thread>(&ServerWorker::Loop, this);
}

void ServerWorker::PostNextTimer()
{
    Timer->expires_after(std::chrono::milliseconds(kServerWorkerTimerIntervalMsec));
    Timer->async_wait([this](const asio::error_code& error)
    {
        if (!!error)
            OnTimerError(error);
        else
            OnTimerTick();
    });
}

void ServerWorker::Loop()
{
    SetThreadName("ServerWorker");

    LOG(INFO) << "Thread " << ThreadId << ": Entering loop";

    if (ThreadId < std::thread::hardware_concurrency())
    {
        if (!SetCurrentThreadAffinity(ThreadId))
        {
            LOG(WARNING) << "Thread " << ThreadId << ": Unable to set affinity";
            DEBUG_BREAK;
        }
    }

    while (!Terminated)
        Context->run();

    LOG(INFO) << "Thread " << ThreadId << ": Exiting loop";
}

void ServerWorker::PromoteNewConnections()
{
    std::list<std::shared_ptr<Connection>> newConnections;
    {
        Locker locker(NewConnectionsLock);
        newConnections.splice(newConnections.end(), NewConnections);
    }

    for (auto& connection : newConnections)
        connection->OnWorkerStart();

    Connections.splice(Connections.end(), newConnections);
}

void ServerWorker::OnTimerTick()
{
    u64 nowMsec = GetTimeMsec();

    //LOG(DEBUG) << "Thread " << ThreadId << ": Tick " << nowMsec;

    PromoteNewConnections();

    Connections.remove_if(
        [nowMsec](std::shared_ptr<Connection>& connection)
    {
        return connection->OnTick(nowMsec);
    });

    PostNextTimer();
}

void ServerWorker::OnTimerError(const asio::error_code& error)
{
    LOG(WARNING) << "Thread " << ThreadId << ": Tick error " << error.message();
}

void ServerWorker::Stop()
{
    LOG(DEBUG) << "Thread " << ThreadId << ": Stopping";

    Terminated = true;

    if (Timer)
        Timer->cancel();
    if (Thread)
        Thread->join();
    Thread = nullptr;
    Context = nullptr;
    Timer = nullptr;
}


//-----------------------------------------------------------------------------
// ServerWorkers

ServerWorkers::ServerWorkers()
{
}

ServerWorkers::~ServerWorkers()
{
}

void ServerWorkers::Start(std::shared_ptr<asio::io_context>& context, std::shared_ptr<ServerSettings>& settings)
{
    Settings = settings;

    if (settings->WorkerCount <= 0)
    {
        unsigned num_cpus = std::thread::hardware_concurrency();
        if (num_cpus < 1)
            num_cpus = 1;
        settings->WorkerCount = num_cpus;
    }

    LOG(INFO) << "Starting " << Settings->WorkerCount << " workers";

    Context = context;

    Workers.resize(Settings->WorkerCount);
    unsigned threadId = 0;
    for (auto& worker : Workers)
    {
        worker = std::make_shared<ServerWorker>();
        worker->Start(Context, threadId++, Settings);
    }
}

ServerWorker* ServerWorkers::FindLaziestWorker()
{
    int laziestWorker = 0, laziestWorkerCount = Workers[0]->GetConnectionCount();
    for (size_t kWorkerCount = Workers.size(), i = 1; i < kWorkerCount; ++i)
    {
        int count = Workers[i]->GetConnectionCount();
        if (laziestWorkerCount > count)
        {
            laziestWorker = (int)i, laziestWorkerCount = count;
        }
    }
    return Workers[laziestWorker].get();
}

void ServerWorkers::Stop()
{
    LOG(INFO) << "Stopping " << Settings->WorkerCount << " workers";

    u64 t0 = GetTimeMsec();
    for (auto& worker : Workers)
        worker->Stop();
    Workers.clear();
    u64 t1 = GetTimeMsec();

    LOG(INFO) << "Stopped " << Settings->WorkerCount << " workers in " << (t1 - t0) / 1000 << " msec";

    Context = nullptr;
}


//-----------------------------------------------------------------------------
// Connection

Connection::Connection()
{
    RPCTimeSyncUDP.CallSender = UDPCallSender;
    RPCHeartbeatTCP.CallSender = TCPCallSender;
    RPCTCPHandshake.CallSender = TCPCallSender;

    Router.Set<C2SHeartbeatT>(C2SHeartbeatID, [this](u16 sentTimeMsec)
    {
        u64 nowMsec = GetTimeMsec();
        u64 sentTimeFullMsec = ReconstructMsec(nowMsec, sentTimeMsec);

        LOG(DEBUG) << "Got heartbeat from " << (int)(nowMsec - sentTimeFullMsec);

        // Client is keeping connection alive
    });
}

Connection::~Connection()
{
}

void Connection::Start(std::shared_ptr<asio::io_context>& context, ConnectionInterface* iface)
{
	SphynxPeer::Start(context);

    Interface = iface;
}

void Connection::OnAccept(const std::shared_ptr<asio::ip::udp::socket>& udpSocket, unsigned short port, u32 cookie)
{
    UDPSocket = udpSocket;
    UDPPort = port;
    ConnectionCookie = cookie;
}

void Connection::OnWorkerStart()
{
    Cipher.InitializeEncryption(0, EncryptionRole::Server);

    LOG(INFO) << "Worker starting on connection. Sending TCP handshake";

    RPCTCPHandshake(ConnectionCookie, UDPPort);

    PostNextTCPRead();
}

void Connection::OnUDPHandshake(asio::ip::udp::endpoint& from, std::shared_ptr<asio::ip::udp::socket>& udpSocket)
{
    PeerUDPAddress = from;
	UDPSocket = udpSocket;

	IsFullConnection = true;

    LOG(INFO) << "Connection got UDP handshake from client: Session established!";

    Interface->OnConnect(this);
}

bool Connection::OnTick(u64 nowMsec)
{
    if (nowMsec - LastReceiveLocalMsec > kS2CTimeoutMsec && LastReceiveLocalMsec != 0)
    {
        LOG(WARNING) << "Client timeout: Disconnecting";

        Disconnect();
    }

    if (!IsDisconnected() && IsFullConnection)
        Interface->OnTick(this, nowMsec);

    if (IsDisconnected())
    {
        LOG(WARNING) << "Client is disconnected: Removing from worker list";

        if (IsFullConnection)
            Interface->OnDisconnect(this);

        // FIXME: Call PreMapRemove() here on UDP server
        // FIXME: Call MapRemove() here on UDP server

        return true; // Remove from list
    }

    if (IsFullConnection && nowMsec - LastUDPTimeSyncMsec > static_cast<u64>(S2CUDPTimeSyncIntervalMsec))
    {
        LastUDPTimeSyncMsec = nowMsec;
        LOG(DEBUG) << "Sending UDP timesync " << nowMsec;

        u16 bestDelta = static_cast<u16>(WinTimes.ComputeDelta(nowMsec));
        RPCTimeSyncUDP(bestDelta);

        if (FastCount <= kS2CUDPTimeSyncFastCount)
        {
            if (FastCount == kS2CUDPTimeSyncFastCount)
                S2CUDPTimeSyncIntervalMsec = kS2CUDPTimeSyncIntervalSlowMsec;
            ++FastCount;
        }
    }

    if (nowMsec - LastTCPHeartbeatMsec > kS2CTCPHeartbeatIntervalMsec)
    {
        LastTCPHeartbeatMsec = nowMsec;
        LOG(DEBUG) << "Sending TCP heartbeat " << nowMsec;

        RPCHeartbeatTCP();
    }

    Flush();

    return false; // Do not remove from list
}


//-----------------------------------------------------------------------------
// UDPServer

UDPServer::UDPServer()
{
}

UDPServer::~UDPServer()
{
}

void UDPServer::Start(std::shared_ptr<asio::io_context>& context, unsigned short port,
    std::shared_ptr<ServerSettings>& settings,
    std::shared_ptr<ServerWorkers>& workers,
    std::shared_ptr<UDPServer>& selfRef)
{
    Context = context;
    Port = port;
    Settings = settings;
    Workers = workers;
    SelfRef = selfRef;

    LOG(INFO) << "UDP " << Port << ": Starting server";

    asio::ip::udp::endpoint UDPEndpoint(asio::ip::udp::v4(), port);
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

    PreConnectionCipher.InitializeEncryption(0, EncryptionRole::Server);

    PreConnectionRouter.Set<C2SUDPHandshakeT>(C2SUDPHandshakeID, [this](u32 cookie)
    {
        std::shared_ptr<Connection> connection;
        if (PreMapFindRemove(cookie, connection))
        {
            LOG(INFO) << "Got UDP data from the client";

            connection->OnUDPHandshake(FromEndpoint, UDPSocket);

            MapInsert(FromEndpoint, connection);
        }
    });

    PostNextRecvFrom();
}

void UDPServer::OnAccept(const std::shared_ptr<Connection>& connection, u32 cookie)
{
    PreMapInsert(cookie, connection);
}

void UDPServer::HandlePreConnectData(Stream& rawStream)
{
    u8* data = rawStream.GetFront();
    int dataSize = rawStream.GetBufferSize();
    PreConnectionCipher.DecryptUDP(data, data, dataSize);

    Stream stream;
    stream.WrapRead(data, dataSize);

    u16 partialTime;
    if (!stream.Serialize(partialTime))
        return;

    PreConnectionRouter.Call(stream);
}

void UDPServer::OnUDPClose()
{
    LOG(WARNING) << "UDP " << Port << ": Closed";
}

void UDPServer::OnUDPError(const asio::error_code& error)
{
    LOG(WARNING) << "UDP " << Port << ": Socket error: " << error.message();
}

void UDPServer::PostNextRecvFrom()
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
        else
        {
            Stream stream;
            stream.WrapRead(&UDPReceiveBuffer[0], bytes_transferred);

            //LOG(DEBUG) << "UDP " << Port << ": Got data len=" << stream.GetRemaining();

            std::shared_ptr<Connection> connection;
            if (MapFind(FromEndpoint, connection))
                connection->OnUDPData(nowMsec, stream);
            else
                HandlePreConnectData(stream);

            PostNextRecvFrom();
        }
    });
}

int UDPServer::GetConnectionCount() const
{
    size_t count = 0;
    {
        Locker locker(EstablishedConnectionsMapLock);
        count += EstablishedConnectionsMap.size();
    }
    {
        Locker locker(PreConnectionsMapLock);
        count += PreConnectionsMap.size();
    }
    return static_cast<int>(count);
}

bool UDPServer::MapInsert(asio::ip::udp::endpoint& addr, const std::shared_ptr<Connection>& conn)
{
    Locker locker(EstablishedConnectionsMapLock);

    // Returns true if the value was inserted, false if it existed already
    return EstablishedConnectionsMap.insert(AddressMap::value_type(addr, conn)).second;
}

bool UDPServer::MapRemove(asio::ip::udp::endpoint& addr)
{
    std::shared_ptr<Connection> connection;
    {
        Locker locker(EstablishedConnectionsMapLock);
        auto iter = EstablishedConnectionsMap.find(addr);
        if (iter == EstablishedConnectionsMap.end())
            return false;
        connection = iter->second;
        EstablishedConnectionsMap.erase(iter);
    }
    connection.reset(); // Allow object to go out of scope
    return true;
}

bool UDPServer::MapFind(asio::ip::udp::endpoint& addr, std::shared_ptr<Connection>& conn)
{
    Locker locker(EstablishedConnectionsMapLock);
    auto iter = EstablishedConnectionsMap.find(addr);
    if (iter == EstablishedConnectionsMap.end())
    {
        conn = nullptr;
        return false;
    }
    conn = iter->second;
    return true;
}

void UDPServer::MapClear()
{
    std::vector<std::shared_ptr<Connection>> connections;
    {
        Locker locker(EstablishedConnectionsMapLock);
        connections.reserve(EstablishedConnectionsMap.size());
        for (auto& connection : EstablishedConnectionsMap)
            connections.push_back(connection.second);
        EstablishedConnectionsMap.clear();
    }
    connections.clear(); // Allow objects to go out of scope
}

bool UDPServer::PreMapInsert(u32 cookie, const std::shared_ptr<Connection>& conn)
{
    Locker locker(PreConnectionsMapLock);

    // Returns true if the value was inserted, false if it existed already
    return PreConnectionsMap.insert(CookieMap::value_type(cookie, conn)).second;
}

bool UDPServer::PreMapRemove(u32 cookie)
{
    std::shared_ptr<Connection> connection;
    {
        Locker locker(PreConnectionsMapLock);
        auto iter = PreConnectionsMap.find(cookie);
        if (iter == PreConnectionsMap.end())
            return false;
        connection = iter->second;
        PreConnectionsMap.erase(iter);
    }
    connection.reset(); // Allow object to go out of scope
    return true;
}

bool UDPServer::PreMapFindRemove(u32 cookie, std::shared_ptr<Connection>& conn)
{
    Locker locker(PreConnectionsMapLock);
    auto iter = PreConnectionsMap.find(cookie);
    if (iter == PreConnectionsMap.end())
    {
        conn = nullptr;
        return false;
    }
    conn = iter->second;
    PreConnectionsMap.erase(iter);
    return true;
}

void UDPServer::PreMapClear()
{
    std::vector<std::shared_ptr<Connection>> connections;
    {
        Locker locker(PreConnectionsMapLock);
        connections.reserve(PreConnectionsMap.size());
        for (auto& connection : PreConnectionsMap)
            connections.push_back(connection.second);
        PreConnectionsMap.clear();
    }
    connections.clear(); // Allow objects to go out of scope
}

void UDPServer::Stop()
{
    LOG(DEBUG) << "UDP " << Port << ": Stopping";

    MapClear();
    PreMapClear();

    if (UDPSocket)
        UDPSocket->close();
    Context = nullptr;
    Settings = nullptr;
    UDPSocket = nullptr;
    Workers = nullptr;
    SelfRef = nullptr;
}


//-----------------------------------------------------------------------------
// Server

Server::Server()
{
}

Server::~Server()
{
}

void Server::Start(std::shared_ptr<ServerSettings>& settings)
{
    Settings = settings;

    LOG(INFO) << "Starting server on TCP port " << Settings->MainTCPPort << " and UDP ports " << Settings->StartUDPPort << " - " << Settings->StopUDPPort;

    KeyGen.Initialize((u32)GetTimeUsec());

    Context = std::make_shared<asio::io_context>();
    Context->restart();

    Workers = std::make_shared<ServerWorkers>();
    Workers->Start(Context, Settings);

    const int UDPPortCount = static_cast<int>(Settings->StopUDPPort - Settings->StartUDPPort + 1);
    UDPServers.resize(UDPPortCount);
    unsigned short udpPort = Settings->StartUDPPort;
    for (auto& udp : UDPServers)
    {
        udp = std::make_shared<UDPServer>();
        udp->Start(Context, udpPort, Settings, Workers, udp);
        udpPort++;
    }

    asio::ip::tcp::endpoint TCPEndpoint(asio::ip::tcp::v4(), Settings->MainTCPPort);
    TCPAcceptor = std::make_shared<asio::ip::tcp::acceptor>(*Context, TCPEndpoint);

    // Set socket options
    asio::socket_base::send_buffer_size SendBufferSizeOption(kTCPSendBufferSizeBytes);
    TCPAcceptor->set_option(SendBufferSizeOption);
    asio::socket_base::receive_buffer_size RecvBufferSizeOption(kTCPRecvBufferSizeBytes);
    TCPAcceptor->set_option(RecvBufferSizeOption);
    asio::socket_base::linger LingerOption(false, 0);
    TCPAcceptor->set_option(LingerOption);
    asio::socket_base::reuse_address ReuseAddressOption(true);
    TCPAcceptor->set_option(ReuseAddressOption);

    // Set TCP options
    asio::ip::tcp::no_delay NoDelayOption(true);
    TCPAcceptor->set_option(NoDelayOption);

    PostNewAccept();
}

void Server::OnAccept(const std::shared_ptr<Connection>& connection)
{
    auto& addr = connection->PeerTCPAddress;
    LOG(INFO) << "Accepted a TCP connection from " << addr.address().to_string() << " : " << addr.port();

    const u32 cookie = KeyGen.Next();

    UDPServer* udp = FindLaziestUDPServer();
    connection->OnAccept(udp->GetUDPSocket(), udp->GetPort(), cookie);

    udp->OnAccept(connection, cookie);

    Workers->FindLaziestWorker()->AddNewConnection(connection);

    PostNewAccept();
}

UDPServer* Server::FindLaziestUDPServer()
{
    UDPServer* best = UDPServers[0].get();
    int lowestCount = best->GetConnectionCount();

    const int serverCount = static_cast<int>(UDPServers.size());
    for (int i = 1; i < serverCount; ++i)
    {
        UDPServer* server = UDPServers[i].get();
        int count = server->GetConnectionCount();
        if (count < lowestCount)
            best = server;
    }

    return best;
}

void Server::OnAcceptError(const asio::error_code& error)
{
    LOG(WARNING) << "TCP acceptor socket error: ", error.message();
}

void Server::PostNewAccept()
{
    auto connection = std::make_shared<Connection>();
    ConnectionInterface* iface = Settings->Interface->CreateConnection(connection.get());
    connection->Start(Context, iface);

    TCPAcceptor->async_accept(*connection->TCPSocket, connection->PeerTCPAddress, [this, connection](const asio::error_code& error)
    {
        if (!!error)
            OnAcceptError(error);
        else
            OnAccept(connection);
    });
}

void Server::Stop()
{
    LOG(INFO) << "Stopping server";

    if (Context)
        Context->stop();

    if (Workers)
        Workers->Stop();

    for (auto& udp : UDPServers)
        udp->Stop();
    UDPServers.clear();

    if (TCPAcceptor)
        TCPAcceptor->cancel();

    Workers = nullptr;
    TCPAcceptor = nullptr;
    Context = nullptr;
    Settings = nullptr;
}
