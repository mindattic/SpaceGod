#pragma once

// US-T147: the showcase tour, one guided run through every feature in the game, in chapters, so nothing has to be launched
// as a demo of its own. Its stops come from a registry (TourRegistry) as the dev menu's entries do (US-T126): a feature adds
// its own from its own file with one TourRegistry::Add, never an edit to the tour. A stop says what to look at (a title and
// a one-line caption on screen), how to set it up (teleport, spawn, embody, scale, time: usually by calling the feature's own
// command or method), when the setup has landed, an optional scripted camera, and how long it lasts.
//
// Started from the main menu, the dev menu (Shift+F1 > Showcase) or the console (sg.Tour). While it runs it has the keys:
// Right / N next, Left / B previous, S skip the chapter, Space pause, 1-9 a chapter, O also show each stop at the other
// scale, Esc end it (the game is left as the stop left it). sg.Tour auto shots <dir> [quit] runs every stop unattended,
// saves a screenshot of each (at both scales where the stop asks), then writes a contact sheet.

#include "Cs/Cs.h"

#include <functional>
#include <vector>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Tour
{
	/// <summary>One stop of the tour (US-T147). Everything but the chapter, id, title and caption is optional.</summary>
	struct TourStop
	{
		/// <summary>The chapter it belongs to (<see cref="TourRegistry.DeclareChapter"/>), and its place in it (lower first).</summary>
		cs::String Chapter;
		int Order = 0;

		/// <summary>A short file-safe name ("flight-dogfight"): its screenshot's name, and how a later registration with the
		/// same id replaces it (a feature landing over its placeholder).</summary>
		cs::String Id;

		cs::String Title;

		/// <summary>One line on screen saying what to look at.</summary>
		cs::String Caption;

		/// <summary>The system it plays in: <see cref="Home"/> (the one the tour started in, Sol in a new game), a system's name,
		/// or empty for wherever the tour is.</summary>
		cs::String System = Home;
		static inline const cs::String Home = "@home";

		/// <summary>The scale it opens at: 1 the game scale, 0 the realistic, -1 as it is.</summary>
		int Scale = 1;

		/// <summary>Shown (and shot, in auto mode) at the other scale too, after its own.</summary>
		bool BothScales = false;

		/// <summary>Seconds it is shown for, once its setup has landed; and the longest it waits for that.</summary>
		double Seconds = 8;
		double ReadyTimeout = 60;

		/// <summary>The helm or seat taken at the stop before is kept (the tour lets go of it before every other stop).</summary>
		bool KeepSeat = false;

		/// <summary>Not built yet: the ticket that will fill it in. Shown as a card saying what is coming, never shot.</summary>
		cs::String Pending;

		/// <summary>Sets it up, once: an empty answer, or why it cannot be shown here (none in this galaxy...).</summary>
		std::function<cs::String(UnitySpaceApp&)> Setup;

		/// <summary>Whether the setup has landed (a trip arrived, a seat taken), <paramref name="t"/> real seconds after it ran.
		/// Unset: at once.</summary>
		std::function<bool(UnitySpaceApp&, double)> Ready;

		/// <summary>The scripted camera (or any per-frame action), each frame it is shown, <paramref name="t"/> seconds in (paused
		/// time does not count). It runs again from 0 for the other scale's pass.</summary>
		std::function<void(UnitySpaceApp&, double)> Camera;

		/// <summary>US-T173: flies the craft the stop put the helm in. The tour idles the player's input every frame it runs
		/// (the keys are its own), so a stop at a helm would otherwise drift; this runs after that idling, each frame from the
		/// setup on (ready wait, shown, shot and the other scale's pass), and writes the frame's input (<c>app.Input</c>, the
		/// aim) itself, <paramref name="t"/> real seconds since the stop was set up.</summary>
		std::function<void(UnitySpaceApp&, double)> Helm;

		static TourStop Make(const cs::String& chapter, const cs::String& id, const cs::String& title, const cs::String& caption);

		/// <summary>A stop set up by a console command (<c>sg.BioMass here</c>): the feature's own setup, by one line. It says it
		/// is not in this build until a command of that name is.</summary>
		static TourStop Console(const cs::String& chapter, const cs::String& id, const cs::String& title, const cs::String& caption, const cs::String& command);

		/// <summary>A placeholder for a feature being built (<paramref name="ticket"/>): the agent building it registers the
		/// real stop under the same <paramref name="id"/> and it takes this one's place.</summary>
		static TourStop Placeholder(const cs::String& chapter, const cs::String& id, const cs::String& title, const cs::String& caption, const cs::String& ticket);

		TourStop& WithSetup(std::function<cs::String(UnitySpaceApp&)> f) { Setup = std::move(f); return *this; }
		TourStop& Until(std::function<bool(UnitySpaceApp&, double)> f, double timeout = 60) { Ready = std::move(f); ReadyTimeout = timeout; return *this; }
		TourStop& WithCamera(std::function<void(UnitySpaceApp&, double)> f) { Camera = std::move(f); return *this; }
		TourStop& WithHelm(std::function<void(UnitySpaceApp&, double)> f) { Helm = std::move(f); return *this; }
		TourStop& For(double seconds) { Seconds = seconds; return *this; }
		TourStop& In(const cs::String& system) { System = system; return *this; }
		TourStop& AtScale(int scale) { Scale = scale; return *this; }
		TourStop& AtBothScales() { BothScales = true; return *this; }
		TourStop& KeepingSeat() { KeepSeat = true; return *this; }
		TourStop& At(int order) { Order = order; return *this; }
	};

	/// <summary>
	/// What the tour shows, by chapter (US-T147). A feature adds its own stops from its own file:
	/// <code>static Tour::TourRegistration reg([] { Tour::TourRegistry::Add(Tour::TourStop::Console("Threats", "biomass", ...)); });</code>
	/// Chapters are played in their order (<see cref="DeclareChapter"/>; an undeclared one goes last), stops by their order
	/// within it, then in the order added.
	/// </summary>
	class TourRegistry final
	{
	public:
		struct Chapter
		{
			cs::String Name;
			cs::String Blurb;
			int Order = 1000;
			std::vector<TourStop> Stops;
		};

		static void DeclareChapter(const cs::String& name, int order, const cs::String& blurb);

		/// <summary>Adds <paramref name="stop"/>; one with the same id already there (a placeholder) is replaced, in place.</summary>
		static void Add(TourStop stop);

		static const std::vector<Chapter>& Chapters();

		/// <summary>Every stop in the order played, with its chapter's index.</summary>
		static std::vector<std::pair<int, const TourStop*>> Flat();

		/// <summary>Runs <paramref name="registerStops"/> the first time the chapters are asked for (not at load).</summary>
		static void Defer(void (*registerStops)());

	private:
		static std::vector<Chapter>& Mutable();
		static std::vector<void (*)()>& Pending();
		static Chapter& Find(const cs::String& name);
	};

	/// <summary>Registers stops (see <see cref="TourRegistry"/>): a file-scope one of these in a feature's own file.</summary>
	struct TourRegistration
	{
		explicit TourRegistration(void (*registerStops)()) { TourRegistry::Defer(registerStops); }
	};

	/// <summary>
	/// The tour itself (US-T147): one at a time, driven from the app's frame (Update, Draw). Its state is the module's own,
	/// not the app's, so it survives nothing but the game it was started in: a new game or the main menu ends it.
	/// </summary>
	class ShowcaseTour final
	{
	public:
		/// <summary>Starts at stop <paramref name="first"/> of the flat order (0: the beginning). In <paramref name="autoShots"/>
		/// mode every stop plays unattended and is screenshot into <paramref name="shotsDir"/>, then a contact sheet is written
		/// there, and the game quits if <paramref name="quit"/>.</summary>
		static void Start(UnitySpaceApp& app, int first = 0, bool autoShots = false, const cs::String& shotsDir = cs::String(), bool quit = false);

		/// <summary>The first stop of the chapter whose name starts with <paramref name="chapter"/> (ignoring case), or -1.</summary>
		static int ChapterStart(const cs::String& chapter);

		static void Stop(UnitySpaceApp& app);

		/// <summary>Its game is going (a new one, the main menu): ended quietly.</summary>
		static void Abandon();

		static bool Running();

		/// <summary>Each frame, from the app's Update after the dev menu and the comms panel had their turn:
		/// <paramref name="keysFree"/> when neither is open. True while it runs (it has the keys: the caller idles the input).</summary>
		static bool Update(UnitySpaceApp& app, bool keysFree);

		/// <summary>The strip at the bottom of the screen: the chapter, the stop out of the total, the caption and the keys.</summary>
		static void Draw(UnitySpaceApp& app);

		static void DestroyOwnedTextures();
	};
}
