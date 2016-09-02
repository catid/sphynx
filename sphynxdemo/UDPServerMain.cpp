#include "SphynxServer.h"
#include <memory>
#include "AABBCollisions.h"

struct PositionPacket
{
    int16_t x, y;
    int16_t vx, vy;
    uint8_t angle;
    uint8_t distance;

    bool Serialize(Stream& stream)
    {
        stream.Serialize(x);
        stream.Serialize(y);
        stream.Serialize(vx);
        stream.Serialize(vy);
        stream.Serialize(angle);
        stream.Serialize(distance);
        return stream.Good();
    }
};

static int GetId()
{
    static std::atomic_int Id{};
    return ++Id;
}

struct MyConnection : ConnectionInterface
{
    int Id = 0;

    NeighborInfo<MyConnection> Neighbor;

    MyConnection()
    {
        Id = GetId();
    }
    virtual ~MyConnection() {}

    void OnConnect(Connection* connection) override
    {
        LOG(INFO) << Id << ": Connect";
    }
    void OnTick(Connection* connection, u64 nowMsec) override
    {
        //LOG(INFO) << Id << ": Tick " << nowMsec;
    }
    void OnDisconnect(Connection* connection) override
    {
        LOG(INFO) << Id << ": Disconnect";
    }
};

struct MyServer : ServerInterface
{
    NeighborTracker<MyConnection> BroadcastTracker;

    MyServer()
    {
    }
    virtual ~MyServer() {}

    ConnectionInterface* CreateConnection(Connection* connection) override
    {
        return new MyConnection();
    }
    void DestroyConnection(ConnectionInterface* iface, Connection* connection) override
    {
        MyConnection* mine = reinterpret_cast<MyConnection*>(iface);
        delete mine;
    }
};

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
    std::this_thread::sleep_for(std::chrono::seconds(1000));
    server.Stop();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    return 0;
}
