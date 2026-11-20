// US-T99 (docs/REWIND.md, phases 2 and 3): time reversal's keyframe ring in the game, the frame log, seeking by replay
// and the timeline bar. Members are declared in UnitySpaceApp.Rewind.inl; the ring itself is Core's RewindRing
// (Persistence/RewindRing.h).

#include "UnitySpaceApp.h"
#include "UI/Hud.h"
#include "UI/UiMotion.h"
#include "Coop/CoopPeer.h"
#include "Input/Gamepad.h"
#include "Input/InputDevice.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <exception>

#include "Astro.h"
#include "Fleet.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/War.h"
#include "Persistence/SaveGame.h"
#include "Systems/FtlNavigation.h"
#include "Systems/Galaxy.h"
#include "Systems/HelmFtl.h"

#include "Bootstrap.h"
#include "Camera/StarTravel.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// "3 min", "5.2 h", "12 days": how far back a keyframe is, in game time.
		cs::String RewindSpan(double s)
		{
			s = std::fabs(s);
			if (s < 120) return cs::Cat(cs::Fmt(s, "F0"), " s");
			if (s < 2 * 3600) return cs::Cat(cs::Fmt(s / 60, "F0"), " min");
			if (s < 2 * 86400) return cs::Cat(cs::Fmt(s / 3600, "F1"), " h");
			if (s < 2 * 365.25 * 86400) return cs::Cat(cs::Fmt(s / 86400, "F1"), " days");
			return cs::Cat(cs::Fmt(s / (365.25 * 86400), "F1"), " years");
		}

		cs::String RewindMb(size_t bytes) { return cs::Cat(cs::Fmt(bytes / (1024.0 * 1024.0), "F1"), " MB"); }
	}

	bool UnitySpaceApp::RewindBattle() const
	{
		const auto& w = World();
		return !Piloted.IsNone() || (w != nullptr && w->War != nullptr && w->War->Battles.Count() > 0);
	}

	void UnitySpaceApp::UpdateRewind()
	{
		if (!rewindInit)
		{
			rewindInit = true;
			rewindEnabled = !Bootstrap::HasCommandLineFlag("-norewind") && !Bootstrap::HasCommandLineFlag("-demo") && !Bootstrap::HasCommandLineFlag("-capture");
		}
		if (Clock == nullptr || Galaxy == nullptr)
		{
			rewindFrameStarted = false;
			return;
		}
		// The timeline's game time: what the clock moved forward (a clock set back by hand does not run the session back).
		if (!std::isnan(rewindLastJd) && Clock->Jd > rewindLastJd) rewindSessionS += (Clock->Jd - rewindLastJd) * 86400.0;
		rewindLastJd = Clock->Jd;

		double wall = Time::realtimeSinceStartupAsDouble;
		if (rewindReplaying)
		{
			// This frame was the logged one: on to the next, until the seek arrives.
			rewindFrameStarted = false;
			if (const RewindFrame* f = RewindFrameAt(rewindReplayNext))
			{
				// The rest of this frame (its adventures) reads the logged frame's.
				rewindAdventureSerial = f->Serial;
				rewindAdventureFromLog = true;
				if (Clock->Jd != f->JdAfter) rewindReplayDrift++;
				// The world itself, where the play kept a check: the replay is exact only while every one matches.
				if (f->Check != 0)
				{
					rewindReplayChecks++;
					if (RewindWorldCheck() != f->Check && rewindReplayMismatches++ == 0) rewindReplayFirstMismatchS = f->SessionS;
				}
			}
			rewindReplayNext++;
			if (rewindReplayNext >= rewindReplayStop) FinishRewindReplay(false);
			return;
		}
		LogRewindFrame();

		if (pendingRewindId >= 0 || pendingRewindBackS >= 0 || pendingRewindSeekS >= 0)
		{
			int id = pendingRewindId;
			double backS = pendingRewindBackS, seekS = pendingRewindSeekS;
			pendingRewindId = -1;
			pendingRewindBackS = -1;
			pendingRewindSeekS = -1;
			if (id >= 0) RewindTo(id);
			else if (seekS >= 0) RewindSeek(seekS);
			else RewindBack(backS);
			return;
		}
		if (Input.Rewind)
		{
			ShowRewindBar(8);
			if (!rewindEnabled && Rewind.Keyframes().empty()) ShowToast("Rewind is off in this run (-norewind, -demo, -capture, or a keyframe failed)");
			else
			{
				// Pressed again soon after a rewind: one further back from the moment rewound to, not from now.
				const RewindKeyframe* headK = Rewind.Head() >= 0 && wall - Rewind.HeadWallS() < RewindStepWindowS ? Rewind.Find(Rewind.Head()) : nullptr;
				const RewindKeyframe* k = Rewind.Before(headK != nullptr ? headK->SessionS : rewindSessionS - 1.0);
				if (k != nullptr) RewindTo(k->Id);
				else ShowToast(Rewind.Keyframes().empty() ? "Nothing to rewind to yet" : "This is as far back as the session goes");
			}
			return;
		}

		// Only a moment a save could be made of: not between the stars, under FTL or latched to a hulk (SaveBlocker).
		if (!rewindEnabled || !SaveBlocker().empty() || !Rewind.Due(rewindSessionS, wall, RewindBattle())) return;
		try
		{
			const RewindKeyframe& k = Rewind.Capture(CaptureSave(), rewindSessionS, Clock->Jd, wall, RewindBattle());
			// Phase 3: the frame this keyframe's world steps into next, where a seek from it starts replaying.
			rewindKeySerial[k.Id] = rewindNextSerial;
			for (auto it = rewindKeySerial.begin(); it != rewindKeySerial.end();)
				it = Rewind.Find(it->first) == nullptr ? rewindKeySerial.erase(it) : std::next(it);
		}
		catch (const std::exception& e)
		{
			// A keyframe that cannot be taken costs the rewind, never the game: keyframing stops for this run rather than
			// walking the world again every frame (the keyframes already kept can still be rewound to).
			Debug::LogError(cs::Cat("[Unity.SpaceGod] Rewind keyframe failed: ", e.what()));
			rewindEnabled = false;
		}
	}

	bool UnitySpaceApp::RewindBack(double backS)
	{
		const RewindKeyframe* k = Rewind.Back(rewindSessionS, backS);
		if (k == nullptr)
		{
			ShowToast("Nothing to rewind to yet");
			return false;
		}
		return RewindTo(k->Id);
	}

	bool UnitySpaceApp::RewindTo(int id)
	{
		// A rewind is a load in place, so it is the host's in co-op; a client's request through the lockstep stream (any
		// player may rewind, the owner's call of 2026-11-05) is still to come (docs/REWIND.md, Build phases).
		if (CoopIsClient())
		{
			ShowToast("In co-op only the host can rewind for now");
			return false;
		}
		if (Travel->Active || InFtlCruise() || Ftl->Stage != FtlStage::Idle)
		{
			ShowToast(SaveBlocker());
			return false;
		}
		const RewindKeyframe* k = Rewind.Find(id);
		if (k == nullptr)
		{
			ShowToast("That moment is no longer kept");
			return false;
		}
		double sessionS = k->SessionS, jd = k->Jd, back = rewindSessionS - k->SessionS;
		cs::Ref<SaveData> data;
		try
		{
			data = Rewind.Restore(id, Time::realtimeSinceStartupAsDouble);
		}
		catch (const std::exception& e)
		{
			Debug::LogError(cs::Cat("[Unity.SpaceGod] Rewind to keyframe ", id, " failed: ", e.what()));
			ShowToast(cs::Cat("Could not rewind: ", e.what()));
			return false;
		}
		if (data == nullptr)
		{
			ShowToast("Could not rewind: the keyframe holds no game");
			return false;
		}
		rewindRestoring = true;
		CoopLoadingHere([this, &data] { ApplyLoaded(data); }); // the host's world is everyone's: the co-op resync follows
		rewindRestoring = false;
		rewindSessionS = sessionS;
		rewindLastJd = Clock->Jd;
		// Phase 3: the log goes on from this keyframe's frame (the old future is dropped as play goes on); a keyframe older
		// than the log starts it afresh.
		auto serial = rewindKeySerial.find(id);
		if (serial != rewindKeySerial.end()) rewindNextSerial = serial->second;
		else rewindFrames.clear();
		rewindReplaying = false;
		ShowRewindBar(8);
		ShowSaves = false;
		ShowToast(cs::Cat("Rewound ", RewindSpan(back), " to ", Astro::ToDateTime(jd).ToString("yyyy-MM-dd HH:mm"),
			" UTC  (Shift+F8 again to go further back; play on and the old future is replaced)"));
		UI::UiMotion::FadeFromBlack();
		return true;
	}

	void UnitySpaceApp::RewindAfterLoad()
	{
		if (rewindRestoring) return;
		Rewind.Clear();
		rewindSessionS = 0;
		rewindLastJd = Clock != nullptr ? Clock->Jd : std::numeric_limits<double>::quiet_NaN();
		rewindFrames.clear();
		rewindKeySerial.clear();
		rewindReplaying = false;
		rewindFrameStarted = false;
	}

	// ---------------------------------------------------------------- phase 3: the frame log and seeking by replay

	bool UnitySpaceApp::RewindFrameStart(float& dt)
	{
		rewindAdventureSerial = -1;
		rewindAdventureFromLog = false;
		rewindFrameStarted = Clock != nullptr;
		if (Clock == nullptr) return false;
		if (!rewindReplaying)
		{
			rewindFrameDt = dt;
			rewindFrameClock = Coop::ClockState::Of(*Clock);
			rewindFrameSessionS = rewindSessionS;
			// Anything the player did this frame may have changed the world; the log cannot replay it yet, so a seek stops
			// short of it. A mouse moved with nothing held only hovers.
			const Input::InputDevice& device = Input::InputDevice::current();
			bool touched = device.present && (device.scrollY != 0 || device.leftButton.isPressed || device.rightButton.isPressed ||
				device.middleButton.isPressed || device.leftButton.wasReleasedThisFrame || device.rightButton.wasReleasedThisFrame);
			for (int k = 1; !touched && device.present && k < static_cast<int>(Input::Key::Count); k++)
			{
				const auto& key = device[static_cast<Input::Key>(k)];
				touched = key.isPressed || key.wasPressedThisFrame || key.wasReleasedThisFrame;
			}
			rewindFrameIntervened = touched || Input::GamepadDevice::current().Touched() || !Input.HandsIdle();
			return false;
		}
		// Esc stops the replay where it is: that moment is as real as the one aimed at.
		const RewindFrame* f = RewindFrameAt(rewindReplayNext);
		if (Input.Escape || f == nullptr)
		{
			FinishRewindReplay(true);
			return true;
		}
		dt = f->Dt;
		f->Before.ApplyTo(*Clock);
		return true;
	}

	void UnitySpaceApp::LogRewindFrame()
	{
		bool logging = rewindFrameStarted && rewindEnabled && Coop::CoopPeer::Current() == nullptr;
		rewindFrameStarted = false;
		if (!logging) return;
		// Play went on from an earlier moment: what the log held after it was the old future.
		while (!rewindFrames.empty() && rewindFrames.back().Serial >= rewindNextSerial) rewindFrames.pop_back();
		if (!rewindFrames.empty() && rewindFrames.back().Serial != rewindNextSerial - 1) rewindFrames.clear();
		RewindFrame f;
		f.Serial = rewindNextSerial++;
		f.SessionS = rewindFrameSessionS;
		f.Dt = rewindFrameDt;
		f.Before = rewindFrameClock;
		f.JdAfter = Clock->Jd;
		f.Intervened = rewindFrameIntervened;
		if (f.Serial % RewindCheckEvery == 0) f.Check = RewindWorldCheck();
		rewindFrames.push_back(f);
		while (rewindFrames.size() > RewindMaxFrames) rewindFrames.pop_front();
		rewindAdventureSerial = f.Serial; // this frame's adventures, later in it, are noted on it
	}

	void UnitySpaceApp::RewindAdventures(bool wanted)
	{
		// A replayed frame runs the adventures it ran, round the point it ran them round: the camera a seek lands with
		// need not stand where it stood (co-op's client does the same with the host's FrameRecord).
		if (rewindAdventureFromLog)
		{
			if (const RewindFrame* f = RewindFrameAt(rewindAdventureSerial))
			{
				if (f->Adventures) World()->UpdateAdventures(Clock->Jd, f->AdventurePoint);
				return;
			}
		}
		// Live: exactly the original's line, and the frame logged this frame notes what it did.
		DVec3 at;
		if (wanted)
		{
			at = PlayerHelioKm();
			World()->UpdateAdventures(Clock->Jd, at);
		}
		if (rewindAdventureSerial >= 0 && !rewindFrames.empty() && rewindFrames.back().Serial == rewindAdventureSerial)
		{
			rewindFrames.back().Adventures = wanted;
			rewindFrames.back().AdventurePoint = at;
		}
	}

	unsigned long long UnitySpaceApp::RewindWorldCheck()
	{
		// FNV-1a over co-op's world digest (clock, every system's counts and treasuries, the watched system's ships and
		// settlements): what a desync check compares, so a replay that matches it re-lived the play as a client would.
		cs::String d = CoopDigest();
		unsigned long long h = 1469598103934665603ULL;
		for (unsigned char c : d)
		{
			h ^= c;
			h *= 1099511628211ULL;
		}
		return h == 0 ? 1 : h;
	}

	const UnitySpaceApp::RewindFrame* UnitySpaceApp::RewindFrameAt(long long serial) const
	{
		if (rewindFrames.empty() || serial < rewindFrames.front().Serial || serial > rewindFrames.back().Serial) return nullptr;
		return &rewindFrames[static_cast<size_t>(serial - rewindFrames.front().Serial)];
	}

	bool UnitySpaceApp::RewindSeek(double targetS)
	{
		targetS = std::max(0.0, targetS);
		const RewindKeyframe* k = Rewind.Before(targetS + 1e-6);
		if (k == nullptr)
		{
			ShowToast(Rewind.Keyframes().empty() ? "Nothing to rewind to yet" : "That is before the session began");
			return false;
		}
		int id = k->Id;
		double fromS = k->SessionS;
		auto serial = rewindKeySerial.find(id);
		long long start = serial != rewindKeySerial.end() ? serial->second : -1;
		if (!RewindTo(id)) return false;
		if (targetS <= fromS + 1e-3 || start < 0 || Coop::CoopPeer::Current() != nullptr) return true;

		// The frames to replay: from the keyframe's on, until the one that reaches the target, the first the player acted
		// on, a gap in the log, or the replay cap.
		long long stop = start;
		double wallS = 0;
		rewindReplayWhy = "";
		for (;; stop++)
		{
			const RewindFrame* f = RewindFrameAt(stop);
			if (f == nullptr) { rewindReplayWhy = "the log ends there"; break; }
			if (f->SessionS >= targetS - 1e-9) break;
			if (f->Intervened) { rewindReplayWhy = "you acted there (your input is not replayed yet)"; break; }
			if (wallS + f->Dt > RewindMaxReplayS) { rewindReplayWhy = cs::Cat("a seek replays at most ", cs::Fmt(RewindMaxReplayS, "F0"), " s of play"); break; }
			wallS += f->Dt;
		}
		if (stop == start)
		{
			if (!rewindReplayWhy.empty()) ShowToast(cs::Cat("Rewound to the keyframe: ", rewindReplayWhy));
			return true;
		}
		rewindReplaying = true;
		rewindReplayStart = rewindReplayNext = start;
		rewindReplayStop = stop;
		rewindReplayTargetS = targetS;
		rewindReplayDrift = 0;
		rewindReplayChecks = rewindReplayMismatches = 0;
		rewindReplayFirstMismatchS = -1;
		ShowToast(cs::Cat("Replaying ", RewindSpan(wallS), " of play to the moment picked  (Esc stops here)"));
		return true;
	}

	void UnitySpaceApp::FinishRewindReplay(bool stopped)
	{
		rewindReplaying = false;
		// Play goes on from here: the frames after it are the old future, dropped as the log goes on.
		rewindNextSerial = rewindReplayNext;
		if (rewindReplayDrift > 0)
			Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Rewind replay: the clock differed from the log on ", rewindReplayDrift, " of ",
				rewindReplayNext - rewindReplayStart, " frames"));
		// Whether the replay re-lived the play: its world checks against the ones the play kept.
		cs::String exact = rewindReplayChecks == 0 ? cs::String("not checked (too short)")
			: rewindReplayMismatches == 0 ? cs::Cat("exact (", rewindReplayChecks, " checks matched)")
			: cs::Cat("drifted from the play from ", RewindSpan(rewindReplayFirstMismatchS), " into the session (", rewindReplayMismatches,
				" of ", rewindReplayChecks, " checks differ)");
		rewindLastVerdict = cs::Cat(rewindReplayNext - rewindReplayStart, " frames replayed, ", exact);
		if (rewindReplayMismatches > 0) Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Rewind replay ", rewindLastVerdict));
		else Debug::Log(cs::Cat("[Unity.SpaceGod] Rewind replay ", rewindLastVerdict));
		cs::String when = Astro::ToDateTime(Clock->Jd).ToString("yyyy-MM-dd HH:mm:ss");
		cs::String tail = rewindReplayMismatches > 0 ? cs::String("  (the replay drifted from the play: sg.Rewind list)") : cs::String();
		if (stopped) ShowToast(cs::Cat("Replay stopped at ", when, " UTC", tail));
		else if (!rewindReplayWhy.empty()) ShowToast(cs::Cat("Replayed to ", when, " UTC, short of the moment picked: ", rewindReplayWhy, tail));
		else ShowToast(cs::Cat("Arrived at ", when, " UTC  (play on and the old future is replaced)", tail));
		ShowRewindBar(6);
	}

	void UnitySpaceApp::ShowRewindBar(double seconds)
	{
		rewindBarUntil = std::max(rewindBarUntil, Time::realtimeSinceStartupAsDouble + seconds);
	}

	void UnitySpaceApp::DrawRewindBar()
	{
		const auto& frames = Rewind.Keyframes();
		double wall = Time::realtimeSinceStartupAsDouble;
		if (frames.empty() || (!rewindReplaying && wall > rewindBarUntil) || ScreenCleared) return;
		if (rewindBarPanel == nullptr)
		{
			rewindBarBack = UI::Hud::Solid(Color(0.03f, 0.05f, 0.09f, 0.86f), Color(0.5f, 0.72f, 1.0f, 0.7f));
			rewindBarLog = UI::Hud::Solid(Color(0.3f, 0.55f, 0.9f, 0.45f), Color(0.3f, 0.55f, 0.9f, 0.45f));
			rewindBarKey = UI::Hud::Solid(Color(0.85f, 0.92f, 1.0f, 0.95f), Color(0.85f, 0.92f, 1.0f, 0.95f));
			rewindBarNow = UI::Hud::Solid(Color(1.0f, 0.78f, 0.3f, 1.0f), Color(1.0f, 0.78f, 0.3f, 1.0f));
			rewindBarHover = UI::Hud::Solid(Color(1.0f, 1.0f, 1.0f, 0.6f), Color(1.0f, 1.0f, 1.0f, 0.6f));
			rewindBarPanel = cs::New<GUIStyle>();
			rewindBarPanel->normal.background = rewindBarBack;
			rewindBarPanel->border = RectOffset(2, 2, 2, 2);
			rewindBarText = cs::New<GUIStyle>();
			rewindBarText->fontSize = 12;
			rewindBarText->alignment = TextAnchor::MiddleCenter;
			rewindBarText->normal.textColor = UI::Hud::Dim;
			rewindBarText->clipping = TextClipping::Clip;
		}

		double endS = std::max(rewindSessionS, frames.back()->SessionS);
		if (!rewindFrames.empty()) endS = std::max(endS, rewindFrames.back().SessionS);
		if (endS <= 0) endS = 1;
		float w = std::min(900.0f, Screen::width - 80.0f), h = 54;
		auto panel = Rect((Screen::width - w) * 0.5f, 70, w, h);
		auto strip = Rect(panel.x + 12, panel.y + 8, w - 24, 14);
		if (!UiRects.Contains(panel)) UiRects.Add(panel);
		GUI::Box(panel, GUIContent::none(), rewindBarPanel);
		auto xOf = [&](double s) { return strip.x + static_cast<float>(cs::Math::Clamp(s / endS, 0.0, 1.0)) * strip.width; };

		// The replayable stretch (the frame log), the keyframes, and where the session is now.
		if (!rewindFrames.empty())
		{
			float a = xOf(rewindFrames.front().SessionS), b = xOf(rewindFrames.back().SessionS);
			GUI::DrawTexture(Rect(a, strip.y + 4, std::max(2.0f, b - a), strip.height - 8), rewindBarLog);
		}
		for (const auto& k : frames) GUI::DrawTexture(Rect(xOf(k->SessionS) - 1, strip.y, k->Pinned ? 3.0f : 2.0f, strip.height), rewindBarKey);
		double nowS = rewindReplaying ? rewindReplayTargetS : rewindSessionS;
		GUI::DrawTexture(Rect(xOf(rewindSessionS) - 1.5f, strip.y - 3, 3, strip.height + 6), rewindBarNow);

		cs::String line;
		Event& e = Event::current();
		auto hit = Rect(strip.x, strip.y - 4, strip.width, strip.height + 8);
		if (rewindReplaying)
		{
			long long total = std::max<long long>(1, rewindReplayStop - rewindReplayStart);
			double done = static_cast<double>(rewindReplayNext - rewindReplayStart) / static_cast<double>(total);
			GUI::DrawTexture(Rect(xOf(nowS) - 1, strip.y - 3, 2, strip.height + 6), rewindBarHover);
			line = cs::Cat("Replaying to ", RewindSpan(rewindReplayTargetS - frames.front()->SessionS), " into the session: ",
				cs::Fmt(done * 100, "F0"), "%  (Esc stops here)");
		}
		else if (hit.Contains(e.mousePosition))
		{
			ShowRewindBar(2);
			double s = (e.mousePosition.x - strip.x) / strip.width * endS;
			GUI::DrawTexture(Rect(e.mousePosition.x - 1, strip.y - 3, 2, strip.height + 6), rewindBarHover);
			const RewindKeyframe* k = Rewind.Before(s + 1e-6);
			bool exact = k != nullptr && rewindKeySerial.count(k->Id) != 0 && !rewindFrames.empty() && s <= rewindFrames.back().SessionS;
			line = cs::Cat("Click to go back ", RewindSpan(rewindSessionS - s), exact ? " (replayed from the keyframe before)" : " (lands on the keyframe before)");
			if (e.type == EventType::MouseDown && e.button == 0)
			{
				RequestRewindSeek(s);
				e.Use();
			}
		}
		else
			line = cs::Cat("Timeline: ", frames.size(), " keyframes over ", RewindSpan(endS), ", the blue stretch replays to any moment  ·  Shift+F8 the last keyframe  ·  click a moment");
		GUI::Label(Rect(panel.x, strip.yMax() + 4, w, 22), line, rewindBarText);
	}

	cs::String UnitySpaceApp::DescribeRewind() const
	{
		const auto& frames = Rewind.Keyframes();
		cs::String text = cs::Cat("Rewind: ", frames.size(), " keyframes, ", RewindMb(Rewind.Bytes()), " of ", RewindMb(Rewind.BudgetBytes),
			", now ", RewindSpan(rewindSessionS), " into the session", rewindEnabled ? "" : " (off in this run)");
		size_t acted = 0;
		for (const auto& f : rewindFrames) acted += f.Intervened ? 1 : 0;
		text += rewindFrames.empty() ? cs::String("\n  frame log: empty (co-op, or nothing played yet)")
			: cs::Cat("\n  frame log: ", rewindFrames.size(), " frames, ", RewindSpan(rewindFrames.back().SessionS - rewindFrames.front().SessionS),
				" of game time (from ", RewindSpan(rewindSessionS - rewindFrames.front().SessionS), " back), ", acted, " with your input");
		if (!rewindLastVerdict.empty()) text += cs::Cat("\n  last replay: ", rewindLastVerdict);
		for (const auto& k : frames)
		{
			text += cs::Cat("\n  #", k->Id, "  ", Astro::ToDateTime(k->Jd).ToString("yyyy-MM-dd HH:mm:ss"), "  ", RewindSpan(rewindSessionS - k->SessionS),
				rewindSessionS >= k->SessionS ? " back" : " ahead", "  ", RewindMb(k->StoredBytes()), "  capture ", cs::Fmt(k->CaptureMs, "F0"), " ms",
				k->Pinned ? "  [start]" : "", k->Battle ? "  [battle]" : "", k->Id == Rewind.Head() ? "  <- here" : "");
		}
		return text;
	}
}
