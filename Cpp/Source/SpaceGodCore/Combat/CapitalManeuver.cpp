// US-T187: dynamic capital-ship combat (see Combat/CapitalManeuver.h).

#include "Combat/CapitalManeuver.h"

#include "Astro.h"
#include "Ships.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/Choreography.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/PilotFlight.h"
#include "Combat/ShieldEmitters.h"
#include "Combat/ShipCatalog.h"
#include "Combat/WeakPatches.h"

namespace SpaceGod::Core
{
	const char* ToString(ShieldFacing v)
	{
		switch (v)
		{
			case ShieldFacing::Fore: return "Fore";
			case ShieldFacing::Aft: return "Aft";
			case ShieldFacing::Port: return "Port";
			case ShieldFacing::Starboard: return "Starboard";
			case ShieldFacing::Dorsal: return "Dorsal";
			case ShieldFacing::Ventral: return "Ventral";
		}
		return "";
	}

	namespace
	{
		double Clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

		DVec3 ToLocal(DVec3 dir, DVec3 right, DVec3 up, DVec3 forward)
		{
			return DVec3(DVec3::Dot(dir, right), DVec3::Dot(dir, up), DVec3::Dot(dir, forward));
		}
	}

	// ------------------------------------------------------------ FacingShields

	ShieldFacing FacingShields::Strongest() const
	{
		int best = 0;
		for (int i = 1; i < 6; i++)
			if (Strength[i] > Strength[best] + 1e-12) best = i;
		return static_cast<ShieldFacing>(best);
	}

	ShieldFacing FacingShields::Weakest() const
	{
		int worst = 0;
		for (int i = 1; i < 6; i++)
			if (Strength[i] < Strength[worst] - 1e-12) worst = i;
		return static_cast<ShieldFacing>(worst);
	}

	double FacingShields::Toward(DVec3 local) const
	{
		if (local.LengthSquared() < 1e-18) return 0;
		auto d = local.Normalized();
		// A unit direction's squared components sum to one: each face counts by the square of how squarely it looks that way.
		double sum = 0, weights = 0;
		for (int i = 0; i < 6; i++)
		{
			double k = DVec3::Dot(CapitalManeuver::NormalOf(static_cast<ShieldFacing>(i)), d);
			if (k <= 0) continue;
			k *= k;
			sum += k * Strength[i];
			weights += k;
		}
		return weights > 0 ? sum / weights : 0;
	}

	// ------------------------------------------------------------ who maneuvers

	bool CapitalManeuver::Flies(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Combat != nullptr && !u->PlayerControlled() && Choreography::IsCapital(u) && Broadsides::Commands(u);
	}

	// ------------------------------------------------------------ the facings

	ShieldFacing CapitalManeuver::FacingOf(DVec3 local)
	{
		double ax = cs::Math::Abs(local.X), ay = cs::Math::Abs(local.Y), az = cs::Math::Abs(local.Z);
		if (az >= ax && az >= ay) return local.Z >= 0 ? ShieldFacing::Fore : ShieldFacing::Aft;
		if (ax >= ay) return local.X >= 0 ? ShieldFacing::Starboard : ShieldFacing::Port;
		return local.Y >= 0 ? ShieldFacing::Dorsal : ShieldFacing::Ventral;
	}

	DVec3 CapitalManeuver::NormalOf(ShieldFacing f)
	{
		switch (f)
		{
			case ShieldFacing::Fore: return DVec3(0, 0, 1);
			case ShieldFacing::Aft: return DVec3(0, 0, -1);
			case ShieldFacing::Port: return DVec3(-1, 0, 0);
			case ShieldFacing::Starboard: return DVec3(1, 0, 0);
			case ShieldFacing::Dorsal: return DVec3(0, 1, 0);
			case ShieldFacing::Ventral: return DVec3(0, -1, 0);
		}
		return DVec3(0, 0, 1);
	}

	FacingShields CapitalManeuver::Facings(const cs::Ref<ShipCombat>& c)
	{
		FacingShields out;
		if (c == nullptr || c->Spec == nullptr) return out;
		// The capsule hull (ship lengths) as WeakPatches samples it: its beam's radius about the keel, its straight length.
		double r = c->Spec->BeamFraction * 0.5, core = cs::Math::Max(0.0, 0.5 - r);
		constexpr double Off = 0.5235987755982988; // 30 degrees round the keel either side of a face's middle
		for (int i = 0; i < 6; i++)
		{
			auto f = static_cast<ShieldFacing>(i);
			auto n = NormalOf(f);
			double sum = 0;
			int count = 0;
			if (f == ShieldFacing::Fore || f == ShieldFacing::Aft)
			{
				// The rounded end: its tip and a ring part-way down it.
				double sign = n.Z;
				auto end = DVec3(0, 0, sign * core);
				sum += ShieldEmitters::PatchStrength(c, end + n * r);
				count++;
				double s = cs::Math::Sin(0.6), k = cs::Math::Cos(0.6);
				for (int q = 0; q < 4; q++)
				{
					double a = q * 1.5707963267948966;
					auto d = DVec3(cs::Math::Cos(a) * s, cs::Math::Sin(a) * s, sign * k);
					sum += ShieldEmitters::PatchStrength(c, end + d * r);
					count++;
				}
			}
			else
			{
				// The side: three stations along the straight length, each the face's middle and either side of it.
				double base = cs::Math::Atan2(n.Y, n.X);
				for (int z = -1; z <= 1; z++)
					for (int q = -1; q <= 1; q++)
					{
						double a = base + q * Off;
						auto at = DVec3(cs::Math::Cos(a) * r, cs::Math::Sin(a) * r, z * core * 0.6);
						sum += ShieldEmitters::PatchStrength(c, at);
						count++;
					}
			}
			out.Strength[i] = count > 0 ? Clamp(sum / count, 0.0, 1.0) : 1;
		}
		return out;
	}

