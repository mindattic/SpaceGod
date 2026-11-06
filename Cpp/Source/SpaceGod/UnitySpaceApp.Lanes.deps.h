#pragma once

// What UnitySpaceApp.Lanes.inl's members need (port of Runtime/UnitySpaceApp.Lanes.cs, US-T29).
// LaneHop (held in a list by value) and LaneThreat (held in a Nullable by value) need their full types.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Hyperlanes.h"  // LaneHop, Lane, Hyperlanes
#include "Combat/War.h"  // LaneThreat
#include "Combat/LaneFlight.h"  // LaneFlight, LaneWake (US-T196, held by value)

namespace SpaceGod::Core
{
	class Ship;
	class StarSystem;
}
