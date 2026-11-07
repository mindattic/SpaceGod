#pragma once

#include "Cs/Cs.h"

class UWorld;
class AUnitySpaceAppActor;

namespace SpaceGod
{
	/// Makes any scene playable: if no UnitySpaceApp exists after load, one is
	/// created. With no flags that is all that happens. <c>-demo</c> runs the scripted tour once and
	/// quits (CaptureRunner) -- the same flag name used for it in every MindAttic Unity
	/// project. Pair it with <c>-capture</c> (parsed separately by MindAttic.Capture) to record the
	/// session.
	///
	/// A headless run (US-T233; since US-T234 the shared MindAtticShared plugin's <c>--headless</c> or
	/// <c>MINDATTIC_HEADLESS=1</c>, FMindAtticHeadless::IsActive()) is this same ordinary session -- not the
	/// -SpaceGodSmoke tour, not -SpaceGodViews -- left running with no window for a test or CI harness to drive and
	/// query, past the main menu and on scripted input. The plugin logs MINDATTIC_HEADLESS_READY; the full launch is
	/// Unreal.Shared's tools/Start-Headless.ps1 (tools/play.ps1 -Offscreen goes through it).
	///
	/// Unreal: [RuntimeInitializeOnLoadMethod(AfterSceneLoad)] is ASpaceGameMode::BeginPlay calling EnsureApp with the
	/// scene the map was opened for (GameLaunch).
	class Bootstrap
	{
	public:
		/// After a scene loads: the main menu scene shows the menu (or skips to the game for
		/// -demo/-capture/-continue/a headless run); the game scene gets its app.
		static void EnsureApp(UWorld* world, const cs::String& scene);

		static bool HasCommandLineFlag(const cs::String& flag);

	private:
		static void Equip(UWorld* world);
	};
}
