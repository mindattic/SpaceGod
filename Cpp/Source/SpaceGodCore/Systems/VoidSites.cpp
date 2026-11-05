// US-T123: what a void station's stretch of open space holds besides the relay (see VoidSites.h).

#include "Systems/VoidSites.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "OrbitalElements.h"
#include "ScaleModel.h"
#include "World.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Combat/War.h"
#include "Fleet.h"
#include "Systems/StarSystemBuilder.h"
#include "Systems/VoidBaseBuilder.h"
#include "Systems/VoidStations.h"

#include <map>
#include <mutex>

namespace SpaceGod::Core
{
	namespace
	{
		/// <summary>The station's own random stream: SplitMix64 seeded from the FNV-1a of its name, the same on every platform.</summary>
		class Stream
		{
			uint64_t state;

		public:
			explicit Stream(const cs::String& name)
			{
				uint64_t h = 0xCBF29CE484222325ull;
				for (unsigned char c : name) { h ^= c; h *= 0x100000001B3ull; }
				state = h ^ 0x9E3779B97F4A7C15ull;
			}

			uint64_t Next()
			{
				uint64_t z = (state += 0x9E3779B97F4A7C15ull);
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				return z ^ (z >> 31);
			}

			double U() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
			double Range(double a, double b) { return a + (b - a) * U(); }
			int Int(int n) { return static_cast<int>(Next() % static_cast<uint64_t>(n)); }
			template <class T> const T& Pick(const cs::List<T>& list) { return list[Int(list.Count())]; }
		};

		const cs::List<cs::String>& DriftWords()
		{
			static const cs::List<cs::String> w = { "Cinder", "Tallow", "Gravel", "Magpie", "Rook", "Slag", "Hobnail", "Ironbark", "Pumice",
				"Flint", "Tinder", "Clinker", "Bramble", "Cobble", "Grist", "Shale", "Marl", "Scree", "Talus", "Burr", "Ember", "Soot",
				"Rust", "Ochre", "Jackdaw", "Tumbler", "Knuckle", "Hailstone", "Cairn", "Dross" };
			return w;
		}

		const cs::List<cs::String>& IceWords()
		{
			static const cs::List<cs::String> w = { "Rime", "Hoarfrost", "Glimmer", "Sleet", "Frostglass", "Glint", "Shiver", "Diamond",
				"Halo", "Snowblind", "Icicle", "Frazil", "Verglas", "Spindrift" };
			return w;
		}

		const cs::List<cs::String>& DriftKinds()
		{
			static const cs::List<cs::String> w = { "Drift", "Shoal", "Reef", "Scatter", "Rubble", "Swarm" };
			return w;
		}

		const cs::List<cs::String>& PlanetoidNames()
		{
			static const cs::List<cs::String> w = { "Hesper", "Morrow", "Calder", "Tamsin", "Orrin", "Ysolde", "Varga", "Ketter", "Brannoch",
				"Sable", "Corwen", "Idris", "Mabon", "Talwyn", "Rhosyn", "Anwen", "Gethin", "Elowen", "Peran", "Ilar", "Morwenna", "Cadoc" };
			return w;
		}

		cs::List<cs::String> SightNames(VoidSight s)
		{
			switch (s)
			{
				case VoidSight::GlassMoonlet: return { "The Glass Moonlet", "The Chandelier", "The Prism", "Crystal Palace" };
				case VoidSight::FrozenFountain: return { "The Frozen Fountain", "The Icicle", "The Standing Plume", "The Spire of Ice" };
				case VoidSight::DerelictArk: return { "The Long Sleep", "The Patient Ark", "The Wanderer Hulk", "The Lamplit Wreck" };
				case VoidSight::StoneArch: return { "The Keyhole", "The Wedding Ring", "The Gate That Isn't", "The Halo Stone" };
				case VoidSight::Lantern: return { "The Lantern", "Will-o'-the-Void", "The Glowworm", "Saint Elmo's Rock" };
				default: return { "The Oddity" };
			}
		}

		cs::String SightSummary(VoidSight s, const cs::String& station)
		{
			switch (s)
			{
				case VoidSight::GlassMoonlet:
					return cs::Cat("A moonlet of ", station, " grown over with metre-long crystals that throw the tiny suns' light back in "
						"slow rainbows. Every crew stopped here to let the drive cool flies out to look; the assay camp rents suits and "
						"sells 'genuine' shards, most of them glass from the galley. A tourist trap, and worth it. Fiction.");
				case VoidSight::FrozenFountain:
					return cs::Cat("A comet nucleus that wandered into ", station, "'s field and froze mid-spurt: its last jet stands off "
						"it as a spire of clear ice, kilometres high and thinner than it has any right to be. Pilots dare each other to "
						"fly the gap between spire and rock. A tourist trap. Fiction.");
				case VoidSight::DerelictArk:
					return "A generation ship nobody has ever claimed, its ring habitats dark, drifting here with its running lights still "
						"on after who knows how long. Tours go aboard as far as the sealed bulkheads; nobody has opened those, and the "
						"camp's guides make a point of saying so. A tourist trap. Fiction.";
				case VoidSight::StoneArch:
					return "A perfect hoop of rock with nothing in the middle. It is not hollowed, cut or cast that anyone can find, and "
						"it is far too round to be an accident. Flying through it is said to bring a safe jump; nearly everyone does. "
						"A tourist trap. Fiction.";
				case VoidSight::Lantern:
					return "A dark rock veined with something that glows a steady green: a mineral, a lichen or a joke, depending on who "
						"is selling the postcards. It is the brightest thing for light-years after the tiny suns, and a landmark for "
						"every pilot dropping out here. A tourist trap. Fiction.";
				default: return "An oddity. Fiction.";
			}
		}

