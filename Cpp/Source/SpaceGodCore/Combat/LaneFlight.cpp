// US-T196: flying the hull inside a hyperlane, and the speed it bleeds off after it leaves one (see Combat/LaneFlight.h).

#include "Combat/LaneFlight.h"
#include "Hyperlanes.h"

#include <cmath>

namespace SpaceGod::Core
{
	double LaneFlight::TubeRadiusKm() { return Hyperlanes::GateRadiusKm; }

	bool LaneFlight::Step(double strafeRight, double strafeUp, double dt)
	{
		if (dt <= 0) return Outside();
		double r = cs::Math::Max(-1.0, cs::Math::Min(1.0, strafeRight));
		double u = cs::Math::Max(-1.0, cs::Math::Min(1.0, strafeUp));
		// The same response however the frames fall: exact for a constant stick across the frame.
		double k = 1 - std::exp(-dt / ResponseS);
		RightKmS += (r * StrafeKmS - RightKmS) * k;
		UpKmS += (u * StrafeKmS - UpKmS) * k;
		RightKm += RightKmS * dt;
		UpKm += UpKmS * dt;
		return Outside();
	}

	double LaneWake::DurationFor(double fromKmS, double toKmS)
	{
		if (!(fromKmS > toKmS) || toKmS <= 0) return 0;
		double s = SecondsPerE * std::log(fromKmS / toKmS);
		return cs::Math::Max(MinS, cs::Math::Min(MaxS, s));
	}

	void LaneWake::Begin(double fromKmS, double toKmS)
	{
		Clear();
		double d = DurationFor(fromKmS, toKmS);
		if (d <= 0) return;
		FromKmS = fromKmS;
		ToKmS = toKmS;
		DurationS = d;
	}

	double LaneWake::SpeedAt(double elapsedS) const
	{
		if (DurationS <= 0 || FromKmS <= 0 || ToKmS <= 0) return ToKmS;
		double t = cs::Math::Max(0.0, cs::Math::Min(1.0, elapsedS / DurationS));
		double ease = t * t * (3 - 2 * t);
		// From the lane's speed down to the hull's own, evenly in log space: every halving takes about as long as the last.
		return FromKmS * std::pow(ToKmS / FromKmS, ease);
	}

	double LaneWake::Step(double dt)
	{
		if (DurationS <= 0) return ToKmS;
		ElapsedS = cs::Math::Min(DurationS, ElapsedS + cs::Math::Max(0.0, dt));
		return SpeedAt(ElapsedS);
	}
}
