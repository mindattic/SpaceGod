#include "Sim/BodyCatalog.h"
#include "Sim/Astro.h"

/*
 * Sources (as in Unity.SpaceGod's BodyCatalog.cs):
 *  - Planets: JPL "Keplerian Elements for Approximate Positions of the Major Planets", table 1 (1800-2050), Standish.
 *    Earth's entry is the Earth-Moon barycentre.
 *  - Eris: JPL SBDB osculating elements, epoch JD 2459000.5. Ceres: mean elements fitted to JPL Horizons, 2000-2050.
 *  - Psyche, Lutetia, Ida and the Trojans Hektor, Patroclus, Eurybates: JPL SBDB, epoch JD 2461200.5.
 *  - The Moon: ELP-2000/82 (see LunarTheory); its catalogue elements are the mean orbit, used only to draw its path.
 *  - Other moons: JPL satellite mean elements (epoch J2000) in each Laplace plane or parent equator, refit to Horizons.
 *  - Radii, GM, poles and rotation: IAU WGCCRE report and NASA planetary fact sheets.
 */

FBodyCatalog::FBodyCatalog(TArray<FBodyDefinition> Definitions)
	: Bodies(MoveTemp(Definitions))
{
	for (int32 I = 0; I < Bodies.Num(); ++I)
	{
		Bodies[I].Index = I;
		ByName.Add(Bodies[I].Name, I);
	}

	Children.SetNum(Bodies.Num());

	for (FBodyDefinition& B : Bodies)
	{
		if (!B.ParentName.IsEmpty())
		{
			const int32* Parent = ByName.Find(B.ParentName);
			checkf(Parent, TEXT("%s: unknown parent %s"), *B.Name, *B.ParentName);
			checkf(*Parent < B.Index, TEXT("%s: parent %s must be listed first"), *B.Name, *B.ParentName);
			B.ParentIndex = *Parent;
			Children[*Parent].Add(B.Index);
		}

		if (!FMath::IsNaN(B.PoleRaDeg))
			B.Pole = Astro::PoleFromRaDec(B.PoleRaDeg, B.PoleDecDeg);
		else if (B.Plane == EOrbitPlane::ParentEquator && B.ParentIndex >= 0)
			B.Pole = Bodies[B.ParentIndex].SpinAxis(); // synchronous moon: spins about its orbit normal
		else
			B.Pole = FVector::ZAxisVector;
	}

	for (FBodyDefinition& B : Bodies)
	{
		if (B.ParentIndex < 0 || !B.Elements.IsSet()) continue;
		FBodyDefinition& Parent = Bodies[B.ParentIndex];
		const FOrbitalElements& El = B.Elements.GetValue();
		const double Apoapsis = El.AKm * FMath::Abs(B.OrbitScale) * (1 + El.E);
		const double Periapsis = El.AKm * FMath::Abs(B.OrbitScale) * (1 - El.E);
		const double Soi = El.AKm * std::pow(FMath::Max(B.Gm, 0.0) / Parent.Gm, 0.4);
		B.SoiKm = FMath::Max(Soi, B.RadiusKm * 3.0);
		if (!Parent.UsesStellarMapping())
			Parent.MoonReachKm = FMath::Max(Parent.MoonReachKm, Apoapsis);
		else if (Parent.ParentIndex >= 0)
			Parent.SystemReachKm = FMath::Max(Parent.SystemReachKm, Apoapsis);
		Parent.InnermostChildKm = Parent.InnermostChildKm > 0 ? FMath::Min(Parent.InnermostChildKm, Periapsis) : Periapsis;
	}
}

FString BodyKindName(EBodyKind Kind)
{
	switch (Kind)
	{
	case EBodyKind::Star: return TEXT("Star");
	case EBodyKind::Planet: return TEXT("Planet");
	case EBodyKind::DwarfPlanet: return TEXT("Dwarf planet");
	case EBodyKind::Moon: return TEXT("Moon");
	case EBodyKind::Barycentre: return TEXT("Barycentre");
	case EBodyKind::Station: return TEXT("Station");
	case EBodyKind::Asteroid: return TEXT("Asteroid");
	}
	return TEXT("Body");
}

