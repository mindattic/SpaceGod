// HoldRewindCurveTests: unit checks of the hold rewind's arithmetic (US-T410, Persistence/HoldRewindCurve.h): the speed
// eases up from its start to its top and never falls or overshoots, the brake settles on the edge, the oldest moment
// reachable never goes past the window, the first kept pose or the last resume point (the designer's "never revisit a
// time already rewound through"), and a hold's steps never pass the oldest moment.
//
//   HoldRewindCurveTests.exe     exit code 0 = every check passed, 1 = failures (listed)
//
// Built only by tools/build-core.ps1 (SPACEGOD_STANDALONE); the UE module compiles it to nothing.

#ifdef SPACEGOD_STANDALONE

#include <cmath>
#include <cstdio>
#include <limits>

#include "Persistence/HoldRewindCurve.h"

using namespace SpaceGod::Core;

namespace
{
	int failures = 0, checks = 0;

	void Check(bool ok, const char* what)
	{
		checks++;
		if (ok) return;
		failures++;
		std::printf("FAIL  %s\n", what);
	}

	bool Near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

	// The shipped numbers (UnitySpaceApp::HoldRewindTuning): a slow start, a 0.8 s ramp, three times real time at the top.
	constexpr double StartRate = 0.25;
	constexpr double TopRate = 3.0;
	constexpr double RampS = 0.8;
	constexpr double WindowS = 10;
}

int main()
{
	// The ease: monotonic, from the start rate to the top, constant after the ramp.
	{
		Check(Near(HoldRewindCurve::Rate(0, StartRate, TopRate, RampS), StartRate), "rate starts at the start rate");
		Check(Near(HoldRewindCurve::Rate(RampS, StartRate, TopRate, RampS), TopRate), "rate reaches the top at the end of the ramp");
		Check(Near(HoldRewindCurve::Rate(RampS * 5, StartRate, TopRate, RampS), TopRate), "rate stays at the top after the ramp");
		double last = -1;
		bool monotonic = true, capped = true;
		for (int i = 0; i <= 2000; i++)
		{
			double r = HoldRewindCurve::Rate(i * 0.001, StartRate, TopRate, RampS);
			if (r < last - 1e-12) monotonic = false;
			if (r > TopRate + 1e-12) capped = false;
			last = r;
		}
		Check(monotonic, "rate never falls while held");
		Check(capped, "rate never passes the top");
		// Smooth at the ends: the first and last steps are much smaller than the middle ones (no jolt).
		double d0 = HoldRewindCurve::Rate(0.01, StartRate, TopRate, RampS) - HoldRewindCurve::Rate(0, StartRate, TopRate, RampS);
		double dm = HoldRewindCurve::Rate(RampS * 0.5 + 0.005, StartRate, TopRate, RampS) - HoldRewindCurve::Rate(RampS * 0.5 - 0.005, StartRate, TopRate, RampS);
		Check(d0 < dm * 0.2, "the ease starts gently");
		Check(Near(HoldRewindCurve::Rate(0.3, TopRate, TopRate, RampS), TopRate), "no ramp when the start is the top");
		Check(Near(HoldRewindCurve::Rate(0.3, StartRate, TopRate, 0), TopRate), "a zero ramp is the top at once");
	}

	// The brake: full rate away from the edge, down to the minimum at it, never below the minimum.
	{
		Check(Near(HoldRewindCurve::Braked(3, 10, 0, 1, 0.1), 3), "no brake far from the edge");
		Check(Near(HoldRewindCurve::Braked(3, 0, 0, 1, 0.1), 0.1), "the minimum at the edge");
		Check(HoldRewindCurve::Braked(3, 0.5, 0, 1, 0.1) < 3 && HoldRewindCurve::Braked(3, 0.5, 0, 1, 0.1) > 0.1, "braking between");
		Check(Near(HoldRewindCurve::Braked(3, -1, 0, 1, 0.1), 0.1), "past the edge is the minimum");
	}

	// The oldest moment reachable: the window, the first kept, the floor (the last resume point), whichever is latest.
	{
		Check(Near(HoldRewindCurve::Oldest(100, WindowS, 0, -1e300), 90), "the window back");
		Check(Near(HoldRewindCurve::Oldest(100, WindowS, 95, -1e300), 95), "not before the first thing kept");
		Check(Near(HoldRewindCurve::Oldest(100, WindowS, 0, 97), 97), "never past the last resume point");
		// The designer's case: a 10 s rewind, then the 10 s cooldown: the next hold has the fresh 10 s, no more.
		double resumedAt = 50;
		double afterCooldown = resumedAt + 10;
		Check(Near(HoldRewindCurve::Oldest(afterCooldown, WindowS, 0, resumedAt), 50), "after the cooldown, back to the resume point and no further");
		Check(Near(HoldRewindCurve::Oldest(afterCooldown + 5, WindowS, 0, resumedAt), 55), "later, the window again");
	}

	// A hold's steps: never past the oldest moment, nor past a test's stop.
	{
		const double none = std::numeric_limits<double>::lowest();
		double tau = 100;
		double oldest = HoldRewindCurve::Oldest(100, WindowS, 0, 95);
		for (int i = 0; i < 1000; i++) tau = HoldRewindCurve::Step(tau, 1.0 / 60, TopRate, oldest, none);
		Check(Near(tau, 95), "a long hold settles on the floor");
		tau = 100;
		for (int i = 0; i < 1000; i++) tau = HoldRewindCurve::Step(tau, 1.0 / 60, TopRate, oldest, 98.5);
		Check(Near(tau, 98.5), "a stop is never passed");
	}

	std::printf("HoldRewindCurveTests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

#endif
