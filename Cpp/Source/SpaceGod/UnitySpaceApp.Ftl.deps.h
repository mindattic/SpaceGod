#pragma once

// What UnitySpaceApp.Ftl.inl's members need (port of Runtime/UnitySpaceApp.Ftl.cs, US-T29).
// HelmFtl and FtlAutopilot are made in default member initialisers (cs::New), so their full types are included.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Systems/HelmFtl.h"
#include "Systems/FtlNavigation.h"

namespace SpaceGod::Core
{
	class Ship;
	class StarSystem;
	class Cruise;
	class FtlTarget;
	class FtlCourse;
}
