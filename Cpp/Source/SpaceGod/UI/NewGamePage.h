#pragma once

// US-T247: the main menu's New Game setup page. Not a port of a C# file.
//
// What a new game picked from the main menu starts with: the start date (today, or a picked day), the galaxy seed (random or
// typed, shown so it can be shared), the starting situation (Core::StartSituation) and the difficulty (Core::Difficulty, the
// same setting as Settings > Gameplay). The main menu's New Game and the Host Co-op page's Host New Game both open it; the
// menu's Quick Start skips it (today, the default galaxy, the god view). Drawn inside the main menu's OnGUI with its styles.

#include "Cs/Cs.h"
#include "NewGameSetup.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

// Windows' rpcndr.h defines `small` as a macro.
#ifdef small
#undef small
#endif

namespace SpaceGod::UI
{
	class NewGamePage final
	{
	public:
		/// The main menu's styles the page draws with.
		struct Styles
		{
			cs::Ref<GUIStyle> panel, title, text, small, button, buttonActive, field;
		};

		enum class Action : int
		{
			None,
			/// Back a page (the main menu, or the Host Co-op page).
			Back,
			/// Start the game set up (Setup()).
			Start,
		};

		/// Opens the page afresh: today, a new random seed, the god view; <paramref name="hosting"/> for the Host Co-op page.
		void Open(bool hosting);

		/// Whether the page was opened to host a co-op game.
		bool Hosting() const { return hosting; }

		/// Once a frame while the page is up, centred on the screen: what the player asked for.
		Action Draw(const Styles& s);

		/// US-T317: the page's keys, once a frame from the menu's Update (Esc is the menu's): Left / Right step the starting
		/// situation and Up / Down a row of them while no field is typed in; Enter starts the game (Draw says Start, if the day is one).
		void Keys();

		/// The choices as they stand (valid once Draw has said Start).
		Core::NewGameSetup Setup() const;

	private:
		bool hosting = false;
		bool pickDate = false;
		cs::String dateText;
		cs::String seedText;
		Core::StartSituation situation = Core::StartSituation::GodView;
		/// US-T317: Enter was pressed; the next Draw starts the game if the day is valid.
		bool keyStart = false;

		/// The typed day, or NaN while it is not one (pickDate only).
		double PickedJd() const;
		int Seed() const;
	};
}
