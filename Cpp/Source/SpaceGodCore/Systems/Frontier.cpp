// US-T449: the frontier (see Systems/Frontier.h).

#include "Systems/Frontier.h"

#include "Astro.h"
#include "BodyDefinition.h"
#include "OrbitalElements.h"
#include "Systems/FrontierSystems.h"
#include "Systems/Galaxy.h"
#include "Systems/NearbySystems.h"
#include "Systems/ProceduralSystems.h"
#include "Systems/SpaceFeatures.h"
#include "Systems/StarSystemBuilder.h"
#include "Systems/SystemSources.h"

namespace SpaceGod::Core
{
	const char* ToString(FrontierKind v)
	{
		switch (v)
		{
		case FrontierKind::None: return "None";
		case FrontierKind::Pulsar: return "Pulsar";
		case FrontierKind::BlackHole: return "BlackHole";
		case FrontierKind::WhiteDwarf: return "WhiteDwarf";
		case FrontierKind::BrownDwarfs: return "BrownDwarfs";
		case FrontierKind::RoguePlanet: return "RoguePlanet";
		case FrontierKind::Binary: return "Binary";
		case FrontierKind::Trinary: return "Trinary";
		case FrontierKind::Nebula: return "Nebula";
		case FrontierKind::CometCloud: return "CometCloud";
		case FrontierKind::Relic: return "Relic";
		case FrontierKind::Wanderers: return "Wanderers";
		}
		return "None";
	}

	namespace
	{
		/// <summary>The salts of the frontier's own streams, so nothing the galaxy drew before is drawn differently.</summary>
		constexpr long long ClassifySalt = 0x46524F4E54LL; // "FRONT"
		constexpr long long FurnishSalt = 0x4655524EL; // "FURN"

		constexpr double JupiterMassSolar = 9.546e-4;
		constexpr double JupiterRadiusSolar = 0.10045;

		double SunRadiiOfKm(double km) { return km / StarSystemBuilder::SunRadiusKm; }

		cs::Ref<StarSpec> NewStar(const cs::String& name, const cs::String& spectral, double mass, double radius, double teff, double rotationDays, const cs::String& summary)
		{
			auto s = cs::New<StarSpec>();
			s->Name = name; s->Spectral = spectral; s->MassSolar = mass; s->RadiusSolar = radius; s->TeffK = teff;
			s->LuminositySolar = FrontierSystems::LuminosityOf(radius, teff); s->RotationDays = rotationDays; s->Summary = summary;
			return s;
		}

		/// <summary>A main-sequence star of <paramref name="mass"/>: radius from SystemSources, luminosity from the mass-luminosity
		/// relation, temperature from both.</summary>
		cs::Ref<StarSpec> MainSequenceStar(const cs::String& name, double mass, SeededRandom& rng, const cs::String& summary)
		{
			double radius = SystemSources::MainSequenceRadius(mass);
			double lum = mass < 0.43 ? 0.23 * cs::Math::Pow(mass, 2.3) : cs::Math::Pow(mass, 4);
			double teff = 5772 * cs::Math::Pow(lum / (radius * radius), 0.25);
			return NewStar(name, Frontier::MainSequenceType(teff), mass, radius, teff, rng.Range(2, 40), summary);
		}

		cs::Ref<PlanetSpec> NewPlanet(const cs::String& name, const cs::String& host, double hostMass, double aAu, double e, double massEarth, SeededRandom& rng, const cs::String& summary)
		{
			auto p = cs::New<PlanetSpec>();
			p->Name = name; p->Host = host; p->AAu = aAu; p->E = e; p->MassEarth = massEarth;
			p->PeriodDays = OrbitalElements::PeriodFromGm(aAu * Astro::AuKm, StarSystemBuilder::SunGm * hostMass);
			p->RadiusEarth = NearbySystems::RadiusFromMass(massEarth); p->MassIsMinimum = false; p->Status = BodyStatus::Fictional;
			p->MeanAnomalyDeg = rng.Range(0, 360); p->ArgPeriDeg = rng.Range(0, 360);
			p->Source = "Generated from the galaxy's seed";
			p->Summary = summary;
			return p;
		}

