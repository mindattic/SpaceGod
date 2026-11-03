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
			if (fromSystem != toSystem) ship->Journey = Lanes->RouteFor(ship, fromSystem, toSystem, departJd);
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
				auto route = fromSystem != toSystem ? Lanes->Route(fromSystem, toSystem, departJd) : cs::List<LaneHop>();
				if (route.Count() > 0)
				{
					RefundUnflown(ship, departJd);
					ship->Orbit = home;
					ship->Ring = ring;
					ship->HoldPoint = hold;
					ship->Journey = route;
					ship->JourneyIndex = 0;
					auto hop = route[0];
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
			if (between.Count() == 0) return false;
			hops.AddRange(between);
		}
		hops.Add(LaneHop(target, target->A, target->B));
		ship->Journey = hops;
		ship->JourneyIndex = 0;
		return true;
	}

	cs::Ref<ParkingOrbit> Fleet::WanderOrbit(int body, cs::Ref<Ship> ship)
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
		auto route = toSystem != hop.From ? Lanes->RouteFor(ship, hop.From, toSystem, jd) : cs::List<LaneHop>();
		ship->Journey = route;
		ship->JourneyIndex = 0;
		if (route.Count() > 0)
		{
			auto next = route[0];
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

	// ---- (Fleet.cs lines 1002-2962 follow, appended from the other parts)
