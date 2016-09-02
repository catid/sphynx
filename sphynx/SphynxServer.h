#pragma once

#include "SphynxCommon.h"
#include <list>

struct ServerSettings;
class Connection;
class ServerWorker;
class ServerWorkers;
class UDPServer;
class Server;


//-----------------------------------------------------------------------------
// Interfaces

class ConnectionInterface
{
public:
    ConnectionInterface() {}
    virtual ~ConnectionInterface() {}

    virtual void OnConnect(Connection* connection) = 0;
    virtual void OnTick(Connection* connection, u64 nowMsec) = 0;
    virtual void OnDisconnect(Connection* connection) = 0;
};

class ServerInterface
{
public:
    ServerInterface() {}
    virtual ~ServerInterface() {}

    virtual ConnectionInterface* CreateConnection(Connection* connection) = 0;
    virtual void DestroyConnection(ConnectionInterface* iface, Connection* connection) = 0;
};


//-----------------------------------------------------------------------------
// ServerSettings

struct ServerSettings
{
    // Suggested: 0 = Match CPU core count
    unsigned WorkerCount = 0;

    // Suggested: 5060 (SIP)
    unsigned short MainTCPPort = 5060;

    // Suggested: Use the same port as TCP port to start with
    unsigned short StartUDPPort = 5060;

    // Suggested: Provide 2 UDP ports
    unsigned short StopUDPPort = 5061;

    ServerInterface* Interface = nullptr;
};


//-----------------------------------------------------------------------------
// Connection

class Connection : public SphynxPeer
{
public:
    Connection();
    virtual ~Connection();

protected:
    friend class Server;
    friend class UDPServer;
    friend class ServerWorker;

    void Start(std::shared_ptr<asio::io_context>& context, ConnectionInterface* iface);

    void OnAccept(const std::shared_ptr<asio::ip::udp::socket>& udpSocket, unsigned short port, u32 cookie);
    void OnWorkerStart();

    void OnUDPHandshake(asio::ip::udp::endpoint& from, std::shared_ptr<asio::ip::udp::socket>& udpSocket);

    bool OnTick(u64 nowMsec);

    asio::ip::tcp::endpoint PeerTCPAddress;

    ConnectionInterface* Interface = nullptr;

    // Heartbeat timing
    u64 LastTCPHeartbeatMsec = 0;
    u64 LastUDPTimeSyncMsec = 0;
    int FastCount = 0;
    int S2CUDPTimeSyncIntervalMsec = kS2CUDPTimeSyncIntervalFastMsec;

    unsigned short UDPPort = 0;
    uint32_t ConnectionCookie = 0;

    CallSerializer<S2CTCPHandshakeID, S2CTCPHandshakeT> RPCTCPHandshake;
    CallSerializer<S2CTimeSyncID, S2CTimeSyncT> RPCTimeSyncUDP;
    CallSerializer<S2CHeartbeatID, S2CHeartbeatT> RPCHeartbeatTCP;
};


//-----------------------------------------------------------------------------
// ServerWorker

class ServerWorker
{
public:
    ServerWorker();
    ~ServerWorker();

    void Start(std::shared_ptr<asio::io_context>& context, unsigned threadId,
        std::shared_ptr<ServerSettings>& settings);
    void Stop();

    void AddNewConnection(const std::shared_ptr<Connection>& connection);
    void RemoveConnection(const std::shared_ptr<Connection>& connection);

    int GetConnectionCount() const
    {
        return ConnectionCount;
    }

protected:
    unsigned ThreadId = 0;
    std::shared_ptr<asio::io_context> Context;
    std::unique_ptr<asio::steady_timer> Timer;
    std::unique_ptr<std::thread> Thread;
    std::atomic_bool Terminated;

    Lock NewConnectionsLock;
    std::list<std::shared_ptr<Connection>> NewConnections;

    std::list<std::shared_ptr<Connection>> Connections;
    std::shared_ptr<ServerSettings> Settings;
    std::atomic_int ConnectionCount;

    void Loop();
    void OnTimerTick();
    void OnTimerError(const asio::error_code& error);
    void PostNextTimer();
    void PromoteNewConnections();
};


