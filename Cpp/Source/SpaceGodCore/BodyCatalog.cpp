// Port of Unity.SpaceGod Core/BodyCatalog.cs (see docs/MIGRATION.md).

#include "BodyCatalog.h"
#include "Lagrange.h"
#include "OrbitalElements.h"
#include "Systems/AlphaCentauri.h"

namespace SpaceGod::Core
{
	BodyCatalog::BodyCatalog(const cs::List<cs::Ref<BodyDefinition>>& definitions)
	{
		bodies = cs::List<cs::Ref<BodyDefinition>>(definitions.begin(), definitions.end());
		for (int i = 0; i < bodies.Count(); i++)
		{
			bodies[i]->Index = i;
			byName[bodies[i]->Name] = bodies[i];
		}

		children = cs::NewArray<cs::List<int>>(bodies.Count());
		for (int i = 0; i < bodies.Count(); i++) children[i] = cs::List<int>();

		for (const auto& b : bodies)
		{
			if (!b->ParentName.empty()) // C#: b.ParentName != null
			{
				cs::Ref<BodyDefinition> parent;
				if (!byName.TryGetValue(b->ParentName, parent))
					throw cs::InvalidOperationException(cs::Cat(b->Name, ": unknown parent ", b->ParentName));
				if (parent->Index > b->Index)
					throw cs::InvalidOperationException(cs::Cat(b->Name, ": parent ", parent->Name, " must be listed first"));
				b->ParentIndex = parent->Index;
				children[parent->Index].Add(b->Index);
			}

			if (!cs::Double::IsNaN(b->PoleRaDeg))
				b->Pole = Astro::PoleFromRaDec(b->PoleRaDeg, b->PoleDecDeg);
			else if (b->Plane == OrbitPlane::ParentEquator && b->ParentIndex >= 0)
				b->Pole = bodies[b->ParentIndex]->SpinAxis(); // synchronous moon: spins about its orbit normal
			else
				b->Pole = DVec3::UnitZ;
		}

		for (const auto& b : bodies)
		{
			if (b->ParentIndex < 0) continue;
			auto parent = bodies[b->ParentIndex];
			double apoapsis = b->Elements->AKm * cs::Math::Abs(b->OrbitScale) * (1 + b->Elements->E);
			double periapsis = b->Elements->AKm * cs::Math::Abs(b->OrbitScale) * (1 - b->Elements->E);
			double soi = b->Elements->AKm * cs::Math::Pow(cs::Math::Max(b->Gm, 0.0) / parent->Gm, 0.4);
			b->SoiKm = cs::Math::Max(soi, b->RadiusKm * 3.0);
			if (!parent->UsesStellarMapping())
				parent->MoonReachKm = cs::Math::Max(parent->MoonReachKm, apoapsis);
			else if (parent->ParentIndex >= 0)
				parent->SystemReachKm = cs::Math::Max(parent->SystemReachKm, apoapsis);
			parent->InnermostChildKm = parent->InnermostChildKm > 0 ? cs::Math::Min(parent->InnermostChildKm, periapsis) : periapsis;
		}

		// A star in a multiple system rules the space out past what orbits it; Laplace's formula
		// against the pair's barycentre would put that boundary absurdly far out.
		for (const auto& b : bodies)
			if (b->IsStar() && b->ParentIndex >= 0)
				b->SoiKm = cs::Math::Max(b->SystemReachKm * 1.5, b->RadiusKm * 20);
	}

	double BodyCatalog::OuterEdgeKm() const
	{
		if (!cs::Double::IsNaN(outerEdgeKm)) return outerEdgeKm;
		double reach = bodies[0]->RadiusKm;
		for (const auto& b : bodies)
		{
			if (b->ParentIndex < 0 || b->Kind == BodyKind::Moon || b->Kind == BodyKind::DwarfPlanet) continue;
			double far = 0;
			bool wide = false;
			for (auto c = b; c->ParentIndex >= 0; c = bodies[c->ParentIndex])
			{
				double apo = c->Elements->AKm * cs::Math::Abs(c->OrbitScale) * (1 + c->Elements->E);
				wide |= apo > WideCompanionKm;
				far += apo;
			}
			if (!wide) reach = cs::Math::Max(reach, far * 1.1);
		}
		return outerEdgeKm = reach;
	}

