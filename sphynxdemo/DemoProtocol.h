#pragma once

#include "Stream.h"
#include <memory>
#include "AABBCollisions.h"

struct PositionPacket
{
    int16_t x, y;
    int16_t vx, vy;
    uint8_t angle;
    uint8_t distance;

    inline bool Serialize(Stream& stream)
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
