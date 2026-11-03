#include "Sim/Orbits.h"
#include "Sim/Astro.h"

FOrbitalElements FOrbitalElements::FromJpl(double AAu, double E, double IDeg, double LDeg, double VarpiDeg, double NodeDeg,
	double ADotAu, double EDot, double IDot, double LDot, double VarpiDot, double NodeDot)
{
	FOrbitalElements O;
	O.EpochJd = Astro::J2000;
	O.AKm = AAu * Astro::AuKm;
	O.ADotKmPerCentury = ADotAu * Astro::AuKm;
	O.E = E;
	O.EDot = EDot;
	O.IDeg = IDeg;
	O.IDot = IDot;
	O.LDeg = LDeg;
	O.LDotDegPerCentury = LDot;
	O.VarpiDeg = VarpiDeg;
	O.VarpiDot = VarpiDot;
	O.NodeDeg = NodeDeg;
	O.NodeDot = NodeDot;
	return O;
}

FOrbitalElements FOrbitalElements::FromClassical(double EpochJd, double AKm, double E, double IDeg, double NodeDeg, double ArgPeriDeg,
	double MeanAnomalyDeg, double PeriodDays, double NodeRateDegPerDay, double VarpiRateDegPerDay, double MeanLongitudeAccelDegPerYear2)
{
	const double Varpi = NodeDeg + ArgPeriDeg;
	FOrbitalElements O;
	O.LDotDotDegPerCentury2 = MeanLongitudeAccelDegPerYear2 * 1e4;
	O.EpochJd = EpochJd;
	O.AKm = AKm;
	O.E = E;
	O.IDeg = IDeg;
	O.LDeg = MeanAnomalyDeg + Varpi;
	O.LDotDegPerCentury = 360.0 / PeriodDays * Astro::DaysPerCentury;
	O.VarpiDeg = Varpi;
	O.VarpiDot = VarpiRateDegPerDay * Astro::DaysPerCentury;
	O.NodeDeg = NodeDeg;
	O.NodeDot = NodeRateDegPerDay * Astro::DaysPerCentury;
	return O;
}

double FOrbitalElements::PeriodFromGm(double AKm, double Gm)
{
	return Astro::TwoPi * std::sqrt(AKm * AKm * AKm / Gm) / Astro::SecondsPerDay;
}

FKeplerState FOrbitalElements::At(double Jd) const
{
	const double T = (Jd - EpochJd) / Astro::DaysPerCentury;
	FKeplerState K;
	K.A = AKm + ADotKmPerCentury * T;
	K.E = FMath::Clamp(E + EDot * T, 0.0, 0.99);
	K.I = (IDeg + IDot * T) * Astro::Deg2Rad;
	const double L = LDeg + LDotDegPerCentury * T + LDotDotDegPerCentury2 * T * T;
	const double Varpi = VarpiDeg + VarpiDot * T;
	const double Node = NodeDeg + NodeDot * T;
	K.MeanAnomaly = Astro::WrapRadians((L - Varpi) * Astro::Deg2Rad);
	// Mean motion is dM/dt = dL/dt - d(varpi)/dt, not the mean-longitude rate alone.
	K.MeanMotion = (LDotDegPerCentury + 2 * LDotDotDegPerCentury2 * T - VarpiDot) / Astro::DaysPerCentury * Astro::Deg2Rad;
	K.Node = Node * Astro::Deg2Rad;
	K.ArgPeri = (Varpi - Node) * Astro::Deg2Rad;
	return K;
}

namespace Kepler
{
	double SolveEccentricAnomaly(double MeanAnomaly, double E)
	{
		const double M = Astro::WrapRadians(MeanAnomaly);
		double Ecc = E < 0.8 ? M + E * std::sin(M) : (M >= 0 ? Astro::Pi : -Astro::Pi);
		for (int32 Iter = 0; Iter < 60; ++Iter)
		{
			const double F = Ecc - E * std::sin(Ecc) - M;
			const double Fp = 1 - E * std::cos(Ecc);
			const double Delta = F / Fp;
			Ecc -= Delta;
			if (std::abs(Delta) < 1e-14) break;
		}
		return Ecc;
	}

	static FVector Rotate(const FKeplerState& K, double Xp, double Yp)
	{
		const double Cw = std::cos(K.ArgPeri), Sw = std::sin(K.ArgPeri);
		const double Cn = std::cos(K.Node), Sn = std::sin(K.Node);
		const double Ci = std::cos(K.I), Si = std::sin(K.I);
		return FVector(
			(Cw * Cn - Sw * Sn * Ci) * Xp + (-Sw * Cn - Cw * Sn * Ci) * Yp,
			(Cw * Sn + Sw * Cn * Ci) * Xp + (-Sw * Sn + Cw * Cn * Ci) * Yp,
			Sw * Si * Xp + Cw * Si * Yp);
	}

	FVector Position(const FKeplerState& K)
	{
		const double Ecc = SolveEccentricAnomaly(K.MeanAnomaly, K.E);
		const double Xp = K.A * (std::cos(Ecc) - K.E);
		const double Yp = K.A * std::sqrt(1 - K.E * K.E) * std::sin(Ecc);
		return Rotate(K, Xp, Yp);
	}

	FVector Velocity(const FKeplerState& K)
	{
		const double Ecc = SolveEccentricAnomaly(K.MeanAnomaly, K.E);
		const double EDot = K.MeanMotion / (1 - K.E * std::cos(Ecc));
		const double Vxp = -K.A * std::sin(Ecc) * EDot;
		const double Vyp = K.A * std::sqrt(1 - K.E * K.E) * std::cos(Ecc) * EDot;
		return Rotate(K, Vxp, Vyp);
	}

	double VisVivaSpeed(double Gm, double RKm, double AKm)
	{
		return std::sqrt(FMath::Max(0.0, Gm * (2.0 / RKm - 1.0 / AKm)));
	}
}