		/// <summary>A circular orbit's position: radius <paramref name="a"/>, argument of latitude <paramref name="uDeg"/>.</summary>
		DVec3 OrbitAt(double a, double iDeg, double nodeDeg, double uDeg)
		{
			double i = iDeg * Astro::Deg2Rad, o = nodeDeg * Astro::Deg2Rad, u = uDeg * Astro::Deg2Rad;
			double cu = cs::Math::Cos(u), su = cs::Math::Sin(u), co = cs::Math::Cos(o), so = cs::Math::Sin(o), ci = cs::Math::Cos(i), si = cs::Math::Sin(i);
			return DVec3(a * (co * cu - so * su * ci), a * (so * cu + co * su * ci), a * su * si);
		}

		/// <summary>The period a body at <paramref name="aKm"/> would have about the generator, scaled from its tiny suns' (Kepler's third law).</summary>
		double PeriodAt(double aKm)
		{
			return VoidBaseBuilder::TinySunPeriodDays * cs::Math::Pow(aKm / VoidBaseBuilder::TinySunOrbitKm, 1.5);
		}

		cs::Ref<BodyDefinition> Probe(BodyKind kind, double radiusKm)
		{
			auto b = cs::New<BodyDefinition>();
			b->Kind = kind;
			b->RadiusKm = radiusKm;
			return b;
		}

		/// <summary>The scene distance of a point <paramref name="x"/> body radii out, in that body's radii, at a blend (see ScaleModel's local mapping).</summary>
		double LocalRadii(double x, double blend)
		{
			double game = 1.0 + ScaleModel::GameLocalC * cs::Math::Pow(cs::Math::Max(0.0, x - 1.0), ScaleModel::GameLocalP);
			return ScaleModel::LogLerp(x, game, blend);
		}

		/// <summary>The two scales every placement is checked at: realistic (0) and game (1).</summary>
		struct Scales
		{
			ScaleModel s[2];
			Scales()
			{
				s[0].SetBlend(0);
				s[1].SetBlend(1);
			}
		};

		const Scales& Both()
		{
			static const Scales scales;
			return scales;
		}

		/// <summary>An annulus about the generator (scene units) at each scale, which another group's must stay clear of.</summary>
		struct Band
		{
			double In[2], Out[2];
		};

		bool Clear(const cs::List<Band>& taken, const Band& b)
		{
			for (const auto& t : taken)
				for (int k = 0; k < 2; k++)
				{
					double margin = 0.04 * cs::Math::Max(t.Out[k], b.Out[k]);
					if (b.In[k] < t.Out[k] + margin && b.Out[k] + margin > t.In[k]) return false;
				}
			return true;
		}

		std::mutex& Gate() { static std::mutex m; return m; }
		// Derived, never saved: a plan is rebuilt from the station's name whenever it is needed.
		std::map<cs::String, VoidSitePlan>& Plans() { static std::map<cs::String, VoidSitePlan> m; return m; }
		std::map<cs::String, cs::String>& PlanetoidStation() { static std::map<cs::String, cs::String> m; return m; }

		const VoidSitePlan& Cached(const cs::String& station)
		{
			auto& plans = Plans();
			auto it = plans.find(station);
			if (it != plans.end()) return it->second;
			auto& plan = plans[station] = VoidSites::Plan(station);
			PlanetoidStation()[plan.PlanetoidName] = station;
			return plan;
		}
	}

	const VoidSite* VoidSitePlan::Find(const cs::String& name) const
	{
		for (const auto& s : Sites)
			if (s.Name == name) return &s;
		return nullptr;
	}

	VoidMix VoidSites::Mix(const cs::String& station)
	{
		// Its own stream (the name and a tag), so the mix never shifts what the plan draws for anything else.
		Stream rng(cs::Cat(station, "/mix"));
		VoidMix mix;
		double f = rng.U();
		mix.Field = f < 0.3 ? VoidFieldStyle::Drift : f < 0.5 ? VoidFieldStyle::Rubble : f < 0.7 ? VoidFieldStyle::MetalSwarm : VoidFieldStyle::IceField;
		mix.Nebula = rng.U() < 0.4;
		mix.Megastructure = rng.U() < 0.3;
		return mix;
	}