		cs::String PlanetName(const cs::String& host, int i) { return cs::Cat(host, " ", static_cast<char>('b' + i)); }

		/// <summary>The settlers' habitat site again, about <paramref name="host"/> (the star it was marked out round is no more).</summary>
		void ResetHabitat(SystemSpec& spec, const cs::Ref<StarSpec>& host, double minAu)
		{
			spec.Stations.Clear();
			auto st = FrontierSystems::Habitat(spec.Name, host);
			st->AAu = cs::Math::Max(st->AAu, minAu);
			spec.Stations.Add(st);
		}

		/// <summary>The planets of <paramref name="spec"/> moved from the star they had to <paramref name="host"/>, keeping those inside <paramref name="maxAu"/>.</summary>
		void Rehost(SystemSpec& spec, const cs::Ref<StarSpec>& host, double minAu, double maxAu)
		{
			cs::List<cs::Ref<PlanetSpec>> kept;
			for (const auto& p : spec.Planets)
				if (p->AAu >= minAu && p->AAu <= maxAu)
				{
					p->Host = host->Name;
					p->PeriodDays = OrbitalElements::PeriodFromGm(p->AAu * Astro::AuKm, StarSystemBuilder::SunGm * host->MassSolar);
					kept.Add(p);
				}
			for (int i = 0; i < kept.Count(); i++) kept[i]->Name = PlanetName(host->Name, i);
			spec.Planets = kept;
			for (const auto& d : spec.Discs) d->Host = host->Name;
		}

		cs::Ref<BinaryOrbitSpec> PairOrbit(double aAu, double totalMass, SeededRandom& rng, double eMax)
		{
			auto o = cs::New<BinaryOrbitSpec>();
			o->AAu = aAu; o->E = rng.Range(0, eMax);
			o->PeriodYears = cs::Math::Sqrt(aAu * aAu * aAu / totalMass);
			o->IDeg = rng.Range(10, 170); o->NodeDeg = rng.Range(0, 360); o->ArgPeriDeg = rng.Range(0, 360);
			o->PeriastronYear = 2000 - rng.Range(0, o->PeriodYears);
			return o;
		}

		/// <summary>Makes <paramref name="spec"/>'s lone star the brighter of a pair; returns the pair's two stars.</summary>
		void MakePair(SystemSpec& spec, SeededRandom& rng, double aAu, cs::Ref<StarSpec>& a, cs::Ref<StarSpec>& b, const cs::String& summary)
		{
			a = spec.Primary->Star;
			a->Name = spec.Name + " A";
			double massB = cs::Math::Max(0.08, a->MassSolar * rng.Range(0.3, 0.95));
			b = MainSequenceStar(spec.Name + " B", massB, rng, cs::Cat("The fainter of the pair, ", cs::Fmt(massB, "0.00"), " solar masses. Fiction, from the galaxy's seed."));
			spec.Primary = StellarNode::Pair(spec.Name, a, b, PairOrbit(aAu, a->MassSolar + b->MassSolar, rng, 0.5), summary);
		}
	}

	// ================================================================ the gradient

	double Frontier::Depth(DVec3 ly)
	{
		double r = ly.Length();
		if (!(r > StartLy)) return 0;
		return cs::Math::Min(1.0, (r - StartLy) / (FullLy - StartLy));
	}

	const char* Frontier::BandName(int band)
	{
		switch (band)
		{
		case 0: return "known space";
		case 1: return "the near frontier";
		case 2: return "the far frontier";
		case 3: return "the deep frontier";
		default: return "the wilds";
		}
	}

