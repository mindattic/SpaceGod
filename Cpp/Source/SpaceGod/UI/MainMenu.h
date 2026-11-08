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
#include "UI/MenuBackdrop.h" // US-T245
#include "UI/NewGamePage.h" // US-T247

class UWorld;
class UCanvas;

namespace SpaceGod::Core
{
	class SaveSlots;
	class SaveHeader;
}

namespace SpaceGod::UI
{
	class PauseMenu;
	class HelpScreen;
	class CreditsScreen;

	/// The main menu, SpaceGod's front door (US-T245): the logo over the player's list -- Continue (the profile's latest
	/// save), New Game (US-T247's setup page, or Quick Start), Load Game, Co-op (US-T248's Host Co-op and Join Co-op:
	/// host a new game or a save, or join one by address or from the recent hosts), Settings (the pause menu's own
	/// pages), Help (every key and control, U.SPC-T494), Credits (US-T246: the rolling credits), Extras (the tutorial,
	/// the dogfight demos and the showcase tour) and Quit -- with Profiles (switch to another, or make a new one) on the
	/// profile line and the build's version and commit in the corner, over a slowly turning sky with Earth and the odd
	/// passing ship (MenuBackdrop). The game scene is loaded only once the player picks a game; a run for the tour, a
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
			Extras, // US-T245: the tutorial, the dogfight demos and the showcase tour (the Asset Library POC page is gone)
			Help,
			Host, // US-T248: host a co-op game (a new game or a save, listening)
			Join, // US-T248: join one, by address or from the recent hosts
			Credits, // US-T246: the rolling credits (fades itself in)
			NewGame, // US-T247: the New Game setup page (a new game, or the Host Co-op page's)
			Coop, // US-T245: the Co-op entry: Host Co-op or Join Co-op
		};

		Page page = Page::Main;
		NewGamePage newGame; // US-T247
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

		/// US-T245: Earth and the passing ships behind the menu.
		std::unique_ptr<MenuBackdrop> backdrop;

		// US-T248: the Host and Join pages: the player's co-op name, the address being typed, the hosts joined lately
		// (newest first, from the profile) and this machine's addresses (for the host to give out).
		cs::String coopName;
		cs::String joinAddress;
		cs::List<cs::String> recentHosts;
		cs::List<cs::String> localAddresses;
		Vector2 hostScroll, recentScroll;

		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, heading, title, text, small, button, buttonActive, row, field;
		cs::Ref<GUIStyle> corner, profileButton; // US-T245: the version line, the profile line's Profiles button
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture, rowTexture, fieldTexture;
		cs::Ref<Texture2D> logo; // US-T245: the SpaceGod logo over the menu (null: the wordmark alone)

		/// The profile in use and its saves, read again (after a switch, a delete, or coming back to the menu).
		void ReadProfile();

		void Open(Page p);

		void CreateProfile();

		void Delete(const cs::String& slot);

		/// US-T248: the profile's co-op name and recent hosts, read again (Host and Join pages).
		void ReadCoop();

		/// US-T248: keeps the name typed on the page (the profile's, and the one CoopPeer gives the host).
		void KeepCoopName();

		/// US-T248: joins the host at this address, remembering it first among the recent hosts.
		void JoinHost(const cs::String& address);

		/// US-T248: drops an address from the recent hosts.
		void ForgetHost(const cs::String& address);

		/// US-T248: writes the recent hosts to the profile (a notice when the disk refuses).
		void WriteRecentHosts();

		static void Quit();

		// ---------------------------------------------------------------- drawing

		static constexpr float RowH = 26;

		static Rect Centred(float w, float h);

		void DrawMain();
		void DrawLoad();
		void DrawProfiles();
		void DrawHost(); // US-T248
		void DrawJoin(); // US-T248
		void DrawNewGame(); // US-T247

		/// US-T248: the "Your name" row the Host and Join pages share, at y; the row's height.
		float DrawCoopName(const Rect& r, float y);

		/// US-T245: the logo and the SPACEGOD wordmark over a page whose panel is <paramref name="r"/>.
		void DrawHeading(const Rect& r);

		/// US-T245: the panel of the given size, centred with room above it for the heading.
		static Rect UnderHeading(float w, float h);

		void DrawCoop(); // US-T245
		void DrawExtras(); // US-T245
		void EnsureStyles();
	};
}