	VoidSitePlan VoidSites::Plan(const cs::String& station)
	{
		Stream rng(station);
		const auto& scales = Both();
		auto generator = Probe(BodyKind::Star, VoidBaseBuilder::RadiusKm);
		auto Ld = [&](int k, double km) { return scales.s[k].LocalDistance(generator, km); };
		auto Disp = [&](int k, double km) { return scales.s[k].DisplayRadius(km); };

		VoidSitePlan plan;
		plan.Station = station;
		plan.Mix = Mix(station);
		const auto style = plan.Mix.Field;
		const bool iceField = style == VoidFieldStyle::IceField;

		// The plane the sites keep to, tilted a little from the ecliptic.
		double tilt = rng.Range(0, 20), azimuth = rng.Range(0, 360);
		double node = azimuth + 90;
		plan.FieldNormal = DVec3(cs::Math::Sin(tilt * Astro::Deg2Rad) * cs::Math::Cos(azimuth * Astro::Deg2Rad),
			cs::Math::Sin(tilt * Astro::Deg2Rad) * cs::Math::Sin(azimuth * Astro::Deg2Rad), cs::Math::Cos(tilt * Astro::Deg2Rad));

		// Nothing comes in over the generator's model or its tiny suns.
		cs::List<Band> taken;
		{
			Band keep;
			for (int k = 0; k < 2; k++)
			{
				keep.In[k] = 0;
				keep.Out[k] = Ld(k, VoidBaseBuilder::TinySunOrbitKm) + Disp(k, VoidBaseBuilder::TinySunRadiusKm) * 2;
			}
			taken.Add(keep);
		}

		// ---- the names
		cs::String driftWord = rng.Pick(iceField ? IceWords() : DriftWords());
		plan.FieldName = iceField ? cs::Cat(driftWord, " Ice Field") : cs::Cat(driftWord, " ", rng.Pick(DriftKinds()));
		cs::String planetoid = rng.Pick(PlanetoidNames());
		static const cs::List<Good> rareOres = { Good::Platinum, Good::RareEarths, Good::Titanium, Good::Copper, Good::Zinc };
		static const cs::List<Good> volatiles = { Good::HeliumThree, Good::Nitrogen, Good::Hydrocarbons };
		plan.FieldOre = iceField ? rng.Pick(volatiles) : rng.Pick(rareOres);
		cs::String oreName = cs::Strings::ToLowerInvariant(GoodInfo::Of(plan.FieldOre)->Name);
		int sightCount = rng.U() < 0.45 ? 2 : 1;
		VoidSight sights[2] = { static_cast<VoidSight>(1 + rng.Int(5)), VoidSight::None };
		if (sightCount == 2)
		{
			sights[1] = static_cast<VoidSight>(1 + rng.Int(4));
			if (sights[1] >= sights[0]) sights[1] = static_cast<VoidSight>(static_cast<int>(sights[1]) + 1);
		}

		// A sight: placed at the first radius from aKm outward whose band is clear at both scales.
		auto PlaceSight = [&](VoidSight kind, double aKm)
		{
			VoidSite s;
			s.Kind = VoidSiteKind::Sight;
			s.Sight = kind;
			s.Name = rng.Pick(SightNames(kind));
			s.ParentName = station;
			s.RadiusKm = kind == VoidSight::DerelictArk ? rng.Range(3, 5) : kind == VoidSight::StoneArch ? rng.Range(2, 4) : rng.Range(4, 9);
			s.InclinationDeg = tilt + rng.Range(-3, 3);
			s.NodeDeg = node;
			s.LongitudeDeg = rng.Range(0, 360);
			s.RotationHours = kind == VoidSight::DerelictArk ? rng.Range(30, 80) : rng.Range(6, 30);
			s.Icy = kind == VoidSight::FrozenFountain;
			s.ShapeSeed = static_cast<uint32_t>(rng.Next());
			s.Summary = SightSummary(kind, station);
			switch (kind)
			{
				case VoidSight::GlassMoonlet: s.Color = Rgb(0.6f, 0.85f, 0.95f); break;
				case VoidSight::FrozenFountain: s.Color = Rgb(0.8f, 0.88f, 0.95f); break;
				case VoidSight::DerelictArk: s.Color = Rgb(0.45f, 0.45f, 0.48f); break;
				case VoidSight::StoneArch: s.Color = Rgb(0.55f, 0.48f, 0.4f); break;
				default: s.Color = Rgb(0.3f, 0.6f, 0.35f); break;
			}
			// The model (and a glow round it) reaches about 1.3 radii.
			for (int tries = 0; tries < 60; tries++, aKm *= 1.12)
			{
				Band b;
				for (int k = 0; k < 2; k++)
				{
					double reach = Disp(k, s.RadiusKm) * 1.3;
					b.In[k] = Ld(k, aKm) - reach;
					b.Out[k] = Ld(k, aKm) + reach;
				}
				if (!Clear(taken, b)) continue;
				taken.Add(b);
				break;
			}
			s.AKm = aKm;
			s.PeriodDays = PeriodAt(aKm);
			return s;
		};

		cs::List<VoidSite> sites;
		sites.Add(PlaceSight(sights[0], rng.Range(9000, 14000)));

		// ---- the asteroid drift: a clump of rocks on one orbit, sharing its period, so the formation it was checked in holds.
		{
			double centre = rng.Range(26000, 38000);
			double clumpAt = rng.Range(0, 360);
			// US-T130: the field's style sets how many rocks, how big, and how loosely they spread.
			int rocks = style == VoidFieldStyle::Rubble ? 9 + rng.Int(4) : style == VoidFieldStyle::Drift ? 6 + rng.Int(4) : 7 + rng.Int(4);
			double bigLo = 9, bigHi = 15, smallLo = 1.2, smallHi = 6.5, spread = 0.16, arc = 30;
			switch (style)
			{
				case VoidFieldStyle::Rubble: bigLo = 4; bigHi = 8; smallLo = 0.8; smallHi = 3.5; spread = 0.22; arc = 42; break;
				case VoidFieldStyle::MetalSwarm: bigLo = 6; bigHi = 11; smallLo = 1.5; smallHi = 5; spread = 0.12; arc = 24; break;
				case VoidFieldStyle::IceField: bigLo = 6; bigHi = 12; smallLo = 1; smallHi = 5; spread = 0.2; arc = 36; break;
				default: break;
			}
			int iceAt = rng.Int(rocks - 1) + 1;
			cs::List<VoidSite> field;
			for (int attempt = 0; attempt < 40; attempt++, centre *= 1.1)
			{
				field.Clear();
				double period = PeriodAt(centre);
				for (int r = 0; r < rocks; r++)
				{
					VoidSite s;
					s.Kind = VoidSiteKind::FieldRock;
					s.Name = cs::Cat(driftWord, " ", r + 1);
					s.ParentName = station;
					s.RadiusKm = r == 0 ? rng.Range(bigLo, bigHi) : rng.Range(smallLo, smallHi);
					s.Icy = iceField || (style == VoidFieldStyle::Drift && (r == iceAt || (r > 0 && rng.U() < 0.2)));
					s.Field = style;
					s.NodeDeg = node;
					s.PeriodDays = period;
					s.RotationHours = rng.Range(3, 40);
					s.ShapeSeed = static_cast<uint32_t>(rng.Next());
					float shade = static_cast<float>(rng.Range(0.75, 1.1));
					s.Color = s.Icy ? Rgb(0.72f * shade, 0.8f * shade, 0.88f * shade)
						: style == VoidFieldStyle::MetalSwarm ? Rgb(0.33f * shade, 0.32f * shade, 0.33f * shade)
						: style == VoidFieldStyle::Rubble ? Rgb(0.5f * shade, 0.4f * shade, 0.32f * shade)
						: Rgb(0.42f * shade, 0.38f * shade, 0.34f * shade);
					// Placed where it clears every rock placed so far at both scales, with their rubble, through a turn of the formation.
					bool placed = false;
					for (int tries = 0; tries < 50 && !placed; tries++)
					{
						s.AKm = centre * (1 + rng.Range(-spread, spread));
						s.LongitudeDeg = clumpAt + rng.Range(-arc, arc);
						s.InclinationDeg = tilt + rng.Range(-2.5, 2.5);
						placed = true;
						for (const auto& o : field)
						{
							for (int phase = 0; phase < 8 && placed; phase++)
							{
								double turn = phase * 45.0;
								auto p = OrbitAt(s.AKm, s.InclinationDeg, s.NodeDeg, s.LongitudeDeg + turn);
								auto q = OrbitAt(o.AKm, o.InclinationDeg, o.NodeDeg, o.LongitudeDeg + turn);
								for (int k = 0; k < 2 && placed; k++)
								{
									auto sp = p.Normalized() * Ld(k, s.AKm), sq = q.Normalized() * Ld(k, o.AKm);
									double need = (Disp(k, s.RadiusKm) * LocalRadii(RubbleRadii, k) + Disp(k, o.RadiusKm) * LocalRadii(RubbleRadii, k)) * 1.15;
									if ((sp - sq).Length() < need) placed = false;
								}
							}
							if (!placed) break;
						}
					}
					if (!placed) break;
					field.Add(s);
				}
				if (field.Count() < rocks) continue;
				Band b;
				for (int k = 0; k < 2; k++)
				{
					b.In[k] = cs::Double::PositiveInfinity;
					b.Out[k] = 0;
					for (const auto& s : field)
					{
						double reach = Disp(k, s.RadiusKm) * LocalRadii(RubbleRadii, k);
						b.In[k] = cs::Math::Min(b.In[k], Ld(k, s.AKm) - reach);
						b.Out[k] = cs::Math::Max(b.Out[k], Ld(k, s.AKm) + reach);
					}
				}
				if (!Clear(taken, b)) continue;
				taken.Add(b);
				break;
			}
			double lo = cs::Double::PositiveInfinity, hi = 0;
			for (const auto& s : field)
			{
				lo = cs::Math::Min(lo, s.AKm);
				hi = cs::Math::Max(hi, s.AKm);
			}
			plan.FieldInnerKm = lo * 0.97;
			plan.FieldOuterKm = hi * 1.03;
			plan.OutpostName = cs::Cat(driftWord, " Assay Camp");
			plan.OutpostPopulation = cs::Math::Round(rng.Range(2500, 9000) / 100) * 100;
			for (int r = 0; r < field.Count(); r++)
			{
				auto& s = field[r];
				cs::String what = iceField ? cs::String("A glittering chunk of clean ice") : s.Icy ? cs::String("A dirty-ice rock")
					: style == VoidFieldStyle::MetalSwarm ? cs::String("A dark, dense lump of metal") : cs::String("A stony, metal-rich rock");
				cs::String take = iceField ? cs::String("Water ice for anyone who stakes a claim (F10), and frozen ")
					: style == VoidFieldStyle::MetalSwarm ? cs::String("Nickel-iron for anyone who stakes a claim (F10), and rich seams of ")
					: cs::String("Nickel-iron and ice for anyone who stakes a claim (F10), and seams of ");
				s.Summary = r == 0
					? cs::Cat("The biggest rock of the ", plan.FieldName, ", a clump the generator of ", station, " holds in one "
						"orbit. ", take, oreName, " the ", plan.OutpostName,
						" here works and buys. Capital crews waiting out a drive cooldown mine here to pay for the stop. Fiction.")
					: cs::Cat(what, " of the ", plan.FieldName, ", riding the generator's field in formation with the rest. Claims on ",
						"it sell at the ", plan.OutpostName, ". Fiction.");
			}
			sites.AddRange(field);
		}

		if (sightCount == 2) sites.Add(PlaceSight(sights[1], rng.Range(60000, 85000)));

		// US-T130: a site on its own band, out from aKm until it is clear of everything at both scales.
		auto Place = [&](VoidSite& site, double aKm, double reachRadii)
		{
			for (int tries = 0; tries < 60; tries++, aKm *= 1.12)
			{
				Band b;
				for (int k = 0; k < 2; k++)
				{
					double reach = Disp(k, site.RadiusKm) * reachRadii;
					b.In[k] = Ld(k, aKm) - reach;
					b.Out[k] = Ld(k, aKm) + reach;
				}
				if (!Clear(taken, b)) continue;
				taken.Add(b);
				break;
			}
			site.AKm = aKm;
			site.PeriodDays = PeriodAt(aKm);
		};

		// ---- US-T130: the megastructure, a city the size of a moon, its spars reaching out with ships berthed along them.
		if (plan.Mix.Megastructure)
		{
			VoidSite m;
			m.Kind = VoidSiteKind::Megastructure;
			static const cs::List<cs::String> cities = { "Babel Stack", "the Hive of Ninefold", "Lantern Spire", "Concourse", "the Beehold",
				"Tenement Ring", "the Steeple", "Gantry City", "Ossuary Tower", "the Warrens" };
			m.Name = rng.Pick(cities);
			if (m.Name.rfind("the ", 0) == 0) m.Name[0] = 'T';
			m.ParentName = station;
			m.RadiusKm = rng.Range(30, 48);
			m.InclinationDeg = tilt + rng.Range(-2, 2);
			m.NodeDeg = node;
			m.LongitudeDeg = rng.Range(0, 360);
			m.RotationHours = rng.Range(60, 140);
			m.ShapeSeed = static_cast<uint32_t>(rng.Next());
			m.Color = Rgb(0.32f, 0.3f, 0.3f);
			plan.MegastructureName = m.Name;
			plan.MegastructurePopulation = cs::Math::Round(rng.Range(150e3, 600e3) / 1000) * 1000;
			m.Summary = cs::Cat(m.Name, ": a megastructure nobody remembers building, stacked tower on tower and lit by thousands of "
				"windows, its docking spars reaching tens of kilometres out with ships berthed all along them. Every capital ship "
				"waiting out a drive cooldown at ", station, " ends up here: a city of ", cs::Fmt(plan.MegastructurePopulation, "N0"),
				" that lives off the people who have to stop. Fiction.");
			Place(m, rng.Range(15000, 22000), 1.25);
			sites.Add(m);
		}

		// ---- US-T130: the nebula the station sits in, and the rock deep inside it a clan has made its haven.
		if (plan.Mix.Nebula)
		{
			static const cs::List<cs::String> clouds = { "Veil", "Shroud", "Smoke", "Pall", "Mantle", "Murk", "Bloom", "Caul", "Gauze", "Haze" };
			static const cs::List<cs::String> hues = { "Rose", "Cinder", "Violet", "Jade", "Amber", "Cobalt", "Saffron", "Orchid", "Teal", "Ember" };
			plan.NebulaName = cs::Cat("the ", rng.Pick(hues), " ", rng.Pick(clouds));
			VoidSite den;
			den.Kind = VoidSiteKind::DenRock;
			den.Name = cs::Cat(rng.Pick(DriftWords()), " Hollow");
			den.ParentName = station;
			den.RadiusKm = rng.Range(5, 10);
			den.InclinationDeg = tilt + rng.Range(-4, 4);
			den.NodeDeg = node;
			den.LongitudeDeg = rng.Range(0, 360);
			den.RotationHours = rng.Range(10, 40);
			den.ShapeSeed = static_cast<uint32_t>(rng.Next());
			den.Color = Rgb(0.3f, 0.27f, 0.26f);
			Place(den, rng.Range(48000, 66000), 1.2);
			plan.DenName = den.Name;
			static const cs::List<cs::String> dens = { "Rat's Nest", "the Lantern-Out", "Blackwater", "the Smokehole", "Gallows Rest", "the Burrow" };
			plan.HavenName = rng.Pick(dens);
			if (plan.HavenName.rfind("the ", 0) == 0) plan.HavenName[0] = 'T';
			plan.NebulaRadiusKm = den.AKm * rng.Range(1.3, 1.6);
			den.Summary = cs::Cat("A dark rock deep in ", plan.NebulaName, ", the nebula ", station, " hides in. The cloud swallows sensors "
				"(passive and active reach both cut short inside it), which is why a clan dug in here: their haven is found only by "
				"flying in close or by pinging (Tab) near it. Fiction.");
			sites.Add(den);
		}

		// ---- the planetoid: its core, then the chunks circling it on one blast plane, sharing one period.
		{
			VoidSite core;
			core.Kind = VoidSiteKind::Planetoid;
			core.Exploded = rng.U() < 0.5;
			core.Name = core.Exploded ? cs::Cat(planetoid, " Remnant") : cs::Cat("Cracked ", planetoid);
			plan.PlanetoidName = core.Name;
			core.ParentName = station;
			core.RadiusKm = core.Exploded ? rng.Range(60, 140) : rng.Range(110, 230);
			core.InclinationDeg = tilt + rng.Range(-2, 2);
			core.NodeDeg = node;
			core.LongitudeDeg = rng.Range(0, 360);
			core.RotationHours = rng.Range(8, 40);
			core.ShapeSeed = static_cast<uint32_t>(rng.Next());
			core.Color = core.Exploded ? Rgb(0.4f, 0.3f, 0.26f) : Rgb(0.46f, 0.4f, 0.34f);
			auto coreBody = Probe(BodyKind::DwarfPlanet, core.RadiusKm);

			int pieces = core.Exploded ? 5 + rng.Int(3) : 3 + rng.Int(2);
			double blastTilt = rng.Range(0, 70), blastNode = rng.Range(0, 360);
			double chunkPeriod = rng.Range(6, 14);
			cs::List<VoidSite> chunks;
			for (int c = 0; c < pieces; c++)
			{
				VoidSite s;
				s.Kind = VoidSiteKind::Chunk;
				s.Name = cs::Cat(planetoid, " Fragment ", c + 1);
				s.ParentName = core.Name;
				s.RadiusKm = core.RadiusKm * (core.Exploded ? rng.Range(0.09, 0.26) : rng.Range(0.14, 0.32));
				s.NodeDeg = blastNode;
				s.PeriodDays = chunkPeriod;
				s.RotationHours = rng.Range(2, 20);
				s.Exploded = core.Exploded;
				s.ShapeSeed = static_cast<uint32_t>(rng.Next());
				float shade = static_cast<float>(rng.Range(0.8, 1.1));
				s.Color = Rgb(core.Color.R * shade, core.Color.G * shade, core.Color.B * shade);
				bool placed = false;
				for (int tries = 0; tries < 60 && !placed; tries++)
				{
					s.AKm = core.RadiusKm * rng.Range(2.7, 5.6);
					s.InclinationDeg = blastTilt + rng.Range(-4, 4);
					s.LongitudeDeg = rng.Range(0, 360);
					placed = true;
					for (int k = 0; k < 2 && placed; k++)
					{
						// Clear of the core and the debris shell drawn round it.
						double shell = scales.s[k].DisplayRadius(core.RadiusKm) * LocalRadii(ShellOuterRadii, k);
						if (scales.s[k].LocalDistance(coreBody, s.AKm) - scales.s[k].DisplayRadius(s.RadiusKm) < shell * 1.1) placed = false;
					}
					for (const auto& o : chunks)
					{
						for (int phase = 0; phase < 8 && placed; phase++)
						{
							double turn = phase * 45.0;
							auto p = OrbitAt(s.AKm, s.InclinationDeg, s.NodeDeg, s.LongitudeDeg + turn);
							auto q = OrbitAt(o.AKm, o.InclinationDeg, o.NodeDeg, o.LongitudeDeg + turn);
							for (int k = 0; k < 2 && placed; k++)
							{
								auto sp = p.Normalized() * scales.s[k].LocalDistance(coreBody, s.AKm);
								auto sq = q.Normalized() * scales.s[k].LocalDistance(coreBody, o.AKm);
								if ((sp - sq).Length() < (scales.s[k].DisplayRadius(s.RadiusKm) + scales.s[k].DisplayRadius(o.RadiusKm)) * 1.3) placed = false;
							}
						}
						if (!placed) break;
					}
				}
				if (placed) chunks.Add(s);
			}
			for (auto& s : chunks)
				s.Summary = core.Exploded
					? cs::Cat("A piece of ", planetoid, ", blown off when it came apart and still circling what is left of it, its broken "
						"faces raw. Claims can be staked on it. Fiction.")
					: cs::Cat("A slab split from ", planetoid, " along the crack that runs through it, drifting slowly away from the rest. ",
						"Claims can be staked on it. Fiction.");
			core.Summary = core.Exploded
				? cs::Cat("What is left of ", planetoid, ", a planetoid that blew apart: a scorched core inside a shell of its own debris, its "
					"biggest pieces still circling it. The wreck of a world is a sight in itself, and the rubble is free for the taking. Fiction.")
				: cs::Cat(planetoid, ", a planetoid cracked through: one great fault runs round it, wrapped in a shell of the rubble it shed, ",
					"with the slabs that split away circling it. Pilots fly the crack for the view. Fiction.");

			// Out from its start until its whole system (core, shell and chunks) is clear of everything inside it at both scales.
			double a = rng.Range(150000, 220000);
			for (int tries = 0; tries < 60; tries++, a *= 1.12)
			{
				Band b;
				for (int k = 0; k < 2; k++)
				{
					double reach = scales.s[k].DisplayRadius(core.RadiusKm) * LocalRadii(ShellOuterRadii, k);
					for (const auto& s : chunks)
						reach = cs::Math::Max(reach, scales.s[k].LocalDistance(coreBody, s.AKm) + scales.s[k].DisplayRadius(s.RadiusKm));
					b.In[k] = Ld(k, a) - reach;
					b.Out[k] = Ld(k, a) + reach;
				}
				if (!Clear(taken, b)) continue;
				taken.Add(b);
				break;
			}
			core.AKm = a;
			core.PeriodDays = PeriodAt(a);
			sites.Add(core);
			sites.AddRange(chunks);

			double chunkReach = 0;
			for (const auto& s : chunks) chunkReach = cs::Math::Max(chunkReach, s.AKm);
			plan.OverviewSpanKm = (a + chunkReach) * 1.15;
		}

		for (const auto& s : sites) plan.OverviewSpanKm = cs::Math::Max(plan.OverviewSpanKm, s.AKm * 1.15);
		plan.OverviewSpanKm = cs::Math::Max(plan.OverviewSpanKm, plan.NebulaRadiusKm * 1.05);
		plan.Sites = sites;
		return plan;
	}

