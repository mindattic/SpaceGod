// Port of Unity.SpaceGod Core/Kepler.cs (see docs/MIGRATION.md).

#include "Kepler.h"

namespace SpaceGod::Core
{
	double Kepler::SolveEccentricAnomaly(double meanAnomaly, double e)
	{
		double m = Astro::WrapRadians(meanAnomaly);
		double ecc = e < 0.8 ? m + e * cs::Math::Sin(m) : (m >= 0 ? cs::Math::PI : -cs::Math::PI);
		for (int iter = 0; iter < 60; iter++)
		{
			double f = ecc - e * cs::Math::Sin(ecc) - m;
			double fp = 1 - e * cs::Math::Cos(ecc);
			double delta = f / fp;
			ecc -= delta;
			if (cs::Math::Abs(delta) < 1e-14) break;
		}
		return ecc;
	}

	DVec3 Kepler::Position(const KeplerState& k)
	{
		double ecc = SolveEccentricAnomaly(k.MeanAnomaly, k.E);
		double xp = k.A * (cs::Math::Cos(ecc) - k.E);
		double yp = k.A * cs::Math::Sqrt(1 - k.E * k.E) * cs::Math::Sin(ecc);
		return Rotate(k, xp, yp);
	}

	DVec3 Kepler::Velocity(const KeplerState& k)
	{
		double ecc = SolveEccentricAnomaly(k.MeanAnomaly, k.E);
		double eDot = k.MeanMotion / (1 - k.E * cs::Math::Cos(ecc));
		double vxp = -k.A * cs::Math::Sin(ecc) * eDot;
		double vyp = k.A * cs::Math::Sqrt(1 - k.E * k.E) * cs::Math::Cos(ecc) * eDot;
		return Rotate(k, vxp, vyp);
	}

	DVec3 Kepler::Rotate(const KeplerState& k, double xp, double yp)
	{
		double cw = cs::Math::Cos(k.ArgPeri), sw = cs::Math::Sin(k.ArgPeri);
		double cn = cs::Math::Cos(k.Node), sn = cs::Math::Sin(k.Node);
		double ci = cs::Math::Cos(k.I), si = cs::Math::Sin(k.I);
		double x = (cw * cn - sw * sn * ci) * xp + (-sw * cn - cw * sn * ci) * yp;
		double y = (cw * sn + sw * cn * ci) * xp + (-sw * sn + cw * cn * ci) * yp;
		double z = sw * si * xp + cw * si * yp;
		return DVec3(x, y, z);
	}
}
