// Port of Assets/UnitySpace/Core/Systems/AlphaCentauri.cs (US-T17).

#include "Systems/AlphaCentauri.h"
#include "Systems/StarSystemSpec.h"
#include "OrbitalElements.h"

namespace SpaceGod::Core
{
	const cs::Ref<BinaryOrbitSpec>& AlphaCentauri::Orbit()
	{
		static const cs::Ref<BinaryOrbitSpec> orbit = []
		{
			auto o = cs::New<BinaryOrbitSpec>();
			o->PeriodYears = 79.762; o->AAu = 23.299; o->E = 0.51947; o->IDeg = 79.243; o->NodeDeg = 205.073; o->ArgPeriDeg = 231.519; o->PeriastronYear = 1875.66;
			return o;
		}();
		return orbit;
	}

	DVec3 AlphaCentauri::Direction() { return Astro::DirectionFromRaDec(RaDeg, DecDeg); }

	DVec3 AlphaCentauri::PositionLy() { return Direction() * DistanceLy; }

	DVec3 AlphaCentauri::ProximaOffsetKm()
	{
		return (Astro::DirectionFromRaDec(ProximaRaDeg, ProximaDecDeg) * ProximaDistanceLy - PositionLy()) * Astro::LightYearKm;
	}

	DVec3 AlphaCentauri::BinaryNormal()
	{
		DVec3 unused, normal;
		OrbitalElements::VisualBinaryGeometry(RaDeg, DecDeg, Orbit()->IDeg, Orbit()->NodeDeg, Orbit()->ArgPeriDeg, unused, normal);
		return normal;
	}