	void VoidSites::AddBodies(cs::List<cs::Ref<BodyDefinition>>& bodies, const VoidSitePlan& plan)
	{
		for (const auto& s : plan.Sites)
		{
			auto b = cs::New<BodyDefinition>();
			b->Name = s.Name;
			b->ParentName = s.ParentName;
			b->Kind = s.Kind == VoidSiteKind::Planetoid ? BodyKind::DwarfPlanet : s.Kind == VoidSiteKind::Megastructure ? BodyKind::Station : BodyKind::Asteroid;
			b->RadiusKm = s.RadiusKm;
			// Light enough that nothing round the generator falls into its pull, heavy enough to hold its chunks (the core).
			b->Gm = s.Kind == VoidSiteKind::Planetoid ? 1e-9 : s.Kind == VoidSiteKind::Chunk ? 1e-16 : 1e-13;
			b->Elements = OrbitalElements::FromClassical(Astro::J2000, s.AKm, 0, s.InclinationDeg, s.NodeDeg, 0, s.LongitudeDeg, s.PeriodDays);
			b->RotationHours = s.RotationHours;
			b->Color = s.Color;
			b->Status = BodyStatus::Fictional;
			b->Summary = s.Summary;
			bodies.Add(b);
		}
	}

	cs::List<cs::Ref<DebrisDisc>> VoidSites::Discs(const VoidSitePlan& plan)
	{
		cs::List<cs::Ref<DebrisDisc>> discs;
		if (plan.FieldOuterKm <= 0) return discs;
		auto d = cs::New<DebrisDisc>();
		d->Name = plan.FieldName;
		d->HostName = plan.Station;
		d->HostIndex = 0;
		d->InnerKm = plan.FieldInnerKm;
		d->OuterKm = plan.FieldOuterKm;
		d->Normal = plan.FieldNormal;
		d->Summary = cs::Cat("The orbit of the ", plan.FieldName, ", the asteroid drift of ", plan.Station, ". Fiction.");
		discs.Add(d);
		return discs;
	}

