#pragma once

// What UnitySpaceApp.Embody.inl's members need (US-T135, the universal embody layer).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h" // EmbodimentSubject, EmbodimentRolePanel, EmbodimentPose (by value)
#include "Session/PlayerId.h"      // PlayerId (by value)

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class RepairWorker; // US-T158
	class Ship;
}

namespace SpaceGod::Views
{
	class EmbodiedShine; // US-T136
	class EvaWorkerView; // US-T158
}
