#pragma once

// Hud: the C++ port of Unity.SpaceGod's Runtime/UI/Hud.cs and Hud.Trade.cs (one partial class, `sealed partial class Hud`).
// The C# file is long, so its members are declared in four fragments included inside the class body below, each
// opening with its own access specifier, with what they need in a matching .deps.h, and defined in a matching .cpp:
//   Hud.Main    - Hud.cs lines 1-1222 and 3746-3868 (fields, Draw, saves, world overlay, flight markers, the chase,
//                 aimed combat, hyperlane rides; formatting; styles)            -> Hud.cpp
//   Hud.Panels  - Hud.cs lines 1223-2382 (between the stars, opportunities, panels: top bar, navigator, selection...)
//                                                                               -> Hud.Panels.cpp
//   Hud.Boards  - Hud.cs lines 2383-3745 (bounties, contracts, fleet, market, outfitting, spawn, cargo, news, picker)
//                                                                               -> Hud.Boards.cpp
//   Hud.Trade   - Hud.Trade.cs (the trade desk)                                 -> Hud.Trade.cpp
// Members a fragment uses but another declares are found at compile time (a class body is one scope).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"
#include "Target.h"

#include "UI/Hud.Main.deps.h"
#include "UI/Hud.Panels.deps.h"
#include "UI/Hud.Boards.deps.h"
#include "UI/Hud.Trade.deps.h"
#include "UI/Hud.Coop.deps.h" // US-T44

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::UI
{
	class GalaxyMapScreen;
	class HelpScreen;

	/// Immediate-mode HUD. World overlay: billboard icons for anything below a few pixels, labels,
	/// selection brackets and an off-screen pointer. Panels: time and scale bar, navigator tree,
	/// selection details, camera status and the controls card.
	class Hud final
	{
	public:
		explicit Hud(UnitySpaceApp* app);
		~Hud();
		Hud(const Hud&) = delete;
		Hud& operator=(const Hud&) = delete;

#include "UI/Hud.Main.inl"
#include "UI/Hud.Panels.inl"
#include "UI/Hud.Boards.inl"
#include "UI/Hud.Trade.inl"
#include "UI/Hud.Coop.inl" // US-T44: co-op (the C# track's Hud.Coop.cs)
	};
}
