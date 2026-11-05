#pragma once

// What UnitySpaceApp.Streaming.inl's members need (port of Runtime/UnitySpaceApp.Streaming.cs).

#include <chrono>

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Streaming.h" // StreamItem (by value in a list), GateCover (made in a member initialiser)

namespace SpaceGod::Core
{
	class Ship;
	class StarSystem;
}
