// Port of Assets/UnitySpace/Core/Systems/NearbySystems.cs (US-T17).

#include "Systems/NearbySystems.h"
#include "Systems/StarSystemSpec.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	namespace
	{
		// The C# object initialisers `new SystemSpec { Name = .., NeighbourName = .., RaDeg = .., DecDeg = .., DistanceLy = .., Primary = .., Summary = .. }`.
		cs::Ref<SystemSpec> NewSystem(const cs::String& name, double raDeg, double decDeg, double distanceLy, const cs::Ref<StellarNode>& primary)
		{
			auto s = cs::New<SystemSpec>();
			s->Name = name; s->NeighbourName = name;
			s->RaDeg = raDeg; s->DecDeg = decDeg; s->DistanceLy = distanceLy;
			s->Primary = primary;
			return s;
		}

		cs::Ref<BinaryOrbitSpec> NewOrbit(double periodYears, double aAu, double e, double iDeg, double nodeDeg, double argPeriDeg, double periastronYear)
		{
			auto o = cs::New<BinaryOrbitSpec>();
			o->PeriodYears = periodYears; o->AAu = aAu; o->E = e; o->IDeg = iDeg; o->NodeDeg = nodeDeg; o->ArgPeriDeg = argPeriDeg;
			o->PeriastronYear = periastronYear;
			return o;
		}

		cs::Ref<DiscSpec> NewDisc(const cs::String& name, const cs::String& host, double innerAu, double outerAu, double iDeg, const cs::String& source, const cs::String& summary)
		{
			auto d = cs::New<DiscSpec>();
			d->Name = name; d->Host = host; d->InnerAu = innerAu; d->OuterAu = outerAu; d->IDeg = iDeg; d->Source = source; d->Summary = summary;
			return d;
		}
	}

	cs::List<cs::Ref<SystemSpec>> NearbySystems::All()
	{
		cs::List<cs::Ref<SystemSpec>> r;
		r.Add(BarnardsStar());
		r.Add(Luhman16());
		r.Add(Wolf359());
		r.Add(Lalande21185());
		r.Add(Sirius());
		r.Add(EpsilonEridani());
		r.Add(Ross128());
		r.Add(EpsilonIndi());
		r.Add(TauCeti());
		r.Add(Gj1061());
		r.Add(LuytensStar());
		r.Add(TeegardensStar());
		return r;
	}

	cs::Ref<StarSpec> NearbySystems::Star(const cs::String& name, const cs::String& spectral, double mass, double radius, double luminosity, double teff, double rotationDays, const cs::String& summary)
	{
		auto s = cs::New<StarSpec>();
		s->Name = name; s->Spectral = spectral; s->MassSolar = mass; s->RadiusSolar = radius; s->LuminositySolar = luminosity; s->TeffK = teff;
		s->RotationDays = rotationDays; s->Summary = summary;
		return s;
	}

	cs::Ref<PlanetSpec> NearbySystems::Archive(const cs::String& name, const cs::String& host, double periodDays, double aAu, double e, double massEarth, double radiusEarth,
		double argPeriDeg, int year, const cs::String& reference, const cs::String& summary, bool minimumMass, double periastronJd)
	{
		auto p = cs::New<PlanetSpec>();
		p->Name = name; p->Host = host; p->PeriodDays = periodDays; p->AAu = aAu; p->E = e; p->MassEarth = massEarth; p->RadiusEarth = radiusEarth;
		p->MassIsMinimum = minimumMass; p->ArgPeriDeg = argPeriDeg; p->PeriastronJd = periastronJd; p->DiscoveryYear = year;
		p->Source = "NASA Exoplanet Archive (" + reference + ")"; p->Summary = summary;
		return p;
	}

	cs::Ref<PlanetSpec> NearbySystems::Candidate(const cs::String& name, const cs::String& host, double periodDays, double aAu, double e, double minMassEarth, int year,
		const cs::String& reference, const cs::String& summary)
	{
		auto p = cs::New<PlanetSpec>();
		p->Name = name; p->Host = host; p->PeriodDays = periodDays; p->AAu = aAu; p->E = e; p->MassEarth = minMassEarth;
		p->RadiusEarth = RadiusFromMass(minMassEarth); p->MassIsMinimum = true; p->Status = BodyStatus::Candidate; p->DiscoveryYear = year;
		p->Source = reference; p->Summary = summary;
		return p;
	}

	double NearbySystems::RadiusFromMass(double massEarth)
	{
		const double terran = 2.04, neptunian = 0.414 * 317.828;
		double rTerran = 1.008 * cs::Math::Pow(terran, 0.279);
		if (massEarth <= terran) return 1.008 * cs::Math::Pow(massEarth, 0.279);
		double rNeptunian = rTerran * cs::Math::Pow(neptunian / terran, 0.589);
		if (massEarth <= neptunian) return rTerran * cs::Math::Pow(massEarth / terran, 0.589);
		return rNeptunian * cs::Math::Pow(massEarth / neptunian, -0.044);
	}

	// ---------------------------------------------------------------- 5.96 ly

	cs::Ref<SystemSpec> NearbySystems::BarnardsStar()
	{
		const cs::String host = "Barnard's Star";
		auto s = NewSystem("Barnard's Star", Astro::Hms(17, 57, 48.49847), Astro::Dms(4, 41, 36.1139), SystemSpec::LyFromParallax(546.9759),
			StellarNode::Single(Star(host, "M4.0V", 0.162, 0.187, 0.003550, 3195, 114.06,
				cs::String("An old M4 red dwarf, the nearest single star to the Sun. It crosses our sky faster than any other star, ")
				+ "10.4 arcseconds a year.")));
		s->Summary = "The nearest single star, with four small rocky planets packed inside Mercury's distance.";
		s->Planets.Add(Archive("Barnard d", host, 2.3402, 0.0188, 0.04, 0.263, 0.694, -51.8, 2025, "Basant et al. 2025",
			"A quarter of Earth's mass at least, the innermost of four, on a 2.3-day orbit. Orbital plane assumed."));
		s->Planets.Add(Archive("Barnard b", host, 3.1542, 0.0229, 0.03, 0.299, 0.72, 3.8, 2024, "Basant et al. 2025",
			"Found by ESPRESSO in 2024, a hot rocky world under a third of Earth's mass. Orbital plane assumed."));
		s->Planets.Add(Archive("Barnard c", host, 4.1244, 0.0274, 0.08, 0.335, 0.743, 90.8, 2025, "Basant et al. 2025",
			"The heaviest of the four, still only a third of Earth's mass at least. Orbital plane assumed."));
		s->Planets.Add(Archive("Barnard e", host, 6.7392, 0.0381, 0.04, 0.193, 0.637, -27.5, 2025, "Basant et al. 2025",
			"The outermost and lightest, a fifth of Earth's mass at least; all four are too hot for liquid water. Orbital plane assumed."));
		return s;
	}

	// ---------------------------------------------------------------- 6.51 ly

	cs::Ref<SystemSpec> NearbySystems::Luhman16()
	{
		const double plx = 500.993;
		auto a = Star("Luhman 16A", "L7.5", 35.4 * JupiterMassSolar, 0.102, 2.2e-5, 1305, 6.94 / 24,
			"A brown dwarf of 35 Jupiter masses at the L-T transition, too light to fuse hydrogen, glowing from its own heat.");
		auto b = Star("Luhman 16B", "T0.5", 29.4 * JupiterMassSolar, 0.102, 2.1e-5, 1320, 5.28 / 24,
			"The lighter brown dwarf, 29 Jupiter masses, with patchy clouds that make it the among the most variable brown dwarfs known.");
		auto s = NewSystem("Luhman 16", Astro::Hms(10, 49, 18.771), Astro::Dms(53, 19, 9.88, true), SystemSpec::LyFromParallax(plx),
			// Bedin et al. 2024: the astrometric orbit of B about A.
			StellarNode::Pair("Luhman 16", a, b, NewOrbit(26.55, SystemSpec::AuFromArcsec(1.764, plx), 0.344, 79.92, 130.02, 136.67, 2018.060)));
		s->Summary = "The nearest brown dwarfs: a pair of failed stars circling each other every 26.6 years, found in 2013.";
		return s;
	}

	// ---------------------------------------------------------------- 7.86 ly

	cs::Ref<SystemSpec> NearbySystems::Wolf359()
	{
		const cs::String host = "Wolf 359";
		auto s = NewSystem("Wolf 359", Astro::Hms(10, 56, 28.92087), Astro::Dms(7, 0, 53.0033), SystemSpec::LyFromParallax(415.1794),
			StellarNode::Single(Star(host, "M6V", 0.110, 0.144, 0.00106, 2749, 2.704,
				"One of the faintest and lowest-mass stars near the Sun: an M6 flare star a tenth of the Sun's mass, spinning in 2.7 days.")));
		s->Summary = "A dim, flaring red dwarf with one unconfirmed giant planet far out.";
		s->Planets.Add(Candidate("Wolf 359 b", host, 2938, 1.845, 0.04, 43.9, 2019, "Tuomi et al. 2019",
			cs::String("CANDIDATE: a cold Neptune-to-Saturn world (44 Earth masses at least) on an 8-year orbit, from one radial-velocity analysis. ")
			+ "Its inner partner \"c\" turned out to be the star's rotation. Radius estimated; plane and phase assumed."));
		return s;
	}

	// ---------------------------------------------------------------- 8.30 ly

	cs::Ref<SystemSpec> NearbySystems::Lalande21185()
	{
		const cs::String host = "Lalande 21185";
		auto s = NewSystem("Lalande 21185", Astro::Hms(11, 3, 20.19482), Astro::Dms(35, 58, 11.5762), SystemSpec::LyFromParallax(392.7529),
			StellarNode::Single(Star(host, "M2V", 0.389, 0.392, 0.01950, 3547, 53.33,
				"The brightest red dwarf in the northern sky, just too faint for the naked eye, in Ursa Major.")));
		s->Summary = "A bright red dwarf with a hot super-Earth and a cold Neptune.";
		s->Planets.Add(Archive("Lalande 21185 b", host, 12.9394, 0.07879, 0.063, 2.69, 1.45, -30, 2019, "Hurt et al. 2022",
			"A hot super-Earth, 2.7 Earth masses at least, on a 13-day orbit. Orbital plane assumed.", true, 2456307.8));
		s->Planets.Add(Candidate("Lalande 21185 d", host, 215.7, 0.5142, 0.18, 3.89, 2022, "Hurt et al. 2022",
			"CANDIDATE: a 216-day signal that may be a 3.9-Earth-mass planet. Radius estimated; plane and phase assumed."));
		s->Planets.Add(Archive("Lalande 21185 c", host, 2946, 2.94, 0.132, 13.6, 3.76, 63, 2021, "Hurt et al. 2022",
			"A cold Neptune-mass world (13.6 Earth masses at least) on an 8-year orbit. Orbital plane assumed.", true, 2457250));
		return s;
	}

	// ---------------------------------------------------------------- 8.61 ly

	cs::Ref<SystemSpec> NearbySystems::Sirius()
	{
		const double plx = 378.9;
		auto a = Star("Sirius A", "A0mA1 Va", 2.063, 1.7144, 24.74, 9845, 5.3,
			cs::String("The brightest star in Earth's night sky: a white main-sequence star of twice the Sun's mass and 25 times its light. ")
			+ "Rotation from its 16 km/s spin, assuming we see the equator.");
		auto b = Star("Sirius B", "DA2", 1.018, 0.008098, 0.02448, 25000, 1,
			cs::String("A white dwarf: the Sun's mass crushed into a ball smaller than Earth, the burnt-out core of a star once heavier than A. ")
			+ "Rotation assumed.");
		auto s = NewSystem("Sirius", Astro::Hms(6, 45, 8.917), Astro::Dms(16, 42, 58.02, true), SystemSpec::LyFromParallax(plx),
			// Bond et al. 2017 (ApJ 840, 70): the visual orbit of B about A.
			StellarNode::Pair("Sirius", a, b, NewOrbit(50.1284, SystemSpec::AuFromArcsec(7.4957, plx), 0.59142, 136.336, 45.400, 149.161, 1994.5715)));
		s->Summary = "The Dog Star and its white-dwarf companion, 8 to 31 AU apart on a 50-year orbit. No planets are known.";
		return s;
	}

	// ---------------------------------------------------------------- 10.47 ly

	cs::Ref<SystemSpec> NearbySystems::EpsilonEridani()
	{
		const cs::String host = "Epsilon Eridani";
		auto s = NewSystem("Epsilon Eridani", Astro::Hms(3, 32, 55.84496), Astro::Dms(9, 27, 29.7312, true), SystemSpec::LyFromParallax(311.37),
			StellarNode::Single(Star(host, "K2V", 0.82, 0.742, 0.326, 5085, 11.60,
				"A young orange dwarf, under a billion years old, magnetically active and ringed with dust like the young Sun.")));
		// The main belt's measured tilt to the sky (Booth et al. 2023); its node is assumed.
		s->ReferenceIDeg = 33.7;
		s->Summary = "A young Sun-like star with a Jupiter, two asteroid belts and a Kuiper belt: the nearest planetary system like ours.";
		s->Planets.Add(Archive("Epsilon Eridani b", host, 2680, 3.53, 0.06, 317.83, 13.8, 268, 2000, "Thompson et al. 2025",
			"AEgir: a Jupiter-mass giant on a 7.3-year orbit, its mass and tilt (40° to the sky) measured from astrometry. Node assumed.",
			false, 2458017.5));
		s->Planets[0]->IDeg = 40;
		s->Discs.Add(NewDisc("Epsilon Eridani inner belt", host, 1.5, 2.0, cs::Double::NaN, "Su et al. 2017; Backman et al. 2009",
			"Warm dust inside the giant planet's orbit, an asteroid belt like the Sun's (some models put it at 3-4 AU instead). Plane assumed to be the main belt's."));
		s->Discs.Add(NewDisc("Epsilon Eridani outer belt", host, 8, 20, cs::Double::NaN, "Su et al. 2017; Backman et al. 2009",
			"A second, denser belt of dust out to 20 AU (Spitzer). Plane assumed to be the main belt's."));
		s->Discs.Add(NewDisc("Epsilon Eridani main belt", host, 65, 75, 33.7, "Booth et al. 2017, 2023; MacGregor et al. 2015",
			"A narrow ring of icy debris at 70 AU, the Kuiper belt's twin, imaged by ALMA."));
		return s;
	}

	// ---------------------------------------------------------------- 11.01 ly

	cs::Ref<SystemSpec> NearbySystems::Ross128()
	{
		const cs::String host = "Ross 128";
		auto s = NewSystem("Ross 128", Astro::Hms(11, 47, 44.39727), Astro::Dms(0, 48, 16.4003), SystemSpec::LyFromParallax(296.3053),
			StellarNode::Single(Star(host, "M4V", 0.176, 0.198, 0.003620, 3189, 162,
				"A quiet, old red dwarf in Virgo. Rotation the middle of the 101-223-day range measured.")));
		s->Summary = "A calm red dwarf with one temperate Earth-mass planet.";
		s->Planets.Add(Archive("Ross 128 b", host, 9.8658, 0.0496, 0.116, 1.40, 1.11, cs::Double::NaN, 2017, "Bonfils et al. 2018",
			"A temperate world of 1.4 Earth masses at least on a 9.9-day orbit, round a star that rarely flares. Orbital plane and phase assumed."));
		return s;
	}

	// ---------------------------------------------------------------- 11.87 ly

	cs::Ref<SystemSpec> NearbySystems::EpsilonIndi()
	{
		const cs::String host = "Epsilon Indi A";
		auto s = NewSystem("Epsilon Indi", Astro::Hms(22, 3, 21.65363), Astro::Dms(56, 47, 9.5228, true), SystemSpec::LyFromParallax(274.8431),
			StellarNode::Single(Star(host, "K5Ve", 0.782, 0.713, 0.21, 4649, 35.732,
				"An orange dwarf in Indus, three-quarters of the Sun's mass, with a giant planet and two brown dwarfs.")));
		s->Summary = "An orange dwarf with a cold super-Jupiter, imaged by JWST, and a binary brown dwarf 1,500 AU out.";
		auto ba = Star("Epsilon Indi Ba", "T1", 66.92 * JupiterMassSolar, 0.0805, 2.04e-5, 1312, 2.0 / 24,
			"A T1 brown dwarf of 67 Jupiter masses, the heavier of the pair. Rotation assumed.");
		auto bb = Star("Epsilon Indi Bb", "T6", 53.25 * JupiterMassSolar, 0.0825, 5.97e-6, 972, 2.0 / 24,
			"A T6 brown dwarf of 53 Jupiter masses, cool enough for methane in its air. Rotation assumed.");
		{
			auto c = cs::New<CompanionSpec>();
			// Chen et al. 2022: the pair's orbit; its phase (periastron year) is assumed.
			c->Node = StellarNode::Pair("Epsilon Indi B", ba, bb, NewOrbit(11.0197, 2.4058, 0.54042, 77.082, 147.959, 328.27, 2000),
				"The centre of mass of the brown dwarfs Ba and Bb, 2.4 AU apart on an 11-year orbit, 1,500 AU from A.");
			// Projected separation about 1,500 AU (Scholz et al. 2003); the direction in depth and the orbit are assumed.
			c->SeparationAu = 1500; c->PositionAngleDeg = 0; c->E = 0;
			s->Companions.Add(c);
		}
		auto ab = Archive("Epsilon Indi Ab", host, 25450, 15.76, 0.25, 2065.88, 12.7, -28.13, 2019, "Sanghi et al. 2026",
			"A cold super-Jupiter of 6.5 Jupiter masses on a 70-year orbit, the nearest giant planet ever imaged (JWST, 2024). Node assumed.",
			false, 2441420.76);
		ab->IDeg = 102.18;
		s->Planets.Add(ab);
		return s;
	}

	// ---------------------------------------------------------------- 11.91 ly

	cs::Ref<SystemSpec> NearbySystems::TauCeti()
	{
		const cs::String host = "Tau Ceti";
		auto s = NewSystem("Tau Ceti", Astro::Hms(1, 44, 4.083), Astro::Dms(15, 56, 14.93, true), SystemSpec::LyFromParallax(273.8097),
			StellarNode::Single(Star(host, "G8V", 0.800, 0.793, 0.488, 5320, 46,
				"The nearest single Sun-like star, older and dimmer than the Sun, swaddled in ten times the Sun's cometary debris.")));
		// The debris disc's tilt to the sky (MacGregor et al. 2016); planets assumed to share it, node assumed.
		s->ReferenceIDeg = 35;
		s->Summary = "A nearby Sun-like star with a heavy debris disc and three super-Earths.";
		auto Feng = [&](const cs::String& name, double p, double a, double e, double m, double r, double w, const cs::String& summary)
		{
			return Archive(name, host, p, a, e, m, r, w, 2017, "Feng et al. 2017", summary);
		};
		s->Planets.Add(Feng("Tau Ceti g", 20.00, 0.133, 0.06, 1.75, 1.18, 35.341,
			"A hot super-Earth on a 20-day orbit, the innermost of Tau Ceti's three known planets."));
		s->Planets.Add(Feng("Tau Ceti h", 49.41, 0.243, 0.23, 1.83, 1.19, 7.45,
			"A 1.8-Earth-mass world on a 49-day orbit."));
		s->Planets.Add(Feng("Tau Ceti f", 636.13, 1.334, 0.16, 3.93, 1.81, 119.75,
			"A cold super-Earth just beyond the outer edge of the habitable zone."));
		s->Discs.Add(NewDisc("Tau Ceti debris disc", host, 6.2, 52, 35, "MacGregor et al. 2016",
			"A broad disc of dust and comets from about 6 to 52 AU, far heavier than the Sun's Kuiper belt (ALMA)."));
		return s;
	}

	// ---------------------------------------------------------------- 11.98 ly

	cs::Ref<SystemSpec> NearbySystems::Gj1061()
	{
		const cs::String host = "GJ 1061";
		auto s = NewSystem("GJ 1061", Astro::Hms(3, 35, 59.69916), Astro::Dms(44, 30, 45.7308, true), SystemSpec::LyFromParallax(272.1615),
			StellarNode::Single(Star(host, "M5.5V", 0.125, 0.152, 0.001700, 2977, 125,
				"A small, quiet red dwarf in Horologium, an eighth of the Sun's mass.")));
		// The inclination Dreizler et al. adopt for all three planets; node assumed.
		s->ReferenceIDeg = 77;
		s->Summary = "A quiet red dwarf with three Earth-mass planets, the outer two temperate.";
		s->Planets.Add(Archive("GJ 1061 b", host, 3.2073, 0.0210, 0.05, 1.37, 1.04, 145, 2020, "Dreizler et al. 2025",
			"A hot Earth-mass world, 1.4 Earth masses at least, on a 3.2-day orbit."));
		s->Planets.Add(Archive("GJ 1061 c", host, 6.6821, 0.0342, 0.02, 1.74, 1.19, 88, 2020, "Dreizler et al. 2025",
			"A 1.7-Earth-mass world on a 6.7-day orbit, getting about one and a half times the light Earth does."));
		s->Planets.Add(Archive("GJ 1061 d", host, 13.066, 0.054, 0.04, 1.64, 1.16, 157, 2020, "Dreizler et al. 2025",
			"A 1.6-Earth-mass world on a 13-day orbit, getting a little over half the light Earth does."));
		return s;
	}

	// ---------------------------------------------------------------- 12.35 ly

	cs::Ref<SystemSpec> NearbySystems::LuytensStar()
	{
		const cs::String host = "Luyten's Star";
		auto s = NewSystem("Luyten's Star", Astro::Hms(7, 27, 24.49897), Astro::Dms(5, 13, 32.8415), SystemSpec::LyFromParallax(264.1269),
			StellarNode::Single(Star(host, "M3.5V", 0.29, 0.293, 0.0088, 3382, 115.6,
				"GJ 273, a red dwarf in Canis Minor; 1.2 light-years from Procyon, its nearest neighbour.")));
		s->Summary = "A red dwarf with a temperate super-Earth (the target of the 2017 \"Sónar Calling GJ 273b\" message) and two candidate outer worlds.";
		s->Planets.Add(Archive("Luyten c", host, 4.7234, 0.036467, 0.17, 1.18, 1.06, cs::Double::NaN, 2017, "Astudillo-Defru et al. 2017",
			"GJ 273 c: a hot rocky world, 1.2 Earth masses at least, on a 4.7-day orbit. Orbital plane and phase assumed."));
		s->Planets.Add(Archive("Luyten b", host, 18.6498, 0.091101, 0.10, 2.89, 1.51, cs::Double::NaN, 2017, "Astudillo-Defru et al. 2017",
			"GJ 273 b: a super-Earth of 2.9 Earth masses at least on an 18.6-day orbit. Orbital plane and phase assumed."));
		s->Planets.Add(Candidate("Luyten d", host, 413.9, 0.712, 0.17, 10.8, 2019, "Tuomi et al. 2019",
			"CANDIDATE: a cold mini-Neptune on a 414-day orbit. Radius estimated; plane and phase assumed."));
		s->Planets.Add(Candidate("Luyten e", host, 542, 0.849, 0.03, 9.3, 2019, "Tuomi et al. 2019",
			"CANDIDATE: a second cold mini-Neptune on a 542-day orbit. Radius estimated; plane and phase assumed."));
		return s;
	}

	// ---------------------------------------------------------------- 12.50 ly

	cs::Ref<SystemSpec> NearbySystems::TeegardensStar()
	{
		const cs::String host = "Teegarden's Star";
		auto s = NewSystem("Teegarden's Star", Astro::Hms(2, 53, 0.89179), Astro::Dms(16, 52, 52.6322), SystemSpec::LyFromParallax(260.9884),
			StellarNode::Single(Star(host, "M7.0V", 0.097, 0.120, 0.000722, 3034, 98.05,
				"A tiny, cool red dwarf in Aries, under a tenth of the Sun's mass; found only in 2003.")));
		s->Summary = "A tiny red dwarf with three Earth-mass planets, the inner one among the most Earth-like known.";
		s->Planets.Add(Archive("Teegarden's Star b", host, 4.90634, 0.0259, 0.03, 1.16, 1.05, 338, 2019, "Dreizler et al. 2024",
			"An Earth-mass world (1.16 at least) receiving about as much light as Earth. Orbital plane assumed."));
		s->Planets.Add(Archive("Teegarden's Star c", host, 11.416, 0.0455, 0.04, 1.05, 1.02, 301, 2019, "Dreizler et al. 2024",
			"An Earth-mass world on an 11.4-day orbit, getting about a third of Earth's light. Orbital plane assumed."));
		s->Planets.Add(Archive("Teegarden's Star d", host, 26.13, 0.0791, 0.07, 0.82, 0.954, 345, 2024, "Dreizler et al. 2024",
			"The outermost, 0.82 Earth masses at least, cold on a 26-day orbit. Orbital plane assumed."));
		return s;
	}
}
