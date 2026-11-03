// Port of Unity.SpaceGod Core/Hyperlanes.cs (see docs/MIGRATION.md).

#include "Hyperlanes.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "Settings/GameSetting.h"
#include "Systems/JumpGate.h"
#include "Systems/LaneUpkeep.h"

namespace SpaceGod::Core
{
	void Hyperlanes::SetDanger(const cs::Ref<Lane>& lane, double value)
	{
		Danger[lane] = value;
		routeCache.Clear();
		routeTrees.Clear();
	}

	double Hyperlanes::ConditionOf(const cs::Ref<Lane>& lane) const
	{
		double c = 0;
		return lane != nullptr && Condition.TryGetValue(lane, c) ? c : 1;
	}

	void Hyperlanes::SetCondition(const cs::Ref<Lane>& lane, double value)
	{
		if (lane == nullptr) return;
		bool carried = Carries(lane);
		Condition[lane] = cs::Math::Max(0.0, cs::Math::Min(1.0, value));
		if (Carries(lane) != carried)
		{
			routeCache.Clear();
			routeTrees.Clear();
		}
	}

	bool Hyperlanes::Carries(const cs::Ref<Lane>& lane) const { return lane != nullptr && LaneCondition::Carries(ConditionOf(lane)); }

	double Hyperlanes::SpeedFactor(const cs::Ref<Lane>& lane) const { return LaneCondition::SpeedFactor(ConditionOf(lane)); }

	double Hyperlanes::Reliability(const cs::Ref<Lane>& lane) const { return LaneCondition::Reliability(ConditionOf(lane)); }

	double Hyperlanes::RideDays(const cs::Ref<Lane>& lane, double lengthKm) const
	{
		return RideDays(lengthKm) / cs::Math::Max(1e-3, SpeedFactor(lane));
	}

	Hyperlanes::Hyperlanes(cs::Ref<Ephemeris> ephemeris)
	{
		this->ephemeris = ephemeris;
		catalog = ephemeris->Catalog;
	}

	namespace
	{
		struct Link { const char* A; const char* B; double Bend; };
	}

	cs::Ref<Hyperlanes> Hyperlanes::CreateDefault(cs::Ref<Ephemeris> ephemeris)
	{
		auto h = cs::New<Hyperlanes>(ephemeris);
		const Link links[] =
		{
			{ "Mercury", "Venus", 0.06 }, { "Venus", "Earth", -0.05 }, { "Earth", "Mars", 0.07 }, { "Mars", "Ceres", -0.06 },
			{ "Ceres", "Jupiter", 0.05 }, { "Earth", "Ceres", -0.04 }, { "Mars", "Jupiter", 0.04 }, { "Jupiter", "Saturn", -0.05 },
			{ "Saturn", "Uranus", 0.05 }, { "Uranus", "Neptune", -0.06 }, { "Neptune", "Pluto", 0.07 }, { "Saturn", "Neptune", 0.03 },
			// Out to the Greek camp's mines at Jupiter's L4, 60 degrees ahead along its orbit (the Trojan camp at
			// L5 has no lane: only the raiders hidden there go).
			{ "Jupiter", "Hektor", 0.04 }, { "Hektor", "Eurybates", -0.05 },
		};
		for (const auto& l : links) h->Add(l.A, l.B, l.Bend);
		// The belt's freight road (U.SPC-T483): the Ceres-Jupiter lane is built heavy, so the Belt Consortium's tugs can
		// push ice and rock from the belt out to its depot at Callisto.
		h->MarkHeavy("Ceres", "Jupiter");
		return h;
	}

	cs::Ref<Lane> Hyperlanes::MarkHeavy(const cs::String& a, const cs::String& b)
	{
		int ia = (*catalog)[a]->Index, ib = (*catalog)[b]->Index;
		for (const auto& lane : Lanes)
			if ((lane->A == ia && lane->B == ib) || (lane->A == ib && lane->B == ia))
			{
				lane->Heavy = true;
				return lane;
			}
		return nullptr;
	}

