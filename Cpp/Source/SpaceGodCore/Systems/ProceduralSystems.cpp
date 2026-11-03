// Port of Assets/UnitySpace/Core/Systems/ProceduralSystems.cs (US-T17).

#include "Systems/ProceduralSystems.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/StarSystemBuilder.h"
#include "Systems/FrontierSystems.h"
#include "Systems/NearbySystems.h"
#include "OrbitalElements.h"

namespace SpaceGod::Core
{
	// ---------------------------------------------------------------- GalacticFrame

	DVec3 GalacticFrame::InPlane(DVec3 v) { return (v - North * DVec3::Dot(v, North)).Normalized(); }

	DVec3 GalacticFrame::CentreLy() { return ToCentre * SunToCentreLy - North * SunHeightLy; }

	void GalacticFrame::Cylindrical(DVec3 ly, double& radiusLy, double& heightLy)
	{
		auto q = ly - CentreLy();
		heightLy = DVec3::Dot(q, North);
		radiusLy = (q - North * heightLy).Length();
	}

	double GalacticFrame::Density(DVec3 ly)
	{
		double r, z;
		Cylindrical(ly, r, z);
		double disc = LocalDensity * cs::Math::Exp(-(r - SunToCentreLy) / ScaleLengthLy) * cs::Math::Exp(-(cs::Math::Abs(z) - SunHeightLy) / ScaleHeightLy);
		if (r > DiscRadiusLy) disc *= cs::Math::Exp(-(r - DiscRadiusLy) / EdgeFadeLy);
		double sphere = cs::Math::Sqrt(r * r + z * z);
		return disc + BulgeDensity * cs::Math::Exp(-sphere / BulgeScaleLy);
	}

	// ---------------------------------------------------------------- SeededRandom

	int SeededRandom::Poisson(double mean)
	{
		if (!(mean > 0)) return 0;
		if (mean > 30)
		{
			double u1 = cs::Math::Max(1e-300, NextDouble()), u2 = NextDouble();
			double n = cs::Math::Sqrt(-2 * cs::Math::Log(u1)) * cs::Math::Cos(2 * cs::Math::PI * u2);
			return cs::Math::Max(0, cs::ToInt(cs::Math::Round(mean + n * cs::Math::Sqrt(mean))));
		}
		double l = cs::Math::Exp(-mean), p = 1;
		int k = 0;
		do
		{
			k++;
			p *= NextDouble();
		} while (p > l);
		return k - 1;
	}

	uint64_t SeededRandom::MixParts(std::initializer_list<long long> parts)
	{
		uint64_t h = 0x243F6A8885A308D3ULL;
		for (long long p : parts)
		{
			h ^= static_cast<uint64_t>(p); // unchecked((ulong)p)
			h = SeededRandom(h).NextULong();
		}
		return h;
	}

	// ---------------------------------------------------------------- ProceduralSystems

	cs::List<cs::Ref<StarEntry>> ProceduralSystems::StarsNear(DVec3 centreLy, double radiusLy, int seed)
	{
		cs::List<cs::Ref<StarEntry>> r;
		long long x0 = Cell(centreLy.X - radiusLy), x1 = Cell(centreLy.X + radiusLy);
		long long y0 = Cell(centreLy.Y - radiusLy), y1 = Cell(centreLy.Y + radiusLy);
		long long z0 = Cell(centreLy.Z - radiusLy), z1 = Cell(centreLy.Z + radiusLy);
		for (long long x = x0; x <= x1; x++)
		for (long long y = y0; y <= y1; y++)
		for (long long z = z0; z <= z1; z++)
		{
			// A cell wholly out of reach is skipped without being made.
			auto near = DVec3(
				cs::Math::Max(static_cast<double>(x) * CellLy, cs::Math::Min(centreLy.X, static_cast<double>(x + 1) * CellLy)),
				cs::Math::Max(static_cast<double>(y) * CellLy, cs::Math::Min(centreLy.Y, static_cast<double>(y + 1) * CellLy)),
				cs::Math::Max(static_cast<double>(z) * CellLy, cs::Math::Min(centreLy.Z, static_cast<double>(z + 1) * CellLy)));
			if ((near - centreLy).Length() > radiusLy) continue;
			for (const auto& e : StarsInCell(seed, x, y, z))
				if ((e->PositionLy - centreLy).Length() <= radiusLy) r.Add(e);
		}
		return r;
	}