	FrontierKind Frontier::Classify(uint64_t starSeed, DVec3 ly)
	{
		double depth = Depth(ly);
		if (depth <= 0) return FrontierKind::None;
		auto rng = SeededRandom(SeededRandom::Mix(static_cast<long long>(starSeed), ClassifySalt));
		if (rng.NextDouble() >= OddChance(depth)) return FrontierKind::None;
		// The common oddities everywhere on the frontier; the remnants and the relics more often the deeper it is.
		struct W { FrontierKind k; double w; };
		const W weights[] =
		{
			{ FrontierKind::Binary, 18 },
			{ FrontierKind::Trinary, 6 + 4 * depth },
			{ FrontierKind::BrownDwarfs, 8 },
			{ FrontierKind::WhiteDwarf, 9 },
			{ FrontierKind::Nebula, 10 },
			{ FrontierKind::CometCloud, 9 },
			{ FrontierKind::Wanderers, 10 },
			{ FrontierKind::Pulsar, 2 + 8 * depth },
			{ FrontierKind::BlackHole, 1 + 6 * depth },
			{ FrontierKind::RoguePlanet, 4 + 4 * depth },
			{ FrontierKind::Relic, 0.5 + 5 * depth },
		};
		double total = 0;
		for (const auto& w : weights) total += w.w;
		double pick = rng.NextDouble() * total;
		for (const auto& w : weights)
		{
			if (pick < w.w) return w.k;
			pick -= w.w;
		}
		return FrontierKind::Binary;
	}

	// ================================================================ the charts

	void Frontier::Chart(StarEntry& e, FrontierKind kind)
	{
		if (kind == FrontierKind::None) return;
		e.Tag = ToString(kind);
		// Unidentified until somebody goes: the charts have only its light.
		e.Spectral = "?";
		switch (kind)
		{
		case FrontierKind::Pulsar: e.TeffK = 30000; e.LuminositySolar = 0.02; break;
		case FrontierKind::BlackHole: e.TeffK = 8000; e.LuminositySolar = 0.01; break;
		case FrontierKind::WhiteDwarf: e.TeffK = 14000; e.LuminositySolar = 0.004; break;
		case FrontierKind::BrownDwarfs: e.TeffK = 1400; e.LuminositySolar = 1e-4; break;
		case FrontierKind::RoguePlanet: e.TeffK = 400; e.LuminositySolar = 1e-7; break;
		default: break;
		}
	}

	// ================================================================ the systems

