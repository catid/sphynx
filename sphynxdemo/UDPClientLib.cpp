#include "UDPClientLib.h"

void MyClient::OnConnectFail(SphynxClient* client)
{
    LOG(INFO) << "Failed to connect";
}

void MyClient::OnConnect(SphynxClient* client)
{
    Client = client;
    LOG(INFO) << "Connected";

    TCPLogin.CallSender = client->TCPCallSender;
    UDPPositionUpdate.CallSender = client->UDPCallSender;

    client->Router.Set<S2CSetPlayerIdT>(S2CSetPlayerIdID, [this](playerid_t pid)
    {
        LOG(INFO) << "Set my player id = " << (int)pid;

        Id = pid;
    });
    client->Router.Set<S2CPlayerAddT>(S2CAddPlayerID, [this](playerid_t pid, std::string name)
    {
        bool success = Players.insert(PlayerMap::value_type(pid, name)).second;
        if (!success)
        {
            LOG(WARNING) << "Player " << (int)pid << " added twice!";
        }
        else
        {
            LOG(INFO) << "Player " << (int)pid << " joined: " << name;
        }
    });
    client->Router.Set<S2CPlayerRemoveT>(S2CRemovePlayerID, [this](playerid_t pid)
    {
        auto iter = Players.find(pid);
        bool found = (iter != Players.end());
        if (found)
        {
            LOG(INFO) << "Player " << (int)pid << " quit: " << iter->second.Name;

            Players.erase(iter);
        }
        else
        {
            LOG(WARNING) << "Player " << (int)pid << " removed twice!";
        }
    });
    client->Router.Set<S2CPlayerUpdatePositionT>(S2CPositionUpdateID, [this](playerid_t pid, u16 timestamp, PlayerPosition position)
    {
        auto iter = Players.find(pid);
        bool found = (iter != Players.end());
        if (found)
        {
            auto& player = iter->second;

            const u64 nowMsec = GetTimeMsec();
            const u64 localTimeWhenSentMsec = Client->FromServerTime15(nowMsec, timestamp);
            const int delayMsec = (int)((s64)nowMsec - (s64)localTimeWhenSentMsec);

            player.PositionUpdateTimeMsec = localTimeWhenSentMsec;
            player.Position = position;
            player.OneWayDelay = delayMsec;

            LOG(INFO) << "Player '" << player.Name << "'(" << (int)pid << ") got position update with one-way-delay=" << delayMsec;
        }
        else
        {
            LOG(WARNING) << "Player " << (int)pid << " was not found to update position!";
        }
    });

    TCPLogin(std::string("guest") + std::to_string(GetTimeUsec()));
}

void MyClient::OnTick(SphynxClient* client, u64 nowMsec)
{
    // Periodically send position
    u16 timestamp = client->ToServerTime15(nowMsec);
    PlayerPosition position = GetPosition();

    UDPPositionUpdate(timestamp, position);
}

void MyClient::OnDisconnect(SphynxClient* client)
{
    LOG(INFO) << "Disconnect";
}


//-----------------------------------------------------------------------------
// Singletons

MyClient* m_myclient = nullptr;
SphynxClient* m_client = nullptr;

void StartSphynx()
{
    SetThreadName("Main");

    InitializeLogging();

    LOG(INFO) << "UDPClient starting";

    m_myclient = new MyClient;
    m_client = new SphynxClient;

    auto settings = std::make_shared<ClientSettings>();
    settings->Host = "slycog.com";
    settings->TCPPort = 5060;
    settings->Interface = m_myclient;

    m_client->Start(settings);
}

void StopSphynx()
{
    if (!m_client)
        return;

    // FIXME: hacky sleeps
    m_client->Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    delete m_client;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    delete m_myclient;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
