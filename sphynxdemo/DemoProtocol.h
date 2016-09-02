#pragma once

#include "Stream.h"
#include <memory>
#include "NeighborTracker.h"


//-----------------------------------------------------------------------------
// Common

struct PlayerPosition
{
    int16_t x = 0, y = 0;
    int16_t vx = 0, vy = 0;
    uint8_t angle = 0;
    uint8_t distance = 0;

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

typedef u8 playerid_t;


//-----------------------------------------------------------------------------
// S2C Protocol

typedef void S2CSetPlayerIdT(playerid_t pid);
static const int S2CSetPlayerIdID = 0;

typedef void S2CPlayerAddT(playerid_t pid, std::string name);
static const int S2CAddPlayerID = 1;

typedef void S2CPlayerRemoveT(playerid_t pid);
static const int S2CRemovePlayerID = 2;

typedef void S2CPlayerUpdatePositionT(playerid_t pid, u16 timestamp, PlayerPosition position);
static const int S2CPositionUpdateID = 3;


//-----------------------------------------------------------------------------
// C2S Protocol

typedef void C2SLoginT(std::string name);
static const int C2SLoginID = 0;

typedef void C2SPositionUpdateT(u16 timestamp, PlayerPosition position);
static const int C2SPositionUpdateID = 1;