namespace
{
	/** A regular satellite with synchronous rotation; elements at J2000 in its Laplace plane or parent equator. Rates deg/day. */
	FBodyDefinition Moon(const TCHAR* Name, const TCHAR* Parent, double RadiusKm, double Gm, double AKm, double E, double IDeg,
		double NodeDeg, double ArgPeriDeg, double MeanAnomalyDeg, double PeriodDays, double NodeRate, double VarpiRate,
		double LaplaceRaDeg, double LaplaceDecDeg, FRgb Color)
	{
		FBodyDefinition B;
		B.Name = Name;
		B.Kind = EBodyKind::Moon;
		B.ParentName = Parent;
		B.RadiusKm = RadiusKm;
		B.Gm = Gm;
		B.Elements = FOrbitalElements::FromClassical(Astro::J2000, AKm, E, IDeg, NodeDeg, ArgPeriDeg, MeanAnomalyDeg, PeriodDays,
			NodeRate, VarpiRate);
		B.Plane = EOrbitPlane::ParentEquator;
		B.LaplacePoleRaDeg = LaplaceRaDeg;
		B.LaplacePoleDecDeg = LaplaceDecDeg;
		B.RotationHours = PeriodDays * 24.0;
		B.bFacesParent = true;
		B.Color = Color;
		return B;
	}

	FBodyDefinition Moon(const TCHAR* Name, const TCHAR* Parent, double RadiusKm, double Gm, double AKm, double E, double IDeg,
		double NodeDeg, double ArgPeriDeg, double MeanAnomalyDeg, double PeriodDays, double NodeRate, double VarpiRate, FRgb Color)
	{
		return Moon(Name, Parent, RadiusKm, Gm, AKm, E, IDeg, NodeDeg, ArgPeriDeg, MeanAnomalyDeg, PeriodDays, NodeRate, VarpiRate,
			Astro::NaN(), Astro::NaN(), Color);
	}

	/** Attaches a colour map (SourceArt/Textures; sources in SOURCES.md). Single-band mosaics are tinted with Color. */
	FBodyDefinition WithMap(FBodyDefinition B, const TCHAR* Texture, bool bTint, bool bMapNorthFlipped = false)
	{
		B.Texture = Texture;
		B.bTintTexture = bTint;
		B.bMapNorthFlipped = bMapNorthFlipped;
		return B;
	}

	/** A minor planet on its JPL SBDB osculating orbit (heliocentric ecliptic J2000, epoch JD 2461200.5). */
	FBodyDefinition Asteroid(const TCHAR* Name, double RadiusKm, double Gm, double AAu, double E, double IDeg, double NodeDeg,
		double ArgPeriDeg, double MeanAnomalyDeg, double RotationHours, FRgb Color, const TCHAR* Summary)
	{
		constexpr double Epoch = 2461200.5;
		FBodyDefinition B;
		B.Name = Name;
		B.Kind = EBodyKind::Asteroid;
		B.ParentName = TEXT("Sun");
		B.RadiusKm = RadiusKm;
		B.Gm = Gm;
		B.Elements = FOrbitalElements::FromClassical(Epoch, AAu * Astro::AuKm, E, IDeg, NodeDeg, ArgPeriDeg, MeanAnomalyDeg,
			FOrbitalElements::PeriodFromGm(AAu * Astro::AuKm, 1.32712440018e11));
		B.RotationHours = RotationHours;
		B.Color = Color;
		B.Summary = Summary;
		return B;
	}

	FBodyDefinition Trojan(FBodyDefinition B, ELagrangePoint Point)
	{
		B.TrojanPoint = Point;
		B.CoOrbitalWith = TEXT("Jupiter");
		return B;
	}

