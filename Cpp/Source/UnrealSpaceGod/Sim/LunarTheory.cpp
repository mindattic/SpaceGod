#include "Sim/LunarTheory.h"
#include "Sim/Astro.h"

namespace
{
	// D, M, M', F multipliers; longitude (1e-6 deg) and distance (1e-3 km) coefficients.
	const int32 LrArgs[60][4] =
	{
		{ 0, 0, 1, 0 }, { 2, 0, -1, 0 }, { 2, 0, 0, 0 }, { 0, 0, 2, 0 }, { 0, 1, 0, 0 }, { 0, 0, 0, 2 }, { 2, 0, -2, 0 },
		{ 2, -1, -1, 0 }, { 2, 0, 1, 0 }, { 2, -1, 0, 0 }, { 0, 1, -1, 0 }, { 1, 0, 0, 0 }, { 0, 1, 1, 0 }, { 2, 0, 0, -2 },
		{ 0, 0, 1, 2 }, { 0, 0, 1, -2 }, { 4, 0, -1, 0 }, { 0, 0, 3, 0 }, { 4, 0, -2, 0 }, { 2, 1, -1, 0 }, { 2, 1, 0, 0 },
		{ 1, 0, -1, 0 }, { 1, 1, 0, 0 }, { 2, -1, 1, 0 }, { 2, 0, 2, 0 }, { 4, 0, 0, 0 }, { 2, 0, -3, 0 }, { 0, 1, -2, 0 },
		{ 2, 0, -1, 2 }, { 2, -1, -2, 0 }, { 1, 0, 1, 0 }, { 2, -2, 0, 0 }, { 0, 1, 2, 0 }, { 0, 2, 0, 0 }, { 2, -2, -1, 0 },
		{ 2, 0, 1, -2 }, { 2, 0, 0, 2 }, { 4, -1, -1, 0 }, { 0, 0, 2, 2 }, { 3, 0, -1, 0 }, { 2, 1, 1, 0 }, { 4, -1, -2, 0 },
		{ 0, 2, -1, 0 }, { 2, 2, -1, 0 }, { 2, 1, -2, 0 }, { 2, -1, 0, -2 }, { 4, 0, 1, 0 }, { 0, 0, 4, 0 }, { 4, -1, 0, 0 },
		{ 1, 0, -2, 0 }, { 2, 1, 0, -2 }, { 0, 0, 2, -2 }, { 1, 1, 1, 0 }, { 3, 0, -2, 0 }, { 4, 0, -3, 0 }, { 2, -1, 2, 0 },
		{ 0, 2, 1, 0 }, { 1, 1, -1, 0 }, { 2, 0, 3, 0 }, { 2, 0, -1, -2 },
	};

	const double Lcoef[60] =
	{
		6288774, 1274027, 658314, 213618, -185116, -114332, 58793, 57066, 53322, 45758, -40923, -34720, -30383, 15327,
		-12528, 10980, 10675, 10034, 8548, -7888, -6766, -5163, 4987, 4036, 3994, 3861, 3665, -2689, -2602, 2390, -2348,
		2236, -2120, -2069, 2048, -1773, -1595, 1215, -1110, -892, -810, 759, -713, -700, 691, 596, 549, 537, 520, -487,
		-399, -381, 351, -340, 330, 327, -323, 299, 294, 0,
	};

	const double Rcoef[60] =
	{
		-20905355, -3699111, -2955968, -569925, 48888, -3149, 246158, -152138, -170733, -204586, -129620, 108743, 104755,
		10321, 0, 79661, -34782, -23210, -21636, 24208, 30824, -8379, -16675, -12831, -10445, -11650, 14403, -7003, 0,
		10056, 6322, -9884, 5751, 0, -4950, 4130, 0, -3958, 0, 3258, 2616, -1897, -2117, 2354, 0, 0, -1423, -1117, -1571,
		-1739, 0, -4421, 0, 0, 0, 0, 1165, 0, 0, 8752,
	};

	// Latitude (1e-6 deg).
	const int32 BArgs[30][4] =
	{
		{ 0, 0, 0, 1 }, { 0, 0, 1, 1 }, { 0, 0, 1, -1 }, { 2, 0, 0, -1 }, { 2, 0, -1, 1 }, { 2, 0, -1, -1 }, { 2, 0, 0, 1 },
		{ 0, 0, 2, 1 }, { 2, 0, 1, -1 }, { 0, 0, 2, -1 }, { 2, -1, 0, -1 }, { 2, 0, -2, -1 }, { 2, 0, 1, 1 }, { 2, 1, 0, -1 },
		{ 2, -1, -1, 1 }, { 2, -1, 0, 1 }, { 2, -1, -1, -1 }, { 0, 1, -1, -1 }, { 4, 0, -1, -1 }, { 0, 1, 0, 1 }, { 0, 0, 0, 3 },
		{ 0, 1, -1, 1 }, { 1, 0, 0, 1 }, { 0, 1, 1, 1 }, { 0, 1, 1, -1 }, { 0, 1, 0, -1 }, { 1, 0, 0, -1 }, { 0, 0, 3, 1 },
		{ 4, 0, 0, -1 }, { 4, 0, -1, 1 },
	};

