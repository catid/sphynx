#include "Tools.h"

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h> // gettimeofday
#endif

#ifdef _WIN32
const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
    DWORD dwType; // Must be 0x1000.
    LPCSTR szName; // Pointer to name (in user addr space).
    DWORD dwThreadID; // Thread ID (-1=caller thread).
    DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)
void SetThreadName(const char* threadName)
{
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = threadName;
    info.dwThreadID = ::GetCurrentThreadId();
    info.dwFlags = 0;
#pragma warning(push)
#pragma warning(disable: 6320 6322)
    __try
    {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
#pragma warning(pop)
}
#else
void SetThreadName(const char* threadName)
{
    pthread_setname_np(pthread_self(), threadName);
}
#endif

bool SetCurrentThreadPriority(ThreadPriority prio)
{
#ifdef _WIN32
    int winPrio = THREAD_PRIORITY_NORMAL;
    switch (prio)
    {
    case ThreadPriority::High: winPrio = THREAD_PRIORITY_ABOVE_NORMAL; break;
    case ThreadPriority::Low: winPrio = THREAD_PRIORITY_BELOW_NORMAL; break;
    case ThreadPriority::Idle: winPrio = THREAD_PRIORITY_IDLE; break;
    default: break;
    }
    return 0 != ::SetThreadPriority(::GetCurrentThread(), winPrio);
#else
    int niceness = 0;
    switch (prio)
    {
    case ThreadPriority::High: niceness = 2; break;
    case ThreadPriority::Low: niceness = -2; break;
    case ThreadPriority::Idle: niceness = 19; break;
    default: break;
    }
    return -1 != nice(niceness);
#endif
}

bool SetCurrentThreadAffinity(unsigned processorIndex)
{
#ifdef _WIN32
    return 0 != ::SetThreadAffinityMask(
        ::GetCurrentThread(), (DWORD_PTR)1 << (processorIndex & 63));
#elif !defined(ANDROID)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(processorIndex, &cpuset);
    return 0 == pthread_setaffinity_np(pthread_self(),
        sizeof(cpu_set_t), &cpuset);
#else
    return true; // FIXME: Unused on Android anyway
#endif
}

//// Time

u64 GetTimeUsec()
{
#ifdef _WIN32
    LARGE_INTEGER timeStamp = {};
    if (!::QueryPerformanceCounter(&timeStamp))
        return 0;
    static double PerfFrequencyInverse = 0.;
    if (PerfFrequencyInverse == 0.)
    {
        LARGE_INTEGER freq = {};
        if (!::QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
            return 0;
        PerfFrequencyInverse = 1000000. / (double)freq.QuadPart;
    }
    return (u64)(PerfFrequencyInverse * timeStamp.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return 1000000 * tv.tv_sec + tv.tv_usec;
#endif // _WIN32
}

u64 GetTimeMsec()
{
#ifdef _WIN32
    LARGE_INTEGER timeStamp = {};
    if (!::QueryPerformanceCounter(&timeStamp))
        return 0;
    static double PerfFrequencyInverse = 0.;
    if (PerfFrequencyInverse == 0.)
    {
        LARGE_INTEGER freq = {};
        if (!::QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
            return 0;
        PerfFrequencyInverse = 1000. / (double)freq.QuadPart;
    }
    return (u64)(PerfFrequencyInverse * timeStamp.QuadPart);
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return 1000 * tv.tv_sec + tv.tv_usec / 1000;
#endif // _WIN32
}

u64 GetSloppyMsec()
{
#ifdef _WIN32
    return ::GetTickCount64();
#else
    return GetTimeMsec();
#endif // _WIN32
}
