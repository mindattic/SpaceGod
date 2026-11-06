// US-T164: battles that look like two nests of hornets (see Combat/Choreography.h).

#include "Combat/Choreography.h"

#include "Astro.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/PilotFlight.h"
#include "Combat/WeakPatches.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	namespace
	{
		constexpr double TwoPi = 6.283185307179586;

		double Clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

		/// <summary>The fractional part of a unit's seed, 0-1: its own share of a timing, the same every run.</summary>
		double Frac(double v) { return v - cs::Math::Floor(v); }

		/// <summary>A unit vector square to <paramref name="v"/> (unit) toward <paramref name="hint"/>, else any.</summary>
		DVec3 SquareTo(DVec3 v, DVec3 hint)
		{
			auto s = hint - v * DVec3::Dot(hint, v);
			if (s.LengthSquared() > 1e-12) return s.Normalized();
			auto any = DVec3::Cross(v, cs::Math::Abs(v.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX);
			return any.Normalized();
		}

		DVec3 KeelOf(const cs::Ref<BattleUnit>& t) { return t->Forward.LengthSquared() > 1e-18 ? t->Forward.Normalized() : DVec3::UnitX; }

		DVec3 Around(DVec3 fromTarget)
		{
			auto around = DVec3::Cross(DVec3::UnitZ, fromTarget);
			if (around.LengthSquared() < 1e-6) around = DVec3::UnitY;
			return around.Normalized();
		}
	}

	bool Choreography::IsCapital(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Kind == UnitKind::Ship && u->LengthKm >= CapitalKm;
	}

	bool Choreography::IsLightWarship(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Kind == UnitKind::Ship && u->LengthKm >= LightMinKm && u->LengthKm < CapitalKm && u->Combat != nullptr && u->Combat->Armed();
	}

	bool Choreography::Strafeable(const cs::Ref<BattleUnit>& t) { return WeakPoints::IsCapital(t); }

	bool Choreography::Strafing(const cs::Ref<BattleUnit>& u)
	{
		auto m = u->Maneuver;
		return m == DogfightManeuver::StrafeBurn || m == DogfightManeuver::StrafeCoast || m == DogfightManeuver::StrafeFlip;
	}

	bool Choreography::Coasting(const cs::Ref<BattleUnit>& u) { return u != nullptr && u->Maneuver == DogfightManeuver::StrafeCoast; }

	void Choreography::EndRun(const cs::Ref<BattleUnit>& u)
	{
		u->Maneuver = DogfightManeuver::Attack;
		u->ManeuverLeft = 0;
	}

	// ---------------------------------------------------------------- the strafing run

	DVec3 Choreography::RakePoint(const cs::Ref<BattleUnit>& t, DVec3 from, DVec3 runDir)
	{
		auto a = KeelOf(t);
		double h = t->LengthKm * 0.5;
		double along = DVec3::Dot(from - t->Pos, a);
		double ahead = DVec3::Dot(runDir, a) >= 0 ? 1 : -1;
		double at = Clamp(along + ahead * (0.12 * t->LengthKm + 0.15), -h, h);
		return t->Pos + a * at;
	}

	Choreography::Flight Choreography::StrafeRun(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double turnDegS, double dt)
	{
		(void)b;
		(void)dt;
		Flight out;
		auto a = KeelOf(t);
		double len = t->LengthKm;
		double h = len * 0.5;
		auto rel = u->Pos - t->Pos;
		double along = DVec3::Dot(rel, a);
		auto lat = rel - a * along;
		double latD = lat.Length();
		DVec3 tr, tu;
		t->Basis(tr, tu);
		// The flank it comes down: the one it is on (or the target's starboard, dead on the keel line).
		auto n = latD > 1e-6 ? lat / latD : tr;
		double standoff = StandoffShare * len + StandoffKm;
		double lead = EntryLeadKm + EntryLeadShare * len;
		double speed = cs::Math::Max(u->Vel.Length(), 0.05);

		if (!Strafing(u))
		{
			// A new pass, from the end it is nearer (BreakSide holds which: +1 the bow, -1 the stern).
			u->Maneuver = DogfightManeuver::StrafeBurn;
			u->ManeuverLeft = 40;
			u->BreakSide = along >= 0 ? 1 : -1;
		}
		double sg = u->BreakSide >= 0 ? 1 : -1;
		auto runDir = a * -sg;

		switch (u->Maneuver)
		{
			case DogfightManeuver::StrafeBurn:
			{
				auto entry = t->Pos + a * (sg * (h + lead)) + n * standoff;
				auto exit = t->Pos - a * (sg * (h + 0.4)) + n * standoff;
				double toEntry = (entry - u->Pos).Length();
				// Hard at a point off the near end, then onto the pass line itself as it gets there.
				out.Goal = toEntry > 2.5 ? (entry + runDir * 1.0) - u->Pos : exit - u->Pos;
				out.Boost = true;
				out.Throttle = 1;
				// Lined up: at the entry, or within the run's reach of the hull moving down it, with way enough on to coast
				// the length. Cut the drive. A run that never lines up (the hull swung away under it) starts again.
				bool alongRun = DVec3::Dot(u->Vel, runDir) > 0.5 * speed;
				bool beside = sg * along < h + lead && latD < standoff * 3 + 1;
				bool way = speed >= MinCoastKmS;
				if (way && (toEntry < cs::Math::Max(1.2, speed * 3) || (beside && alongRun)))
				{
					u->Maneuver = DogfightManeuver::StrafeCoast;
					u->ManeuverLeft = cs::Math::Min((2 * h + lead + 2) / speed + 2, MaxCoastS);
				}
				else if (u->ManeuverLeft <= 0) EndRun(u);
				break;
			}
			case DogfightManeuver::StrafeCoast:
			{
				// Free fall: nothing burning, the nose kept on the stretch of hull it is passing, so the guns walk down it.
				out.Coast = true;
				out.Throttle = 0;
				out.Goal = RakePoint(t, u->Pos, runDir) - u->Pos;
				// Never into the plating: too close in, it burns out square to the keel.
				if (latD < standoff * 0.45 && sg * along < h && sg * along > -h)
				{
					out.Coast = false;
					out.Throttle = 1;
					out.Boost = true;
					out.Goal = n;
				}
				bool past = sg * along < -(h + 0.3);
				bool away = DVec3::Dot(u->Vel, rel) > 0 && latD > standoff * 3 + 1.5;
				bool stalled = u->Vel.Length() < MinCoastKmS * 0.5;
				if (past || away || stalled || u->ManeuverLeft <= 0)
				{
					u->Maneuver = DogfightManeuver::StrafeFlip;
					u->ManeuverLeft = FlipSeconds + 180.0 / cs::Math::Max(turnDegS, 1.0) * 0.5 + Frac(u->Wander) * 1.5;
				}
				break;
			}
			default: // StrafeFlip
			{
				// Flip onto the retrograde and burn, swinging back out to the flank for the next pass, rolling as it goes.
				auto back = (t->Pos + n * (standoff + 2.0)) - u->Pos;
				auto retro = speed > 0.06 ? -u->Vel / speed : DVec3::Zero;
				out.Goal = retro + (back.LengthSquared() > 1e-12 ? back.Normalized() * 0.6 : DVec3::Zero);
				if (out.Goal.LengthSquared() < 1e-12) out.Goal = back;
				out.Boost = true;
				out.Throttle = 1;
				out.Roll = sg;
				if (u->ManeuverLeft <= 0) EndRun(u);
				break;
			}
		}
		return out;
	}

	DVec3 Choreography::BearingPoint(const cs::Ref<BattleUnit>& shooter, const cs::Ref<BattleUnit>& target)
	{
		if (target == nullptr) return DVec3::Zero;
		if (shooter == nullptr || !WeakPoints::IsSmallCraft(shooter) || !WeakPoints::IsCapital(target)) return target->Pos;
		// The point of the keel segment nearest the nose's line (closest points of a ray and a segment).
		auto a = KeelOf(target);
		double h = target->LengthKm * 0.5;
		auto p0 = target->Pos - a * h;
		auto seg = a * (2 * h);
		auto d = shooter->Forward;
		auto w = shooter->Pos - p0;
		double aa = DVec3::Dot(seg, seg), bb = DVec3::Dot(seg, d), cc = DVec3::Dot(d, d), dd = DVec3::Dot(seg, w), ee = DVec3::Dot(d, w);
		double den = aa * cc - bb * bb;
		double s = den > 1e-12 ? (dd * cc - bb * ee) / den : 0.5;
		s = Clamp(s, 0.0, 1.0);
		return p0 + seg * s;
	}

	// ---------------------------------------------------------------- fighters on fighters

	bool Choreography::FighterTurn(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double turnDegS, double dt, Flight& out)
	{
		DVec3 right, up;
		u->Basis(right, up);
		auto rel = t != nullptr ? t->Pos - u->Pos : DVec3::Zero;
		double dist = rel.Length();
		auto toT = dist > 1e-9 ? rel / dist : u->Forward;
		double turn = cs::Math::Max(turnDegS, 1.0);
		if (u->Maneuver != DogfightManeuver::Loop && u->Maneuver != DogfightManeuver::Roll && u->Maneuver != DogfightManeuver::Attack)
			return false;
		if (u->Maneuver == DogfightManeuver::Attack)
		{
			if (t == nullptr || u->ManeuverLeft > 0) return false;
			// Through the merge (the target close and behind it): pull up and over, the loop that brings it round on its tail.
			if (dist < 0.6 && DVec3::Dot(toT, u->Forward) < -0.2)
			{
				u->Maneuver = DogfightManeuver::Loop;
				u->ManeuverLeft = 180.0 / turn + 1.5;
			}
			// In the furball now and then: a rolling break, so a chaser on it has a harder time of it.
			else if (dist < 2.5 && b->Rng().NextDouble() < dt * 0.5)
			{
				u->Maneuver = DogfightManeuver::Roll;
				u->ManeuverLeft = 1.4 + Frac(u->Wander * 3.7);
				u->BreakSide = DVec3::Dot(toT, right) >= 0 ? 1 : -1;
			}
			else return false;
		}
		out.Throttle = 1;
		out.Boost = true;
		if (u->Maneuver == DogfightManeuver::Loop)
		{
			// The nose pulled up, round over the top, until it bears back on the target.
			out.Goal = up + u->Forward * 0.1;
			double full = 180.0 / turn + 1.5;
			if (u->ManeuverLeft <= 0 || (u->ManeuverLeft < full - 1.0 && DVec3::Dot(u->Forward, toT) > 0.6))
			{
				u->Maneuver = DogfightManeuver::Attack;
				u->ManeuverLeft = 3 + Frac(u->Wander * 5.3) * 3;
				return false;
			}
			return true;
		}
		// The rolling break: hard over one way, rolling the way it turns.
		out.Goal = right * u->BreakSide + up * 0.25 + u->Forward * 0.3;
		out.Roll = u->BreakSide;
		if (u->ManeuverLeft <= 0)
		{
			u->Maneuver = DogfightManeuver::Attack;
			u->ManeuverLeft = 3 + Frac(u->Wander * 2.9) * 4;
			return false;
		}
		return true;
	}

	void Choreography::Bank(const cs::Ref<BattleUnit>& u, double rollDegS, double dt)
	{
		auto f = u->Forward;
		if (f.LengthSquared() < 1e-12 || dt <= 0) return;
		auto level = WeakPatches::LevelRoof(f);
		auto roof = u->Roof.LengthSquared() > 1e-12 ? u->Roof : level;
		roof = roof - f * DVec3::Dot(roof, f);
		roof = roof.LengthSquared() > 1e-12 ? roof.Normalized() : level;
		if (cs::Math::Abs(rollDegS) > 1e-9)
			roof = PilotFlight::RotateAbout(roof, f, rollDegS * Astro::Deg2Rad * dt);
		else
		{
			// Into the turn: the roof leans the way the nose is swinging, steeper the harder it turns.
			auto swing = DVec3::Cross(u->TurnSpin, f);
			double rate = swing.Length();
			auto want = level;
			if (rate > 1e-6)
			{
				double bank = cs::Math::Min(1.2, rate * 2.2);
				want = level * cs::Math::Cos(bank) + SquareTo(f, swing) * cs::Math::Sin(bank);
				want = want - f * DVec3::Dot(want, f);
				want = want.LengthSquared() > 1e-12 ? want.Normalized() : level;
			}
			double angle = cs::Math::Acos(Clamp(DVec3::Dot(roof, want), -1.0, 1.0));
			if (angle > 1e-6)
			{
				auto axis = DVec3::Cross(roof, want);
				axis = axis.LengthSquared() > 1e-12 ? axis.Normalized() : f;
				roof = PilotFlight::RotateAbout(roof, axis, cs::Math::Min(angle, 3.14159265358979 * dt));
			}
		}
		roof = roof - f * DVec3::Dot(roof, f);
		if (roof.LengthSquared() > 1e-12) u->Roof = roof.Normalized();
	}

	// ---------------------------------------------------------------- light warships

	DVec3 Choreography::LightPass(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double rangeKm, double maxSpeed)
	{
		auto d = u->Pos - t->Pos;
		double dist = cs::Math::Max(d.Length(), 1e-6);
		auto dir = d / dist;
		// In close and out again on a slow cycle of its own, round the target at speed: a run across it, not a parked hull.
		double phase = b->Seconds * (TwoPi / 50.0) + u->Wander;
		double want = rangeKm * (0.7 + 0.35 * cs::Math::Sin(phase));
		auto spot = t->Pos + dir * want;
		return (spot - u->Pos) * 0.05 + Around(dir) * (maxSpeed * 0.85 * u->Circle)
			+ DVec3::UnitZ * (maxSpeed * 0.25 * cs::Math::Sin(phase * 0.7 + u->Wander * 2));
	}

	DVec3 Choreography::Weave(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, DVec3 face)
	{
		if (face.LengthSquared() < 1e-12) return face;
		auto f = face.Normalized();
		auto side = SquareTo(f, DVec3::Cross(f, DVec3::UnitZ));
		auto lift = DVec3::Cross(side, f);
		double t = b->Seconds;
		double yaw = 0.2 * cs::Math::Sin(t * 0.9 + u->Wander) + 0.08 * cs::Math::Sin(t * 2.3 + u->Wander * 1.7);
		double pitch = 0.1 * cs::Math::Sin(t * 0.6 + u->Wander * 2.1);
		return (f + side * yaw + lift * pitch).Normalized();
	}

	// ---------------------------------------------------------------- capitals

	DVec3 Choreography::Crossing(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double rangeKm, double maxSpeed, double dt)
	{
		auto d = u->Pos - t->Pos;
		double dist = cs::Math::Max(d.Length(), 1e-6);
		auto dir = d / dist;
		double hold = ComeAboutS * (0.8 + 0.4 * Frac(u->Wander * 7.3));
		if (u->Maneuver != DogfightManeuver::Crossing)
		{
			u->Maneuver = DogfightManeuver::Crossing;
			u->ManeuverLeft = hold;
		}
		u->ManeuverLeft -= dt;
		bool about = u->ManeuverLeft <= 0;
		// Every few seconds (its own slot of them), a look at how worn the face it shows the enemy is: past the mark, and
		// held long enough to be worth the turn, it comes about early to fight with the fresh side.
		double now = b->Seconds + Frac(u->Wander) * ComeAboutCheckS;
		bool look = cs::Math::Floor(now / ComeAboutCheckS) != cs::Math::Floor((now - dt) / ComeAboutCheckS);
		if (!about && look && u->ManeuverLeft < hold - 25 && WeakPatches::HasPatches(u->Combat))
			about = WeakPatches::Exposure(u, WeakPatches::IncomingFireOn(b, u)) > WornExposure;
		if (about)
		{
			u->Circle = u->Circle >= 0 ? -1 : 1;
			u->ManeuverLeft = hold;
		}
		auto spot = t->Pos + dir * rangeKm;
		return (spot - u->Pos) * 0.02 + Around(dir) * (maxSpeed * 0.8 * (u->Circle >= 0 ? 1 : -1));
	}

	DVec3 Choreography::Course(const cs::Ref<BattleUnit>& u)
	{
		// The AI's only: a hull at the player's helm is laid where the player lays it.
		if (!IsCapital(u) || u->PlayerControlled() || u->Maneuver != DogfightManeuver::Crossing || u->Target == nullptr) return DVec3::Zero;
		// Holding station in its flagship's formation, it steams as the flagship does, not on a course of its own.
		if (Broadsides::HoldsFormation(u)) return Choreography::Course(u->Leader);
		auto d = u->Pos - u->Target->Pos;
		if (d.LengthSquared() < 1e-12) return DVec3::Zero;
		return Around(d.Normalized()) * (u->Circle >= 0 ? 1 : -1);
	}
}