	void Frontier::Furnish(SystemSpec& spec, FrontierKind kind, uint64_t starSeed, int seed)
	{
		if (kind == FrontierKind::None || spec.Primary == nullptr || spec.Primary->IsPair()) return;
		auto rng = SeededRandom(SeededRandom::Mix(seed, static_cast<long long>(starSeed), FurnishSalt));
		const cs::String name = spec.Name;
		auto star = spec.Primary->Star;
		spec.Oddity = kind;
		cs::String summary;
		switch (kind)
		{
		case FrontierKind::Pulsar:
		{
			double mass = rng.Range(1.2, 2.0), km = rng.Range(10.5, 13.5), teff = rng.Range(3e5, 1e6);
			double spinS = rng.LogRange(0.0016, 1.2);
			*star = *NewStar(name, PulsarSpectral, mass, SunRadiiOfKm(km), teff, spinS / Astro::SecondsPerDay,
				cs::Cat("A pulsar: the collapsed core of a star that went supernova, ", cs::Fmt(mass, "0.00"), " solar masses crushed into a ball ",
					cs::Fmt(km * 2, "0"), " km across, spinning ", spinS < 0.03 ? cs::Cat(cs::Fmt(1 / spinS, "0"), " times a second") : cs::Cat("once every ", cs::Fmt(spinS, "0.###"), " s"),
					". Two beams of radio and X-rays sweep from its magnetic poles. Fiction, from the galaxy's seed."));
			spec.Planets.Clear();
			spec.Discs.Clear();
			// Pulsar planets (PSR B1257+12's are real): small worlds made again from the wreckage.
			int n = rng.Poisson(1.6);
			double a = rng.Range(0.15, 0.4);
			for (int i = 0; i < n && i < 4; i++)
			{
				double m = rng.LogRange(0.02, 4);
				spec.Planets.Add(NewPlanet(PlanetName(name, i), name, mass, a, rng.Range(0, 0.05), m, rng,
					cs::Cat("A pulsar planet, ", cs::Fmt(m, "0.00"), " Earth masses, grown from the supernova's leavings and scoured by the beams. Fiction.")));
				a *= rng.Range(1.3, 1.9);
			}
			ResetHabitat(spec, star, 1.5);
			summary = cs::Cat("A pulsar with ", n, n == 1 ? " planet" : " planets", " in its glare. Fiction, from the galaxy's seed.");
			break;
		}
		case FrontierKind::BlackHole:
		{
			double mass = rng.Range(5, 25);
			*star = *NewStar(name, BlackHoleSpectral, mass, SunRadiiOfKm(HorizonKm(mass)), 9000, rng.Range(0.001, 0.01) / Astro::SecondsPerDay,
				cs::Cat("A stellar black hole of ", cs::Fmt(mass, "0.0"), " solar masses, its event horizon ", cs::Fmt(HorizonKm(mass) * 2, "0"),
					" km across. What falls in first heats to millions of degrees in the accretion disc and shines. Fiction, from the galaxy's seed."));
			// What light the system has is the disc's.
			star->LuminositySolar = rng.Range(0.05, 0.3);
			spec.Planets.Clear();
			spec.Discs.Clear();
			int n = rng.Poisson(1.2);
			double a = rng.Range(3, 8);
			for (int i = 0; i < n && i < 3; i++)
			{
				double m = rng.LogRange(0.3, 400);
				spec.Planets.Add(NewPlanet(PlanetName(name, i), name, mass, a, rng.Range(0.1, 0.5), m, rng,
					cs::Cat("A captured world, ", cs::Fmt(m, "0.0"), " Earth masses, on a long, eccentric orbit about the black hole. Fiction.")));
				a *= rng.Range(1.8, 3.5);
			}
			auto d = cs::New<DiscSpec>();
			d->Name = name + " Infall Ring"; d->Host = name; d->InnerAu = rng.Range(0.6, 1.2); d->OuterAu = d->InnerAu * rng.Range(1.6, 2.4);
			d->Source = "Generated from the galaxy's seed"; d->Summary = "Gas and rubble spiralling in toward the accretion disc. Fiction.";
			spec.Discs.Add(d);
			ResetHabitat(spec, star, 2);
			summary = "A black hole and its accretion disc, with whatever it has caught. Fiction, from the galaxy's seed.";
			break;
		}
		case FrontierKind::WhiteDwarf:
		{
			double mass = rng.Range(0.5, 1.1), teff = rng.Range(9000, 40000);
			double radius = 0.0127 * cs::Math::Pow(mass / 0.6, -1.0 / 3.0);
			*star = *NewStar(name, cs::Cat("DA", cs::Math::Max(1, cs::Math::Min(9, cs::ToInt(cs::Math::Round(50400 / teff))))), mass, radius, teff, rng.Range(0.05, 2),
				cs::Cat("A young white dwarf, the bare core of a star that swelled into a red giant and blew its outer layers off; they glow round it still ",
					"as a planetary nebula. ", cs::Fmt(mass, "0.00"), " solar masses in a ball the size of Earth. Fiction, from the galaxy's seed."));
			// Its inner worlds were swallowed by the giant; those further out survived, pushed outward.
			Rehost(spec, star, 1.2, 200);
			for (const auto& p : spec.Planets)
			{
				p->AAu *= 1.6;
				p->PeriodDays = OrbitalElements::PeriodFromGm(p->AAu * Astro::AuKm, StarSystemBuilder::SunGm * mass);
			}
			auto d = cs::New<DiscSpec>();
			d->Name = name + " Ruin Disc"; d->Host = name; d->InnerAu = rng.Range(0.004, 0.008); d->OuterAu = d->InnerAu * rng.Range(2, 4);
			d->Source = "Generated from the galaxy's seed"; d->Summary = "The dust of a world torn apart by the white dwarf's tides, falling onto it. Fiction.";
			spec.Discs.Add(d);
			ResetHabitat(spec, star, 0.02);
			summary = "A white dwarf inside the glowing shell of its old outer layers. Fiction, from the galaxy's seed.";
			break;
		}
		case FrontierKind::BrownDwarfs:
		{
			auto dwarf = [&](const cs::String& n)
			{
				double m = rng.Range(0.03, 0.075), teff = rng.Range(700, 2200);
				return NewStar(n, cs::Cat(teff < 1300 ? "T" : "L", cs::ToInt(cs::Math::Floor(rng.Range(0, 9.99)))), m, rng.Range(0.08, 0.1), teff, rng.Range(0.1, 1),
					cs::Cat("A brown dwarf, ", cs::Fmt(m / JupiterMassSolar, "0"), " Jupiter masses: too small ever to burn hydrogen, glowing dull red with the heat it was born with. Fiction."));
			};
			auto a = dwarf(name + " A"), b = dwarf(name + " B");
			double aAu = rng.Range(1, 6);
			spec.Primary = StellarNode::Pair(name, a, b, PairOrbit(aAu, a->MassSolar + b->MassSolar, rng, 0.4),
				"The centre of mass two brown dwarfs circle: no true star burns here.");
			spec.Planets.Clear();
			spec.Discs.Clear();
			int n = rng.Poisson(0.9);
			double pa = aAu * rng.Range(0.05, 0.12);
			for (int i = 0; i < n && i < 2; i++)
			{
				double m = rng.LogRange(0.05, 2);
				spec.Planets.Add(NewPlanet(PlanetName(a->Name, i), a->Name, a->MassSolar, pa, rng.Range(0, 0.1), m, rng,
					cs::Cat("A dim, frozen world of ", cs::Fmt(m, "0.00"), " Earth masses lit only by its brown dwarf's red glow. Fiction.")));
				pa *= 1.5;
			}
			ResetHabitat(spec, a, aAu * 0.04);
			summary = "Two brown dwarfs circling each other in the dark. Fiction, from the galaxy's seed.";
			break;
		}
		case FrontierKind::RoguePlanet:
		{
			double mj = rng.LogRange(0.5, 12), teff = rng.Range(90, 400);
			double mass = mj * JupiterMassSolar;
			*star = *NewStar(name, RogueSpectral, mass, JupiterRadiusSolar * rng.Range(0.9, 1.2), teff, rng.Range(0.3, 1.2),
				cs::Cat("A rogue planet: a gas giant of ", cs::Fmt(mj, "0.0"), " Jupiter masses flung out of the system it was born in, ",
					"drifting between the stars with no sun at all; it glows only faintly with its own leftover heat. Fiction, from the galaxy's seed."));
			spec.Planets.Clear();
			spec.Discs.Clear();
			int n = 1 + rng.Poisson(1.5);
			double a = rng.Range(0.0025, 0.004);
			for (int i = 0; i < n && i < 5; i++)
			{
				double m = rng.LogRange(0.005, 0.5);
				auto p = NewPlanet(PlanetName(name, i), name, mass, a, rng.Range(0, 0.04), m, rng,
					cs::Cat("A moon that went with its planet into the dark, ", cs::Fmt(m, "0.000"), " Earth masses, its ice warmed from within by tides alone. Fiction."));
				p->Color = Rgb(0.62f, 0.68f, 0.78f);
				spec.Planets.Add(p);
				a *= rng.Range(1.5, 2.2);
			}
			ResetHabitat(spec, star, 0.01);
			summary = cs::Cat("A starless rogue planet with ", n, n == 1 ? " moon" : " moons", ". Fiction, from the galaxy's seed.");
			break;
		}
		case FrontierKind::Binary:
		case FrontierKind::Trinary:
		{
			bool wide = rng.NextDouble() < 0.5;
			double aAu = wide ? rng.Range(20, 80) : rng.Range(0.08, 0.6);
			cs::Ref<StarSpec> a, b;
			MakePair(spec, rng, aAu, a, b, kind == FrontierKind::Trinary ? "The centre of mass the inner pair of a triple star circles." : "The centre of mass the two stars circle.");
			// The worlds that can hold an orbit: well inside a wide pair, about one star; a close pair keeps none of the planets it had.
			if (wide) Rehost(spec, a, 0, aAu * 0.25);
			else spec.Planets.Clear();
			if (wide && rng.NextDouble() < 0.6)
			{
				double m = rng.LogRange(0.2, 300);
				spec.Planets.Add(NewPlanet(PlanetName(b->Name, 0), b->Name, b->MassSolar, aAu * rng.Range(0.05, 0.18), rng.Range(0, 0.15), m, rng,
					cs::Cat(cs::Fmt(m, "0.0"), " Earth masses about the fainter star, with two suns in its sky. Fiction.")));
			}
			for (const auto& d : spec.Discs)
			{
				d->Host = a->Name;
				if (!wide) { d->InnerAu = cs::Math::Max(d->InnerAu, aAu * 4); d->OuterAu = cs::Math::Max(d->OuterAu, d->InnerAu * 1.4); }
				else if (d->OuterAu > aAu * 0.3) { d->InnerAu = aAu * 0.12; d->OuterAu = aAu * 0.22; }
			}
			ResetHabitat(spec, a, 0.05);
			if (kind == FrontierKind::Trinary)
			{
				double mc = rng.Range(0.09, 0.4);
				auto c = cs::New<CompanionSpec>();
				c->Node = StellarNode::Single(MainSequenceStar(name + " C", mc, rng, cs::Cat("The third star, a red dwarf of ", cs::Fmt(mc, "0.00"),
					" solar masses on a slow orbit far out round the inner pair. Fiction, from the galaxy's seed.")));
				c->SeparationAu = cs::Math::Max(aAu * 6, rng.Range(150, 900));
				c->PositionAngleDeg = rng.Range(0, 360);
				c->E = rng.Range(0.1, 0.5);
				c->MeanAnomalyDeg = rng.Range(0, 360);
				spec.Companions.Add(c);
				summary = cs::Cat("A triple star: a ", wide ? "wide" : "close", " pair, and a red dwarf far out. Fiction, from the galaxy's seed.");
			}
			else summary = cs::Cat(wide ? "A wide" : "A close", " binary star. Fiction, from the galaxy's seed.");
			break;
		}
		case FrontierKind::Nebula:
			summary = cs::Cat("A ", star->TeffK < 3900 ? "red dwarf" : "star", " wrapped in a nebula of glowing gas that swallows sensors. Fiction, from the galaxy's seed.");
			break;
		case FrontierKind::CometCloud:
		{
			double inner = rng.Range(40, 90) * cs::Math::Max(0.5, cs::Math::Sqrt(star->LuminositySolar));
			auto d = cs::New<DiscSpec>();
			d->Name = name + " Comet Cloud"; d->Host = star->Name; d->InnerAu = inner; d->OuterAu = inner * rng.Range(2.5, 4);
			d->Source = "Generated from the galaxy's seed";
			d->Summary = "A vast, dense cloud of comet nuclei ringing the system; every so often one falls sunward, trailing ice. Fiction.";
			spec.Discs.Add(d);
			summary = "A star ringed by a vast comet cloud, its inner system strewn with falling ice. Fiction, from the galaxy's seed.";
			break;
		}
		case FrontierKind::Relic:
		{
			double a = rng.Range(1.5, 5) * cs::Math::Max(0.3, cs::Math::Sqrt(star->LuminositySolar));
			auto p = NewPlanet(name + RelicSuffix, star->Name, star->MassSolar, a, rng.Range(0, 0.02), 0.08, rng,
				cs::String("The ruin of a ring-shaped structure a quarter of a million kilometres across, built round nothing, by nobody the galaxy remembers. ")
				+ "Its arcs are broken and its lights long out; what made it, and why, no survey has told. Fiction.");
			p->RadiusEarth = 18;
			p->Color = Rgb(0.55f, 0.5f, 0.42f);
			p->GasGiant = false;
			p->TidallyLocked = false;
			p->RotationHours = 60;
			spec.Planets.Add(p);
			summary = "An ordinary star with something extraordinary in orbit: the ruin of an ancient ring. Fiction, from the galaxy's seed.";
			break;
		}
		case FrontierKind::Wanderers:
		{
			double starAu = star->RadiusSolar * StarSystemBuilder::SunRadiusKm / Astro::AuKm;
			double a = cs::Math::Max(starAu * 10, 0.08 * cs::Math::Max(0.3, cs::Math::Sqrt(star->LuminositySolar)));
			for (int i = spec.Planets.Count(); i < 3; i++)
			{
				double m = rng.LogRange(0.1, 200);
				spec.Planets.Add(NewPlanet(PlanetName(name, i), name, star->MassSolar, cs::Math::Max(a, (i + 1) * 0.6), 0, m, rng,
					cs::Cat(cs::Fmt(m, "0.0"), " Earth masses. Fiction.")));
			}
			for (const auto& p : spec.Planets)
			{
				// Thrown about by some old encounter: steep, eccentric orbits, some running backwards; none grazing the star.
				double eMax = cs::Math::Max(0.0, 1 - 4 * starAu / p->AAu);
				p->E = cs::Math::Min(eMax, rng.Range(0.3, 0.85));
				p->TiltFromReferenceDeg = rng.Range(25, 170);
				p->Summary = cs::Cat(p->Summary, p->TiltFromReferenceDeg > 90 ? " Its orbit runs backwards" : " Its orbit is steeply tilted",
					", e ", cs::Fmt(p->E, "0.00"), ".");
			}
			double m = rng.LogRange(0.01, 0.5), aw = rng.Range(8, 30);
			auto w = NewPlanet(PlanetName(name, spec.Planets.Count()), name, star->MassSolar, aw, rng.Range(0.88, 0.95), m, rng,
				cs::Cat("An iceball of ", cs::Fmt(m, "0.00"), " Earth masses on a comet's orbit, plunging in from ", cs::Fmt(aw * 2, "0"), " AU against the others' turn. Fiction."));
			w->TiltFromReferenceDeg = rng.Range(120, 175);
			spec.Planets.Add(w);
			summary = "A star whose worlds run on wild orbits: steep, eccentric, some backwards. Fiction, from the galaxy's seed.";
			break;
		}
		case FrontierKind::None:
			break;
		}
		if (!summary.empty()) spec.Summary = summary;
		Note(spec);
	}

