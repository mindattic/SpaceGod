#pragma once

// What UnitySpaceApp.Hauler.inl's members need (port of Runtime/UnitySpaceApp.Hauler.cs).

#include "Cs/Cs.h"
#include "Combat/HaulerFlight.h" // HaulerStop (by value in PilotStop)

namespace SpaceGod::Core
{
	class IFlightModel;
	struct HullDynamics;
	struct PilotInput;
}
