// US-T446: possession as a show, and the possessed view (declarations in UnitySpaceApp.Possession.inl). View only: nothing
// here writes to the simulation, so co-op stays in lockstep; each peer plays its own player's show and sees through its
// own player's seat.

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "PlayerOptions.h"
#include "UI/UiMotion.h"
#include "Views/PossessionFx.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/EvaPilot.h"
#include "Embodiment/MechWalker.h"
#include "Embodiment/RepairWorker.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "Sensors/Sensors.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

namespace SpaceGod
{
	using namespace Core;
	using Tune = Views::PossessionTune;

	namespace
	{
		constexpr double LightKmS = 299792.458;

		float Smooth01(float a, float b, float x)
		{
			float t = Mathf::Clamp01((x - a) / (b - a));
			return t * t * (3 - 2 * t);
		}

		float Bump(float t, float in, float peak, float out) { return t < peak ? Smooth01(in, peak, t) : 1 - Smooth01(peak, out, t); }

		bool IsPersonKind(EmbodimentKind k)
		{
			return k == EmbodimentKind::RepairWorker || k == EmbodimentKind::EvaPilot || k == EmbodimentKind::Marine || k == EmbodimentKind::Mech;
		}

		/// <summary>A person's eyes, a hull's sensors: what a seat sees with (the helm's picture is the simulation's own).</summary>
		SensorSuite SuiteOf(const cs::Ref<IEmbodiable>& seat)
		{
			auto sub = seat->Subject();
			switch (seat->Kind())
			{
				case EmbodimentKind::Station: return SensorSuite::Station;
				case EmbodimentKind::Fighter: return sub.Ship != nullptr ? SensorSuite::Of(sub.Ship) : SensorSuite(Tune::CraftRangeKm, Tune::CraftResolution);
				case EmbodimentKind::Turret: return sub.Ship != nullptr ? SensorSuite::Of(sub.Ship) : SensorSuite::Station; // the hull's fire control, a station's
				case EmbodimentKind::Satellite: return SensorSuite(Tune::SatelliteRangeKm, Tune::SatelliteResolution);
				case EmbodimentKind::Mech: return SensorSuite(Tune::MechRangeKm, Tune::MechResolution);
				default: return sub.Ship != nullptr ? SensorSuite::Of(sub.Ship) : SensorSuite::Civil;
			}
		}
	}

	UnitySpaceApp::PerceptionKind UnitySpaceApp::PerceptionOf(const cs::Ref<Ship>& helm, const cs::Ref<IEmbodiable>& seat)
	{
		if (helm != nullptr) return PerceptionKind::Sensors;
		if (seat == nullptr) return PerceptionKind::God;
		switch (seat->Kind())
		{
			case EmbodimentKind::Station: return PerceptionKind::Panorama;
			case EmbodimentKind::RepairWorker:
			case EmbodimentKind::EvaPilot:
			case EmbodimentKind::Marine: return PerceptionKind::Eyes;
			default: return PerceptionKind::Sensors;
		}
	}

	cs::Ref<ContactBook> UnitySpaceApp::PerceivedPicture() const
	{
		// The god's full view until the energy has gone in (and at once on letting go: the god sees everything again).
		if (perceptionShown < 0.5f) return nullptr;
		if (possessedHelm != nullptr) return World() != nullptr && World()->Sensors != nullptr ? World()->Sensors->Player : nullptr;
		if (possessedSeat != nullptr) return perceivedBook;
		return nullptr;
	}

