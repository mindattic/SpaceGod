#pragma once

// What Hud.Panels.inl's members need: the "between the stars", "opportunities (U.SPC-T44)" and "panels" sections of
// Unity.SpaceGod's Runtime/UI/Hud.cs (its lines 1223-2382; US-T31). Target (held by value in NavigatorRow) and
// DVec3 come in through Hud.h; the Core types below are only held by cs::Ref or passed as enums.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Target.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::Core
{
	class Ship;
	class Battle;
	class Opportunity;
	class BodyDefinition;
	class OrbitalStructure;
	class DockingRing;
	class Company;
	class Faction;
	class Settlement;
	enum class EngineTelegraph : int;
	enum class PilotControl : int;
	enum class SettlementKind : int;
}
