#pragma once

// What Hud.Drawer.inl's members need: the bottom drawer (US-T200).

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"
#include "Target.h"
#include "UI/HudDrawers.h" // US-T438

#include <vector>

namespace SpaceGod::Shared::Json
{
	class JsonObject; // US-T441
}

namespace SpaceGod::Core
{
	class Ship;
	class StarSystem;
}
