// Port of Unity.SpaceGod Core/Lagrange.cs (see docs/MIGRATION.md).

#include "Lagrange.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "Ephemeris.h"
#include "OrbitalElements.h"

namespace SpaceGod::Core
{
	const char* ToString(LagrangePoint v)
	{
		switch (v)
		{
		case LagrangePoint::None: return "None";
		case LagrangePoint::L1: return "L1";
		case LagrangePoint::L2: return "L2";
		case LagrangePoint::L3: return "L3";
		case LagrangePoint::L4: return "L4";
		case LagrangePoint::L5: return "L5";
		}
		return "";
	}

	double Lagrange::CollinearDistance(double mu, LagrangePoint point)
	{
		mu = cs::Math::Max(mu, 1e-15);
		double g;
		cs::Func<double, double> f, df;
		switch (point)
		{
		case LagrangePoint::L1:
			g = cs::Math::Pow(mu / 3, 1.0 / 3);
			f = [mu](double x) { return ((((x - (3 - mu)) * x + (3 - 2 * mu)) * x - mu) * x + 2 * mu) * x - mu; };
			df = [mu](double x) { return (((5 * x - 4 * (3 - mu)) * x + 3 * (3 - 2 * mu)) * x - 2 * mu) * x + 2 * mu; };
			break;
		case LagrangePoint::L2:
			g = cs::Math::Pow(mu / 3, 1.0 / 3);
			f = [mu](double x) { return ((((x + (3 - mu)) * x + (3 - 2 * mu)) * x - mu) * x - 2 * mu) * x - mu; };
			df = [mu](double x) { return (((5 * x + 4 * (3 - mu)) * x + 3 * (3 - 2 * mu)) * x - 2 * mu) * x - 2 * mu; };
			break;
		case LagrangePoint::L3:
			g = 1 - 7 * mu / 12;
			f = [mu](double x) { return ((((x + (2 + mu)) * x + (1 + 2 * mu)) * x - (1 - mu)) * x - 2 * (1 - mu)) * x - (1 - mu); };
			df = [mu](double x) { return (((5 * x + 4 * (2 + mu)) * x + 3 * (1 + 2 * mu)) * x - 2 * (1 - mu)) * x - 2 * (1 - mu); };
			break;
		default:
			return 0;
		}
		for (int i = 0; i < 50; i++)
		{
			double step = f(g) / df(g);
			g -= step;
			if (cs::Math::Abs(step) < 1e-15) break;
		}
		return g;
	}

	DVec3 Lagrange::Offset(DVec3 relative, DVec3 normal, double mu, LagrangePoint point)
	{
		switch (point)
		{
		case LagrangePoint::L1: return relative * (1 - CollinearDistance(mu, point));
		case LagrangePoint::L2: return relative * (1 + CollinearDistance(mu, point));
		case LagrangePoint::L3: return relative * -CollinearDistance(mu, point);
		case LagrangePoint::L4:
		case LagrangePoint::L5:
		{
			// Turned 60° about the orbit's pole: ahead for L4, behind for L5.
			auto n = normal.Normalized();
			auto along = DVec3::Cross(n, relative);
			double sign = point == LagrangePoint::L4 ? 1 : -1;
			return relative * 0.5 + along * (sign * Sin60);
		}
		default: return DVec3::Zero;
		}
	}

	bool Lagrange::HasPoints(const cs::Ref<BodyCatalog>& catalog, int secondary)
	{
		if (secondary < 0 || secondary >= catalog->Count()) return false;
		auto b = (*catalog)[secondary];
		if (b->ParentIndex < 0 || b->Elements == nullptr) return false;
		auto p = (*catalog)[b->ParentIndex];
		return p->Kind != BodyKind::Barycentre && p->Gm > 0;
	}

	cs::List<std::pair<int, int>> Lagrange::Pairs(const cs::Ref<BodyCatalog>& catalog)
	{
		cs::List<std::pair<int, int>> result;
		for (int i = 0; i < catalog->Count(); i++)
			if (HasPoints(catalog, i)) result.Add({ (*catalog)[i]->ParentIndex, i });
		return result;
	}

	DVec3 Lagrange::MassCentre(const cs::Ref<Ephemeris>& eph, int body, double jd, double& gm)
	{
		auto catalog = eph->Catalog;
		auto b = (*catalog)[body];
		gm = cs::Math::Max(b->Gm, 0.0);
		auto sum = eph->HelioAt(body, jd) * gm;
		if (b->ParentIndex >= 0 && (*catalog)[b->ParentIndex]->UsesStellarMapping())
			for (int c : catalog->ChildrenOf(body))
			{
				double m = cs::Math::Max((*catalog)[c]->Gm, 0.0);
				if (m <= 0) continue;
				sum += eph->HelioAt(c, jd) * m;
				gm += m;
			}
		return gm > 0 ? sum / gm : eph->HelioAt(body, jd);
	}

	DVec3 Lagrange::HelioAt(const cs::Ref<Ephemeris>& eph, int secondary, LagrangePoint point, double jd)
	{
		auto catalog = eph->Catalog;
		int primary = (*catalog)[secondary]->ParentIndex;
		auto p = eph->HelioAt(primary, jd);
		double gm2;
		auto s = MassCentre(eph, secondary, jd, gm2);
		constexpr double h = 1e-3;
		double discard;
		// C# evaluates left to right; keep each call in its own statement in that order (HelioAt memoises).
		auto m1 = MassCentre(eph, secondary, jd + h, discard);
		auto p1 = eph->HelioAt(primary, jd + h);
		auto m0 = MassCentre(eph, secondary, jd - h, discard);
		auto p0 = eph->HelioAt(primary, jd - h);
		auto motion = m1 - p1
			- (m0 - p0);
		auto rel = s - p;
		double mu = MassRatio((*catalog)[primary]->Gm, gm2);
		return p + Offset(rel, DVec3::Cross(rel, motion), mu, point);
	}

	DVec3 Lagrange::OffsetAt(const cs::Ref<Ephemeris>& eph, int secondary, LagrangePoint point, double jd)
	{
		auto at = HelioAt(eph, secondary, point, jd);
		return at - eph->HelioAt((*eph->Catalog)[secondary]->ParentIndex, jd);
	}

	int Lagrange::MappedNear(const cs::Ref<BodyCatalog>& catalog, int secondary, LagrangePoint point)
	{
		return point == LagrangePoint::L1 || point == LagrangePoint::L2 ? secondary : (*catalog)[secondary]->ParentIndex;
	}

	cs::String Lagrange::Name(const cs::Ref<BodyCatalog>& catalog, int secondary, LagrangePoint point)
	{
		return cs::Cat((*catalog)[(*catalog)[secondary]->ParentIndex]->Name, "-", (*catalog)[secondary]->Name, " ", point);
	}

	double Lagrange::LeadDeg(const cs::Ref<Ephemeris>& eph, int host, int body, double jd)
	{
		auto catalog = eph->Catalog;
		int star = (*catalog)[host]->ParentIndex;
		auto c = eph->HelioAt(star, jd);
		auto r = eph->HelioAt(host, jd) - c;
		constexpr double h = 1e-2;
		auto vp = eph->HelioAt(host, jd + h);
		auto vm = eph->HelioAt(host, jd - h);
		auto v = vp - vm;
		auto n = DVec3::Cross(r, v).Normalized();
		auto x = r.Normalized();
		auto y = DVec3::Cross(n, x);
		auto q = eph->HelioAt(body, jd) - c;
		return cs::Math::Atan2(DVec3::Dot(q, y), DVec3::Dot(q, x)) * Astro::Rad2Deg;
	}
}
