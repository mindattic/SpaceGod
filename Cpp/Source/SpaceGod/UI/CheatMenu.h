#pragma once

// US-T126: the dev menu, a pull-down over the top of the view (Shift+F1) that gathers the game's dev and debug functions
// in one place: spawn any hull beside the player, spawn enemies, travel (the cruise drive, teleports, other stars), take
// a helm or a seat, the dogfight demo, time and the two scales. Its entries come from a registry (CheatRegistry), grouped
// by category, so a new one is a CheatRegistry::Add from wherever the feature lives, never an edit to this menu.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

#include <functional>
#include <vector>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::UI
{
	/// <summary>
	/// One line of the dev menu: an action (<see cref="Run"/>) or a submenu (<see cref="Children"/>, built when it is
	/// opened, so lists of worlds, stations and stars are always the system's own). Everything but the label is optional.
	/// </summary>
	struct CheatEntry
	{
		cs::String Label;

		/// <summary>A short line on what it does, drawn dim beside the label (and searched by the filter).</summary>
		cs::String Detail;

		/// <summary>The key that does the same outside the menu, if any ("Shift+V"), drawn at the right.</summary>
		cs::String Keys;

		std::function<void(UnitySpaceApp&)> Run;
		std::function<std::vector<CheatEntry>(UnitySpaceApp&)> Children;

		/// <summary>Why it cannot run right now (drawn in its place, the line greyed), or empty when it can.</summary>
		std::function<cs::String(UnitySpaceApp&)> Blocker;

		/// <summary>A live readout at the right (ON / OFF, the warp now, GAME / REALISTIC).</summary>
		std::function<cs::String(UnitySpaceApp&)> State;

		/// <summary>The menu stays open after it runs (a toggle, a time step), so the next can follow.</summary>
		bool KeepOpen = false;

		/// <summary>Its place in its category: lower first, then in the order added (registrations run in no set order).</summary>
		int Order = 0;

		bool IsMenu() const { return static_cast<bool>(Children); }

		static CheatEntry Action(const cs::String& label, const cs::String& detail, std::function<void(UnitySpaceApp&)> run);
		static CheatEntry Menu(const cs::String& label, const cs::String& detail, std::function<std::vector<CheatEntry>(UnitySpaceApp&)> children);

		/// <summary>An entry that runs a console command (<c>sg.BioMass here</c>), greyed until a command of that name is
		/// registered: a feature with a console command of its own is in the menu by one line, and lights up when it lands.</summary>
		static CheatEntry Console(const cs::String& label, const cs::String& detail, const cs::String& command);

		CheatEntry& WithKeys(const cs::String& keys) { Keys = keys; return *this; }
		CheatEntry& WithBlocker(std::function<cs::String(UnitySpaceApp&)> f) { Blocker = std::move(f); return *this; }
		CheatEntry& WithState(std::function<cs::String(UnitySpaceApp&)> f) { State = std::move(f); return *this; }
		CheatEntry& StaysOpen() { KeepOpen = true; return *this; }
		CheatEntry& At(int order) { Order = order; return *this; }
	};

	/// <summary>
	/// What the dev menu lists, by category (US-T126). A feature adds its own entries from its own file:
	/// <code>static UI::CheatRegistration reg([] { UI::CheatRegistry::Add("Travel", UI::CheatEntry::Action(...)); });</code>
	/// Categories are listed by their order (<see cref="DeclareCategory"/>; an undeclared one goes last), entries in the
	/// order they were added.
	/// </summary>
	class CheatRegistry final
	{
	public:
		struct Category
		{
			cs::String Name;
			cs::String Blurb;
			int Order = 1000;
			std::vector<CheatEntry> Entries;
		};

		static void DeclareCategory(const cs::String& name, int order, const cs::String& blurb);
		static void Add(const cs::String& category, CheatEntry entry);
		static const std::vector<Category>& Categories();

		/// <summary>Runs <paramref name="registerEntries"/> the first time the categories are asked for (not at load, where
		/// the game's own tables may not be up yet).</summary>
		static void Defer(void (*registerEntries)());

	private:
		static std::vector<Category>& Mutable();
		static std::vector<void (*)()>& Pending();
		static Category& Find(const cs::String& name);
	};

	/// <summary>Registers entries (see <see cref="CheatRegistry"/>): a file-scope one of these in a feature's own file.</summary>
	struct CheatRegistration
	{
		explicit CheatRegistration(void (*registerEntries)()) { CheatRegistry::Defer(registerEntries); }
	};

	/// <summary>
	/// The dev menu itself (US-T126). Shift+F1 pulls it down over the top of the view; while it is open it owns the keyboard
	/// and mouse (above the craft held and the camera, below the pause menu, see InputState's ownership rules from US-T90),
	/// so flying and the menu never fight: the craft holds its heading meanwhile and the simulation runs on. Up/Down pick,
	/// Enter or a click runs (or opens a submenu), Right opens, Left or Backspace backs out, Tab steps through the
	/// categories, typing filters every entry and submenu line at once, Esc backs out and then closes. Actions run in the
	/// app's Update, never from inside a GUI pass.
	/// </summary>
	class CheatMenu final
	{
	public:
		bool IsOpen = false;

		void Open();
		void Close();

		/// <summary>Each frame while open: its keys (read from the device, the game's input is idle meanwhile), the slide,
		/// and whatever was picked last GUI pass, run now.</summary>
		void Update(UnitySpaceApp& app, float realSeconds);

		/// <summary>The menu (while open or sliding away) and, while closed, the HUD's hint for its key.</summary>
		void Draw(UnitySpaceApp& app);

		/// <summary>Shows <paramref name="category"/> (the first whose name starts with it, ignoring case); false if none does.</summary>
		bool ShowCategory(const cs::String& category);

		/// <summary>Runs the entry at <paramref name="path"/> ("Travel/Teleport to a world/Mars"): each step the first line
		/// whose label starts with it, ignoring case. <paramref name="said"/> says what ran or why not.</summary>
		static bool RunPath(UnitySpaceApp& app, const cs::String& path, cs::String& said);

		void DestroyOwnedTextures();

	private:
		struct Row
		{
			CheatEntry Entry;
			cs::String Where; // the category and submenu it is in, while filtering
			cs::String Blocker;
			cs::String State;
		};
		struct Level
		{
			cs::String Title;
			std::vector<CheatEntry> Entries;
		};

		int category = 0;
		int selected = 0;
		int scroll = 0;
		std::vector<Level> stack;
		cs::String filter;
		std::vector<Row> rows;
		float slide = 0;
		int visibleRows = 12;
		int clickedRow = -1, clickedCategory = -1;
		bool clickedBack = false;
		bool focusFilter = false;

		void Rebuild(UnitySpaceApp& app);
		void Activate(UnitySpaceApp& app, int row);
		void Back();
		void Clamp();

		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, text, small, row, rowSelected, tab, tabSelected, state, keys, blocked, field, hint;
		cs::Ref<Texture2D> panelTexture, rowHoverTexture, rowSelectedTexture, tabTexture, tabSelectedTexture, fieldTexture, hintTexture;
		void EnsureStyles();
	};
}
