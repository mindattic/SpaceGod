// Port of Unity.SpaceGod Core/SimClock.cs (see docs/MIGRATION.md).

#include "SimClock.h"
#include "Astro.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	const cs::List<SimClock::WarpLevel>& SimClock::Levels()
	{
		static const cs::List<WarpLevel> value =
		{
			WarpLevel(1, "real time"),
			WarpLevel(10, "10 s/s"),
			WarpLevel(60, "1 min/s"),
			WarpLevel(600, "10 min/s"),
			WarpLevel(3600, "1 hour/s"),
			WarpLevel(6 * 3600, "6 hours/s"),
			WarpLevel(86400, "1 day/s"),
			WarpLevel(7 * 86400, "1 week/s"),
			WarpLevel(30 * 86400, "1 month/s"),
			WarpLevel(365.25 * 86400, "1 year/s"),
		};
		return value;
	}

	const cs::Ref<ChoiceSetting>& SimClock::StartingWarp()
	{
		static const cs::Ref<ChoiceSetting> value = []
		{
			// C#: Array.ConvertAll(Levels, l => l.Label)
			cs::List<cs::String> labels;
			for (const auto& l : Levels()) labels.Add(l.Label);
			return cs::New<ChoiceSetting>("simulation.startingWarp", SettingGroups::Simulation,
				"Starting time warp", labels, DefaultWarpIndex,
				"How fast the clock runs when a new game starts; the time controls still change it in play.", "speed clock rate time");
		}();
		return value;
	}

	void SimClock::ToggleEasedPause()
	{
		if (paused || pausing)
		{
			paused = false;
			pausing = false;
			resuming = true;
			Speed = cs::Math::Max(Speed, EaseFloor);
		}
		else
		{
			resuming = false;
			pausing = true;
		}
	}

	cs::DateTime SimClock::Utc() const { return Astro::ToDateTime(Jd); }

	cs::Ref<SimClock> SimClock::StartingNow() { return cs::New<SimClock>(Astro::JulianDate(cs::DateTime::UtcNow())); }

	int SimClock::WarpToCover(double simSeconds, double realSeconds, int atLeast)
	{
		int i = cs::Math::Max(0, cs::Math::Min(Levels().Count() - 1, atLeast));
		while (i < Levels().Count() - 1 && simSeconds / Levels()[i].SimSecondsPerSecond > realSeconds) i++;
		return i;
	}

	double SimClock::Advance(double realSeconds)
	{
		if (paused || realSeconds <= 0) return 0;
		if (pausing || resuming)
		{
			double factor = cs::Math::Pow(EaseFactor, realSeconds * EaseStepsPerSecond);
			if (pausing)
			{
				Speed *= factor;
				if (Speed < EaseFloor)
				{
					Speed = 0;
					pausing = false;
					paused = true;
					return 0;
				}
			}
			else
			{
				Speed /= factor;
				if (Speed >= 1)
				{
					Speed = 1;
					resuming = false;
				}
			}
		}
		double days = realSeconds * Warp().SimSecondsPerSecond * Speed / Astro::SecondsPerDay;
		Jd += days;
		return days;
	}
}
