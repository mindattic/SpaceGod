#pragma once

#include "Cs/Cs.h"

namespace SpaceGod
{
	/// The way between the main menu scene and the game scene: the menu starts a new game or names the save to
	/// resume, and the game goes back to the menu. A run started for the tour, a capture, <c>-continue</c> or
	/// a headless run (US-T233/US-T234: FMindAtticHeadless::IsActive(), no window to click a menu in) skips the menu.
	///
	/// Unreal: a "scene" is a map. Both are the same empty map (the game is built at runtime), so loading the game
	/// scene reopens the map with ?Scene=UnitySpace and the menu with ?Scene=MainMenu; ASpaceGameMode reads the
	/// option and spawns the main menu or the app (see Bootstrap).
	class GameLaunch
	{
	public:
		static constexpr const char* MenuScene = "MainMenu";
		static constexpr const char* GameScene = "UnitySpace";

		/// Whether this game was started from the main menu (not straight into the game scene).
		static inline bool FromMenu = false;

		/// Flags that go straight into the game, with no menu to click through.
		static bool SkipsMenu();

		static void NewGame();

		/// US-T254: the main menu's New Game: a new game that offers the guided tutorial if this profile has never been
		/// offered it (a console command's or a script's NewGame never does).
		static void MenuNewGame();

		/// US-T254: a new game straight into the guided tutorial (the main menu's and the Help screen's Tutorial button).
		static void Tutorial();

		/// US-T254: what this game was asked to do about the tutorial, once: 0 nothing, 1 offer it on a profile's first
		/// New Game, 2 start it.
		static int TakeTutorial();

		static void Load(const cs::String& slot);

		/// US-T88: a new game that drops the player straight into the dogfight demo (UnitySpaceApp::StartDogfightDemo),
		/// at a light fighter's helm or, with <paramref name="heavy"/>, a heavy fighter's.
		static void DogfightDemo(bool heavy);

		/// US-T88: the dogfight demo this game was asked to open with, once: 0 none, 1 light fighter, 2 heavy fighter.
		/// Asked from the main menu, or on the command line with <c>-dogfight</c> (<c>-dogfight-heavy</c> for the heavy).
		static int TakeDogfightDemo();

		/// The save the menu asked the game to resume, once (empty for a new game; C# null).
		static cs::String TakeLoadSlot();

		static void ToMainMenu();

		/// SceneManager.LoadScene(name): the host travels to the map with this scene (set by ASpaceGameMode's world).
		static void LoadScene(const cs::String& scene);

		/// Unity's SceneUtility.GetBuildIndexByScenePath(scenePath) >= 0, for the main menu's Asset Library page: a
		/// candidate package's demo scene is "in the build" only in Unity (Asset Store packages). Unreal has none, so
		/// none is installed (the C# track's SceneUtility answers -1 the same way); the game's own two scenes go
		/// through LoadScene.
		static bool SceneInBuild(const cs::String& scenePath);

		/// Set by the game mode: how a scene load reaches Unreal (ServerTravel / OpenLevel on the current map).
		static inline cs::Action<cs::String> SceneLoader;

		/// US-T255: LoadScene through a fade to black (UiMotion::FadeOutThen), for a change the player asked for (menu ->
		/// game, game -> menu); at once when motion is off. A run skipping the menu loads straight away (LoadScene).
		static void Travel(const cs::String& scene);

	private:
		static void Start(const cs::String& slot);
		static inline cs::String loadSlot;
		static inline int dogfightDemo = 0;
		static inline bool dogfightFlagTaken = false;
		static inline int tutorial = 0; // US-T254
	};
}
