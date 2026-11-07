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
	/// <c>--headless</c> (US-T233) is this same ordinary session -- not the -SpaceGodSmoke tour, not -SpaceGodViews --
	/// left running with no window for a test or CI harness to drive and query: paired on the real command line with
	/// -RenderOffScreen -unattended -nosplash -nosound exactly as -SpaceGodSmoke/-SpaceGodViews already pair with
	/// -nullrhi -unattended -nosplash (tools/play.ps1 -Offscreen, US-T163; those draw no window, not this flag).
	///
	/// Unreal: [RuntimeInitializeOnLoadMethod(AfterSceneLoad)] is ASpaceGameMode::BeginPlay calling EnsureApp with the
	/// scene the map was opened for (GameLaunch).
	class Bootstrap
	{
	public:
		/// After a scene loads: the main menu scene shows the menu (or skips to the game for
		/// -demo/-capture/-continue/--headless); the game scene gets its app.
		static void EnsureApp(UWorld* world, const cs::String& scene);

		static bool HasCommandLineFlag(const cs::String& flag);

	private:
		static void Equip(UWorld* world);
	};
}