	cs::Ref<SystemSpec> AlphaCentauri::Spec()
	{
		auto a = cs::New<StarSpec>();
		a->Name = "Alpha Centauri A"; a->Spectral = "G2V"; a->MassSolar = MassA; a->RadiusSolar = 1.223; a->LuminositySolar = 1.5059; a->TeffK = 5748; a->RotationDays = 28.3;
		a->Summary = "Rigil Kentaurus. G2V, 1.08 solar masses, half again as luminous as the Sun. Pole assumed along the A-B orbit's.";
		auto b = cs::New<StarSpec>();
		b->Name = "Alpha Centauri B"; b->Spectral = "K1V"; b->MassSolar = MassB; b->RadiusSolar = 0.864; b->LuminositySolar = 0.4981; b->TeffK = 5154; b->RotationDays = 36.7;
		b->Summary = cs::String("Toliman. K1V, an orange dwarf. Swings between 11.2 and 35.4 AU from A on an 80-year orbit. ")
			+ "No planet confirmed (the 2012 \"Bb\" was refuted).";
		auto proxima = cs::New<StarSpec>();
		proxima->Name = "Proxima Centauri"; proxima->Spectral = "M5.5Ve"; proxima->MassSolar = MassProxima; proxima->RadiusSolar = 0.1542; proxima->LuminositySolar = 0.001567; proxima->TeffK = 2992; proxima->RotationDays = 84.9;
		proxima->Summary = cs::String("The nearest star to the Sun: an M5.5 red dwarf flare star, 4.25 light-years away. ")
			+ "Orbits A and B every ~500,000 years; its orbital plane is assumed.";

		auto spec = cs::New<SystemSpec>();
		spec->Name = "Alpha Centauri"; spec->RaDeg = RaDeg; spec->DecDeg = DecDeg; spec->DistanceLy = DistanceLy;
		spec->Primary = StellarNode::Pair("Alpha Centauri", a, b, Orbit(), "The centre of mass that Alpha Centauri A and B circle every 79.8 years.");
		spec->OverviewSpanAu = 22;
		spec->Summary = "The Sun's nearest neighbour: a Sun-like binary and a red dwarf a fifth of a light-year out.";

		// Proxima: on a 0.5-eccentricity orbit about the pair, now near apastron (Kervella et al. 2017:
		// P ≈ 550 kyr, periastron in ~280 kyr), placed where its sky position and distance put it, at
		// mean anomaly 174°. The orbital plane's orientation is assumed (the A-B plane, tipped to contain the offset).
		{
			auto c = cs::New<CompanionSpec>();
			c->Node = StellarNode::Single(proxima); c->RaDeg = ProximaRaDeg; c->DecDeg = ProximaDecDeg; c->DistanceLy = ProximaDistanceLy;
			c->E = 0.50; c->MeanAnomalyDeg = 173.7;
			spec->Companions.Add(c);
		}

		// Proxima's planets: periods, separations, eccentricities and minimum masses from the NASA
		// Exoplanet Archive (radii are its mass-radius estimates); the plane (the A-B plane) and phases are assumed.
		{
			auto p = cs::New<PlanetSpec>();
			p->Name = "Proxima d"; p->Host = "Proxima Centauri"; p->PeriodDays = 5.12338; p->AAu = 0.02881; p->E = 0; p->MassEarth = 0.26; p->RadiusEarth = 0.692;
			p->MeanAnomalyDeg = 40; p->DiscoveryYear = 2025; p->Source = "NASA Exoplanet Archive (Suárez Mascareño et al. 2025)";
			p->Color = Rgb(0.55f, 0.42f, 0.36f);
			p->Summary = cs::String("Smallest known planet of Proxima, a quarter of Earth's mass at least, scorched on a 5.1-day orbit. ")
				+ "Confirmed in 2025. Radius estimated; orbital plane and phase assumed.";
			spec->Planets.Add(p);
		}
		{
			auto p = cs::New<PlanetSpec>();
			p->Name = "Proxima b"; p->Host = "Proxima Centauri"; p->PeriodDays = 11.18465; p->AAu = 0.04848; p->E = 0; p->MassEarth = 1.055; p->RadiusEarth = 1.02;
			p->MeanAnomalyDeg = 210; p->DiscoveryYear = 2016; p->Source = "NASA Exoplanet Archive (Suárez Mascareño et al. 2025)";
			p->Color = Rgb(0.5f, 0.45f, 0.42f); p->AtmosphereColor = Rgb(0.75f, 0.55f, 0.5f); p->AtmosphereStrength = 0.3f;
			p->Summary = cs::String("At least 1.06 Earth masses, probably tidally locked: one face in endless day. ")
				+ "Discovered 2016. Radius estimated, atmosphere unknown (drawn thin); orbital plane and phase assumed.";
			spec->Planets.Add(p);
		}
		{
			auto p = cs::New<PlanetSpec>();
			p->Name = "Proxima c"; p->Host = "Proxima Centauri"; p->PeriodDays = 1928; p->AAu = 1.489; p->E = 0.04; p->MassEarth = 7; p->RadiusEarth = 1.6;
			p->MeanAnomalyDeg = 95; p->Status = BodyStatus::Disputed; p->DiscoveryYear = 2020; p->Source = "Damasso et al. 2020";
			p->Color = Rgb(0.72f, 0.76f, 0.8f); p->RotationHours = 30;
			p->Summary = cs::String("DISPUTED. A cold super-Earth claimed in 2020 on a 5.3-year orbit; later data have not confirmed it. ")
				+ "Radius guessed from its 7 Earth masses.";
			spec->Planets.Add(p);
		}

		// The JWST candidate around A, inclined to the A-B plane (Beichman et al. 2025: a ≈ 1-2 AU,
		// e ≈ 0.4, inclined orbit; one of the allowed solutions is used).
		{
			auto p = cs::New<PlanetSpec>();
			p->Name = "Alpha Centauri Ab"; p->Host = "Alpha Centauri A"; p->AAu = 1.9; p->E = 0.4;
			p->PeriodDays = OrbitalElements::PeriodFromGm(1.9 * Astro::AuKm, SunGm * MassA);
			p->MassEarth = 120; p->MassIsMinimum = false; p->RadiusEarth = 1.05 * JupiterRadiusKm / EarthRadiusKm;
			p->TiltFromReferenceDeg = 50; p->MeanAnomalyDeg = 300; p->Status = BodyStatus::Candidate; p->DiscoveryYear = 2025; p->Source = "Beichman et al. 2025";
			p->Color = Rgb(0.78f, 0.72f, 0.62f); p->GasGiant = true; p->RotationHours = 10.5;
			p->AtmosphereColor = Rgb(0.85f, 0.78f, 0.65f); p->AtmosphereStrength = 0.3f;
			p->Summary = cs::String("CANDIDATE \"S1\": a Saturn-mass gas giant glimpsed by JWST in August 2024, ")
				+ "not yet seen again. Orbit is one of the solutions allowed (a 1.9 AU, e 0.4, tilted 50°).";
			spec->Planets.Add(p);
		}

		// Game layer: free-flying stations on their own orbits about each star.
		auto station = [&](const char* name, const char* host, double aAu, double meanAnomalyDeg, const char* summary)
		{
			auto s = cs::New<StationSpec>();
			s->Name = name; s->Host = host; s->AAu = aAu; s->MeanAnomalyDeg = meanAnomalyDeg; s->Summary = summary;
			spec->Stations.Add(s);
		};
		station("Rigil Gate", "Alpha Centauri A", 1.2, 20,
			"Fiction. The Toliman Combine's trade hub in A's habitable zone, where the long lanes meet.");
		station("Toliman Forge", "Alpha Centauri B", 0.7, 140,
			"Fiction. Orbital foundry and shipyard of the Toliman Combine, in B's habitable zone.");
		station("Blackhollow", "Proxima Centauri", 0.5, 260,
			"Fiction. A derelict survey station the Long Dark took over, dark against a dim star.");
		return spec;
	}

	cs::List<cs::Ref<BodyDefinition>> AlphaCentauri::Definitions() { return StarSystemBuilder::Build(Spec())->Bodies; }

	Rgb AlphaCentauri::StarColor(double teffK)
	{
		double t = teffK / 100;
		double r, g, b;
		if (t <= 66)
		{
			r = 255;
			g = 99.4708025861 * cs::Math::Log(t) - 161.1195681661;
			b = t <= 19 ? 0 : 138.5177312231 * cs::Math::Log(t - 10) - 305.0447927307;
		}
		else
		{
			r = 329.698727446 * cs::Math::Pow(t - 60, -0.1332047592);
			g = 288.1221695283 * cs::Math::Pow(t - 60, -0.0755148492);
			b = 255;
		}
		auto C = [](double v) { return static_cast<float>(cs::Math::Max(0.0, cs::Math::Min(255.0, v)) / 255); };
		return Rgb(C(r), C(g), C(b));
	}
}
