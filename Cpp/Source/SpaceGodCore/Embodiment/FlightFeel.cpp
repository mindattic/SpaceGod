// US-T170: the flight feel pass (see FlightFeel.h).

#include "Embodiment/FlightFeel.h"

#include <algorithm>
#include <cmath>

namespace SpaceGod::Core
{
	const char* ToString(FeelRole v)
	{
		switch (v)
		{
		case FeelRole::Fighter: return "Fighter";
		case FeelRole::Hauler: return "Hauler";
		case FeelRole::Capital: return "Capital";
		case FeelRole::Drone: return "Drone";
		}
		return "";
	}

	double FlightFeelProfile::MassScale(double massTonnes)
	{
		return std::clamp(std::log10(1 + std::max(0.0, massTonnes)) / 5, 0.0, 1.2);
	}

	FeelRole FlightFeelProfile::RoleOf(PilotControl stick)
	{
		switch (stick)
		{
		case PilotControl::Hauler: return FeelRole::Hauler;
		case PilotControl::Helm: return FeelRole::Capital;
		default: return FeelRole::Fighter;
		}
	}

	FlightFeelProfile FlightFeelProfile::Of(FeelRole role, double massTonnes)
	{
		FlightFeelProfile p;
		p.Role = role;
		p.MassTonnes = massTonnes;
		switch (role)
		{
		case FeelRole::Fighter:
			// Twitchy and thrilling: the view snaps wide on the afterburner with a punch on lighting it, a fast thin buzz.
			p.BoostFovDeg = 12; p.CruiseFovDeg = 26; p.SpeedFovDeg = 5; p.IgnitionFovDeg = 7; p.FovResponseHz = 9;
			p.ShakeDeg = 0.32; p.ShakeHz = 14; p.HeftS = 0.08; p.KickDeg = 0.7; p.StreakFullPace = 1.15;
			break;
		case FeelRole::Hauler:
			// Lumbering but satisfying: the view swells slowly, and the drive's throb builds as it takes the load.
			p.BoostFovDeg = 9; p.CruiseFovDeg = 22; p.SpeedFovDeg = 4; p.IgnitionFovDeg = 2.5; p.FovResponseHz = 3.5;
			p.ShakeDeg = 0.24; p.ShakeHz = 6; p.HeftS = 0.55; p.KickDeg = 0.5; p.StreakFullPace = 1.1;
			break;
		case FeelRole::Capital:
			// Immense: barely a widening, a deep slow rumble through the whole frame, and a broadside that rocks the view.
			p.BoostFovDeg = 5; p.CruiseFovDeg = 18; p.SpeedFovDeg = 2; p.IgnitionFovDeg = 1; p.FovResponseHz = 2;
			p.ShakeDeg = 0.2; p.ShakeHz = 2.4; p.HeftS = 1.4; p.KickDeg = 1.1; p.StreakFullPace = 1.0;
			break;
		case FeelRole::Drone:
			// Nimble: quick, light, nothing to rumble; a sharp tick at each good weld is its feel.
			p.BoostFovDeg = 10; p.CruiseFovDeg = 0; p.SpeedFovDeg = 4; p.IgnitionFovDeg = 3; p.FovResponseHz = 10;
			p.ShakeDeg = 0.06; p.ShakeHz = 18; p.HeftS = 0.05; p.KickDeg = 0.45; p.StreakFullPace = 1.2;
			break;
		}
		// The hull's own mass on top of its role's: heavier is slower to come on, lower and broader in its shake.
		double m = MassScale(massTonnes);
		p.HeftS *= 0.6 + m;
		p.ShakeHz *= 1.3 - 0.5 * m;
		p.ShakeDeg *= 0.8 + 0.4 * m;
		p.KickDeg *= 0.8 + 0.4 * m;
		return p;
	}

	void FlightFeel::Reset(const FlightFeelProfile& profile)
	{
		*this = FlightFeel();
		Profile = profile;
	}

	void FlightFeel::Kick(double strength)
	{
		kick = std::min(kick + std::max(0.0, strength), 2.0);
	}

	void FlightFeel::Step(double dt, const FlightFeelInput& in)
	{
		if (!(dt > 0)) return;
		t += dt;
		const auto& p = Profile;
		// The afterburner lighting kicks the view wide for a moment, over the steady widening it holds while lit.
		if (in.Boosting && !boostWas) ignition = 1;
		boostWas = in.Boosting;
		ignition *= std::exp(-dt * 4.0);

		// Speed streaks: from a third of top speed, full at the role's pace; full at once under the cruise drive.
		double pace = std::max(0.0, in.Pace);
		double s = std::clamp((pace - 0.33) / std::max(p.StreakFullPace - 0.33, 1e-6), 0.0, 1.0);
		s = s * s * (3 - 2 * s);
		if (in.Cruising) s = 1;
		Streaks += (s - Streaks) * (1 - std::exp(-dt * 4.0));

		double fov = in.Cruising ? p.CruiseFovDeg : (in.Boosting ? p.BoostFovDeg : 0) + p.SpeedFovDeg * std::min(pace, 1.5);
		FovDeg += (fov - FovDeg) * (1 - std::exp(-dt * p.FovResponseHz));

		// The load the shake runs at: the drive's push, more with the afterburner or the cruise drive, eased by the heft.
		double load = std::clamp(in.Thrust, 0.0, 1.5) * 0.55 + (in.Boosting ? 0.45 : 0) + (in.Cruising ? 0.3 : 0) + 0.15 * std::min(pace, 1.5);
		Load += (load - Load) * (1 - std::exp(-dt / std::max(p.HeftS, 1e-3)));
		kick *= std::exp(-dt * (6.0 / (0.6 + FlightFeelProfile::MassScale(p.MassTonnes))));
		Shake(p.ShakeDeg * Load + p.KickDeg * kick);
	}

	void FlightFeel::Settle(double dt)
	{
		if (!(dt > 0)) return;
		t += dt;
		double k = 1 - std::exp(-dt * 4.0);
		Streaks -= Streaks * k;
		FovDeg -= FovDeg * k;
		Load -= Load * k;
		kick -= kick * k;
		ignition = 0;
		boostWas = false;
		Shake(Profile.ShakeDeg * Load + Profile.KickDeg * kick);
	}

	void FlightFeel::Shake(double amplitude)
	{
		// Three incommensurate sines an axis, so it never repeats visibly; a capital's sways mostly in roll, the deep
		// heave of something that big, a fighter's mostly in yaw and pitch, a buzz.
		double w = 2 * 3.14159265358979323846 * Profile.ShakeHz;
		auto wave = [&](double a, double b, double c) { return (std::sin(w * t * a + b) * 0.6 + std::sin(w * t * a * 1.731 + c) * 0.3 + std::sin(w * t * a * 2.437 + b + c) * 0.1); };
		double roll = Profile.Role == FeelRole::Capital ? 1.4 : Profile.Role == FeelRole::Hauler ? 0.9 : 0.5;
		ShakeYawDeg = amplitude * wave(1.0, 0.3, 1.9);
		ShakePitchDeg = amplitude * wave(1.13, 2.1, 0.7);
		ShakeRollDeg = amplitude * roll * wave(0.71, 4.0, 2.6);
	}
}