	void Frontier::Note(const SystemSpec& spec)
	{
		if (spec.Oddity == FrontierKind::Nebula || spec.Oddity == FrontierKind::WhiteDwarf || spec.Oddity == FrontierKind::CometCloud)
			SpaceFeatures::Mark(spec.Name, static_cast<int>(spec.Oddity));
	}

	FrontierKind Frontier::KindOf(const StarSystem& s)
	{
		return s.Spec != nullptr ? s.Spec->Oddity : FrontierKind::None;
	}

	bool Frontier::IsUncharted(const StarSystem& s)
	{
		if (s.IsVoidBase || Depth(s.PositionLy) <= 0 || s.Gates().Count() > 0) return false;
		return s.World == nullptr || !s.Settled();
	}

	double Frontier::RevealKm(const BodyDefinition& b)
	{
		return cs::Math::Max(RevealMinKm, RevealRadii * b.RadiusKm);
	}

	// ================================================================ words

	const char* Frontier::Title(FrontierKind kind)
	{
		switch (kind)
		{
		case FrontierKind::Pulsar: return "Pulsar";
		case FrontierKind::BlackHole: return "Black hole";
		case FrontierKind::WhiteDwarf: return "White dwarf and nebula";
		case FrontierKind::BrownDwarfs: return "Brown dwarf pair";
		case FrontierKind::RoguePlanet: return "Rogue planet";
		case FrontierKind::Binary: return "Binary star";
		case FrontierKind::Trinary: return "Triple star";
		case FrontierKind::Nebula: return "Star in a nebula";
		case FrontierKind::CometCloud: return "Comet cloud";
		case FrontierKind::Relic: return "Ancient relic";
		case FrontierKind::Wanderers: return "Wandering worlds";
		case FrontierKind::None: break;
		}
		return "Star";
	}

