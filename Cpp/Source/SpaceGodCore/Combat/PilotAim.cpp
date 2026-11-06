#include "Combat/PilotAim.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	StringAim PilotAim::StringFor(PilotControl control, double hullTurnRateDegS, bool drifting)
	{
		StringAim s;
		if (drifting || control == PilotControl::FreeAim) return s;
		s.MaxLeadDeg = HeavyMaxLeadDeg;
		s.LookRateDegS = cs::Math::Max(1.0, hullTurnRateDegS * HeavyLookRateFactor);
		return s;
	}

	void PilotAim::Look(DVec3& aim, DVec3& viewUp, double& pendingRight, double& pendingUp, double rightRad, double upRad,
		DVec3 hullForward, DVec3 hullUp, double dt, const StringAim& string)
	{
		double maxLeadRad = cs::Math::Min(180.0, string.MaxLeadDeg) * Astro::Deg2Rad;
		// The mouse's travel, paid out no faster than the look rate; what is left waits for the next frame, never more
		// than the string's own length of it.
		pendingRight += rightRad;
		pendingUp += upRad;
		double owed = cs::Math::Sqrt(pendingRight * pendingRight + pendingUp * pendingUp);
		if (owed > maxLeadRad)
		{
			pendingRight *= maxLeadRad / owed;
			pendingUp *= maxLeadRad / owed;
			owed = maxLeadRad;
		}
		double take = 1;
		if (string.LookRateDegS > 0 && owed > 0)
		{
			double step = string.LookRateDegS * Astro::Deg2Rad * cs::Math::Max(0.0, dt);
			if (owed > step) take = step / owed;
		}
		rightRad = pendingRight * take;
		upRad = pendingUp * take;
		pendingRight -= rightRad;
		pendingUp -= upRad;

		viewUp = Square(viewUp, aim, hullUp);
		if (rightRad != 0)
		{
			aim = PilotFlight::RotateAbout(aim, viewUp, -rightRad).Normalized();
		}
		if (upRad != 0)
		{
			auto right = DVec3::Cross(aim, viewUp).Normalized();
			aim = PilotFlight::RotateAbout(aim, right, upRad).Normalized();
			viewUp = PilotFlight::RotateAbout(viewUp, right, upRad);
		}
		// Pulled taut against the nose: the hull drags the aim along (and what is owed against the pull is dropped).
		auto clamped = PilotFlight::ClampAim(aim, hullForward, maxLeadRad);
		if (DVec3::Dot(clamped, aim) < 1 - 1e-12) pendingRight = pendingUp = 0;
		aim = clamped;
		viewUp = Square(viewUp, aim, hullUp);

		// The hull's roof seen square to the line of sight: well defined unless the hull is pitched
		// nearly across it, when the view keeps its own roof.
		auto roof = hullUp - aim * DVec3::Dot(hullUp, aim);
		if (roof.LengthSquared() > 0.25 && dt > 0)
		{
			double k = 1 - cs::Math::Exp(-dt * ViewRollFollowHz);
			viewUp = Square(viewUp + (roof.Normalized() - viewUp) * k, aim, hullUp);
		}
	}

	DVec3 PilotAim::Nose(DVec3 aim, DVec3 hullForward, DVec3 hullUp, double rightRad, double upRad, double leadRad)
	{
		auto right = DVec3::Cross(hullForward, hullUp).Normalized();
		aim = PilotFlight::RotateAbout(aim, hullUp, -rightRad);
		aim = PilotFlight::RotateAbout(aim, right, upRad).Normalized();
		return PilotFlight::ClampAim(aim, hullForward, leadRad);
	}

	DVec3 PilotAim::Helm(DVec3 hullForward, DVec3 hullUp, double yaw, double pitch, double leadRad)
	{
		yaw = cs::Math::Max(-1.0, cs::Math::Min(1.0, yaw));
		pitch = cs::Math::Max(-1.0, cs::Math::Min(1.0, pitch));
		double mag = cs::Math::Sqrt(yaw * yaw + pitch * pitch);
		if (mag < 1e-9) return hullForward;
		auto right = DVec3::Cross(hullForward, hullUp).Normalized();
		// Turn about the axis square to the asked direction (right·yaw + up·pitch), by the lead scaled to the stick.
		auto toward = (right * yaw + hullUp * pitch) / mag;
		auto axis = DVec3::Cross(hullForward, toward).Normalized();
		return PilotFlight::RotateAbout(hullForward, axis, leadRad * cs::Math::Min(1.0, mag)).Normalized();
	}

	DVec3 PilotAim::Square(DVec3 up, DVec3 forward, DVec3 fallback)
	{
		auto u = up - forward * DVec3::Dot(up, forward);
		if (u.LengthSquared() > 1e-6) return u.Normalized();
		u = fallback - forward * DVec3::Dot(fallback, forward);
		return u.LengthSquared() > 1e-6 ? u.Normalized() : PilotState::LevelUp(forward);
	}
}
