// Port of Unity.SpaceGod Assets/UnitySpace/Core/Fleet.cs (see docs/MIGRATION.md and the notes atop Fleet.h).
// This file holds Fleet.cs lines 1-1001; lines 1002-2962 are appended below from the other parts.

#include "Fleet.h"

#include <vector>

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "BodyOrientation.h"
#include "DVec3.h"
#include "Ephemeris.h"
#include "Hyperlanes.h"
#include "Kepler.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "Trajectories.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Economy/BeltMining.h"
#include "Economy/Economy.h"
#include "Economy/LocalTraffic.h"
#include "Economy/Propulsion.h"
#include "Infrastructure/Infrastructure.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"

namespace SpaceGod::Core
{
	Fleet::Fleet(cs::Ref<Ephemeris> ephemeris, int seed)
		: rng(seed), leaks(seed ^ 0x5eed)
	{
		this->ephemeris = ephemeris;
		catalog = ephemeris->Catalog;
	}

	// ---------------------------------------------------------------- simulation

	bool Fleet::Burns(cs::Ref<Ship> ship) { return BurnsPropellant != nullptr && BurnsPropellant(ship); }

	bool Fleet::Coarse() const { return coarse; }

	void Fleet::SetCoarse(bool value)
	{
		coarse = value;
		if (value) evasion.Clear();
	}

	DVec3 Fleet::EvasionOf(cs::Ref<Ship> ship)
	{
		std::pair<DVec3, DVec3> e;
		return evasion.TryGetValue(ship->Controller(), e) ? e.first : DVec3::Zero;
	}

	cs::Ref<Ship> Fleet::EvadingFrom(cs::Ref<Ship> ship)
	{
		cs::Ref<Ship> s;
		return evadingFrom.TryGetValue(ship->Controller(), s) ? s : nullptr;
	}

	double Fleet::SafeDistanceKm(cs::Ref<Ship> a, cs::Ref<Ship> b) { return 3 + 4 * (a->LengthKm() + b->LengthKm()); }

	bool Fleet::IsAt(cs::Ref<Ship> ship, int body)
	{
		return (ship->Phase == ShipPhase::Parked || ship->Phase == ShipPhase::Queued) && ship->Orbit != nullptr && ship->Orbit->Body == body;
	}

	void Fleet::Update(double jd)
	{
		for (int guard = 0; guard < 100000; guard++)
		{
			cs::Ref<Ship> next = nullptr;
			double when = jd;
			for (const auto& ship : ships)
			{
				if (!RunsSchedule(ship)) continue;
				double at = NextEventJd(ship);
				if (at <= when)
				{
					when = at;
					next = ship;
				}
			}
			if (next == nullptr) break;
			if (BeforeEvent) BeforeEvent(when);
			AdvancePhase(next);
		}
		StepAvoidance(jd);
	}

	bool Fleet::Manoeuvrable(cs::Ref<Ship> s)
	{
		return !s->IsEscort() && !s->Destroyed && !s->Derelict && s->Battle == nullptr &&
			(s->Phase == ShipPhase::Parked || s->Phase == ShipPhase::Transit || s->Phase == ShipPhase::Queued || s->Phase == ShipPhase::Lurk || s->Phase == ShipPhase::Adrift);
	}

	void Fleet::StepAvoidance(double jd)
	{
		double dt = cs::Double::IsNaN(lastAvoidanceJd) ? 0 : jd - lastAvoidanceJd;
		lastAvoidanceJd = jd;
		if (!AvoidCollisions || coarse || dt <= 0) return;
		auto list = cs::List<cs::Ref<Ship>>();
		// A held ship (the one the player is flying) isn't where its schedule says: nobody steers round that ghost.
		for (const auto& s : ships) if (Manoeuvrable(s) && (IsHeld == nullptr || !IsHeld(s))) list.Add(s);
		int n = list.Count();
		auto pos = cs::NewArray<DVec3>(n);
		auto vel = cs::NewArray<DVec3>(n);
		auto target = cs::NewArray<DVec3>(n);
		// bool[]: std::vector<bool> (a cs::List<bool> cannot hand out a bool&).
		std::vector<bool> threatened(static_cast<size_t>(n));
		// `near` is spelt nearby here: <windows.h> defines a `near` macro, which a UE unity build could drag in.
		std::vector<bool> nearby(static_cast<size_t>(n));
		for (int i = 0; i < n; i++)
		{
			auto s = list[i];
			std::pair<DVec3, DVec3> e{};
			evasion.TryGetValue(s, e);
			pos[i] = BaseStateOf(s, jd).HelioKm + e.first;
			vel[i] = HelioVelocityOf(s, jd) + e.second;
		}
		evadingFrom.Clear();
		for (int i = 0; i < n; i++)
		{
			for (int j = i + 1; j < n; j++)
			{
				auto a = list[i];
				auto b = list[j];
				// Aim for a little more than the minimum, so they hold clear rather than see-saw at the edge.
				double safe = SafeDistanceKm(a, b) * 1.25;
				auto r = pos[j] - pos[i];
				// Far apart and not closing fast enough to meet within the horizon: skip.
				auto dv = vel[j] - vel[i];
				double reach = safe + dv.Length() * AvoidanceHorizonDays;
				if (r.LengthSquared() > reach * reach) continue;
				double v2 = dv.LengthSquared();
				double tca = v2 > 1e-12 ? cs::Math::Max(0.0, cs::Math::Min(AvoidanceHorizonDays, -DVec3::Dot(r, dv) / v2)) : 0;
				auto miss = r + dv * tca;
				double dmin = miss.Length();
				if (dmin >= safe)
				{
					// Clear, but only just: hold the sidestep rather than drift straight back into it.
					if (dmin < safe * 1.6) nearby[static_cast<size_t>(i)] = nearby[static_cast<size_t>(j)] = true;
					continue;
				}
				DVec3 away;
				if (dmin > 1e-6) away = miss / dmin;
				else
				{
					// Dead on: break to one side, the same way for the same pair.
					auto side = DVec3::Cross(dv.LengthSquared() > 0 ? dv : DVec3::UnitX, DVec3::UnitZ);
					away = side.LengthSquared() > 1e-12 ? side.Normalized() : DVec3::UnitY;
				}
				// The nearer the pass in time, the harder they turn.
				double push = (safe - dmin) * (1 - 0.5 * tca / AvoidanceHorizonDays);
				double la = a->LengthKm(), lb = b->LengthKm();
				target[i] = target[i] - away * (push * lb / (la + lb));
				target[j] = target[j] + away * (push * la / (la + lb));
				threatened[static_cast<size_t>(i)] = threatened[static_cast<size_t>(j)] = true;
				evadingFrom[a] = b;
				evadingFrom[b] = a;
			}
		}
		// Ease each ship toward its sidestep (or back to its path): a critically damped turn that
		// settles in about a quarter of an hour, never harder than its drive can push.
		constexpr double omega = 4 / 0.0104;
		for (int i = 0; i < n; i++)
		{
			auto s = list[i];
			std::pair<DVec3, DVec3> e{};
			evasion.TryGetValue(s, e);
			bool isThreatened = threatened[static_cast<size_t>(i)], isNear = nearby[static_cast<size_t>(i)];
			// Threatened: step further aside from where it already is. Clear: drift back onto the path.
			target[i] = isThreatened ? e.first + target[i] : isNear ? e.first : DVec3::Zero;
			if (!isThreatened && !isNear && e.first.LengthSquared() < 1e-8 && e.second.LengthSquared() < 1e-8)
			{
				evasion.Remove(s);
				continue;
			}
			auto off = e.first;
			auto v = e.second;
			if (omega * dt > 1)
			{
				// A long frame (high time warp): settle part way in one go, without overshoot, rather than integrating thousands of tiny steps.
				off = DVec3::Lerp(off, target[i], cs::Math::Min(1.0, omega * dt * 0.5));
				v = DVec3::Zero;
			}
			else
			{
				double step = cs::Math::Min(dt, 0.002);
				for (double done = 0; done < dt - 1e-12; done += step)
				{
					double d = cs::Math::Min(step, dt - done);
					auto acc = (target[i] - off) * (omega * omega) - v * (2 * omega);
					double maxAcc = s->AccelG() * Astro::GravityKmPerDay2;
					if (acc.Length() > maxAcc) acc = acc.Normalized() * maxAcc;
					v = v + acc * d;
					off = off + v * d;
				}
			}
			evasion[s] = std::make_pair(off, v);
		}
		// Ships no longer flying free (landed, fighting, lost, or flown by the player) let go of their sidestep.
		auto stale = cs::List<cs::Ref<Ship>>();
		for (auto kv : evasion) if (!Manoeuvrable(kv.first) || (IsHeld != nullptr && IsHeld(kv.first))) stale.Add(kv.first);
		for (const auto& s : stale) evasion.Remove(s);
	}

	bool Fleet::RunsSchedule(cs::Ref<Ship> ship) { return !ship->IsEscort() && !ship->Destroyed && (IsHeld == nullptr || !IsHeld(ship)); }

	ShipState Fleet::StateOf(cs::Ref<Ship> ship, double jd)
	{
		auto st = BaseStateOf(ship, jd);
		auto off = EvasionOf(ship);
		if (off.LengthSquared() <= 0) return st;
		return ShipState(st.HelioKm + off, st.Facing, st.SpeedKmS, st.Burning, st.Progress, st.Heat);
	}

	DVec3 Fleet::HelioVelocityOf(cs::Ref<Ship> ship, double jd)
	{
		constexpr double h = 1e-4;
		auto v = (BaseStateOf(ship, jd + h).HelioKm - BaseStateOf(ship, jd - h).HelioKm) / (2 * h);
		auto s = ship->Controller();
		int frame = s->Phase == ShipPhase::Parked ? s->Orbit->Body
			: s->Phase == ShipPhase::Queued || s->Planetside() ? s->Ring->Body
			: -1;
		if (frame >= 0) v = v + (ephemeris->HelioAt(frame, jd + h) - ephemeris->HelioAt(frame, jd - h)) / (2 * h);
		return v;
	}

	ShipState Fleet::BaseStateOf(cs::Ref<Ship> ship, double jd)
	{
		auto s = ship->Controller();
		if (s->Phase == ShipPhase::Parked)
		{
			auto helio = ephemeris->Helio(s->Orbit->Body) + s->Orbit->OffsetAt(jd);
			return ShipState(helio, s->Orbit->TangentAt(jd), s->Orbit->SpeedKmS(), false, 0);
		}
		if (s->Phase == ShipPhase::Queued || s->Planetside()) return RingState(s, jd);
		if (s->Phase == ShipPhase::Lurk || s->Phase == ShipPhase::Adrift)
		{
			// Slide out of the lane over the first couple of hours rather than jump.
			double ease = Astro::SmoothStep(0, 0.08, jd - s->PhaseStartJd);
			auto held = LanePoint(s, jd, s->LaneHold) + s->LaneHoldSide * ease;
			auto along = LanePoint(s, jd, cs::Math::Min(1.0, s->LaneHold + 1e-4)) - LanePoint(s, jd, cs::Math::Max(0.0, s->LaneHold - 1e-4));
			return ShipState(held, along.LengthSquared() > 0 ? along.Normalized() : DVec3::UnitX, 0, false, s->LaneHold);
		}
		if (s->Phase == ShipPhase::Lane)
		{
			double progress = LaneProgress(s, jd);
			auto here = RidePoint(s, jd, progress);
			constexpr double h = 1e-3;
			auto ahead = RidePoint(s, jd + h, LaneProgress(s, jd + h));
			auto behind = RidePoint(s, jd - h, LaneProgress(s, jd - h));
			auto v = (ahead - behind) / (2 * h);
			auto facing = v.LengthSquared() > 0 ? v.Normalized() : (ahead - here).Normalized();
			return ShipState(here, facing, v.Length() / Astro::SecondsPerDay, true, progress);
		}

		if (s->Conic != nullptr) return ConicState(s, jd);

		double total = s->PhaseEndJd - s->PhaseStartJd;
		double tau = cs::Math::Max(0.0, cs::Math::Min(total, jd - s->PhaseStartJd));
		double half = total * 0.5;
		double dist = tau < half
			? 0.5 * s->AccelKmPerDay2 * tau * tau
			: s->PathLength - 0.5 * s->AccelKmPerDay2 * (total - tau) * (total - tau);

		DVec3 pos, dir;
		PointAtDistance(s, dist, pos, dir);
		auto burn = dir * (s->AccelKmPerDay2 * cs::Math::Min(tau, total - tau));
		auto velocity = burn + DriftVelocity(s, tau);
		// Same accelerate-then-decelerate profile as ever (see dist above), but the nose stays
		// pointed prograde the whole way instead of flipping to burn retrograde at the midpoint --
		// a visible 180 every single trip read as dead time, not drama, so the hull now just slows
		// (owner: "same physics, just have the ship slowing down... it doesn't look fun").
		auto thrust = dir * s->AccelKmPerDay2 + DriftAcceleration(s);
		auto at = ephemeris->HelioAt(s->FrameBody, jd) + pos + Drift(s, tau);
		return ShipState(at, thrust.Normalized(), velocity.Length() / Astro::SecondsPerDay, true,
			s->PathLength > 0 ? dist / s->PathLength : 1);
	}

	ShipState Fleet::ConicState(cs::Ref<Ship> s, double jd)
	{
		auto c = s->Conic;
		double total = c->Days;
		double tau = cs::Math::Max(0.0, cs::Math::Min(total, jd - s->PhaseStartJd));
		auto pos = c->OffsetAt(tau);
		constexpr double h = 1e-4;
		double t0 = cs::Math::Max(0.0, tau - h), t1 = cs::Math::Min(total, tau + h);
		auto v = t1 > t0 ? (c->OffsetAt(t1) - c->OffsetAt(t0)) / (t1 - t0) : c->V0;
		bool burning = tau < c->DepartureBurnDays || total - tau < c->ArrivalBurnDays;
		auto at = ephemeris->HelioAt(s->FrameBody, jd) + pos;
		return ShipState(at, v.LengthSquared() > 0 ? v.Normalized() : DVec3::UnitX, v.Length() / Astro::SecondsPerDay, burning,
			total > 0 ? tau / total : 1);
	}

	ShipState Fleet::RingState(cs::Ref<Ship> s, double jd)
	{
		auto ring = s->Ring;
		auto centre = ephemeris->Helio(ring->Body);
		double total = cs::Math::Max(s->PhaseEndJd - s->PhaseStartJd, 1e-9);
		double u = cs::Math::Max(0.0, cs::Math::Min(1.0, (jd - s->PhaseStartJd) / total));
		if (s->Phase == ShipPhase::Queued)
		{
			auto p = QueuePosition(s, jd);
			constexpr double h = 1e-4;
			auto drift = (QueuePosition(s, jd + h) - QueuePosition(s, jd - h)).Length() / (2 * h * Astro::SecondsPerDay);
			return ShipState(centre + p, (centre - (centre + p)).Normalized(), drift, false, 0);
		}
		auto up = ring->Up(jd);
		double r = RingRadiusAt(s, u);
		double rate = (RingRadiusAt(s, cs::Math::Min(1.0, u + 1e-4)) - RingRadiusAt(s, cs::Math::Max(0.0, u - 1e-4))) / (2e-4 * total * Astro::SecondsPerDay);
		float heat = 0;
		if (s->Phase != ShipPhase::Landed)
		{
			double altitude = r - ring->SurfaceKm;
			// Brightest on the way down, a little after the interface; a gentler glow climbing out.
			double depth = 1 - altitude / cs::Math::Max(ring->EntryInterfaceKm, 1.0);
			heat = static_cast<float>(cs::Math::Max(0.0, cs::Math::Min(1.0, depth * 1.6)) * cs::Math::Max(0.0, cs::Math::Min(1.0, altitude / (ring->EntryInterfaceKm * 0.15))) * (s->Phase == ShipPhase::Descent ? 1 : 0.35));
		}
		// Tail-first like a landing rocket: the drive points at the ground, braking on the way
		// down and lifting on the way up.
		auto facing = up;
		return ShipState(centre + up * r, facing, cs::Math::Abs(rate), s->Phase != ShipPhase::Landed, u, heat);
	}

	DVec3 Fleet::QueuePosition(cs::Ref<Ship> s, double jd)
	{
		auto ring = s->Ring;
		double ahead = cs::Math::Max(0.0, s->PhaseEndJd - jd) / cs::Math::Max(ring->EntryIntervalDays, 1e-9);
		double along = cs::Math::Min(ring->HoldKm(), QueueSpacingKm * ahead);
		auto slot = ring->Point(ring->RadiusKm + along)->OffsetAt(jd);
		double total = cs::Math::Max(s->PhaseEndJd - s->PhaseStartJd, 1e-9);
		double join = Astro::SmoothStep(0, cs::Math::Min(total, 0.01), jd - s->PhaseStartJd);
		return DVec3::Lerp(s->HoldPoint->OffsetAt(jd), slot, join);
	}

	double Fleet::RingRadiusAt(cs::Ref<Ship> s, double u)
	{
		auto ring = s->Ring;
		if (s->Phase == ShipPhase::Landed) return ring->SurfaceKm;
		// Fast through the empty upper part, slow and careful through the air near the ground.
		double e = s->Phase == ShipPhase::Descent ? 1 - cs::Math::Pow(1 - u, 2.2) : cs::Math::Pow(u, 2.2);
		double top = ring->RadiusKm, bottom = ring->SurfaceKm;
		return s->Phase == ShipPhase::Descent ? top + (bottom - top) * e : bottom + (top - bottom) * e;
	}

	double Fleet::LaneProgress(cs::Ref<Ship> s, double jd)
	{
		return s->LaneStartProgress + (s->LaneEndProgress - s->LaneStartProgress) * Hyperlanes::Progress((jd - s->PhaseStartJd) / cs::Math::Max(s->PhaseEndJd - s->PhaseStartJd, 1e-9));
	}

	DVec3 Fleet::RidePoint(cs::Ref<Ship> s, double jd, double progress)
	{
		return LanePoint(s, jd, progress) + s->LaneHoldSide * LaneRejoin(s, jd);
	}

	double Fleet::LaneRejoin(cs::Ref<Ship> s, double jd)
	{
		return s->LaneHoldSide.LengthSquared() > 0 ? 1 - Astro::SmoothStep(0, 0.08, jd - s->PhaseStartJd) : 0;
	}

	DVec3 Fleet::LanePoint(cs::Ref<Ship> s, double jd, double progress)
	{
		auto a = ephemeris->HelioAt(s->LaneLeg.From, jd) + s->LaneFromOffset;
		auto b = ephemeris->HelioAt(s->LaneLeg.To, jd) + s->LaneToOffset;
		return Lanes->CurveKm(s->LaneLeg.Lane, a, b, progress, jd, s->LaneStar, s->LaneShape);
	}