	void VoidSites::Settle(const cs::Ref<World>& world, const VoidSitePlan& plan)
	{
		if (world == nullptr || plan.Sites.Count() == 0) return;
		cs::Ref<BodyDefinition> rock;
		for (const auto& s : plan.Sites)
			if (s.Kind == VoidSiteKind::FieldRock && world->Catalog()->TryGet(s.Name, rock)) break;
		if (rock == nullptr) return;
		auto owner = world->Independents != nullptr ? world->Independents : world->Sol;
		if (owner == nullptr) return;
		auto camp = world->Economy->Add(plan.OutpostName, rock->Index, SettlementKind::MiningOutpost, owner, plan.OutpostPopulation);
		double scale = plan.OutpostPopulation / 5000;
		auto Mine = [&](const cs::Ref<Settlement>& at, Good g, double perDay) { at->Facilities.Add(cs::New<Facility>(Recipe::Mine(g, perDay * scale), 1)); };
		// US-T130: what the camp works follows the field: ice for water and volatiles, metal for iron and rare ore.
		bool ice = plan.Mix.Field == VoidFieldStyle::IceField, metal = plan.Mix.Field == VoidFieldStyle::MetalSwarm;
		Mine(camp, Good::NickelIron, ice ? 120 : metal ? 1500 : 900);
		Mine(camp, Good::Water, ice ? 1400 : 450);
		// The rarer ore, sized by its price so every camp's take is worth about the same.
		Mine(camp, plan.FieldOre, (metal ? 14000 : 9000) / cs::Math::Max(1.0, GoodInfo::Of(plan.FieldOre)->BasePrice));

		// US-T130: the megastructure's city: a big independent port living off the ships that have to stop here.
		cs::Ref<BodyDefinition> tower;
		if (!plan.MegastructureName.empty() && world->Catalog()->TryGet(plan.MegastructureName, tower))
		{
			auto city = world->Economy->Add(plan.MegastructureName, tower->Index, SettlementKind::Station, owner, plan.MegastructurePopulation);
			double k = plan.MegastructurePopulation / 300e3;
			city->Facilities.Add(cs::New<Facility>(Recipe::Mine(Good::Water, 900 * k), 1));
			city->Facilities.Add(cs::New<Facility>(Recipe::Of(IndustryKind::Smelter), 0.6 * k));
			city->Facilities.Add(cs::New<Facility>(Recipe::Of(IndustryKind::Shipyard), 0.5));
		}
		world->Economy->Initialise();

		// US-T130: the clan's haven, hidden on the rock deep in the nebula; one band of raiders already out of it.
		cs::Ref<BodyDefinition> den;
		if (!plan.DenName.empty() && world->Catalog()->TryGet(plan.DenName, den) && world->Pirates != nullptr && world->Pirates->IsPirate)
		{
			double jd = cs::Double::IsNaN(world->Jd) ? Astro::J2000 : world->Jd;
			if (world->FoundHaven(plan.HavenName, den->Index, world->Pirates, jd) != nullptr && world->War != nullptr)
				world->War->RaisePirateGroup(jd, den->Index);
		}
	}

