// US-T254: the tutorial's steps and the first-time hints (Tutorial.h).
#include "Tutorial.h"

#include <algorithm>

namespace SpaceGod::Core
{
	const std::vector<TutorialStepInfo>& Tutorial::Steps()
	{
		using H = TutorialHighlight;
		static const std::vector<TutorialStepInfo> steps =
		{
			{ TutorialStep::SwimLook, "swim", "Swim and look", "W A S D  \xC2\xB7  E / Q  \xC2\xB7  right mouse",
				"Fly the camera: W and S forward and back, A and D sideways, E and Q up and down (Shift for speed). Hold the right mouse button and drag to look about.", H::None },
			{ TutorialStep::Select, "select", "Select something", "Left click  \xC2\xB7  Tab",
				"Click a world, a moon or a ship to select it: its card opens at the bottom right. Tab steps through the ships here, 1 to 0 the major worlds.", H::None },
			{ TutorialStep::Focus, "focus", "Focus on it", "F",
				"Press F to put the camera on what you selected and orbit it: drag with the right mouse to circle it, the wheel zooms. Esc lets go again.", H::None },
			{ TutorialStep::TimeWarp, "warp", "Time warp", ".  /  ,",
				"Press . to speed time up and , to slow it down (Space pauses). The clock at the top shows the date and the rate.", H::TimeControls },
			{ TutorialStep::Scale, "scale", "Scale", "G",
				"Press G to swap between the gameplay scale, worlds big and close together, and the true scale of space.", H::ScaleControl },
			{ TutorialStep::Navigator, "navigator", "Navigator", "click NAVIGATOR",
				"Open the NAVIGATOR at the top left: every star system, and the worlds and ships in this one, to select or fly to with a click.", H::Navigator },
			{ TutorialStep::Embody, "embody", "Embody a fighter", "P  \xC2\xB7  Shift+V",
				"Select a fighter and press P to take its helm -- or press Shift+V (or the button below) for a practice fighter at the camera, with you aboard.", H::None },
			{ TutorialStep::FlyFire, "fly", "Fly and fire", "mouse  \xC2\xB7  W / S  \xC2\xB7  left mouse",
				"The mouse steers, W and S throttle, A, D, Q and E slide, Z and X roll. Hold the left mouse button to fire the forward guns.", H::Reticle },
			{ TutorialStep::LockMissile, "lock", "Lock and missiles", "T  \xC2\xB7  right mouse",
				"Hold the crosshair on a hull and press T to lock it. The right mouse fires homing missiles at the lock (rockets straight down the nose without one).", H::Reticle },
			{ TutorialStep::Hail, "hail", "Hail", "Shift+H",
				"Press Shift+H for comms: hail any ship or station in range, as the unit you hold. Shift+H again closes it.", H::None },
			{ TutorialStep::LetGo, "letgo", "Let go", "P",
				"Press P again to let go of the controls and float free as the SpaceGod once more.", H::None },
			{ TutorialStep::Market, "market", "Market", "M",
				"Press M for the market board of the port in view: what it buys and sells, and at what price. M again closes it.", H::None },
			{ TutorialStep::GalaxyMap, "galaxy", "Galaxy map", "[",
				"Press [ for the galaxy map: every star, who holds it, its trade and its wars. Shift+[ is the map of this system.", H::None },
		};
		return steps;
	}

	const TutorialStepInfo& Tutorial::Current() const
	{
		const auto& steps = Steps();
		return steps[std::clamp(index, 0, static_cast<int>(steps.size()) - 1)];
	}

	void Tutorial::Start(const TutorialSignals& s)
	{
		running = true;
		finished = false;
		Begin(0, s);
	}

	void Tutorial::Stop()
	{
		running = false;
	}

	void Tutorial::Begin(int step, const TutorialSignals& s)
	{
		index = step;
		stepAge = 0;
		partA = partB = 0;
		warpAtStart = s.WarpIndex;
		scaleAtStart = s.ScaleGoal;
	}

	void Tutorial::Skip(const TutorialSignals& s)
	{
		if (!running) return;
		if (index + 1 >= StepCount())
		{
			running = false;
			finished = true;
			return;
		}
		Begin(index + 1, s);
	}

	bool Tutorial::Update(const TutorialSignals& s)
	{
		if (!running) return false;
		stepAge += s.Dt;
		if (!Done(s)) return false;
		Skip(s);
		return true;
	}

