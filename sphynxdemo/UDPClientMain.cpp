#include "UDPClientLib.h"

int main()
{
    SetThreadName("Main");

    InitializeLogging();

    LOG(INFO) << "UDPClient starting";

    StartSphynx();
    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    StopSphynx();

    return 0;
}
