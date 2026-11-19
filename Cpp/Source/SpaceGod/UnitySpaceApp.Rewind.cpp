// US-T99 (docs/REWIND.md, phase 2): time reversal's keyframe ring in the game. Members are declared in
// UnitySpaceApp.Rewind.inl; the ring itself is Core's RewindRing (Persistence/RewindRing.h).

#include "UnitySpaceApp.h"
#include "UI/UiMotion.h"

#include <cmath>
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
		if (Clock == nullptr || Galaxy == nullptr) return;
		// The timeline's game time: what the clock moved forward (a clock set back by hand does not run the session back).
		if (!std::isnan(rewindLastJd) && Clock->Jd > rewindLastJd) rewindSessionS += (Clock->Jd - rewindLastJd) * 86400.0;
		rewindLastJd = Clock->Jd;

		double wall = Time::realtimeSinceStartupAsDouble;
		if (pendingRewindId >= 0 || pendingRewindBackS >= 0)
		{
			int id = pendingRewindId;
			double backS = pendingRewindBackS;
			pendingRewindId = -1;
			pendingRewindBackS = -1;
			if (id >= 0) RewindTo(id);
			else RewindBack(backS);
			return;
		}
		if (Input.Rewind)
		{
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
			Rewind.Capture(CaptureSave(), rewindSessionS, Clock->Jd, wall, RewindBattle());
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
	}

	cs::String UnitySpaceApp::DescribeRewind() const
	{
		const auto& frames = Rewind.Keyframes();
		cs::String text = cs::Cat("Rewind: ", frames.size(), " keyframes, ", RewindMb(Rewind.Bytes()), " of ", RewindMb(Rewind.BudgetBytes),
			", now ", RewindSpan(rewindSessionS), " into the session", rewindEnabled ? "" : " (off in this run)");
		for (const auto& k : frames)
		{
			text += cs::Cat("\n  #", k->Id, "  ", Astro::ToDateTime(k->Jd).ToString("yyyy-MM-dd HH:mm:ss"), "  ", RewindSpan(rewindSessionS - k->SessionS),
				rewindSessionS >= k->SessionS ? " back" : " ahead", "  ", RewindMb(k->StoredBytes()), "  capture ", cs::Fmt(k->CaptureMs, "F0"), " ms",
				k->Pinned ? "  [start]" : "", k->Battle ? "  [battle]" : "", k->Id == Rewind.Head() ? "  <- here" : "");
		}
		return text;
	}
}