	double Hyperlanes::RideDays(const cs::Ref<Ship>& ship, const cs::Ref<Lane>& lane, double lengthKm) const
	{
		return ship != nullptr && ship->Tow != nullptr && lane->Heavy ? HeavyRideDays(lengthKm) / cs::Math::Max(1e-3, SpeedFactor(lane)) : RideDays(lane, lengthKm);
	}

	cs::Ref<cs::List<LaneHop>> Hyperlanes::RouteFor(const cs::Ref<Ship>& ship, int fromSystem, int toSystem, double jd)
	{
		return ship != nullptr && ship->Tow != nullptr ? HeavyRoute(fromSystem, toSystem, jd) : Route(fromSystem, toSystem, jd);
	}

	cs::Ref<cs::List<LaneHop>> Hyperlanes::HeavyRoute(int fromSystem, int toSystem, double jd)
	{
		return fromSystem == toSystem ? cs::Ref<cs::List<LaneHop>>() : FindRoute(fromSystem, toSystem, jd, /* heavyOnly: */ true);
	}

	cs::Ref<Hyperlanes> Hyperlanes::CreateAlphaCentauri(cs::Ref<Ephemeris> ephemeris)
	{
		auto h = cs::New<Hyperlanes>(ephemeris);
		const Link links[] =
		{
			{ "Rigil Gate", "Alpha Centauri Ab", 0.06 }, { "Rigil Gate", "Toliman Forge", -0.05 },
			{ "Toliman Forge", "Proxima b", 0.02 }, { "Rigil Gate", "Proxima b", -0.02 },
			{ "Proxima b", "Proxima c", 0.05 }, { "Proxima b", "Proxima d", -0.06 }, { "Proxima b", "Blackhollow", 0.07 },
		};
		for (const auto& l : links) h->Add(l.A, l.B, l.Bend);
		return h;
	}

	int Hyperlanes::RingCount(double lengthKm)
	{
		return cs::Math::Max(MinRings, cs::Math::Min(MaxRings, cs::ToInt(cs::Math::Round(cs::Math::Max(0.0, lengthKm) / RingSpacingKm))));
	}

	int Hyperlanes::RingsPassed(double progress, int count)
	{
		return cs::Math::Max(0, cs::Math::Min(count, cs::ToInt(cs::Math::Floor(cs::Math::Max(0.0, progress) * (count + 1) + 1e-9))));
	}

	DVec3 Hyperlanes::GateHelio(const cs::Ref<JumpGate>& gate) { return gate->PositionKm; }

	cs::Ref<Lane> Hyperlanes::AddGateSpur(const cs::Ref<JumpGate>& gate, double jd)
	{
		if (gate == nullptr || Lanes.Count() == 0) return nullptr;
		for (const auto& spur : GateSpurs)
			if (spur->Gate == gate) return spur;
		auto at = GateHelio(gate);
		int best = -1;
		double bestKm = cs::Double::MaxValue;
		for (const auto& lane : Lanes)
			for (int end = 0; end < 2; end++)
			{
				int system = end == 0 ? lane->A : lane->B;
				double km = (ephemeris->HelioAt(system, jd) - at).Length();
				if (km >= bestKm) continue;
				bestKm = km;
				best = system;
			}
		if (best < 0) return nullptr;
		int root = catalog->Root()->Index;
		auto made = cs::New<Lane>();
		made->Id = GateSpurIdBase + GateSpurs.Count();
		made->A = best;
		made->B = root;
		made->Bend = GateSpurs.Count() % 2 == 0 ? 0.03 : -0.03;
		made->Name = cs::Cat((*catalog)[best]->Name, "–", gate->Name);
		made->Hub = root;
		made->Gate = gate;
		GateSpurs.Add(made);
		return made;
	}

	cs::String Hyperlanes::EndName(const cs::Ref<Lane>& lane, int at) const
	{
		return lane->Gate != nullptr && at == lane->B ? lane->Gate->Name : (*catalog)[at]->Name;
	}

	DVec3 Hyperlanes::AnchorHelio(const cs::Ref<Lane>& lane, int at, double jd)
	{
		return lane->Gate != nullptr && at == lane->B ? GateHelio(lane->Gate) : ephemeris->HelioAt(at, jd);
	}