	const double Bcoef[30] =
	{
		5128122, 280602, 277693, 173237, 55413, 46271, 32573, 17198, 9266, 8822, 8216, 4324, 4200, -3359, 2463, 2211, 2065,
		-1870, 1828, -1794, -1749, -1565, -1491, -1475, -1410, -1344, -1335, 1107, 1021, 833,
	};

	double R(double Deg) { return Deg * Astro::Deg2Rad; }

	double Eccentricity(int32 Cm, double E)
	{
		const int32 A = FMath::Abs(Cm);
		return A == 1 ? E : A == 2 ? E * E : 1.0;
	}
}

FVector LunarTheory::Position(double Jd)
{
	const double T = (Jd - Astro::J2000) / Astro::DaysPerCentury;
	const double T2 = T * T, T3 = T2 * T, T4 = T3 * T;
	const double Lp = 218.3164477 + 481267.88123421 * T - 0.0015786 * T2 + T3 / 538841 - T4 / 65194000;
	const double D = 297.8501921 + 445267.1114034 * T - 0.0018819 * T2 + T3 / 545868 - T4 / 113065000;
	const double M = 357.5291092 + 35999.0502909 * T - 0.0001536 * T2 + T3 / 24490000;
	const double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T2 + T3 / 69699 - T4 / 14712000;
	const double F = 93.2720950 + 483202.0175233 * T - 0.0036539 * T2 - T3 / 3526000 + T4 / 863310000;
	const double A1 = 119.75 + 131.849 * T, A2 = 53.09 + 479264.290 * T, A3 = 313.45 + 481266.484 * T;
	const double E = 1 - 0.002516 * T - 0.0000074 * T2;

	double Sl = 0, Sr = 0, Sb = 0;
	for (int32 I = 0; I < 60; ++I)
	{
		const int32* C = LrArgs[I];
		const double Arg = R(C[0] * D + C[1] * M + C[2] * Mp + C[3] * F);
		const double Ef = Eccentricity(C[1], E);
		Sl += Lcoef[I] * Ef * std::sin(Arg);
		Sr += Rcoef[I] * Ef * std::cos(Arg);
	}
	for (int32 I = 0; I < 30; ++I)
	{
		const int32* C = BArgs[I];
		const double Arg = R(C[0] * D + C[1] * M + C[2] * Mp + C[3] * F);
		Sb += Bcoef[I] * Eccentricity(C[1], E) * std::sin(Arg);
	}
	Sl += 3958 * std::sin(R(A1)) + 1962 * std::sin(R(Lp - F)) + 318 * std::sin(R(A2));
	Sb += -2235 * std::sin(R(Lp)) + 382 * std::sin(R(A3)) + 175 * std::sin(R(A1 - F)) + 175 * std::sin(R(A1 + F))
		+ 127 * std::sin(R(Lp - Mp)) - 115 * std::sin(R(Lp + Mp));

	const double Lambda = Lp + Sl / 1e6; // mean ecliptic and equinox of date
	const double Beta = Sb / 1e6;
	const double Distance = 385000.56 + Sr / 1000;

	// Of date to J2000: undo general precession in longitude, then the slow tilt of the ecliptic (Lieske 1977).
	const double PA = (5029.0966 * T + 1.11113 * T2) / 3600;
	const double PiA = (47.0029 * T - 0.03302 * T2) / 3600 * Astro::Deg2Rad;
	const double BigPi = (174.876384 * 3600 - 869.8089 * T + 0.03536 * T2) / 3600;
	const double Lon = R(Lambda - PA - BigPi), Lat = R(Beta);
	const double X = std::cos(Lat) * std::cos(Lon), Y = std::cos(Lat) * std::sin(Lon), Z = std::sin(Lat);
	const double Y2 = Y * std::cos(PiA) - Z * std::sin(PiA);
	const double Z2 = Y * std::sin(PiA) + Z * std::cos(PiA);
	const double Bp = R(BigPi);
	const double X3 = X * std::cos(Bp) - Y2 * std::sin(Bp);
	const double Y3 = X * std::sin(Bp) + Y2 * std::cos(Bp);
	return FVector(X3, Y3, Z2) * Distance;
}
