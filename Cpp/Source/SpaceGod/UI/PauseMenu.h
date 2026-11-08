#pragma once

// PauseMenu: the C++ port of Unity.SpaceGod's Runtime/UI/PauseMenu.cs (US-T31). A plain class, held by its owner
// (UnitySpaceApp, MainMenu) as cs::Ref<PauseMenu>.

#include "Cs/Cs.h"
#include "DisplaySettings.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::Core
{
	class GameSetting;
}

namespace SpaceGod::UI
{
	class CreditsScreen;

	/// The pause menu (U.SPC-T276), opened by Esc once there is nothing left for it to back out of (or at the
	/// helm): Resume, Help, Save / Load, Settings, Credits (US-T246), Main Menu and Quit. Settings lists every setting the game's systems
	/// declare in the <see cref="SettingsRegistry"/> (U.SPC-T431), by group, with a search across them all, each
	/// taking effect at once and remembered in the profile, with Reset per setting and per group; a new setting
	/// shows up here by being declared, never by an edit to this menu. Settings also holds the Display page, where
	/// the resolution, the window mode and VSync are picked; nothing changes there until Apply, and what Apply puts
	/// on the screen is taken back after ten real seconds unless the player keeps it. While the menu is open the whole simulation stands still
	/// (the app skips its frame), the cursor is free and the piloted hull takes no stick or trigger. Esc goes
	/// back one page, and on the first page resumes. The main menu opens the same Settings on their own
	/// (<see cref="OpenSettings"/>).
	class PauseMenu final
	{
	private:
		enum class Page : int
		{
			Main,
			Settings,
			Display,
			Credits, // US-T246: the rolling credits, over the whole screen
		};

		Page page = Page::Main;
		float timeScaleBefore = 1;

		/// US-T246: the Credits page's screen (made the first time it is opened).
		cs::Ref<CreditsScreen> credits;

		/// The Display page's sizes, the settings it is editing (not yet applied), and how far its list is scrolled.
		cs::List<Core::DisplayResolution> resolutions, allResolutions;

		/// The aspect ratio the resolution list is narrowed to (U.SPC-T284), or "All".
		cs::String aspect = "All";
		Core::DisplaySettings pending;
		Vector2 resolutionScroll;

		/// The countdown after Apply, or null when no new settings are waiting to be kept.
		cs::Ref<Core::DisplayConfirmation> confirm;

		/// A line under the Display page's buttons saying what last happened (kept, reverted). (Empty: C# null.)
		cs::String notice;

		/// The Settings page: the group shown (null: the first), the search typed, and how far its list is scrolled.
		/// (group: empty is C# null.)
		cs::String group;
		cs::String search = "";
		Vector2 settingsScroll;

		/// US-T252: whether the nav's "Advanced (simulation)" section (the design groups) is unfolded -- folded until the
		/// player opens it -- and how far the nav is scrolled when its groups run past the panel.
		bool advancedOpen = false;
		Vector2 navScroll;

		/// US-T249: the Key bindings page listening for a key: the action (an Input::KeyAction) and slot clicked, -1 while
		/// it is not. Update takes the next chord pressed into it; Esc cancels.
		int captureAction = -1, captureSlot = -1;

		/// US-T252: Main Menu or Quit picked with Confirm before quitting on: the question shown over the first page.
		enum class Leave : int { None, MainMenu, Quit };
		Leave leaving = Leave::None;

		/// The words that find the Display page in a search, as the registry's settings are found by theirs.
		static constexpr const char* DisplaySearchWords = "display graphics resolution screen window mode borderless fullscreen exclusive vsync aspect ratio ultrawide";

		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, text, small, button, buttonActive, row, rowSelected, countdown, field, label;
		cs::Ref<Texture2D> panelTexture, shadeTexture, buttonTexture, buttonActiveTexture, rowHoverTexture, rowSelectedTexture, fieldTexture;

	public:
		/// C#: public bool IsOpen { get; private set; }
		bool IsOpen = false;

	private:
		/// Opened on Settings by itself (from the main menu): backing out of Settings closes it.
		bool settingsOnly = false;

	public:
		/// Save / Load: the game's saves panel (the menu closes first).
		cs::Action<> ShowSaves;

		/// Help: the game's Help screen, every key and control (the menu closes first, U.SPC-T494).
		cs::Action<> ShowHelp;

		/// Encyclopedia: the game's Encyclopedia, every article in one place (the menu closes first, US-T271).
		cs::Action<> ShowEncyclopedia;

		/// Main Menu: leaves the game for the main menu scene.
		cs::Action<> ToMainMenu;

		/// Opens on the first page and stops scaled time (particles, anything on <see cref="Time.deltaTime"/>);
		/// the simulation itself stands still because the app skips its frame while the menu is open.
		void Open();

		/// Opens straight on Settings, for a screen of its own (the main menu): Back from there closes it.
		void OpenSettings();

		/// Closes the menu and lets time run as it was. New display settings not yet kept are taken back.
		void Close();

		/// Each frame while open: Esc goes back a page (or resumes from the first), and the countdown after
		/// Apply runs on <paramref name="realSeconds"/> of unscaled time, reverting when it runs out.
		void Update(bool escape, float realSeconds);

	private:
		void Back();
		void OpenDisplay();

		/// Narrows the resolution list to <paramref name="a"/> (every size if none is of it).
		void PickAspect(const cs::String& a);

		void ApplyPending();
		void Keep();
		void Revert();
		static void Quit();

		// ---------------------------------------------------------------- drawing

		static constexpr float RowH = 24;

	public:
		/// Draws the open page over the whole screen (the HUD underneath is drawn disabled by the app).
		void Draw();

	private:
		Rect Centred(float w, float h) const;

		void DrawMain();

		static constexpr float SettingRowH = 58;

		/// The Settings page (U.SPC-T431): the player's groups and Display down the left, then the design groups folded
		/// under "Advanced (simulation)" (US-T252), the chosen group's settings (or, while something is typed in Search,
		/// every setting it finds, folded or not) on the right.
		void DrawSettings();

		/// One setting's row: its name and what it does on the left, its control and Reset on the right.
		void DrawSetting(const cs::Ref<Core::GameSetting>& setting, Rect r, bool showGroup);

		/// US-T249: a key binding's row: its action and any clash on the left, its three key slots (click one, then press
		/// the key) and Reset (Clear while a slot listens) on the right.
		void DrawKeyBinding(const cs::Ref<Core::GameSetting>& setting, Rect r, bool showGroup);

		/// US-T249: while a slot listens, the chord pressed this frame goes into it; Esc (or leaving the page) cancels.
		void UpdateCapture(bool escape);

		/// Whether every word of <paramref name="query"/> is among <paramref name="words"/>.
		static bool MatchesAll(const cs::String& words, const cs::String& query);

		void DrawDisplay();
		void DrawConfirm();

		/// US-T252: "Leave for the main menu?" / "Quit the game?" over the first page; Yes leaves, No (or Esc) stays.
		void DrawLeave();
		/// Main Menu or Quit, now (the question answered, or none asked).
		void LeaveNow(Leave what);

		/// US-T252: one nav button for a group (highlighted while shown); picking it shows the group.
		void NavGroup(const cs::String& g, Rect r, bool searching);
		void EnsureStyles();

	public:
		/// The menu's own styles are going with whatever owns it -- the game session or the main menu
		/// (U.SPC-T366): DontSave outlives the scene otherwise, so every session (or trip through the main menu)
		/// that opened Settings would leave its textures behind.
		void DestroyOwnedTextures();
	};
}