	cs::Nullable<LaneHop> Hyperlanes::GateSpurRingCrossed(DVec3 fromHelio, DVec3 toHelio, double jd)
	{
		for (const auto& spur : GateSpurs)
			for (int end = 0; end < 2; end++)
			{
				int at = end == 0 ? spur->A : spur->B;
				auto centre = RampHelio(spur, at, jd);
				auto n = RampDirection(spur, at, jd);
				double d0 = DVec3::Dot(fromHelio - centre, n), d1 = DVec3::Dot(toHelio - centre, n);
				// d0 must be clearly behind the ring: a path lying in its plane is not a crossing, and float noise at
				// heliocentric magnitudes (~1e-7 km) must not read as one.
				if (!(d0 < -1e-4 && d1 >= 0)) continue;
				auto hit = fromHelio + (toHelio - fromHelio) * (d0 / (d0 - d1));
				if ((hit - centre).Length() <= GateRadiusKm) return LaneHop(spur, at, spur->Other(at));
			}
		return std::nullopt;
	}

	cs::Ref<Lane> Hyperlanes::Add(const cs::String& a, const cs::String& b, double bend)
	{
		auto lane = cs::New<Lane>();
		lane->Id = Lanes.Count(); lane->A = (*catalog)[a]->Index; lane->B = (*catalog)[b]->Index; lane->Bend = bend; lane->Name = cs::Cat(a, "–", b);
		lane->Hub = CommonStar(lane->A, lane->B);
		Lanes.Add(lane);
		return lane;
	}

	int Hyperlanes::CommonStar(int a, int b) const
	{
		for (int x = (*catalog)[a]->ParentIndex; x >= 0; x = (*catalog)[x]->ParentIndex)
		{
			if (!(*catalog)[x]->UsesStellarMapping()) continue;
			for (int y = (*catalog)[b]->ParentIndex; y >= 0; y = (*catalog)[y]->ParentIndex)
				if (x == y) return x;
		}
		return 0;
	}

	int Hyperlanes::SystemOf(int body) const
	{
		while (body > 0 && !(*catalog)[(*catalog)[body]->ParentIndex]->UsesStellarMapping()) body = (*catalog)[body]->ParentIndex;
		return body;
	}

	double Hyperlanes::KeepOutKm(int star) const
	{
		return SunKeepOutKm * cs::Math::Sqrt(cs::Math::Max((*catalog)[star]->LuminositySolar, 1e-6));
	}

	int Hyperlanes::KeepOutStar(DVec3 aHelio, DVec3 bHelio, double jd)
	{
		int best = 0;
		double bestRatio = cs::Double::MaxValue;
		for (int i = 0; i < catalog->Count(); i++)
		{
			if (!(*catalog)[i]->IsStar()) continue;
			auto c = ephemeris->HelioAt(i, jd);
			auto chord = bHelio - aHelio;
			double t = cs::Math::Max(0.0, cs::Math::Min(1.0, DVec3::Dot(c - aHelio, chord) / cs::Math::Max(chord.LengthSquared(), 1e-12)));
			double ratio = (aHelio + chord * t - c).Length() / KeepOutKm(i);
			if (ratio < bestRatio)
			{
				bestRatio = ratio;
				best = i;
			}
		}
		return best;
	}

	DVec3 Hyperlanes::CurveKm(const cs::Ref<Lane>& lane, DVec3 a, DVec3 b, double s, double jd, int star, int shape)
	{
		if (star < 0) star = KeepOutStar(a, b, jd);
		auto hub = ephemeris->HelioAt(lane->Hub, jd);
		auto centre = ephemeris->HelioAt(star, jd);
		return Curve(a, b, lane->Bend, hub, centre, KeepOutKm(star), s, DVec3::UnitZ, shape);
	}

