#pragma once

// What UnitySpaceApp.Comms.inl's members need (US-T137 comms, US-T138 named pilots).

#include "Cs/Cs.h"
#include "Comms/Comms.h" // Core::CommsParty, CommsLine, CommsContext (held by value)
#include "Lore/Encounters.h" // US-T239: Core::EncounterNode (passed by reference)
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Ship;
	struct LoreText;
}
