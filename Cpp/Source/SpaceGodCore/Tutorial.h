#pragma once
// US-T254: the guided first-run tutorial and the first-time hints, as data and a step machine (no engine code). The game
// (UnitySpaceApp.Tutorial) fills a TutorialSignals from its own state each frame and draws what Current() says; the
// steps and the hints' wording live here, so the order, the keys named and the completion rules are one place.

#include "Cs/Cs.h"

#include <set>
#include <vector>

namespace SpaceGod::Core
{
	/// <summary>The tutorial's steps, in the order played. Hail comes before Let go: comms speak as the unit held.</summary>
	enum class TutorialStep : int
	{
		SwimLook,
		Select,
		Focus,
		TimeWarp,
		Scale,
		Navigator,
		Embody,
		FlyFire,
		LockMissile,
		Hail,
		LetGo,
		Market,
		GalaxyMap,
		Count,
	};

	/// <summary>The HUD element a step points at (outlined while the step waits), or none.</summary>
	enum class TutorialHighlight : int
	{
		None,
		/// The clock's slower / pause / faster / now buttons on the top bar.
		TimeControls,
		/// The scale button and its blend bar on the top bar.
		ScaleControl,
		/// The NAVIGATOR panel's title bar, top left.
		Navigator,
		/// The crosshair in the middle of the screen.
		Reticle,
	};

	struct SPACEGODCORE_API TutorialStepInfo
	{
		TutorialStep Step = TutorialStep::Count;
		/// A short file-safe name ("swim"), for logs and the audit.
		const char* Id = "";
		const char* Title = "";
		/// The keys the step teaches, as the panel's key plate shows them.
		const char* Keys = "";
		const char* Text = "";
		TutorialHighlight Highlight = TutorialHighlight::None;
	};

	/// <summary>What the player did and where the game stands this frame, as the game saw it. Levels, except the two
	/// presses (edges this frame).</summary>
	struct SPACEGODCORE_API TutorialSignals
	{
		/// Real seconds since the last frame.
		float Dt = 0;
		/// The stick (WASD, rise and fall) pushed, in free swim or at the controls.
		bool Moving = false;
		/// The view turned with the right mouse held (free swim).
		bool Looking = false;
		/// The selection changed to something this frame.
		bool SelectedNew = false;
		/// The camera is focused on something (F).
		bool Focused = false;
		/// The clock's warp step, and the scale the view is going to (1 gameplay, 0 realistic).
		int WarpIndex = 0;
		double ScaleGoal = 1;
		bool NavigatorOpen = false;
		/// At a helm, a seat or any controls (embodied).
		bool AtControls = false;
		/// The forward guns' trigger held (left mouse, at the controls).
		bool Firing = false;
		/// T pressed at the controls this frame, or a hull is locked.
		bool LockPressed = false, Locked = false;
		/// The missile trigger (right mouse, at the controls) went down this frame.
		bool MissilePressed = false;
		bool CommsOpen = false;
		bool MarketOpen = false;
		bool GalaxyMapOpen = false;
	};

	/// <summary>
	/// The tutorial's progress: which step waits, and how far into it the player is. Update moves it on when the step's
	/// action has been seen; Skip moves on regardless; past the last step it is finished.
	/// </summary>
	class SPACEGODCORE_API Tutorial final
	{
	public:
		/// Every step, in the order played (TutorialStep's order).
		static const std::vector<TutorialStepInfo>& Steps();
		static int StepCount() { return static_cast<int>(TutorialStep::Count); }

		/// How long the stick must be pushed (and the view turned) to count as having swum, looked, flown, fired.
		static constexpr float SwimSeconds = 0.6f;
		static constexpr float LookSeconds = 0.3f;
		static constexpr float FlySeconds = 0.5f;
		static constexpr float FireSeconds = 0.15f;

		/// Starts at the first step; <paramref name="s"/> is the game as it stands (the warp and scale noted, so a change
		/// is what counts).
		void Start(const TutorialSignals& s);
		void Stop();

		bool Running() const { return running; }
		/// Ran past its last step (not stopped part way).
		bool Finished() const { return finished; }
		int Index() const { return index; }
		const TutorialStepInfo& Current() const;

		/// Moves on to the next step without waiting for its action (the last step finishes it).
		void Skip(const TutorialSignals& s);

		/// Feeds one frame; true on the frame a step was completed (the tutorial has moved on to the next, or finished).
		bool Update(const TutorialSignals& s);

		/// 0..1, how far into the current step's action the player is (the parts of a two-part step, the seconds held).
		float Progress() const;

		/// Real seconds since the step now waiting began (the panel flashes the step just done meanwhile).
		float StepAge() const { return stepAge; }

	private:
		bool running = false, finished = false;
		int index = 0;
		float stepAge = 0;
		/// Per step: the seconds the two parts have been seen (swim and look; fly and fire), or the two presses seen.
		float partA = 0, partB = 0;
		int warpAtStart = 0;
		double scaleAtStart = 1;

		void Begin(int step, const TutorialSignals& s);
		bool Done(const TutorialSignals& s);
	};

	/// <summary>The first-time hints, each said once per profile after the tutorial: the first battle, the first lane, the
	/// first jump gate in reach, the first void stop and the first fight aboard.</summary>
	enum class TutorialHint : int
	{
		FirstBattle,
		FirstLane,
		FirstGate,
		FirstVoidStop,
		FirstBoarding,
		Count,
	};

	struct SPACEGODCORE_API TutorialHintInfo
	{
		TutorialHint Hint = TutorialHint::Count;
		/// Its name in the profile's list of hints seen.
		const char* Id = "";
		const char* Title = "";
		const char* Text = "";
	};

	class SPACEGODCORE_API TutorialHints final
	{
	public:
		static const std::vector<TutorialHintInfo>& All();
		static const TutorialHintInfo& Of(TutorialHint hint);

		/// The profile's settings.json keys (its game block): whether the tutorial was offered on a first New Game, and the
		/// hints already seen, comma separated.
		static constexpr const char* OfferedKey = "tutorialOffered";
		static constexpr const char* SeenKey = "tutorialHintsSeen";

		/// The hints seen, from the profile's text ("" none); unknown names are kept so a later build's hints survive.
		static std::set<cs::String> Parse(const cs::String& text);
		static cs::String Format(const std::set<cs::String>& seen);
	};
}
