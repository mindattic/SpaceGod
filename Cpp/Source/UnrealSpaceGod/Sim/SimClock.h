#pragma once

#include "CoreMinimal.h"

/** The time-warp ladder of Unity.SpaceGod's SimClock: simulated seconds per real second. */
namespace SimClock
{
	struct FWarpLevel
	{
		double SimSecondsPerSecond;
		const TCHAR* Label;
	};

	inline const FWarpLevel Levels[] =
	{
		{ 1, TEXT("real time") },
		{ 10, TEXT("10 s/s") },
		{ 60, TEXT("1 min/s") },
		{ 600, TEXT("10 min/s") },
		{ 3600, TEXT("1 hour/s") },
		{ 6 * 3600, TEXT("6 hours/s") },
		{ 86400, TEXT("1 day/s") },
		{ 7 * 86400, TEXT("1 week/s") },
		{ 30 * 86400, TEXT("1 month/s") },
		{ 365.25 * 86400, TEXT("1 year/s") },
	};

	constexpr int32 NumLevels = UE_ARRAY_COUNT(Levels);
	constexpr int32 DefaultWarpIndex = 4;
}
