#pragma once

#include "SphynxClient.h"
#include "DemoProtocol.h"


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


//-----------------------------------------------------------------------------
// Singletons

void StartSphynxClient();
void StopSphynxClient();
