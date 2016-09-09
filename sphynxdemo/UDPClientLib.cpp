#include "UDPClientLib.h"

static logging::Channel Logger("MyClient");


void MyClient::OnConnectFail(SphynxClient* client)
{
    Logger.Info("Failed to connect");
}

void MyClient::OnConnect(SphynxClient* client)
{
    Client = client;
    Logger.Info("Connected");

    TCPLogin.CallSender = client->TCPCallSender;
    UDPPositionUpdate.CallSender = client->UDPCallSender;

    client->Router.Set<S2CSetPlayerIdT>(S2CSetPlayerIdID, [this](playerid_t pid)
    {
        Logger.Info("Set my player id = ", (int)pid);

        Id = pid;
    });
    client->Router.Set<S2CPlayerAddT>(S2CAddPlayerID, [this](playerid_t pid, std::string name)
    {
        bool success = Players.insert(PlayerMap::value_type(pid, name)).second;
        if (!success)
        {
            Logger.Warning("Player ", (int)pid, " added twice!");
        }
        else
        {
            Logger.Info("Player ", (int)pid, " joined: ", name);
        }
    });
    client->Router.Set<S2CPlayerRemoveT>(S2CRemovePlayerID, [this](playerid_t pid)
    {
        auto iter = Players.find(pid);
        bool found = (iter != Players.end());
        if (found)
        {
            Logger.Info("Player ", (int)pid, " quit: ", iter->second.Name);

            Players.erase(iter);
        }
        else
        {
            Logger.Warning("Player ", (int)pid, " removed twice!");
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

            Logger.Info("Player '", player.Name, "'(", (int)pid, ") got position update with one-way-delay=", delayMsec);
        }
        else
        {
            Logger.Warning("Player ", (int)pid, " was not found to update position!");
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
    Logger.Info("Disconnect");
}


//-----------------------------------------------------------------------------
// Singletons

MyClient* m_myclient = nullptr;
SphynxClient* m_client = nullptr;

void StartSphynxClient()
{
    SetThreadName("Main");

    Logger.Info("UDPClient starting");

    m_myclient = new MyClient;
    m_client = new SphynxClient;

    auto settings = std::make_shared<ClientSettings>();
    //settings->Host = "slycog.com";
    settings->Host = "127.0.0.1";
    settings->TCPPort = 5060;
    settings->Interface = m_myclient;

    m_client->Start(settings);
}

void StopSphynxClient()
{
    if (!m_client)
        return;

    m_client->Stop();

    delete m_client;
    delete m_myclient;
}
