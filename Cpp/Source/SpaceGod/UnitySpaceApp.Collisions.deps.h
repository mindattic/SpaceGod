#pragma once

// What UnitySpaceApp.Collisions.inl's members need (port of Runtime/UnitySpaceApp.Collisions.cs).

#include <memory>

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/Collisions.h" // CollisionVolume, Impact (by value / by reference in the members' signatures)

namespace SpaceGod::Core
{
	class Ship;
	class AsteroidField;
}
