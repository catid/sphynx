#include "SphynxClient.h"
#include "DemoProtocol.h"
#include <iostream>


struct RemotePlayerInfo
{
    RemotePlayerInfo(std::string name)
    {
        Name = name;
    }

    // Player name
    std::string Name;

    // Last known position
    PlayerPosition Position;

    // Local time when position was last updated
    u64 PositionUpdateTimeMsec;

    // Latency from remote user to local user
    int OneWayDelay = 0;
};

struct MyClient : ClientInterface
{
    // Network client object
    SphynxClient* Client = nullptr;

    // My id
    playerid_t Id = 0;

    // Player list
    typedef std::unordered_map<playerid_t, RemotePlayerInfo> PlayerMap;
    PlayerMap Players;

    // Current position
    mutable Lock PlayerDataLock;
    PlayerPosition Position;

    PlayerPosition GetPosition() const
    {
        Locker locker(PlayerDataLock);
        return Position;
    }

    MyClient() {}
    virtual ~MyClient() {}

    void OnConnectFail(SphynxClient* client) override;
    void OnConnect(SphynxClient* client) override;
    void OnTick(SphynxClient* client, u64 nowMsec) override;
    void OnDisconnect(SphynxClient* client) override;

    CallSerializer<C2SLoginID, C2SLoginT> TCPLogin;
    CallSerializer<C2SPositionUpdateID, C2SPositionUpdateT> UDPPositionUpdate;
};




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


// Logging
struct CustomSink
{
    void log(g3::LogMessageMover message)
    {
        std::string str = message.get().toString();
        std::cout << str;
#ifdef _WIN32
        ::OutputDebugStringA(str.c_str());
#endif
    }
};

int main()
{
    // Setup logging
    std::unique_ptr<g3::LogWorker> logworker = g3::LogWorker::createLogWorker();
    //logworker->addDefaultLogger("client", "");
    auto sinkHandle = logworker->addSink(std::make_unique<CustomSink>(),
        &CustomSink::log);
    g3::initializeLogging(logworker.get());

    SetThreadName("Main");

    LOG(INFO) << "UDPClient starting";

    MyClient myclient;

    auto settings = std::make_shared<ClientSettings>();
    settings->Host = "slycog.com";
    //settings->Host = "127.0.0.1";
    settings->TCPPort = 5060;
    settings->Interface = &myclient;

    SphynxClient client;
    client.Start(settings);
    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    client.Stop();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    return 0;
}
