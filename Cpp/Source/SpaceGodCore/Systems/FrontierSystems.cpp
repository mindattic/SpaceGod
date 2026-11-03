// Port of Assets/UnitySpace/Core/Systems/FrontierSystems.cs (US-T17).

#include "Systems/FrontierSystems.h"
#include "Systems/CsGaps.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/StarSystemBuilder.h"
#include "Systems/NearbySystems.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	cs::List<cs::Ref<SystemSpec>> FrontierSystems::All()
	{
		cs::List<cs::Ref<SystemSpec>> r;
		r.Add(Wise0855());
		r.Add(Gliese65());
		r.Add(Ross154());
		r.Add(Ross248());
		r.Add(Gliese887());
		r.Add(Cygni61());
		r.Add(Procyon());
		r.Add(Struve2398());
		r.Add(Groombridge34());
		r.Add(DxCancri());
		return r;
	}

	cs::Ref<SystemSpec> FrontierSystems::Find(const cs::String& name)
	{
		for (const auto& s : All())
			if (EqualsNoCase(s->Name, name)) return s;
		return nullptr;
	}

	cs::Ref<SystemSpec> FrontierSystems::System(const cs::String& name, double rh, double rm, double rs, double dd, double dm, double ds, bool south, double ly,
		const cs::Ref<StarSpec>& star, const cs::String& summary)
	{
		auto s = cs::New<SystemSpec>();
		s->Name = name; s->NeighbourName = name;
		s->RaDeg = Astro::Hms(rh, rm, rs); s->DecDeg = Astro::Dms(dd, dm, ds, south); s->DistanceLy = ly;
		s->Primary = StellarNode::Single(star); s->Summary = summary;
		s->Stations.Add(Habitat(name, star));
		return s;
	}

	cs::Ref<StationSpec> FrontierSystems::Habitat(const cs::String& system, const cs::Ref<StarSpec>& star)
	{
		double inner, outer;
		HabitableZones::ConservativeEdgesAu(star->LuminositySolar, star->TeffK, inner, outer);
		double aAu = cs::Math::Max(cs::Math::Sqrt(inner * outer), HabitatMinRadii * star->RadiusSolar * StarSystemBuilder::SunRadiusKm / Astro::AuKm);
		auto st = cs::New<StationSpec>();
		st->Name = system + " Habitat"; st->Host = star->Name; st->AAu = aAu; st->MeanAnomalyDeg = 0; st->HabitatSite = true;
		st->Summary = cs::String("An orbit marked out for a settlers' habitat, in the habitable zone where the star has one clear of it; ") +
			"empty until a colony ship comes through the gate. Fiction.";
		return st;
	}

	cs::Ref<StarSpec> FrontierSystems::Star(const cs::String& name, const cs::String& spectral, double mass, double radius, double luminosity, double teff, double rotationDays, const cs::String& summary)
	{
		auto s = cs::New<StarSpec>();
		s->Name = name; s->Spectral = spectral; s->MassSolar = mass; s->RadiusSolar = radius; s->LuminositySolar = luminosity; s->TeffK = teff;
		s->RotationDays = rotationDays; s->Summary = summary;
		return s;
	}

	cs::Ref<PlanetSpec> FrontierSystems::Planet(const cs::String& name, const cs::String& host, double periodDays, double aAu, double e, double minMassEarth, int year, const cs::String& reference, const cs::String& summary)
	{
		auto p = cs::New<PlanetSpec>();
		p->Name = name; p->Host = host; p->PeriodDays = periodDays; p->AAu = aAu; p->E = e; p->MassEarth = minMassEarth;
		p->RadiusEarth = NearbySystems::RadiusFromMass(minMassEarth); p->MassIsMinimum = true; p->DiscoveryYear = year;
		p->Source = reference; p->Summary = summary;
		return p;
	}

	// ---------------------------------------------------------------- 7.43 ly

	cs::Ref<SystemSpec> FrontierSystems::Wise0855()
	{
		return System("WISE 0855−0714", 8, 55, 10.8, 7, 14, 43, true, 7.430,
			Star("WISE 0855−0714", "Y4", 5 * JupiterMassSolar, 0.10, LuminosityOf(0.10, 285), 285, 0.4,
				cs::String("The coldest brown dwarf known, about freezing point at its cloud tops, a few Jupiter masses. Found by WISE in 2014. ")
				+ "Mass uncertain (3 to 10 Jupiter masses); rotation assumed."),
			"A lone, sunless brown dwarf colder than the North Pole: a gate site with nothing to warm it.");
	}

	// ---------------------------------------------------------------- 8.77 ly

	cs::Ref<SystemSpec> FrontierSystems::Gliese65()
	{
		return System("Gliese 65", 1, 39, 1.3, 17, 57, 1, true, 8.770,
			Star("Gliese 65", "M5.5Ve", 0.1225, 0.165, LuminosityOf(0.165, 2670), 2670, 0.243,
				cs::String("BL Ceti, the brighter of the Luyten 726-8 pair of flare stars; its twin UV Ceti, 5 AU off, gave its name to the class. ")
				+ "Only A is simulated."),
			"Two tiny flare stars in a 26-year orbit, the nearest pair of their kind.");
	}

	// ---------------------------------------------------------------- 9.71 ly

	cs::Ref<SystemSpec> FrontierSystems::Ross154()
	{
		return System("Ross 154", 18, 49, 49.4, 23, 50, 10, true, 9.7063,
			Star("Ross 154", "M3.5Ve", 0.177, 0.240, 0.0038, 3248, 2.85,
				"A young, fast-spinning flare star in Sagittarius, a sixth of the Sun's mass."),
			"A lone red flare star with no planet yet found.");
	}

	// ---------------------------------------------------------------- 10.31 ly

	cs::Ref<SystemSpec> FrontierSystems::Ross248()
	{
		return System("Ross 248", 23, 41, 54.7, 44, 10, 30, false, 10.3057,
			Star("Ross 248", "M5.5Ve", 0.136, 0.160, 0.0018, 2799, 4.2,
				"A small flare star in Andromeda, closing on the Sun: in some 36,000 years it will be the nearest star of all."),
			"A dim red dwarf heading our way.");
	}

	// ---------------------------------------------------------------- 10.72 ly

	cs::Ref<SystemSpec> FrontierSystems::Gliese887()
	{
		const cs::String host = "Gliese 887";
		auto s = System(host, 23, 5, 52.0, 35, 51, 11, true, 10.7241,
			Star(host, "M0.5V", 0.489, 0.471, 0.0368, 3688, 200,
				"Lacaille 9352, the brightest red dwarf in the sky and one of the quietest: barely a flare in years of watching."),
			"A bright, calm red dwarf with two hot super-Earths.");
		s->Planets.Add(Planet("Gliese 887 b", host, 9.262, 0.068, 0.09, 4.2, 2020, "Jeffers et al. 2020",
			"A hot super-Earth, 4.2 Earth masses at least, on a 9-day orbit. Radius estimated; plane and phase assumed."));
		s->Planets.Add(Planet("Gliese 887 c", host, 21.789, 0.120, 0.22, 7.6, 2020, "Jeffers et al. 2020",
			"A warm super-Earth, 7.6 Earth masses at least, a little closer in than the habitable zone. Radius estimated; plane and phase assumed."));
		return s;
	}

	// ---------------------------------------------------------------- 11.40 ly

	cs::Ref<SystemSpec> FrontierSystems::Cygni61()
	{
		return System("61 Cygni", 21, 6, 53.9, 38, 44, 58, false, 11.4039,
			Star("61 Cygni A", "K5.0V", 0.70, 0.665, 0.153, 4526, 35.4,
				cs::String("The first star to have its distance measured (Bessel, 1838), an orange dwarf; its companion 61 Cygni B, ")
				+ "84 AU off on a 680-year orbit, is not simulated."),
			"Bessel's star: a pair of orange dwarfs, the nearest naked-eye stars of their kind in the north.");
	}

	// ---------------------------------------------------------------- 11.46 ly

	cs::Ref<SystemSpec> FrontierSystems::Procyon()
	{
		return System("Procyon", 7, 39, 18.1, 5, 13, 30, false, 11.463,
			Star("Procyon", "F5IV–V", 1.478, 2.048, 6.93, 6530, 23,
				cs::String("The Little Dog Star, the eighth brightest in the sky: a yellow-white star leaving the main sequence. ")
				+ "Its white dwarf companion, Procyon B, on a 41-year orbit, is not simulated."),
			"A bright yellow-white star with a white dwarf companion and no known planets.");
	}

	// ---------------------------------------------------------------- 11.49 ly

	cs::Ref<SystemSpec> FrontierSystems::Struve2398()
	{
		return System("Struve 2398", 18, 42, 46.7, 59, 37, 49, false, 11.4908,
			Star("Struve 2398", "M3.0V", 0.334, 0.351, LuminosityOf(0.351, 3400), 3400, 40,
				"The brighter of a pair of red dwarfs in Draco, 50 AU apart; only A is simulated. Rotation assumed."),
			"A wide pair of red dwarfs in Draco.");
	}

	// ---------------------------------------------------------------- 11.62 ly

	cs::Ref<SystemSpec> FrontierSystems::Groombridge34()
	{
		const cs::String host = "Groombridge 34";
		auto s = System(host, 0, 18, 22.9, 44, 1, 23, false, 11.6191,
			Star(host, "M1.5V", 0.38, 0.38, LuminosityOf(0.38, 3700), 3700, 44,
				"Groombridge 34 A, the brighter of a wide pair of red dwarfs in Andromeda; B, 147 AU off, is not simulated."),
			"A wide pair of red dwarfs, the brighter with a hot super-Earth.");
		s->Planets.Add(Planet("Groombridge 34 Ab", host, 11.4433, 0.0717, 0.09, 3.03, 2014, "Howard et al. 2014; Pinamonti et al. 2018",
			"A hot super-Earth of 3 Earth masses at least on an 11-day orbit. Radius estimated; plane and phase assumed."));
		return s;
	}

	// ---------------------------------------------------------------- 11.68 ly

	cs::Ref<SystemSpec> FrontierSystems::DxCancri()
	{
		return System("DX Cancri", 8, 29, 49.5, 26, 46, 37, false, 11.6797,
			Star("DX Cancri", "M6.5Ve", 0.087, 0.11, LuminosityOf(0.11, 2700), 2700, 1.4,
				"A tiny flare star in Cancer, under a tenth of the Sun's mass. Rotation assumed."),
			"One of the faintest red dwarfs near the Sun.");
	}
}
