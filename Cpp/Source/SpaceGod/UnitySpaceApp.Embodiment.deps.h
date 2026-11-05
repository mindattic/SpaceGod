#pragma once

// What UnitySpaceApp.Embodiment.inl's members need (port of Runtime/UnitySpaceApp.Embodiment.cs).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"   // IEmbodiable, EmbodimentPose
#include "Session/LocalSession.h"    // session's default initialiser (LocalSession::Create)

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Ship;
	class FighterSeat;
	class MountedTurret;
	enum class WingOrder : int;
}