	double Hyperlanes::CurveLengthKm(const cs::Ref<Lane>& lane, DVec3 a, DVec3 b, double jd, int star, int shape)
	{
		if (star < 0) star = KeepOutStar(a, b, jd);
		auto hub = ephemeris->HelioAt(lane->Hub, jd);
		auto centre = ephemeris->HelioAt(star, jd);
		return CurveLength(a, b, lane->Bend, hub, centre, KeepOutKm(star), DVec3::UnitZ, shape);
	}

	int Hyperlanes::ShapeOf(const cs::Ref<Lane>& lane, DVec3 a, DVec3 b, int star, double jd)
	{
		(void)lane; // unused in the original too
		return Detours(a, b, ephemeris->HelioAt(star, jd), KeepOutKm(star)) ? 1 : 0;
	}

	bool Hyperlanes::Detours(DVec3 a, DVec3 b, DVec3 keepOutCentre, double keepOut)
	{
		auto chord = b - a;
		auto ac = a - keepOutCentre;
		double t0 = cs::Math::Max(0.0, cs::Math::Min(1.0, -DVec3::Dot(ac, chord) / cs::Math::Max(chord.LengthSquared(), 1e-12)));
		return !((ac + chord * t0).Length() > keepOut * 1.5 || t0 <= 0 || t0 >= 1);
	}

	double Hyperlanes::RampRadiusKm(int system) const
	{
		auto p = (*catalog)[system];
		double wanted = cs::Math::Max(p->MoonReachKm * 1.1, p->RadiusKm * 6);
		return cs::Math::Min(wanted, ScaleModel::NeighbourhoodKm(p) * 0.9);
	}

	DVec3 Hyperlanes::RampDirection(const cs::Ref<Lane>& lane, int at, double jd)
	{
		auto far = AnchorHelio(lane, lane->Other(at), jd);
		auto near = AnchorHelio(lane, at, jd);
		return (far - near).Normalized();
	}

	DVec3 Hyperlanes::RampOffset(const cs::Ref<Lane>& lane, int at, double jd)
	{
		if (lane->Gate != nullptr && at == lane->B)
		{
			auto gateAt = GateHelio(lane->Gate);
			auto dir = RampDirection(lane, at, jd);
			auto body = ephemeris->HelioAt(at, jd);
			return gateAt + dir * GateRampInsetKm - body;
		}
		return RampDirection(lane, at, jd) * RampRadiusKm(at);
	}

	DVec3 Hyperlanes::RampHelio(const cs::Ref<Lane>& lane, int at, double jd)
	{
		auto body = ephemeris->HelioAt(at, jd);
		return body + RampOffset(lane, at, jd);
	}

	cs::Nullable<LaneHop> Hyperlanes::OnRampAt(DVec3 helioKm, double jd)
	{
		cs::Nullable<LaneHop> best;
		double bestKm = GateRadiusKm;
		for (const auto& lane : Lanes)
			for (int end = 0; end < 2; end++)
			{
				// A failed lane's ring gates are dark (U.SPC-T481): flying through one joins nothing.
				if (!Carries(lane)) break;
				int at = end == 0 ? lane->A : lane->B;
				double km = (helioKm - RampHelio(lane, at, jd)).Length();
				if (km > bestKm) continue;
				bestKm = km;
				best = LaneHop(lane, at, lane->Other(at));
			}
		return best;
	}

	cs::List<LaneHop> Hyperlanes::OnwardHops(int system, const cs::Ref<Lane>& cameIn) const
	{
		cs::List<LaneHop> hops;
		if (cameIn != nullptr && cameIn->Gate != nullptr && system == cameIn->B) return hops;
		for (const auto& lane : Lanes)
			if (lane != cameIn && (lane->A == system || lane->B == system) && Carries(lane)) hops.Add(LaneHop(lane, system, lane->Other(system)));
		for (const auto& spur : GateSpurs)
			if (spur != cameIn && spur->A == system) hops.Add(LaneHop(spur, system, spur->B));
		return hops;
	}

	DVec3 Hyperlanes::Curve(DVec3 a, DVec3 b, double bend, double keepOut, double s, DVec3 up)
	{
		return Curve(a, b, bend, DVec3::Zero, DVec3::Zero, keepOut, s, up);
	}