	bool Tutorial::Done(const TutorialSignals& s)
	{
		switch (static_cast<TutorialStep>(index))
		{
			case TutorialStep::SwimLook:
				if (s.Moving && !s.AtControls) partA += s.Dt;
				if (s.Looking && !s.AtControls) partB += s.Dt;
				return partA >= SwimSeconds && partB >= LookSeconds;
			case TutorialStep::Select: return s.SelectedNew;
			case TutorialStep::Focus: return s.Focused;
			case TutorialStep::TimeWarp: return s.WarpIndex != warpAtStart;
			case TutorialStep::Scale: return s.ScaleGoal != scaleAtStart;
			case TutorialStep::Navigator: return s.NavigatorOpen;
			case TutorialStep::Embody: return s.AtControls;
			case TutorialStep::FlyFire:
				if (s.Moving && s.AtControls) partA += s.Dt;
				if (s.Firing && s.AtControls) partB += s.Dt;
				return partA >= FlySeconds && partB >= FireSeconds;
			case TutorialStep::LockMissile:
				if ((s.LockPressed || s.Locked) && s.AtControls) partA = 1;
				if (s.MissilePressed && s.AtControls) partB = 1;
				return partA > 0 && partB > 0;
			case TutorialStep::Hail: return s.CommsOpen;
			case TutorialStep::LetGo: return !s.AtControls;
			case TutorialStep::Market: return s.MarketOpen;
			case TutorialStep::GalaxyMap: return s.GalaxyMapOpen;
			default: return true;
		}
	}

	float Tutorial::Progress() const
	{
		switch (static_cast<TutorialStep>(index))
		{
			case TutorialStep::SwimLook: return 0.5f * std::min(1.0f, partA / SwimSeconds) + 0.5f * std::min(1.0f, partB / LookSeconds);
			case TutorialStep::FlyFire: return 0.5f * std::min(1.0f, partA / FlySeconds) + 0.5f * std::min(1.0f, partB / FireSeconds);
			case TutorialStep::LockMissile: return 0.5f * (partA > 0 ? 1.0f : 0.0f) + 0.5f * (partB > 0 ? 1.0f : 0.0f);
			default: return 0;
		}
	}

	// ---------------------------------------------------------------- the first-time hints

	const std::vector<TutorialHintInfo>& TutorialHints::All()
	{
		static const std::vector<TutorialHintInfo> hints =
		{
			{ TutorialHint::FirstBattle, "battle", "Your first battle",
				"Time is held at real speed while you fight. T locks the hull under the crosshair, left mouse fires the guns, right mouse the missiles; the arrow keys share the reactor between weapons, shields and engines." },
			{ TutorialHint::FirstLane, "lane", "Riding a hyperlane",
				"The hull rides the lane on its own, far faster than its drive. The course banner shows the next ramp; drop out early and Y rejoins the lane." },
			{ TutorialHint::FirstGate, "gate", "A jump gate in reach",
				"Press Y to go through the gate to the system at its far end. The galaxy map ([) shows where every gate leads." },
			{ TutorialHint::FirstVoidStop, "void", "A void stop",
				"Between jumps the drive recharges out in the dark. Tab pings (you see more, and are seen), Shift+F3 runs dark until it powers up again. Watch for ghosts closing." },
			{ TutorialHint::FirstBoarding, "boarding", "A fight aboard",
				"Marines are fighting through a hull's compartments. B opens the boarding panel: 1 picks a compartment, 2 sends the boarders at it (or musters the defenders), 3 holds, 0 hands it back." },
		};
		return hints;
	}

	const TutorialHintInfo& TutorialHints::Of(TutorialHint hint)
	{
		const auto& all = All();
		return all[std::clamp(static_cast<int>(hint), 0, static_cast<int>(all.size()) - 1)];
	}

	std::set<cs::String> TutorialHints::Parse(const cs::String& text)
	{
		std::set<cs::String> seen;
		size_t start = 0;
		while (start <= text.size())
		{
			size_t comma = text.find(',', start);
			if (comma == cs::String::npos) comma = text.size();
			cs::String id = text.substr(start, comma - start);
			id.erase(0, id.find_first_not_of(' '));
			id.erase(id.find_last_not_of(' ') + 1);
			if (!id.empty()) seen.insert(id);
			start = comma + 1;
		}
		return seen;
	}

	cs::String TutorialHints::Format(const std::set<cs::String>& seen)
	{
		cs::String text;
		for (const auto& id : seen)
		{
			if (id.empty()) continue;
			if (!text.empty()) text += ",";
			text += id;
		}
		return text;
	}
}
