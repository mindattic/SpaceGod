// US-T127: the in-system cruise drive (see Combat/SystemCruise.h).

#include "Combat/SystemCruise.h"
#include "Combat/PilotFlight.h"

#include <cmath>

namespace SpaceGod::Core
{
	const char* ToString(SystemCruisePhase v)
	{
		switch (v)
		{
			case SystemCruisePhase::Off: return "Off";
			case SystemCruisePhase::Spooling: return "Spooling";
			case SystemCruisePhase::Cruising: return "Cruising";
		}
		return "";
	}

	double SystemCruise::SpoolSecondsFor(double massKt)
	{
		// A fighter is a few tonnes of kilotonne, a carrier a few hundred kilotonnes: a log scale between them.
		double s = 2.5 + 1.4 * std::log10(1 + cs::Math::Max(0.0, massKt) * 10);
		return cs::Math::Max(2.5, cs::Math::Min(8.0, s));
	}

	bool SystemCruise::Engage(const PilotInput& input, double massKt)
	{
		if (!input.Piloted || Active()) return false;
		Phase = SystemCruisePhase::Spooling;
		SpoolS = SpoolLeftS = SpoolSecondsFor(massKt);
		SpeedKmS = 0;
		return true;
	}

	void SystemCruise::Drop()
	{
		Phase = SystemCruisePhase::Off;
		SpoolLeftS = SpoolS = 0;
		SpeedKmS = 0;
	}

	double SystemCruise::SpeedLimit(double toDropKm, double clearanceKm)
	{
		double v = MaxKmS;
		if (toDropKm < cs::Double::PositiveInfinity) v = cs::Math::Min(v, toDropKm / ApproachS);
		if (clearanceKm < cs::Double::PositiveInfinity) v = cs::Math::Min(v, cs::Math::Max(0.0, clearanceKm) / ClearanceS);
		return cs::Math::Max(MinKmS, v);
	}

	double SystemCruise::Step(const PilotInput& input, double toDropKm, double clearanceKm, bool mayEngage, double dt)
	{
		if (!input.Piloted)
		{
			// Never a crew's drive (the Bench's flights are untouched by it).
			if (Active()) Drop();
			return 0;
		}
		if (dt <= 0 || Phase == SystemCruisePhase::Off) return 0;
		if (Phase == SystemCruisePhase::Spooling)
		{
			SpoolLeftS = cs::Math::Max(0.0, SpoolLeftS - dt);
			if (SpoolLeftS > 0 || !mayEngage) return 0;
			Phase = SystemCruisePhase::Cruising;
			SpeedKmS = MinKmS;
		}
		double limit = SpeedLimit(toDropKm, clearanceKm);
		SpeedKmS = SpeedKmS < limit ? cs::Math::Min(limit, SpeedKmS * std::exp(dt / RiseS)) : limit;
		return cs::Math::Min(SpeedKmS * dt, cs::Math::Max(0.0, toDropKm));
	}

	double SystemCruise::EtaS(double toDropKm) const
	{
		if (!(toDropKm < cs::Double::PositiveInfinity)) return cs::Double::PositiveInfinity;
		double t = Phase == SystemCruisePhase::Spooling ? SpoolLeftS : 0;
		double v = Phase == SystemCruisePhase::Cruising ? cs::Math::Max(MinKmS, SpeedKmS) : MinKmS;
		double d = toDropKm;
		constexpr double h = 0.05;
		for (int i = 0; i < 40000 && d > 1e-6; i++)
		{
			double limit = SpeedLimit(d, cs::Double::PositiveInfinity);
			v = v < limit ? cs::Math::Min(limit, v * std::exp(h / RiseS)) : limit;
			double step = v * h;
			if (step >= d)
			{
				t += d / v;
				return t;
			}
			d -= step;
			t += h;
		}
		return t;
	}
}
