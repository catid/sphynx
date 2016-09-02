#include "UDPClientLib.h"

int main()
{
    StartSphynxClient();
    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    StopSphynxClient();

    return 0;
}
