#pragma once

// What UnitySpaceApp.Comms.inl's members need (US-T137 comms, US-T138 named pilots).

#include "Cs/Cs.h"
#include "Comms/Comms.h" // Core::CommsParty, CommsLine, CommsContext (held by value)
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Ship;
}
