#include "SphynxClient.h"
#include <memory>
#include "AABBCollisions.h"

struct MyClient : ClientInterface
{
    int Id = 0;

	MyClient()
    {
    }
    virtual ~MyClient() {}

    void OnConnectFail(SphynxClient* client) override
    {
        LOG(INFO) << Id << ": Failed to connect";
    }
	void OnConnect(SphynxClient* client) override
	{
		LOG(INFO) << Id << ": Connect";
	}

	void OnTick(SphynxClient* client, u64 nowMsec) override
	{
		//LOG(INFO) << Id << ": Tick " << nowMsec;
	}

	void OnDisconnect(SphynxClient* client) override
	{
		LOG(INFO) << Id << ": Disconnect";
	}
};


struct CustomSink
{
    void log(g3::LogMessageMover message)
    {
        std::string str = message.get().toString();
        std::cout << str;
        ::OutputDebugStringA(str.c_str());
    }
};

int main()
{
    std::unique_ptr<g3::LogWorker> logworker = g3::LogWorker::createLogWorker();
    //logworker->addDefaultLogger("client", "");
    auto sinkHandle = logworker->addSink(std::make_unique<CustomSink>(),
        &CustomSink::log);
    g3::initializeLogging(logworker.get());

    SetThreadName("Main");

    LOG(INFO) << "UDPClient starting";

    MyClient myclient;

    auto settings = std::make_shared<ClientSettings>();
    settings->Host = "127.0.0.1";
    settings->TCPPort = 5060;
    settings->Interface = &myclient;

    SphynxClient client;
    client.Start(settings);
    ::Sleep(1000000);
    client.Stop();

    ::Sleep(100);

    return 0;
}
