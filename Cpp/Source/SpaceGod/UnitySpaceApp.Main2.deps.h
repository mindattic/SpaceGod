#pragma once

// What UnitySpaceApp.Main2.inl's members need (port of Runtime/UnitySpaceApp.cs lines 1471-end, US-T29): the
// value types its declarations hold or return (DVec3, ShipState, Rgb, SurrenderTerms, Unity's Vector/Quaternion/Mesh)
// and forward declarations of the classes it holds by cs::Ref.

#include <tuple>

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Ships.h"          // ShipState (returned by value: ShipStateOf)
#include "BodyDefinition.h" // Rgb (SunLight)
#include "UnityCompat/UnityTypes.h"
#include "Target.h"

namespace SpaceGod::Core
{
	class Ship;
	class Battle;
	class BattleUnit;
	class Pursuer;
	class Settlement;
	class Ephemeris;
	class StarSystem;
	class BodyDefinition;
	class ToggleSetting;
}

namespace SpaceGod::UnityCompat
{
	class Renderer; // ModelRenderersOf (US-T78)
}