	DVec3 Fleet::TransitOffset(cs::Ref<Ship> s, double progress)
	{
		// On a transfer orbit progress is by time (see ConicState).
		if (s->Conic != nullptr) return s->Conic->OffsetAt(cs::Math::Max(0.0, cs::Math::Min(1.0, progress)) * s->Conic->Days);
		double dist = cs::Math::Max(0.0, cs::Math::Min(1.0, progress)) * s->PathLength;
		DVec3 pos, unusedDir;
		PointAtDistance(s, dist, pos, unusedDir);
		double total = s->PhaseEndJd - s->PhaseStartJd;
		double tau = s->AccelKmPerDay2 <= 0 ? progress * total
			: dist < s->PathLength * 0.5
				? cs::Math::Sqrt(2 * dist / s->AccelKmPerDay2)
				: total - cs::Math::Sqrt(cs::Math::Max(0.0, 2 * (s->PathLength - dist) / s->AccelKmPerDay2));
		return pos + Drift(s, tau);
	}

	DVec3 Fleet::TransitOffsetAt(cs::Ref<Ship> s, double tau)
	{
		if (s->Conic != nullptr) return s->Conic->OffsetAt(tau);
		double total = s->PhaseEndJd - s->PhaseStartJd;
		tau = cs::Math::Max(0.0, cs::Math::Min(total, tau));
		double dist = tau < total * 0.5
			? 0.5 * s->AccelKmPerDay2 * tau * tau
			: s->PathLength - 0.5 * s->AccelKmPerDay2 * (total - tau) * (total - tau);
		DVec3 pos, unusedDir;
		PointAtDistance(s, dist, pos, unusedDir);
		return pos + Drift(s, tau);
	}

	// A steady acceleration (v1 - v0) / T on top of the flip-and-burn takes the ship from its
	// departure velocity to its arrival velocity; its displacement is v0·t + (v1 - v0)·t² / 2T.
	DVec3 Fleet::Drift(cs::Ref<Ship> s, double tau)
	{
		double total = s->PhaseEndJd - s->PhaseStartJd;
		return s->DepartureVelocity * tau + (s->ArrivalVelocity - s->DepartureVelocity) * (tau * tau / (2 * total));
	}

	DVec3 Fleet::DriftVelocity(cs::Ref<Ship> s, double tau)
	{
		return DVec3::Lerp(s->DepartureVelocity, s->ArrivalVelocity, tau / (s->PhaseEndJd - s->PhaseStartJd));
	}

	DVec3 Fleet::DriftAcceleration(cs::Ref<Ship> s)
	{
		return (s->ArrivalVelocity - s->DepartureVelocity) / (s->PhaseEndJd - s->PhaseStartJd);
	}

	void Fleet::PointAtDistance(cs::Ref<Ship> s, double dist, DVec3& pos, DVec3& dir)
	{
		const auto& path = s->Path;
		for (int i = 1; i < path.Count(); i++)
		{
			if (dist <= s->PathCumulative[i] || i == path.Count() - 1)
			{
				double segStart = s->PathCumulative[i - 1];
				double segLen = s->PathCumulative[i] - segStart;
				double t = segLen > 0 ? cs::Math::Max(0.0, cs::Math::Min(1.0, (dist - segStart) / segLen)) : 1;
				pos = DVec3::Lerp(path[i - 1], path[i], t);
				dir = (path[i] - path[i - 1]).Normalized();
				return;
			}
		}
		pos = path[path.Count() - 1];
		dir = DVec3::UnitX;
	}

	double Fleet::RemainingDays(cs::Ref<Ship> ship, double jd) { return cs::Math::Max(0.0, ship->Controller()->PhaseEndJd - jd); }

	double Fleet::NextEventJd(cs::Ref<Ship> s) { return s->Phase == ShipPhase::Transit && s->StrandJd < s->PhaseEndJd ? s->StrandJd : s->PhaseEndJd; }

	void Fleet::AdvancePhase(cs::Ref<Ship> ship)
	{
		if (ship->Phase == ShipPhase::Transit && ship->StrandJd < ship->PhaseEndJd)
		{
			Strand(ship, ship->StrandJd);
			return;
		}
		if (ship->Phase == ShipPhase::Parked && ship->OutOfPropellant)
		{
			TankerArrives(ship);
			return;
		}
		if (ship->Phase == ShipPhase::Parked)
		{
			// Holding position (a siege, a refit, an ambush): decide again rather than fly to itself.
			// A free ship's hop out to open space near the same world is a real trip, though.
			// A ship bound for a gate is leaving even from the root it is parked at (one gate to the next).
			// So is an ore tug's push in from the field to the very world the field lies round (U.SPC-T498).
			if (ship->DestinationBody == ship->Orbit->Body && !ship->Wander && !ship->Homing && !ship->GateKm.has_value()) ScheduleNext(ship, ship->PhaseEndJd);
			else Depart(ship);
		}
		else if (ship->Phase == ShipPhase::Lane) EndLane(ship);
		else if (ship->Phase == ShipPhase::Lurk || ship->Phase == ShipPhase::Adrift) ResumeLane(ship, ship->PhaseEndJd);
		else if (ship->Phase == ShipPhase::Queued) BeginDescent(ship);
		else if (ship->Phase == ShipPhase::Descent) Arrive(ship, true);
		else if (ship->Phase == ShipPhase::Landed)
		{
			if (ship->DestinationBody == ship->Ring->Body) ScheduleNext(ship, ship->PhaseEndJd);
			else BeginAscent(ship);
		}
		else if (ship->Phase == ShipPhase::Ascent) Depart(ship);
		// ship.Journey != null && ship.JourneyIndex < ship.Journey.Count (a null journey is the empty list).
		else if (ship->Journey.Count() > 0 && ship->JourneyIndex < ship->Journey.Count() && ship->Orbit->Fixed) ReachRamp(ship, ship->PhaseEndJd);
		else if (ship->Ring != nullptr && ship->Orbit == ship->HoldPoint) JoinQueue(ship);
		else Arrive(ship);
	}

	// ---------------------------------------------------------------- docking rings

	cs::Ref<ParkingOrbit> Fleet::ArrivalFor(cs::Ref<Ship> ship, int body)
	{
		ship->Ring = nullptr;
		ship->HoldPoint = nullptr;
		if (Lands == nullptr || !Lands(ship)) return NewParkingOrbit(body, ship);
		cs::Ref<DockingRing> best = nullptr;
		for (const auto& r : Rings)
			if (r->Body == body && (best == nullptr || r->NextEntryJd < best->NextEntryJd)) best = r;
		if (best == nullptr) return NewParkingOrbit(body, ship);
		ship->Ring = best;
		ship->HoldPoint = best->HoldPoint(ship);
		return ship->HoldPoint;
	}

	void Fleet::JoinQueue(cs::Ref<Ship> ship)
	{
		double jd = ship->PhaseEndJd;
		auto ring = ship->Ring;
		double entry = cs::Math::Max(jd + MinQueueDays, ring->NextEntryJd);
		ring->NextEntryJd = entry + ring->EntryIntervalDays;
		ship->Phase = ShipPhase::Queued;
		ship->Path = cs::List<DVec3>();
		ship->PhaseStartJd = jd;
		ship->PhaseEndJd = entry;
		if (ReachedRing) ReachedRing(ship, jd);
	}

	void Fleet::BeginDescent(cs::Ref<Ship> ship)
	{
		double jd = ship->PhaseEndJd;
		ship->Orbit = ship->Ring->RingPoint();
		ship->Phase = ShipPhase::Descent;
		ship->PhaseStartJd = jd;
		ship->PhaseEndJd = jd + ship->Ring->DescentDays;
	}

	void Fleet::BeginAscent(cs::Ref<Ship> ship)
	{
		double jd = ship->PhaseEndJd;
		ship->Phase = ShipPhase::Ascent;
		ship->PhaseStartJd = jd;
		ship->PhaseEndJd = jd + ship->Ring->DescentDays;
	}

	double Fleet::RingOverheadDays(cs::Ref<Ship> ship, int body, double arriveJd)
	{
		if (Lands == nullptr || !Lands(ship)) return 0;
		double best = cs::Double::MaxValue;
		for (const auto& r : Rings)
			if (r->Body == body) best = cs::Math::Min(best, cs::Math::Max(0.0, r->NextEntryJd - arriveJd) + MinQueueDays + 2 * r->DescentDays);
		return best == cs::Double::MaxValue ? 0 : best;
	}

	void Fleet::Depart(cs::Ref<Ship> ship)
	{
		double departJd = ship->PhaseEndJd;
		int from = ship->Orbit->Body;
		int to = ship->DestinationBody;
		ship->FinalBody = to;
		ship->Journey = cs::List<LaneHop>();
		ship->JourneyIndex = 0;
		if (ship->GateKm.has_value() && to == catalog->Root()->Index)
		{
			DVec3 gate = *ship->GateKm;
			// A plotted course (U.SPC-T516) crosses the system by the hyperlanes where they run to the gate.
			if (ship->Voyage != nullptr && ship->Voyage->Purpose == VoyagePurpose::Plotted && TryGateJourney(ship, from, gate, departJd))
			{
				auto first = ship->Journey[0];
				Burn(ship, departJd, first.From, [&]() { return RampOrbit(first, first.From, departJd); }, std::nullopt, true);
				return;
			}
			// Out to a jump gate: a fixed point off the system's root.
			Burn(ship, departJd, to, [&]() { return FixedPoint(to, gate); });
			return;
		}
		if (ship->StakeoutKm.has_value() && to == catalog->Root()->Index)
		{
			DVec3 approach = *ship->StakeoutKm;
			// Raiders out to lie in wait off a gate (U.SPC-T94): the same fixed point off the root.
			Burn(ship, departJd, to, [&]() { return FixedPoint(to, approach); });
			return;
		}
		if (ship->PicketGate != nullptr && to == catalog->Root()->Index)
		{
			// A gate picket back out to its post off its gate's exit (U.SPC-T480).
			auto post = ship->PicketPostKm;
			Burn(ship, departJd, to, [&]() { return FixedPoint(to, post); });
			return;
		}
		// A slow boat that judged the lanes' tolls dearer than the time flies straight across on a transfer orbit.
		bool direct = ship->PlannedDirect && UsesConics(ship);
		ship->PlannedDirect = false;
		if (Lanes != nullptr && !direct)
		{
			int fromSystem = Lanes->SystemOf(from), toSystem = Lanes->SystemOf(to);
			if (fromSystem != toSystem)
			{
				// A null route clears the journey (the empty list stands for C#'s null Journey; a route is never empty).
				auto found = Lanes->RouteFor(ship, fromSystem, toSystem, departJd);
				ship->Journey = found ? *found : cs::List<LaneHop>();
			}
		}
		if (ship->Journey.Count() > 0)
		{
			auto hop = ship->Journey[0];
			Burn(ship, departJd, hop.From, [&]() { return RampOrbit(hop, hop.From, departJd); }, std::nullopt, true);
		}
		else if (ship->Wander) Burn(ship, departJd, to, [&]() { return WanderOrbit(to, ship); });
		else
		{
			auto home = ship->Orbit;
			auto ring = ship->Ring;
			auto hold = ship->HoldPoint;
			Burn(ship, departJd, to, [&]() { return ArrivalFor(ship, to); }, std::nullopt, true);
			// The planned transfer orbit failed at the real departure: the straight flip-and-burn was only ever the
			// comparison baseline, so take the lanes the planner priced as the alternative (U.SPC-T390).
			if (direct && ship->Conic == nullptr && Lanes != nullptr && !ship->Wander)
			{
				int fromSystem = Lanes->SystemOf(from), toSystem = Lanes->SystemOf(to);
				cs::Ref<cs::List<LaneHop>> route = fromSystem != toSystem ? Lanes->Route(fromSystem, toSystem, departJd) : nullptr;
				if (route != nullptr)
				{
					RefundUnflown(ship, departJd);
					ship->Orbit = home;
					ship->Ring = ring;
					ship->HoldPoint = hold;
					ship->Journey = *route;
					ship->JourneyIndex = 0;
					auto hop = (*route)[0];
					Burn(ship, departJd, hop.From, [&]() { return RampOrbit(hop, hop.From, departJd); }, std::nullopt, true);
				}
			}
		}
	}

	bool Fleet::TryGateJourney(cs::Ref<Ship> ship, int from, DVec3 gate, double jd)
	{
		if (Lanes == nullptr || Lanes->GateSpurs.Count() == 0) return false;
		cs::Ref<Lane> target = nullptr;
		for (const auto& spur : Lanes->GateSpurs)
			if ((Hyperlanes::GateHelio(spur->Gate) - gate).LengthSquared() < 1) target = spur;
		if (target == nullptr) return false;
		auto hops = cs::List<LaneHop>();
		int at;
		if (from == catalog->Root()->Index)
		{
			// At the gate it came through: out from there along its spur.
			auto here = ship->Orbit->OffsetAt(jd);
			cs::Ref<Lane> start = nullptr;
			double nearest = AtGateKm;
			for (const auto& spur : Lanes->GateSpurs)
			{
				double km = (Hyperlanes::GateHelio(spur->Gate) - here).Length();
				if (km >= nearest) continue;
				nearest = km;
				start = spur;
			}
			if (start == nullptr || start == target) return false;
			hops.Add(LaneHop(start, start->B, start->A));
			at = start->A;
		}
		else at = Lanes->SystemOf(from);
		if (at != target->A)
		{
			auto between = Lanes->Route(at, target->A, jd);
			if (between == nullptr) return false;
			hops.AddRange(*between);
		}
		hops.Add(LaneHop(target, target->A, target->B));
		ship->Journey = hops;
		ship->JourneyIndex = 0;
		return true;
	}

