// Port of Unity.SpaceGod Core/GalacticPosition.cs (see docs/MIGRATION.md).

#include "GalacticPosition.h"

namespace SpaceGod::Core
{
	GalacticPosition::GalacticPosition(long long x, long long y, long long z, DVec3 localKm)
	{
		long long cx, cy, cz;
		double lx, ly, lz;
		Carry(localKm.X, cx, lx);
		Carry(localKm.Y, cy, ly);
		Carry(localKm.Z, cz, lz);
		X = x + cx;
		Y = y + cy;
		Z = z + cz;
		LocalKm = DVec3(lx, ly, lz);
	}

	void GalacticPosition::Carry(double km, long long& sectors, double& rest)
	{
		if (cs::Double::IsNaN(km) || cs::Double::IsInfinity(km)) throw cs::ArgumentOutOfRangeException(cs::Cat("km: Not a place: ", km));
		double s = cs::Math::Round(km / SectorKm, cs::Math::MidpointRounding::AwayFromZero);
		sectors = cs::ToLong(s);
		rest = km - s * SectorKm;
	}

	GalacticPosition GalacticPosition::FromLy(DVec3 ly)
	{
		// Whole sectors first, from the light-years, so the offset is taken off a small number, not a huge one.
		auto Split = [](double v, long long& sector, double& km)
		{
			double sectors = v * SectorsPerLy;
			double whole = cs::Math::Round(sectors, cs::Math::MidpointRounding::AwayFromZero);
			sector = cs::ToLong(whole);
			km = (sectors - whole) * SectorKm;
		};

		long long x, y, z;
		double kx, ky, kz;
		Split(ly.X, x, kx);
		Split(ly.Y, y, ky);
		Split(ly.Z, z, kz);
		return GalacticPosition(x, y, z, DVec3(kx, ky, kz));
	}

	size_t GalacticPosition::HashCode() const
	{
		// C#: unchecked h = X.GetHashCode(); h = h * 397 ^ Y...; only lookup uses it.
		size_t h = std::hash<long long>{}(X);
		h = h * 397 ^ std::hash<long long>{}(Y);
		h = h * 397 ^ std::hash<long long>{}(Z);
		return h * 397 ^ LocalKm.HashCode();
	}

	cs::String GalacticPosition::ToString() const
	{
		auto ly = Ly();
		return cs::Cat("[", X, ",", Y, ",", Z, "]+(", cs::Fmt(LocalKm.X, "0.###"), ",", cs::Fmt(LocalKm.Y, "0.###"), ",", cs::Fmt(LocalKm.Z, "0.###"),
			") km (", cs::Fmt(ly.X, "0.###"), ", ", cs::Fmt(ly.Y, "0.###"), ", ", cs::Fmt(ly.Z, "0.###"), " ly)");
	}
}
