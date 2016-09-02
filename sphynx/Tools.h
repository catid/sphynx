#pragma once

#include <thread>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN

    #ifndef _WINSOCKAPI_
        #define DID_DEFINE_WINSOCKAPI
        #define _WINSOCKAPI_
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601 /* Windows 7+ */
    #endif

    #include <windows.h>
#else
    #include <pthread.h>
#endif

#ifdef DID_DEFINE_WINSOCKAPI
    #undef _WINSOCKAPI_
    #undef DID_DEFINE_WINSOCKAPI
#endif

#ifdef _MSC_VER
    #define FORCE_INLINE __forceinline
#else
    #define FORCE_INLINE inline
#endif

#include <stdint.h>

typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint8_t u8;
typedef int8_t s8;

#ifdef _WIN32
    #include <intrin.h>
#endif

#ifdef _WIN32
    #define DEBUG_BREAK __debugbreak()
#else
    #define DEBUG_BREAK __builtin_trap()
#endif
#define DEBUG_ASSERT(cond) do { if (!(cond)) { DEBUG_BREAK; } } while(false);

#ifdef _WIN32

class Lock
{
public:
    Lock() { ::InitializeCriticalSectionAndSpinCount(&cs, 1000); }
    ~Lock() { ::DeleteCriticalSection(&cs); }
    bool TryEnter() { return ::TryEnterCriticalSection(&cs) != FALSE; }
    void Enter() { ::EnterCriticalSection(&cs); }
    void Leave() { ::LeaveCriticalSection(&cs); }
private:
    CRITICAL_SECTION cs;
};

#else

#include <mutex>

class Lock
{
public:
    Lock() {}
    ~Lock() {}
    bool TryEnter() { return cs.try_lock(); }
    void Enter() { cs.lock(); }
    void Leave() { cs.unlock(); }
private:
    std::recursive_mutex cs;
};

#endif

class Locker
{
public:
    Locker(Lock& lock) {
        TheLock = &lock;
        if (TheLock)
            TheLock->Enter();
    }
    ~Locker() { Clear(); }
    bool TrySet(Lock& lock) {
        Clear();
        if (!lock.TryEnter())
            return false;
        TheLock = &lock;
        return true;
    }
    void Set(Lock& lock) {
        Clear();
        lock.Enter();
        TheLock = &lock;
    }
    void Clear() {
        if (TheLock)
            TheLock->Leave();
        TheLock = nullptr;
    }
private:
    Lock* TheLock;
};

u64 GetTimeUsec();
u64 GetTimeMsec();
u64 GetSloppyMsec();

// When counters are in milliseconds, this is 32 seconds ahead and behind
FORCE_INLINE u64 ReconstructCounter16(u64 center_count, u16 sixteen_bits)
{
    u32 iv_msb = (1 << 16);
    u64 iv_mask = (iv_msb - 1);
    s16 diff = sixteen_bits - (u16)center_count;
    return ((center_count & ~iv_mask) | sixteen_bits) - (((iv_msb >> 1) - (u16)diff) & iv_msb) + (diff & iv_msb);
}

// 8 seconds ahead, 24.768 behind
FORCE_INLINE u64 ReconstructMsec(u64 center_count, u16 fifteen_bits)
{
    DEBUG_ASSERT((fifteen_bits & 0x8000) == 0);
    center_count = center_count - (1 << (15 - 1)) + 8000;
    const u32 IV_MSB = (1 << 15);
    const u32 IV_MASK = (IV_MSB - 1);
    s32 diff = fifteen_bits - (u32)(center_count & IV_MASK);
    return ((center_count & ~(u64)IV_MASK) | fifteen_bits)
        - (((IV_MSB >> 1) - (diff & IV_MASK)) & IV_MSB)
        + (diff & IV_MSB);
}

bool SetCurrentThreadAffinity(unsigned processorIndex);

enum class ThreadPriority
{
    High,
    Normal,
    Low,
    Idle
};
bool SetCurrentThreadPriority(ThreadPriority prio);

void SetThreadName(const char* name);

#define CAT_ROL8(n, r)  ( (u8)((u8)(n) << (r)) | (u8)((u8)(n) >> ( 8 - (r))) ) /* only works for u8 */
#define CAT_ROR8(n, r)  ( (u8)((u8)(n) >> (r)) | (u8)((u8)(n) << ( 8 - (r))) ) /* only works for u8 */
#define CAT_ROL16(n, r) ( (u16)((u16)(n) << (r)) | (u16)((u16)(n) >> (16 - (r))) ) /* only works for u16 */
#define CAT_ROR16(n, r) ( (u16)((u16)(n) >> (r)) | (u16)((u16)(n) << (16 - (r))) ) /* only works for u16 */
#define CAT_ROL32(n, r) ( (u32)((u32)(n) << (r)) | (u32)((u32)(n) >> (32 - (r))) ) /* only works for u32 */
#define CAT_ROR32(n, r) ( (u32)((u32)(n) >> (r)) | (u32)((u32)(n) << (32 - (r))) ) /* only works for u32 */
#define CAT_ROL64(n, r) ( (u64)((u64)(n) << (r)) | (u64)((u64)(n) >> (64 - (r))) ) /* only works for u64 */
#define CAT_ROR64(n, r) ( (u64)((u64)(n) >> (r)) | (u64)((u64)(n) << (64 - (r))) ) /* only works for u64 */