	DVec3 Hyperlanes::Curve(DVec3 a, DVec3 b, double bend, DVec3 hub, DVec3 keepOutCentre, double keepOut, double s, DVec3 up, int shape)
	{
		auto chord = b - a;
		double length = chord.Length();
		auto dir = length > 0 ? chord / length : DVec3::UnitX;
		auto mid = (a + b) * 0.5;
		auto side = DVec3::Cross(chord, up);
		side = side.LengthSquared() > 0 ? side.Normalized() : DVec3::UnitX;
		// Bow away from the hub, so the curve never sags toward the star.
		if (DVec3::Dot(side, mid - hub) < 0) side = -side;

		// Closest approach of the straight line to the star.
		auto ac = a - keepOutCentre;
		double t0 = cs::Math::Max(0.0, cs::Math::Min(1.0, -DVec3::Dot(ac, chord) / cs::Math::Max(length * length, 1e-12)));
		auto nearest = ac + chord * t0;
		bool detour = shape >= 0 ? shape == 1 : !(nearest.Length() > keepOut * 1.5 || t0 <= 0 || t0 >= 1);
		if (!detour)
		{
			auto apex = mid + side * (cs::Math::Abs(bend) * length);
			// The control point that puts the curve's middle at the apex.
			auto control = apex * 2 - mid;
			return Quadratic(a, control, b, s);
		}

		// The line passes near the star: go round through a waypoint out on the keep-out circle,
		// as two quadratics that join smoothly there (tangent along the chord).
		auto outward = nearest - dir * DVec3::Dot(nearest, dir);
		outward = outward.LengthSquared() > 1e-6 * keepOut * keepOut ? outward.Normalized() : side;
		auto w = keepOutCentre + nearest - outward * DVec3::Dot(nearest, outward) + outward * (keepOut * 1.4);
		if (s < 0.5)
		{
			auto c1 = w - dir * ((w - a).Length() * 0.5);
			return Quadratic(a, c1, w, s * 2);
		}
		auto c2 = w + dir * ((b - w).Length() * 0.5);
		return Quadratic(w, c2, b, s * 2 - 1);
	}

	DVec3 Hyperlanes::Quadratic(DVec3 a, DVec3 c, DVec3 b, double t)
	{
		double u = 1 - t;
		return a * (u * u) + c * (2 * u * t) + b * (t * t);
	}

	DVec3 Hyperlanes::CurveTangent(DVec3 a, DVec3 b, double bend, double keepOut, double s, DVec3 up)
	{
		return CurveTangent(a, b, bend, DVec3::Zero, DVec3::Zero, keepOut, s, up);
	}

	DVec3 Hyperlanes::CurveTangent(DVec3 a, DVec3 b, double bend, DVec3 hub, DVec3 keepOutCentre, double keepOut, double s, DVec3 up, int shape)
	{
		constexpr double h = 1e-4;
		double s0 = cs::Math::Max(0.0, s - h), s1 = cs::Math::Min(1.0, s + h);
		auto d = Curve(a, b, bend, hub, keepOutCentre, keepOut, s1, up, shape) - Curve(a, b, bend, hub, keepOutCentre, keepOut, s0, up, shape);
		return d.LengthSquared() > 0 ? d.Normalized() : (b - a).Normalized();
	}

	double Hyperlanes::CurveLength(DVec3 a, DVec3 b, double bend, double keepOut, DVec3 up)
	{
		return CurveLength(a, b, bend, DVec3::Zero, DVec3::Zero, keepOut, up);
	}

	double Hyperlanes::CurveLength(DVec3 a, DVec3 b, double bend, DVec3 hub, DVec3 keepOutCentre, double keepOut, DVec3 up, int shape)
	{
		double len = 0;
		auto prev = a;
		for (int i = 1; i <= 32; i++)
		{
			auto p = Curve(a, b, bend, hub, keepOutCentre, keepOut, i / 32.0, up, shape);
			len += (p - prev).Length();
			prev = p;
		}
		return len;
	}