	cs::List<cs::Ref<StarEntry>> ProceduralSystems::StarsInCell(int seed, long long x, long long y, long long z)
	{
		auto rng = SeededRandom(SeededRandom::Mix(seed, x, y, z));
		auto centre = DVec3((static_cast<double>(x) + 0.5) * CellLy, (static_cast<double>(y) + 0.5) * CellLy, (static_cast<double>(z) + 0.5) * CellLy);
		int n = cs::Math::Min(MaxPerCell, rng.Poisson(GalacticFrame::Density(centre) * CellLy * CellLy * CellLy));
		cs::List<cs::Ref<StarEntry>> list;
		for (int i = 0; i < n; i++)
		{
			// C# evaluates the three NextDouble calls left to right; C++ argument order is unspecified, so draw them first.
			double rx = rng.NextDouble();
			double ry = rng.NextDouble();
			double rz = rng.NextDouble();
			auto at = DVec3((static_cast<double>(x) + rx) * CellLy, (static_cast<double>(y) + ry) * CellLy, (static_cast<double>(z) + rz) * CellLy);
			uint64_t starSeed = rng.NextULong();
			auto e = Star(starSeed);
			e->PositionLy = at;
			e->Name = Designation(starSeed);
			e->Source = SourceName;
			e->Seed = starSeed;
			list.Add(e);
		}
		return list;
	}

	cs::String ProceduralSystems::Designation(uint64_t starSeed)
	{
		const cs::String digits = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";
		char c[9] = {};
		uint64_t v = starSeed;
		for (int i = 0; i < 8; i++)
		{
			c[i < 4 ? i : i + 1] = digits[static_cast<size_t>(v % static_cast<uint64_t>(digits.size()))];
			v /= static_cast<uint64_t>(digits.size());
		}
		c[4] = '-';
		return cs::String(Prefix) + " " + cs::String(c, 9);
	}

	// ---------------------------------------------------------------- stars

	const cs::List<ProceduralSystems::StarClass>& ProceduralSystems::Classes()
	{
		static const cs::List<StarClass> classes
		{
			{ "M", 0.73, 0.08, 0.45, 2400, 3850 },
			{ "K", 0.12, 0.45, 0.8, 3900, 5250 },
			{ "G", 0.07, 0.8, 1.04, 5300, 5950 },
			{ "F", 0.03, 1.04, 1.4, 6000, 7300 },
			{ "A", 0.006, 1.4, 2.1, 7500, 10000 },
			{ "B", 0.0013, 2.1, 10, 10500, 25000 },
			{ "D", 0.05, 0.5, 0.8, 5000, 20000 },
			{ "L", 0.0127, 0.03, 0.075, 600, 2200 },
		};
		return classes;
	}

	cs::Ref<StarEntry> ProceduralSystems::Star(uint64_t starSeed)
	{
		auto rng = SeededRandom(starSeed);
		double pick = rng.NextDouble(), sum = 0;
		auto k = Classes()[0];
		for (const auto& c : Classes())
		{
			sum += c.share;
			k = c;
			if (pick < sum) break;
		}
		double f = rng.NextDouble();
		double mass = k.m0 + (k.m1 - k.m0) * f;
		double teff = k.t0 + (k.t1 - k.t0) * f;
		double radius;
		cs::String spectral;
		if (k.cls == "D")
		{
			// A white dwarf: Earth-sized, its temperature how long it has been cooling, not its mass.
			teff = rng.Range(k.t0, k.t1);
			radius = 0.012;
			spectral = cs::Cat("DA", cs::Math::Max(1, cs::Math::Min(9, cs::ToInt(cs::Math::Round(50400 / teff)))));
		}
		else if (k.cls == "L")
		{
			radius = 0.1;
			spectral = cs::Cat(teff < 1300 ? "T" : "L", cs::ToInt(cs::Math::Floor((1 - f) * 9.99)));
		}
		else
		{
			radius = SystemSources::MainSequenceRadius(mass);
			spectral = cs::Cat(k.cls, cs::ToInt(cs::Math::Floor((1 - f) * 9.99)), "V");
		}
		auto e = cs::New<StarEntry>();
		e->Spectral = spectral; e->MassSolar = mass; e->RadiusSolar = radius; e->TeffK = teff;
		e->LuminositySolar = FrontierSystems::LuminosityOf(radius, teff);
		return e;
	}

	// ---------------------------------------------------------------- systems

