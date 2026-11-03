// Port of Unity.SpaceGod Core/Ephemeris.cs (see docs/MIGRATION.md).

#include "Ephemeris.h"
#include "BodyCatalog.h"
#include "Kepler.h"
#include "LunarTheory.h"
#include "OrbitalElements.h"

namespace SpaceGod::Core
{
	const Ephemeris::FrameBasis& Ephemeris::FrameBasis::Ecliptic()
	{
		static const FrameBasis value(DVec3::UnitX, DVec3::UnitY, DVec3::UnitZ);
		return value;
	}

	Ephemeris::FrameBasis Ephemeris::FrameBasis::FromPole(DVec3 pole)
	{
		auto z = pole.Normalized();
		auto node = DVec3::Cross(DVec3::UnitZ, z);
		auto x = node.LengthSquared() < 1e-12 ? DVec3::UnitX : node.Normalized();
		auto y = DVec3::Cross(z, x);
		return FrameBasis(x, y, z);
	}

	Ephemeris::Ephemeris(cs::Ref<BodyCatalog> catalog)
	{
		Catalog = catalog;
		relative = cs::NewArray<DVec3>(catalog->Count());
		helio = cs::NewArray<DVec3>(catalog->Count());
		velocity = cs::NewArray<DVec3>(catalog->Count());
		frames = cs::NewArray<FrameBasis>(catalog->Count());
		memoJd = cs::NewArray<double>(catalog->Count());
		memoHelio = cs::NewArray<DVec3>(catalog->Count());
		for (int i = 0; i < memoJd.Count(); i++) memoJd[i] = cs::Double::NaN;

		for (int i = 0; i < catalog->Count(); i++)
		{
			auto b = (*catalog)[i];
			if (b->Plane != OrbitPlane::ParentEquator || b->ParentIndex < 0)
				frames[i] = FrameBasis::Ecliptic();
			else if (!cs::Double::IsNaN(b->LaplacePoleRaDeg))
				frames[i] = FrameBasis::FromPole(Astro::PoleFromRaDec(b->LaplacePoleRaDeg, b->LaplacePoleDecDeg));
			else
				frames[i] = FrameBasis::FromPole((*catalog)[b->ParentIndex]->SpinAxis());
		}

		cs::Ref<BodyDefinition> earth, moon;
		if (catalog->TryGet("Earth", earth) && catalog->TryGet("Moon", moon) && moon->ParentIndex == earth->Index)
		{
			earthIndex = earth->Index;
			moonIndex = moon->Index;
			moonMassFraction = moon->Gm / (earth->Gm + moon->Gm);
		}
	}

	void Ephemeris::Update(double jd)
	{
		Jd = jd;
		for (int i = 0; i < Catalog->Count(); i++)
		{
			auto b = (*Catalog)[i];
			if (b->Elements == nullptr)
			{
				relative[i] = DVec3::Zero;
				velocity[i] = DVec3::Zero;
				continue;
			}
			auto k = b->Elements->At(jd);
			relative[i] = frames[i].ToEcliptic(Kepler::Position(k)) * b->OrbitScale;
			velocity[i] = frames[i].ToEcliptic(Kepler::Velocity(k)) * b->OrbitScale;
		}

		// The Moon follows a lunar theory, not an ellipse (see LunarTheory).
		if (moonIndex >= 0)
		{
			relative[moonIndex] = Lunar(jd);
			constexpr double h = 1e-3;
			velocity[moonIndex] = (LunarTheory::Position(jd + h) - LunarTheory::Position(jd - h)) / (2 * h);
		}

		// JPL's "Earth" elements are the Earth-Moon barycentre; Earth sits 4,670 km from it.
		if (earthIndex >= 0)
		{
			relative[earthIndex] -= relative[moonIndex] * moonMassFraction;
			velocity[earthIndex] -= velocity[moonIndex] * moonMassFraction;
		}

		for (int i = 0; i < Catalog->Count(); i++)
		{
			int p = (*Catalog)[i]->ParentIndex;
			helio[i] = p < 0 ? relative[i] : helio[p] + relative[i];
		}
	}

	DVec3 Ephemeris::HelioAt(int body, double jd)
	{
		const auto& b = (*Catalog)[body];
		if (b->Elements == nullptr) return DVec3::Zero;
		if (memoJd[body] == jd) return memoHelio[body];
		auto rel = body == moonIndex ? Lunar(jd) : frames[body].ToEcliptic(Kepler::Position(b->Elements->At(jd))) * b->OrbitScale;
		if (body == earthIndex)
			rel -= RelativeAt(moonIndex, jd) * moonMassFraction;
		auto at = HelioAt(b->ParentIndex, jd) + rel;
		memoJd[body] = jd;
		memoHelio[body] = at;
		return at;
	}

	DVec3 Ephemeris::RelativeAt(int body, double jd)
	{
		return body == moonIndex ? Lunar(jd) : frames[body].ToEcliptic(Kepler::Position((*Catalog)[body]->Elements->At(jd))) * (*Catalog)[body]->OrbitScale;
	}

	DVec3 Ephemeris::Lunar(double jd)
	{
		if (lunarJd != jd)
		{
			lunarPos = LunarTheory::Position(jd);
			lunarJd = jd;
		}
		return lunarPos;
	}

	void Ephemeris::SampleOrbitTrail(int body, double jd, cs::List<DVec3>& output) const
	{
		auto b = (*Catalog)[body];
		auto k = b->Elements->At(jd);
		double now = Kepler::SolveEccentricAnomaly(k.MeanAnomaly, k.E);
		int n = output.Count();
		double sense = k.MeanMotion >= 0 ? 1 : -1;
		// The Moon (lunar theory) and Earth (Earth-Moon barycentre offset) are drawn off their
		// mean Kepler ellipse; shift the whole trail so it still meets the body where it is drawn.
		DVec3 offset = DVec3::Zero;
		if (body == moonIndex) offset = LunarTheory::Position(jd) - frames[body].ToEcliptic(Kepler::Position(k)) * b->OrbitScale;
		else if (body == earthIndex) offset = -LunarTheory::Position(jd) * moonMassFraction;
		for (int i = 0; i < n; i++)
		{
			double ecc = now - sense * Astro::TwoPi * i / cs::Math::Max(1, n - 1);
			double m = ecc - k.E * cs::Math::Sin(ecc);
			output[i] = frames[body].ToEcliptic(Kepler::Position(k.WithMeanAnomaly(m))) * b->OrbitScale + offset;
		}
	}

	double Ephemeris::OrbitalSpeedKmS(int body) const
	{
		auto b = (*Catalog)[body];
		if (b->ParentIndex < 0 || b->Elements == nullptr) return 0;
		if (b->OrbitScale != 1) return RelativeVelocityKmS(body).Length();
		double gm = (*Catalog)[b->ParentIndex]->Gm + b->Gm;
		return Kepler::VisVivaSpeed(gm, relative[body].Length(), b->Elements->At(Jd).A);
	}
}