	const VoidSite* VoidSites::Find(const cs::String& body, const cs::String& parent)
	{
		if (parent.empty()) return nullptr;
		std::lock_guard<std::mutex> lock(Gate());
		if (cs::Strings::StartsWith(parent, VoidStations::Prefix)) return Cached(parent).Find(body);
		// A chunk circles the planetoid, whose station was planned when the planetoid itself was looked up (or is found now).
		auto it = PlanetoidStation().find(parent);
		if (it == PlanetoidStation().end()) return nullptr;
		return Cached(it->second).Find(body);
	}

	cs::String VoidSites::KindLabel(const cs::Ref<BodyDefinition>& body)
	{
		if (body == nullptr) return cs::String();
		auto site = Find(body->Name, body->ParentName);
		if (site == nullptr) return cs::String();
		switch (site->Kind)
		{
			case VoidSiteKind::FieldRock: return site->Field == VoidFieldStyle::IceField ? "Ice chunk (mineable)" : site->Icy ? "Mineable ice asteroid" : "Mineable asteroid";
			case VoidSiteKind::Planetoid: return site->Exploded ? "Shattered planetoid" : "Cracked planetoid";
			case VoidSiteKind::Chunk: return "Planetoid fragment";
			case VoidSiteKind::Sight: return "Sight (worth the detour)";
			case VoidSiteKind::DenRock: return "Rock in the nebula";
			case VoidSiteKind::Megastructure: return "Megastructure (inhabited)";
			default: return cs::String();
		}
	}
}