	void UnitySpaceApp::UpdatePossession()
	{
		float dt = Mathf::Min(Time::unscaledDeltaTime, 0.1f);
		// What this machine's player holds now: the helm flown here, else a session seat.
		cs::Ref<Ship> helm = IsPiloting() ? Piloted.Ship : nullptr;
		cs::Ref<IEmbodiable> seat = helm == nullptr ? HeldNonHelm() : nullptr;
		bool same = helm == possessedHelm
			&& ((seat == nullptr && possessedSeat == nullptr) || (seat != nullptr && possessedSeat != nullptr && EmbodiableEquals(seat, possessedSeat)));
		if (!same)
		{
			bool had = possessedHelm != nullptr || possessedSeat != nullptr;
			bool has = helm != nullptr || seat != nullptr;
			if (has || had)
			{
				possessionShow = PossessionShowState();
				possessionShow.Active = true;
				possessionShow.Entering = has;
				// From one seat straight into the next the energy jumps across quicker, as the view does (US-T90's Hurry).
				possessionShow.Speed = has && had ? Tune::SwitchSpeed : 1.0f;
				possessionShow.Helm = has ? helm : possessedHelm;
				possessionShow.Seat = has ? seat : possessedSeat;
			}
			possessedHelm = helm;
			possessedSeat = seat;
			perceivedBook = nullptr;
			perceptionSweepTimer = 0;
			if (has) perceiving = PerceptionOf(helm, seat);
			// From seat to seat the view changes with the seat (the old picture is not the new seat's).
			if (has && had) perceptionShown = 0;
		}
		if (possessionShow.Active)
		{
			possessionShow.T += dt * possessionShow.Speed;
			if (possessionShow.T >= (possessionShow.Entering ? Tune::EnterSeconds : Tune::LeaveSeconds)) possessionShow.Active = false;
		}

		bool holding = possessedHelm != nullptr || possessedSeat != nullptr;
		bool settled = holding && (!possessionShow.Active || !possessionShow.Entering || possessionShow.T >= Tune::SettleStartS);
		perceptionShown = settled ? Mathf::Min(1.0f, perceptionShown + dt / Tune::SettleSeconds)
			: Mathf::Max(0.0f, perceptionShown - dt / Tune::ReleaseSeconds);
		if (!holding && perceptionShown <= 0) perceiving = PerceptionKind::God;

		// Where the perceiver is and what it sees with.
		perceiverShip = possessedHelm;
		perceivedReachKm = 0;
		if (possessedHelm != nullptr)
		{
			perceiverHelio = PlayerHelioKm();
			perceivedReachKm = Sensors::PilotSuite(possessedHelm).RangeKm;
		}
		else if (possessedSeat != nullptr)
		{
			auto sub = possessedSeat->Subject();
			perceiverShip = sub.Ship != nullptr ? sub.Ship : sub.Unit != nullptr ? sub.Unit->Ship : nullptr;
			if (perceiving != PerceptionKind::Eyes) perceivedReachKm = SuiteOf(possessedSeat).RangeKm;
			perceptionSweepTimer -= dt;
			if (perceptionSweepTimer <= 0)
			{
				perceptionSweepTimer = Tune::SweepSeconds;
				SweepPerception();
			}
		}
	}

	DVec3 UnitySpaceApp::PerceivedHelioOf(int i, DVec3& velocityKmS)
	{
		auto ship = Fleet()->Ships()[i];
		auto battle = ship->Battle;
		if (battle != nullptr)
		{
			auto u = battle->UnitOf(ship);
			if (u != nullptr)
			{
				velocityKmS = u->Vel;
				return Ephemeris()->Helio(battle->Body) + battle->Anchor + u->Pos;
			}
		}
		const auto& st = shipStates[i];
		velocityKmS = st.Facing.LengthSquared() > 1e-18 ? st.Facing.Normalized() * st.SpeedKmS : DVec3::Zero;
		return st.HelioKm;
	}

