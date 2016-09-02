#include "Stream.h"

#include <string.h>


//-----------------------------------------------------------------------------
// Stream

Stream::Stream()
    : Writing(true)
    , Front(nullptr)
    , Size(0)
    , Used(0)
    , Truncated(false)
    , Dynamic(nullptr)
{
}

Stream::~Stream()
{
    delete[] Dynamic;
    Dynamic = nullptr;
    Front = nullptr; // Catch use-after-free
}

// Next highest power of two (e.g. 13 -> 16)
// Zero input gives undefined output
static uint32_t NextHighestPow2(uint32_t n)
{
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

void Stream::WriteReset()
{
    if (!Writing)
    {
        DEBUG_BREAK; // Invalid operation on a read stream
        return;
    }

    Used = 0;
}

bool Stream::Grow(int newUsed)
{
    if (!Writing)
    {
        // Never grow when writing.  This probably means the buffer is truncated.
        return false;
    }

    // This would cause undefined output for the POW2 function.
    if (newUsed <= 0)
    {
        // Invalid input.
        return false;
    }

    // Bump buffer size to the next power of two.
    int newSize = NextHighestPow2(newUsed);

    // This can happen on 32-bit roll-around.
    if (newSize <= 0)
    {
        // Invalid input.
        return false;
    }

    // Allocate this size.
    u8* newDynamic = new u8[newSize];

    if (!newDynamic)
    {
        // Out of memory.
        return false;
    }

    // Copy the current Used memory over to the new buffer.
    memcpy(newDynamic, Front, Used);

    // Use the new dynamic buffer.
    delete[] Dynamic;
    Dynamic = Front = newDynamic;
    Size = newSize;

    return true;
}

void Stream::WrapBuffer(void* vbuffer, int size, bool writing)
{
    Front     = (u8*)vbuffer;
    Size      = size;
    Writing   = writing;
    Used      = 0;
    Truncated = false;

    if (!vbuffer || size <= 0)
    {
        DEBUG_BREAK; // Invalid input
        return;
    }
}

u8* Stream::GetBlock(int bytes)
{
    int newUsed = Used + bytes;

    // If truncated,
    if (newUsed > Size && !Grow(newUsed))
    {
        Truncated = true;
        return nullptr;
    }

    // Store off pointer to return
    u8* data = Front + Used;

    // Update used count
    Used = newUsed;

    // Return start of the region
    return data;
}
