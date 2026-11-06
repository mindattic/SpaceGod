// US-T197: the helm's shared speed regime (see Combat/DriveRegime.h).

#include "Combat/DriveRegime.h"

namespace SpaceGod::Core
{
	const char* ToString(DriveRegime v)
	{
		switch (v)
		{
			case DriveRegime::Normal: return "Normal";
			case DriveRegime::CruiseSpool: return "CruiseSpool";
			case DriveRegime::Cruise: return "Cruise";
			case DriveRegime::Lane: return "Lane";
			case DriveRegime::Ftl: return "Ftl";
		}
		return "";
	}

	const char* ToString(DriveCue v)
	{
		switch (v)
		{
			case DriveCue::None: return "None";
			case DriveCue::Spool: return "Spool";
			case DriveCue::Engage: return "Engage";
			case DriveCue::Drop: return "Drop";
			case DriveCue::Abort: return "Abort";
		}
		return "";
	}

	const char* DriveRegimes::Label(DriveRegime v)
	{
		switch (v)
		{
			case DriveRegime::Normal: return "";
			case DriveRegime::CruiseSpool: return "CRUISE SPOOLING";
			case DriveRegime::Cruise: return "CRUISE";
			case DriveRegime::Lane: return "LANE";
			case DriveRegime::Ftl: return "FTL";
		}
		return "";
	}

	DriveCue DriveRegimes::CueFor(DriveRegime was, DriveRegime now)
	{
		if (was == now) return DriveCue::None;
		if (now == DriveRegime::CruiseSpool) return DriveCue::Spool;
		if (DriveCarries(now) && !DriveCarries(was)) return DriveCue::Engage;
		if (was == DriveRegime::CruiseSpool && now == DriveRegime::Normal) return DriveCue::Abort;
		if (DriveCarries(was) && !DriveCarries(now)) return DriveCue::Drop;
		return DriveCue::None; // one carried regime to another (a lane's end into FTL): no cue of its own
	}

	DriveCue DriveRegimeWatch::Update(DriveRegime now, double dt)
	{
		double step = dt > 0 ? dt : 0;
		SinceS += step;
		CueSinceS += step;
		if (now == Now) return DriveCue::None;
		auto cue = DriveRegimes::CueFor(Now, now);
		Was = Now;
		Now = now;
		SinceS = 0;
		if (cue != DriveCue::None)
		{
			LastCue = cue;
			CueSinceS = 0;
		}
		return cue;
	}

	double DriveRegimeWatch::Pulse(DriveCue cue, double fadeS) const
	{
		if (cue != LastCue || fadeS <= 0 || CueSinceS >= fadeS) return 0;
		return 1 - CueSinceS / fadeS;
	}
}
