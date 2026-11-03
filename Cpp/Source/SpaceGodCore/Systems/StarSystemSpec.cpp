// Port of Assets/UnitySpace/Core/Systems/StarSystemSpec.cs (US-T17).

#include "Systems/StarSystemSpec.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	const char* ToString(HabitableZone v)
	{
		switch (v)
		{
		case HabitableZone::None: return "None";
		case HabitableZone::Optimistic: return "Optimistic";
		case HabitableZone::Conservative: return "Conservative";
		}
		return "";
	}

	cs::Ref<StellarNode> StellarNode::Single(cs::Ref<StarSpec> star)
	{
		auto n = cs::New<StellarNode>();
		n->Star = star;
		return n;
	}

	cs::Ref<StellarNode> StellarNode::Pair(const cs::String& name, cs::Ref<StarSpec> a, cs::Ref<StarSpec> b, cs::Ref<BinaryOrbitSpec> orbit, const cs::String& summary)
	{
		auto n = cs::New<StellarNode>();
		n->Name = name;
		n->A = a;
		n->B = b;
		n->Orbit = orbit;
		n->Summary = summary;
		return n;
	}

	DVec3 SystemSpec::PositionLy() const { return Astro::DirectionFromRaDec(RaDeg, DecDeg) * DistanceLy; }

	cs::List<cs::Ref<StarSpec>> SystemSpec::Stars() const
	{
		cs::List<cs::Ref<StarSpec>> r;
		for (const auto& s : NodeStars(Primary)) r.Add(s);
		for (const auto& c : Companions)
			for (const auto& s : NodeStars(c->Node)) r.Add(s);
		return r;
	}

	cs::List<cs::Ref<StarSpec>> SystemSpec::NodeStars(const cs::Ref<StellarNode>& n)
	{
		cs::List<cs::Ref<StarSpec>> r;
		if (n->IsPair())
		{
			r.Add(n->A);
			r.Add(n->B);
		}
		else r.Add(n->Star);
		return r;
	}

	double HabitableZones::Seff(double teffK, double s0, double a, double b, double c, double d)
	{
		double t = cs::Math::Max(2600.0, cs::Math::Min(7200.0, teffK)) - 5780;
		return s0 + a * t + b * t * t + c * t * t * t + d * t * t * t * t;
	}

	HabitableZone HabitableZones::Classify(double luminositySolar, double teffK, double aAu, double e)
	{
		double s = MeanFlux(luminositySolar, aAu, e);
		double inner = RunawayFlux(teffK), outer = MaxGreenhouseFlux(teffK);
		if (s <= inner && s >= outer) return HabitableZone::Conservative;
		if (s <= inner * RecentVenusRatio && s >= outer * EarlyMarsRatio) return HabitableZone::Optimistic;
		return HabitableZone::None;
	}

	void HabitableZones::ConservativeEdgesAu(double luminositySolar, double teffK, double& innerAu, double& outerAu)
	{
		innerAu = cs::Math::Sqrt(luminositySolar / RunawayFlux(teffK));
		outerAu = cs::Math::Sqrt(luminositySolar / MaxGreenhouseFlux(teffK));
	}
}
