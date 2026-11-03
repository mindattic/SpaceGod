#pragma once

#include "HAL/PlatformTime.h"

class UWorld;

// -SpaceGodRace (US-T46, docs/RACE.md): the race's scripted tour, the same script and frame counts as the C# track's
// (CSharp/.../Runtime/Race/RaceRun.cs); the shared sampler (SpaceGodRender's SpaceGodRace.cpp) times every frame.

namespace SpaceGod::Race
{
	/// The track's own per-frame times handed to the shared sampler: the simulation steps of UnitySpaceApp::Update
	/// (Galaxy->Update and the adventures), the whole Unity frame (AUnitySpaceAppActor::Step) and the IMGUI pass.
	struct Probe
	{
		static inline double SimMs = 0, AppMs = 0, GuiMs = 0;
		static double Now() { return FPlatformTime::Seconds() * 1000.0; }
	};

	/// Called by ASpaceGameMode::StartPlay: starts the race once per process when -SpaceGodRace is given.
	void BeginScene(UWorld* world);
}
