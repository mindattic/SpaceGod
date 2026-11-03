// Port of Unity.SpaceGod Assets/UnitySpace/Core/Infrastructure/Satellite.cs (see docs/MIGRATION.md).

#include "Infrastructure/Satellite.h"

#include "Astro.h"
#include "BodyDefinition.h"
#include "Combat/PilotFlight.h"
#include "Infrastructure/Infrastructure.h"

namespace SpaceGod::Core
{
	Satellite::Satellite(cs::Ref<SpaceGod::Core::Constellation> constellation, cs::Ref<BodyDefinition> body, int index, double jd)
	{
		Constellation = constellation;
		Body = body;
		Index = index;
		ReturnToSlot(jd);
	}

	cs::String Satellite::Name() const { return Constellation->NameOf(Index); }

	double Satellite::SlotErrorKm() const
	{
		DVec3 slot, discard;
		Constellation->SlotAt(Body, Index, Jd, slot, discard);
		return (OffsetKm - slot).Length();
	}

	void Satellite::WorkingAttitude(DVec3 offsetKm, DVec3 velocityKmS, DVec3& forward, DVec3& up)
	{
		forward = offsetKm.LengthSquared() > 1e-18 ? -offsetKm.Normalized() : -DVec3::UnitX;
		auto along = velocityKmS - forward * DVec3::Dot(velocityKmS, forward);
		up = along.LengthSquared() > 1e-18 ? along.Normalized() : PilotState::LevelUp(forward);
	}

	void Satellite::ReturnToSlot(double jd)
	{
		Jd = jd;
		Constellation->SlotAt(Body, Index, jd, OffsetKm, VelocityKmS);
		WorkingAttitude(OffsetKm, VelocityKmS, Forward, Up);
		LastThrustKmS2 = 0;
		OnStation = !Held();
	}

	void Satellite::AdvanceTo(double jd)
	{
		if (!(jd > Jd)) return;
		if (OnStation && !Held())
		{
			ReturnToSlot(jd);
			return;
		}
		double total = (jd - Jd) * Astro::SecondsPerDay;
		if (!Held() && total > MaxCatchUpSeconds)
		{
			ReturnToSlot(jd);
			return;
		}
		int steps = cs::ToInt(cs::Math::Ceiling(total / MaxStepSeconds));
		// A held satellite is flown at the player's pace (real time): a long gap is only ever a hitch, taken in coarser steps.
		steps = cs::Math::Max(1, cs::Math::Min(steps, cs::ToInt(MaxCatchUpSeconds / MaxStepSeconds)));
		double h = total / steps;
		double start = Jd;
		for (int i = 0; i < steps; i++)
		{
			Step(start + i * h / Astro::SecondsPerDay, h);
			if (!Held() && Settled(start + (i + 1) * h / Astro::SecondsPerDay))
			{
				ReturnToSlot(jd);
				return;
			}
		}
		Jd = jd;
	}

	DVec3 Satellite::Gravity(DVec3 r) const
	{
		double r2 = r.LengthSquared();
		if (r2 < 1e-12) return DVec3::Zero;
		return r * (-Body->Gm / (r2 * cs::Math::Sqrt(r2)));
	}

	void Satellite::Step(double jd, double h)
	{
		auto g = Gravity(OffsetKm);
		DVec3 thrust;
		if (Held())
		{
			auto c = Command;
			if (c.Aim.LengthSquared() > 1e-12) Slew(c.Aim.Normalized(), h);
			Roll(Clamp(c.Roll) * RollDegS * Astro::Deg2Rad * h);
			thrust = (Forward * Clamp(c.Throttle) + Right() * Clamp(c.StrafeRight) + Up * Clamp(c.StrafeUp)) * ThrustKmS2;
			if (thrust.Length() > ThrustKmS2) thrust = thrust.Normalized() * ThrustKmS2;
		}
		else
		{
			thrust = KeepStation(jd, g);
			DVec3 f, u;
			WorkingAttitude(OffsetKm, VelocityKmS, f, u);
			Slew(f, h);
			RollToward(u, h);
		}
		LastThrustKmS2 = thrust.Length();
		VelocityKmS += (g + thrust) * h;
		OffsetKm += VelocityKmS * h;
	}

	DVec3 Satellite::KeepStation(double jd, DVec3 gravity) const
	{
		DVec3 slot, slotVelocity;
		Constellation->SlotAt(Body, Index, jd, slot, slotVelocity);
		auto trim = Gravity(slot) - gravity;
		auto error = OffsetKm - slot;
		double distance = error.Length();
		auto wanted = slotVelocity;
		if (distance > 1e-12)
			wanted -= error / distance * cs::Math::Min(cs::Math::Sqrt(ThrustKmS2 * distance), ClosingRate * distance);
		auto chase = (wanted - VelocityKmS) * ChaseGain;
		if (chase.Length() > ThrustKmS2) chase = chase.Normalized() * ThrustKmS2;
		return trim + chase;
	}

	bool Satellite::Settled(double jd) const
	{
		DVec3 slot, slotVelocity;
		Constellation->SlotAt(Body, Index, jd, slot, slotVelocity);
		if ((OffsetKm - slot).Length() > StationKm || (VelocityKmS - slotVelocity).Length() > StationKmS) return false;
		DVec3 f, u;
		WorkingAttitude(slot, slotVelocity, f, u);
		return AngleDeg(Forward, f) < StationDeg && AngleDeg(Up, u) < StationDeg;
	}

	void Satellite::Slew(DVec3 aim, double h)
	{
		double angle = AngleDeg(Forward, aim);
		if (angle < 1e-9) return;
		double t = cs::Math::Min(1.0, SlewDegS * h / angle);
		Forward = DVec3::TurnStep(Forward, aim, t);
		auto roof = Up - Forward * DVec3::Dot(Up, Forward);
		Up = roof.LengthSquared() > 1e-12 ? roof.Normalized() : PilotState::LevelUp(Forward);
	}

	void Satellite::Roll(double radians)
	{
		if (cs::Math::Abs(radians) < 1e-15) return;
		Up = PilotFlight::RotateAbout(Up, Forward, radians).Normalized();
	}

	void Satellite::RollToward(DVec3 up, double h)
	{
		auto target = up - Forward * DVec3::Dot(up, Forward);
		if (target.LengthSquared() < 1e-12) return;
		target = target.Normalized();
		double angle = cs::Math::Atan2(DVec3::Dot(DVec3::Cross(Up, target), Forward), DVec3::Dot(Up, target));
		double maxRoll = RollDegS * Astro::Deg2Rad * h;
		Roll(cs::Math::Max(-maxRoll, cs::Math::Min(maxRoll, angle)));
	}

	bool Satellite::Covers(DVec3 pointKm) const
	{
		auto to = pointKm - OffsetKm;
		double d = to.Length();
		if (d < 1e-9) return true;
		if (AngleDeg(Forward, to / d) > BeamHalfDeg) return false;
		// The closest the line of sight comes to the body's centre, short of the point itself.
		double along = cs::Math::Max(0.0, cs::Math::Min(d, -DVec3::Dot(OffsetKm, to / d)));
		auto nearest = OffsetKm + to / d * along;
		return nearest.Length() >= Body->RadiusKm * 0.999;
	}

	double Satellite::AngleDeg(DVec3 a, DVec3 b)
	{
		return cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(a, b)))) * Astro::Rad2Deg;
	}
}
