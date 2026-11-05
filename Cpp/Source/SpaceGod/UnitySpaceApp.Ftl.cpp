// Port of Unity.SpaceGod Runtime/UnitySpaceApp.Ftl.cs (US-T29): the helm's own FTL drive, its crossing and the FTL
// navigator and autopilot. Members are declared in UnitySpaceApp.Ftl.inl. A C# null string is the empty string here.

#include "UnitySpaceApp.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "GalacticPosition.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/PilotFlight.h"
#include "Combat/Pursuit.h"
#include "Economy/Contracts.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Sensors/Sensors.h"
#include "Systems/CruiseDrive.h"
#include "Systems/DeepSpace.h"
#include "Systems/FtlNavigation.h"
#include "Systems/FtlRules.h"
#include "Systems/Galaxy.h"
#include "Systems/HelmFtl.h"
#include "Systems/VoidRaiders.h"

#include "Camera/SpaceCamera.h"
#include "Input/InputDevice.h"
#include "Rendering/AssetStreamer.h"
#include "Streaming/AreaStreaming.h"
#include "Views/InterstellarView.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// C#'s `a ?? b` on a string, with empty standing for null.
		cs::String FtlOrElse(const cs::String& a, const char* b) { return !a.empty() ? a : cs::String(b); }
	}

	bool UnitySpaceApp::CanPlotFtl()
	{
		return IsPiloting() && !RidingLane() && Piloted.Ship != nullptr && Core::HelmFtl::Refusal(Piloted.Ship).empty();
	}

	double UnitySpaceApp::FtlSecondsLeft()
	{
		double jd = Clock->Jd, end;
		if (InFtlCruise()) end = FtlCruise->ArriveJd();
		else if (Ftl->Stage == Core::FtlStage::Spooling) end = Ftl->ChargedJd;
		else if (FtlAutopilot->Stage == Core::FtlAutopilotStage::Cooling) end = Core::FtlRules::CoolUntil(Piloted.Ship);
		else if (FtlAutopilot->Stage == Core::FtlAutopilotStage::OutRun) end = FtlAutopilot->RunEndJd;
		else return 0;
		if (cs::Double::IsInfinity(end) || cs::Double::IsNaN(end)) return 0;
		return cs::Math::Max(0.0, end - jd) * Core::Astro::SecondsPerDay / cs::Math::Max(Clock->Warp().SimSecondsPerSecond, 1e-9);
	}

	double UnitySpaceApp::FtlSecondsTo(double end)
	{
		if (cs::Double::IsInfinity(end) || cs::Double::IsNaN(end)) return 0;
		return cs::Math::Max(0.0, end - Clock->Jd) * Core::Astro::SecondsPerDay / cs::Math::Max(Clock->Warp().SimSecondsPerSecond, 1e-9);
	}

	void UnitySpaceApp::PrefetchFtlDestination(const cs::Ref<Core::FtlTarget>& target)
	{
		if (target == nullptr) return;
		PrefetchFtlDestination(target->System, target->Name);
	}

	void UnitySpaceApp::PrefetchFtlDestination(const cs::Ref<Core::StarSystem>& system, const cs::String& name)
	{
		// The area streaming (US-T133) brings the drop-out in ahead of the jump: the running system, or the plain star
		// by its name, at the most urgent priority, so it is built before the player can see it.
		AreaStreaming::Prefetch(system, name, StreamPriority::Imminent);
	}

	DVec3 UnitySpaceApp::PilotRootKm()
	{
		return Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm();
	}

	double UnitySpaceApp::FtlRimAu()
	{
		return Core::HelmFtl::RimKm(ActiveSystem) / Core::Astro::AuKm;
	}

	double UnitySpaceApp::PilotFromStarAu()
	{
		return IsPiloting() ? PilotRootKm().Length() / Core::Astro::AuKm : 0;
	}

	// ---------------------------------------------------------------- at the helm

	bool UnitySpaceApp::UpdateHelmFtl(const cs::Ref<Core::Ship>& ship)
	{
		double jd = Clock->Jd;
		bool canJump = Core::HelmFtl::Refusal(ship).empty();
		FtlBeyondRim = Core::HelmFtl::BeyondRim(ActiveSystem, PilotRootKm());
		FtlPointed = canJump && Ftl->Stage == Core::FtlStage::Idle ? PointedTarget(PilotRootKm(), pilot().Forward) : nullptr;
		FtlBlocked = cs::String();
		if (FtlPointed != nullptr && FtlBeyondRim)
		{
			double km;
			Core::HelmFtl::Bearing(FtlPointed, ActiveSystem, PilotRootKm(), km);
			cs::String well = Core::HelmFtl::WellRefusal(ActiveSystem, PilotRootKm(), Galaxy);
			FtlBlocked = !well.empty() ? well : Core::HelmFtl::DriveRefusal(ship, km, jd);
		}

		// Enter is the manual charge; while the autopilot is turning onto the star it is the autopilot's to spool.
		if (Input.EngageFtl && !(FtlAutopilot->Active() && Ftl->Stage != Core::FtlStage::Spooling))
		{
			if (Ftl->Stage == Core::FtlStage::Spooling)
			{
				Ftl->Abort("Charge called off");
				EndFtlWarp();
				ShowToast("FTL charge called off");
				return false;
			}
			if (!Ftl->Spool(ship, ActiveSystem, FtlPointed, PilotRootKm(), pilot().Forward, ship->Battle != nullptr, jd, Galaxy))
			{
				ShowToast(Ftl->AbortReason);
				return false;
			}
			if (ftlWarpBefore < 0) ftlWarpBefore = Clock->WarpIndex;
			Clock->SetWarp(Core::SimClock::WarpToCover((Ftl->ChargedJd - jd) * Core::Astro::SecondsPerDay, FtlSpoolRealSeconds, ftlWarpBefore));
			auto t = Ftl->Target;
			PrefetchFtlDestination(t);
			ShowToast(cs::Cat("FTL DRIVE SPOOLING for ", cs::Strings::ToUpperInvariant(t->Name), ", ", Distance(t), "  (hold the nose on it; Enter calls it off)"));
			return false;
		}

		if (Ftl->Stage != Core::FtlStage::Spooling) return false;
		switch (Ftl->Step(ActiveSystem, PilotRootKm(), pilot().Forward, ship->Battle != nullptr, jd, Galaxy))
		{
			case Core::FtlEvent::Aborted:
				EndFtlWarp();
				ShowToast(Ftl->AbortReason);
				return false;
			case Core::FtlEvent::Charged:
				return EngageCruise(ship, jd);
			default:
				return false;
		}
	}

	cs::Ref<Core::FtlTarget> UnitySpaceApp::PointedTarget(DVec3 rootKm, DVec3 forward)
	{
		ftlTargetsAge -= Time::unscaledDeltaTime;
		if (ftlTargets == nullptr || ftlTargetsFor != ActiveSystem || ftlTargetsChart != Galaxy->ChartVersion || ftlTargetsAge <= 0)
		{
			ftlTargets = cs::New<cs::List<cs::Ref<Core::FtlTarget>>>(Core::FtlTargets::All(Galaxy, ActiveSystem));
			ftlTargetsFor = ActiveSystem;
			ftlTargetsChart = Galaxy->ChartVersion;
			ftlTargetsAge = 1.0f;
		}
		return Core::HelmFtl::Pointed(*ftlTargets, ActiveSystem, rootKm, forward);
	}

	cs::String UnitySpaceApp::Distance(const cs::Ref<Core::FtlTarget>& t)
	{
		if (t == nullptr || !IsPiloting()) return "";
		double km;
		Core::HelmFtl::Bearing(t, ActiveSystem, PilotRootKm(), km);
		return DistanceText(km);
	}

	cs::String UnitySpaceApp::DistanceText(double km)
	{
		if (km >= 0.01 * Core::Astro::LightYearKm) return cs::Fmt(km / Core::Astro::LightYearKm, "0.00") + " ly";
		if (km >= 0.01 * Core::Astro::AuKm) return cs::Fmt(km / Core::Astro::AuKm, "0.00") + " AU";
		return cs::Fmt(km, "N0") + " km";
	}

	// ---------------------------------------------------------------- between the stars

	bool UnitySpaceApp::EngageCruise(const cs::Ref<Core::Ship>& ship, double jd)
	{
		auto from = ActiveSystem;
		auto rootKm = PilotRootKm();
		// FTL carries momentum (U.SPC-T418): the velocity it goes in with, against the system's root, it comes out with.
		DVec3 frameVel = FrameVelocityKmS(pilotFrameBody, jd);
		DVec3 rootVel = FrameVelocityKmS(0, jd);
		int wing;
		auto c = Ftl->EngageInterstellar(Galaxy, from, ship, rootKm, jd, pilotVelKmS() + frameVel - rootVel, wing);
		if (c == nullptr)
		{
			EndFtlWarp();
			ShowToast(Ftl->AbortReason);
			return false;
		}
		EndSalvage(cs::String());
		SalvageApproach = cs::String();
		// The system left no longer sees the pilot, nor holds a patrol's hail or a chase on it.
		World()->PilotedShip = nullptr;
		if (World()->Sensors) World()->Sensors->UpdatePilot(nullptr, DVec3(), false, jd);
		if (World()->Law) World()->Law->UpdatePilot(nullptr, 0, DVec3(), DVec3(), jd);
		if (World()->Pursuit) World()->Pursuit->ReleaseAll(jd);
		Piloted = Target::None();
		pilotLockTarget = Target::None();
		pilotLockUnit = nullptr;
		pilotBoosting = false;
		SetpilotCruise(Core::PilotCruiseState());
		TankerDueS = -1;
		SpaceCamera->PilotLockTarget = Target::None();
		SpaceCamera->PilotView = std::nullopt;
		if (CoopHelmMine()) SpaceCamera->ExitFocus(); // US-T44 (US-T43): the holder's view only (the cruise view below is everyone's)
		Select(Target::None());

		FtlCruise = c;
		ftlShip = ship;
		ftlDestinationBuilt = false;
		// The course is flown: the crossing has it from here (U.SPC-T420).
		FtlAutopilot->Reset();
		PlottedCourse = nullptr;
		// A long crossing compressed on the sim clock, the rest of the galaxy moving on with it (U.SPC-T420).
		ftlCruiseLegJd = c->LegJd;
		Clock->SetWarp(Core::FtlNavigation::WarpFor(c->ArriveJd() - jd, cs::Math::Max(0, ftlWarpBefore)));
		// What the far system is drawn with starts loading now, in the background (U.SPC-T414).
		PrefetchFtlDestination(c->To, c->ToName);
		SetSystemVisible(false);
		Interstellar->SetVisible(true);
		GateFlash = 1;
		fovKick = GateFovKick;
		// Craft in the hangars, and escorts clamped to the hull (U.SPC-T473).
		int clamped = Core::FtlCarriage::CountClamped(c->Aboard.Keys());
		int docked = c->Aboard.Count() - clamped;
		cs::String carried = docked > 0 && clamped > 0 ? cs::Cat(" (", docked, " docked aboard, ", clamped, " clamped to the hull)")
			: docked > 0 ? cs::Cat(" (", docked, " docked aboard)") : clamped > 0 ? cs::Cat(" (", clamped, " clamped to the hull)") : cs::String();
		// No jump longer than the drive reaches (US-T86): a longer way is flown in even jumps, a stop in the void between each.
		int jumps = c->JumpsLeft(jd);
		ftlAtStop = false;
		ShowToast(cs::Cat("FTL ENGAGED for ", cs::Strings::ToUpperInvariant(c->ToName), ", ", cs::Fmt(c->Target.DistanceLy(c->Start), "0.00"),
			" ly through open space", (jumps > 1 ? cs::Cat(" in ", jumps, " jumps of ", cs::Fmt(c->Profile.Ly, "0.0"), " ly (the drive cools and spools again in the void between them)") : cs::String()),
			(wing == 0 ? cs::String() : cs::Cat(", ", wing, " of your wing with you", carried))));
		return true;
	}

	void UnitySpaceApp::UpdateFtlCruise()
	{
		auto c = FtlCruise;
		double jd = Clock->Jd;
		if (Galaxy->DeepSpace->Cruises().Contains(c))
		{
			// The galaxy is charted and built about the hull as it goes (U.SPC-T413).
			Galaxy->Viewpoint = c->PositionAt(jd);
			// A new leg (pulled out by a well or a gravity projector, or spooled again past it, U.SPC-T418): the clock
			// is compressed again for what is left of it (U.SPC-T420).
			bool atStop = c->AtVoidStop(jd);
			if (c->LegJd != ftlCruiseLegJd && !c->Holding)
			{
				ftlCruiseLegJd = c->LegJd;
				if (atStop)
				{
					// Dropped out in the void between jumps (US-T86): the stop runs at a pace the drive can be seen to cool
					// and spool at, then the next jump is compressed again.
					Clock->SetWarp(Core::SimClock::WarpToCover((c->EngageJd() - jd) * Core::Astro::SecondsPerDay, FtlStopRealSeconds));
					// The last jump next: the far end streams in while the drive cools and spools (US-T133 hook).
					if (c->JumpsLeft(jd) <= 1) PrefetchFtlDestination(c->To, c->ToName);
					ShowToast(cs::Cat("VOID STOP ", c->Stops, ": dropped out ", cs::Fmt(c->LyToGo(jd), "0.0"), " ly short of ", cs::Strings::ToUpperInvariant(c->ToName),
						"; the drive cools ", Core::FtlNavigation::Describe(cs::Math::Max(0.0, c->ChargeJd - jd)), " and spools ",
						Core::FtlNavigation::Describe(c->EngageJd() - cs::Math::Max(jd, c->ChargeJd)), " before jump ", c->Stops + 1, " of ", c->Stops + c->JumpsLeft(jd)));
				}
				else
				{
					Clock->SetWarp(Core::FtlNavigation::WarpFor(c->ArriveJd() - jd));
					ShowToast(c->Crossing
						? cs::Cat("PULLED OUT OF FTL by ", FtlOrElse(c->InterdictedBy, "a gravity well"), ": crossing it under the ordinary drive, ", Core::FtlNavigation::Describe(c->ArriveJd() - jd))
						: cs::Cat("Clear of ", FtlOrElse(c->InterdictedBy, "the well"), ": the drive spools again for ", c->ToName));
				}
			}
			else if (ftlAtStop && !atStop && !c->Holding)
			{
				// Charged at a void stop: the next jump is under way.
				Clock->SetWarp(Core::FtlNavigation::WarpFor(c->ArriveJd() - jd));
				int left = c->JumpsLeft(jd);
				ShowToast(cs::Cat("JUMP ", c->Stops + 1, " of ", c->Stops + left, " ENGAGED for ", cs::Strings::ToUpperInvariant(c->ToName), ", ", cs::Fmt(c->Profile.Ly, "0.0"), " ly"));
			}
			ftlAtStop = atStop;
			UpdateVoidStop(c, jd);
			double total = c->ArriveJd() - c->LegJd;
			double share = total > 0 && !cs::Double::IsInfinity(total) ? (jd - c->LegJd) / total : 0;
			// Built on the last jump only (US-T86): the far system is not yet anywhere near on the earlier ones.
			if (!ftlDestinationBuilt && share >= 0.5 && c->To != nullptr && !c->Stopover)
			{
				Activate(c->To);
				SetSystemVisible(false);
				Interstellar->SetVisible(true);
				ftlDestinationBuilt = true;
			}
			return;
		}

		auto ship = ftlShip;
		auto to = c->To;
		EndFtlCruise();
		if (ship == nullptr || ship->Destroyed || to == nullptr || !to->World->Fleet->Ships().Contains(ship))
		{
			// Destroyed out there (a fight between the stars): nothing comes out the far end.
			SetSystemVisible(true);
			FocusSystemOverview();
			ShowToast(cs::Cat("The ", (ship ? ship->Name : cs::String("ship")), " was lost between the stars"));
			return;
		}
		if (ActiveSystem != to) Activate(to);
		SetSystemVisible(true);
		Interstellar->SetVisible(false);
		EnterPiloting(Target::OfShip(ship));
		// Out moving as it went in (U.SPC-T418), in the frame the helm now flies in.
		DVec3 frameVel = FrameVelocityKmS(pilotFrameBody, Clock->Jd);
		DVec3 rootVel = FrameVelocityKmS(0, Clock->Jd);
		SetpilotVelKmS(c->CarriedVelocityKmS - frameVel + rootVel);
		EndFtlWarp();
		// The far side streams in behind the drop-out's own flash and fold (U.SPC-T414).
		BeginHelmGateCover();
		ShowToast(cs::Cat("Dropped out of FTL at the edge of ", cs::Strings::ToUpperInvariant(to->Name), ", ",
			DistanceText(ship->Orbit->Center.Length()), " from its star  (in from ", (c->From ? c->From->Name : cs::String("open space")), ")"));
	}

	void UnitySpaceApp::UpdateVoidStop(const cs::Ref<Core::Cruise>& c, double jd)
	{
		// The sensor keys at a void stop (US-T124), read off the keyboard: between the stars nothing is at the controls.
		if (c->AtVoidStop(jd) && CoopHelmMine())
		{
			const auto& kb = SpaceGod::Input::InputDevice::current();
			bool shift = kb[SpaceGod::Input::Key::LeftShift].isPressed || kb[SpaceGod::Input::Key::RightShift].isPressed;
			if (kb.present && kb[SpaceGod::Input::Key::Tab].wasPressedThisFrame)
			{
				cs::String why = Core::VoidRaiders::Ping(c, jd);
				ShowToast(why.empty() ? cs::String("ACTIVE PING: anything out there closing on you shows now, and may break off; you are lit up a while (Shift+F3 runs dark)") : cs::Cat("NO PING: ", why));
			}
			else if (kb.present && kb[SpaceGod::Input::Key::F3].wasPressedThisFrame && shift)
			{
				bool changed;
				ShowToast(Core::VoidRaiders::ToggleDark(c, jd, changed));
			}
		}
		// What happened to the hull out here with the ghosts, said once each (newest first back to the last one said).
		const auto& events = Galaxy->DeepSpace->Events();
		cs::String name = c->Leader() != nullptr ? c->Leader()->Name : cs::String();
		cs::Ref<Core::DeepSpaceEvent> newest = events.Count() > 0 ? events[events.Count() - 1] : nullptr;
		if (ftlRaidEventSeen == nullptr || !events.Contains(ftlRaidEventSeen)) ftlRaidEventSeen = newest;
		else if (newest != ftlRaidEventSeen)
		{
			for (int i = events.Count() - 1; i >= 0 && events[i] != ftlRaidEventSeen; i--)
			{
				const auto& t = events[i]->Text;
				bool raid = t.find("ghost") != cs::String::npos || t.find("Raider") != cs::String::npos || t.find("Out of the void") != cs::String::npos;
				if (raid && !name.empty() && t.find(name) != cs::String::npos)
				{
					ShowToast(t);
					break;
				}
			}
			ftlRaidEventSeen = newest;
		}
	}

	void UnitySpaceApp::UpdateVoidRaiderNews()
	{
		if (Galaxy == nullptr || Galaxy->DeepSpace == nullptr) return;
		int stage = Galaxy->DeepSpace->VoidRaidLore;
		if (voidLoreGalaxy != Galaxy.get() || voidLoreSeen < 0 || stage < voidLoreSeen)
		{
			// A new galaxy (or a load): what is known already is not news.
			voidLoreGalaxy = Galaxy.get();
			voidLoreSeen = stage;
			return;
		}
		if (stage == voidLoreSeen) return;
		const auto& l = Core::VoidRaiders::Lore()[stage - 1];
		voidLoreSeen = stage;
		ShowToast(cs::Cat("NEWS  ·  OUT OF THE VOID: ", cs::Strings::ToUpperInvariant(l.Title), "  ·  ", l.Text));
	}

	void UnitySpaceApp::UpdateFtlCruiseView(float dt)
	{
		auto c = FtlCruise;
		if (c == nullptr) return;
		auto heading = c->Direction().SwapYZ();
		auto forward = Vector3(static_cast<float>(heading.X), static_cast<float>(heading.Y), static_cast<float>(heading.Z));
		if (forward.sqrMagnitude() < 1e-12f) forward = Vector3::forward();
		auto want = Quaternion::LookRotation(forward, Mathf::Abs(forward.normalized().y) > 0.98f ? Vector3::forward() : Vector3::up());
		auto rotation = Quaternion::Slerp(SpaceCamera->Rotation, want, 1.0f - Mathf::Exp(-2.5f * dt));
		SpaceCamera->SetRotation(rotation);
		Interstellar->SetVisible(true);
		Interstellar->Update(c->PositionAt(Clock->Jd).Ly(), rotation, 1.0f);
	}

	void UnitySpaceApp::EndFtlCruise()
	{
		ftlAtStop = false;
		FtlCruise = nullptr;
		ftlCruiseLegJd = cs::Double::NaN;
		ftlShip = nullptr;
		ftlDestinationBuilt = false;
		Interstellar->SetVisible(false);
	}

	void UnitySpaceApp::EndFtlWarp()
	{
		if (ftlWarpBefore >= 0) Clock->SetWarp(ftlWarpBefore);
		ftlWarpBefore = -1;
	}

	void UnitySpaceApp::StopFtl()
	{
		bool flying = FtlAutopilot->Active();
		FtlAutopilot->Abandon(Clock->Jd);
		PlottedCourse = nullptr;
		if (Ftl->Stage == Core::FtlStage::Idle && !flying) return;
		Ftl->Reset();
		EndFtlWarp();
	}

	// ---------------------------------------------------------------- navigation (U.SPC-T420)

	bool UnitySpaceApp::PlotFtlCourse(const cs::Ref<Core::FtlTarget>& target, bool quiet)
	{
		if (target != nullptr && CoopSends("ftlplot", cs::Cat(quiet ? "1 " : "0 ", target->Name))) return true; // US-T44 (US-T43): the shared helm's course: into the frames
		if (!IsPiloting() || Piloted.Ship == nullptr)
		{
			ShowToast("Take the helm of a capital ship or an explorer (P) to plot an FTL course");
			return false;
		}
		auto ship = Piloted.Ship;
		cs::String refusal = RidingLane() ? cs::String("Not from a lane: come off it first") : Core::HelmFtl::Refusal(ship);
		if (!refusal.empty())
		{
			ShowToast(refusal);
			return false;
		}
		if (FtlAutopilot->Active()) DisengageFtlAutopilot(cs::String());
		PlottedCourse = Core::FtlCourse::Plot(Galaxy, ActiveSystem, ship, PilotRootKm(), target, Clock->Jd);
		ftlCourseAge = 1.0f;
		if (!quiet) ShowToast(CourseSummary(PlottedCourse));
		return PlottedCourse->Flyable();
	}

	cs::String UnitySpaceApp::CourseSummary(const cs::Ref<Core::FtlCourse>& c)
	{
		if (c == nullptr) return "No course plotted";
		cs::String name = c->Target ? cs::Strings::ToUpperInvariant(c->Target->Name) : cs::String("nowhere");
		if (!c->Flyable()) return cs::Cat("No course for ", name, ": ", c->Refusal);
		return cs::Cat("COURSE PLOTTED for ", name, " (", c->Target->KindName(), "), ", cs::Fmt(c->Ly, "0.00"), " ly",
			(c->Stops > 0 ? cs::Cat(" in ", c->Jumps(), " jumps (", c->Stops, " void stop", c->Stops == 1 ? "" : "s", ")") : cs::String()), ", ETA ",
			Core::FtlNavigation::Describe(c->TotalDays()), ", ", cs::Fmt(c->FuelTonnes(), "N0"), " t of ", cs::Fmt(c->TanksTonnes, "N0"),
			" t reaction mass", (c->Interdictions.Count() > 0 ? cs::Cat(", pulled out ", c->Interdictions.Count(), "x on the way") : cs::String()), "  (End engages the autopilot)");
	}

	void UnitySpaceApp::ClearFtlCourse()
	{
		if (CoopSends("ftlclear")) return; // US-T44 (US-T43): into the frames (the loop's own Delete key is already in them)
		if (FtlAutopilot->Active()) DisengageFtlAutopilot(cs::String());
		PlottedCourse = nullptr;
		ShowToast("FTL course cleared");
	}

	void UnitySpaceApp::CycleFtlCourse(int step)
	{
		if (!CanPlotFtl())
		{
			if (IsPiloting() && Piloted.Ship != nullptr) ShowToast(FtlOrElse(Core::HelmFtl::Refusal(Piloted.Ship), "Not from a lane: come off it first"));
			return;
		}
		auto list = Core::FtlNavigation::Nearest(Galaxy, ActiveSystem, PilotRootKm());
		auto next = Core::FtlNavigation::Cycle(list, PlottedCourse ? PlottedCourse->Target : nullptr, step);
		if (next == nullptr)
		{
			ShowToast("No other star or beacon on the charts");
			return;
		}
		PlotFtlCourse(next);
	}

	bool UnitySpaceApp::EngageFtlAutopilot()
	{
		if (CoopSends("ftlauto")) return true; // US-T44 (US-T43): into the frames (the loop's own End key is already in them)
		if (!IsPiloting() || Piloted.Ship == nullptr)
		{
			ShowToast("Take the helm of a capital ship or an explorer (P) to fly an FTL course");
			return false;
		}
		auto ship = Piloted.Ship;
		if (FtlAutopilot->Active()) return true;
		if (Ftl->Stage == Core::FtlStage::Spooling)
		{
			ShowToast(cs::Cat("The drive is already spooling for ", Ftl->Target->Name, " (Enter calls it off)"));
			return false;
		}
		cs::Ref<Core::FtlTarget> target = PlottedCourse ? PlottedCourse->Target : nullptr;
		if (target == nullptr) target = FtlPointed;
		if (target == nullptr)
		{
			ShowToast("No course plotted: put the nose on a star, Page Down for the nearest, or pick one on the galaxy map ([)");
			return false;
		}
		if (!PlotFtlCourse(target, /*quiet:*/ true))
		{
			ShowToast(CourseSummary(PlottedCourse));
			return false;
		}
		double jd = Clock->Jd;
		auto from = PilotRootKm();
		if (!FtlAutopilot->Engage(PlottedCourse, from, ship->Battle != nullptr, jd))
		{
			ShowToast(FtlAutopilot->Reason);
			return false;
		}
		// Flown in the system's own frame, from the star, the stick let go of.
		pilotFrameBody = Catalog()->Root()->Index;
		pilot().Pos = from;
		pilot().Vel = FtlAutopilot->VelocityKmS(jd);
		pilot().AngularVelocity = DVec3::Zero;
		SetpilotCruise(Core::PilotCruiseState());
		pilotBoosting = false;
		if (ftlWarpBefore < 0) ftlWarpBefore = Clock->WarpIndex;
		auto c = PlottedCourse;
		if (FtlAutopilot->Stage == Core::FtlAutopilotStage::OutRun)
			Clock->SetWarp(Core::FtlNavigation::WarpFor(FtlAutopilot->RunDaysLeft(jd), ftlWarpBefore));
		ShowToast(cs::Cat("FTL AUTOPILOT for ", cs::Strings::ToUpperInvariant(c->Target->Name), ": ",
			(c->NeedsOutRun() ? cs::Cat("out beyond the rim (", cs::Fmt(c->OutRunKm / Core::Astro::AuKm, "0.##"), " AU, ", Core::FtlNavigation::Describe(c->OutRunDays), "), then ") : cs::String()),
			"onto the star, spool and engage; ETA ", Core::Astro::ToDateTime(c->EtaJd()).ToString("yyyy-MM-dd"), "  (End takes the helm back)"));
		return true;
	}

	bool UnitySpaceApp::BeginFtlAutopilotSpool(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::FtlAutopilot>& ap, double jd)
	{
		double coolUntil = Core::FtlRules::CoolUntil(ship);
		if (coolUntil > jd)
		{
			if (ap->Stage != Core::FtlAutopilotStage::Cooling)
			{
				ap->Cooling();
				if (ftlWarpBefore < 0) ftlWarpBefore = Clock->WarpIndex;
				ShowToast(cs::Cat("FTL drive still cooling: the autopilot holds the nose on ", cs::Strings::ToUpperInvariant(ap->Course->Target->Name), " and spools when it has cooled"));
			}
			Clock->SetWarp(Core::SimClock::WarpToCover((coolUntil - jd) * Core::Astro::SecondsPerDay, FtlSpoolRealSeconds, cs::Math::Max(0, ftlWarpBefore)));
			return true;
		}
		if (!Ftl->Spool(ship, ActiveSystem, ap->Course->Target, PilotRootKm(), pilot().Forward, false, jd, Galaxy))
		{
			DisengageFtlAutopilot(cs::Cat("FTL autopilot off: ", Ftl->AbortReason));
			return false;
		}
		ap->Spooling();
		PrefetchFtlDestination(ap->Course->Target);
		if (ftlWarpBefore < 0) ftlWarpBefore = Clock->WarpIndex;
		Clock->SetWarp(Core::SimClock::WarpToCover((Ftl->ChargedJd - jd) * Core::Astro::SecondsPerDay, FtlSpoolRealSeconds, cs::Math::Max(0, ftlWarpBefore)));
		ShowToast(cs::Cat("FTL DRIVE SPOOLING for ", cs::Strings::ToUpperInvariant(ap->Course->Target->Name), ", ", Distance(ap->Course->Target), "  (the autopilot holds the nose on it; End lets go)"));
		return true;
	}

	void UnitySpaceApp::DisengageFtlAutopilot(const cs::String& whyIn)
	{
		// Copied: a caller may pass a string this clears (Ftl->AbortReason).
		cs::String why = whyIn;
		auto ap = FtlAutopilot;
		if (!ap->Active()) return;
		double jd = Clock->Jd;
		if (ap->Stage == Core::FtlAutopilotStage::OutRun)
		{
			pilot().Pos = ap->PositionKm(jd);
			pilot().Vel = ap->VelocityKmS(jd);
		}
		if (ap->Stage == Core::FtlAutopilotStage::Spooling && Ftl->Stage == Core::FtlStage::Spooling) Ftl->Abort("Charge called off with the autopilot");
		ap->Disengage(why, jd);
		EndFtlWarp();
		// A fight or a chase is flown at the pace it is fought, not the pace the clock had before the trip.
		if ((Piloted.Ship != nullptr && Piloted.Ship->Battle != nullptr) || (World()->Pursuit != nullptr && World()->Pursuit->Active())) Clock->SetWarp(0);
		ResetPilotAim();
		if (why.empty()) return;
		double speed = pilot().Vel.Length();
		ShowToast(cs::Cat(why, (speed > 1 ? cs::Cat("  (still moving at ", cs::Fmt(speed, "N0"), " km/s)") : cs::String())));
	}

	void UnitySpaceApp::RefreshFtlCourse(const cs::Ref<Core::Ship>& ship)
	{
		auto c = PlottedCourse;
		if (c == nullptr || FtlAutopilot->Active()) return;
		if (c->Ship != ship || c->From != ActiveSystem)
		{
			PlottedCourse = nullptr;
			return;
		}
		ftlCourseAge -= Time::unscaledDeltaTime;
		if (ftlCourseAge > 0) return;
		ftlCourseAge = 1.0f;
		PlottedCourse = Core::FtlCourse::Plot(Galaxy, ActiveSystem, ship, PilotRootKm(), c->Target, Clock->Jd);
	}

	bool UnitySpaceApp::PlottedBearing(DVec3& direction, double& km)
	{
		direction = DVec3();
		km = 0;
		if (PlottedCourse == nullptr || PlottedCourse->Target == nullptr || !IsPiloting() || PlottedCourse->From != ActiveSystem) return false;
		direction = PlottedCourse->Bearing(PilotRootKm(), km);
		return km > 0;
	}

	bool UnitySpaceApp::UpdateFtlAutopilot(const cs::Ref<Core::Ship>& ship, float dt)
	{
		auto ap = FtlAutopilot;
		if (!ap->Active()) return false;
		double jd = Clock->Jd;
		if (Input.ToggleFtlAutopilot)
		{
			DisengageFtlAutopilot("FTL autopilot off: the helm is yours");
			return false;
		}
		if (ship->Battle != nullptr)
		{
			DisengageFtlAutopilot("FTL autopilot off: battle stations");
			return false;
		}
		if (World()->Pursuit != nullptr && World()->Pursuit->Active())
		{
			DisengageFtlAutopilot("FTL autopilot off: hunters closing");
			return false;
		}
		if (PilotStunSeconds() > 0)
		{
			DisengageFtlAutopilot("FTL autopilot off: the drives are knocked out");
			return false;
		}

		if (ap->Step(false, jd) == Core::FtlAutopilotEvent::Arrived)
		{
			pilot().Pos = ap->RunToKm;
			pilot().Vel = DVec3::Zero;
			// The turn is flown at the pace the clock had before.
			Clock->SetWarp(cs::Math::Max(0, ftlWarpBefore));
			ShowToast(cs::Cat("Beyond ", ActiveSystem->Name, "'s rim: turning onto ", cs::Strings::ToUpperInvariant(ap->Course->Target->Name)));
		}
		switch (ap->Stage)
		{
			case Core::FtlAutopilotStage::OutRun:
				pilot().Pos = ap->PositionKm(jd);
				pilot().Vel = ap->VelocityKmS(jd);
				SetPilotFacing(ap->RunDirection());
				break;
			case Core::FtlAutopilotStage::Aligning:
			{
				pilot().Vel = DVec3::Zero;
				auto forward = pilot().Forward;
				auto turned = ap->Turn(forward, PilotRootKm(), Core::FtlAutopilot::TurnDegPerSecond * dt);
				SetPilotFacing(forward);
				if (turned != Core::FtlAutopilotEvent::Aligned) break;
				if (!BeginFtlAutopilotSpool(ship, ap, jd)) return false;
				break;
			}
			case Core::FtlAutopilotStage::Cooling:
			{
				// Held, nose on the star, until the drive has cooled; then it spools.
				pilot().Vel = DVec3::Zero;
				if (jd < Core::FtlRules::CoolUntil(ship)) break;
				if (!BeginFtlAutopilotSpool(ship, ap, jd)) return false;
				break;
			}
			default:
				// Spooling: held still with the nose on the star; the charge engages it (UpdateHelmFtl, below).
				pilot().Vel = DVec3::Zero;
				break;
		}
		pilot().AngularVelocity = DVec3::Zero;
		SetpilotCruise(Core::PilotCruiseState());
		ResetPilotAim();
		SpaceCamera->PilotView = PilotViewRotation(PilotControlMode);

		// What everyone else sees of it, and what it sees: its drive is lit on the run out.
		if (World()->Sensors) World()->Sensors->UpdatePilot(ship, Ephemeris()->HelioAt(pilotFrameBody, jd) + pilotOffsetKm(), ap->Stage == Core::FtlAutopilotStage::OutRun, jd);
		World()->Law->UpdatePilot(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), jd);
		World()->Outlaws->UpdatePilot(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), jd);
		if (World()->Contracts) World()->Contracts->UpdatePilot(ship, pilotFrameBody, pilotOffsetKm(), jd);
		UpdatePilotBattle(ship, dt);
		UpdatePursuit(ship, dt);
		if (UpdateHelmFtl(ship)) return true;
		if (ap->Stage == Core::FtlAutopilotStage::Spooling && Ftl->Stage != Core::FtlStage::Spooling)
		{
			// The charge was lost or called off (Enter): the autopilot goes with it.
			cs::String why = Ftl->AbortReason;
			ap->Disengage(why, jd);
			EndFtlWarp();
			ResetPilotAim();
			ShowToast(cs::Cat("FTL autopilot off: ", FtlOrElse(why, "the charge was lost")));
		}
		ShowWarNotices();
		return true;
	}

	void UnitySpaceApp::SetPilotFacing(DVec3 forward)
	{
		if (forward.LengthSquared() <= 0) return;
		auto f = forward.Normalized();
		auto up = pilot().Up - f * DVec3::Dot(pilot().Up, f);
		pilot().Forward = f;
		pilot().Up = up.LengthSquared() > 1e-6 ? up.Normalized() : Core::PilotState::LevelUp(f);
	}
}