	cs::Ref<ParkingOrbit> Fleet::WanderOrbit(int body, cs::Ref<Ship> /*ship*/)
	{
		auto b = (*catalog)[body];
		double inner = b->RadiusKm * 3;
		double outer = cs::Math::Max(inner * 1.5, cs::Math::Min(ScaleModel::NeighbourhoodKm(b) * 0.6, cs::Math::Max(b->RadiusKm * 12, b->MoonReachKm * 0.9)));
		double r = inner + (outer - inner) * rng.NextDouble();
		// C# evaluates the arguments left to right; each Jitter draws from rng, so sequence them.
		double jx = Jitter(1);
		double jy = Jitter(1);
		double jz = Jitter(1);
		auto normal = DVec3(jx, jy, jz);
		if (normal.LengthSquared() < 1e-6) normal = DVec3::UnitZ;
		normal = normal.Normalized();
		auto reference = cs::Math::Abs(normal.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX;
		auto u = DVec3::Cross(reference, normal).Normalized();
		auto orbit = cs::New<ParkingOrbit>();
		orbit->Body = body;
		orbit->RadiusKm = r;
		orbit->U = u;
		orbit->V = DVec3::Cross(normal, u);
		orbit->RateRadPerDay = Kepler::CircularRate(cs::Math::Max(b->Gm, 1e-12), r);
		return orbit;
	}

	cs::Ref<ParkingOrbit> Fleet::RampOrbit(LaneHop hop, int system, double jd) { return FixedPoint(system, Lanes->RampOffset(hop.Lane, system, jd + 0.4)); }

	cs::Ref<ParkingOrbit> Fleet::FixedPoint(int body, DVec3 offset)
	{
		auto u = offset.Normalized();
		auto v = DVec3::Cross(cs::Math::Abs(u.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX, u).Normalized();
		auto orbit = cs::New<ParkingOrbit>();
		orbit->Body = body;
		orbit->RadiusKm = offset.Length();
		orbit->U = u;
		orbit->V = v;
		orbit->RateRadPerDay = 0;
		orbit->Fixed = true;
		return orbit;
	}

	void Fleet::ReachRamp(cs::Ref<Ship> ship, double jd)
	{
		auto hop = ship->Journey[ship->JourneyIndex];
		if (Lanes->Carries(hop.Lane))
		{
			StartLane(ship, jd);
			return;
		}
		int to = ship->FinalBody;
		int toSystem = Lanes->SystemOf(to);
		cs::Ref<cs::List<LaneHop>> route = toSystem != hop.From ? Lanes->RouteFor(ship, hop.From, toSystem, jd) : nullptr;
		ship->Journey = route ? *route : cs::List<LaneHop>();
		ship->JourneyIndex = 0;
		if (route != nullptr)
		{
			auto next = (*route)[0];
			Burn(ship, jd, next.From, [&]() { return RampOrbit(next, next.From, jd); }, std::nullopt, true);
		}
		else if (ship->GateKm.has_value() && to == catalog->Root()->Index)
		{
			DVec3 gate = *ship->GateKm;
			Burn(ship, jd, to, [&]() { return FixedPoint(to, gate); });
		}
		else Burn(ship, jd, to, [&]() { return ArrivalFor(ship, to); }, std::nullopt, true);
	}

	bool Fleet::StallRide(cs::Ref<Ship> ship, double at, double days)
	{
		if (ship->Phase != ShipPhase::Lane || ship->LaneStartProgress > 0 || ship->LaneEndProgress < 1) return false;
		at = cs::Math::Max(0.05, cs::Math::Min(0.95, at));
		ship->LaneEndProgress = at;
		ship->PhaseEndJd = ship->PhaseStartJd + (ship->PhaseEndJd - ship->PhaseStartJd) * at;
		ship->LurkDays = cs::Math::Max(0.01, days);
		ship->Stalled = true;
		return true;
	}

	void Fleet::StartLane(cs::Ref<Ship> ship, double jd)
	{
		auto hop = ship->Journey[ship->JourneyIndex];
		ship->LaneLeg = hop;
		ship->LaneFromOffset = ship->Orbit->OffsetAt(jd);
		auto a = ephemeris->HelioAt(hop.From, jd) + ship->LaneFromOffset;
		auto estimate = Lanes->RideDays(ship, hop.Lane, Lanes->LaneLengthKm(hop.Lane, jd));
		ship->LaneToOffset = Lanes->RampOffset(hop.Lane, hop.To, jd + estimate);
		auto b = ephemeris->HelioAt(hop.To, jd) + ship->LaneToOffset;
		ship->LaneStar = Lanes->KeepOutStar(a, b, jd);
		ship->LaneShape = Lanes->ShapeOf(hop.Lane, a, b, ship->LaneStar, jd);
		ship->LaneHoldSide = DVec3::Zero;
		// A fresh ride: no point left part-way on an earlier lane to get back in at (CanRejoin).
		ship->LaneHold = 0;
		ship->LaneLengthKm = Lanes->CurveLengthKm(hop.Lane, a, b, jd, ship->LaneStar, ship->LaneShape);
		ship->DepartureOrbit = ship->Orbit;
		ship->OriginBody = hop.From;
		ship->FrameBody = 0;
		ship->Path = cs::List<DVec3>();
		ship->Conic = nullptr;
		ship->Phase = ShipPhase::Lane;
		ship->LaneSurged = false;
		ship->Stalled = false;
		ship->LaneStartProgress = 0;
		ship->LaneEndProgress = ship->LurkLane == hop.Lane ? ship->LurkAt : 1;
		ship->PhaseStartJd = jd;
		// A worn lane (U.SPC-T481) rides at its condition's pace.
		ship->PhaseEndJd = jd + Lanes->RideDays(ship, hop.Lane, ship->LaneLengthKm) * ship->LaneEndProgress;
		if (LaneJoined) LaneJoined(ship, hop, jd);
	}

	void Fleet::ResumeLane(cs::Ref<Ship> ship, double jd)
	{
		ship->Phase = ShipPhase::Lane;
		ship->LaneSurged = false;
		ship->Stalled = false;
		ship->LaneStartProgress = ship->LaneHold;
		ship->LaneEndProgress = 1;
		// LaneHoldSide is kept: the ship eases back in from beside the lane over the first hours.
		ship->PhaseStartJd = jd;
		ship->PhaseEndJd = jd + Lanes->RideDays(ship, ship->LaneLeg.Lane, ship->LaneLengthKm) * cs::Math::Max(1e-3, 1 - ship->LaneHold);
	}

	void Fleet::PullOut(cs::Ref<Ship> ship, double jd, double progress, DVec3 side)
	{
		ship->Phase = ShipPhase::Adrift;
		ship->LaneHold = cs::Math::Max(0.0, cs::Math::Min(1.0, progress));
		ship->LaneHoldSide = side;
		ship->PhaseStartJd = jd;
		ship->PhaseEndJd = jd + 0.5;
	}

	DVec3 Fleet::OffLane(cs::Ref<Ship> s, double jd, double progress, double km, int sign)
	{
		auto along = LanePoint(s, jd, cs::Math::Min(1.0, progress + 1e-3)) - LanePoint(s, jd, cs::Math::Max(0.0, progress - 1e-3));
		auto side = DVec3::Cross(along, DVec3::UnitZ);
		return side.LengthSquared() > 0 ? side.Normalized() * (km * sign) : DVec3::UnitX * km;
	}

	void Fleet::EndLane(cs::Ref<Ship> ship)
	{
		double jd = ship->PhaseEndJd;
		auto hop = ship->LaneLeg;
		if (ship->LaneEndProgress < 1)
		{
			// Stop part-way and wait just off the lane.
			ship->Phase = ShipPhase::Lurk;
			ship->LaneHold = ship->LaneEndProgress;
			ship->LaneHoldSide = OffLane(ship, jd, ship->LaneHold, 30000, ship->Id % 2 == 0 ? 1 : -1);
			ship->PhaseStartJd = jd;
			ship->PhaseEndJd = jd + ship->LurkDays;
			ship->LurkLane = nullptr;
			return;
		}
		// Ridden to its end: nothing left part-way to rejoin.
		ship->LaneHold = 0;
		ship->Orbit = FixedPoint(hop.To, ship->LaneToOffset);
		ship->JourneyIndex++;
		if (ship->JourneyIndex < ship->Journey.Count())
		{
			auto next = ship->Journey[ship->JourneyIndex];
			Burn(ship, jd, next.From, [&]() { return RampOrbit(next, next.From, jd); }, std::nullopt, true);
		}
		else
		{
			int to = ship->FinalBody;
			// Off the spur at a jump gate (a plotted course): in to the gate itself, not into an orbit round the star.
			if (ship->GateKm.has_value() && to == catalog->Root()->Index)
			{
				DVec3 gate = *ship->GateKm;
				Burn(ship, jd, to, [&]() { return FixedPoint(to, gate); });
			}
			else Burn(ship, jd, to, [&]() { return ArrivalFor(ship, to); }, std::nullopt, true);
		}
	}

	// ---------------------------------------------------------------- the player's own rides (U.SPC-T41)

	void Fleet::JoinLane(cs::Ref<Ship> ship, LaneHop hop, DVec3 helioKm, double jd)
	{
		ship->Journey = cs::List<LaneHop>{ hop };
		ship->JourneyIndex = 0;
		ship->FinalBody = ship->DestinationBody = RideDestination(hop);
		ship->Wander = false;
		ship->Ring = nullptr;
		ship->HoldPoint = nullptr;
		ship->LurkLane = nullptr;
		ship->StrandedInFlight = false;
		ship->Orbit = FixedPoint(hop.From, helioKm - ephemeris->HelioAt(hop.From, jd));
		StartLane(ship, jd);
	}

	int Fleet::RideDestination(LaneHop hop) { return hop.Lane->Gate != nullptr && hop.To == hop.Lane->B ? hop.From : hop.To; }

	int Fleet::RideRings(cs::Ref<Ship> ship, double jd, int& passed, bool& accelerating)
	{
		int count = Hyperlanes::RingCount(ship->LaneLengthKm);
		passed = Hyperlanes::RingsPassed(LaneProgress(ship, jd), count);
		double span = cs::Math::Max(ship->PhaseEndJd - ship->PhaseStartJd, 1e-9);
		accelerating = Hyperlanes::RingsAccelerate((jd - ship->PhaseStartJd) / span);
		return count;
	}

	bool Fleet::RideOver(cs::Ref<Ship> ship, double jd) { return ship->Phase == ShipPhase::Lane && jd >= ship->PhaseEndJd; }

	// ---------------- Fleet.cs lines 1002-1988 (US-T8 part 2)

	/// <summary>
	/// A piloted ride has reached its far ramp: carry on along <paramref name="next"/> (one of
	/// <see cref="Hyperlanes.OnwardHops"/>) from right where the hull is, or, with none, come off and be
	/// pinned there under the pilot's hand, facing <paramref name="facing"/>.
	/// </summary>
	void Fleet::RampReached(cs::Ref<Ship> ship, cs::Nullable<LaneHop> next, DVec3 facing, double jd)
	{
		auto hop = ship->LaneLeg;
		if (next.has_value() && next->From == hop.To)
		{
			LaneHop onward = *next;
			ship->Orbit = FixedPoint(hop.To, ship->LaneToOffset);
			// A fresh list: a journey may be a planner's cached route, never to be added to.
			ship->Journey = cs::List<LaneHop>{ onward };
			ship->JourneyIndex = 0;
			ship->FinalBody = ship->DestinationBody = RideDestination(onward);
			StartLane(ship, jd);
			return;
		}
		// Brought off at the far ramp: the ride is done, nothing left part-way to rejoin.
		ship->LaneHold = 0;
		PinAt(ship, hop.To, ship->LaneToOffset, facing, jd);
	}

	// ---------------------------------------------------------------- the player's answers to an interdiction (U.SPC-T42)

	/// <summary>
	/// A pilot boosting through: the rest of the ride goes at <see cref="SurgeFactor"/> times its pace, from right where
	/// the hull is (its progress along the lane does not jump), so it reaches every point ahead, and its off-ramp, sooner.
	/// Once a ride; false if the hull is not riding or has already surged.
	/// </summary>
	bool Fleet::SurgeRide(cs::Ref<Ship> ship, double jd)
	{
		if (ship->Phase != ShipPhase::Lane || ship->LaneSurged || jd >= ship->PhaseEndJd) return false;
		double span = cs::Math::Max(ship->PhaseEndJd - ship->PhaseStartJd, 1e-9);
		double u = cs::Math::Max(0.0, cs::Math::Min(1.0, (jd - ship->PhaseStartJd) / span));
		double surged = span / SurgeFactor;
		// A hull still easing back in from beside the lane times that from PhaseStartJd too: what is left of its sideways
		// offset is rescaled so moving the start does not pop it outward (U.SPC-T388).
		double eased = LaneRejoin(ship, jd);
		ship->PhaseStartJd = jd - u * surged;
		ship->PhaseEndJd = ship->PhaseStartJd + surged;
		double easedNow = LaneRejoin(ship, jd);
		if (easedNow > 0) ship->LaneHoldSide *= eased / easedNow;
		ship->LaneSurged = true;
		return true;
	}

	/// <summary>
	/// A pilot drops out of the lane early, on their own terms: off it right where the hull is, pinned there in the
	/// system root's frame facing <paramref name="facing"/>, the point it left kept (<see cref="Ship.LaneHold"/>) so it
	/// can get back in there (<see cref="RejoinLane"/>).
	/// </summary>
	void Fleet::DropOut(cs::Ref<Ship> ship, double jd, DVec3 facing)
	{
		if (ship->Phase != ShipPhase::Lane) return;
		auto helio = StateOf(ship, jd).HelioKm;
		ship->LaneHold = LaneProgress(ship, jd);
		int root = catalog->Root()->Index;
		PinAt(ship, root, helio - ephemeris->HelioAt(root, jd), facing, jd);
	}

	/// <summary>Where a hull out of its lane would get back in: the lane line at the point it left, true heliocentric km.</summary>
	DVec3 Fleet::RejoinPoint(cs::Ref<Ship> ship, double jd) { return LanePoint(ship, jd, ship->LaneHold); }

	/// <summary>Whether a hull at <paramref name="helioKm"/> that left its lane part-way can get back in where it left.</summary>
	bool Fleet::CanRejoin(cs::Ref<Ship> ship, DVec3 helioKm, double jd)
	{
		return ship->LaneLeg.Lane != nullptr && ship->Phase != ShipPhase::Lane && ship->Battle == nullptr && !ship->Destroyed &&
			ship->LaneHold > 0 && ship->LaneHold < 1 && (helioKm - RejoinPoint(ship, jd)).Length() <= RejoinKm;
	}

	/// <summary>
	/// Back into the lane a hull left part-way (dropped out, or pulled out by a pulse and fought clear): it eases in from
	/// where it is (<paramref name="helioKm"/>) to the point it left and rides on from there to the far ramp, like a
	/// ship that survived an ambush (<see cref="ResumeLane"/>). False if it is not near enough (<see cref="CanRejoin"/>).
	/// </summary>
	bool Fleet::RejoinLane(cs::Ref<Ship> ship, DVec3 helioKm, double jd)
	{
		if (!CanRejoin(ship, helioKm, jd)) return false;
		auto hop = ship->LaneLeg;
		ship->Journey = cs::List<LaneHop>{ hop };
		ship->JourneyIndex = 0;
		ship->FinalBody = hop.To;
		ship->DestinationBody = hop.To;
		ship->Orbit = FixedPoint(hop.From, ship->LaneFromOffset);
		ship->LaneHoldSide = helioKm - RejoinPoint(ship, jd);
		ResumeLane(ship, jd);
		return true;
	}

	/// <summary>
	/// Changes course mid-flight: from wherever the ship is now, still moving as it is, a new
	/// flip-and-burn to <paramref name="to"/>. For a burn between worlds (not a hyperlane ride).
	/// Returns false if the ship is not on such a burn.
	/// </summary>
	bool Fleet::Divert(cs::Ref<Ship> ship, int to, double jd)
	{
		// (C#'s `ship.Path == null` is an empty Path here, see Fleet.h.)
		if (ship->IsEscort() || ship->Destroyed || ship->Battle != nullptr || ship->Phase != ShipPhase::Transit || ship->Path.Count() == 0) return false;
		if (ship->JourneyIndex < ship->Journey.Count()) return false;
		if (jd >= ship->PhaseEndJd - 1e-4) return false;
		int frame = ship->FrameBody;
		const double h = 1e-4;
		auto here = BaseStateOf(ship, jd).HelioKm;
		DVec3 ahead = BaseStateOf(ship, jd + h).HelioKm;
		DVec3 behind = BaseStateOf(ship, jd - h).HelioKm;
		DVec3 frameAhead = ephemeris->HelioAt(frame, jd + h);
		DVec3 frameBehind = ephemeris->HelioAt(frame, jd - h);
		auto velocity = (ahead - behind) / (2 * h)
			- (frameAhead - frameBehind) / (2 * h);
		RefundUnflown(ship, jd);
		ship->Orbit = FixedPoint(frame, here - ephemeris->HelioAt(frame, jd));
		ship->Journey = cs::List<LaneHop>();
		ship->JourneyIndex = 0;
		ship->FinalBody = to;
		ship->DestinationBody = to;
		ship->Wander = false;
		Burn(ship, jd, to, [this, ship, to]() { return ArrivalFor(ship, to); }, velocity);
		return true;
	}

	/// <summary>A flip-and-burn from the ship's current orbit (or ramp) to <paramref name="to"/>, arriving in the orbit the factory gives.</summary>
	void Fleet::Burn(cs::Ref<Ship> ship, double departJd, int to, cs::Func<cs::Ref<ParkingOrbit>> arrivalOrbit,
		cs::Nullable<DVec3> initialVelocity, bool allowConic)
	{
		ship->Conic = nullptr;
		ship->GravityLossKmS = 0;
		int from = ship->Orbit->Body;
		int frame = CommonFrame(from, to);
		double keepOut = FrameKeepOutKm(frame);
		auto start = RelativeTo(frame, from, departJd) + ship->Orbit->OffsetAt(departJd);
		// Leaving an orbit it moves with the orbit; changing course mid-flight it keeps its own velocity.
		DVec3 frameVelocity = VelocityRelativeTo(frame, from, departJd);
		auto startVelocity = frameVelocity + (initialVelocity.has_value() ? *initialVelocity : ship->Orbit->VelocityAt(departJd));
		// Match velocities at both ends unless that path runs into something the rest-to-rest
		// one in the frame avoids; if both are blocked, try another parking orbit plane.
		cs::Ref<ParkingOrbit> arrival = nullptr;
		bool clear = false;
		// A ship already under way cannot stop dead to start from rest: it keeps its momentum.
		bool underWay = initialVelocity.has_value();
		for (int attempt = 0; attempt < 6 && !clear; attempt++)
		{
			arrival = arrivalOrbit();
			clear = Plan(ship, frame, from, to, departJd, start, startVelocity, arrival, keepOut, true)
				|| (!underWay && Plan(ship, frame, from, to, departJd, start, startVelocity, arrival, keepOut, false));
		}
		// Nothing cleared: fly rest to rest, with no drift, so the ship keeps exactly to its path
		// (which goes round the frame's centre) rather than drifting through it.
		if (!clear) Plan(ship, frame, from, to, departJd, start, startVelocity, arrival, keepOut, underWay);
		if (OrbitalTransfers)
		{
			// A torch holds its straight line against the wells it climbs out of and falls into.
			ship->GravityLossKmS = GravityLoss(ship, frame, from, to);
			// A slow boat weighs a transfer orbit against that flip-and-burn and flies whichever costs less.
			if (allowConic && !underWay && UsesConics(ship))
			{
				// Weighing the conic asks for an arrival of its own, which books a ring's holding point afresh:
				// keep the one this flip-and-burn arrives at unless the conic is flown.
				auto ring = ship->Ring;
				auto hold = ship->HoldPoint;
				auto conic = TryConic(ship, frame, from, to, departJd, arrivalOrbit, keepOut);
				if (conic != nullptr) arrival = conic;
				else
				{
					ship->Ring = ring;
					ship->HoldPoint = hold;
				}
			}
		}
		ship->PlannedConicDepartJd = cs::Double::NaN;

		ship->DepartureOrbit = ship->Orbit;
		ship->Orbit = arrival;
		ship->OriginBody = from;
		ship->Phase = ShipPhase::Transit;
		ship->FrameBody = frame;
		ship->PhaseStartJd = departJd;
		ChargePropellant(ship);
	}

	// ---------------------------------------------------------------- propellant

	/// <summary>
	/// Takes the transit just planned out of the tanks: its delta-v (<see cref="Propulsion.TransitDeltaV"/>)
	/// at the ship's present mass, by the rocket equation. If the tanks cannot cover it all, the ship
	/// sets out anyway and its drive cuts out when they run dry (<see cref="Ship.StrandJd"/>): the burn
	/// is constant thrust, so that is the same share of the way through in time as of the delta-v.
	/// </summary>
	void Fleet::ChargePropellant(cs::Ref<Ship> ship)
	{
		ship->StrandJd = cs::Double::NaN;
		ship->LegPropellant = 0;
		if (!Burns(ship)) return;
		double dv = Propulsion::TransitDeltaV(ship);
		if (!(dv > 0)) return;
		double need = Propulsion::PropellantFor(ship, dv);
		if (LeakChance > 0 && !tankerAlongside && leaks.NextDouble() < LeakChance)
		{
			// A holed tank: what is left sees the ship only part of the way (a fifth to four fifths of the leg's delta-v).
			double share = 0.2 + 0.6 * leaks.NextDouble();
			double kept = cs::Math::Min(ship->Propellant(), (ship->MassTonnes() - ship->Propellant()) * (Propulsion::MassRatio(dv * share, ship->ExhaustVelocityKmS()) - 1));
			double vented = ship->Propellant() - kept;
			ship->SetPropellant(kept);
			if (Leaked) Leaked(ship, vented, ship->PhaseStartJd);
		}
		if (need <= ship->Propellant())
		{
			ship->SetPropellant(ship->Propellant() - need);
			ship->LegPropellant = need;
			return;
		}
		double have = ship->Propellant(), mass = ship->MassTonnes();
		double reach = Propulsion::DeltaV(ship->ExhaustVelocityKmS(), mass, mass - have);
		ship->SetPropellant(0);
		ship->LegPropellant = have;
		// A tanker that came out to it stays alongside to the end of the leg, topping it up: it does not run dry twice.
		if (tankerAlongside) return;
		auto conic = ship->Conic;
		if (conic != nullptr)
		{
			// A transfer orbit spends its delta-v in a burn at each end, not evenly along the way: short of the
			// departure burn it dies leaving, else it coasts the whole arc and dies in the capture burn.
			double dep = conic->DepartureBurnKmS, rest = dv - dep;
			if (reach < dep)
				ship->StrandJd = ship->PhaseStartJd + conic->DepartureBurnDays * cs::Math::Max(0.0, reach / cs::Math::Max(dep, 1e-9));
			else
				ship->StrandJd = ship->PhaseEndJd - conic->ArrivalBurnDays * (1 - cs::Math::Max(0.0, cs::Math::Min(1.0, rest > 0 ? (reach - dep) / rest : 1.0)));
			return;
		}
		ship->StrandJd = ship->PhaseStartJd + (ship->PhaseEndJd - ship->PhaseStartJd) * cs::Math::Max(0.0, cs::Math::Min(1.0, dv > 0 ? reach / dv : 1.0));
	}

	/// <summary>Gives back the propellant of the part of a transit that will not now be flown (a change of course).</summary>
	void Fleet::RefundUnflown(cs::Ref<Ship> ship, double jd)
	{
		if (!Burns(ship) || ship->LegPropellant <= 0) return;
		double end = ship->StrandJd < ship->PhaseEndJd ? ship->StrandJd : ship->PhaseEndJd;
		auto conic = ship->Conic;
		if (conic != nullptr && conic->DeltaVKmS() > 0)
		{
			// A transfer orbit burns at each end and coasts between, so what is unflown is the delta-v not yet burned.
			double paid = ConicDeltaVSpent(ship, conic, end);
			if (paid > 0) ship->SetPropellant(ship->Propellant() + ship->LegPropellant * cs::Math::Max(0.0, cs::Math::Min(1.0, (paid - ConicDeltaVSpent(ship, conic, jd)) / paid)));
		}
		else
		{
			double span = end - ship->PhaseStartJd;
			if (span > 0) ship->SetPropellant(ship->Propellant() + ship->LegPropellant * cs::Math::Max(0.0, cs::Math::Min(1.0, (end - jd) / span)));
		}
		ship->LegPropellant = 0;
		ship->StrandJd = cs::Double::NaN;
	}

	/// <summary>Delta-v a ship on <paramref name="conic"/> has burned by <paramref name="jd"/>: the departure burn, the
	/// handoff trims spread over the coast, then the capture burn, km/s.</summary>
	double Fleet::ConicDeltaVSpent(cs::Ref<Ship> ship, cs::Ref<ConicTransfer> conic, double jd)
	{
		double depEnd = ship->PhaseStartJd + conic->DepartureBurnDays, capStart = ship->PhaseEndJd - conic->ArrivalBurnDays;
		return conic->DepartureBurnKmS * Share(jd, ship->PhaseStartJd, depEnd)
			+ conic->CorrectionBurnKmS * Share(jd, depEnd, capStart)
			+ conic->ArrivalBurnKmS * Share(jd, capStart, ship->PhaseEndJd);
	}

	double Fleet::Share(double t, double from, double to)
	{
		return to <= from ? (t >= to ? 1.0 : 0.0) : cs::Math::Max(0.0, cs::Math::Min(1.0, (t - from) / (to - from)));
	}

	/// <summary>
	/// The tanks have run dry part way through a transit: the ship drifts dead where the drive cut out,
	/// fixed in the transit's frame, still bound where it was going, and calls for a tanker
	/// (<see cref="RanDry"/>), which reaches it in a couple of days.
	/// </summary>
	void Fleet::Strand(cs::Ref<Ship> ship, double jd)
	{
		int frame = ship->FrameBody;
		auto here = BaseStateOf(ship, jd).HelioKm;
		ship->StrandJd = cs::Double::NaN;
		ship->LegPropellant = 0;
		ship->Orbit = FixedPoint(frame, here - ephemeris->HelioAt(frame, jd));
		ship->Path = cs::List<DVec3>();
		ship->Conic = nullptr;
		ship->Phase = ShipPhase::Parked;
		ship->StrandedInFlight = true;
		CallTanker(ship, jd);
	}

	/// <summary>Calls a tanker out to a ship that cannot move for want of propellant: it waits where it is until one comes.</summary>
	void Fleet::CallTanker(cs::Ref<Ship> ship, double jd)
	{
		ship->OutOfPropellant = true;
		ship->OutOfPropellantSinceJd = jd;
		ship->PhaseStartJd = jd;
		ship->PhaseEndJd = jd + Propulsion::TankerDays * (1 + rng.NextDouble());
		if (RanDry) RanDry(ship, jd);
	}

	/// <summary>
	/// The tanker is alongside (or a passing pilot answered the call first): it fills the ship's tanks
	/// and the ship carries on to where it was bound.
	/// </summary>
	void Fleet::TankerArrives(cs::Ref<Ship> ship)
	{
		double jd = ship->PhaseEndJd;
		// A ship someone already refuelled (a pilot answering the call) needs no tanker, and pays for none.
		double load = ship->PropellantFraction() >= 0.25 ? 0 : Propulsion::TankerLoad(ship);
		ship->SetPropellant(ship->Propellant() + load);
		ship->OutOfPropellant = false;
		if (TankerArrived) TankerArrived(ship, load, jd);
		Resume(ship, jd);
	}

	/// <summary>Back under way from wherever the ship is: the rest of the leg it was flying (to its next ramp, or its destination).</summary>
	void Fleet::Resume(cs::Ref<Ship> ship, double jd)
	{
		tankerAlongside = true;
		bool inFlight = ship->StrandedInFlight;
		ship->StrandedInFlight = false;
		// try { ... } finally { tankerAlongside = false; }
		struct Finally { bool& flag; ~Finally() { flag = false; } } reset{ tankerAlongside };
		ResumeLeg(ship, jd, inFlight);
	}

	void Fleet::ResumeLeg(cs::Ref<Ship> ship, double jd, bool inFlight)
	{
		if (ship->JourneyIndex < ship->Journey.Count())
		{
			auto hop = ship->Journey[ship->JourneyIndex];
			Burn(ship, jd, hop.From, [this, hop, jd]() { return RampOrbit(hop, hop.From, jd); });
		}
		// Dead in open space short of the world its frame is kept by (a moon's hop in to its planet, or the
		// last burn from a lane's ramp in to the world itself), it still has the way there to fly.
		else if (ship->FinalBody >= 0 && (ship->FinalBody != ship->Orbit->Body || inFlight))
		{
			int to = ship->FinalBody;
			if (!inFlight)
			{
				// Filled in port, it sets out as from any port: by the lanes (or a transfer orbit) its trip was costed for.
				ship->DestinationBody = to;
				ship->PhaseEndJd = jd;
				Depart(ship);
			}
			// Dry on the way out to a jump gate: on to the gate, not into an orbit round the star.
			else if (ship->GateKm.has_value() && to == catalog->Root()->Index)
			{
				DVec3 gate = *ship->GateKm;
				Burn(ship, jd, to, [to, gate]() { return FixedPoint(to, gate); });
			}
			else if (ship->Wander) Burn(ship, jd, to, [this, to, ship]() { return WanderOrbit(to, ship); });
			else Burn(ship, jd, to, [this, ship, to]() { return ArrivalFor(ship, to); });
		}
		else
		{
			ship->DestinationBody = ship->Orbit->Body;
			ship->Journey = cs::List<LaneHop>();
			ScheduleNext(ship, jd);
		}
	}

	/// <summary>Whether a ship flying on propellant lacks what its drive needs to get from <paramref name="from"/> to <paramref name="to"/>.</summary>
	bool Fleet::ShortOfPropellant(cs::Ref<Ship> ship, int from, int to, double jd)
	{
		return Burns(ship) && Propulsion::PropellantFor(ship, EstimateTrip(ship, from, to, jd).DeltaVKmS) > ship->Propellant();
	}

	/// <summary>
	/// How wide a transit flown in a frame gives its centre: <see cref="SunKeepOutKm"/> for a star
	/// (scaled for its luminosity, so Proxima's close planets stay reachable), a couple of radii
	/// for a planet, nothing for an empty barycentre (its stars are steered round instead).
	/// </summary>
	double Fleet::FrameKeepOutKm(int frame)
	{
		auto f = (*catalog)[frame];
		if (f->IsStar()) return SunKeepOutKm * cs::Math::Sqrt(cs::Math::Max(f->LuminositySolar, 1e-6));
		if (f->Kind == BodyKind::Barycentre) return 0;
		return f->RadiusKm * PlanetKeepOutRadii;
	}

	/// <summary>
	/// Plans the transit onto <paramref name="ship"/>. Returns false if the path would pass inside
	/// the keep-out around the frame's centre, or through either end's body or the planets they orbit.
	/// </summary>
	bool Fleet::Plan(cs::Ref<Ship> ship, int frame, int from, int to, double departJd, DVec3 start, DVec3 startVelocity,
		cs::Ref<ParkingOrbit> arrival, double keepOut, bool matchVelocity)
	{
		double accel = ship->AccelG() * Astro::GravityKmPerDay2;
		auto v0 = matchVelocity ? startVelocity : DVec3::Zero;
		auto v1 = DVec3::Zero;
		cs::List<DVec3> path; // C#: DVec3[] path = null

		// For a flight time T, the path to where the target will be then, and how much longer
		// (positive) or shorter the drive needs to fly it than T.
		auto Shortfall = [&](double flightDays) -> double
		{
			double arriveJd = departJd + flightDays;
			auto target = RelativeTo(frame, to, arriveJd);
			// A fixed point keeps its own epoch: re-dating it would turn a point that rotates with
			// its body (a docking ring) to the wrong place.
			if (!arrival->Fixed)
			{
				arrival->EpochJd = arriveJd;
				arrival->PhaseAtEpoch = arrival->PhaseFacing((start - target).Normalized());
			}
			v1 = matchVelocity ? VelocityRelativeTo(frame, to, arriveJd) + arrival->VelocityAt(arriveJd) : DVec3::Zero;
			// The drift covers (v0 + v1)·T / 2 of the way; the flip-and-burn covers the rest.
			auto shift = (v0 + v1) * (flightDays * 0.5);
			auto end = target + arrival->OffsetAt(arriveJd) - shift;
			path = BuildPath(start, end, keepOut, Avoidances(frame, from, to, departJd, arriveJd, shift));
			return 2 * cs::Math::Sqrt(PolylineLength(path) / accel) - flightDays;
		};

		// Intercept: bracket the flight time, then bisect. The drive can always outrun the
		// target eventually, so the shortfall turns negative for a long enough flight.
		double lo = 1e-4;
		double hi = cs::Math::Max(2 * cs::Math::Sqrt((RelativeTo(frame, to, departJd) - start).Length() / accel), 2 * lo);
		for (int i = 0; i < 60 && Shortfall(hi) > 0; i++) hi *= 2;
		for (int i = 0; i < 60 && hi - lo > 1e-9 * hi; i++)
		{
			double mid = 0.5 * (lo + hi);
			if (Shortfall(mid) > 0) lo = mid;
			else hi = mid;
		}
		double flight = hi;
		Shortfall(flight);

		ship->DepartureVelocity = v0;
		ship->ArrivalVelocity = v1;
		ship->Path = path;
		ship->PathCumulative = Cumulative(path);
		ship->PathLength = ship->PathCumulative[path.Count() - 1];
		// The path was built for exactly this arrival time; trim the drive (by a hair, once
		// converged) so the brachistochrone lands on it then.
		ship->AccelKmPerDay2 = 4 * ship->PathLength / (flight * flight);
		ship->PhaseStartJd = departJd;
		ship->PhaseEndJd = departJd + flight;

		return Clear(ship, frame, from, to, keepOut);
	}

	/// <summary>Samples a planned transit against the frame's keep-out and the bodies near its ends.</summary>
	bool Fleet::Clear(cs::Ref<Ship> ship, int frame, int from, int to, double keepOut)
	{
		auto obstacles = cs::List<int>();
		for (int end : { from, to })
			for (int b = end; b >= 0 && b != frame; b = (*catalog)[b]->ParentIndex)
				if (!obstacles.Contains(b)) obstacles.Add(b);
		// Every other world orbiting the frame is something to miss as well.
		for (int other : catalog->ChildrenOf(frame))
			if (!obstacles.Contains(other) && (*catalog)[other]->IsVisible() && !(*catalog)[other]->IsStar()) obstacles.Add(other);

		// Bodies sit at the ends, where the ship is slow but a planet is passed in minutes: sample
		// the first and last few percent densely, and test each segment, not just its ends. At coarse
		// detail a third as many samples: the segments between them are still tested whole.
		int samples = coarse ? 128 : 384;
		double total = ship->PhaseEndJd - ship->PhaseStartJd;
		auto previous = cs::NewArray<DVec3>(obstacles.Count());
		for (int k = 0; k <= samples; k++)
		{
			double u = static_cast<double>(k) / samples;
			double tau = total * (u < 0.25 ? 0.12 * u / 0.25 * (u / 0.25) : u > 0.75 ? 1 - 0.12 * ((1 - u) / 0.25) * ((1 - u) / 0.25) : 0.12 + (u - 0.25) / 0.5 * 0.76);
			auto offset = TransitOffsetAt(ship, tau);
			if (k > 0 && k < samples && offset.Length() < keepOut * 0.8) return false;
			double jd = ship->PhaseStartJd + tau;
			auto helio = ephemeris->HelioAt(frame, jd) + offset;
			for (int i = 0; i < obstacles.Count(); i++)
			{
				int b = obstacles[i];
				auto rel = helio - ephemeris->HelioAt(b, jd);
				double limit = (*catalog)[b]->RadiusKm * 1.5;
				if (k > 0 && k < samples && rel.Length() < limit) return false;
				if (k > 0 && SegmentDistance(previous[i], rel) < limit) return false;
				previous[i] = rel;
			}
		}
		return true;
	}

	/// <summary>Closest approach to the origin of the segment from <paramref name="a"/> to <paramref name="b"/>.</summary>
	double Fleet::SegmentDistance(DVec3 a, DVec3 b)
	{
		auto d = b - a;
		double len2 = d.LengthSquared();
		double t = len2 > 0 ? cs::Math::Max(0.0, cs::Math::Min(1.0, -DVec3::Dot(a, d) / len2)) : 0;
		return (a + d * t).Length();
	}

	void Fleet::Arrive(cs::Ref<Ship> ship, bool landed)
	{
		double arriveJd = ship->PhaseEndJd;
		ship->Phase = landed ? ShipPhase::Landed : ShipPhase::Parked;
		ship->Wander = false;
		ship->Homing = false;
		ship->TripsCompleted++;
		ship->Path = cs::List<DVec3>();
		ship->Conic = nullptr;
		ship->Journey = cs::List<LaneHop>();
		ship->JourneyIndex = 0;
		ship->PhaseStartJd = arriveJd;
		if (Arrived) Arrived(ship, arriveJd);
		if (ship->Destroyed || (IsHeld != nullptr && IsHeld(ship))) return;
		ScheduleNext(ship, arriveJd);
	}

	/// <summary>
	/// Holds a hull fixed at <paramref name="offsetKm"/> in <paramref name="body"/>'s frame, nose on
	/// <paramref name="forward"/>, as of <paramref name="jd"/>: where a pilot let go of it, the raiders cast it
	/// off, or its captors took it. Whatever trip it had planned before is forgotten -- its departure is due
	/// now and it is bound nowhere but here -- so the next <see cref="ScheduleNext"/> plans afresh from this
	/// spot (for its new owner, if it changed hands) rather than the hull sitting frozen until the old plan's
	/// departure and then setting off on it (U.SPC-T178).
	/// </summary>
	void Fleet::PinAt(cs::Ref<Ship> ship, int body, DVec3 offsetKm, DVec3 forward, double jd)
	{
		auto v = forward.LengthSquared() > 1e-12 ? forward.Normalized() : DVec3::UnitX;
		auto u = DVec3::Cross(cs::Math::Abs(v.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX, v).Normalized();
		auto orbit = cs::New<ParkingOrbit>();
		orbit->Body = body;
		orbit->Center = offsetKm;
		orbit->RadiusKm = 0;
		orbit->U = u;
		orbit->V = v;
		orbit->EpochJd = jd;
		orbit->PhaseAtEpoch = 0;
		orbit->RateRadPerDay = 0;
		orbit->Fixed = true;
		ship->Orbit = orbit;
		ship->Phase = ShipPhase::Parked;
		ship->Path = cs::List<DVec3>();
		ship->Journey = cs::List<LaneHop>();
		ship->JourneyIndex = 0;
		ship->Ring = nullptr;
		ship->HoldPoint = nullptr;
		ship->LaneHoldSide = DVec3::Zero;
		ship->Wander = false;
		ship->DestinationBody = body;
		// Bound only where it is now: a tanker that later fills it does not send it on the old trip.
		ship->FinalBody = body;
		ship->StrandedInFlight = false;
		ship->PhaseStartJd = ship->PhaseEndJd = jd;
	}

	/// <summary>Picks the next destination and the departure time: dwell, then wait for the parking orbit to face the target.</summary>
	void Fleet::ScheduleNext(cs::Ref<Ship> ship, double nowJd)
	{
		// Mid-descent or mid-climb the schedule is fixed; a queued ship (after a fight, say) just
		// takes its turn again.
		if (ship->Phase == ShipPhase::Descent || ship->Phase == ShipPhase::Ascent) return;
		if (ship->OutOfPropellant)
		{
			// Dead in space (back from a fight, or let go by its pilot): it waits on for its tanker, unless
			// somebody has filled its tanks meanwhile.
			if (ship->PropellantFraction() < 0.25 || ship->Phase != ShipPhase::Parked)
			{
				ship->PhaseEndJd = cs::Math::Max(ship->PhaseEndJd, nowJd + 0.05);
				return;
			}
			// Filled: it carries on to where it was bound, from where it lay (not from the frame's centre,
			// which is all its fixed point's body says of where it is).
			ship->OutOfPropellant = false;
			Resume(ship, nowJd);
			return;
		}
		if (ship->Phase == ShipPhase::Queued && ship->Ring == nullptr)
		{
			// Lost track of its ring (it changed hands mid-queue): hold where it is, as in orbit.
			ship->Orbit = ship->HoldPoint != nullptr ? ship->HoldPoint : ship->Orbit;
			ship->Phase = ShipPhase::Parked;
		}
		if (ship->Phase == ShipPhase::Queued)
		{
			double entry = ship->PhaseEndJd;
			if (entry < nowJd + MinQueueDays)
			{
				// Its turn went by while it was fighting: back to its spot off the top of the line,
				// for the next free turn (it is drawn in the battle until then, so nothing jumps).
				entry = cs::Math::Max(nowJd + MinQueueDays, ship->Ring->NextEntryJd);
				ship->Ring->NextEntryJd = entry + ship->Ring->EntryIntervalDays;
				ship->HoldPoint = ship->Ring->HoldPoint(ship);
			}
			else ship->HoldPoint = FixedHold(ship, nowJd);
			ship->Orbit = ship->HoldPoint;
			ship->PhaseStartJd = nowJd;
			ship->PhaseEndJd = entry;
			return;
		}
		if (ship->GateKm.has_value())
		{
			// Bound for a gate (see InterstellarTraffic): no markets or orders to consult, just go.
			ship->DestinationBody = catalog->Root()->Index;
			ship->Wander = false;
			// A ship already under way only retargets: moving its phase end would cut the transit short and
			// teleport it (U.SPC-T570). It heads for the gate when this leg ends.
			if (ship->Phase != ShipPhase::Transit && ship->Phase != ShipPhase::Lane)
				ship->PhaseEndJd = nowJd + 0.05 + rng.NextDouble() * 0.15;
			return;
		}
		int here = ship->Orbit->Body;
		ship->WaitingForPropellant = false;
		int chosen = ChooseNext != nullptr ? ChooseNext(ship, here, nowJd) : -1;
		double dwell = Dwell != nullptr ? Dwell(ship, DwellDays(ship)) : DwellDays(ship);
		// A point with no size (a binary's barycentre) has no space round it to wander out into.
		// Nor a star: three to five of its radii out is deep inside the keep-out every trajectory steers clear of.
		// Someone's own little craft (U.SPC-T375) goes out sightseeing the same way.
		if ((ship->Role == ShipRole::Free || (ship->Role == ShipRole::Commuter && LocalTraffic::IsPersonal(ship->Civil))) && chosen < 0 && rng.NextDouble() < 0.55 && (*catalog)[here]->RadiusKm > 0 && !(*catalog)[here]->UsesStellarMapping())
		{
			// A free ship's whim: out to somewhere new nearby rather than to another world.
			ship->Wander = true;
			ship->DestinationBody = here;
			ship->Cargo = cs::String(); // C#: null
			ship->PhaseEndJd = nowJd + dwell;
			return;
		}
		if (chosen == here)
		{
			ship->DestinationBody = here;
			ship->PhaseEndJd = nowJd + cs::Math::Max(dwell, 0.05);
			return;
		}
		// A system with nowhere to go (only stars round a barycentre): hold here rather than pick from no
		// ports. A patrol goes by its route, not ports.
		bool nowhere = ship->Role != ShipRole::Patrol && ship->Ports.Count() == 0; // C#: Ports == null || Ports.Length == 0
		int next = chosen >= 0 ? chosen : nowhere ? here : ChooseDestination(ship, here, nowJd);
		if (next == here)
		{
			ship->DestinationBody = here;
			ship->PhaseEndJd = nowJd + cs::Math::Max(dwell, 0.05);
			return;
		}
		// The fleet's own pick (a free ship's whim, a trader with no work) it cannot reach on what is in its
		// tanks: a few more picks, for somewhere it can.
		for (int k = 0; k < 6 && chosen < 0 && !nowhere && next != here && ShortOfPropellant(ship, here, next, nowJd); k++)
			next = ChooseDestination(ship, here, nowJd);
		if (next == here)
		{
			ship->DestinationBody = here;
			ship->PhaseEndJd = nowJd + cs::Math::Max(dwell, 0.05);
			return;
		}
		if (ShortOfPropellant(ship, here, next, nowJd) && ForceTopUp != nullptr && CanRefuelAt != nullptr && CanRefuelAt(ship, here))
			ForceTopUp(ship, here, nowJd);
		if (ShortOfPropellant(ship, here, next, nowJd))
		{
			// Not enough in the tanks for the trip. In port, it waits for propellant to be had; anywhere
			// else, with next to nothing left, it calls a tanker; with some, it sets out and gets as far as it can.
			if (CanRefuelAt != nullptr && CanRefuelAt(ship, here))
			{
				ship->DestinationBody = here;
				ship->WaitingForPropellant = true;
				ship->PhaseEndJd = nowJd + cs::Math::Max(dwell, 0.5);
				return;
			}
			if (ship->PropellantFraction() < 0.01 && ship->Phase == ShipPhase::Parked)
			{
				ship->DestinationBody = next;
				ship->FinalBody = next;
				ship->Journey = cs::List<LaneHop>();
				CallTanker(ship, nowJd);
				return;
			}
		}
		ship->DestinationBody = next;
		if (ChooseNext == nullptr) ship->Cargo = CargoFor(ship, here);

		double windowJd = nowJd + dwell;
		ship->PlannedConicDepartJd = cs::Double::NaN;
		ship->PlannedDirect = false;
		// A slow boat waits in port for its launch window when a transfer orbit then beats flying now.
		if (UsesConics(ship))
		{
			double windowDepartJd = 0;
			if (PlanWindow(ship, here, next, windowJd, windowDepartJd))
			{
				ship->PhaseEndJd = windowDepartJd;
				return;
			}
			// No transfer orbit beats flying now: remember that, so the departure does not weigh it again.
			ship->PlannedConicDepartJd = windowJd;
			ship->PlannedConicDays = -1;
		}
		DVec3 firstLeg = FirstLegTarget(here, next, windowJd);
		auto toTarget = (firstLeg - ephemeris->HelioAt(here, windowJd)).Normalized();
		double want = ship->Orbit->PhaseFacing(toTarget);
		double have = ship->Orbit->PhaseAt(windowJd);
		double delta = std::fmod(want - have, Astro::TwoPi); // C# double % (truncated remainder)
		if (delta < 0) delta += Astro::TwoPi;
		// Around a station (no real gravity) or on a fixed point, the "orbit" barely turns: just go.
		double wait = ship->Orbit->RateRadPerDay > 0 && !ship->Orbit->Fixed ? delta / ship->Orbit->RateRadPerDay : 0;
		ship->PhaseEndJd = windowJd + (wait > MaxFacingWaitDays ? 0 : wait);
	}

	/// <summary>A queued ship's current spot, frozen into a new holding point to ease in from.</summary>
	cs::Ref<ParkingOrbit> Fleet::FixedHold(cs::Ref<Ship> ship, double jd)
	{
		auto at = RingState(ship, jd).HelioKm - ephemeris->Helio(ship->Ring->Body);
		auto hold = ship->Ring->Point(at.Length());
		// Keep it on the ship's own parallel and meridian, not the ring's.
		auto north = BodyOrientation::MapNorth((*catalog)[ship->Ring->Body]);
		double lat = cs::Math::Asin(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(at.Normalized(), north))));
		double phase = cs::Math::Atan2(DVec3::Dot(at, hold->V), DVec3::Dot(at, hold->U));
		hold->Center = north * (at.Length() * cs::Math::Sin(lat));
		hold->RadiusKm = at.Length() * cs::Math::Cos(lat);
		hold->EpochJd = jd;
		hold->PhaseAtEpoch = phase;
		return hold;
	}

	int Fleet::ChooseDestination(cs::Ref<Ship> ship, int here, double jd)
	{
		switch (ship->Role)
		{
			case ShipRole::Patrol:
				ship->RouteIndex = (ship->RouteIndex + 1) % ship->Route.Count();
				if (ship->Route[ship->RouteIndex] == here)
					ship->RouteIndex = (ship->RouteIndex + 1) % ship->Route.Count();
				return ship->Route[ship->RouteIndex];

			case ShipRole::Miner:
			// An ore tug with no belt works to send it (U.SPC-T498) shuttles the same way: out from its field and home.
			case ShipRole::Tug:
			// So does a works' freighter with no works to load it (U.SPC-T523): out to its depot and home.
			case ShipRole::Hauler:
				if (here != ship->Ports[0]) return ship->Ports[0];
				return ship->Ports.Count() > 1 ? ship->Ports[1 + rng.Next(ship->Ports.Count() - 1)] : here;

			case ShipRole::Free:
				return Whim(ship, here, jd);

			case ShipRole::Commuter:
			{
				// A bus keeps to its line, passing over a stop it should not call at (U.SPC-T375); a taxi or
				// someone's own craft goes where it fancies about its neighbourhood, as a free ship does.
				if (ship->Route.Count() == 0) return Whim(ship, here, jd); // C#: Route == null || Route.Length == 0
				for (int k = 0; k < ship->Route.Count(); k++)
				{
					ship->RouteIndex = (ship->RouteIndex + 1) % ship->Route.Count();
					int stop = ship->Route[ship->RouteIndex];
					if (stop != here && (Shuns == nullptr || !Shuns(ship, stop))) return stop;
				}
				return here;
			}

			default:
			{
				// Prefer nearby ports: weight 1 / (0.3 AU + distance).
				auto herePos = ephemeris->HelioAt(here, jd);
				double total = 0;
				auto weights = cs::NewArray<double>(ship->Ports.Count());
				for (int i = 0; i < ship->Ports.Count(); i++)
				{
					if (ship->Ports[i] == here) continue;
					double au = (ephemeris->HelioAt(ship->Ports[i], jd) - herePos).Length() / Astro::AuKm;
					weights[i] = 1.0 / (0.3 + au);
					total += weights[i];
				}
				double pick = rng.NextDouble() * total;
				for (int i = 0; i < weights.Count(); i++)
				{
					pick -= weights[i];
					if (weights[i] > 0 && pick <= 0) return ship->Ports[i];
				}
				return ship->Ports[0] == here ? ship->Ports[cs::Math::Min(1, ship->Ports.Count() - 1)] : ship->Ports[0];
			}
		}
	}

	/// <summary>Somewhere a free ship (or a taxi, or someone's own craft) fancies, mostly close: weight
	/// 1 / (0.05 AU + distance)², so neighbouring moons and planets come up far more often than the far side of the
	/// system. Never a place it shuns; with nowhere else, it stays put.</summary>
	int Fleet::Whim(cs::Ref<Ship> ship, int here, double jd)
	{
		auto herePos = ephemeris->HelioAt(here, jd);
		double total = 0;
		auto weights = cs::NewArray<double>(ship->Ports.Count());
		for (int i = 0; i < ship->Ports.Count(); i++)
		{
			if (ship->Ports[i] == here || (Shuns != nullptr && Shuns(ship, ship->Ports[i]))) continue;
			double au = (ephemeris->HelioAt(ship->Ports[i], jd) - herePos).Length() / Astro::AuKm;
			weights[i] = 1.0 / ((0.05 + au) * (0.05 + au));
			total += weights[i];
		}
		double pick = rng.NextDouble() * total;
		for (int i = 0; i < weights.Count(); i++)
		{
			pick -= weights[i];
			if (weights[i] > 0 && pick <= 0) return ship->Ports[i];
		}
		// Nowhere it would go but places it shuns: it stays put.
		if (!(total > 0)) return here;
		// Array.FindLastIndex(weights, x => x > 0)
		int last = -1;
		for (int i = weights.Count() - 1; i >= 0; i--)
			if (weights[i] > 0) { last = i; break; }
		return ship->Ports[last];
	}

	double Fleet::DwellDays(cs::Ref<Ship> ship)
	{
		switch (ship->Role)
		{
			// A bus keeps a timetable of a few hours; a taxi waits for a fare; a private flight lingers longer.
			case ShipRole::Commuter:
				return ship->Civil == CivilCraft::Bus ? 0.05 + rng.NextDouble() * 0.1
					: ship->Civil == CivilCraft::Taxi ? 0.05 + rng.NextDouble() * 0.3
					: 0.2 + rng.NextDouble() * 1.0;
			case ShipRole::Patrol: return 1.0 + rng.NextDouble() * 3.0;
			case ShipRole::Courier: return 0.1 + rng.NextDouble() * 0.4;
			case ShipRole::Free: return 0.1 + rng.NextDouble() * 0.6;
			case ShipRole::Miner: return 1.0 + rng.NextDouble() * 2.0;
			// An ore tug with a load latched on sets straight out; running empty it lies at the field a while.
			case ShipRole::Tug: return ship->Tow != nullptr ? 0.05 + rng.NextDouble() * 0.1 : 0.5 + rng.NextDouble() * 1.0;
			// A works' freighter with a convoy's load aboard sails at once; empty it lies at its refinery a while.
			case ShipRole::Hauler: return ship->HasCargo ? 0.05 + rng.NextDouble() * 0.1 : 0.5 + rng.NextDouble() * 1.0;
			default: return 0.5 + rng.NextDouble() * 1.5;
		}
	}

	cs::Ref<ParkingOrbit> Fleet::NewParkingOrbit(int body, cs::Ref<Ship> ship)
	{
		auto b = (*catalog)[body];
		// Military ships hold high orbits; civilian traffic sits lower and more varied.
		double radii = ship->Spec->IsMilitary ? 4.0 + rng.NextDouble() * 3.0 : 2.5 + rng.NextDouble() * 4.5;
		// Round a tiny moon or a station, a few radii is a crowded few tens of km: spread out to room for a fleet.
		double r = cs::Math::Max(b->RadiusKm * radii, b->RadiusKm + 60 + rng.NextDouble() * 140);

		// C# evaluates the constructor's arguments left to right; C++ does not promise an order, so sequence the dice.
		double jx = Jitter(0.12);
		double jy = Jitter(0.12);
		double jz = Jitter(0.12);
		auto normal = (b->SpinAxis() + DVec3(jx, jy, jz)).Normalized();
		auto reference = cs::Math::Abs(normal.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX;
		auto u = DVec3::Cross(reference, normal).Normalized();
		auto v = DVec3::Cross(normal, u);
		auto orbit = cs::New<ParkingOrbit>();
		orbit->Body = body;
		orbit->RadiusKm = r;
		orbit->U = u;
		orbit->V = v;
		orbit->RateRadPerDay = Kepler::CircularRate(b->Gm, r);
		return orbit;
	}

	double Fleet::Jitter(double amount) { return (rng.NextDouble() * 2 - 1) * amount; }

	/// <summary>
	/// The frame a transit is flown in: the lowest body that both ends orbit within (or are), so
	/// Earth for Earth to the Moon, Jupiter for Io to Europa, the Sun between planetary systems.
	/// </summary>
	int Fleet::CommonFrame(int a, int b)
	{
		for (int x = a; x >= 0; x = (*catalog)[x]->ParentIndex)
			for (int y = b; y >= 0; y = (*catalog)[y]->ParentIndex)
				if (x == y) return x;
		return 0;
	}

	DVec3 Fleet::RelativeTo(int frame, int body, double jd)
	{
		if (frame == body) return DVec3::Zero;
		// C# evaluates the left operand first; sequence the cached lookups the same way.
		DVec3 b = HelioAt(body, jd);
		DVec3 f = HelioAt(frame, jd);
		return b - f;
	}

	// Planning asks where the same bodies are at the same instants over and over (each Lambert try,
	// each intercept bisection): remember the last few thousand answers.
	// (helioCache is declared in Fleet.h.)

	DVec3 Fleet::HelioAt(int body, double jd)
	{
		DVec3 p;
		if (helioCache.TryGetValue(std::make_pair(body, jd), p)) return p;
		if (helioCache.Count() > 8192) helioCache.Clear();
		p = ephemeris->HelioAt(body, jd);
		helioCache[std::make_pair(body, jd)] = p;
		return p;
	}

	/// <summary>Velocity of <paramref name="body"/> relative to <paramref name="frame"/>, km/day (central difference).</summary>
	DVec3 Fleet::VelocityRelativeTo(int frame, int body, double jd)
	{
		const double h = 1e-3;
		DVec3 ahead = RelativeTo(frame, body, jd + h);
		DVec3 behind = RelativeTo(frame, body, jd - h);
		return (ahead - behind) / (2 * h);
	}

	/// <summary>
	/// The planets that the ends' moons orbit, as spheres to steer around, in path coordinates
	/// (the arrival side is shifted like the path's end, since drift is added on top of the path).
	/// </summary>
	cs::List<std::pair<DVec3, double>> Fleet::Avoidances(int frame, int from, int to, double departJd, double arriveJd, DVec3 shift)
	{
		auto list = cs::List<std::pair<DVec3, double>>();
		// Other worlds orbiting the frame (Io when flying Ganymede to Europa): give them a berth too.
		double mid = 0.5 * (departJd + arriveJd);
		for (int other : catalog->ChildrenOf(frame))
		{
			if (OnChain(other, from) || OnChain(other, to) || !(*catalog)[other]->IsVisible() || (*catalog)[other]->IsStar()) continue;
			DVec3 center = RelativeTo(frame, other, mid) - shift * 0.5;
			list.Add(std::make_pair(center, (*catalog)[other]->RadiusKm * 3));
		}
		if ((*catalog)[frame]->Kind == BodyKind::Barycentre)
			for (int star : catalog->ChildrenOf(frame))
				if ((*catalog)[star]->IsStar())
				{
					DVec3 center = RelativeTo(frame, star, 0.5 * (departJd + arriveJd));
					list.Add(std::make_pair(center, FrameKeepOutKm(star)));
				}
		for (int b = (*catalog)[from]->ParentIndex, moon = from; b > 0 && b != frame; moon = b, b = (*catalog)[b]->ParentIndex)
		{
			DVec3 center = RelativeTo(frame, b, departJd);
			double radius = Berth(b, moon, departJd);
			list.Add(std::make_pair(center, radius));
		}
		for (int b = (*catalog)[to]->ParentIndex, moon = to; b > 0 && b != frame; moon = b, b = (*catalog)[b]->ParentIndex)
		{
			DVec3 center = RelativeTo(frame, b, arriveJd) - shift;
			double radius = Berth(b, moon, arriveJd);
			list.Add(std::make_pair(center, radius));
		}
		return list;
	}

	/// <summary>True if <paramref name="body"/> is <paramref name="end"/> or one of the bodies it orbits.</summary>
	bool Fleet::OnChain(int body, int end)
	{
		for (int b = end; b >= 0; b = (*catalog)[b]->ParentIndex)
			if (b == body) return true;
		return false;
	}

	/// <summary>
	/// How wide a ship leaving or joining a moon gives its planet: a good part of the moon system,
	/// so it never skims the world, but inside the moon's own orbit so the moon stays reachable.
	/// </summary>
	double Fleet::Berth(int planet, int moon, double jd)
	{
		auto p = (*catalog)[planet];
		double wide = cs::Math::Max(p->RadiusKm * 2.5, p->MoonReachKm * 0.3);
		DVec3 moonAt = ephemeris->HelioAt(moon, jd);
		DVec3 planetAt = ephemeris->HelioAt(planet, jd);
		double moonDistance = (moonAt - planetAt).Length();
		return cs::Math::Max(p->RadiusKm * 2.5, cs::Math::Min(wide, moonDistance * 0.75));
	}

	/// <summary>
	/// Straight line, or a dog-leg around the frame's centre when the line would pass within
	/// <paramref name="keepOutKm"/>, then detours around any of <paramref name="avoid"/> a leg runs through.
	/// </summary>
	cs::List<DVec3> Fleet::BuildPath(DVec3 start, DVec3 end, double keepOutKm, cs::List<std::pair<DVec3, double>> avoid)
	{
		auto points = cs::List<DVec3>{ start, end };
		// Swing round the frame's centre: keep adding waypoints out past the keep-out until no leg
		// cuts inside it (one is not always enough when the ends are nearly opposite each other).
		for (int pass = 0; pass < 8; pass++)
		{
			bool inserted = false;
			for (int i = 0; i + 1 < points.Count() && !inserted; i++)
			{
				auto a = points[i];
				auto d = points[i + 1] - a;
				double t = d.LengthSquared() > 0 ? cs::Math::Max(0.0, cs::Math::Min(1.0, -DVec3::Dot(a, d) / d.LengthSquared())) : 0;
				auto closest = a + d * t;
				if (closest.Length() >= keepOutKm * 0.999 || t <= 0 || t >= 1) continue;
				auto away = closest.LengthSquared() > 1 ? closest.Normalized() : DVec3::Cross(d, DVec3::UnitZ).Normalized();
				points.Insert(i + 1, away * (keepOutKm * 1.4));
				inserted = true;
			}
			if (!inserted) break;
		}

		// (C#'s `avoid != null`: a null list is the empty list here, which the loop skips.)
		for (const auto& [center, radius] : avoid)
		{
			for (int i = 0; i + 1 < points.Count(); i++)
			{
				auto a = points[i] - center;
				auto seg = points[i + 1] - points[i];
				double len2 = seg.LengthSquared();
				double u = len2 > 0 ? -DVec3::Dot(a, seg) / len2 : 0;
				if (u <= 0 || u >= 1) continue;
				auto near = a + seg * u;
				if (near.Length() >= radius) continue;
				auto out1 = near.LengthSquared() > 1 ? near.Normalized() : DVec3::Cross(seg, DVec3::UnitZ).Normalized();
				points.Insert(i + 1, center + out1 * (radius * 1.4));
				break;
			}
		}
		return points.ToArray();
	}

	double Fleet::PolylineLength(cs::List<DVec3> p)
	{
		double len = 0;
		for (int i = 1; i < p.Count(); i++) len += (p[i] - p[i - 1]).Length();
		return len;
	}

	cs::List<double> Fleet::Cumulative(cs::List<DVec3> p)
	{
		auto c = cs::NewArray<double>(p.Count());
		for (int i = 1; i < p.Count(); i++) c[i] = c[i - 1] + (p[i] - p[i - 1]).Length();
		return c;
	}

	cs::String Fleet::CargoFor(cs::Ref<Ship> ship, int origin)
	{
		if (ship->Spec->IsMilitary) return cs::String(); // C#: null
		if (ship->Role == ShipRole::Courier) return "Priority mail and data cores";
		if (ship->Role == ShipRole::Miner)
			return origin == ship->Ports[0] ? (rng.Next(2) == 0 ? "Nickel-iron ore" : "Platinum-group metals") : "Empty, outbound to the belt";
		if (ship->Role == ShipRole::Tug)
			return ship->Tow != nullptr ? cs::Cat("Pushing ", ship->Tow->Designation, ", ", cs::Fmt(ship->Tow->Tonnes, "N0"), " t") : origin == ship->Ports[0] ? "Claws open, out for a load" : "Running empty, back to the field";

		const cs::String& name = (*catalog)[origin]->Name;
		if (name == "Earth") return Pick(cs::List<cs::String>{ "Machinery", "Medical supplies", "Electronics" });
		if (name == "Moon") return Pick(cs::List<cs::String>{ "Helium-3", "Regolith glass" });
		if (name == "Mars") return Pick(cs::List<cs::String>{ "Fabricated alloys", "Hydroponic food" });
		if (name == "Ceres") return Pick(cs::List<cs::String>{ "Water ice", "Nickel-iron ore" });
		if (name == "Europa") return "Water";
		if (name == "Ganymede") return Pick(cs::List<cs::String>{ "Habitat modules", "Reactor fuel" });
		if (name == "Callisto") return "Refined metals";
		if (name == "Titan") return Pick(cs::List<cs::String>{ "Hydrocarbons", "Nitrogen" });
		if (name == "Enceladus") return "Volatiles";
		if (name == "Titania") return "Water ice";
		if (name == "Triton") return "Nitrogen ice";
		return "General freight";
	}

	cs::String Fleet::Pick(cs::List<cs::String> options) { return options[rng.Next(options.Count())]; }

	// ---------------- Fleet.cs lines 1989-2962 (US-T8 part 3)

	// ---------------------------------------------------------------- setup

	cs::Ref<Faction> Fleet::AddFaction(cs::String name, cs::String prefix, Rgb color)
	{
		auto f = cs::New<Faction>(name, prefix, color);
		factions.Add(f);
		return f;
	}

	/// <summary>Gives a faction that already exists (an interstellar polity, see <see cref="Polities"/>) a presence in this system.</summary>
	cs::Ref<Faction> Fleet::AddFaction(cs::Ref<Faction> f)
	{
		if (!factions.Contains(f)) factions.Add(f);
		return f;
	}

	cs::Ref<Ship> Fleet::AddShip(cs::String name, ShipClass c, cs::Ref<Faction> faction, ShipRole role)
	{
		auto ship = cs::New<Ship>();
		ship->Id = ships.Count();
		ship->Name = static_cast<int>(faction->Prefix.size()) > 0 ? cs::Cat(faction->Prefix, " ", name) : name;
		ship->Spec = ShipClassSpec::Of(c);
		ship->Faction = faction;
		ship->Role = role;
		ship->Combat = cs::New<ShipCombat>(CombatSpec::RandomOf(c, rng), c);
		ships.Add(ship);
		return ship;
	}

	/// <summary>
	/// Adds a ship built elsewhere than <see cref="AddShip"/> straight into this fleet's list: a jump gate's
	/// arrival or a cruise dropping out (see <see cref="GateTransit"/>, <see cref="LetGo"/>) -- the same hull
	/// that left another fleet's list, not a copy (U.SPC-T453), since every other ship's id there is its fixed
	/// index into that list (ships are never removed, only marked <see cref="Ship.Destroyed"/> in place) --
	/// shuffling an existing one out from the middle would desync every id after it.
	/// </summary>
	void Fleet::Receive(cs::Ref<Ship> ship)
	{
		ship->Id = ships.Count();
		ships.Add(ship);
	}

	/// <summary>
	/// Whether <paramref name="ship"/> itself still sits in this very fleet's list at its own id (not a gone
	/// marker that has taken its slot, see <see cref="LetGo"/>): a tracker outside the fleet (an outlaw's
	/// captain, a sensor book) that holds a ship reference across a long game should check this before reading
	/// anything this fleet's own catalogue and ephemeris give it a position with -- once it is carried on
	/// elsewhere (U.SPC-T453), that position belongs to a different system's fleet, never this one's.
	/// </summary>
	bool Fleet::IsPresent(cs::Ref<Ship> ship)
	{
		return ship != nullptr && ship->Id >= 0 && ship->Id < ships.Count() && ships[ship->Id] == ship;
	}

	/// <summary>
	/// Takes <paramref name="ship"/> itself out of this fleet to go on somewhere else, the same object (U.SPC-T417: a
	/// hull jumping out under its own FTL drive is carried, not copied). Its slot keeps a marker of it, gone
	/// (<see cref="Ship.Tombstone"/>), so no other id here moves; escorts it leaves behind follow the marker, so the
	/// fleet's own succession picks a new leader among them as if it had gone through a gate. Its bookings here end.
	/// Returns the marker, or null if it is not in this fleet.
	/// </summary>
	cs::Ref<Ship> Fleet::LetGo(cs::Ref<Ship> ship)
	{
		if (ship == nullptr || ship->Id < 0 || ship->Id >= ships.Count() || ships[ship->Id] != ship) return nullptr;
		auto marker = ship->Tombstone();
		ships[ship->Id] = marker;
		for (const auto& s : ships)
			if (s->Leader == ship) s->Leader = marker;
		ship->Leader = nullptr;
		ship->Battle = nullptr;
		ship->GateKm = std::nullopt;
		ship->StakeoutKm = std::nullopt;
		ship->Route = cs::List<int>(); // C#: Route = null (an int[] is a cs::List by value; empty stands for null)
		ship->RouteIndex = 0;
		return marker;
	}

	/// <summary>
	/// Creates a ship of a class/design (see <see cref="AddShip"/>) and parks it at an exact
	/// body-relative offset facing <paramref name="forward"/>, for a dev/sandbox spawn rather than
	/// a real yard -- parked the same way a jump gate's arrival parks the hull at the far gate
	/// (see <see cref="GateTransit.TryPerform"/>). Free role with every visible body as a port, so
	/// it behaves like any independent ship if ever released from piloting.
	/// </summary>
	cs::Ref<Ship> Fleet::SpawnAt(cs::String name, ShipClass c, cs::Ref<Faction> faction, int frameBody, DVec3 offsetKm, DVec3 forward, double jd)
	{
		auto ship = AddShip(name, c, faction, ShipRole::Free);
		cs::List<int> anywhere;
		for (const auto& b : catalog->Bodies())
			if (b->IsVisible() && !b->IsStar()) anywhere.Add(b->Index);
		ship->Ports = anywhere.ToArray();

		DVec3 v = forward.LengthSquared() > 1e-12 ? forward.Normalized() : DVec3::UnitX;
		DVec3 u = DVec3::Cross(cs::Math::Abs(v.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX, v).Normalized();
		auto orbit = cs::New<ParkingOrbit>();
		orbit->Body = frameBody;
		orbit->RadiusKm = 0;
		orbit->Center = offsetKm;
		orbit->U = u;
		orbit->V = v;
		orbit->EpochJd = jd;
		orbit->PhaseAtEpoch = 0;
		orbit->RateRadPerDay = 0;
		orbit->Fixed = true;
		ship->Orbit = orbit;
		ship->Phase = ShipPhase::Parked;
		ship->PhaseStartJd = jd;
		// Holding here: if nothing holds it (no pilot, no battle), the next update picks a real trip
		// rather than departing for no destination at all.
		ship->DestinationBody = frameBody;
		ship->PhaseEndJd = jd;
		return ship;
	}

	void Fleet::AddEscort(cs::Ref<Ship> leader, cs::Ref<Ship> escort, DVec3 slot)
	{
		// The leader's own first muster (never reassigned since, even if it later ends up riding as
		// somebody else's escort after a merge) names the group for as long as any of today's members
		// are in it (see Ship.BandId, U.SPC-T363).
		if (leader->BandId < 0) leader->BandId = leader->Id;
		escort->Leader = leader;
		escort->BandId = leader->BandId;
		escort->FormationSlot = slot;
		escort->Role = ShipRole::Escort;
	}

	/// <summary>Parks a ship at a body with a randomised departure so a fresh fleet does not move in lockstep.</summary>
	void Fleet::Station(cs::Ref<Ship> ship, int body, double jd)
	{
		Station(ship, body, jd, nullptr);
	}

	/// <summary>As <see cref="Station(Ship, int, double)"/>, in a given orbit (a new hull leaving its yard).</summary>
	void Fleet::Station(cs::Ref<Ship> ship, int body, double jd, cs::Ref<ParkingOrbit> orbit)
	{
		ship->Ring = nullptr;
		ship->HoldPoint = nullptr;
		if (orbit != nullptr)
		{
			auto o = cs::New<ParkingOrbit>();
			o->Body = body;
			o->RadiusKm = orbit->RadiusKm;
			o->U = orbit->U;
			o->V = orbit->V;
			o->Center = orbit->Center;
			o->EpochJd = jd;
			o->PhaseAtEpoch = orbit->PhaseAt(jd) - 0.002;
			o->RateRadPerDay = orbit->RateRadPerDay;
			ship->Orbit = o;
			ship->Phase = ShipPhase::Parked;
			ship->PhaseStartJd = jd;
			ScheduleNext(ship, jd + 0.2 + rng.NextDouble() * 0.5);
			return;
		}
		ship->Orbit = NewParkingOrbit(body, ship);
		ship->Orbit->EpochJd = jd;
		ship->Orbit->PhaseAtEpoch = rng.NextDouble() * Astro::TwoPi;
		ship->Phase = ShipPhase::Parked;
		ship->PhaseStartJd = jd;
		ScheduleNext(ship, jd + rng.NextDouble() * 2.0);
	}

	int Fleet::Body(cs::String name)
	{
		return (*catalog)[name]->Index;
	}

	cs::List<int> Fleet::Bodies(cs::List<cs::String> names)
	{
		auto result = cs::NewArray<int>(names.Count());
		for (int i = 0; i < names.Count(); i++) result[i] = Body(names[i]);
		return result;
	}

	/// <summary>
	/// The default traffic: three navies patrolling with carrier and battleship groups, plus
	/// freighters, ore haulers and couriers working the ports. The schedule is pre-rolled for
	/// a few weeks so ships start spread along their routes instead of all parked.
	/// </summary>
	cs::Ref<Fleet> Fleet::CreateDefault(cs::Ref<Ephemeris> ephemeris, double jd, int seed)
	{
		using Esc = std::pair<ShipClass, cs::String>;
		auto fleet = cs::New<Fleet>(ephemeris, seed);
		double startJd = jd - 45;

		auto sol = fleet->AddFaction("Sol Federation", "SFS", Rgb(0.35f, 0.65f, 1.0f));
		auto belt = fleet->AddFaction("Belt Consortium", "BCS", Rgb(1.0f, 0.6f, 0.25f));
		auto reach = fleet->AddFaction("Outer Reach Compact", "ORS", Rgb(0.72f, 0.5f, 1.0f));
		auto civil = fleet->AddFaction("Independent", "MV", Rgb(0.85f, 0.85f, 0.78f));

		fleet->Group(sol, startJd, ShipClass::Carrier, "Resolute", cs::List<cs::String>{ "Earth", "Moon", "Mars" },
			cs::List<Esc>{ Esc(ShipClass::Battleship, "Dauntless"), Esc(ShipClass::Cruiser, "Aegis"), Esc(ShipClass::Cruiser, "Halberd"),
			Esc(ShipClass::Destroyer, "Kestrel"), Esc(ShipClass::Destroyer, "Merlin") });
		fleet->Group(sol, startJd, ShipClass::Battleship, "Vanguard", cs::List<cs::String>{ "Mars", "Ceres", "Earth" },
			cs::List<Esc>{ Esc(ShipClass::Cruiser, "Lance"), Esc(ShipClass::Destroyer, "Swift"), Esc(ShipClass::Destroyer, "Talon") });
		fleet->Group(belt, startJd, ShipClass::Carrier, "Tycho Ascendant", cs::List<cs::String>{ "Ceres", "Ganymede", "Callisto", "Europa" },
			cs::List<Esc>{ Esc(ShipClass::Battleship, "Iron Ledger"), Esc(ShipClass::Cruiser, "Ore Hawk"), Esc(ShipClass::Cruiser, "Pallas"),
			Esc(ShipClass::Destroyer, "Flint") });
		fleet->Group(belt, startJd, ShipClass::Cruiser, "Hygiea", cs::List<cs::String>{ "Ceres", "Mars" },
			cs::List<Esc>{ Esc(ShipClass::Destroyer, "Shard"), Esc(ShipClass::Destroyer, "Spall") });
		fleet->Group(reach, startJd, ShipClass::Battleship, "Long Night", cs::List<cs::String>{ "Titan", "Titania", "Triton" },
			cs::List<Esc>{ Esc(ShipClass::Cruiser, "Umbra"), Esc(ShipClass::Cruiser, "Perihelion"), Esc(ShipClass::Destroyer, "Wisp") });
		fleet->Group(reach, startJd, ShipClass::Carrier, "Endurance", cs::List<cs::String>{ "Triton", "Charon", "Titan", "Ganymede" },
			cs::List<Esc>{ Esc(ShipClass::Cruiser, "Aphelion"), Esc(ShipClass::Destroyer, "Rime"), Esc(ShipClass::Destroyer, "Hoarfrost") });

		auto ports = fleet->Bodies(cs::List<cs::String>{ "Earth", "Moon", "Mars", "Ceres", "Europa", "Ganymede", "Callisto", "Titan", "Enceladus", "Titania", "Triton" });
		auto innerPorts = fleet->Bodies(cs::List<cs::String>{ "Earth", "Moon", "Mars", "Ceres", "Ganymede" });
		cs::List<cs::String> freighterNames =
		{
			"Bountiful Harvest", "Kepler's Due", "Slow Boat", "Red Ledger", "Ice Mule", "Long Haul", "Patience",
			"Margin of Error", "Cold Comfort", "Ballast", "Heavy Lifting", "Second Wind", "Tin Can", "Far Shore",
		};
		cs::List<cs::Ref<Faction>> traderFactions = { civil, civil, sol, belt, reach };
		for (int i = 0; i < freighterNames.Count(); i++)
		{
			auto f = traderFactions[i % traderFactions.Count()];
			auto ship = fleet->AddShip(freighterNames[i], ShipClass::Freighter, f, ShipRole::Trader);
			ship->Ports = i % 3 == 0 ? ports : innerPorts;
			fleet->Station(ship, ship->Ports[fleet->rng.Next(ship->Ports.Count())], startJd);
		}

		cs::List<cs::String> minerNames = { "Pickaxe", "Rockhound", "Prospect", "Tailings", "Motherlode", "Seam" };
		auto minerPorts = fleet->Bodies(cs::List<cs::String>{ "Ceres", "Mars", "Earth", "Moon" });
		for (const cs::String& name : minerNames)
		{
			auto ship = fleet->AddShip(name, ShipClass::Miner, belt, ShipRole::Miner);
			ship->Ports = minerPorts;
			fleet->Station(ship, minerPorts[fleet->rng.Next(minerPorts.Count())], startJd);
		}

		cs::List<cs::String> courierNames = { "Quicksilver", "Hermes", "Dispatch", "Lightfoot" };
		for (const cs::String& name : courierNames)
		{
			auto ship = fleet->AddShip(name, ShipClass::Courier, civil, ShipRole::Courier);
			ship->Ports = ports;
			fleet->Station(ship, ports[fleet->rng.Next(ports.Count())], startJd);
		}

		// The Consortium's ore tugs (U.SPC-T498): every class a real hull in the fleet. With no belt works here to send
		// them, they push metal from the field round Psyche in to the refinery at Ceres and go back for more.
		auto tugPorts = fleet->Bodies(cs::List<cs::String>{ "Psyche", "Ceres" });
		for (const cs::String& name : cs::List<cs::String>{ "Bulldog", "Sisyphus" })
		{
			auto ship = fleet->AddShip(name, ShipClass::OreTug, belt, ShipRole::Tug);
			ship->Ports = tugPorts;
			fleet->Station(ship, tugPorts[0], startJd);
		}

		fleet->Update(jd);
		return fleet;
	}

	DVec3 Fleet::SlotFor(int index)
	{
		return EscortSlots()[index % EscortSlots().Count()] * (1 + index / EscortSlots().Count() * 0.6);
	}

	// C#: static readonly DVec3[] EscortSlots (a function-local static, see docs/MIGRATION.md).
	const cs::List<DVec3>& Fleet::EscortSlots()
	{
		static const cs::List<DVec3> slots =
		{
			DVec3(0, 0.45, -1.7),
			DVec3(-1.5, 0.05, 0.2),
			DVec3(1.5, 0.05, 0.2),
			DVec3(-2.6, -0.25, 1.3),
			DVec3(2.6, -0.25, 1.3),
		};
		return slots;
	}

	/// <summary>
	/// Rough flight time between two bodies for planning: the brachistochrone over their current
	/// separation, plus a day of manoeuvring.
	/// </summary>
	double Fleet::EstimateTravelDays(cs::Ref<Ship> ship, int from, int to, double jd)
	{
		return EstimateTrip(ship, from, to, jd).Days;
	}

	/// <summary>
	/// A trip as a planner sees it: the days of <see cref="EstimateTravelDays"/>, plus the delta-v its own
	/// drive supplies (each burn's 2·√(a·d) over the straight distance, with a margin for dog-legs and
	/// velocity matching) and the lanes it rides. Hyperlane rides cost no delta-v; they are tolled instead.
	/// </summary>
	TripEstimate Fleet::EstimateTrip(cs::Ref<Ship> ship, int from, int to, double jd)
	{
		bool discard;
		return EstimateTrip(ship, from, to, jd, discard);
	}

	/// <summary>
	/// As <see cref="EstimateTrip(Ship, int, int, double)"/>, and whether a slow boat would rather fly
	/// straight across on a transfer orbit than pay the lanes' tolls (U.SPC-T95). With
	/// <see cref="OrbitalTransfers"/> on, each burn also carries its gravity correction, and a
	/// low-thrust hauler prices each leg as the cheaper of the flip-and-burn and a transfer orbit
	/// (its launch window and flight time against its propellant, at the hull's time value).
	/// </summary>
	TripEstimate Fleet::EstimateTrip(cs::Ref<Ship> ship, int from, int to, double jd, bool& direct)
	{
		direct = false;
		double accel = ship->AccelG() * Astro::GravityKmPerDay2;
		bool conics = UsesConics(ship);
		double deltaV = 0, cost = 0;
		auto Burn = [&](double km, int frame, const LegEnd& start, const LegEnd& end) -> double
		{
			double days = 2 * cs::Math::Sqrt(cs::Math::Max(km, 1.0) / accel);
			double dv = accel * days / Astro::SecondsPerDay * LegMargin + LegMatchKmS;
			if (OrbitalTransfers) dv += GravityLossEstimate(ship, frame, start, end, days);
			double conicDays = 0, conicDv = 0;
			if (conics && BestConic(ship, frame, start, end, jd, conicDays, conicDv)
				&& LegCost(ship, conicDays, conicDv) < LegCost(ship, days, dv))
			{
				days = conicDays;
				dv = conicDv;
			}
			deltaV += dv;
			cost += LegCost(ship, days, dv);
			return days;
		};
		if (Lanes != nullptr)
		{
			int a = Lanes->SystemOf(from), b = Lanes->SystemOf(to);
			cs::Ref<cs::List<LaneHop>> route = a != b ? Lanes->RouteFor(ship, a, b, jd) : nullptr;
			if (route != nullptr)
			{
				int first = CommonFrame(from, a), last = CommonFrame(b, to);
				double days = Burn((Lanes->RampHelio((*route)[0].Lane, a, jd) - ephemeris->HelioAt(from, jd)).Length(),
					first, BodyEnd(first, from, ship, jd), RampEnd(a));
				double rideDays = 0;
				for (int i = 0; i < route->Count(); i++)
				{
					double ride = Lanes->RideDays(ship, (*route)[i].Lane, Lanes->LaneLengthKm((*route)[i].Lane, jd));
					days += ride;
					rideDays += ride;
					if (i + 1 < route->Count()) days += Burn(Lanes->RampRadiusKm((*route)[i].To) * 1.5, (*route)[i].To, RampEnd((*route)[i].To), RampEnd((*route)[i].To));
				}
				days += Burn((Lanes->RampHelio((*route)[route->Count() - 1].Lane, b, jd) - ephemeris->HelioAt(to, jd)).Length(),
					last, RampEnd(b), BodyEnd(last, to, nullptr, jd));
				auto viaLanes = TripEstimate(days + 0.3 + RingOverheadDays(ship, to, jd + days), deltaV, route->Count());
				if (!conics) return viaLanes;
				// A slow boat may sooner wait for a window and fall across than pay the tolls.
				// The burns are priced in cost; the time on the lanes themselves and in the rings is not, so add it here.
				double laneCost = cost + route->Count() * Economy::LaneToll(ship->Spec->Class, ship->MassTonnes()) + (rideDays + viaLanes.Days - days) * TimeValueOf(ship);
				int frame = CommonFrame(from, to);
				double straightDays = 0, straightDv = 0;
				if (BestConic(ship, frame, BodyEnd(frame, from, ship, jd), BodyEnd(frame, to, nullptr, jd), jd, straightDays, straightDv)
					&& LegCost(ship, straightDays, straightDv) < laneCost)
				{
					direct = true;
					double total = straightDays + 1.0;
					return TripEstimate(total + RingOverheadDays(ship, to, jd + total), straightDv, 0);
				}
				return viaLanes;
			}
		}
		int common = CommonFrame(from, to);
		double d = (ephemeris->HelioAt(to, jd) - ephemeris->HelioAt(from, jd)).Length();
		double flight = Burn(d, common, BodyEnd(common, from, ship, jd), BodyEnd(common, to, nullptr, jd)) + 1.0;
		return TripEstimate(flight + RingOverheadDays(ship, to, jd + flight), deltaV, 0);
	}

	// ---------------------------------------------------------------- transfer orbits (U.SPC-T95)

	// C#: static readonly double[] TransferTimeFactors (a function-local static, see docs/MIGRATION.md).
	/// <summary>Flight times a slow boat tries, as fractions of the leg's Hohmann time.</summary>
	const cs::List<double>& Fleet::TransferTimeFactors()
	{
		static const cs::List<double> factors = { 0.15, 0.22, 0.3, 0.45, 0.6, 0.8, 1.0, 1.2 };
		return factors;
	}

	// C#: static readonly double[] CoarseTimeFactors (a function-local static, see docs/MIGRATION.md).
	/// <summary>The fewer flight times a window search first sweeps its whole span with.</summary>
	const cs::List<double>& Fleet::CoarseTimeFactors()
	{
		static const cs::List<double> factors = { 0.15, 0.3, 0.6, 1.0 };
		return factors;
	}

	DriveKind Fleet::DriveOf(cs::Ref<Ship> ship)
	{
		return ship->Spec->AccelG >= TorchAccelG ? DriveKind::Torch : DriveKind::LowThrust;
	}

	// An ore tug works to its operation's timetable, laden or not (U.SPC-T498): it never waits on a launch window.
	bool Fleet::UsesConics(cs::Ref<Ship> ship)
	{
		return OrbitalTransfers && !ship->Spec->IsMilitary && DriveOf(ship) == DriveKind::LowThrust && ship->Role != ShipRole::Tug && ship->Role != ShipRole::Hauler &&
			ship->Role != ShipRole::Colonist && !NavySupply(ship);  // nor does a colony ship (U.SPC-T524): it flies to its voyage's timetable
	}

	// Nor does a navy's supply ship (U.SPC-T525), at its base or on its way to a war fleet that is waiting on it.
	bool Fleet::NavySupply(cs::Ref<Ship> ship)
	{
		return ship->Role == ShipRole::Supply || (ship->Voyage != nullptr && ship->Voyage->Purpose == VoyagePurpose::Supply);
	}

	/// <summary>
	/// What a day of the hull's time is worth to its owner (credits) when it trades time for propellant:
	/// its crew's days (<see cref="Economy.RunningCostPerDay"/>), what its cargo cost (money tied up on
	/// the way, 0.3% a day), and <see cref="UrgentCargoFactor"/> times as much for contracted supplies.
	/// An empty or cheaply laden hauler's time is worth little, so it takes the slow boat.
	/// </summary>
	double Fleet::TimeValuePerDay(cs::Ref<Ship> ship)
	{
		double v = Economy::RunningCostPerDay(ship->Spec->Class);
		if (ship->HasCargo) v += 0.003 * cs::Math::Max(0.0, ship->CargoCost);
		if (ship->HasCargo && ship->CargoBonus > 0) v *= UrgentCargoFactor;
		return v;
	}

	double Fleet::TimeValueOf(cs::Ref<Ship> ship)
	{
		return TimeValue != nullptr ? cs::Math::Max(0.0, TimeValue(ship)) : TimeValuePerDay(ship);
	}

	/// <summary>What a leg costs the ship's owner: its days at its time value and its propellant at the going rate.</summary>
	double Fleet::LegCost(cs::Ref<Ship> ship, double days, double deltaVKmS)
	{
		return cs::Math::Max(0.0, days) * TimeValueOf(ship) + Propulsion::PropellantFor(ship, cs::Math::Max(0.0, deltaVKmS)) * Propulsion::PropellantBasePrice();
	}

	/// <summary>The frame's gravitational parameter in km³/day².</summary>
	double Fleet::FrameMu(int frame)
	{
		return cs::Math::Max(0.0, (*catalog)[frame]->Gm) * Astro::SecondsPerDay * Astro::SecondsPerDay;
	}

	/// <summary>The pole a frame's transfers go round prograde about: the ecliptic's for a star, the body's spin axis otherwise.</summary>
	DVec3 Fleet::FrameNormal(int frame)
	{
		auto f = (*catalog)[frame];
		return f->IsStar() || f->Kind == BodyKind::Barycentre ? DVec3::UnitZ : f->SpinAxis();
	}

	/// <summary>
	/// The world whose well a ship at <paramref name="body"/> climbs out of to fly free in
	/// <paramref name="frame"/>: the body on its chain that orbits the frame directly (Earth for the
	/// Moon in the Sun's frame), or -1 if it is the frame itself.
	/// </summary>
	int Fleet::WellOf(int frame, int body)
	{
		if (body == frame) return -1;
		int b = body;
		while (b >= 0 && (*catalog)[b]->ParentIndex != frame) b = (*catalog)[b]->ParentIndex;
		return b;
	}

	/// <summary>A world's sphere of influence against the frame it orbits, km, where it is now.</summary>
	double Fleet::SoiKm(int frame, int well, double jd)
	{
		return Conics::SoiRadius(RelativeTo(frame, well, jd).Length(), (*catalog)[well]->Gm, (*catalog)[frame]->Gm);
	}

	/// <summary>The parking orbit radius a planner assumes at a world it has not yet reached, km.</summary>
	double Fleet::NominalParkingKm(int body)
	{
		auto b = (*catalog)[body];
		return cs::Math::Max(b->RadiusKm * 4.75, b->RadiusKm + 160);
	}

	/// <summary>A parking orbit the planner assumes at a world (equatorial, at <see cref="NominalParkingKm"/>), for weighing windows.</summary>
	cs::Ref<ParkingOrbit> Fleet::NominalOrbit(int body)
	{
		auto b = (*catalog)[body];
		DVec3 normal = b->SpinAxis().LengthSquared() > 0 ? b->SpinAxis().Normalized() : DVec3::UnitZ;
		DVec3 u = DVec3::Cross(cs::Math::Abs(normal.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX, normal).Normalized();
		double r = NominalParkingKm(body);
		auto orbit = cs::New<ParkingOrbit>();
		orbit->Body = body;
		orbit->RadiusKm = r;
		orbit->U = u;
		orbit->V = DVec3::Cross(normal, u);
		orbit->RateRadPerDay = Kepler::CircularRate(cs::Math::Max(b->Gm, 0.0), r);
		return orbit;
	}

	/// <summary>
	/// Solves the transfer orbit from <paramref name="departure"/> (at <paramref name="from"/>) to
	/// <paramref name="arrival"/> (at <paramref name="to"/>) leaving at <paramref name="departJd"/> and
	/// taking <paramref name="days"/>, in <paramref name="frame"/>: a Lambert conic between the ends,
	/// patched onto an escape hyperbola where the ship leaves a world whose sphere of influence is well
	/// outside its orbit (and a capture hyperbola where it arrives at one), and the burns at each end.
	/// A free (non-fixed) arrival orbit is phased so the ship circularises where the capture leaves it.
	/// Returns null when there is no such orbit, or its legs inside the spheres would overlap.
	/// </summary>
	cs::Ref<ConicTransfer> Fleet::SolveConic(cs::Ref<Ship> ship, int frame, int from, int to, double departJd, double days, cs::Ref<ParkingOrbit> departure, cs::Ref<ParkingOrbit> arrival)
	{
		double mu = FrameMu(frame);
		if (!(mu > 0) || !(days > 0)) return nullptr;
		constexpr double day = Astro::SecondsPerDay;
		double arriveJd = departJd + days;

		DVec3 startRel = RelativeTo(frame, from, departJd) + departure->OffsetAt(departJd);
		DVec3 startVel = (from == frame ? DVec3::Zero : VelocityRelativeTo(frame, from, departJd)) + departure->VelocityAt(departJd);
		int esc = WellOf(frame, from);
		DVec3 escR = DVec3::Zero, escV = DVec3::Zero;
		double escSoi = 0, rpDep = 0;
		bool patchedOut = false;
		if (esc >= 0 && (*catalog)[esc]->Gm > 0)
		{
			escR = RelativeTo(frame, esc, departJd);
			escV = VelocityRelativeTo(frame, esc, departJd);
			escSoi = SoiKm(frame, esc, departJd);
			rpDep = (startRel - escR).Length();
			patchedOut = rpDep > 0 && escSoi > rpDep * PatchFactor;
		}

		DVec3 toR = RelativeTo(frame, to, arriveJd);
		DVec3 toV = to == frame ? DVec3::Zero : VelocityRelativeTo(frame, to, arriveJd);
		int cap = WellOf(frame, to);
		DVec3 capR = DVec3::Zero, capV = DVec3::Zero;
		double capSoi = 0, rpArr = 0;
		bool patchedIn = false;
		if (cap >= 0 && (*catalog)[cap]->Gm > 0)
		{
			capR = RelativeTo(frame, cap, arriveJd);
			capV = VelocityRelativeTo(frame, cap, arriveJd);
			capSoi = SoiKm(frame, cap, arriveJd);
			rpArr = (toR + arrival->OffsetAt(arriveJd) - capR).Length();
			patchedIn = rpArr > 0 && capSoi > rpArr * PatchFactor;
		}

		DVec3 r1 = patchedOut ? escR : startRel;
		DVec3 h = DVec3::Cross(r1, patchedOut ? escV : startVel);
		DVec3 normal = h.LengthSquared() > 1e-6 ? h.Normalized() : FrameNormal(frame);
		if (!patchedIn && !arrival->Fixed)
		{
			// Arrive where the conic comes in: facing the incoming ship, or (in orbit round the
			// frame's own centre) a little short of opposite where it left, a near-Hohmann half turn.
			DVec3 dir;
			if (to == frame)
			{
				DVec3 rh = r1.Normalized();
				DVec3 n = arrival->U.LengthSquared() > 0 ? DVec3::Cross(arrival->U, arrival->V).Normalized() : normal;
				dir = rh * -cs::Math::Cos(10 * Astro::Deg2Rad) + DVec3::Cross(n, rh) * cs::Math::Sin(10 * Astro::Deg2Rad);
			}
			else dir = (r1 - toR).Normalized();
			arrival->EpochJd = arriveJd;
			arrival->PhaseAtEpoch = arrival->PhaseFacing(dir);
		}
		DVec3 endRel = toR + arrival->OffsetAt(arriveJd);
		DVec3 endVel = toV + arrival->VelocityAt(arriveJd);
		DVec3 r2 = patchedIn ? capR : endRel;
		DVec3 v1, v2;
		if (!Conics::Lambert(r1, r2, days, mu, normal, v1, v2)) return nullptr;

		auto c = cs::New<ConicTransfer>();
		c->Mu = mu;
		c->Days = days;
		// The legs inside each sphere of influence, and the states where they hand over to the frame's conic.
		DVec3 exit = startRel, entry = endRel, exitVel = startVel, entryVel = endVel;
		if (patchedOut)
		{
			// The escape burn at the ship's own spot in its parking orbit, onto the hyperbola through it.
			DVec3 vInf = v1 - escV;
			DVec3 rel = startRel - escR;
			DVec3 relVel = startVel - escV;
			DVec3 nPark = DVec3::Cross(rel, relVel);
			c->Escape = Hyperbola::Through(FrameMu(esc), rel, vInf, nPark.LengthSquared() > 0 ? nPark.Normalized() : normal);
			if (c->Escape == nullptr) return nullptr;
			c->FromR0 = escR;
			c->FromV0 = escV;
			c->FromSoiKm = escSoi;
			c->ExitDays = c->Escape->TimeToRadius(escSoi);
			if (c->ExitDays > 0.45 * days) return nullptr;
			DVec3 bodyAt, bodyVel;
			Conics::Propagate(escR, escV, mu, c->ExitDays, bodyAt, bodyVel);
			exit = bodyAt + c->Escape->PositionAt(c->ExitDays);
			exitVel = bodyVel + c->Escape->VelocityAt(c->ExitDays);
			c->DepartureBurnKmS = (c->Escape->Q * c->Escape->PeriapsisSpeed() - relVel).Length() / day;
		}
		if (patchedIn)
		{
			DVec3 vInf = v2 - capV;
			if (!arrival->Fixed && to == cap && arrival->U.LengthSquared() > 0)
			{
				// Circularise where a capture from this approach bottoms out: θ∞ round from the incoming asymptote.
				auto ideal = Hyperbola::Arrival(FrameMu(cap), rpArr, vInf, DVec3::Cross(arrival->U, arrival->V).Normalized());
				if (ideal == nullptr) return nullptr;
				arrival->EpochJd = arriveJd;
				arrival->PhaseAtEpoch = arrival->PhaseFacing(ideal->P);
				endRel = toR + arrival->OffsetAt(arriveJd);
				endVel = toV + arrival->VelocityAt(arriveJd);
			}
			c->Capture = Hyperbola::Through(FrameMu(cap), endRel - capR, vInf, normal);
			if (c->Capture == nullptr) return nullptr;
			c->ToR1 = capR;
			c->ToV1 = capV;
			c->ToSoiKm = capSoi;
			c->EntryDays = c->Capture->TimeToRadius(capSoi);
			if (c->EntryDays > 0.45 * days) return nullptr;
			DVec3 bodyAt, bodyVel;
			Conics::Propagate(capR, capV, mu, -c->EntryDays, bodyAt, bodyVel);
			entry = bodyAt + c->Capture->PositionAt(-c->EntryDays);
			entryVel = bodyVel + c->Capture->VelocityAt(-c->EntryDays);
			c->ArrivalBurnKmS = (c->Capture->Q * c->Capture->PeriapsisSpeed() - (endVel - capV)).Length() / day;
		}
		double cruise = days - c->ExitDays - c->EntryDays;
		if (cruise < 0.1 * days) return nullptr;
		if (patchedOut || patchedIn)
		{
			// The frame's conic from handoff to handoff, and the trims that match it to each hyperbola there.
			if (!Conics::Lambert(exit, entry, cruise, mu, normal, v1, v2)) return nullptr;
			c->CorrectionBurnKmS = ((patchedOut ? (v1 - exitVel).Length() : 0) + (patchedIn ? (v2 - entryVel).Length() : 0)) / day;
		}
		c->R0 = exit;
		c->V0 = v1;
		c->R1 = entry;
		c->V1 = v2;
		if (!patchedOut) c->DepartureBurnKmS = (v1 - startVel).Length() / day;
		if (!patchedIn) c->ArrivalBurnKmS = (v2 - endVel).Length() / day;

		double accel = cs::Math::Max(1e-9, ship->AccelG() * 9.80665e-3);
		c->DepartureBurnDays = c->DepartureBurnKmS / accel / day;
		c->ArrivalBurnDays = c->ArrivalBurnKmS / accel / day;
		if (c->DepartureBurnDays + c->ArrivalBurnDays > 0.5 * days) return nullptr;
		return c;
	}

	/// <summary>The Hohmann time between the ends of a leg about its frame, days (the yardstick for the flight times tried).</summary>
	double Fleet::HohmannDaysFor(int frame, int from, int to, double jd, cs::Ref<ParkingOrbit> departure, cs::Ref<ParkingOrbit> arrival)
	{
		double r1 = (RelativeTo(frame, from, jd) + departure->OffsetAt(jd)).Length();
		double r2 = (RelativeTo(frame, to, jd) + arrival->OffsetAt(jd)).Length();
		double mu = FrameMu(frame);
		return mu > 0 && r1 > 0 && r2 > 0 ? Conics::HohmannTime(mu, r1, r2) : 0;
	}

	/// <summary>
	/// Weighs a transfer orbit against the flip-and-burn just planned onto <paramref name="ship"/>, leaving
	/// now: the flight times it tries (and the one its window was planned for), each priced at
	/// <see cref="LegCost"/>; if the cheapest beats the flip-and-burn and keeps clear of everything, it
	/// becomes the transit and its arrival orbit is returned. Otherwise the flip-and-burn stands (null).
	/// </summary>
	cs::Ref<ParkingOrbit> Fleet::TryConic(cs::Ref<Ship> ship, int frame, int from, int to, double departJd, cs::Func<cs::Ref<ParkingOrbit>> arrivalOrbit, double keepOut)
	{
		double brachyCost = LegCost(ship, ship->PhaseEndJd - departJd, Propulsion::TransitDeltaV(ship));
		auto arrival = arrivalOrbit();
		double hohmann = HohmannDaysFor(frame, from, to, departJd, ship->Orbit, arrival);
		if (!(hohmann > 0)) return nullptr;
		double sincePlan = cs::Double::IsNaN(ship->PlannedConicDepartJd) ? cs::Double::PositiveInfinity : cs::Math::Abs(ship->PlannedConicDepartJd - departJd);
		// Its window planner already judged the flip-and-burn the better buy for this leg.
		if (ship->PlannedConicDays < 0 && sincePlan <= MaxFacingWaitDays + 0.5) return nullptr;
		bool planned = ship->PlannedConicDays > 0 && sincePlan < 1;
		cs::List<double> tries;
		if (planned) tries.AddRange(cs::List<double>{ ship->PlannedConicDays, ship->PlannedConicDays * 0.8, ship->PlannedConicDays * 1.25 });
		else for (double f : TransferTimeFactors()) tries.Add(hohmann * f);
		double bestCost = brachyCost, bestDays = 0;
		for (double days : tries)
		{
			auto c = SolveConic(ship, frame, from, to, departJd, days, ship->Orbit, arrival);
			if (c == nullptr) continue;
			double cost = LegCost(ship, days, c->DeltaVKmS());
			if (cost < bestCost)
			{
				bestCost = cost;
				bestDays = days;
			}
		}
		if (bestDays <= 0) return nullptr;
		auto conic = SolveConic(ship, frame, from, to, departJd, bestDays, ship->Orbit, arrival);
		if (conic == nullptr) return nullptr;

		auto saved = std::make_tuple(ship->Path, ship->PathCumulative, ship->PathLength, ship->PhaseEndJd, ship->DepartureVelocity, ship->ArrivalVelocity, ship->GravityLossKmS);
		ship->Conic = conic;
		ship->GravityLossKmS = 0;
		ship->PhaseStartJd = departJd;
		ship->PhaseEndJd = departJd + conic->Days;
		ship->DepartureVelocity = conic->V0;
		ship->ArrivalVelocity = conic->V1;
		// The arc as a polyline, for whatever reads the path (its length on the helm, a diversion's check).
		constexpr int n = 32;
		auto path = cs::NewArray<DVec3>(n + 1);
		for (int i = 0; i <= n; i++) path[i] = conic->OffsetAt(conic->Days * i / n);
		ship->Path = path;
		ship->PathCumulative = Cumulative(path);
		ship->PathLength = ship->PathCumulative[n];
		if (Clear(ship, frame, from, to, keepOut)) return arrival;

		ship->Conic = nullptr;
		std::tie(ship->Path, ship->PathCumulative, ship->PathLength, ship->PhaseEndJd, ship->DepartureVelocity, ship->ArrivalVelocity, ship->GravityLossKmS) = saved;
		return nullptr;
	}

	/// <summary>
	/// Sets a slow boat's departure for its next trip (U.SPC-T95): over the coming synodic period
	/// (capped at <see cref="MaxWindowWaitDays"/>, and at however long waiting would cost more than
	/// flying now) it tries leaving on each of a grid of dates with each of a range of flight times,
	/// and keeps the cheapest wait plus transfer. If that beats a flip-and-burn now, it waits in port for
	/// that window and records the plan; otherwise it returns false and leaves as usual.
	/// </summary>
	bool Fleet::PlanWindow(cs::Ref<Ship> ship, int here, int next, double earliestJd, double& departJd)
	{
		departJd = earliestJd;
		if (ship->Wander || ship->Orbit == nullptr || ship->GateKm.has_value()) return false;
		int legTo = next;
		cs::Func<cs::Ref<ParkingOrbit>, double> arrivalAt = nullptr;
		bool direct = false;
		DVec3 target = ephemeris->HelioAt(next, earliestJd);
		if (Lanes != nullptr)
		{
			int a = Lanes->SystemOf(here), b = Lanes->SystemOf(next);
			cs::Ref<cs::List<LaneHop>> route = a != b ? Lanes->Route(a, b, earliestJd) : nullptr;
			if (route != nullptr)
			{
				EstimateTrip(ship, here, next, earliestJd, direct);
				if (!direct)
				{
					LaneHop hop = (*route)[0];
					legTo = hop.From;
					arrivalAt = [this, hop](double jd) { return RampOrbit(hop, hop.From, jd); };
					target = Lanes->RampHelio(hop.Lane, a, earliestJd);
				}
			}
		}
		if (arrivalAt == nullptr) arrivalAt = [this, legTo](double jd) { (void)jd; return NominalOrbit(legTo); };
		int frame = CommonFrame(here, legTo);
		double mu = FrameMu(frame);
		if (!(mu > 0)) return false;

		// What flying now by flip-and-burn would cost, for comparison.
		double accel = ship->AccelG() * Astro::GravityKmPerDay2;
		double km = cs::Math::Max(1.0, (target - ephemeris->HelioAt(here, earliestJd)).Length());
		double burnDays = 2 * cs::Math::Sqrt(km / accel);
		double burnDv = accel * burnDays / Astro::SecondsPerDay * LegMargin + LegMatchKmS
						+ GravityLossEstimate(ship, frame, BodyEnd(frame, here, ship, earliestJd), BodyEnd(frame, legTo, nullptr, earliestJd), burnDays);
		double brachyCost = LegCost(ship, burnDays, burnDv);
		double tv = cs::Math::Max(1e-6, TimeValueOf(ship));

		// Windows come round once a synodic period of the two ends about the frame.
		double n1 = here == frame ? ship->Orbit->RateRadPerDay : MeanMotion(frame, WellOf(frame, here), earliestJd);
		auto probe = arrivalAt(earliestJd);
		double n2 = legTo == frame ? (probe->Fixed ? 0 : probe->RateRadPerDay) : MeanMotion(frame, WellOf(frame, legTo), earliestJd);
		double synodic = cs::Math::Abs(n1 - n2) > 1e-12 ? Astro::TwoPi / cs::Math::Abs(n1 - n2) : MaxWindowWaitDays;
		double maxWait = cs::Math::Max(0.0, cs::Math::Min(cs::Math::Min(synodic, MaxWindowWaitDays), brachyCost / tv));
		double hohmann = HohmannDaysFor(frame, here, legTo, earliestJd, ship->Orbit, probe);
		if (!(hohmann > 0)) return false;

		double bestCost = brachyCost, bestWait = -1, bestDays = 0;
		double period = !ship->Orbit->Fixed && ship->Orbit->RateRadPerDay > 0 ? Astro::TwoPi / ship->Orbit->RateRadPerDay : 0;
		auto Try = [&](double wait, const cs::List<double>& factors, double onlyDays = 0)
		{
			if (wait < 0 || wait > maxWait + period) return;
			double jd = earliestJd + wait;
			auto arrival = arrivalAt(jd);
			for (double f : factors)
			{
				double days = onlyDays > 0 ? onlyDays : hohmann * f;
				if (onlyDays > 0 && f != factors[0]) break;
				auto c = SolveConic(ship, frame, here, legTo, jd, days, ship->Orbit, arrival);
				if (c == nullptr) continue;
				double cost = wait * tv + LegCost(ship, days, c->DeltaVKmS());
				if (cost < bestCost)
				{
					bestCost = cost;
					bestWait = wait;
					bestDays = days;
				}
			}
		};
		// At coarse detail (a system nobody is looking at) the grid is half as fine and the escape burn is not timed to the turn.
		int steps = coarse ? 8 : 16, refine = coarse ? 2 : 4;
		double step = maxWait / steps;
		// A coarse look over the whole span, then every flight time around the best date found.
		for (int i = 0; i <= steps; i++) Try(step * i, CoarseTimeFactors());
		if (bestWait >= 0)
		{
			double centre = bestWait;
			if (step > 0)
				for (int i = -refine; i <= refine; i++) Try(centre + step * i / refine, TransferTimeFactors());
			else Try(centre, TransferTimeFactors());
		}
		if (bestWait < 0) return false;
		// Time the escape burn to the ship's place in its parking orbit: within one turn of it, the
		// moment it lies the right way round from where it is going.
		if (period > 0 && period < 5 && !coarse)
		{
			double centre = bestWait, days = bestDays;
			for (int i = -12; i <= 12; i++) if (i != 0) Try(centre + period * i / 24.0, TransferTimeFactors(), days);
		}
		ship->PlannedConicDepartJd = earliestJd + bestWait;
		ship->PlannedConicDays = bestDays;
		ship->PlannedDirect = direct;
		departJd = ship->PlannedConicDepartJd;
		return true;
	}

	/// <summary>Mean motion (rad/day) of a world about the frame, from its distance now.</summary>
	double Fleet::MeanMotion(int frame, int well, double jd)
	{
		if (well < 0) return 0;
		double r = RelativeTo(frame, well, jd).Length();
		double mu = FrameMu(frame);
		return r > 0 && mu > 0 ? cs::Math::Sqrt(mu / (r * r * r)) : 0;
	}

	// (Fleet::LegEnd, Fleet.cs line 2747: a readonly struct whose members are all inline; declared in Fleet.h.)

	/// <summary>A world (or a parking orbit round the frame's own centre) as a leg end.</summary>
	Fleet::LegEnd Fleet::BodyEnd(int frame, int body, cs::Ref<Ship> ship, double jd)
	{
		bool parkedHere = ship != nullptr && ship->Orbit != nullptr && ship->Orbit->Body == body && !ship->Orbit->Fixed && ship->Orbit->RadiusKm > 0;
		double parking = parkedHere ? ship->Orbit->RadiusKm : NominalParkingKm(body);
		if (body == frame) return LegEnd(-1, parking, 0, false, false);
		int well = WellOf(frame, body);
		if (well < 0) return LegEnd(-1, parking, 0, false, false);
		double r = RelativeTo(frame, well, jd).Length();
		double rp = well == body ? parking : RelativeTo(well, body, jd).Length();
		bool patched = (*catalog)[well]->Gm > 0 && SoiKm(frame, well, jd) > rp * PatchFactor;
		return LegEnd(well, r, rp, false, patched);
	}

	/// <summary>A hyperlane ramp (a point held still off its planet) as a leg end in that planet's frame.</summary>
	Fleet::LegEnd Fleet::RampEnd(int system)
	{
		return LegEnd(-1, Lanes->RampRadiusKm(system), 0, true, false);
	}

	/// <summary>
	/// A flip-and-burn's gravity correction as the estimate sees it (U.SPC-T95): climbing straight out of
	/// each end's well from rest (<see cref="Conics.GravityLossLeaving"/>), plus the frame's own pull
	/// across the flight between two worlds that orbit it.
	/// </summary>
	double Fleet::GravityLossEstimate(cs::Ref<Ship> ship, int frame, LegEnd start, LegEnd end, double days)
	{
		double accel = ship->AccelG() * 9.80665e-3;
		double loss = 0;
		for (const LegEnd& e : cs::List<LegEnd>{ start, end })
			loss += e.Well >= 0 ? Conics::GravityLossLeaving((*catalog)[e.Well]->Gm, e.Rp, accel) : Conics::GravityLossLeaving((*catalog)[frame]->Gm, e.R, accel);
		if (start.Well >= 0 && end.Well >= 0)
		{
			double r = 0.5 * (start.R + end.R);
			if (r > 0) loss += cs::Math::Max(0.0, (*catalog)[frame]->Gm) / (r * r) * days * Astro::SecondsPerDay;
		}
		return loss;
	}

	/// <summary>
	/// A flip-and-burn's gravity correction as flown (U.SPC-T95): the pull of the frame's centre and of
	/// every world on either end's chain, summed along the planned path, ∫ Σ μ/d² dt. A torch spends
	/// that holding its straight line instead of falling off it.
	/// </summary>
	double Fleet::GravityLoss(cs::Ref<Ship> ship, int frame, int from, int to)
	{
		cs::List<int> bodies;
		if ((*catalog)[frame]->Gm > 0) bodies.Add(frame);
		for (int end : { from, to })
			for (int b = end; b >= 0 && b != frame; b = (*catalog)[b]->ParentIndex)
				if (!bodies.Contains(b) && (*catalog)[b]->Gm > 0) bodies.Add(b);
		double total = ship->PhaseEndJd - ship->PhaseStartJd;
		if (!(total > 0) || bodies.Count() == 0) return 0;
		// Dense at the ends, where the wells are steep and the ship is slow.
		constexpr int samples = 64;
		double loss = 0, prevTau = 0, prevG = 0;
		for (int k = 0; k <= samples; k++)
		{
			double u = static_cast<double>(k) / samples;
			double tau = total * (u < 0.5 ? 2 * u * u : 1 - 2 * (1 - u) * (1 - u));
			double jd = ship->PhaseStartJd + tau;
			DVec3 helio = HelioAt(frame, jd) + TransitOffsetAt(ship, tau);
			double g = 0;
			for (int b : bodies)
			{
				double d = cs::Math::Max((helio - HelioAt(b, jd)).Length(), (*catalog)[b]->RadiusKm);
				if (d > 0) g += (*catalog)[b]->Gm / (d * d);
			}
			if (k > 0) loss += 0.5 * (g + prevG) * (tau - prevTau) * Astro::SecondsPerDay;
			prevTau = tau;
			prevG = g;
		}
		return loss;
	}

	/// <summary>
	/// The cheapest transfer orbit for a leg as the trip estimate sees it, priced at
	/// <see cref="LegCost"/>: a Hohmann transfer (waiting for its window when both ends are worlds
	/// going round the frame), or a faster Lambert arc leaving now. False if the frame has no gravity.
	/// </summary>
	bool Fleet::BestConic(cs::Ref<Ship> ship, int frame, LegEnd start, LegEnd end, double jd, double& days, double& dv)
	{
		days = dv = 0;
		auto options = ConicOptions(frame, start, end, jd);
		if (options.Count() == 0) return false; // C#: options == null || options.Length == 0 (null is an empty list here)
		double best = cs::Double::PositiveInfinity;
		for (const auto& o : options)
		{
			double cost = LegCost(ship, std::get<0>(o), std::get<1>(o));
			if (cost < best)
			{
				best = cost;
				days = std::get<0>(o);
				dv = std::get<1>(o);
			}
		}
		return best < cs::Double::PositiveInfinity;
	}

	long long Fleet::Bucket(double km)
	{
		return km > 0 ? cs::ToLong(cs::Math::Round(cs::Math::Log(km) * 200)) : 0LL;
	}

	// C#: (double days, double dv)[] ConicOptions(...): null is returned as an empty list (the one caller treats both alike).
	cs::List<std::pair<double, double>> Fleet::ConicOptions(int frame, LegEnd start, LegEnd end, double jd)
	{
		double mu = FrameMu(frame);
		double gm = (*catalog)[frame]->Gm;
		if (!(mu > 0) || start.R <= 0 || end.R <= 0) return cs::List<std::pair<double, double>>();
		bool worlds = start.Well >= 0 && end.Well >= 0;
		long long flags = (start.Still ? 1 : 0) | (end.Still ? 2 : 0) | (start.Patched ? 4 : 0) | (end.Patched ? 8 : 0);
		auto key = std::make_tuple(frame, start.Well, end.Well, Bucket(start.R) * 4096 + Bucket(start.Rp), Bucket(end.R) * 4096 + Bucket(end.Rp),
			flags, worlds ? cs::ToLong(cs::Math::Floor(jd)) : 0LL, 0LL);
		cs::List<std::pair<double, double>> cached;
		if (conicOptions.TryGetValue(key, cached)) return cached;
		if (conicOptions.Count() > 20000) conicOptions.Clear();

		constexpr double day = Astro::SecondsPerDay;
		double r1 = start.R, r2 = end.R;
		double hohmann = Conics::HohmannTime(mu, r1, r2);
		auto EndBurn = [&](const LegEnd& e, double vInf, double vTransfer) -> double
		{
			if (e.Patched) return Conics::EscapeBurn((*catalog)[e.Well]->Gm, e.Rp, vInf, Conics::CircularSpeed((*catalog)[e.Well]->Gm, e.Rp));
			return e.Still ? vTransfer : vInf;
		};
		cs::List<std::pair<double, double>> list;
		double a = 0.5 * (r1 + r2);
		double vt1 = cs::Math::Sqrt(gm * (2 / r1 - 1 / a)), vt2 = cs::Math::Sqrt(gm * (2 / r2 - 1 / a));
		double vc1 = cs::Math::Sqrt(gm / r1), vc2 = cs::Math::Sqrt(gm / r2);
		double wait = 0;
		if (worlds)
		{
			DVec3 p1 = RelativeTo(frame, start.Well, jd);
			DVec3 p2 = RelativeTo(frame, end.Well, jd);
			DVec3 n = FrameNormal(frame);
			double phase = cs::Math::Atan2(DVec3::Dot(n, DVec3::Cross(p1, p2)), DVec3::Dot(p1, p2));
			wait = Conics::WaitForPhase(phase, Conics::HohmannLeadAngle(mu, r1, r2), cs::Math::Sqrt(mu / (r1 * r1 * r1)), cs::Math::Sqrt(mu / (r2 * r2 * r2)));
			if (cs::Double::IsInfinity(wait)) wait = 0;
		}
		list.Add(std::pair<double, double>(wait + hohmann, EndBurn(start, cs::Math::Abs(vt1 - vc1), vt1) + EndBurn(end, cs::Math::Abs(vc2 - vt2), vt2)));

		// Faster arcs leaving now: between the worlds where they really are, or between idealised
		// circular orbits a long arc apart (a parking orbit can be phased to suit within hours).
		for (double f : { 0.18, 0.3, 0.5, 0.8 })
		{
			double tof = hohmann * f;
			DVec3 a1, a2, w1, w2, normal;
			if (worlds)
			{
				a1 = RelativeTo(frame, start.Well, jd);
				w1 = VelocityRelativeTo(frame, start.Well, jd);
				a2 = RelativeTo(frame, end.Well, jd + tof);
				w2 = VelocityRelativeTo(frame, end.Well, jd + tof);
				DVec3 h = DVec3::Cross(a1, w1);
				normal = h.LengthSquared() > 0 ? h.Normalized() : DVec3::UnitZ;
			}
			else
			{
				double ang = 150 * Astro::Deg2Rad;
				a1 = DVec3(r1, 0, 0);
				a2 = DVec3(r2 * cs::Math::Cos(ang), r2 * cs::Math::Sin(ang), 0);
				w1 = start.Still ? DVec3::Zero : DVec3(0, vc1 * day, 0);
				w2 = end.Still ? DVec3::Zero : DVec3(-cs::Math::Sin(ang), cs::Math::Cos(ang), 0) * (vc2 * day);
				normal = DVec3::UnitZ;
			}
			DVec3 v1, v2;
			if (!Conics::Lambert(a1, a2, tof, mu, normal, v1, v2)) continue;
			double inf1 = (v1 - w1).Length() / day, inf2 = (v2 - w2).Length() / day;
			list.Add(std::pair<double, double>(tof, EndBurn(start, inf1, inf1) + EndBurn(end, inf2, inf2)));
		}
		auto result = list.ToArray();
		conicOptions[key] = result;
		return result;
	}

	/// <summary>Where the first leg of a trip heads: the first lane's ramp, or the destination itself.</summary>
	DVec3 Fleet::FirstLegTarget(int here, int next, double jd)
	{
		if (Lanes != nullptr)
		{
			int a = Lanes->SystemOf(here), b = Lanes->SystemOf(next);
			cs::Ref<cs::List<LaneHop>> route = a != b ? Lanes->Route(a, b, jd) : nullptr;
			if (route != nullptr) return Lanes->RampHelio((*route)[0].Lane, a, jd);
		}
		return ephemeris->HelioAt(next, jd);
	}

	// C#: public Random Rng => rng; (non-const: callers draw from it)
	cs::Random& Fleet::Rng() const
	{
		return rng;
	}

	cs::Ref<Ship> Fleet::Group(cs::Ref<Faction> faction, double jd, ShipClass leaderClass, cs::String leaderName, cs::List<cs::String> route,
		cs::List<std::pair<ShipClass, cs::String>> escorts)
	{
		auto leader = AddShip(leaderName, leaderClass, faction, ShipRole::Patrol);
		leader->Route = Bodies(route);
		leader->RouteIndex = 0;
		for (int i = 0; i < escorts.Count(); i++)
		{
			auto escort = AddShip(std::get<1>(escorts[i]), std::get<0>(escorts[i]), faction, ShipRole::Escort);
			AddEscort(leader, escort, SlotFor(i));
		}
		Station(leader, leader->Route[0], jd);
		return leader;
	}
} // namespace SpaceGod::Core