//-----------------------------------------------------------------------------
// ServerWorkers

class ServerWorkers
{
public:
    ServerWorkers();
    ~ServerWorkers();

    void Start(std::shared_ptr<asio::io_context>& context, std::shared_ptr<ServerSettings>& settings);
    void Stop();

    ServerWorker* FindLaziestWorker();

protected:
    std::shared_ptr<ServerSettings> Settings;
    std::shared_ptr<asio::io_context> Context;
    std::vector<std::shared_ptr<ServerWorker>> Workers;
};


//-----------------------------------------------------------------------------
// UDPServer

uint32_t hash_ip_addr(const asio::ip::udp::endpoint& addr);

namespace std {
    template<> struct hash<asio::ip::udp::endpoint>
    {
        inline std::size_t operator()(const asio::ip::udp::endpoint& addr) const
        {
            return hash_ip_addr(addr);
        }
    };
} // namespace std

class UDPServer
{
public:
    UDPServer();
    ~UDPServer();

    void Start(std::shared_ptr<asio::io_context>& context, unsigned short port, std::shared_ptr<ServerSettings>& settings,
        std::shared_ptr<ServerWorkers>& workers,
        std::shared_ptr<UDPServer>& selfRef);
    void OnAccept(const std::shared_ptr<Connection>& connection, u32 cookie);
    void Stop();

    int GetConnectionCount() const;
    std::shared_ptr<asio::ip::udp::socket> GetUDPSocket() const
    {
        return UDPSocket;
    }
    unsigned short GetPort() const
    {
        return Port;
    }

protected:
    std::shared_ptr<ServerSettings> Settings;
    std::shared_ptr<asio::io_context> Context;
    std::shared_ptr<ServerWorkers> Workers;
    std::shared_ptr<UDPServer> SelfRef;

	// UDP server port
	unsigned short Port;

	// UDP socket
	std::shared_ptr<asio::ip::udp::socket> UDPSocket;
	asio::ip::udp::endpoint FromEndpoint;
    std::array<u8, kUDPDatagramMax> UDPReceiveBuffer;

    typedef std::unordered_map<asio::ip::udp::endpoint, std::shared_ptr<Connection>> AddressMap;

    mutable Lock EstablishedConnectionsMapLock;
    AddressMap EstablishedConnectionsMap;

    typedef std::unordered_map<uint32_t, std::shared_ptr<Connection>> CookieMap;

    mutable Lock PreConnectionsMapLock;
    CookieMap PreConnectionsMap;

    // Router for incoming pre-connection calls
    CallRouter PreConnectionRouter;
    Encryptor PreConnectionCipher;

    void OnUDPClose();
    void OnUDPError(const asio::error_code& error);
    void PostNextRecvFrom();
    void HandlePreConnectData(Stream& stream);

    bool MapInsert(asio::ip::udp::endpoint& addr, const std::shared_ptr<Connection>& conn);
    bool MapRemove(asio::ip::udp::endpoint& addr);
    bool MapFind(asio::ip::udp::endpoint& addr, std::shared_ptr<Connection>& conn);
    void MapClear();

    bool PreMapInsert(u32 cookie, const std::shared_ptr<Connection>& conn);
    bool PreMapRemove(u32 cookie);
    bool PreMapFindRemove(u32 cookie, std::shared_ptr<Connection>& conn);
    void PreMapClear();
};


//-----------------------------------------------------------------------------
// Server

class Server
{
public:
    Server();
    ~Server();

    void Start(std::shared_ptr<ServerSettings>& settings);
    void Stop();

protected:
    std::shared_ptr<ServerSettings> Settings;
    std::shared_ptr<asio::io_context> Context;
    std::shared_ptr<asio::ip::tcp::acceptor> TCPAcceptor;
    std::vector<std::shared_ptr<UDPServer>> UDPServers;
    std::shared_ptr<ServerWorkers> Workers;
    Abyssinian KeyGen;

    void OnAccept(const std::shared_ptr<Connection>& connection);
    void OnAcceptError(const asio::error_code& error);
    void PostNewAccept();
    UDPServer* FindLaziestUDPServer();
};
