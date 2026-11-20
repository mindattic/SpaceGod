// US-T410: the pure arithmetic of the hold rewind (docs/REWIND.md, "Hold to rewind"), kept here so it is unit-checked by
// Tests/HoldRewind/HoldRewindCurveTests.cpp (tools/build-core.ps1). The game layer (UnitySpaceApp.HoldRewind.cpp) feeds it
// the numbers from UnitySpaceApp::HoldRewindTuning. Nothing in the simulation calls it.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace SpaceGod::Core::HoldRewindCurve
{
	/// <summary>Smoothstep on [0, 1]: 0 and 1 at the ends, flat at both (no jolt as the speed starts or settles).</summary>
	inline double Smooth(double x)
	{
		x = std::clamp(x, 0.0, 1.0);
		return x * x * (3 - 2 * x);
	}

	/// <summary>How fast the past plays back (seconds of it per real second held) after <paramref name="heldS"/> held:
	/// <paramref name="startRate"/> at the press, easing up over <paramref name="rampS"/> to <paramref name="topRate"/>, then
	/// constant. Never decreasing in <paramref name="heldS"/>, never above <paramref name="topRate"/>.</summary>
	inline double Rate(double heldS, double startRate, double topRate, double rampS)
	{
		if (topRate <= startRate) return topRate;
		double x = rampS > 0 ? heldS / rampS : 1.0;
		return startRate + (topRate - startRate) * Smooth(x);
	}

	/// <summary>The rate near the end of what can be gone back to: <paramref name="rate"/> scaled down as the moment reached
	/// comes within <paramref name="brakeS"/> of <paramref name="oldest"/>, to <paramref name="minRate"/> at it, so the
	/// hold settles on the edge instead of hitting it.</summary>
	inline double Braked(double rate, double tau, double oldest, double brakeS, double minRate)
	{
		if (brakeS <= 0) return rate;
		double room = tau - oldest;
		if (room >= brakeS) return rate;
		double f = Smooth(std::max(0.0, room) / brakeS);
		return std::max(minRate, rate * f);
	}

	/// <summary>The oldest moment a hold may reach at <paramref name="livedS"/>: the window back, but never before the
	/// first thing kept nor before <paramref name="floorS"/> (the last resume point: a moment already rewound through is
	/// never revisited).</summary>
	inline double Oldest(double livedS, double windowS, double firstKeptS, double floorS)
	{
		return std::max({ livedS - windowS, firstKeptS, floorS });
	}

	/// <summary>One frame of a hold: <paramref name="tau"/> moved back by <paramref name="dt"/> at
	/// <paramref name="rate"/>, not past <paramref name="oldest"/> nor past <paramref name="target"/> (a test's stop; the
	/// lowest double for none).</summary>
	inline double Step(double tau, double dt, double rate, double oldest, double target)
	{
		return std::max({ oldest, target, tau - dt * rate });
	}
}
