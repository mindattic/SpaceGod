#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/FlightModels.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	using cs::Math::Max;
	using cs::Math::Min;

	HullDynamics HullDynamics::Of(const cs::Ref<CombatSpec>& spec)
	{
		double m = spec->MassKt;
		if (m <= 0) return HullDynamics{};
		double iPy = spec->PitchYawInertia(), iRoll = spec->RollInertia();
		return HullDynamics(
			m,
			spec->MainThrustMN / (m * MetresPerKm),
			spec->RetroThrustMN / (m * MetresPerKm),
			spec->LateralThrustMN / (m * MetresPerKm),
			spec->VerticalThrustMN / (m * MetresPerKm),
			iPy > 0 ? spec->PitchYawTorqueMNKm / (iPy * MetresPerKm) : 0,
			iRoll > 0 ? spec->RollTorqueMNKm / (iRoll * MetresPerKm) : 0);
	}

	DVec3 PilotState::Right() const { return DVec3::Cross(Forward, Up).Normalized(); }

	double PilotState::TurnRateDegS() const { return (AngularVelocity - Forward * DVec3::Dot(AngularVelocity, Forward)).Length() * Astro::Rad2Deg; }

	double PilotState::RollRateDegS() const { return DVec3::Dot(AngularVelocity, Forward) * Astro::Rad2Deg; }

	PilotState PilotState::At(DVec3 pos, DVec3 vel, DVec3 forward)
	{
		auto f = forward.LengthSquared() > 1e-18 ? forward.Normalized() : DVec3::UnitX;
		PilotState s;
		s.Pos = pos;
		s.Vel = vel;
		s.Forward = f;
		s.Up = LevelUp(f);
		return s;
	}

	DVec3 PilotState::LevelUp(DVec3 forward)
	{
		auto reference = DVec3::Cross(forward, DVec3::UnitZ).LengthSquared() > 1e-12 ? DVec3::UnitZ : DVec3::UnitX;
		return (reference - forward * DVec3::Dot(reference, forward)).Normalized();
	}

	void PilotFlight::Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		if (dt <= 0) return;
		auto forwardBeforeTurn = s.Forward;
		Rotate(s, hull, tuning, input, dt);
		double driveMult = DriveMultiplier(s, combat, tuning, input, forwardBeforeTurn, dt);

		auto thrust = input.FlightAssistOff
			? RawThrust(s, hull, input, driveMult)
			: AssistThrust(s, hull, tuning, input, gravityKmS2, driveMult, dt, StrafeSpeedFraction, hull.LateralAccel, hull.VerticalAccel);
		Integrate(s, thrust, gravityKmS2, dt);
	}

	double PilotFlight::DriveMultiplier(PilotState& s, const cs::Ref<ShipCombat>& combat, const PilotTuning& tuning, const PilotInput& input, DVec3 forwardBeforeTurn, double dt)
	{
		// Holding boost only ever drains it (never recharges, even once empty); recharge only
		// resumes once released — otherwise holding it down at empty would flicker on and off.
		bool wantsBoost = input.Boost && tuning.BoostDurationS > 0;
		bool boosting = wantsBoost && combat->BoostFraction > 0;
		if (tuning.BoostDurationS > 0)
		{
			double rate = 1 / tuning.BoostDurationS;
			combat->BoostFraction = wantsBoost
				? Max(0.0, combat->BoostFraction - rate * dt)
				: Min(1.0, combat->BoostFraction + rate * RechargeFraction * dt);
		}
		return (boosting ? tuning.BoostSpeedMultiplier : 1)
			* StepCruise(s.Cruise, input, forwardBeforeTurn, combat, tuning, dt);
	}

	DVec3 PilotFlight::RawThrust(const PilotState& s, const HullDynamics& hull, const PilotInput& input, double driveMult)
	{
		double throttle = Clamp(input.Throttle), strafeR = Clamp(input.StrafeRight), strafeU = Clamp(input.StrafeUp);
		double main = hull.MainAccel * driveMult;
		return s.Forward * (throttle >= 0 ? throttle * main : throttle * hull.RetroAccel)
			+ s.Right() * (strafeR * hull.LateralAccel)
			+ s.Up * (strafeU * hull.VerticalAccel);
	}

	DVec3 PilotFlight::AssistThrust(const PilotState& s, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double driveMult, double dt, double strafeSpeedFraction, double lateralCap, double verticalCap)
	{
		auto f = s.Forward;
		auto u = s.Up;
		auto r = s.Right();
		double throttle = Clamp(input.Throttle), strafeR = Clamp(input.StrafeRight), strafeU = Clamp(input.StrafeUp);
		double main = hull.MainAccel * driveMult;
		double setPoint = tuning.MaxSpeed * driveMult;
		auto commanded = f * (throttle * setPoint)
			+ r * (strafeR * setPoint * strafeSpeedFraction)
			+ u * (strafeU * setPoint * strafeSpeedFraction);
		auto need = (commanded - s.Vel) / dt - gravityKmS2;
		return f * Max(-hull.RetroAccel, Min(main, DVec3::Dot(need, f)))
			+ r * Limit(DVec3::Dot(need, r), lateralCap)
			+ u * Limit(DVec3::Dot(need, u), verticalCap);
	}

	void PilotFlight::Integrate(PilotState& s, DVec3 thrustKmS2, DVec3 gravityKmS2, double dt)
	{
		s.Vel = s.Vel + (thrustKmS2 + gravityKmS2) * dt;
		s.Pos = s.Pos + s.Vel * dt;
	}

	DVec3 PilotFlight::Gravity(DVec3 fromBodyKm, double gmKm3S2, double radiusKm)
	{
		double d = fromBodyKm.Length();
		double reach = radiusKm * GravityReachRadii;
		if (gmKm3S2 <= 0 || radiusKm <= 0 || d >= reach || d < 1e-9) return DVec3::Zero;
		double rr = Max(d, radiusKm);
		double g = gmKm3S2 / (rr * rr);
		double fade = Min(1.0, (reach - d) / (reach * GravityFadeFraction));
		return fromBodyKm * (-g * fade / d);
	}

	bool PilotFlight::KeepAboveSurface(PilotState& s, DVec3 bodyKm, double floorKm)
	{
		double discard = 0;
		return KeepAboveSurface(s, bodyKm, floorKm, discard);
	}

	bool PilotFlight::KeepAboveSurface(PilotState& s, DVec3 bodyKm, double floorKm, double& sinkKmS)
	{
		sinkKmS = 0;
		auto rel = s.Pos - bodyKm;
		double d = rel.Length();
		if (floorKm <= 0 || d >= floorKm) return false;
		auto n = d > 1e-9 ? rel / d : s.Up;
		s.Pos = bodyKm + n * floorKm;
		double down = DVec3::Dot(s.Vel, n);
		if (down < 0)
		{
			s.Vel = s.Vel - n * down;
			sinkKmS = -down;
		}
		return true;
	}

	double PilotFlight::StepCruise(PilotCruiseState& cruise, const PilotInput& input, DVec3 forwardBeforeTurn, const cs::Ref<ShipCombat>& combat, const PilotTuning& tuning, double dt)
	{
		bool tookDamage = cruise.NoteHullAndCheckDamage(combat->Hull);
		if (tuning.CruiseSpeedMultiplier <= 1 || tuning.CruiseRampS <= 0)
		{
			cruise.RampT = 0;
			return 1;
		}

		bool headingStraight = input.DesiredForward.LengthSquared() < 1e-12
			|| DVec3::Dot(forwardBeforeTurn, input.DesiredForward.Normalized()) > cs::Math::Cos(CruiseMaxHeadingDriftDeg * Astro::Deg2Rad);
		bool eligible = input.Cruise && !input.Boost && !input.InCombat && !tookDamage
			&& input.Throttle >= CruiseMinThrottle && headingStraight;

		if (!eligible)
		{
			cruise.RampT = 0;
			return 1;
		}
		cruise.RampT = Min(1.0, cruise.RampT + dt / tuning.CruiseRampS);
		return 1 + (tuning.CruiseSpeedMultiplier - 1) * cruise.RampT;
	}

	void PilotFlight::Rotate(PilotState& s, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, double dt)
	{
		Rotate(s, hull, tuning.TurnRateDegS, tuning.RollRateDegS(), input, dt);
	}

	void PilotFlight::Rotate(PilotState& s, const HullDynamics& hull, double turnRateDegS, double rollRateDegS, const PilotInput& input, double dt)
	{
		auto f = s.Forward;
		auto w = s.AngularVelocity;
		double wRoll = DVec3::Dot(w, f);
		auto wTurn = w - f * wRoll;
		double alpha = hull.PitchYawAngularAccel;
		double maxDw = alpha * dt;

		bool steering = input.DesiredForward.LengthSquared() > 1e-12;
		auto want = steering ? input.DesiredForward.Normalized() : f;
		double angle = 0;
		if (steering || !input.FlightAssistOff)
		{
			auto target = DVec3::Zero;
			if (steering)
			{
				auto axis = DVec3::Cross(f, want);
				double sin = axis.Length(), cos = DVec3::Dot(f, want);
				angle = cs::Math::Atan2(sin, cos);
				// Dead astern: any axis across the nose will do; the roof is as good as any.
				if (sin < 1e-9) axis = cos > 0 ? DVec3::Zero : s.Up;
				if (axis.LengthSquared() > 0)
				{
					// Fast enough to get there, slow enough to stop on it: v² = 2·α·θ.
					double rate = Min(turnRateDegS * Astro::Deg2Rad, cs::Math::Sqrt(2 * alpha * angle));
					target = axis.Normalized() * rate;
				}
			}
			auto dw = target - wTurn;
			if (dw.Length() > maxDw) dw = dw.Normalized() * maxDw;
			wTurn = wTurn + dw;
		}

		// Roll is about the nose; positive input rolls clockwise from behind, a positive spin about it.
		double rollAlpha = hull.RollAngularAccel;
		double rollIn = Clamp(input.Roll);
		if (input.FlightAssistOff)
		{
			wRoll += rollIn * rollAlpha * dt;
		}
		else
		{
			double targetRoll = rollIn * rollRateDegS * Astro::Deg2Rad;
			wRoll += Limit(targetRoll - wRoll, rollAlpha * dt);
		}

		s.AngularVelocity = wTurn + f * wRoll;
		double spin = s.AngularVelocity.Length() * dt;
		if (spin > 0)
		{
			auto k = s.AngularVelocity.Normalized();
			s.Forward = RotateAbout(s.Forward, k, spin).Normalized();
			s.Up = RotateAbout(s.Up, k, spin);
		}

		// Arriving on the heading this frame, with little enough swing left to kill in a frame or
		// so: settle exactly on it rather than overshooting by a hair and hunting back and forth.
		double turnRate = wTurn.Length();
		if (steering && angle > 0 && angle <= turnRate * dt + 1e-12 && turnRate <= 3 * maxDw + 1e-12)
		{
			s.Forward = want;
			s.AngularVelocity = want * DVec3::Dot(s.AngularVelocity, want);
		}

		auto up = s.Up - s.Forward * DVec3::Dot(s.Up, s.Forward);
		s.Up = up.LengthSquared() > 1e-18 ? up.Normalized() : PilotState::LevelUp(s.Forward);
	}

	double PilotFlight::AimLeadRad(const HullDynamics& hull, const PilotTuning& tuning)
	{
		double rate = tuning.TurnRateDegS * Astro::Deg2Rad;
		double brake = hull.PitchYawAngularAccel > 0 ? rate * rate / (2 * hull.PitchYawAngularAccel) : 0;
		return Min(cs::Math::PI, Max(MinAimLeadDeg * Astro::Deg2Rad, brake));
	}

	DVec3 PilotFlight::ClampAim(DVec3 aim, DVec3 forward, double maxLeadRad)
	{
		auto axis = DVec3::Cross(forward, aim);
		double sin = axis.Length(), cos = DVec3::Dot(forward, aim);
		double angle = cs::Math::Atan2(sin, cos);
		if (angle <= maxLeadRad) return aim;
		// Dead astern: any axis across the nose will do.
		if (sin < 1e-9) axis = PilotState::LevelUp(forward);
		return RotateAbout(forward, axis.Normalized(), maxLeadRad).Normalized();
	}

	DVec3 PilotFlight::RotateAbout(DVec3 v, DVec3 k, double angle)
	{
		double c = cs::Math::Cos(angle), sn = cs::Math::Sin(angle);
		return v * c + DVec3::Cross(k, v) * sn + k * (DVec3::Dot(k, v) * (1 - c));
	}

	double PilotFlight::Clamp(double x) { return Max(-1.0, Min(1.0, x)); }

	double PilotFlight::Limit(double x, double max) { return Max(-max, Min(max, x)); }
}