	cs::Ref<SystemSpec> ProceduralSystems::Define(const cs::Ref<StarEntry>& star, int seed)
	{
		if (star == nullptr) return nullptr;
		auto made = Star(star->Seed);
		cs::String name = !star->Name.empty() ? star->Name : Designation(star->Seed);
		auto rng = SeededRandom(SeededRandom::Mix(seed, static_cast<long long>(star->Seed), 7));
		auto spec = cs::New<StarSpec>();
		spec->Name = name; spec->Spectral = made->Spectral; spec->MassSolar = made->MassSolar; spec->RadiusSolar = made->RadiusSolar;
		spec->LuminositySolar = made->LuminositySolar; spec->TeffK = made->TeffK;
		spec->RotationDays = cs::Strings::StartsWith(made->Spectral, "D") ? rng.Range(0.1, 2) : rng.Range(1, 45);
		spec->Summary = cs::Cat("A ", Kind(made->Spectral), ", ", Format(made->MassSolar, "0.00"), " solar masses, made up from the galaxy's seed. Fiction.");
		auto entry = cs::New<StarEntry>();
		entry->Name = name; entry->PositionLy = star->PositionLy;
		auto system = SystemSources::LoneStar(entry, spec, cs::String());
		system->ReferenceIDeg = cs::Math::Acos(rng.Range(-1, 1)) * Astro::Rad2Deg;
		system->ReferenceNodeDeg = rng.Range(0, 360);

		double starAu = made->RadiusSolar * StarSystemBuilder::SunRadiusKm / Astro::AuKm;
		int planets = rng.Poisson(cs::Strings::StartsWith(made->Spectral, "D") ? 0.6
			: made->MassSolar < 0.08 ? 0.5 : made->MassSolar > 2 ? 1.5 : 2.6);
		double a = cs::Math::Max(starAu * 8, rng.LogRange(0.02, 0.25) * cs::Math::Max(0.15, cs::Math::Sqrt(made->LuminositySolar)));
		for (int i = 0; i < planets && a < 200; i++)
		{
			double roll = rng.NextDouble();
			double massEarth = roll < 0.6 ? rng.LogRange(0.1, 5) : roll < 0.85 ? rng.LogRange(5, 50) : rng.LogRange(50, 3000);
			// Left to right, as C# evaluates them.
			double e1 = rng.NextDouble();
			double e2 = rng.NextDouble();
			double e = cs::Math::Min(0.6, e1 * e2 * 0.5);
			auto p = cs::New<PlanetSpec>();
			p->Name = name + " " + static_cast<char>('b' + i); p->Host = name; p->AAu = a; p->E = e; p->MassEarth = massEarth;
			p->PeriodDays = OrbitalElements::PeriodFromGm(a * Astro::AuKm, StarSystemBuilder::SunGm * made->MassSolar);
			p->RadiusEarth = NearbySystems::RadiusFromMass(massEarth); p->MassIsMinimum = false; p->Status = BodyStatus::Fictional;
			// The initialiser's order: MeanAnomalyDeg drawn before ArgPeriDeg.
			p->MeanAnomalyDeg = rng.Range(0, 360); p->ArgPeriDeg = rng.Range(0, 360);
			p->Source = "Generated from the galaxy's seed";
			p->Summary = cs::Cat(Format(massEarth, "0.0"), " Earth masses, ", Format(a, "0.###"), " AU out. Made up from the galaxy's seed. Fiction.");
			system->Planets.Add(p);
			a *= rng.Range(1.4, 3.0);
		}
		if (rng.NextDouble() < 0.15)
		{
			double inner = cs::Math::Max(a, rng.LogRange(2, 40) * cs::Math::Max(0.3, cs::Math::Sqrt(made->LuminositySolar)));
			auto d = cs::New<DiscSpec>();
			d->Name = name + " Belt"; d->Host = name; d->InnerAu = inner; d->OuterAu = inner * rng.Range(1.2, 2.0);
			d->Source = "Generated from the galaxy's seed"; d->Summary = "A ring of rock and ice past the planets. Fiction.";
			system->Discs.Add(d);
		}
		system->Summary = planets == 0 ? cs::Cat("A lone ", Kind(made->Spectral), " with no planets. Fiction, from the galaxy's seed.")
			: cs::Cat("A ", Kind(made->Spectral), " with ", planets, " planet", (planets == 1 ? "" : "s"), ". Fiction, from the galaxy's seed.");
		return system;
	}

	cs::String ProceduralSystems::Kind(const cs::String& spectral)
	{
		switch (spectral.at(0))
		{
		case 'M': return "red dwarf";
		case 'K': return "orange dwarf";
		case 'G': return "yellow dwarf";
		case 'F': return "yellow-white star";
		case 'A': return "white star";
		case 'B': return "blue-white star";
		case 'D': return "white dwarf";
		default: return "brown dwarf";
		}
	}
}