	void UnitySpaceApp::SweepPerception()
	{
		if (possessedSeat == nullptr || World() == nullptr || Fleet() == nullptr) return;
		if (perceivedBook == nullptr) perceivedBook = cs::New<ContactBook>();
		double jd = Clock->Jd;
		// Where the seat is: its pose, in its frame body's km (ecliptic).
		auto pose = possessedSeat->PoseAt(jd);
		if (pose.FrameBody >= 0 && pose.FrameBody < Catalog()->Count()) perceiverHelio = Ephemeris()->Helio(pose.FrameBody) + pose.OffsetKm;
		bool eyes = perceiving == PerceptionKind::Eyes;
		auto suite = SuiteOf(possessedSeat);
		auto mine = possessedSeat->Faction();
		const auto& ships = Fleet()->Ships();
		int n = cs::Math::Min(ships.Count(), shipStates.Count());
		for (int i = 0; i < n; i++)
		{
			auto s = ships[i];
			if (s == nullptr || s->Destroyed || s->GateDeparted || s->Faction == nullptr || s == perceiverShip) continue;
			if (s->Controller()->Phase == ShipPhase::Landed) continue;
			DVec3 vel;
			auto helio = PerceivedHelioOf(i, vel);
			double km = (helio - perceiverHelio).Length();
			// The eye: seen within one arcminute's range, its class closer, who it is closer still (livery, name, lights).
			double sight = VisualRangeKm(s->LengthKm());
			ContactLevel level = km <= sight * Tune::EyeIdentifyShare ? ContactLevel::Identified
				: km <= sight * Tune::EyeClassShare ? ContactLevel::Class
				: km <= sight ? ContactLevel::Blip : ContactLevel::None;
			double sig = 0;
			if (!eyes)
			{
				// Sensors: its signature (its burn, its fire), resolved by the suite; its own side squawks to it (IFF).
				sig = Signatures::Of(s, shipStates[i].Burning);
				auto scanned = Sensors::Resolve(suite, sig, km, Signatures::Transponder(s) || (mine != nullptr && s->Faction == mine));
				if (scanned > level) level = scanned;
			}
			if (level == ContactLevel::None)
			{
				perceivedBook->Observe(s, level, helio, sig, jd);
				continue;
			}
			// Where the light it was seen by left it: back along its course by the light's delay.
			double lag = km / LightKmS;
			perceivedBook->Observe(s, level, helio - vel * lag, sig, jd, perceiverHelio, jd - lag / Astro::SecondsPerDay, -1);
		}
		perceivedBook->Expire(jd, Tune::HoldSimSeconds / Astro::SecondsPerDay);
	}

	bool UnitySpaceApp::PerceivesShipModel(int i)
	{
		auto picture = PerceivedPicture();
		if (picture == nullptr) return true;
		const auto& ships = Fleet()->Ships();
		if (i < 0 || i >= ships.Count() || i >= shipStates.Count()) return true;
		auto ship = ships[i];
		if (ship == possessedHelm || ship == perceiverShip) return true;
		auto c = picture->Of(ship);
		if (c != nullptr && !c->Cold()) return true;
		// Close enough to see with the eye, whatever the scanners hold.
		DVec3 vel;
		return (PerceivedHelioOf(i, vel) - perceiverHelio).Length() <= VisualRangeKm(ship->LengthKm());
	}

	DVec3 UnitySpaceApp::PerceivedLagOffset(const cs::Ref<Ship>& ship, double lagSeconds)
	{
		if (lagSeconds <= 0 || ship == nullptr || ship->Id < 0 || ship->Id >= shipStates.Count() || ship->Id >= Fleet()->Ships().Count()) return DVec3::Zero;
		DVec3 vel;
		auto helio = PerceivedHelioOf(ship->Id, vel);
		double k = ship->Battle != nullptr ? BattleKmScale() : KmToSceneNear(ship, helio);
		return (vel * -lagSeconds).SwapYZ() * k;
	}

	float UnitySpaceApp::PossessionWash() const
	{
		if (!possessionShow.Active) return 0;
		return Bump(possessionShow.T, 0.0f, Tune::WashPeakS, Tune::WashS) * Mathf::Max(PlayerOptions::FlashScale(), 0.35f);
	}

	float UnitySpaceApp::PossessionFovBreath() const
	{
		if (!possessionShow.Active || UI::UiMotion::ProfileReducesMotion()) return 0;
		float s = Mathf::Clamp01(possessionShow.T / Tune::FovBreathS);
		float breath = Mathf::Sin(Mathf::PI * s);
		return (possessionShow.Entering ? 1.0f : -1.0f) * Tune::FovBreathDeg * breath * breath;
	}

