// Port of Unity.SpaceGod Runtime/UnitySpaceApp.Lanes.cs (US-T29): the player riding the hyperlanes, the scanners'
// look ahead for interdictors, and the gate spurs. Members are declared in UnitySpaceApp.Lanes.inl.

#include "UnitySpaceApp.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/War.h"
#include "Sensors/Sensors.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"

#include "Views/LaneView.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	bool UnitySpaceApp::RidingLane()
	{
		return IsPiloting() && Piloted.Ship != nullptr && Piloted.Ship->Phase == Core::ShipPhase::Lane;
	}

	int UnitySpaceApp::RideRingCount(int& passed, bool& accelerating)
	{
		passed = 0;
		accelerating = false;
		return RidingLane() ? Core::Fleet::RideRings(Piloted.Ship, Clock->Jd, passed, accelerating) : 0;
	}

	float UnitySpaceApp::LaneStreaks()
	{
		return RidingLane() ? 0.3f + 0.7f * LaneRideSpeed : LaneExitFade * 0.35f;
	}

	double UnitySpaceApp::RideSecondsLeft()
	{
		return RidingLane()
			? cs::Math::Max(0.0, Piloted.Ship->PhaseEndJd - Clock->Jd) * Core::Astro::SecondsPerDay / cs::Math::Max(Clock->Warp().SimSecondsPerSecond, 1e-9)
			: 0;
	}

	// ---------------------------------------------------------------- interdiction (U.SPC-T42)

	double UnitySpaceApp::ThreatRealSeconds()
	{
		return RideThreat.has_value() ? RideThreat->SecondsToPulse / cs::Math::Max(Clock->Warp().SimSecondsPerSecond, 1e-9) : 0;
	}

	double UnitySpaceApp::PilotStunSeconds()
	{
		cs::Ref<Core::Ship> ship = IsPiloting() ? Piloted.Ship : nullptr;
		cs::Ref<Core::BattleUnit> unit = ship != nullptr && ship->Battle != nullptr ? ship->Battle->UnitOf(ship) : nullptr;
		// A hunter's EMP out of battle (U.SPC-T273) knocks the drives out just as a pulse or an EMP round in one.
		return cs::Math::Max(unit != nullptr && unit->Stunned > 0 ? unit->Stunned : 0.0, pilotEmpSeconds);
	}

	bool UnitySpaceApp::CanRejoinLane()
	{
		return IsPiloting() && laneRejoinable && nearGate == nullptr && Piloted.Ship != nullptr &&
			Fleet()->CanRejoin(Piloted.Ship, Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm(), Clock->Jd);
	}

	void UnitySpaceApp::UpdateRideThreat(const cs::Ref<Core::Ship>& ship, double jd)
	{
		cs::Nullable<Core::LaneThreat> threat;
		if (World()->War != nullptr) threat = World()->War->ThreatAhead(ship, jd);
		RideThreat = threat;
		if (threat.has_value())
		{
			const Core::LaneThreat& t = *threat;
			if (t.Lurker == warnedOf) return;
			warnedOf = t.Lurker;
			int i = Clock->WarpIndex;
			while (i > 0 && t.SecondsToPulse / Core::SimClock::Levels()[i].SimSecondsPerSecond < WarningRealSeconds) i--;
			Clock->SetWarp(i);
			ShowToast(cs::Cat("SENSORS: interdiction field ahead on the ", ship->LaneLeg.Lane->Name, " lane, ", cs::Math::Round(t.DistanceKm / 1000), " thousand km",
				(t.Braced ? " (a raider's hull made out: braced for the pulse)" : ""),
				"  ·  Shift surge through, Ctrl drop out, or ride on"));
			return;
		}
		if (warnedOf == nullptr) return;
		warnedOf = nullptr;
		// Clear of it: back to the pace that gets the rest of the ride over within the minute.
		double simSeconds = cs::Math::Max(0.0, ship->PhaseEndJd - jd) * Core::Astro::SecondsPerDay;
		Clock->SetWarp(Core::SimClock::WarpToCover(simSeconds, RideRealSeconds, cs::Math::Max(0, rideWarpBefore)));
	}

	void UnitySpaceApp::SurgeThroughLane(const cs::Ref<Core::Ship>& ship)
	{
		if (!Core::Fleet::SurgeRide(ship, Clock->Jd))
		{
			if (ship->LaneSurged) ShowToast("Already surging: the drive has nothing more to give on this lane");
			return;
		}
		ship->Combat->BoostFraction = 0;
		fovKick = GateFovKick * 0.6f;
		ShowToast(cs::Cat("SURGE: the drive pours everything into the lane, ", cs::Fmt(Core::Fleet::SurgeFactor, "0.#"), "x the pace",
			(RideThreat.has_value() ? " -- through the field before the pulse closes, with luck" : "")));
	}

	void UnitySpaceApp::DropOutOfLane(const cs::Ref<Core::Ship>& ship)
	{
		double jd = Clock->Jd;
		auto hop = ship->LaneLeg;
		auto facing = pilot().Forward;
		bool threatened = RideThreat.has_value();
		Fleet()->DropOut(ship, jd, facing);
		EndRide();
		pilotFrameBody = ship->Orbit->Body;
		auto tuning = Core::PilotTuning::Of(ship->Spec->Class);
		auto up = pilot().Up;
		pilot() = Core::PilotState::At(ship->Orbit->OffsetAt(jd), facing * tuning.MaxSpeed, facing);
		auto level = up - facing * DVec3::Dot(up, facing);
		if (level.LengthSquared() > 1e-9) pilot().Up = level.Normalized();
		ResetPilotAim();
		insideRampGate = false;
		laneRejoinable = true;
		LaneExitFade = 1;
		fovKick = GateFovKick * 0.5f;
		ShowToast(cs::Cat("Dropped out of the ", hop.Lane->Name, " lane", (threatened ? " short of the field" : ""),
			"  (Y to get back in where you left it)"));
	}

	void UnitySpaceApp::UpdateLaneRejoin(const cs::Ref<Core::Ship>& ship)
	{
		if (!Input.ActivateGate || !CanRejoinLane()) return;
		if (!Fleet()->RejoinLane(ship, Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm(), Clock->Jd)) return;
		laneRejoinable = false;
		SalvageApproach = cs::String();
		BeginRide(ship, false);
		fovKick = GateFovKick;
		ShowToast(cs::Cat("Back into the ", ship->LaneLeg.Lane->Name, " lane, bound for ", Fleet()->Lanes->EndName(ship->LaneLeg.Lane, ship->LaneLeg.To)));
	}

	cs::Nullable<Core::LaneHop> UnitySpaceApp::SpurRingFlownThrough(const cs::Ref<Core::Hyperlanes>& lanes, DVec3 helio)
	{
		cs::Nullable<DVec3> before = lastSpurCheckSystem == ActiveSystem ? lastSpurCheckHelio : cs::Nullable<DVec3>();
		lastSpurCheckHelio = helio;
		lastSpurCheckSystem = ActiveSystem;
		if (!before.has_value() || lanes->GateSpurs.Count() == 0) return std::nullopt;
		auto from = *before;
		// A jump across the system in one frame (a ride's end, a teleport) is not a flight through anything.
		if ((helio - from).Length() > Core::Hyperlanes::GateRadiusKm * 4) return std::nullopt;
		return lanes->GateSpurRingCrossed(from, helio, Clock->Jd);
	}

	void UnitySpaceApp::ShowWarNotices()
	{
		if (World()->War == nullptr) return;
		auto notices = World()->War->TakePilotNotices();
		if (notices.Count() > 0) ShowToast(cs::Strings::Join("   ·   ", notices));
	}

	void UnitySpaceApp::UpdateLaneEntry(const cs::Ref<Core::Ship>& ship)
	{
		auto lanes = Fleet()->Lanes;
		if (lanes == nullptr) return;
		auto helio = Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm();
		auto hop = lanes->OnRampAt(helio, Clock->Jd);
		bool inside = hop.has_value();
		bool enter = inside && !insideRampGate;
		insideRampGate = inside;
		// Not inside a lane's ramp: perhaps through the ring at either end of a spur to a jump gate (U.SPC-T379).
		auto spur = SpurRingFlownThrough(lanes, helio);
		if (!enter && spur.has_value())
		{
			hop = spur;
			enter = true;
		}
		if (!enter || ship->Battle != nullptr || Salvage != nullptr) return;
		Fleet()->JoinLane(ship, *hop, helio, Clock->Jd);
		SalvageApproach = cs::String();
		BeginRide(ship, true);
		auto lane = hop->Lane;
		ShowToast(cs::Cat("Into the ", lane->Name, (lane->IsGateSpur() ? " spur" : " lane"), ", bound for ", lanes->EndName(lane, hop->To),
			(lane->IsGateSpur() && hop->To == lane->B ? "  (Y at the gate to jump, P hands the ride to the crew)"
				: "  (number keys pick the off-ramp's exit, P hands the ride to the crew)")));
	}

	void UnitySpaceApp::BeginRide(const cs::Ref<Core::Ship>& ship, bool throughGate)
	{
		auto hop = ship->LaneLeg;
		// Into a lane, an FTL charge is lost (U.SPC-T417).
		StopFtl();
		if (rideWarpBefore < 0) rideWarpBefore = Clock->WarpIndex;
		double simSeconds = cs::Math::Max(0.0, ship->PhaseEndJd - Clock->Jd) * Core::Astro::SecondsPerDay;
		Clock->SetWarp(Core::SimClock::WarpToCover(simSeconds, RideRealSeconds, rideWarpBefore));
		RideOnward.Clear();
		RideOnward.AddRange(Fleet()->Lanes->OnwardHops(hop.To, hop.Lane));
		RideChoice = -1;
		warnedOf = nullptr;
		RideThreat = std::nullopt;
		laneRejoinable = false;
		// A key already held as the ride starts (boosting in through the ring) is not a press.
		rideBoostHeld = rideDropHeld = true;
		pilotFrameBody = hop.From;
		SetpilotCruise(Core::PilotCruiseState());
		pilotBoosting = false;
		nearGate = nullptr;
		lastSpurCheckHelio = std::nullopt;
		if (throughGate) GateEffect(hop.Lane, hop.From);
	}

	void UnitySpaceApp::EndRide()
	{
		if (rideWarpBefore >= 0) Clock->SetWarp(rideWarpBefore);
		rideWarpBefore = -1;
		RideOnward.Clear();
		RideChoice = -1;
		LaneRideSpeed = 0;
		RideThreat = std::nullopt;
		warnedOf = nullptr;
		lastSpurCheckHelio = std::nullopt;
	}

	void UnitySpaceApp::GateEffect(const cs::Ref<Core::Lane>& lane, int at)
	{
		GateFlash = 1;
		fovKick = GateFovKick;
		if (laneView) laneView->Flash(lane, at);
	}

	void UnitySpaceApp::UpdateLaneRide(const cs::Ref<Core::Ship>& ship)
	{
		double jd = Clock->Jd;
		if (ship->Battle != nullptr)
		{
			// Pulled into a fight mid-ride (an interdiction pulse, U.SPC-T42): off the lane where the hull is, and the
			// fight is flown from there, at the pace of a fight; out of it, the hull can get back into the lane.
			auto at = Fleet()->StateOf(ship, jd).HelioKm;
			EndRide();
			pilotFrameBody = Catalog()->Root()->Index;
			SetpilotOffsetKm(at - Ephemeris()->Helio(pilotFrameBody));
			PinWherePiloted(ship);
			Target piloted = Piloted;
			EnterPiloting(piloted);
			laneRejoinable = true;
			GateFlash = 0.8f;
			fovKick = GateFovKick;
			LaneExitFade = 1;
			return;
		}
		UpdateRideThreat(ship, jd);
		bool boost = Input.Boost, drop = Input.Slow;
		bool boostPressed = boost && !rideBoostHeld, dropPressed = drop && !rideDropHeld;
		rideBoostHeld = boost;
		rideDropHeld = drop;
		if (dropPressed)
		{
			DropOutOfLane(ship);
			return;
		}
		if (boostPressed) SurgeThroughLane(ship);
		if (Input.NumberKey == 1 && RideChoice != -1)
		{
			RideChoice = -1;
			ShowToast(cs::Cat("At the off-ramp: come off at ", Fleet()->Lanes->EndName(ship->LaneLeg.Lane, ship->LaneLeg.To)));
		}
		else if (Input.NumberKey >= 2 && Input.NumberKey - 2 < RideOnward.Count())
		{
			RideChoice = Input.NumberKey - 2;
			auto next = RideOnward[RideChoice];
			ShowToast(cs::Cat("At the off-ramp: on along the ", next.Lane->Name, (next.Lane->IsGateSpur() ? " spur" : " lane"), " to ",
				Fleet()->Lanes->EndName(next.Lane, next.To), "  (1 to come off instead)"));
		}

		if (Core::Fleet::RideOver(ship, jd))
		{
			ReachRamp(ship);
			return;
		}

		auto st = Fleet()->StateOf(ship, jd);
		SetpilotOffsetKm(st.HelioKm - Ephemeris()->Helio(pilotFrameBody));
		SetpilotVelKmS(st.Facing * st.SpeedKmS);
		pilot().Forward = st.Facing;
		auto level = pilot().Up - st.Facing * DVec3::Dot(pilot().Up, st.Facing);
		pilot().Up = level.LengthSquared() > 1e-9 ? level.Normalized() : Core::PilotState::LevelUp(st.Facing);
		pilot().AngularVelocity = DVec3::Zero;
		ResetPilotAim();
		double span = cs::Math::Max(ship->PhaseEndJd - ship->PhaseStartJd, 1e-9);
		double u = cs::Math::Max(0.0, cs::Math::Min(1.0, (jd - ship->PhaseStartJd) / span));
		LaneRideSpeed = static_cast<float>((1 - cs::Math::Cos(Core::Astro::TwoPi * u)) * 0.5);
		// A rider's drive is lit (as every rider's is) for whoever is watching.
		if (World()->Sensors) World()->Sensors->UpdatePilot(ship, st.HelioKm, true, jd);
	}

	void UnitySpaceApp::ReachRamp(const cs::Ref<Core::Ship>& ship)
	{
		auto hop = ship->LaneLeg;
		double jd = Clock->Jd;
		cs::Nullable<Core::LaneHop> next = RideChoice >= 0 && RideChoice < RideOnward.Count() ? cs::Nullable<Core::LaneHop>(RideOnward[RideChoice]) : cs::Nullable<Core::LaneHop>();
		auto facing = pilot().Forward;
		Fleet()->RampReached(ship, next, facing, jd);
		GateEffect(hop.Lane, hop.To);
		if (next.has_value())
		{
			Core::LaneHop onward = *next;
			BeginRide(ship, false);
			ShowToast(cs::Cat("On along the ", onward.Lane->Name, (onward.Lane->IsGateSpur() ? " spur" : " lane"), ", bound for ",
				Fleet()->Lanes->EndName(onward.Lane, onward.To)));
			return;
		}
		EndRide();
		pilotFrameBody = hop.To;
		// Out of the gate still moving: flight assist brings the hull down from there.
		auto tuning = Core::PilotTuning::Of(ship->Spec->Class);
		auto up = pilot().Up;
		pilot() = Core::PilotState::At(ship->Orbit->OffsetAt(jd), facing * tuning.MaxSpeed, facing);
		auto level = up - facing * DVec3::Dot(up, facing);
		if (level.LengthSquared() > 1e-9) pilot().Up = level.Normalized();
		ResetPilotAim();
		insideRampGate = true;
		LaneExitFade = 1;
		// A spur's far end is at its gate, in range to open it (U.SPC-T379).
		if (hop.Lane->IsGateSpur() && hop.To == hop.Lane->B)
			ShowToast(cs::Cat("Off the ", hop.Lane->Name, " spur at ", cs::Strings::ToUpperInvariant(hop.Lane->Gate->Name),
				(hop.Lane->Gate->Destination() != nullptr ? cs::Cat(", to ", cs::Strings::ToUpperInvariant(hop.Lane->Gate->Destination()->Name), " (Y to jump)") : cs::String())));
		else ShowToast(cs::Cat("Off the ", hop.Lane->Name, (hop.Lane->IsGateSpur() ? " spur" : " lane"), " at ", Fleet()->Lanes->EndName(hop.Lane, hop.To)));
	}
}