	const char* Frontier::Describe(FrontierKind kind)
	{
		switch (kind)
		{
		case FrontierKind::Pulsar: return "A neutron star spinning on its axis, sweeping two beams across the sky like a lighthouse; the few worlds about it were made from the wreckage of the supernova that made it.";
		case FrontierKind::BlackHole: return "A black hole: no light from the thing itself, only the accretion disc of gas heated white as it falls in, and a ring of bent starlight about the dark.";
		case FrontierKind::WhiteDwarf: return "A dead star's core, white-hot and Earth-sized, inside the glowing shell it shed; its inner worlds were swallowed long ago.";
		case FrontierKind::BrownDwarfs: return "Two failed stars circling each other, too small ever to burn: a system with no sun, lit dull red.";
		case FrontierKind::RoguePlanet: return "A planet with no star, adrift between the stars with its moons; dark, cold, and found only by coming near.";
		case FrontierKind::Binary: return "Two stars bound together, their worlds circling one or the other.";
		case FrontierKind::Trinary: return "A pair of stars with a third circling them from far out.";
		case FrontierKind::Nebula: return "A star wrapped in a nebula of glowing gas; inside it sensors reach only a fraction as far.";
		case FrontierKind::CometCloud: return "A star ringed by a dense cloud of comet nuclei, its inner system strewn with ice.";
		case FrontierKind::Relic: return "The broken ring of something ancient and enormous, orbiting an ordinary star; nobody knows who built it.";
		case FrontierKind::Wanderers: return "Worlds on wild orbits, steep and eccentric, some running backwards: the marks of an old disaster.";
		case FrontierKind::None: break;
		}
		return "";
	}

