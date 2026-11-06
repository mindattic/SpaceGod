#pragma once

// What UnitySpaceApp.Cruise.inl's members need (US-T127, the in-system cruise drive).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/SystemCruise.h" // SystemCruise (held by value in systemCruise)
#include "Combat/DriveRegime.h" // DriveRegime, DriveRegimeWatch (US-T197, held by value in driveRegime)
#include "UnityCompat/UnityGui.h" // GUIStyle (the readout's)

namespace SpaceGod::Core
{
	class Ship;
	struct EmbodimentInput;
}
