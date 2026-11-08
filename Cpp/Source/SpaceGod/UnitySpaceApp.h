#pragma once

// UnitySpaceApp: the C++ port of Unity.SpaceGod's Runtime/UnitySpaceApp.cs and its partials. A plain C++ class (the
// MonoBehaviour's logic), owned and ticked by AUnitySpaceAppActor, which calls the Unity messages below in Unity's
// order (Awake once; Update then OnGUI each frame; OnApplicationQuit / OnDestroy at the end).
//
// The C# class is partial across UnitySpaceApp.cs and UnitySpaceApp.<Part>.cs. Here each part declares its own
// members in UnitySpaceApp.<Part>.inl (included inside the class body below, each opening with its own access
// specifier) with what those members need in UnitySpaceApp.<Part>.deps.h, and defines them in UnitySpaceApp.<Part>.cpp.
// UnitySpaceApp.cs itself is split in two at its line 1470 (Main: lines 1-1470, Main2: the rest) only for porting.
//
// Name rules (docs/MIGRATION.md, plus): ICameraWorld's members are virtual methods, so the app's Scale is the method
// Scale() over the field scaleModel; bodied properties (Catalog, Ephemeris, World, Fleet, Economy, IsPiloting ...) are
// methods; auto-properties (Galaxy, ActiveSystem, Clock, Selected, ShowHud ...) are fields. Inside the class, types
// are always namespace-qualified (Core::Galaxy, Views::ShipView, UI::Hud) since many members share a type's name.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "UnityCompat/UnityTypes.h"
#include "Target.h"
#include "Camera/SpaceCamera.h" // ICameraWorld
#include "Input/InputState.h"

#include "UnitySpaceApp.Main.deps.h"
#include "UnitySpaceApp.Main2.deps.h"
#include "UnitySpaceApp.Drift.deps.h" // US-T165
#include "UnitySpaceApp.Boarding.deps.h"
#include "UnitySpaceApp.Broadsides.deps.h"
#include "UnitySpaceApp.Collisions.deps.h"
#include "UnitySpaceApp.Course.deps.h"
#include "UnitySpaceApp.Embodiment.deps.h"
#include "UnitySpaceApp.Ftl.deps.h"
#include "UnitySpaceApp.Hauler.deps.h"
#include "UnitySpaceApp.Lanes.deps.h"
#include "UnitySpaceApp.Practice.deps.h"
#include "UnitySpaceApp.Saves.deps.h"
#include "UnitySpaceApp.Streaming.deps.h"
#include "UnitySpaceApp.Coop.deps.h" // US-T44
#include "UnitySpaceApp.Comms.deps.h" // US-T137
#include "UnitySpaceApp.Cheats.deps.h" // US-T126
#include "UnitySpaceApp.Cruise.deps.h" // US-T127
#include "UnitySpaceApp.Tractor.deps.h" // US-T95
#include "UnitySpaceApp.SkyTravel.deps.h" // US-T145
#include "UnitySpaceApp.Embody.deps.h" // US-T135
#include "UnitySpaceApp.Influence.deps.h" // US-T140
#include "UnitySpaceApp.Discoveries.deps.h" // US-T259
#include "UnitySpaceApp.Tutorial.deps.h" // US-T254

namespace SpaceGod
{
	/// Owns the simulation and drives every frame:
	/// input -> clock -> every star system (ephemeris, fleet, markets, wars) -> scale layout of the
	/// active system -> ships -> camera -> camera-relative views.
	///
	/// Every star system in the Galaxy is simulated on the shared clock all the time;
	/// only the active one is drawn. Switching system tears its views down and builds the other's
	/// (the camera flies between them through the interstellar view, see StarTravel).
	///
	/// Floating origin: the camera never moves from the world origin. All positions are kept
	/// in double-precision scene units and every renderer is placed at (scene - camera) each frame,
	/// so a 3 km ship next to the camera renders cleanly while Neptune sits 4.5 million units away.
	/// (UnrealSpace converts scene units to Unreal's at the last moment, on top of Large World Coordinates.)
	class UnitySpaceApp final : public Cameras::ICameraWorld, public std::enable_shared_from_this<UnitySpaceApp>
	{
	public:
		UnitySpaceApp() = default;
		~UnitySpaceApp() override = default;
		UnitySpaceApp(const UnitySpaceApp&) = delete;
		UnitySpaceApp& operator=(const UnitySpaceApp&) = delete;

		// ---- Unity messages, called by AUnitySpaceAppActor (private in C#; public here for the host).
		void Awake();
		void Update();
		void OnGUI();
		void OnApplicationQuit();
		void OnDestroy();

		/// MonoBehaviour.enabled: false stops Update (the host checks it).
		bool enabled = true;

		/// US-T45 (the C# track's UnitySpaceApp.StartJd): a fixed start for the next game's clock (the screenshot runs,
		/// so both tracks show the same moment); NaN starts at now, as the original.
		static inline double StartJd = std::numeric_limits<double>::quiet_NaN();

		/// The smoke run (Smoke/SpaceGodSmoke.cpp) reads private members, as the C# track's smoke does by reflection.
		friend struct SmokeAccess;

#include "UnitySpaceApp.Main.inl"
#include "UnitySpaceApp.Main2.inl"
#include "UnitySpaceApp.Drift.inl" // US-T165: drift, and the RCS jets on the hull flown
#include "UnitySpaceApp.Boarding.inl"
#include "UnitySpaceApp.Broadsides.inl"
#include "UnitySpaceApp.Collisions.inl"
#include "UnitySpaceApp.Course.inl"
#include "UnitySpaceApp.Embodiment.inl"
#include "UnitySpaceApp.Ftl.inl"
#include "UnitySpaceApp.Hauler.inl"
#include "UnitySpaceApp.Lanes.inl"
#include "UnitySpaceApp.Practice.inl"
#include "UnitySpaceApp.Saves.inl"
#include "UnitySpaceApp.Streaming.inl"
#include "UnitySpaceApp.Coop.inl" // US-T44: co-op (the C# track's UnitySpaceApp.Coop.cs)
#include "UnitySpaceApp.Comms.inl" // US-T137: the comms panel, US-T138: the held unit's pilot
#include "UnitySpaceApp.Cheats.inl" // US-T126: the dev menu
#include "UnitySpaceApp.Cruise.inl" // US-T127: the in-system cruise drive
#include "UnitySpaceApp.Tractor.inl" // US-T95: cargo pods and the tractor beam
#include "UnitySpaceApp.Mining.inl" // US-T264: hands-on mining's readout and beam
#include "UnitySpaceApp.SkyTravel.inl" // US-T145: the sky's layers and the travel between them
#include "UnitySpaceApp.Embody.inl" // US-T135: the universal embody layer
#include "UnitySpaceApp.Influence.inl" // US-T140: embody limits, Influence and squad orders as the player meets them
#include "UnitySpaceApp.Discoveries.inl" // US-T259: the discovery log and its screen
#include "UnitySpaceApp.Tutorial.inl" // US-T254: the guided tutorial and the first-time hints
	};
}
