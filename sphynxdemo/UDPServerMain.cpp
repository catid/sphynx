#include "SphynxServer.h"
#include "DemoProtocol.h"
#include <iostream>

// FIXME: This needs to support multiple arenas of up to 256 players since we have
// a limited number of Ids to give out.


/*
    Player Position Rebroadcasting:

    On a timer we send each player's latest position to all of the nearby neighbors.
    To determine if a player is a nearby neighbor, we draw a conceptual square centered
    at the (x, y) coordinates of each player.  The distance from the center to the
    midpoint of any side of this "AABB" square is kBroadcastDistance.

    For example if PlayerA is at (0, 0) and PlayerB is at (99, 50),
    then they are neighbors and will receive eachothers' updates when
    the broadcast distance is 100.  But if PlayerB is at (101, 50),
    then they are too far away to broadcast.
*/
static const int kBroadcastDistance = 100;

// Number of players to broadcast at most for each tick.
// Note: Each tick it will round-robin through the set of neighbors to give each
// player a fair shot at rebroadcasting in noisy situations.
static const int kBroadcastPlayerLimit = 15;

// Do not rebroadcast data older than 2 seconds
static const int kBroadcastTimeLimitMsec = 2000; // 2 seconds


//-----------------------------------------------------------------------------
// PidAssigner
//
// Tool to keep track of available pids
class PidAssigner
{
public:
    static_assert(sizeof(playerid_t) == sizeof(u8), "PidAssigner would need to be rewritten");

    PidAssigner()
    {
        for (int i = 0; i < 256; ++i)
        {
            Pids[i] = (u8)i;
        }
    }

    bool Acquire(u8& pid)
    {
        Locker locker(PidLock);
        if (PidLine <= 0)
            return false;
        pid = Pids[--PidLine];
        return true;
    }
    void Release(u8 pid)
    {
        Locker locker(PidLock);
        Pids[PidLine++] = pid;
    }

protected:
    Lock PidLock;
    u8 Pids[256];
    int PidLine = 256;
};


//-----------------------------------------------------------------------------
// MyConnection

struct PlayerPositionData
{
    bool HasPosition = false;
    PlayerPosition Position;
    u16 PositionTimestamp15 = 0;
    u64 PositionMsec = 0;

    bool inline ShouldBroadcast(u64 nowMsec)
    {
        return HasPosition && (int)((s64)nowMsec - (s64)PositionMsec) < kBroadcastTimeLimitMsec;
    }
};

struct MyServer;

struct MyConnection : ConnectionInterface
{
    MyServer* Server = nullptr;

    // Player info
    playerid_t Id = 0;

    mutable Lock PlayerDataLock;
    std::string Name;
    PlayerPositionData PositionData;

    std::string GetName() const
    {
        Locker locker(PlayerDataLock);
        return Name;
    }

    PlayerPositionData GetPosition() const
    {
        Locker locker(PlayerDataLock);
        return PositionData;
    }

    // Persistent data local to OnTick():

    // Next neighbor array index to start broadcasting from
    int LastBroadcastIndex = 0;

    NeighborInfo<MyConnection> Neighbor;

    MyConnection(MyServer* server)
    {
        Server = server;
    }
    virtual ~MyConnection() {}

    void OnConnect(Connection* connection) override;
    void OnTick(Connection* connection, u64 nowMsec) override;
    void OnDisconnect(Connection* connection) override;

    CallSerializer<S2CSetPlayerIdID, S2CSetPlayerIdT> TCPSetPlayerId;
    CallSerializer<S2CAddPlayerID, S2CPlayerAddT> TCPAddPlayer;
    CallSerializer<S2CRemovePlayerID, S2CPlayerRemoveT> TCPRemovePlayer;
    CallSerializer<S2CPositionUpdateID, S2CPlayerUpdatePositionT> UDPPositionUpdate;
};


//-----------------------------------------------------------------------------
// MyServer

typedef std::list<MyConnection*> ConnectionListT;

struct MyServer : ServerInterface
{
    NeighborTracker<MyConnection> BroadcastTracker;

    PidAssigner Pids;

    mutable RWLock ConnectionsLock;
    ConnectionListT Connections;

    void InsertConnection(MyConnection* connection)
    {
        WriteLocker heavyLocker(ConnectionsLock);
        Connections.push_back(connection);
    }
    void RemoveConnection(MyConnection* connection)
    {
        WriteLocker heavyLocker(ConnectionsLock);
        Connections.remove(connection);
    }

    // Locker returned should be held until done using connection objects
    const ConnectionListT& GetConnections(ReadLocker& locker) const
    {
        locker.Set(ConnectionsLock);
        return Connections;
    }

    // Broadcast a message to all connections
    template<typename T, typename... Args>
    void Broadcast(MyConnection* excluded, T MyConnection::*pFunction, Args... args)
    {
        ReadLocker locker;
        const auto& connections = GetConnections(locker);

        for (auto& connection : connections)
        {
            if (connection != excluded)
            {
                LOG(DEBUG) << "Broadcasting from " << (int)excluded->Id << " to " << (int)connection->Id;

                (connection->*pFunction)(args...);
            }
        }
    }