	double Frontier::SurveyValue(FrontierKind kind)
	{
		switch (kind)
		{
		case FrontierKind::Pulsar: return 120000;
		case FrontierKind::BlackHole: return 200000;
		case FrontierKind::WhiteDwarf: return 60000;
		case FrontierKind::BrownDwarfs: return 50000;
		case FrontierKind::RoguePlanet: return 90000;
		case FrontierKind::Binary: return 35000;
		case FrontierKind::Trinary: return 70000;
		case FrontierKind::Nebula: return 50000;
		case FrontierKind::CometCloud: return 45000;
		case FrontierKind::Relic: return 250000;
		case FrontierKind::Wanderers: return 55000;
		case FrontierKind::None: break;
		}
		return 0;
	}

	bool Frontier::IsPulsar(const BodyDefinition& b) { return b.Kind == BodyKind::Star && b.SpectralType == PulsarSpectral; }
	bool Frontier::IsBlackHole(const BodyDefinition& b) { return b.Kind == BodyKind::Star && b.SpectralType == BlackHoleSpectral; }
	bool Frontier::IsRogue(const BodyDefinition& b) { return b.Kind == BodyKind::Star && b.SpectralType == RogueSpectral; }

	bool Frontier::IsRelic(const BodyDefinition& b)
	{
		const cs::String suffix = RelicSuffix;
		return b.Kind == BodyKind::Planet && b.Name.size() > suffix.size() && b.Name.compare(b.Name.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	cs::String Frontier::MainSequenceType(double teffK)
	{
		// Each class's temperature span (Pecaut and Mamajek, rounded); the subclass runs 9 at the cool end to 0 at the hot.
		struct Band { double t0, t1; const char* cls; };
		static const Band bands[] = { { 2300, 3900, "M" }, { 3900, 5300, "K" }, { 5300, 6000, "G" }, { 6000, 7500, "F" }, { 7500, 10000, "A" }, { 10000, 30000, "B" } };
		for (const auto& b : bands)
			if (teffK < b.t1 || b.cls[0] == 'B')
			{
				double f = cs::Math::Max(0.0, cs::Math::Min(0.999, (teffK - b.t0) / (b.t1 - b.t0)));
				return cs::Cat(b.cls, cs::ToInt(cs::Math::Floor((1 - f) * 9.99)), "V");
			}
		return "B0V";
	}
}
