// Port of Assets/UnitySpace/Core/Systems/NearbySocieties.cs (US-T17).

#include "Systems/NearbySocieties.h"
#include "Systems/StarSystemSpec.h"
#include "World.h"

namespace SpaceGod::Core
{
	namespace
	{
		using Names = cs::List<cs::String>;

		// The C# object initialiser `new SocietySpec { Authority = .., Company = .., Traders = .., Raiders = .., Summary = .. }`.
		cs::Ref<SocietySpec> NewSociety(cs::Ref<FactionSpec> authority, cs::Ref<FactionSpec> company, cs::Ref<FactionSpec> traders, cs::Ref<FactionSpec> raiders, const cs::String& summary)
		{
			auto s = cs::New<SocietySpec>();
			s->Authority = authority;
			s->Company = company;
			s->Traders = traders;
			s->Raiders = raiders;
			s->Summary = summary;
			return s;
		}

		cs::Ref<FactionSpec> F(const cs::String& name, const cs::String& prefix, Rgb color, double treasury, double marines)
		{
			return cs::New<FactionSpec>(name, prefix, color, treasury, marines);
		}
	}

	cs::Ref<SocietySpec> NearbySocieties::For(const cs::Ref<SystemSpec>& system)
	{
		cs::Ref<SocietySpec> s;
		const cs::String& n = system->Name;
		if (n == "Barnard's Star") s = BarnardsStar();
		else if (n == "Luhman 16") s = Luhman16();
		else if (n == "Wolf 359") s = Wolf359();
		else if (n == "Lalande 21185") s = Lalande21185();
		else if (n == "Sirius") s = Sirius();
		else if (n == "Epsilon Eridani") s = EpsilonEridani();
		else if (n == "Ross 128") s = Ross128();
		else if (n == "Epsilon Indi") s = EpsilonIndi();
		else if (n == "Tau Ceti") s = TauCeti();
		else if (n == "GJ 1061") s = Gj1061();
		else if (n == "Luyten's Star") s = LuytensStar();
		else if (n == "Teegarden's Star") s = TeegardensStar();
		else return nullptr;
		s->System = system->Name;
		s->DistanceLy = system->DistanceLy;
		return s;
	}

	const cs::List<Good>& NearbySocieties::Metals() { static const cs::List<Good> v{ Good::NickelIron, Good::Titanium, Good::Copper, Good::Zinc }; return v; }
	const cs::List<Good>& NearbySocieties::Precious() { static const cs::List<Good> v{ Good::RareEarths, Good::Platinum }; return v; }
	const cs::List<Good>& NearbySocieties::Volatiles() { static const cs::List<Good> v{ Good::Water, Good::Nitrogen, Good::Hydrocarbons }; return v; }
	const cs::List<Good>& NearbySocieties::Skimmed() { static const cs::List<Good> v{ Good::HeliumThree, Good::Hydrocarbons, Good::Nitrogen }; return v; }

	cs::List<Good> NearbySocieties::Join(std::initializer_list<cs::List<Good>> sets)
	{
		cs::List<Good> list;
		for (const auto& set : sets)
			for (const auto& g : set)
				if (!list.Contains(g)) list.Add(g);
		return list.ToArray();
	}

	// ---------------------------------------------------------------- 5.96 ly

