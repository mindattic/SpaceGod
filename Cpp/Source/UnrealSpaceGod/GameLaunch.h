#pragma once

#include "Cs/Cs.h"

namespace SpaceGod
{
	/// The way between the main menu scene and the game scene: the menu starts a new game or names the save to
	/// resume, and the game goes back to the menu. A run started for the tour, a capture or <c>-continue</c>
	/// skips the menu.
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

		static void Load(const cs::String& slot);

		/// The save the menu asked the game to resume, once (empty for a new game; C# null).
		static cs::String TakeLoadSlot();

		static void ToMainMenu();

		/// SceneManager.LoadScene(name): the host travels to the map with this scene (set by ASpaceGameMode's world).
		static void LoadScene(const cs::String& scene);

		/// Set by the game mode: how a scene load reaches Unreal (ServerTravel / OpenLevel on the current map).
		static inline cs::Action<cs::String> SceneLoader;

	private:
		static void Start(const cs::String& slot);
		static inline cs::String loadSlot;
	};
}