	double Hyperlanes::Progress(double u)
	{
		u = cs::Math::Max(0.0, cs::Math::Min(1.0, u));
		return u - cs::Math::Sin(Astro::TwoPi * u) / Astro::TwoPi;
	}

	long long Hyperlanes::Bucket(double jd)
	{
		long long bucket = cs::ToLong(cs::Math::Floor(jd * 2));
		if (bucket != cacheBucket)
		{
			cacheBucket = bucket;
			lengthCache.Clear();
			routeCache.Clear();
			routeTrees.Clear();
		}
		return bucket;
	}

	double Hyperlanes::LaneLengthKm(const cs::Ref<Lane>& lane, double jd)
	{
		auto key = LengthKey(lane->Id, Bucket(jd));
		double km = 0;
		if (lengthCache.TryGetValue(key, km)) return km;
		auto a = RampHelio(lane, lane->A, jd);
		auto b = RampHelio(lane, lane->B, jd);
		km = CurveLengthKm(lane, a, b, jd);
		lengthCache[key] = km;
		return km;
	}

	cs::Ref<cs::List<LaneHop>> Hyperlanes::Route(int fromSystem, int toSystem, double jd)
	{
		if (fromSystem == toSystem) return nullptr;
		auto key = RouteKey(fromSystem, toSystem, Bucket(jd));
		cs::Ref<cs::List<LaneHop>> cached;
		if (routeCache.TryGetValue(key, cached)) return cached;
		auto route = FindRoute(fromSystem, toSystem, jd);
		routeCache[key] = route;
		return route;
	}

	cs::Ref<cs::List<LaneHop>> Hyperlanes::FindRoute(int fromSystem, int toSystem, double jd, bool heavyOnly)
	{
		cs::Ref<RouteTreeMap> via;
		if (heavyOnly) via = RouteTree(fromSystem, jd, true);
		else
		{
			auto treeKey = LengthKey(fromSystem, Bucket(jd));
			if (!routeTrees.TryGetValue(treeKey, via)) routeTrees[treeKey] = via = RouteTree(fromSystem, jd, false);
		}
		if (!via->ContainsKey(toSystem)) return nullptr;
		auto route = cs::New<cs::List<LaneHop>>();
		for (int at = toSystem; at != fromSystem; at = via->At(at).From) route->Insert(0, via->At(at));
		return route;
	}

	cs::Ref<Hyperlanes::RouteTreeMap> Hyperlanes::RouteTree(int fromSystem, double jd, bool heavyOnly)
	{
		cs::Dictionary<int, double> dist;
		dist[fromSystem] = 0;
		auto via = cs::New<RouteTreeMap>();
		cs::List<int> open = { fromSystem };
		cs::HashSet<int> done;
		while (open.Count() > 0)
		{
			// C#: dist[x].CompareTo(dist[y]) -- double.CompareTo, NaN ordered first.
			open.Sort([&](int x, int y)
			{
				double a = dist.At(x), b = dist.At(y);
				if (a < b) return -1;
				if (a > b) return 1;
				if (a == b) return 0;
				return cs::Double::IsNaN(a) ? (cs::Double::IsNaN(b) ? 0 : -1) : 1;
			});
			int here = open[0];
			open.RemoveAt(0);
			if (!done.Add(here)) continue;
			for (const auto& lane : Lanes)
			{
				if (lane->A != here && lane->B != here) continue;
				if (heavyOnly && !lane->Heavy) continue;
				if (!Carries(lane)) continue;
				int next = lane->Other(here);
				// Each hop also costs the crossing between ramps at a junction: about half a day.
				double danger = 0;
				Danger.TryGetValue(lane, danger);
				double stalls = (1 - Reliability(lane)) * LaneCondition::StallDays()->Value;
				double cost = dist.At(here) + RideDays(lane, LaneLengthKm(lane, jd)) + 0.5 + danger + stalls;
				double old = 0;
				if (dist.TryGetValue(next, old) && old <= cost) continue;
				dist[next] = cost;
				(*via)[next] = LaneHop(lane, here, next);
				open.Add(next);
			}
		}
		return via;
	}
}
