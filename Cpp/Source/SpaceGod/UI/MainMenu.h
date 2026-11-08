#pragma once

// MainMenu: the C++ port of Unity.SpaceGod's Runtime/UI/MainMenu.cs (US-T31).
// In Unity the menu is a MonoBehaviour that lives in the MainMenu scene; in Unreal that scene is the empty map opened
// with ?Scene=MainMenu (GameLaunch), so the two entry points US-T29 wired stay:
//  - Bootstrap::EnsureApp calls MainMenu::Create(world) when that scene loads (Unity: the scene's MainMenu object);
//  - ASpaceHUD::DrawHUD calls MainMenu::DrawGui(world, canvas) every frame while no app exists: the menu's Update
//    (its OnGUI is registered with GuiHost, which draws it on its own overlay).

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

class UWorld;
class UCanvas;

namespace SpaceGod::Core
{
	class SaveSlots;
	class SaveHeader;
	class AssetCandidate;
}

namespace SpaceGod::UI
{
	class PauseMenu;
	class HelpScreen;
	class CreditsScreen;

	/// The main menu, the scene the game opens on: Continue (the profile's latest save), New Game, Load Game,
	/// Profiles (switch to another, or make a new one), Settings (the pause menu's own pages), Help (every key
	/// and control, U.SPC-T494), Credits (US-T246: the rolling credits) and Quit, over a
	/// slowly turning sky. The game scene is loaded only once the player picks a game; a run for the tour, a
	/// capture or <c>-continue</c> never stops here (<see cref="Bootstrap"/>). Esc goes back a page.
	class MainMenu final
	{
	public:
		MainMenu();
		MainMenu(const MainMenu&) = delete;
		MainMenu& operator=(const MainMenu&) = delete;

		/// The menu scene has loaded: make the scene's menu (once per world).
		static void Create(UWorld* world);

		/// Once a frame while the menu scene has no app (ASpaceHUD::DrawHUD): the menu's Update. The canvas is not
		/// used: GuiHost draws the menu's OnGUI on its own overlay.
		static void DrawGui(UWorld* world, UCanvas* canvas);

		/// Whether the menu scene's menu is up (the C# track's MainMenu.Showing; the smoke run asks).
		static bool Showing();

		/// Stands in for the GameObject's transform (the sky is parented to it).
		cs::Ref<Transform> transform = cs::New<Transform>();

		void Awake();
		void Update();

		/// The main menu scene is unloading (a game was picked, or the app is quitting): its own styles'
		/// DontSave textures, and the Settings page's, outlive the scene otherwise, so every trip back through the
		/// main menu would leave a round's worth behind (U.SPC-T366).
		void OnDestroy();

		void OnGUI();

	private:
		enum class Page : int
		{
			Main,
			Load,
			Profiles,
			AssetLibraries,
			Help,
			Credits, // US-T246: the rolling credits (fades itself in)
		};

		Page page = Page::Main;
		const cs::Ref<PauseMenu> settings;
		const cs::Ref<HelpScreen> help;
		const cs::Ref<CreditsScreen> credits; // US-T246

		cs::Ref<Core::SaveSlots> slots;
		cs::List<cs::String> saveNames;
		cs::List<cs::Ref<Core::SaveHeader>> saveHeaders;
		cs::Ref<Core::SaveHeader> latest;
		Vector2 saveScroll, profileScroll;

		/// The save or profile the player has asked to delete, waiting for the second click that confirms it.
		/// (Empty: C# null.)
		cs::String deleteArmed;

		cs::List<cs::String> profiles;
		cs::String newProfileName = "";

		/// A line under a page's buttons saying what went wrong (a name refused, a file that would not go).
		/// (Empty: C# null.)
		cs::String notice;

		Camera* skyCamera = nullptr;

		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, heading, title, text, small, button, buttonActive, row, field;
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture, rowTexture, fieldTexture;

		/// The profile in use and its saves, read again (after a switch, a delete, or coming back to the menu).
		void ReadProfile();

		void Open(Page p);

		void CreateProfile();

		void Delete(const cs::String& slot);

		static void Quit();

		// ---------------------------------------------------------------- drawing

		static constexpr float RowH = 26;

		static Rect Centred(float w, float h);

		void DrawMain();
		void DrawLoad();
		void DrawProfiles();

		/// Whether a candidate's demo scene is in the build, so the menu can load it (U.SPC-T378).
		static bool DemoInstalled(const cs::Ref<Core::AssetCandidate>& c);

		void DrawAssetLibraries();
		void EnsureStyles();
	};
}