class Abyssinian
{
    u64 _x, _y;

public:
    FORCE_INLINE void Initialize(u32 x, u32 y)
    {
        // Based on the mixing functions of MurmurHash3
        static const u64 C1 = 0xff51afd7ed558ccdULL;
        static const u64 C2 = 0xc4ceb9fe1a85ec53ULL;

        x += y;
        y += x;

        u64 seed_x = 0x9368e53c2f6af274ULL ^ x;
        u64 seed_y = 0x586dcd208f7cd3fdULL ^ y;

        seed_x *= C1;
        seed_x ^= seed_x >> 33;
        seed_x *= C2;
        seed_x ^= seed_x >> 33;

        seed_y *= C1;
        seed_y ^= seed_y >> 33;
        seed_y *= C2;
        seed_y ^= seed_y >> 33;

        _x = seed_x;
        _y = seed_y;

        // Inlined Next(): Discard first output

        _x = (u64)0xfffd21a7 * (u32)_x + (u32)(_x >> 32);
        _y = (u64)0xfffd1361 * (u32)_y + (u32)(_y >> 32);
    }

    FORCE_INLINE void Initialize(u32 seed)
    {
        Initialize(seed, seed);
    }

    FORCE_INLINE u32 Next()
    {
        _x = (u64)0xfffd21a7 * (u32)_x + (u32)(_x >> 32);
        _y = (u64)0xfffd1361 * (u32)_y + (u32)(_y >> 32);
        return CAT_ROL32((u32)_x, 7) + (u32)_y;
    }
};

#if defined(_MSC_VER)
    #define CAT_ALIGNED(x) __declspec(align(x))
#else
#if defined(__GNUC__)
    #define CAT_ALIGNED(x) __attribute__ ((aligned(x)))
#endif
#endif

#define ALIGNED_TYPE(t,x) typedef t CAT_ALIGNED(x)


//-----------------------------------------------------------------------------
// Read/Write Lock

class RWLock
{
public:
    RWLock()
    {
#ifdef _WIN32
        ::InitializeSRWLock(&PrimitiveLock);
#else
        pthread_rwlock_init(&PrimitiveLock, nullptr);
#endif
    }
    ~RWLock()
    {
#ifndef _WIN32
        pthread_rwlock_destroy(&PrimitiveLock);
#endif
    }

protected:
    friend class ReadLocker;
    friend class WriteLocker;
#ifdef _WIN32
    SRWLOCK PrimitiveLock = SRWLOCK_INIT;
#else
    pthread_rwlock_t PrimitiveLock = PTHREAD_RWLOCK_INITIALIZER;
#endif
};

class ReadLocker
{
public:
    ReadLocker() {}
    ReadLocker(RWLock& lock)
    {
        Set(lock);
    }
    ~ReadLocker()
    {
        Clear();
    }
    void Set(RWLock& lock)
    {
        Clear();
        Lock = &lock;
#ifdef _WIN32
        ::AcquireSRWLockShared(&lock.PrimitiveLock);
#else
        pthread_rwlock_rdlock(&lock.PrimitiveLock);
#endif
    }
    bool TrySet(RWLock& lock)
    {
        Clear();
#ifdef _WIN32
        if (::TryAcquireSRWLockShared(&lock.PrimitiveLock) != 0)
#else
        if (pthread_rwlock_tryrdlock(&lock.PrimitiveLock) == 0)
#endif
        {
            Lock = &lock;
            return true;
        }
        return false;
    }
    void Clear()
    {
        if (Lock)
#ifdef _WIN32
            ::ReleaseSRWLockShared(&Lock->PrimitiveLock);
#else
            pthread_rwlock_unlock(&Lock->PrimitiveLock);
#endif
        Lock = nullptr;
    }

protected:
    RWLock* Lock = nullptr;
};

class WriteLocker
{
public:
    WriteLocker() {}
    WriteLocker(RWLock& lock)
    {
        Set(lock);
    }
    ~WriteLocker()
    {
        Clear();
    }
    void Set(RWLock& lock)
    {
        Clear();
        Lock = &lock;
#ifdef _WIN32
        ::AcquireSRWLockExclusive(&lock.PrimitiveLock);
#else
        pthread_rwlock_wrlock(&lock.PrimitiveLock);
#endif
    }
    bool TrySet(RWLock& lock)
    {
        Clear();
#ifdef _WIN32
        if (::TryAcquireSRWLockExclusive(&lock.PrimitiveLock) != 0)
#else
        if (pthread_rwlock_trywrlock(&lock.PrimitiveLock) == 0)
#endif
        {
            Lock = &lock;
            return true;
        }
        return false;
    }
    void Clear()
    {
        if (Lock)
#ifdef _WIN32
            ::ReleaseSRWLockExclusive(&Lock->PrimitiveLock);
#else
            pthread_rwlock_unlock(&Lock->PrimitiveLock);
#endif
        Lock = nullptr;
    }

protected:
    RWLock* Lock = nullptr;
};


#ifdef ANDROID
#include <sstream>
namespace std {
    template <typename T>
    std::string to_string(T value)
    {
        std::ostringstream os;
        os << value;
        return os.str();
    }
} // namespace std
#endif