	cs::Ref<BodyCatalog> BodyCatalog::CreateSolSystem() { return cs::New<BodyCatalog>(SolDefinitions()); }

	cs::Ref<BodyCatalog> BodyCatalog::CreateAlphaCentauri() { return cs::New<BodyCatalog>(AlphaCentauri::Definitions()); }

	cs::List<cs::Ref<BodyDefinition>> BodyCatalog::SolDefinitions()
	{
		// C# yields lazily; nothing here depends on that, so the list is built eagerly.
		cs::List<cs::Ref<BodyDefinition>> result;
		constexpr double sbdbEpoch = 2459000.5;

		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Sun"; b->Kind = BodyKind::Star; b->RadiusKm = 695700; b->Gm = 1.32712440018e11;
			b->PoleRaDeg = 286.13; b->PoleDecDeg = 63.87; b->RotationHours = 609.12;
			b->PrimeMeridianDeg = 84.176; b->PrimeMeridianRateDegPerDay = 14.1844;
			b->Color = Rgb(1.0f, 0.86f, 0.6f);
			b->Summary = "G2V main-sequence star. 99.86% of the system's mass.";
			result.Add(b);
		}
		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Mercury"; b->Kind = BodyKind::Planet; b->ParentName = "Sun"; b->RadiusKm = 2439.7; b->Gm = 22031.8;
			b->Elements = OrbitalElements::FromJpl(0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593,
				0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081);
			b->PoleRaDeg = 281.01; b->PoleDecDeg = 61.42; b->RotationHours = 1407.6;
			b->PrimeMeridianDeg = 329.5988; b->PrimeMeridianRateDegPerDay = 6.1385108;
			b->Color = Rgb(0.6f, 0.58f, 0.55f); b->Texture = "mercury_color"; b->TexturePoleMargin = 0.035f; b->NormalMap = "mercury_normal";
			b->Summary = "Smallest planet; a 3:2 spin-orbit resonance gives it a 176-day solar day.";
			result.Add(b);
		}
		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Venus"; b->Kind = BodyKind::Planet; b->ParentName = "Sun"; b->RadiusKm = 6051.8; b->Gm = 324858.6;
			b->Elements = OrbitalElements::FromJpl(0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255,
				0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418);
			b->PoleRaDeg = 272.76; b->PoleDecDeg = 67.16; b->RotationHours = -5832.6;
			b->PrimeMeridianDeg = 160.20; b->PrimeMeridianRateDegPerDay = -1.4813688;
			// Visible light never reaches the surface: what an eye sees is the nearly featureless
			// cloud deck, drawn procedurally (see Planet.shader).
			b->Color = Rgb(0.93f, 0.86f, 0.68f); b->SurfaceHaze = 1.0f;
			b->AtmosphereColor = Rgb(1.0f, 0.88f, 0.6f); b->AtmosphereStrength = 0.7f;
			b->Summary = "Spins backwards under a 92-bar CO2 atmosphere. Its clouds hide the surface from every visible-light camera.";
			result.Add(b);
		}
		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Earth"; b->Kind = BodyKind::Planet; b->ParentName = "Sun"; b->RadiusKm = 6371.0; b->Gm = 398600.435;
			b->Elements = OrbitalElements::FromJpl(1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0,
				0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0);
			b->PoleRaDeg = 0; b->PoleDecDeg = 90; b->RotationHours = 23.9344696;
			b->PrimeMeridianDeg = 190.147; b->PrimeMeridianRateDegPerDay = 360.9856235;
			b->Color = Rgb(0.3f, 0.45f, 0.7f); b->Texture = "earth_color";
			b->AtmosphereColor = Rgb(0.4f, 0.65f, 1.0f); b->AtmosphereStrength = 0.65f;
			b->Summary = "Home. Elements are for the Earth-Moon barycentre; Earth is offset from it by the Moon.";
			result.Add(b);
		}
		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Moon"; b->Kind = BodyKind::Moon; b->ParentName = "Earth"; b->RadiusKm = 1737.4; b->Gm = 4902.8;
			// Mean elements at J2000: L = 218.3165, perigee longitude 83.3532, node 125.0445.
			b->Elements = OrbitalElements::FromClassical(Astro::J2000, 384400, 0.0549, 5.145, 125.0445, 83.3532 - 125.0445,
				218.3165 - 83.3532, 27.321661, -360.0 / 6798.38, 360.0 / 3232.6);
			b->PoleRaDeg = 269.99; b->PoleDecDeg = 66.54; b->RotationHours = 655.72; b->FacesParent = true;
			b->Color = Rgb(0.62f, 0.61f, 0.6f); b->Texture = "moon_color"; b->NormalMap = "moon_normal";
			b->Summary = "Tidally locked. Slowly receding from Earth at 3.8 cm a year.";
			result.Add(b);
		}
		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Mars"; b->Kind = BodyKind::Planet; b->ParentName = "Sun"; b->RadiusKm = 3389.5; b->Gm = 42828.37;
			b->Elements = OrbitalElements::FromJpl(1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891,
				0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343);
			b->PoleRaDeg = 317.68; b->PoleDecDeg = 52.89; b->RotationHours = 24.6229;
			b->PrimeMeridianDeg = 176.630; b->PrimeMeridianRateDegPerDay = 350.89198226;
			b->Color = Rgb(0.75f, 0.45f, 0.3f); b->Texture = "mars_color"; b->NormalMap = "mars_normal";
			b->AtmosphereColor = Rgb(0.95f, 0.6f, 0.4f); b->AtmosphereStrength = 0.2f;
			b->Summary = "Home of Olympus Mons and Valles Marineris.";
			result.Add(b);
		}
		// Phobos spirals in under Mars's tides: its mean longitude gains 1.26e-3 deg/yr² (fitted, matching
		// the published secular acceleration). The node regresses and periapsis advances with Mars's J2.
		auto phobos = Moon("Phobos", "Mars", 11.27, 7.087e-4, 9374.92, 0.015125, 1.07438, 128.2222, 216.4756, 189.5918, 0.3189101053, -0.4357808, 0.4351860, 317.7, 52.9, Rgb(0.42f, 0.38f, 0.35f));
		phobos->Elements = OrbitalElements::FromClassical(Astro::J2000, 9374.92, 0.015125, 1.07438, 128.2222, 216.4756, 189.5918, 0.3189101053,
			-0.4357808, 0.4351860, 1.2614e-3);
		result.Add(WithMap(phobos, "phobos_color", true));
		result.Add(WithMap(Moon("Deimos", "Mars", 6.2, 9.6e-5, 23457, 0.0, 1.7878, 13.753, 352.1363, 212.2813, 1.26244093, -0.018163, -0.036847, 316.6, 53.5, Rgb(0.55f, 0.5f, 0.45f)), "deimos_color", true));

		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Ceres"; b->Kind = BodyKind::DwarfPlanet; b->ParentName = "Sun"; b->RadiusKm = 469.7; b->Gm = 62.6284;
			// Mean elements fitted to JPL Horizons over 2000-2050 (within 0.5° along its orbit; Jupiter
			// perturbs it). The SBDB osculating set once used here had the wrong mean anomaly for its epoch.
			b->Elements = OrbitalElements::FromClassical(Astro::J2000, 413958905.2, 0.0777927, 10.589118, 80.537317, 72.967047,
				7.312862, 1681.58004, -3.7765e-5, 1.0614e-5);
			b->PoleRaDeg = 291.42; b->PoleDecDeg = 66.76; b->RotationHours = 9.074;
			b->PrimeMeridianDeg = 170.650; b->PrimeMeridianRateDegPerDay = 952.1532;
			b->Color = Rgb(0.5f, 0.49f, 0.47f); b->Texture = "ceres_color"; b->TintTexture = true;
			b->Summary = "Largest object in the asteroid belt. Bright salt deposits in Occator crater.";
			result.Add(b);
		}

		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Jupiter"; b->Kind = BodyKind::Planet; b->ParentName = "Sun"; b->RadiusKm = 69911; b->Gm = 1.26686534e8;
			b->Elements = OrbitalElements::FromJpl(5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909,
				-0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106);
			b->PoleRaDeg = 268.06; b->PoleDecDeg = 64.50; b->RotationHours = 9.925;
			b->PrimeMeridianDeg = 284.95; b->PrimeMeridianRateDegPerDay = 870.536; // System III
			b->Color = Rgb(0.8f, 0.7f, 0.58f); b->Texture = "jupiter_color"; b->TexturePoleMargin = 0.06f; // Hubble map ends near ±80°
			b->GasGiant = true;
			b->AtmosphereColor = Rgb(0.9f, 0.8f, 0.65f); b->AtmosphereStrength = 0.3f;
			b->Summary = "Two and a half times the mass of every other planet combined.";
			result.Add(b);
		}
		result.Add(WithMap(Moon("Io", "Jupiter", 1821.6, 5959.9, 421800, 0.00413, 0.0, 20.0402, 49.0252, 330.8831, 1.76913776, 0, -0.73951, 268.1, 64.5, Rgb(0.93f, 0.83f, 0.45f)), "io_color", false));
		result.Add(WithMap(Moon("Europa", "Jupiter", 1560.8, 3202.7, 671100, 0.00928, 0.4643, 204.4438, 44.6561, 345.4036, 3.55118104, -0.032732, -0.739518, 268.1, 64.5, Rgb(0.86f, 0.8f, 0.72f)), "europa_color", true));
		result.Add(WithMap(Moon("Ganymede", "Jupiter", 2634.1, 9887.8, 1070400, 0.00194, 0.2299, 59.8522, 246.3051, 295.4604, 7.15455318, -0.005846, 0.005568, 268.2, 64.6, Rgb(0.6f, 0.57f, 0.52f)), "ganymede_color", false));
		result.Add(WithMap(Moon("Callisto", "Jupiter", 2410.3, 7179.3, 1882700, 0.00727, 0.2197, 314.8165, 55.8575, 86.0337, 16.68901692, -0.001697, 0.001773, 268.7, 64.8, Rgb(0.42f, 0.39f, 0.35f)), "callisto_color", true));

		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Saturn"; b->Kind = BodyKind::Planet; b->ParentName = "Sun"; b->RadiusKm = 58232; b->Gm = 3.7931187e7;
			b->Elements = OrbitalElements::FromJpl(9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448,
				-0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794);
			b->PoleRaDeg = 40.59; b->PoleDecDeg = 83.54; b->RotationHours = 10.656;
			b->PrimeMeridianDeg = 38.90; b->PrimeMeridianRateDegPerDay = 810.7939024;
			b->Color = Rgb(0.85f, 0.77f, 0.6f); b->Texture = "saturn_color"; b->GasGiant = true;
			b->AtmosphereColor = Rgb(0.95f, 0.85f, 0.6f); b->AtmosphereStrength = 0.3f;
			// C ring inner edge to A ring outer edge.
			b->RingInnerKm = 74658; b->RingOuterKm = 136775;
			b->Summary = "Less dense than water. Its main rings are ~10 m thick and 270,000 km across.";
			result.Add(b);
		}
		result.Add(WithMap(Moon("Enceladus", "Saturn", 252.1, 7.21, 238400, 0.00476, 0.0, 320.0881, 171.4837, 10.7909, 1.3702181, 0, 0.337987, 40.6, 83.5, Rgb(0.95f, 0.95f, 0.97f)), "enceladus_color", true));
		auto titan = Moon("Titan", "Saturn", 2574.7, 8978.1, 1221900, 0.02869, 0.3472, 339.0229, 185.0478, 163.5777, 15.9454481, -0.001294, 0.001419, 36.4, 84.0, Rgb(0.85f, 0.62f, 0.32f));
		titan->AtmosphereColor = Rgb(0.9f, 0.65f, 0.3f);
		titan->AtmosphereStrength = 0.9f;
		// Cassini ISS 938 nm surface map, under the haze that hides it in visible light.
		WithMap(titan, "titan_surface", false);
		titan->SurfaceHaze = 0.82f;
		titan->Summary = "Only moon with a thick atmosphere; methane lakes at its poles.";
		result.Add(titan);

		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Uranus"; b->Kind = BodyKind::Planet; b->ParentName = "Sun"; b->RadiusKm = 25362; b->Gm = 5.793939e6;
			b->Elements = OrbitalElements::FromJpl(19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503,
				-0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589);
			b->PoleRaDeg = 257.31; b->PoleDecDeg = -15.18; b->RotationHours = -17.24;
			b->PrimeMeridianDeg = 203.81; b->PrimeMeridianRateDegPerDay = -501.1600928;
			b->Color = Rgb(0.65f, 0.85f, 0.9f); b->Texture = "uranus_color"; b->GasGiant = true;
			b->AtmosphereColor = Rgb(0.6f, 0.9f, 0.95f); b->AtmosphereStrength = 0.4f;
			b->Summary = "Rolls around the Sun on its side, axial tilt 98 degrees.";
			result.Add(b);
		}
		result.Add(WithMap(Moon("Miranda", "Uranus", 235.8, 4.4, 129846, 0.00129, 4.427, 95.7866, 151.4373, 76.6609, 1.41347955, -0.055411, 0.055218, Rgb(0.7f, 0.7f, 0.72f)), "miranda_color", true, /* mapNorthFlipped: */ true));
		result.Add(WithMap(Moon("Titania", "Uranus", 788.4, 228.2, 436298, 0.00158, 0.0645, 331.0516, 249.1247, 56.3287, 8.70586893, 0.00373, 0.003818, Rgb(0.68f, 0.63f, 0.6f)), "titania_color", true, /* mapNorthFlipped: */ true));
		result.Add(WithMap(Moon("Oberon", "Uranus", 761.4, 205.3, 583511, 0.00153, 0.2035, 33.7756, 134.2823, 179.436, 13.46323712, -0.001675, 0.002236, Rgb(0.6f, 0.55f, 0.53f)), "oberon_color", true, /* mapNorthFlipped: */ true));

		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Neptune"; b->Kind = BodyKind::Planet; b->ParentName = "Sun"; b->RadiusKm = 24622; b->Gm = 6.836529e6;
			b->Elements = OrbitalElements::FromJpl(30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574,
				0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664);
			b->PoleRaDeg = 299.36; b->PoleDecDeg = 43.46; b->RotationHours = 16.11;
			b->PrimeMeridianDeg = 249.978; b->PrimeMeridianRateDegPerDay = 541.1397757;
			b->Color = Rgb(0.35f, 0.5f, 0.9f); b->Texture = "neptune_color"; b->GasGiant = true;
			b->AtmosphereColor = Rgb(0.4f, 0.6f, 1.0f); b->AtmosphereStrength = 0.4f;
			b->Summary = "Fastest winds in the system, over 2,000 km/h.";
			result.Add(b);
		}
		auto triton = Moon("Triton", "Neptune", 1353.4, 1427.6, 354800, 0.00013, 157.2335, 152.2806, 76.2454, 341.4831, 5.87657447, 0.001458, 0.004035, 299.8, 43.1, Rgb(0.85f, 0.8f, 0.78f));
		triton->RotationHours = -triton->RotationHours;
		triton->Summary = "Orbits backwards: a captured Kuiper belt object.";
		WithMap(triton, "triton_color", true);
		result.Add(triton);

		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Pluto"; b->Kind = BodyKind::DwarfPlanet; b->ParentName = "Sun"; b->RadiusKm = 1188.3; b->Gm = 869.6;
			b->Elements = OrbitalElements::FromJpl(39.48211675, 0.24882730, 17.14001206, 238.92903833, 224.06891629, 110.30393684,
				-0.00031596, 0.00005170, 0.00004818, 145.20780515, -0.04062942, -0.01183482);
			// IAU gives Pluto's positive (right-hand) pole, RA 132.99 Dec -6.16. Its antipode is the
			// north pole in the planets' convention, about which Pluto, like Uranus, spins backwards.
			b->PoleRaDeg = 312.99; b->PoleDecDeg = 6.16; b->RotationHours = -153.2935;
			// New Horizons maps use the positive pole as north, with W = 302.695 + 56.3625225 d about it.
			b->MapNorthFlipped = true; b->PrimeMeridianDeg = 302.695; b->PrimeMeridianRateDegPerDay = 56.3625225;
			b->Color = Rgb(0.8f, 0.68f, 0.55f); b->Texture = "pluto_color"; b->TintTexture = true;
			b->Summary = "Its eccentric orbit dips inside Neptune's for 20 years of every 248.";
			result.Add(b);
		}
		result.Add(WithMap(Moon("Charon", "Pluto", 606, 105.88, 19595.8, 0.00016, 0.0861, 28.2176, 144.2491, 148.7655, 6.387222074, 0.000156, 0.000002, Rgb(0.6f, 0.58f, 0.56f)), "charon_color", true));

		{
			auto b = cs::New<BodyDefinition>();
			b->Name = "Eris"; b->Kind = BodyKind::DwarfPlanet; b->ParentName = "Sun"; b->RadiusKm = 1163; b->Gm = 1108;
			b->Elements = OrbitalElements::FromClassical(sbdbEpoch, 67.864 * Astro::AuKm, 0.43607, 44.040, 35.951, 151.639,
				205.989, OrbitalElements::PeriodFromGm(67.864 * Astro::AuKm, 1.32712440018e11));
			b->RotationHours = 378.9;
			b->Color = Rgb(0.92f, 0.92f, 0.9f);
			b->Summary = "Most massive known dwarf planet; its discovery got Pluto reclassified.";
			result.Add(b);
		}

		// Three real main-belt asteroids, appended after everything else so no other body's index moves.
		// Each hides a pirate haven in the game layer (World.BuildSettlements).
		result.Add(Asteroid("Psyche", 111, 1.601, 2.925720466462538, 0.1349324738201893, 3.098749116151128, 149.9753859305033,
			230.0326782748359, 79.76939505329617, 4.196, Rgb(0.6f, 0.58f, 0.55f),
			"Metal-rich M-type asteroid, perhaps an exposed planetary core; NASA's Psyche probe arrives in 2029."));
		result.Add(Asteroid("Lutetia", 49, 0.1134, 2.434430742712361, 0.1647703512177245, 3.064452891064415, 80.83855721319993,
			249.8802780888828, 298.7726414605465, 8.1655, Rgb(0.52f, 0.5f, 0.48f),
			"Primitive 100 km asteroid, photographed by Rosetta as it flew past in July 2010."));
		result.Add(Asteroid("Ida", 16, 0.00275, 2.863348031699649, 0.04610962795708528, 1.130363094271507, 323.5366609419851,
			113.2571826848795, 49.64769088009876, 4.634, Rgb(0.62f, 0.56f, 0.48f),
			"Koronis-family asteroid; Galileo found the first asteroid moon, Dactyl, orbiting it in 1993."));

		// Three real Jupiter Trojans, appended after the belt asteroids so no other index moves: two of
		// the Greek camp 60° ahead of Jupiter (L4) and one of the Trojan camp 60° behind (L5). Mining
		// grounds and a hidden haven in the game layer (World.BuildSettlements).
		result.Add(Trojan(Asteroid("Hektor", 112.5, 0.527, 5.276986092673823, 0.02434489234275647, 18.14715224028094, 342.8018513679027,
			181.288859678094, 24.91385327109803, 6.924, Rgb(0.47f, 0.4f, 0.35f),
			"Largest Jupiter Trojan, a 225 km contact binary leading Jupiter at L4, with a small moon, Skamandrios."), LagrangePoint::L4));
		result.Add(Trojan(Asteroid("Patroclus", 70.2, 0.091, 5.205975173988769, 0.1391467916238868, 22.06359071054631, 44.34968792774898,
			308.8377281363527, 58.67543160284279, 102.8, Rgb(0.44f, 0.39f, 0.35f),
			"Twin Trojan at L5: Patroclus and Menoetius, two 110 km bodies circling each other every 4.3 days; Lucy flies by in 2033."), LagrangePoint::L5));
		result.Add(Trojan(Asteroid("Eurybates", 31.9, 0.0091, 5.217371617810976, 0.09059867172297777, 8.05147293527498, 43.5587275998936,
			28.69968222483612, 125.7480299363769, 8.711, Rgb(0.42f, 0.4f, 0.38f),
			"Grey Trojan at L4, the largest of a collisional family, with a 1 km moon, Queta; Lucy flies past in August 2027."), LagrangePoint::L4));
		return result;
	}

	cs::Ref<BodyDefinition> BodyCatalog::Trojan(cs::Ref<BodyDefinition> body, LagrangePoint point)
	{
		body->TrojanPoint = point;
		body->CoOrbitalWith = "Jupiter";
		return body;
	}

	cs::Ref<BodyDefinition> BodyCatalog::Asteroid(const cs::String& name, double radiusKm, double gm, double aAu, double e, double iDeg, double nodeDeg,
		double argPeriDeg, double meanAnomalyDeg, double rotationHours, Rgb color, const cs::String& summary)
	{
		constexpr double epoch = 2461200.5;
		auto b = cs::New<BodyDefinition>();
		b->Name = name; b->Kind = BodyKind::Asteroid; b->ParentName = "Sun"; b->RadiusKm = radiusKm; b->Gm = gm;
		b->Elements = OrbitalElements::FromClassical(epoch, aAu * Astro::AuKm, e, iDeg, nodeDeg, argPeriDeg, meanAnomalyDeg,
			OrbitalElements::PeriodFromGm(aAu * Astro::AuKm, 1.32712440018e11));
		b->RotationHours = rotationHours;
		b->Color = color;
		b->Summary = summary;
		return b;
	}

	cs::Ref<BodyDefinition> BodyCatalog::Moon(const cs::String& name, const cs::String& parent, double radiusKm, double gm, double aKm, double e, double iDeg,
		double nodeDeg, double argPeriDeg, double meanAnomalyDeg, double periodDays, double nodeRate, double varpiRate,
		double laplaceRaDeg, double laplaceDecDeg, Rgb color)
	{
		auto b = cs::New<BodyDefinition>();
		b->Name = name; b->Kind = BodyKind::Moon; b->ParentName = parent; b->RadiusKm = radiusKm; b->Gm = gm;
		b->Elements = OrbitalElements::FromClassical(Astro::J2000, aKm, e, iDeg, nodeDeg, argPeriDeg, meanAnomalyDeg, periodDays,
			nodeRate, varpiRate);
		b->Plane = OrbitPlane::ParentEquator;
		b->LaplacePoleRaDeg = laplaceRaDeg;
		b->LaplacePoleDecDeg = laplaceDecDeg;
		b->RotationHours = periodDays * 24.0;
		b->FacesParent = true;
		b->Color = color;
		return b;
	}

	cs::Ref<BodyDefinition> BodyCatalog::WithMap(cs::Ref<BodyDefinition> body, const cs::String& texture, bool tint, bool mapNorthFlipped)
	{
		body->Texture = texture;
		body->TintTexture = tint;
		body->MapNorthFlipped = mapNorthFlipped;
		return body;
	}

	cs::Ref<BodyDefinition> BodyCatalog::Moon(const cs::String& name, const cs::String& parent, double radiusKm, double gm, double aKm, double e, double iDeg,
		double nodeDeg, double argPeriDeg, double meanAnomalyDeg, double periodDays, double nodeRate, double varpiRate, Rgb color)
	{
		return Moon(name, parent, radiusKm, gm, aKm, e, iDeg, nodeDeg, argPeriDeg, meanAnomalyDeg, periodDays, nodeRate, varpiRate,
			cs::Double::NaN, cs::Double::NaN, color);
	}
}