	cs::Ref<SocietySpec> NearbySocieties::BarnardsStar()
	{
		auto s = NewSociety(
			// The Federation's own frontier (U.SPC-T91): the one star past Centauri it governs from Earth, with the same
			// treasury, navy and capital as at home; the farther systems are states of their own.
			F(World::SolFederation, "SFS", World::SolFederationColor, 6e8, 1.25),
			F("Arrowhead Metals", "AHM", C(0.85f, 0.8f, 0.45f), 5e8, 0.8),
			F("Runaway Star Traders", "RST", C(0.8f, 0.82f, 0.9f), 1e8, 0.6),
			F("The Drift", "", C(0.75f, 0.3f, 0.55f), 6e7, 1.15),
			"The first star past Centauri the gates reached, and the Sol Federation's frontier province: a busy frontier of hot mining worlds and a hub station in the habitable zone.");
		s->Station("Arrow Station", "Barnard's Star", 0.075, 30, "The Federation's hub in the habitable zone, outside all four planets' orbits.");
		s->Station("Longwater", "Barnard's Star", 0.6, 200, "An ice harvester working a comet swarm (no belt is known here).");
		s->Station("Slipstream", "Barnard's Star", 0.3, 120, "A raider hold in a dead freighter's hull, parked off the lanes.");
		s->Add("Arrow Station", "Arrow Station", Station, Gov, 260e3, "The only temperate orbit in the system; a station, since every planet is too hot.",
			G({ Good::HeliumThree }), I({ Fab, Works, Pharma }), 0.8, 0.6);
		s->Add("Ironside", "Barnard e", Outpost, Co, 160e3, "The coolest of the four rocky worlds, still twice Earth's sunlight: metal mines dug in on the night side.",
			Metals(), I({ Smelter }), 0.5);
		s->Add("Crucible", "Barnard c", Outpost, Co, 50e3, "The heaviest planet: rare metals, and helium-3 from the stellar wind in its regolith.",
			With(Precious(), { Good::Copper, Good::HeliumThree }));
		s->Add("Longwater", "Longwater", Station, Gov, 40e3, "Water, nitrogen and hydrocarbons from captured comets.", Volatiles());
		s->Add("Slipstream", "Slipstream", Haven, Raid, 8e3, "The Drift's hold.");
		s->Lane("Arrow Station", "Barnard e", 0.06);
		s->Lane("Arrow Station", "Barnard c", -0.05);
		s->Lane("Barnard c", "Barnard e", 0.04);
		s->Lane("Arrow Station", "Longwater", -0.04);
		s->Lane("Longwater", "Slipstream", 0.05);
		s->Freighters = Names{ "Proper Motion", "Runaway", "Tenth Arcsecond", "Ophiuchus", "Old Red", "Fast Mover", "Nearest Single", "Long Drift", "Dimlight", "Second Neighbour", "Steady Glow", "Cold Iron" };
		s->Haulers = Names{ "Night Side", "Slag Moth", "Ironmonger", "Furnace Rat" };
		s->Couriers = Names{ "Quickstep", "Barnard's Arrow", "Errand" };
		s->Yachts = Names{ "Wanderjahr" };
		s->Explorers = Names{ "Over the Horizon" };
		s->Prospectors = Names{ "Iron Hope" };
		s->Navy = Names{ "Steadfast", "Bulwark", "Sentry" };
		s->RaiderNames = Names{ "Slipknot", "Undertow", "Riptide", "Drift Ice", "Cutpurse", "Fade" };
		s->Companies.Add({ "Arrowhead Ore", "AHO", "Ironside", Co, true });
		s->Companies.Add({ "Runaway Star Carrying Company", "RSC", "Arrow Station", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 6.51 ly

	cs::Ref<SocietySpec> NearbySocieties::Luhman16()
	{
		auto s = NewSociety(
			F("Twin Dark Authority", "TDA", C(0.6f, 0.45f, 0.35f), 4e8, 1.0),
			F("Cloudline Extraction", "CLX", C(0.9f, 0.6f, 0.3f), 4e8, 0.8),
			F("Ember Lantern Traders", "ELT", C(0.85f, 0.75f, 0.65f), 8e7, 0.6),
			nullptr,
			"Two brown dwarfs and no worlds: skimmer stations dredging the dwarfs' clouds, and little else.");
		s->Station("Cloudbreak", "Luhman 16A", 0.006, 40, "The Authority's skimmer hub, low over the larger brown dwarf.");
		s->Station("Ironrain", "Luhman 16B", 0.005, 220, "A dredge station in the lighter dwarf's patchy iron and silicate clouds.");
		s->Station("Scree", "Luhman 16A", 0.05, 160, "A rubble-processing station on a captured swarm of rock (no disc is known here).");
		s->Add("Cloudbreak", "Cloudbreak", Station, Gov, 80e3, "Water, methane and ammonia (for nitrogen) skimmed from a T-dwarf's air.",
			With(Volatiles(), { Good::HeliumThree }), I({ Pharma, Fab }), 0.4);
		s->Add("Ironrain", "Ironrain", Station, Co, 55e3, "Iron and silicates rained out of the cloud decks.",
			G({ Good::NickelIron, Good::Titanium }), I({ Smelter, Works }));
		// Too far out and too small for the Authority's writ: its middlemen keep a black market (U.SPC-T94).
		s->Add("Scree", "Scree", Outpost, Co, 35e3, "Metals and rare metals from captured rubble.", With(Precious(), { Good::Copper, Good::Zinc }), {}, 0, 0, false, true);
		s->Lane("Cloudbreak", "Scree", 0.05);
		s->Lane("Cloudbreak", "Ironrain", -0.03);
		s->Lane("Scree", "Ironrain", 0.03);
		s->Freighters = Names{ "Dim Ember", "Cloud Diver", "Iron Rain", "Twin Shadow" };
		s->Haulers = Names{ "Dredge" };
		s->Couriers = Names{ "Faint Signal" };
		s->Yachts = Names{ "Infrared" };
		s->Navy = Names{ "Lantern", "Spark" };
		s->Companies.Add({ "Cloudline Haulage", "CLH", "Ironrain", Co, true });
		s->Companies.Add({ "Ember Lantern Carriers", "ELC", "Cloudbreak", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 7.86 ly

	cs::Ref<SocietySpec> NearbySocieties::Wolf359()
	{
		auto s = NewSociety(
			F("Wolf Flare Accord", "WFA", C(0.95f, 0.35f, 0.3f), 4e8, 1.1),
			F("Leo Mining Combine", "LMC", C(0.95f, 0.75f, 0.35f), 4e8, 0.8),
			F("Flarewatch Free Traders", "FFT", C(0.8f, 0.8f, 0.75f), 8e7, 0.6),
			nullptr,
			"A flare star's hardened stations and a skimmer on its one (unconfirmed) giant.");
		s->Station("Flarewall", "Wolf 359", 0.045, 80, "The Accord's hub in the habitable zone, shielded deep against the star's flares.");
		s->Station("Cinderfall", "Wolf 359", 0.4, 260, "A smelting station on a captured swarm of rock (no belt is known here).");
		s->Add("Flarewall", "Flarewall", Station, Gov, 100e3, "The temperate orbit; nothing else at this star is.", {}, I({ Fab, Works, Pharma }), 0.6, 0.4);
		s->Add("Howling Deep", "Wolf 359 b", Station, Gov, 80e3, "An aerostat skimming the cold giant (a candidate planet).", With(Skimmed(), { Good::Water }));
		s->Add("Cinderfall", "Cinderfall", Outpost, Co, 70e3, "Ore and rare metals from the rubble.", Join({ Metals(), Precious() }), I({ Smelter }), 0, 0, false, true);
		s->Lane("Flarewall", "Cinderfall", 0.05);
		s->Lane("Cinderfall", "Wolf 359 b", -0.04);
		s->Lane("Flarewall", "Wolf 359 b", 0.03);
		s->Freighters = Names{ "Flare Warning", "Leo's Mane", "Faint Sun", "Shield Wall", "Burst" };
		s->Haulers = Names{ "Cinder Moth", "Grit" };
		s->Couriers = Names{ "Fast Flare" };
		s->Yachts = Names{ "Long Howl" };
		s->Prospectors = Names{ "Pick of the Litter" };
		s->Navy = Names{ "Grey Wolf", "Snarl", "Fang" };
		s->Companies.Add({ "Leo Ore Haulers", "LOH", "Cinderfall", Co, true });
		s->Companies.Add({ "Flarewatch Carriers", "FWC", "Flarewall", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 8.30 ly

	cs::Ref<SocietySpec> NearbySocieties::Lalande21185()
	{
		auto s = NewSociety(
			F("Ursa Commonwealth", "UCW", C(0.4f, 0.7f, 0.95f), 5e8, 1.1),
			F("Hearthstone Combine", "HSC", C(0.9f, 0.65f, 0.35f), 4e8, 0.8),
			F("Great Bear Carriers", "GBC", C(0.8f, 0.85f, 0.9f), 8e7, 0.6),
			nullptr,
			"The brightest red dwarf in the north: a habitable-zone hub, a hot mining world and a skimmer on the cold Neptune.");
		s->Station("Ursa Station", "Lalande 21185", 0.18, 300, "The Commonwealth's hub in the habitable zone, between the hot super-Earth and the candidate d.");
		s->Add("Ursa Station", "Ursa Station", Station, Gov, 180e3, "The temperate orbit, where the lanes meet: the yards and the Commonwealth.", {}, {}, 0.7, 0.6);
		s->Add("Hearthstone", "Lalande 21185 b", Outpost, Co, 120e3, "A hot super-Earth's metal mines, under three times Earth's sunlight.", Metals(), I({ Smelter, Fab, Works }), 0.3);
		s->Add("Bear Skimmer", "Lalande 21185 c", Station, Gov, 60e3, "An aerostat skimming the cold Neptune, with the labs that turn its hydrocarbons into medicine.", With(Skimmed(), { Good::Water }), I({ Pharma }));
		s->Add("Polaris Diggings", "Lalande 21185 d", Outpost, Co, 40e3, "Rare metals on a cold world (a candidate planet).", With(Precious(), { Good::Water }));
		s->Lane("Ursa Station", "Lalande 21185 b", 0.05);
		s->Lane("Ursa Station", "Lalande 21185 d", -0.04);
		s->Lane("Lalande 21185 d", "Lalande 21185 c", 0.03);
		s->Freighters = Names{ "Great Bear", "Big Dipper", "Northern Light", "Ursa Minor", "Seven Stars", "Hearth Fire", "Pole Star" };
		s->Haulers = Names{ "Bear Claw", "Hearthstone Mule" };
		s->Couriers = Names{ "Alkaid", "Merak" };
		s->Yachts = Names{ "Northbound" };
		s->Prospectors = Names{ "Honeypot" };
		s->Navy = Names{ "Arcturus", "Guardian", "Bearward" };
		s->Companies.Add({ "Hearthstone Ore", "HSO", "Hearthstone", Co, true });
		s->Companies.Add({ "Great Bear Freight", "GBF", "Ursa Station", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 8.61 ly

	cs::Ref<SocietySpec> NearbySocieties::Sirius()
	{
		auto s = NewSociety(
			F("Sirian Directorate", "SDR", C(0.7f, 0.85f, 1.0f), 5e8, 1.0),
			F("Canis Major Foundries", "CMF", C(0.95f, 0.85f, 0.5f), 5e8, 0.8),
			F("Dogstar Free Traders", "DFT", C(0.85f, 0.88f, 0.92f), 8e7, 0.6),
			nullptr,
			"A white star and its white dwarf with no worlds at all: solar-powered stations on stable orbits close to each.");
		s->Station("Canicula", "Sirius A", 2.2, 50, "The Directorate's solar-powered hub, well inside the orbit B allows (its habitable zone, past 5 AU, is not stable).");
		s->Station("Frostline", "Sirius A", 1.4, 230, "An ice station working a comet swarm close to A (no belt is known here).");
		s->Station("Pup Forge", "Sirius B", 0.4, 140, "A foundry by the white dwarf, on captured rock (no disc is known here).");
		s->Add("Canicula", "Canicula", Station, Gov, 150e3, "Sunlight for nothing, and the helium-3 of A's hard wind.", G({ Good::HeliumThree }), I({ Fab, Works, Pharma }), 0.6, 0.5);
		s->Add("Frostline", "Frostline", Station, Gov, 40e3, "Water, nitrogen and hydrocarbons from captured comets.", Volatiles());
		s->Add("Pup Forge", "Pup Forge", Outpost, Co, 90e3, "Ore and rare metals from the rubble, smelted by the white dwarf's light.", Join({ Metals(), Precious() }), I({ Smelter }), 0.3);
		s->Lane("Canicula", "Frostline", 0.04);
		s->Lane("Canicula", "Pup Forge", -0.03);
		s->Lane("Frostline", "Pup Forge", 0.03);
		s->Freighters = Names{ "Dog Star", "White Fire", "Canicula", "Heliacal Rising", "The Pup" };
		s->Haulers = Names{ "Forge Hound" };
		s->Couriers = Names{ "Sothis" };
		s->Yachts = Names{ "Brightest" };
		s->Navy = Names{ "Orion's Hound", "Laelaps" };
		s->Companies.Add({ "Canis Major Ore", "CMO", "Pup Forge", Co, true });
		s->Companies.Add({ "Dogstar Carriers", "DSC", "Canicula", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 10.47 ly

	cs::Ref<SocietySpec> NearbySocieties::EpsilonEridani()
	{
		auto s = NewSociety(
			F("Eridani Union", "ERU", C(0.45f, 0.8f, 0.85f), 6e8, 1.1),
			F("Aegir Deepworks", "ADW", C(0.4f, 0.55f, 0.9f), 5e8, 0.8),
			F("River Traders", "RVT", C(0.82f, 0.86f, 0.9f), 1e8, 0.6),
			F("Wave Daughters", "", C(0.55f, 0.3f, 0.75f), 6e7, 1.15),
			"The nearest system like our own: a habitable-zone hub, miners in both inner belts and a skimmer on AEgir.");
		s->Station("Eridanus", "Epsilon Eridani", 0.75, 10, "The Union's hub in the habitable zone of a young orange dwarf.");
		s->Station("Ran", "Epsilon Eridani", 1.75, 130, "A mining station in the warm inner belt.");
		s->Station("Hlesey", "Epsilon Eridani", 12, 250, "An ice station in the outer belt.");
		s->Station("Wavecrest", "Epsilon Eridani", 16, 60, "A raider hold hidden in the outer belt's dust.");
		s->Add("Eridanus", "Eridanus", Station, Gov, 280e3, "The temperate orbit, where the lanes meet: the yards and the Union.", {}, {}, 0.8, 0.6);
		s->Add("Ran", "Ran", Outpost, Co, 200e3, "Ore and rare metals from the inner belt's rock, and the fabs and works they feed.", Join({ Metals(), Precious() }), I({ Smelter, Fab, Works }), 0.4);
		s->Add("AEgir Skimmer", "Epsilon Eridani b", Station, Gov, 90e3, "An aerostat skimming the Jupiter-mass giant, with labs on its hydrocarbons.", Skimmed(), I({ Pharma }));
		s->Add("Hlesey", "Hlesey", Station, Co, 70e3, "Water and nitrogen from the outer belt's ice.", Volatiles());
		s->Add("Wavecrest", "Wavecrest", Haven, Raid, 8e3, "The Wave Daughters' hold.", {}, {}, 0, 0, true);
		s->HavenSite("Farstrand", "Epsilon Eridani", 70, 200, "An empty hide among the icy debris of the 70 AU ring: room for another clan's hold.");
		s->Lane("Eridanus", "Ran", 0.05);
		s->Lane("Ran", "Epsilon Eridani b", -0.04);
		s->Lane("Epsilon Eridani b", "Hlesey", 0.04);
		s->Lane("Eridanus", "Epsilon Eridani b", 0.03);
		s->Freighters = Names{ "River of Stars", "AEgir's Cup", "Young Sun", "Eridanus", "Delta", "Long Water", "Ninth Wave" };
		s->Haulers = Names{ "Reef", "Shoal" };
		s->Couriers = Names{ "Ran's Net", "Swift Current" };
		s->Yachts = Names{ "Ozma", "Dustlight" };
		s->Prospectors = Names{ "Beachcomber" };
		s->Navy = Names{ "Hymir", "Breaker", "Skerry" };
		s->RaiderNames = Names{ "Billow", "Blood-Hair", "Comber", "Surge", "Undertow", "Wavelet" };
		s->Companies.Add({ "Aegir Ore", "AGO", "Ran", Co, true });
		s->Companies.Add({ "River Freight", "RFT", "Eridanus", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 11.01 ly

	cs::Ref<SocietySpec> NearbySocieties::Ross128()
	{
		auto s = NewSociety(
			F("Ross Landing Council", "RLC", C(0.5f, 0.85f, 0.55f), 5e8, 1.0),
			F("Virgo Quarrying", "VQC", C(0.85f, 0.7f, 0.5f), 4e8, 0.8),
			F("Quiet Star Freight", "QSF", C(0.82f, 0.86f, 0.82f), 8e7, 0.6),
			nullptr,
			"A calm red dwarf and its temperate world: the most Earth-like colony of the frontier, and a quarry station.");
		s->Station("Ross Quarry", "Ross 128", 0.15, 200, "A quarry station on a captured swarm of rock (no belt is known here).");
		s->Add("Calm Harbour", "Ross 128 b", Colony, Gov, 450e3, "A temperate world round a star that rarely flares: farms under domes.",
			Join({ Volatiles() }), I({ Pharma, Fab, Works }), 0.6, 0.4);
		s->Add("Ross Quarry", "Ross Quarry", Outpost, Co, 150e3, "Ore, rare metals and wind-blown helium-3 from the rubble.", Join({ Metals(), Precious(), cs::List<Good>{ Good::HeliumThree } }), I({ Smelter }));
		s->Lane("Ross 128 b", "Ross Quarry", 0.05);
		s->Freighters = Names{ "Stillwater", "Calm Seas", "Virgo", "Quiet Hours", "Harbour Light" };
		s->Haulers = Names{ "Quarryman", "Gravel" };
		s->Couriers = Names{ "Zavijava" };
		s->Yachts = Names{ "Hushed" };
		s->Navy = Names{ "Vigil", "Watchman" };
		s->Companies.Add({ "Virgo Ore", "VGO", "Ross Quarry", Co, true });
		s->Companies.Add({ "Quiet Star Lines", "QSL", "Calm Harbour", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 11.87 ly

	cs::Ref<SocietySpec> NearbySocieties::EpsilonIndi()
	{
		auto s = NewSociety(
			F("Indus Protectorate", "INP", C(0.95f, 0.65f, 0.45f), 4e8, 1.0),
			F("Indus Gas and Ore", "IGO", C(0.7f, 0.8f, 0.45f), 4e8, 0.8),
			F("Peacock Free Traders", "PFT", C(0.4f, 0.75f, 0.75f), 8e7, 0.6),
			nullptr,
			"An orange dwarf with a habitable-zone hub, a quarry and a skimmer on its super-Jupiter; the brown dwarfs 1,500 AU out are left alone.");
		s->Station("Indus Station", "Epsilon Indi A", 0.6, 100, "The Protectorate's hub in the habitable zone.");
		s->Station("Tucana Quarry", "Epsilon Indi A", 2.5, 300, "A quarry station on a captured swarm of rock (no belt is known here).");
		s->Add("Indus Station", "Indus Station", Station, Gov, 220e3, "The temperate orbit, where the lanes meet: the yards and the Protectorate.", {}, {}, 0.6, 0.5);
		s->Add("Ab Skimmer", "Epsilon Indi Ab", Station, Co, 80e3, "An aerostat skimming the cold super-Jupiter, with labs on its hydrocarbons.", With(Skimmed(), { Good::Water }), I({ Pharma }));
		s->Add("Tucana Quarry", "Tucana Quarry", Outpost, Co, 130e3, "Ore and rare metals from the rubble, and the fabs and works they feed.", Join({ Metals(), Precious() }), I({ Smelter, Fab, Works }));
		s->Lane("Indus Station", "Tucana Quarry", 0.05);
		s->Lane("Tucana Quarry", "Epsilon Indi Ab", -0.04);
		s->Lane("Indus Station", "Epsilon Indi Ab", 0.03);
		s->Freighters = Names{ "Peacock", "Toucan", "Southern Sky", "Indus River", "Tail Feather" };
		s->Haulers = Names{ "Rock Wren" };
		s->Couriers = Names{ "Far Pair" };
		s->Yachts = Names{ "Brown Study" };
		s->Prospectors = Names{ "Cold Comfort" };
		s->Navy = Names{ "Pavo", "Grus", "Tucana" };
		s->Companies.Add({ "Indus Ore", "INO", "Tucana Quarry", Co, true });
		s->Companies.Add({ "Peacock Carriers", "PKC", "Indus Station", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 11.91 ly

	cs::Ref<SocietySpec> NearbySocieties::TauCeti()
	{
		auto s = NewSociety(
			F("Ceti Federation", "CTF", C(0.55f, 0.75f, 1.0f), 5e8, 1.1),
			F("Whale Road Mining", "WRM", C(0.4f, 0.6f, 0.75f), 4e8, 0.8),
			F("Cetus Free Traders", "CFT", C(0.82f, 0.85f, 0.9f), 8e7, 0.6),
			nullptr,
			"The nearest single Sun-like star: a habitable-zone hub, miners on its worlds and ice from its heavy debris disc.");
		s->Station("Cetus Station", "Tau Ceti", 0.9, 170, "The Federation's hub in the habitable zone, between e's old place and f.");
		s->Station("Cometreach", "Tau Ceti", 10, 40, "An ice station in the inner edge of the debris disc.");
		s->Add("Cetus Station", "Cetus Station", Station, Gov, 300e3, "The temperate orbit, where the lanes meet: the yards and the Federation.", {}, {}, 0.7, 0.6);
		s->Add("Frostfall", "Tau Ceti f", Outpost, Gov, 140e3, "A cold super-Earth just past the habitable zone: ice and titanium under domes.",
			G({ Good::Water, Good::Nitrogen, Good::Titanium, Good::RareEarths }));
		s->Add("Kiln", "Tau Ceti h", Outpost, Co, 110e3, "A hot world's metal mines, and the fabs and works they feed.", Join({ Metals(), cs::List<Good>{ Good::Platinum } }), I({ Smelter, Fab, Works }), 0.3);
		s->Add("Cometreach", "Cometreach", Station, Co, 60e3, "Water, hydrocarbons and helium-3 from the disc's comets, and labs on the hydrocarbons.", With(Volatiles(), { Good::HeliumThree }), I({ Pharma }),
			0, 0, false, true);
		// Deep in the disc, a hollow where a clan from elsewhere could hide a haven (U.SPC-T94).
		s->HavenSite("Driftglass", "Tau Ceti", 30, 300, "A hollow comet deep in the debris disc, dark and empty: room for a raider hold.");
		s->Lane("Cetus Station", "Tau Ceti f", 0.04);
		s->Lane("Cetus Station", "Tau Ceti h", -0.05);
		s->Lane("Tau Ceti f", "Cometreach", 0.04);
		s->Freighters = Names{ "Whale Road", "Leviathan", "Sun Twin", "Ceti Alpha", "Deep Disc", "Old Sun" };
		s->Haulers = Names{ "Kiln Mule", "Snowball" };
		s->Couriers = Names{ "Menkar" };
		s->Yachts = Names{ "Ozma's Call", "Long Silence" };
		s->Prospectors = Names{ "Comet Chaser" };
		s->Navy = Names{ "Cetus", "Harpoon", "Fluke" };
		s->Companies.Add({ "Whale Road Ore", "WRO", "Kiln", Co, true });
		s->Companies.Add({ "Cetus Lines", "CTL", "Cetus Station", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 11.98 ly

	cs::Ref<SocietySpec> NearbySocieties::Gj1061()
	{
		auto s = NewSociety(
			F("Horologe Council", "HGC", C(0.75f, 0.7f, 0.95f), 4e8, 1.0),
			F("Clockwork Metals", "CWM", C(0.85f, 0.7f, 0.4f), 4e8, 0.8),
			F("Escapement Carriers", "ESC", C(0.85f, 0.85f, 0.85f), 6e7, 0.6),
			nullptr,
			"A quiet red dwarf's three Earth-mass worlds: a colony on the temperate outer one, mines on the others.");
		s->Add("Tidewater", "GJ 1061 d", Colony, Gov, 230e3, "The temperate outer world, getting half Earth's light.", Join({ Volatiles() }), I({ Pharma, Fab, Works }), 0.5, 0.3);
		s->Add("Balance Wheel", "GJ 1061 c", Outpost, Co, 120e3, "Metal mines on the warmer middle world.", Join({ Metals(), cs::List<Good>{ Good::HeliumThree } }), I({ Smelter }));
		s->Add("Hairspring", "GJ 1061 b", Outpost, Co, 50e3, "Rare metals on the hot inner world.", Precious());
		s->Lane("GJ 1061 d", "GJ 1061 c", 0.05);
		s->Lane("GJ 1061 c", "GJ 1061 b", -0.05);
		s->Freighters = Names{ "Tick", "Tock", "Pendulum", "Mainspring" };
		s->Haulers = Names{ "Ratchet" };
		s->Couriers = Names{ "Second Hand" };
		s->Yachts = Names{ "Horologium" };
		s->Navy = Names{ "Regulator", "Chime" };
		s->Companies.Add({ "Clockwork Ore", "CWO", "Balance Wheel", Co, true });
		s->Companies.Add({ "Escapement Freight", "ESF", "Tidewater", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 12.35 ly

	cs::Ref<SocietySpec> NearbySocieties::LuytensStar()
	{
		auto s = NewSociety(
			F("Sonar Assembly", "SNA", C(0.45f, 0.85f, 0.8f), 4e8, 1.0),
			F("Canis Minor Mining", "CMM", C(0.9f, 0.7f, 0.45f), 4e8, 0.8),
			F("Signal Free Traders", "SGT", C(0.85f, 0.88f, 0.85f), 6e7, 0.6),
			nullptr,
			"The red dwarf humanity once sent a message to: a colony on its temperate super-Earth, mines and a skimmer.");
		s->Add("Sonar Landing", "Luyten b", Colony, Gov, 230e3, "The temperate super-Earth GJ 273 b, the 2017 message's target.", Join({ Volatiles() }), I({ Pharma, Fab, Works }), 0.5, 0.3);
		s->Add("Echo Mines", "Luyten c", Outpost, Co, 110e3, "A hot rocky world's metal mines.", Join({ Metals(), Precious() }), I({ Smelter }));
		s->Add("Reply Skimmer", "Luyten d", Station, Co, 50e3, "An aerostat on a cold mini-Neptune (a candidate planet).", Skimmed());
		s->Lane("Luyten b", "Luyten c", 0.05);
		s->Lane("Luyten b", "Luyten d", -0.04);
		s->Freighters = Names{ "Carrier Wave", "Sonar", "Calling", "Long Reply" };
		s->Haulers = Names{ "Echo" };
		s->Couriers = Names{ "Ping" };
		s->Yachts = Names{ "Listener" };
		s->Navy = Names{ "Beacon", "Pulse" };
		s->Companies.Add({ "Canis Minor Ore", "CMI", "Echo Mines", Co, true });
		s->Companies.Add({ "Signal Lines", "SGL", "Sonar Landing", Gov, false });
		return s;
	}

	// ---------------------------------------------------------------- 12.50 ly

	cs::Ref<SocietySpec> NearbySocieties::TeegardensStar()
	{
		auto s = NewSociety(
			F("Aries Settlement Board", "ASB", C(0.95f, 0.8f, 0.55f), 4e8, 1.0),
			F("Ramshorn Mining", "RHM", C(0.75f, 0.6f, 0.4f), 4e8, 0.8),
			F("Fleece Carriers", "FLC", C(0.9f, 0.88f, 0.75f), 6e7, 0.6),
			nullptr,
			"The far edge: a colony on one of the most Earth-like worlds known, with an ice outpost and a mine beside it.");
		s->Add("Golden Fleece", "Teegarden's Star b", Colony, Gov, 230e3, "An Earth-mass world with about Earth's sunlight.", G({ Good::Water, Good::Nitrogen }), {}, 0.5, 0.3);
		s->Add("Ramshorn", "Teegarden's Star c", Outpost, Co, 80e3, "Metal mines on the cooler middle world, and the fabs and works they feed.", Join({ Metals(), Precious() }), I({ Smelter, Fab, Works }));
		s->Add("Hamal Ice", "Teegarden's Star d", Outpost, Gov, 60e3, "Ice, hydrocarbons and helium-3 on the cold outer world, and labs on the hydrocarbons.", With(Volatiles(), { Good::HeliumThree }), I({ Pharma }));
		s->Lane("Teegarden's Star b", "Teegarden's Star c", 0.05);
		s->Lane("Teegarden's Star c", "Teegarden's Star d", -0.05);
		s->Freighters = Names{ "Golden Ram", "Fleece", "Argo", "First Point" };
		s->Haulers = Names{ "Tup" };
		s->Couriers = Names{ "Hermes' Ram" };
		s->Yachts = Names{ "Far Edge" };
		s->Navy = Names{ "Ram", "Bellwether" };
		s->Companies.Add({ "Ramshorn Ore", "RHO", "Ramshorn", Co, true });
		s->Companies.Add({ "Fleece Lines", "FLL", "Golden Fleece", Gov, false });
		return s;
	}
}
