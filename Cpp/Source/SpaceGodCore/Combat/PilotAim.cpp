#include "Combat/PilotAim.h"
#include "Combat/PilotFlight.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	void PilotAim::Look(DVec3& aim, DVec3& viewUp, double rightRad, double upRad, DVec3 hullForward, DVec3 hullUp, double dt)
	{
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
		aim = PilotFlight::ClampAim(aim, hullForward, FreeAimMaxLeadDeg * Astro::Deg2Rad);
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

	DVec3 PilotAim::Stick(double& stickX, double& stickY, double rightDeg, double upDeg, DVec3 hullForward, DVec3 hullUp, double leadRad, double dt)
	{
		if (dt > 0)
		{
			double keep = cs::Math::Exp(-StickReturnHz * dt);
			stickX *= keep;
			stickY *= keep;
		}
		stickX += rightDeg / StickFullDeg;
		stickY += upDeg / StickFullDeg;
		double mag = cs::Math::Sqrt(stickX * stickX + stickY * stickY);
		if (mag > 1)
		{
			stickX /= mag;
			stickY /= mag;
		}
		return Helm(hullForward, hullUp, stickX, stickY, leadRad);
	}

	double PilotAim::RollOffLevelDeg(DVec3 hullForward, DVec3 hullUp)
	{
		auto levelUp = PilotState::LevelUp(hullForward);
		auto levelRight = DVec3::Cross(hullForward, levelUp);
		return cs::Math::Atan2(DVec3::Dot(hullUp, levelRight), DVec3::Dot(hullUp, levelUp)) * Astro::Rad2Deg;
	}

	double PilotAim::LevelRoll(DVec3 hullForward, DVec3 hullUp, double stickX)
	{
		// No horizon to roll against with the nose near the poles: fade the auto-level out there (as Soulslike's does).
		double horizon = cs::Math::Max(0.0, cs::Math::Min(1.0, (0.97 - cs::Math::Abs(hullForward.Z)) / 0.25));
		if (horizon <= 0) return 0;
		double want = cs::Math::Max(-1.0, cs::Math::Min(1.0, stickX)) * BankDeg;
		double error = want - RollOffLevelDeg(hullForward, hullUp);
		while (error > 180) error -= 360;
		while (error < -180) error += 360;
		return cs::Math::Max(-1.0, cs::Math::Min(1.0, error / LevelFullDeg)) * horizon;
	}

	DVec3 PilotAim::Square(DVec3 up, DVec3 forward, DVec3 fallback)
	{
		auto u = up - forward * DVec3::Dot(up, forward);
		if (u.LengthSquared() > 1e-6) return u.Normalized();
		u = fallback - forward * DVec3::Dot(fallback, forward);
		return u.LengthSquared() > 1e-6 ? u.Normalized() : PilotState::LevelUp(forward);
	}
}
