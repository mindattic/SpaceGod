// Port of Unity.SpaceGod Core/ScaleModel.cs (see docs/MIGRATION.md).

#include "ScaleModel.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	double ScaleModel::LogLerp(double a, double b, double t)
	{
		if (a <= 0 || b <= 0) return a + (b - a) * t;
		return cs::Math::Exp(cs::Math::Log(a) + (cs::Math::Log(b) - cs::Math::Log(a)) * t);
	}

	double ScaleModel::GameplayRadius(double radiusKm)
	{
		return GameEarthRadius * cs::Math::Pow(radiusKm / EarthRadiusKm, GameRadiusExponent);
	}

	double ScaleModel::DisplayRadius(double radiusKm) const
	{
		return LogLerp(RealisticRadius(radiusKm), GameplayRadius(radiusKm), blend);
	}

	double ScaleModel::DisplayRadius(const cs::Ref<BodyDefinition>& body) const { return DisplayRadius(body->RadiusKm); }

	double ScaleModel::ShipLength(double lengthKm) const
	{
		return LogLerp(lengthKm / KmPerUnit, GameShipScale * cs::Math::Pow(lengthKm, GameShipExponent), blend);
	}

	double ScaleModel::LocalDistance(const cs::Ref<BodyDefinition>& body, double distanceKm) const
	{
		return LogLerp(distanceKm / KmPerUnit, GameplayLocalDistance(body, distanceKm), blend);
	}

	double ScaleModel::GameplayLocalDistance(const cs::Ref<BodyDefinition>& body, double distanceKm)
	{
		double r = body->RadiusKm;
		double rDisplay = GameplayRadius(r);
		if (distanceKm <= r) return r > 0 ? rDisplay * distanceKm / r : 0;

		if (body->UsesStellarMapping())
		{
			return rDisplay + GameAuScale *
				(cs::Math::Pow(distanceKm / Astro::AuKm, GameAuExponent) - cs::Math::Pow(r / Astro::AuKm, GameAuExponent));
		}

		double x = distanceKm / r;
		return rDisplay * (1.0 + GameLocalC * cs::Math::Pow(x - 1.0, GameLocalP));
	}

	double ScaleModel::InverseLocalDistance(const cs::Ref<BodyDefinition>& body, double sceneDistance) const
	{
		if (sceneDistance <= 0) return 0;
		double lo = cs::Math::Log(cs::Math::Max(body->RadiusKm, 1.0) * 1e-6);
		double hi = cs::Math::Log(1e15);
		for (int i = 0; i < 90; i++)
		{
			double mid = 0.5 * (lo + hi);
			if (LocalDistance(body, cs::Math::Exp(mid)) < sceneDistance) lo = mid;
			else hi = mid;
		}
		return cs::Math::Exp(0.5 * (lo + hi));
	}

	double ScaleModel::LocalDistanceDerivative(const cs::Ref<BodyDefinition>& body, double distanceKm) const
	{
		double h = cs::Math::Max(distanceKm * 1e-5, 1e-6);
		double lo = cs::Math::Max(distanceKm - h, 0.0);
		return (LocalDistance(body, distanceKm + h) - LocalDistance(body, lo)) / (distanceKm + h - lo);
	}

	DVec3 ScaleModel::LocalOffset(const cs::Ref<BodyDefinition>& body, DVec3 trueOffsetKm) const
	{
		double d = trueOffsetKm.Length();
		if (d <= 0) return DVec3::Zero;
		return trueOffsetKm.SwapYZ() * (LocalDistance(body, d) / d);
	}

	DVec3 ScaleModel::InverseLocalOffset(const cs::Ref<BodyDefinition>& body, DVec3 sceneOffset) const
	{
		double s = sceneOffset.Length();
		if (s <= 0) return DVec3::Zero;
		return sceneOffset.SwapYZ() * (InverseLocalDistance(body, s) / s);
	}

	void ScaleModel::Layout(const cs::Ref<Ephemeris>& ephemeris, cs::List<DVec3>& scenePositions) const
	{
		auto catalog = ephemeris->Catalog;
		for (int i = 0; i < catalog->Count(); i++)
		{
			auto b = (*catalog)[i];
			if (b->ParentIndex < 0)
			{
				scenePositions[i] = DVec3::Zero;
				continue;
			}
			scenePositions[i] = scenePositions[b->ParentIndex] + LocalOffset((*catalog)[b->ParentIndex], ephemeris->Relative(i));
		}
	}

	double ScaleModel::NeighbourhoodKm(const cs::Ref<BodyDefinition>& body)
	{
		return cs::Math::Max(body->RadiusKm * NeighbourhoodRadii, cs::Math::Max(body->MoonReachKm, body->SystemReachKm) * NeighbourhoodMoonMargin);
	}

	DVec3 ScaleModel::MapNear(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, int body, DVec3 helioKm) const
	{
		cs::List<int> chain;
		AddChain(ephemeris->Catalog, body, chain);
		return MapThrough(ephemeris, scene, chain, chain.Count(), helioKm);
	}

	DVec3 ScaleModel::MapThrough(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, const cs::List<int>& bodies, int count, DVec3 helioKm) const
	{
		if (count == 0) return scene[0] + LocalOffset(ephemeris->Catalog->Root(), helioKm - ephemeris->Helio(0));

		int body = bodies[count - 1];
		auto b = (*ephemeris->Catalog)[body];
		auto r = helioKm - ephemeris->Helio(body);
		double distance = r.Length();
		double edgeKm = NeighbourhoodKm(b);
		if (distance <= edgeKm) return scene[body] + LocalOffset(b, r);

		// Local and outer mappings disagree by c at the edge; the gap closes as the outer-map
		// separation s grows by 2|c| past the edge, at half rate, so distance never turns back.
		auto toOuter = MapThrough(ephemeris, scene, bodies, count - 1, helioKm) - scene[body];
		double s = toOuter.Length();
		auto edge = ephemeris->Helio(body) + r * (edgeKm / distance);
		double s1 = (MapThrough(ephemeris, scene, bodies, count - 1, edge) - scene[body]).Length();
		double l1 = LocalDistance(b, edgeKm);
		double c = l1 - s1;
		if (cs::Math::Abs(c) < 1e-9 * cs::Math::Max(1.0, l1)) return scene[body] + toOuter;

		double k = cs::Math::Max(0.0, cs::Math::Min(1.0, 1 - (s - s1) / (2 * cs::Math::Abs(c))));
		if (k <= 0) return scene[body] + toOuter;
		double d = cs::Math::Max(s + c * k, l1);
		auto dir = DVec3::Lerp(toOuter / cs::Math::Max(s, 1e-12), r.SwapYZ() / distance, k).Normalized();
		return scene[body] + dir * d;
	}

	void ScaleModel::AddChain(const cs::Ref<BodyCatalog>& catalog, int body, cs::List<int>& into)
	{
		if (body <= 0) return;
		AddChain(catalog, (*catalog)[body]->ParentIndex, into);
		if (!into.Contains(body)) into.Add(body);
	}

	DVec3 ScaleModel::TransitScene(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, const cs::Ref<Ship>& ship, double progress, DVec3& travelDirection) const
	{
		auto catalog = ephemeris->Catalog;
		cs::List<int> originLast;
		AddChain(catalog, ship->Orbit->Body, originLast);
		AddChain(catalog, ship->OriginBody, originLast);
		MoveLast(originLast, ship->OriginBody);
		cs::List<int> destinationLast;
		AddChain(catalog, ship->OriginBody, destinationLast);
		AddChain(catalog, ship->Orbit->Body, destinationLast);
		MoveLast(destinationLast, ship->Orbit->Body);

		auto At = [&](double u)
		{
			auto helio = ephemeris->Helio(ship->FrameBody) + Fleet::TransitOffset(ship, u);
			auto leaving = MapThrough(ephemeris, scene, originLast, originLast.Count(), helio);
			auto arriving = MapThrough(ephemeris, scene, destinationLast, destinationLast.Count(), helio);
			return DVec3::Lerp(leaving, arriving, Astro::SmoothStep(0, 1, u));
		};

		auto here = At(progress);
		constexpr double step = 1e-4;
		auto ahead = progress + step <= 1 ? At(progress + step) - here : here - At(progress - step);
		travelDirection = ahead.LengthSquared() > 0 ? ahead.Normalized() : DVec3::UnitX;
		return here;
	}

	void ScaleModel::MoveLast(cs::List<int>& chain, int body)
	{
		if (body <= 0 || !chain.Remove(body)) return;
		chain.Add(body);
	}

	DVec3 ScaleModel::LaneScene(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, const cs::Ref<Hyperlanes>& lanes, const cs::Ref<Ship>& ship, double progress, DVec3& travelDirection) const
	{
		auto hop = ship->LaneLeg;
		auto aKm = ephemeris->Helio(hop.From) + ship->LaneFromOffset;
		auto bKm = ephemeris->Helio(hop.To) + ship->LaneToOffset;
		return LaneCurveScene(ephemeris, scene, lanes, hop.Lane, hop.From, aKm, hop.To, bKm, progress, travelDirection, ship->LaneStar, ship->LaneShape);
	}

	DVec3 ScaleModel::LaneCurveScene(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, const cs::Ref<Hyperlanes>& lanes, const cs::Ref<Lane>& lane, int from, DVec3 aKm, int to, DVec3 bKm,
		double progress, DVec3& travelDirection, int star, int shape) const
	{
		auto a = MapNear(ephemeris, scene, from, aKm);
		auto b = MapNear(ephemeris, scene, to, bKm);
		if (star < 0) star = lanes->KeepOutStar(aKm, bKm, ephemeris->Jd);
		if (shape < 0) shape = lanes->ShapeOf(lane, aKm, bKm, star, ephemeris->Jd);
		double keepOut = LocalDistance((*ephemeris->Catalog)[star], lanes->KeepOutKm(star));
		auto up = DVec3::UnitY;
		travelDirection = Hyperlanes::CurveTangent(a, b, lane->Bend, scene[lane->Hub], scene[star], keepOut, progress, up, shape);
		return Hyperlanes::Curve(a, b, lane->Bend, scene[lane->Hub], scene[star], keepOut, progress, up, shape);
	}

	double ScaleModel::SceneSoi(const cs::Ref<BodyDefinition>& body) const
	{
		return cs::Double::IsInfinity(body->SoiKm) ? cs::Double::PositiveInfinity : LocalDistance(body, body->SoiKm);
	}
}
