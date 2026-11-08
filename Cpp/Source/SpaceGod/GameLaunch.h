#pragma once

#include "Cs/Cs.h"
#include "NewGameSetup.h" // US-T247

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

		/// US-T247: a new game set up on the main menu's New Game page: its start day, galaxy seed and starting situation (the
		/// difficulty is the setting itself). The god view offers the guided tutorial as MenuNewGame does; the others never.
		static void NewGame(const Core::NewGameSetup& setup);

		/// US-T247: sets up the next new game started (the Host Co-op page's new game: SetUpNext, then Host with no slot).
		static void SetUpNext(const Core::NewGameSetup& setup);

		/// US-T247: what the new game starting was set up with, once; false for a game without a setup (Quick Start, a
		/// script, a console command, a load), which starts as it always did.
		static bool TakeSetup(Core::NewGameSetup& setup);

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

		// ---------------------------------------------------------------- co-op from the menu (US-T248)

		/// US-T248: the main menu's Host Co-op page: a new game (an empty slot) or a save, opened as a listen server, so
		/// other players can join it (as -CoopHost does from the command line).
		static void Host(const cs::String& slot);

		/// US-T248: whether the next scene load should listen for co-op players, once (SceneLoader asks).
		static bool TakeHost();

		/// US-T248: the main menu's Join Co-op page: connect to a host at this address (as -CoopJoin=<address> or the
		/// console's open <address> do), through a fade.
		static void Join(const cs::String& address);

		/// Set by the game mode: how a join reaches Unreal (a client travel to the address).
		static inline cs::Action<cs::String> Connector;

		/// US-T248: why the last join failed or the host was lost (empty: nothing to say), for the Join page to show
		/// once the engine has brought the game back to the menu; taken (cleared) by the reader.
		static inline cs::String JoinFailure;

		/// US-T248: the name this player goes by in co-op, picked on the Host/Join pages (empty: -CoopName= or the
		/// account's user name; CoopPeer::NameFromCommandLine).
		static inline cs::String CoopName;

		/// US-T248: the profile settings the Host/Join pages keep: the player's co-op name, and the hosts joined
		/// lately (newest first, '|' between them).
		static constexpr const char* CoopNameKey = "coopName";
		static constexpr const char* RecentHostsKey = "coopRecentHosts";
		static constexpr int MaxRecentHosts = 8;

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
		static inline bool hostCoop = false; // US-T248
		static inline Core::NewGameSetup setup; // US-T247
		static inline bool hasSetup = false; // US-T247
	};
}
