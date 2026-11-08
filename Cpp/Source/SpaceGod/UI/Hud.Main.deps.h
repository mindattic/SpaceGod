#pragma once

// What Hud.Main.inl's members need: the port of Unity.SpaceGod's Runtime/UI/Hud.cs lines 1-1222 and 3746-3868
// (fields, Draw, saves, world overlay, flight markers, the chase, aimed combat, hyperlane rides; formatting; styles).
// Bodies in Hud.cpp.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"
#include "Target.h"
#include "RenderLod.h" // LodMark, held by value in iconMarks
#include "UI/HudLayout.h" // US-T152..T155: the docking regions, held by value in layout
#include "UI/UiStyle.h" // US-T199: the shared palette, type scale, frames and spacing every panel draws with

namespace SpaceGod::Core
{
	class Ship;
	class Faction;
	class Fleet;
	class Contact;
	class ContactBook;
	class CargoItem;
	class CargoHold;
	class SaveHeader;
	class FtlCourse;
	class Cruise;
	class BodyDefinition;
	class Battle;
	class BattleUnit;
	class BroadsideCommand;
	class ShipCombat;
	enum class SubsystemKind : int;
}

namespace SpaceGod::UI
{
	class GalaxyMapScreen;
	class HelpScreen;
	class LogsScreen; // US-T261
}