	DVec3 CapitalManeuver::ThreatDir(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		DVec3 sum = DVec3::Zero;
		for (const auto& f : WeakPatches::IncomingFireOn(b, u)) sum = sum + f.Dir * f.Weight;
		return sum.LengthSquared() > 1e-18 ? sum.Normalized() : DVec3::Zero;
	}

	ShieldFacing CapitalManeuver::FacingToward(const cs::Ref<BattleUnit>& u, DVec3 dir)
	{
		DVec3 right, up;
		u->Basis(right, up);
		return FacingOf(ToLocal(dir, right, up, u->Forward));
	}

	bool CapitalManeuver::BroadsideWorn(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		if (!Flies(u)) return false;
		auto threat = ThreatDir(b, u);
		if (threat.LengthSquared() < 1e-18) return false;
		DVec3 right, up;
		u->Basis(right, up);
		// The broadside it fights with is the side the threat lies off, whatever else of the hull also faces it.
		bool starboard = DVec3::Dot(threat, right) >= 0;
		auto faces = Facings(u->Combat);
		double engaged = faces.Of(starboard ? ShieldFacing::Starboard : ShieldFacing::Port);
		double other = faces.Of(starboard ? ShieldFacing::Port : ShieldFacing::Starboard);
		return engaged < DepletedAt && other >= engaged + FreshMargin;
	}

	double CapitalManeuver::PresentBonus(const FacingShields& faces, DVec3 threat, DVec3 forward, DVec3 up)
	{
		if (threat.LengthSquared() < 1e-18 || forward.LengthSquared() < 1e-18) return 0;
		auto right = DVec3::Cross(forward, up);
		if (right.LengthSquared() < 1e-18) return 0;
		right = right.Normalized();
		return PresentWeight * faces.Toward(ToLocal(threat, right, up, forward));
	}

	// ------------------------------------------------------------ the run

	double CapitalManeuver::Closing(double distKm, double rangeKm)
	{
		if (rangeKm <= 0 || distKm <= rangeKm) return 0;
		double t = Clamp((distKm - rangeKm) / (rangeKm * (CloseFromShare - 1)), 0.0, 1.0);
		return t * t * (3 - 2 * t);
	}

	// ------------------------------------------------------------ the roll

	double CapitalManeuver::RollRateDegS(const cs::Ref<BattleUnit>& u)
	{
		double turn = u->Combat->Spec->TurnRateDegS;
		double factor = DefaultRollFactor;
		HullHandling h;
		if (u->Ship != nullptr && u->Ship->Spec != nullptr && ShipCatalog::TryHandling(u->Ship->Spec->Class, h) && h.TurnRateDegS > 0)
			factor = h.RollRateDegS / h.TurnRateDegS;
		return cs::Math::Max(0.0, turn * factor);
	}

	double CapitalManeuver::RollAlpha(const cs::Ref<BattleUnit>& u)
	{
		double rate = RollRateDegS(u) * Astro::Deg2Rad;
		if (rate <= 0) return 0;
		double authority = HullDynamics::Of(u->Combat->Spec).RollAngularAccel;
		return Clamp(authority, rate / MaxSpinUpS, rate / MinSpinUpS);
	}

	bool CapitalManeuver::Roll(const cs::Ref<BattleUnit>& u, DVec3 roof, double dt)
	{
		if (!Flies(u)) return false;
		auto cmd = Broadsides::Of(u);
		if (cmd == nullptr) return false;
		auto f = u->Forward;
		if (f.LengthSquared() < 1e-18 || dt <= 0) return true;
		f = f.Normalized();
		auto cur = u->Roof.LengthSquared() > 1e-12 ? u->Roof : WeakPatches::LevelRoof(f);
		cur = cur - f * DVec3::Dot(cur, f);
		auto want = roof - f * DVec3::Dot(roof, f);
		if (want.LengthSquared() < 1e-18) return true;
		want = want.Normalized();
		if (cur.LengthSquared() < 1e-18)
		{
			u->Roof = want;
			cmd->RollSpin = 0;
			return true;
		}
		cur = cur.Normalized();
		double rate = RollRateDegS(u) * Astro::Deg2Rad;
		double alpha = RollAlpha(u);
		if (rate <= 0 || alpha <= 0)
		{
			cmd->RollSpin = 0;
			return true;
		}
		double angle = WeakPatches::RollAngle(cur, want, f);
		double maxDw = alpha * dt;
		// The swing it wants: toward the roll, no faster than it can still brake to a stop on it.
		double target = cs::Math::Sign(angle) * cs::Math::Min(rate, cs::Math::Sqrt(2 * alpha * cs::Math::Abs(angle)));
		double spin = cmd->RollSpin;
		spin += Clamp(target - spin, -maxDw, maxDw);
		// Arriving with little swing left to kill: settle on the roll rather than rock about it.
		if (cs::Math::Abs(angle) <= cs::Math::Abs(spin) * dt + 1e-9 && cs::Math::Abs(spin) <= 3 * maxDw + 1e-12)
		{
			u->Roof = want;
			cmd->RollSpin = 0;
			return true;
		}
		u->Roof = PilotFlight::RotateAbout(cur, f, spin * dt).Normalized();
		cmd->RollSpin = spin;
		return true;
	}
}
