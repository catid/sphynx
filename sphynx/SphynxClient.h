#pragma once

#include "SphynxCommon.h"

class SphynxClient;


//-----------------------------------------------------------------------------
// Interfaces

class ClientInterface
{
public:
    ClientInterface() {}
    virtual ~ClientInterface() {}

    virtual void OnConnectFail(SphynxClient* client) = 0;
    virtual void OnConnect(SphynxClient* client) = 0;
    virtual void OnTick(SphynxClient* client, u64 nowMsec) = 0;
    virtual void OnDisconnect(SphynxClient* client) = 0;
};


//-----------------------------------------------------------------------------
// ClientSettings

struct ClientSettings
{
    // Remote host name
    std::string Host;

    // Remote TCP port
    unsigned short TCPPort = 0;

    // Client interface
    ClientInterface* Interface = nullptr;
};


//-----------------------------------------------------------------------------
// SphynxClient

class SphynxClient : public SphynxPeer
{
public:
    SphynxClient();
    virtual ~SphynxClient();

    void Start(const std::shared_ptr<ClientSettings>& settings);
    void Stop();

    // Returns 15-bit server time field to send in a packet
    u16 ToServerTime15(u64 localMsec)
    {
        return (u16)((localMsec + ServerTimeDeltaMsec) & 0x7fff);
    }

    // Returns local time given server time from packet (masks high bit out for you)
    u64 FromServerTime15(u64 nowMsec, u16 fifteen)
    {
        return ReconstructMsec(nowMsec, (u16)((fifteen - ServerTimeDeltaMsec) & 0x7fff));
    }

protected:
    // Client settings
    std::shared_ptr<ClientSettings> Settings;

	// Timer ticking at server or client rate
	std::unique_ptr<asio::steady_timer> Timer;

	// Thread hosting the timer
	std::unique_ptr<std::thread> Thread;
	std::atomic_bool Terminated;

	// Connection cookie
	uint32_t ConnectionCookie = 0;
    std::atomic_bool SendingHandshakes;
    uint32_t LastHandshakeAttemptMsec = 0;

	// Client interface for callbacks
	ClientInterface* Interface = nullptr;

	// UDP socket
    asio::ip::udp::endpoint FromEndpoint;
    std::array<u8, kUDPDatagramMax> UDPReceiveBuffer;

    // Resolved server addresses
    std::vector<asio::ip::tcp::endpoint> ServerAddrs;
    int ConnectAddrIndex = 0;
    int ConnectAddrEnd = 0;
    asio::ip::tcp::endpoint ServerTCPAddr;

    // Server time delta. Note: Low 15 bits are valid
    std::atomic<u16> ServerTimeDeltaMsec; // in msec

    // Heartbeat timing
    u64 LastTCPHeartbeatMsec = 0;
    u64 LastUDPTimeSyncMsec = 0;
    int FastCount = 0;
    int S2CUDPTimeSyncIntervalMsec = kS2CUDPTimeSyncIntervalFastMsec;

    void OnResolveError(const asio::error_code& error);
    void PostNextConnect();
    void OnConnect();

	void PostNextTimer();
	void Loop();
	void OnTimerTick();
	void OnTimerError(const asio::error_code& error);

	void OnUDPClose();
	void OnUDPError(const asio::error_code& error);
	void PostNextRecvFrom();

    CallSerializer<C2SHeartbeatID, C2SHeartbeatT> RPCHeartbeatTCP;
    CallSerializer<C2SHeartbeatID, C2SHeartbeatT> RPCHeartbeatUDP;
    CallSerializer<C2SUDPHandshakeID, C2SUDPHandshakeT> RPCHandshakeUDP;
};