	float UnitySpaceApp::PossessionHullRim(const cs::Ref<Ship>& ship) const
	{
		if (!possessionShow.Active || ship == nullptr || ship != possessionShow.RimShip) return 0;
		float env = possessionShow.Entering ? Bump(possessionShow.T, Tune::HaloInS, Tune::HaloPeakS + 0.1f, Tune::HaloOutS) : Bump(possessionShow.T, 0.0f, 0.3f, 1.4f) * 0.7f;
		return Tune::HullRimPeak * env * Mathf::Max(PlayerOptions::FlashScale(), 0.4f);
	}

	bool UnitySpaceApp::PlacePossession(PossessionShowState& s)
	{
		s.RimShip = nullptr;
		Target t = Target::None();
		if (s.Helm != nullptr)
		{
			if (s.Helm->Destroyed) return false;
			t = Target::OfShip(s.Helm);
		}
		else if (s.Seat != nullptr)
		{
			// A person or a mech: round the suit as it is drawn.
			DVec3 at, up, forward;
			double h = 0;
			bool person = false;
			if (auto w = cs::As<RepairWorker>(s.Seat)) person = WorkerPose(w, at, up, forward, h);
			else if (auto p = cs::As<EvaPilot>(s.Seat)) person = EvaPilotPose(p, at, up, forward, h);
			else if (auto m = cs::As<MechWalker>(s.Seat)) person = MechPose(m, at, up, forward, h);
			if (person && h > 0)
			{
				s.Centre = at + up * (h * 0.5);
				s.Radius = h * Tune::PersonGlowHeights;
				s.Forward = forward;
				s.Person = true;
				s.Placed = true;
				return true;
			}
			if (IsPersonKind(s.Seat->Kind())) return false;
			t = SeatTarget(s.Seat);
			if (t.IsNone())
			{
				// A station outside a fight: where its pose puts it, a platform's drawn size.
				auto pose = s.Seat->PoseAt(Clock->Jd);
				if (pose.FrameBody < 0 || pose.FrameBody >= bodyScene.Count()) return false;
				s.Centre = bodyScene[pose.FrameBody] + Scale()->LocalOffset((*Catalog())[pose.FrameBody], pose.OffsetKm);
				s.Radius = Scale()->ShipLength(CombatSpec::PlatformLengthKm) * 0.5;
				s.Forward = pose.Forward.SwapYZ();
				s.Person = false;
				s.Placed = true;
				return true;
			}
		}
		if (t.IsNone()) return false;
		s.Centre = FocusScene(t);
		s.Radius = TargetRadius(t);
		auto f = TargetRotation(t) * Vector3::forward();
		s.Forward = DVec3(f.x, f.y, f.z);
		s.Person = false;
		s.Placed = true;
		if (t.IsShip()) s.RimShip = t.Ship;
		return s.Radius > 0;
	}

	void UnitySpaceApp::UpdatePossessionFx()
	{
		if (systemRoot == nullptr) return;
		if (possessionFx == nullptr || possessionRoot != systemRoot)
		{
			possessionFx = cs::New<Views::PossessionFx>(this, systemRoot);
			possessionRoot = systemRoot;
		}
		Views::PossessionFrame f;
		if (possessionShow.Active)
		{
			PlacePossession(possessionShow);
			if (possessionShow.Placed)
			{
				f.Show = true;
				f.Entering = possessionShow.Entering;
				f.T = possessionShow.T;
				f.Rel = Relative(possessionShow.Centre);
				f.Radius = static_cast<float>(possessionShow.Radius);
				f.Forward = Vector3(static_cast<float>(possessionShow.Forward.X), static_cast<float>(possessionShow.Forward.Y), static_cast<float>(possessionShow.Forward.Z));
				f.Person = possessionShow.Person;
				f.Still = UI::UiMotion::ProfileReducesMotion();
				f.Brightness = Mathf::Max(PlayerOptions::FlashScale(), 0.4f);
			}
		}
		possessionFx->Update(f);
	}
}
