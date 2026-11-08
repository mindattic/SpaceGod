#pragma once

// What Hud.Boards.inl's members need: the port of Unity.SpaceGod's Runtime/UI/Hud.cs lines 2383-3745 (the bounty
// board, contracts, fleet, market board, outfitting, spawn, cargo hold, news feed and the take-charge picker).

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"
#include "World.h" // WorldEvent (by value in newsShown), WorldEventKind

namespace SpaceGod::Core
{
	class Settlement;
	class Faction;
	class LawRecord;
	class OwnedShip;
	class StandingOrder;
	class Ship;
	class Loadout;
	class FacilityKind;
	class CargoHold;
	class CargoItem;
	class Compartment;
	class GroupOrder;
	class StarSystem;
	enum class Good : int;
	enum class ShipClass : int;
	enum class ModuleKind : int;
	struct RepairItem; // US-T266
	enum class RepairItemKind : int; // US-T266
	enum class RepairOutcome : int; // US-T266
}
