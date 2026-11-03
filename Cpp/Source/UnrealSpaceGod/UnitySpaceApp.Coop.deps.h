#pragma once

// What UnitySpaceApp.Coop.inl's members need (US-T44: the C# track's Runtime/UnitySpaceApp.Coop.cs, US-T39).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Coop/CoopWire.h" // ClockState (by value)
#include "Coop/CoopPeer.h" // CoopPeer::NewSession (the session's initialiser, UnitySpaceApp.Embodiment.inl)

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace SpaceGod::Core
{
	class LocalSession;
	class IEmbodiable;
	class IGameEvent;
	class IGameCommand;
	class StarSystem;
}