	TArray<FBodyDefinition> SolDefinitions()
	{
		constexpr double SbdbEpoch = 2459000.5;
		TArray<FBodyDefinition> Out;

		{
			FBodyDefinition B;
			B.Name = TEXT("Sun"); B.Kind = EBodyKind::Star; B.RadiusKm = 695700; B.Gm = 1.32712440018e11;
			B.PoleRaDeg = 286.13; B.PoleDecDeg = 63.87; B.RotationHours = 609.12;
			B.PrimeMeridianDeg = 84.176; B.PrimeMeridianRateDegPerDay = 14.1844;
			B.Color = FRgb(1.f, 0.86f, 0.6f);
			B.Summary = TEXT("G2V main-sequence star. 99.86% of the system's mass.");
			Out.Add(B);
		}
		{
			FBodyDefinition B;
			B.Name = TEXT("Mercury"); B.ParentName = TEXT("Sun"); B.RadiusKm = 2439.7; B.Gm = 22031.8;
			B.Elements = FOrbitalElements::FromJpl(0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593,
				0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081);
			B.PoleRaDeg = 281.01; B.PoleDecDeg = 61.42; B.RotationHours = 1407.6;
			B.PrimeMeridianDeg = 329.5988; B.PrimeMeridianRateDegPerDay = 6.1385108;
			B.Color = FRgb(0.6f, 0.58f, 0.55f); B.Texture = TEXT("mercury_color"); B.TexturePoleMargin = 0.035f; B.NormalMap = TEXT("mercury_normal");
			B.Summary = TEXT("Smallest planet; a 3:2 spin-orbit resonance gives it a 176-day solar day.");
			Out.Add(B);
		}
		{
			FBodyDefinition B;
			B.Name = TEXT("Venus"); B.ParentName = TEXT("Sun"); B.RadiusKm = 6051.8; B.Gm = 324858.6;
			B.Elements = FOrbitalElements::FromJpl(0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255,
				0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418);
			B.PoleRaDeg = 272.76; B.PoleDecDeg = 67.16; B.RotationHours = -5832.6;
			B.PrimeMeridianDeg = 160.20; B.PrimeMeridianRateDegPerDay = -1.4813688;
			// Visible light never reaches the surface: what an eye sees is the nearly featureless cloud deck.
			B.Color = FRgb(0.93f, 0.86f, 0.68f); B.SurfaceHaze = 1.f;
			B.AtmosphereColor = FRgb(1.f, 0.88f, 0.6f); B.AtmosphereStrength = 0.7f;
			B.Summary = TEXT("Spins backwards under a 92-bar CO2 atmosphere. Its clouds hide the surface from every visible-light camera.");
			Out.Add(B);
		}
		{
			FBodyDefinition B;
			B.Name = TEXT("Earth"); B.ParentName = TEXT("Sun"); B.RadiusKm = 6371.0; B.Gm = 398600.435;
			B.Elements = FOrbitalElements::FromJpl(1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0,
				0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0);
			B.PoleRaDeg = 0; B.PoleDecDeg = 90; B.RotationHours = 23.9344696;
			B.PrimeMeridianDeg = 190.147; B.PrimeMeridianRateDegPerDay = 360.9856235;
			B.Color = FRgb(0.3f, 0.45f, 0.7f); B.Texture = TEXT("earth_color");
			B.AtmosphereColor = FRgb(0.4f, 0.65f, 1.f); B.AtmosphereStrength = 0.65f;
			B.Summary = TEXT("Home. Elements are for the Earth-Moon barycentre; Earth is offset from it by the Moon.");
			Out.Add(B);
		}
		{
			FBodyDefinition B;
			B.Name = TEXT("Moon"); B.Kind = EBodyKind::Moon; B.ParentName = TEXT("Earth"); B.RadiusKm = 1737.4; B.Gm = 4902.8;
			// Mean elements at J2000: L = 218.3165, perigee longitude 83.3532, node 125.0445.
			B.Elements = FOrbitalElements::FromClassical(Astro::J2000, 384400, 0.0549, 5.145, 125.0445, 83.3532 - 125.0445,
				218.3165 - 83.3532, 27.321661, -360.0 / 6798.38, 360.0 / 3232.6);
			B.PoleRaDeg = 269.99; B.PoleDecDeg = 66.54; B.RotationHours = 655.72; B.bFacesParent = true;
			B.Color = FRgb(0.62f, 0.61f, 0.6f); B.Texture = TEXT("moon_color"); B.NormalMap = TEXT("moon_normal");
			B.Summary = TEXT("Tidally locked. Slowly receding from Earth at 3.8 cm a year.");
			Out.Add(B);
		}
		{
			FBodyDefinition B;
			B.Name = TEXT("Mars"); B.ParentName = TEXT("Sun"); B.RadiusKm = 3389.5; B.Gm = 42828.37;
			B.Elements = FOrbitalElements::FromJpl(1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891,
				0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343);
			B.PoleRaDeg = 317.68; B.PoleDecDeg = 52.89; B.RotationHours = 24.6229;
			B.PrimeMeridianDeg = 176.630; B.PrimeMeridianRateDegPerDay = 350.89198226;
			B.Color = FRgb(0.75f, 0.45f, 0.3f); B.Texture = TEXT("mars_color"); B.NormalMap = TEXT("mars_normal");
			B.AtmosphereColor = FRgb(0.95f, 0.6f, 0.4f); B.AtmosphereStrength = 0.2f;
			B.Summary = TEXT("Home of Olympus Mons and Valles Marineris.");
			Out.Add(B);
		}
		{
			// Phobos spirals in under Mars's tides: its mean longitude gains 1.26e-3 deg/yr^2.
			FBodyDefinition Phobos = Moon(TEXT("Phobos"), TEXT("Mars"), 11.27, 7.087e-4, 9374.92, 0.015125, 1.07438, 128.2222, 216.4756,
				189.5918, 0.3189101053, -0.4357808, 0.4351860, 317.7, 52.9, FRgb(0.42f, 0.38f, 0.35f));
			Phobos.Elements = FOrbitalElements::FromClassical(Astro::J2000, 9374.92, 0.015125, 1.07438, 128.2222, 216.4756, 189.5918,
				0.3189101053, -0.4357808, 0.4351860, 1.2614e-3);
			Out.Add(WithMap(Phobos, TEXT("phobos_color"), true));
			Out.Add(WithMap(Moon(TEXT("Deimos"), TEXT("Mars"), 6.2, 9.6e-5, 23457, 0.0, 1.7878, 13.753, 352.1363, 212.2813, 1.26244093,
				-0.018163, -0.036847, 316.6, 53.5, FRgb(0.55f, 0.5f, 0.45f)), TEXT("deimos_color"), true));
		}
		{
			FBodyDefinition B;
			B.Name = TEXT("Ceres"); B.Kind = EBodyKind::DwarfPlanet; B.ParentName = TEXT("Sun"); B.RadiusKm = 469.7; B.Gm = 62.6284;
			B.Elements = FOrbitalElements::FromClassical(Astro::J2000, 413958905.2, 0.0777927, 10.589118, 80.537317, 72.967047,
				7.312862, 1681.58004, -3.7765e-5, 1.0614e-5);
			B.PoleRaDeg = 291.42; B.PoleDecDeg = 66.76; B.RotationHours = 9.074;
			B.PrimeMeridianDeg = 170.650; B.PrimeMeridianRateDegPerDay = 952.1532;
			B.Color = FRgb(0.5f, 0.49f, 0.47f); B.Texture = TEXT("ceres_color"); B.bTintTexture = true;
			B.Summary = TEXT("Largest object in the asteroid belt. Bright salt deposits in Occator crater.");
			Out.Add(B);
		}
		{
			FBodyDefinition B;
			B.Name = TEXT("Jupiter"); B.ParentName = TEXT("Sun"); B.RadiusKm = 69911; B.Gm = 1.26686534e8;
			B.Elements = FOrbitalElements::FromJpl(5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909,
				-0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106);
			B.PoleRaDeg = 268.06; B.PoleDecDeg = 64.50; B.RotationHours = 9.925;
			B.PrimeMeridianDeg = 284.95; B.PrimeMeridianRateDegPerDay = 870.536; // System III
			B.Color = FRgb(0.8f, 0.7f, 0.58f); B.Texture = TEXT("jupiter_color"); B.TexturePoleMargin = 0.06f;
			B.bGasGiant = true;
			B.AtmosphereColor = FRgb(0.9f, 0.8f, 0.65f); B.AtmosphereStrength = 0.3f;
			B.Summary = TEXT("Two and a half times the mass of every other planet combined.");
			Out.Add(B);
		}
		Out.Add(WithMap(Moon(TEXT("Io"), TEXT("Jupiter"), 1821.6, 5959.9, 421800, 0.00413, 0.0, 20.0402, 49.0252, 330.8831, 1.76913776, 0, -0.73951, 268.1, 64.5, FRgb(0.93f, 0.83f, 0.45f)), TEXT("io_color"), false));
		Out.Add(WithMap(Moon(TEXT("Europa"), TEXT("Jupiter"), 1560.8, 3202.7, 671100, 0.00928, 0.4643, 204.4438, 44.6561, 345.4036, 3.55118104, -0.032732, -0.739518, 268.1, 64.5, FRgb(0.86f, 0.8f, 0.72f)), TEXT("europa_color"), true));
		Out.Add(WithMap(Moon(TEXT("Ganymede"), TEXT("Jupiter"), 2634.1, 9887.8, 1070400, 0.00194, 0.2299, 59.8522, 246.3051, 295.4604, 7.15455318, -0.005846, 0.005568, 268.2, 64.6, FRgb(0.6f, 0.57f, 0.52f)), TEXT("ganymede_color"), false));
		Out.Add(WithMap(Moon(TEXT("Callisto"), TEXT("Jupiter"), 2410.3, 7179.3, 1882700, 0.00727, 0.2197, 314.8165, 55.8575, 86.0337, 16.68901692, -0.001697, 0.001773, 268.7, 64.8, FRgb(0.42f, 0.39f, 0.35f)), TEXT("callisto_color"), true));

		{
			FBodyDefinition B;
			B.Name = TEXT("Saturn"); B.ParentName = TEXT("Sun"); B.RadiusKm = 58232; B.Gm = 3.7931187e7;
			B.Elements = FOrbitalElements::FromJpl(9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448,
				-0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794);
			B.PoleRaDeg = 40.59; B.PoleDecDeg = 83.54; B.RotationHours = 10.656;
			B.PrimeMeridianDeg = 38.90; B.PrimeMeridianRateDegPerDay = 810.7939024;
			B.Color = FRgb(0.85f, 0.77f, 0.6f); B.Texture = TEXT("saturn_color"); B.bGasGiant = true;
			B.AtmosphereColor = FRgb(0.95f, 0.85f, 0.6f); B.AtmosphereStrength = 0.3f;
			// C ring inner edge to A ring outer edge.
			B.RingInnerKm = 74658; B.RingOuterKm = 136775;
			B.Summary = TEXT("Less dense than water. Its main rings are ~10 m thick and 270,000 km across.");
			Out.Add(B);
		}
		Out.Add(WithMap(Moon(TEXT("Enceladus"), TEXT("Saturn"), 252.1, 7.21, 238400, 0.00476, 0.0, 320.0881, 171.4837, 10.7909, 1.3702181, 0, 0.337987, 40.6, 83.5, FRgb(0.95f, 0.95f, 0.97f)), TEXT("enceladus_color"), true));
		{
			FBodyDefinition Titan = Moon(TEXT("Titan"), TEXT("Saturn"), 2574.7, 8978.1, 1221900, 0.02869, 0.3472, 339.0229, 185.0478, 163.5777,
				15.9454481, -0.001294, 0.001419, 36.4, 84.0, FRgb(0.85f, 0.62f, 0.32f));
			Titan.AtmosphereColor = FRgb(0.9f, 0.65f, 0.3f);
			Titan.AtmosphereStrength = 0.9f;
			// Cassini ISS 938 nm surface map, under the haze that hides it in visible light.
			Titan = WithMap(Titan, TEXT("titan_surface"), false);
			Titan.SurfaceHaze = 0.82f;
			Titan.Summary = TEXT("Only moon with a thick atmosphere; methane lakes at its poles.");
			Out.Add(Titan);
		}
		{
			FBodyDefinition B;
			B.Name = TEXT("Uranus"); B.ParentName = TEXT("Sun"); B.RadiusKm = 25362; B.Gm = 5.793939e6;
			B.Elements = FOrbitalElements::FromJpl(19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503,
				-0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589);
			B.PoleRaDeg = 257.31; B.PoleDecDeg = -15.18; B.RotationHours = -17.24;
			B.PrimeMeridianDeg = 203.81; B.PrimeMeridianRateDegPerDay = -501.1600928;
			B.Color = FRgb(0.65f, 0.85f, 0.9f); B.Texture = TEXT("uranus_color"); B.bGasGiant = true;
			B.AtmosphereColor = FRgb(0.6f, 0.9f, 0.95f); B.AtmosphereStrength = 0.4f;
			B.Summary = TEXT("Rolls around the Sun on its side, axial tilt 98 degrees.");
			Out.Add(B);
		}
		Out.Add(WithMap(Moon(TEXT("Miranda"), TEXT("Uranus"), 235.8, 4.4, 129846, 0.00129, 4.427, 95.7866, 151.4373, 76.6609, 1.41347955, -0.055411, 0.055218, FRgb(0.7f, 0.7f, 0.72f)), TEXT("miranda_color"), true, true));
		Out.Add(WithMap(Moon(TEXT("Titania"), TEXT("Uranus"), 788.4, 228.2, 436298, 0.00158, 0.0645, 331.0516, 249.1247, 56.3287, 8.70586893, 0.00373, 0.003818, FRgb(0.68f, 0.63f, 0.6f)), TEXT("titania_color"), true, true));
		Out.Add(WithMap(Moon(TEXT("Oberon"), TEXT("Uranus"), 761.4, 205.3, 583511, 0.00153, 0.2035, 33.7756, 134.2823, 179.436, 13.46323712, -0.001675, 0.002236, FRgb(0.6f, 0.55f, 0.53f)), TEXT("oberon_color"), true, true));

		{
			FBodyDefinition B;
			B.Name = TEXT("Neptune"); B.ParentName = TEXT("Sun"); B.RadiusKm = 24622; B.Gm = 6.836529e6;
			B.Elements = FOrbitalElements::FromJpl(30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574,
				0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664);
			B.PoleRaDeg = 299.36; B.PoleDecDeg = 43.46; B.RotationHours = 16.11;
			B.PrimeMeridianDeg = 249.978; B.PrimeMeridianRateDegPerDay = 541.1397757;
			B.Color = FRgb(0.35f, 0.5f, 0.9f); B.Texture = TEXT("neptune_color"); B.bGasGiant = true;
			B.AtmosphereColor = FRgb(0.4f, 0.6f, 1.f); B.AtmosphereStrength = 0.4f;
			B.Summary = TEXT("Fastest winds in the system, over 2,000 km/h.");
			Out.Add(B);
		}
		{
			FBodyDefinition Triton = Moon(TEXT("Triton"), TEXT("Neptune"), 1353.4, 1427.6, 354800, 0.00013, 157.2335, 152.2806, 76.2454,
				341.4831, 5.87657447, 0.001458, 0.004035, 299.8, 43.1, FRgb(0.85f, 0.8f, 0.78f));
			Triton.RotationHours = -Triton.RotationHours;
			Triton.Summary = TEXT("Orbits backwards: a captured Kuiper belt object.");
			Out.Add(WithMap(Triton, TEXT("triton_color"), true));
		}
		{
			FBodyDefinition B;
			B.Name = TEXT("Pluto"); B.Kind = EBodyKind::DwarfPlanet; B.ParentName = TEXT("Sun"); B.RadiusKm = 1188.3; B.Gm = 869.6;
			B.Elements = FOrbitalElements::FromJpl(39.48211675, 0.24882730, 17.14001206, 238.92903833, 224.06891629, 110.30393684,
				-0.00031596, 0.00005170, 0.00004818, 145.20780515, -0.04062942, -0.01183482);
			// IAU gives Pluto's positive pole at RA 132.99 Dec -6.16; its antipode is north in the planets' convention.
			B.PoleRaDeg = 312.99; B.PoleDecDeg = 6.16; B.RotationHours = -153.2935;
			// New Horizons maps use the positive pole as north, with W = 302.695 + 56.3625225 d about it.
			B.bMapNorthFlipped = true; B.PrimeMeridianDeg = 302.695; B.PrimeMeridianRateDegPerDay = 56.3625225;
			B.Color = FRgb(0.8f, 0.68f, 0.55f); B.Texture = TEXT("pluto_color"); B.bTintTexture = true;
			B.Summary = TEXT("Its eccentric orbit dips inside Neptune's for 20 years of every 248.");
			Out.Add(B);
		}
		Out.Add(WithMap(Moon(TEXT("Charon"), TEXT("Pluto"), 606, 105.88, 19595.8, 0.00016, 0.0861, 28.2176, 144.2491, 148.7655, 6.387222074, 0.000156, 0.000002, FRgb(0.6f, 0.58f, 0.56f)), TEXT("charon_color"), true));

		{
			FBodyDefinition B;
			B.Name = TEXT("Eris"); B.Kind = EBodyKind::DwarfPlanet; B.ParentName = TEXT("Sun"); B.RadiusKm = 1163; B.Gm = 1108;
			B.Elements = FOrbitalElements::FromClassical(SbdbEpoch, 67.864 * Astro::AuKm, 0.43607, 44.040, 35.951, 151.639,
				205.989, FOrbitalElements::PeriodFromGm(67.864 * Astro::AuKm, 1.32712440018e11));
			B.RotationHours = 378.9;
			B.Color = FRgb(0.92f, 0.92f, 0.9f);
			B.Summary = TEXT("Most massive known dwarf planet; its discovery got Pluto reclassified.");
			Out.Add(B);
		}

		// Three real main-belt asteroids (pirate havens in Unity.SpaceGod's game layer).
		Out.Add(Asteroid(TEXT("Psyche"), 111, 1.601, 2.925720466462538, 0.1349324738201893, 3.098749116151128, 149.9753859305033,
			230.0326782748359, 79.76939505329617, 4.196, FRgb(0.6f, 0.58f, 0.55f),
			TEXT("Metal-rich M-type asteroid, perhaps an exposed planetary core; NASA's Psyche probe arrives in 2029.")));
		Out.Add(Asteroid(TEXT("Lutetia"), 49, 0.1134, 2.434430742712361, 0.1647703512177245, 3.064452891064415, 80.83855721319993,
			249.8802780888828, 298.7726414605465, 8.1655, FRgb(0.52f, 0.5f, 0.48f),
			TEXT("Primitive 100 km asteroid, photographed by Rosetta as it flew past in July 2010.")));
		Out.Add(Asteroid(TEXT("Ida"), 16, 0.00275, 2.863348031699649, 0.04610962795708528, 1.130363094271507, 323.5366609419851,
			113.2571826848795, 49.64769088009876, 4.634, FRgb(0.62f, 0.56f, 0.48f),
			TEXT("Koronis-family asteroid; Galileo found the first asteroid moon, Dactyl, orbiting it in 1993.")));

		// Three real Jupiter Trojans: two of the Greek camp at L4, one of the Trojan camp at L5.
		Out.Add(Trojan(Asteroid(TEXT("Hektor"), 112.5, 0.527, 5.276986092673823, 0.02434489234275647, 18.14715224028094, 342.8018513679027,
			181.288859678094, 24.91385327109803, 6.924, FRgb(0.47f, 0.4f, 0.35f),
			TEXT("Largest Jupiter Trojan, a 225 km contact binary leading Jupiter at L4, with a small moon, Skamandrios.")), ELagrangePoint::L4));
		Out.Add(Trojan(Asteroid(TEXT("Patroclus"), 70.2, 0.091, 5.205975173988769, 0.1391467916238868, 22.06359071054631, 44.34968792774898,
			308.8377281363527, 58.67543160284279, 102.8, FRgb(0.44f, 0.39f, 0.35f),
			TEXT("Twin Trojan at L5: Patroclus and Menoetius, two 110 km bodies circling each other every 4.3 days; Lucy flies by in 2033.")), ELagrangePoint::L5));
		Out.Add(Trojan(Asteroid(TEXT("Eurybates"), 31.9, 0.0091, 5.217371617810976, 0.09059867172297777, 8.05147293527498, 43.5587275998936,
			28.69968222483612, 125.7480299363769, 8.711, FRgb(0.42f, 0.4f, 0.38f),
			TEXT("Grey Trojan at L4, the largest of a collisional family, with a 1 km moon, Queta; Lucy flies past in August 2027.")), ELagrangePoint::L4));

		return Out;
	}
}

TSharedRef<FBodyCatalog> FBodyCatalog::CreateSolSystem()
{
	return MakeShared<FBodyCatalog>(SolDefinitions());
}