    MyServer() {}
    virtual ~MyServer() {}

    ConnectionInterface* CreateConnection(Connection* connection) override;
    void DestroyConnection(ConnectionInterface* iface, Connection* connection) override;

    void OnDisconnect(playerid_t pid, MyConnection* connection);
};


//-----------------------------------------------------------------------------
// MyServer

ConnectionInterface* MyServer::CreateConnection(Connection* connection)
{
    return new MyConnection(this);
}

void MyServer::DestroyConnection(ConnectionInterface* iface, Connection* connection)
{
    delete iface;
}

void MyServer::OnDisconnect(playerid_t pid, MyConnection* connection)
{
    BroadcastTracker.Remove(connection);

    RemoveConnection(connection);

    Broadcast(connection, &MyConnection::TCPRemovePlayer,
        pid);

    Pids.Release(pid);
}


//-----------------------------------------------------------------------------
// MyConnection

void MyConnection::OnConnect(Connection* connection)
{
    if (!Server->Pids.Acquire(Id))
    {
        LOG(WARNING) << "FIXME: Too many players not handled";
    }

    LOG(INFO) << (int)Id << ": Connect";

    TCPSetPlayerId.CallSender = connection->TCPCallSender;
    TCPAddPlayer.CallSender = connection->TCPCallSender;
    TCPRemovePlayer.CallSender = connection->TCPCallSender;
    UDPPositionUpdate.CallSender = connection->UDPCallSender;

    connection->Router.Set<C2SLoginT>(C2SLoginID, [connection, this](std::string name)
    {
        LOG(INFO) << (int)Id << ": User login '" << name << "'";

        {
            Locker locker(PlayerDataLock);
            Name = name;
        }

        this->Server->InsertConnection(this);

        this->Server->Broadcast(this, &MyConnection::TCPAddPlayer,
            Id, name);

        // Send them the whole player list
        {
            ReadLocker locker;
            const auto& connections = this->Server->GetConnections(locker);

            for (auto& connection : connections)
                TCPAddPlayer(connection->Id, connection->GetName());
        }

        connection->Router.Set<C2SPositionUpdateT>(C2SPositionUpdateID, [this](u16 timestamp, PlayerPosition position)
        {
            Locker locker(PlayerDataLock);
            if (!PositionData.HasPosition)
            {
                LOG(INFO) << (int)Id << ": Received player position for the first time";
                PositionData.HasPosition = true;
            }

            const u64 nowMsec = GetTimeMsec();
            const u64 localSentTimeMsec = ReconstructMsec(nowMsec, timestamp);
            int delayMsec = (int)((s64)nowMsec - (s64)localSentTimeMsec);
            // This data is used to avoid rebroadcasting data after a given timeout

            PositionData.Position = position;
            PositionData.PositionTimestamp15 = timestamp;
            PositionData.PositionMsec = localSentTimeMsec;

            LOG(INFO) << (int)Id << ": Received player position with one-way-delay=" << delayMsec;

            this->Server->BroadcastTracker.Update(this, position.x, position.y);
        });
    });

    TCPSetPlayerId(Id);
}

void MyConnection::OnTick(Connection* connection, u64 nowMsec)
{
    PlayerPositionData currentPlayerData = GetPosition();

    if (currentPlayerData.ShouldBroadcast(nowMsec))
    {
        ReadLocker locker;
        std::vector<MyConnection*> neighbors;

        Server->BroadcastTracker.GetNeighbors(this, kBroadcastDistance, neighbors, locker);

        const int neighborCount = static_cast<int>(neighbors.size());
        if (neighborCount > 0)
        {
            int broadcastIndex = LastBroadcastIndex; // Start from last index

            for (int neighborUpdateCount = 0; neighborUpdateCount < neighborCount && neighborUpdateCount < kBroadcastPlayerLimit; ++neighborUpdateCount);
            {
                if (++broadcastIndex >= neighborCount)
                    broadcastIndex = 0;

                MyConnection* neighbor = neighbors[broadcastIndex];
                PlayerPositionData neighborData = neighbor->GetPosition();

                if (neighborData.ShouldBroadcast(nowMsec))
                {
                    UDPPositionUpdate(neighbor->Id, neighborData.PositionTimestamp15, neighborData.Position);
                }
            }

            LastBroadcastIndex = broadcastIndex;
        }
    }
}

void MyConnection::OnDisconnect(Connection* connection)
{
    LOG(INFO) << (int)Id << ": Disconnected";

    Server->OnDisconnect(Id, this);
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
    //logworker->addDefaultLogger("server", "");
    auto sinkHandle = logworker->addSink(std::make_unique<CustomSink>(),
        &CustomSink::log);
    g3::initializeLogging(logworker.get());

    SetThreadName("Main");

    LOG(INFO) << "UDPServer starting";

    MyServer myserver;

    auto settings = std::make_shared<ServerSettings>();
    settings->WorkerCount = 0;
    settings->MainTCPPort = 5060;
    settings->StartUDPPort = 5060;
    settings->StopUDPPort = 5061;
    settings->Interface = &myserver;

    Server server;
    server.Start(settings);
    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    server.Stop();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    return 0;
}
