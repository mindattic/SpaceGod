// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Facilities.cs (see docs/MIGRATION.md).

#include "Combat/Facilities.h"

#include "Combat/Boarding.h"
#include "Combat/BreacherBays.h"
#include "Combat/BulkFreighters.h"
#include "Combat/CombatSpecs.h"
#include "Combat/Construction.h"
#include "Combat/GateBreakers.h"
#include "Combat/LaneInterdictors.h"
#include "Combat/MarineHabitats.h"
#include "Combat/Outfitting.h"
#include "Combat/SpinalMounts.h"
#include "Economy/Goods.h"
#include "Ships.h"
#include "Systems/CruiseDrive.h"

namespace SpaceGod::Core
{
	namespace
	{
		using BillOf = cs::List<std::pair<Good, double>>;

		// `new FacilitySite(...)`, spelled short so the layouts below read as the C# does.
		cs::Ref<FacilitySite> Site(const cs::String& kind, const cs::String& name = cs::String(), double z = 0, double y = 0,
			int link = FacilitySite::Keel, cs::Nullable<double> berths = std::nullopt)
		{
			return cs::New<FacilitySite>(kind, name, z, y, link, berths);
		}

		// The key an OrdinalIgnoreCase dictionary or set files a facility id under.
		cs::String Key(const cs::String& id) { return cs::Strings::ToLowerInvariant(id); }
	}

	// ---------------------------------------------------------------- FacilityKind

	FacilityKind::FacilityKind(const cs::String& id, const cs::String& name, double size, double massTonnes, bool scaled, const cs::List<std::pair<Good, double>>& bill,
		cs::Nullable<cs::List<HullCategory>> categories, bool fittable, cs::Nullable<CompartmentKind> compartment,
		cs::Nullable<ModuleKind> module, cs::Action<cs::Ref<CombatSpec>, cs::Ref<Loadout>, int> effect, double berths,
		double cargoTonnes, int holdRows, const cs::HashSet<ShipClass>* classes)
	{
		Berths = cs::Math::Max(0.0, berths);
		Id = id; // id ?? throw new ArgumentNullException(nameof(id)): a cs::String is never null
		Name = !name.empty() ? name : id;
		Size = cs::Math::Max(0.0, size);
		MassTonnes = massTonnes;
		Scaled = scaled;
		Bill = bill; // bill ?? Array.Empty<(Good, double)>()
		Categories = categories;
		Fittable = fittable;
		Compartment = compartment;
		Module = module;
		Effect = effect;
		CargoTonnes = cs::Math::Max(0.0, cargoTonnes);
		HoldRows = cs::Math::Max(0, holdRows);
		Classes = classes;
	}

	cs::Ref<FacilityKind> FacilityKind::OfModule(const cs::String& id, ModuleKind module, double size, cs::Nullable<cs::List<HullCategory>> categories)
	{
		auto m = ModuleSpec::Of(module);
		return cs::New<FacilityKind>(id, m->Name, size, m->MassTonnes, m->Scaled, m->Bill, categories, true, std::nullopt, cs::Nullable<ModuleKind>(module));
	}

	double FacilityKind::Tonnes(int i, double scale) { return Bill[i].second * (Scaled ? scale : 1); }

	double FacilityKind::Mass(double scale) { return MassTonnes * (Scaled ? scale : 1); }

	bool FacilityKind::AllowedOn(HullCategory category)
	{
		if (!Categories.has_value()) return true;
		for (auto c : *Categories) if (c == category) return true;
		return false;
	}

	bool FacilityKind::AllowedOn(HullCategory category, cs::Nullable<ShipClass> cls)
	{
		if (!AllowedOn(category) || (ClassFilter != nullptr && !ClassFilter(cls))) return false;
		if (cls.has_value() && FacilityLayouts::Bars(*cls, Id)) return false;
		return Classes == nullptr || (cls.has_value() && Classes->Contains(*cls));
	}

	// ---------------------------------------------------------------- FacilityKinds

	void FacilityKinds::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		// The static fields, in textual order...
		All();
		ById();
		BreacherHulls();
		MarineHulls();
		Capitals();
		// ...then the static constructor's body.
		Register(Structure(Bridge, "Bridge", 1, CompartmentKind::Bridge));
		Register(Structure(Engineering, "Engineering", 2, CompartmentKind::Engineering));
		Register(Structure(Quarters, "Crew quarters", 2, CompartmentKind::Quarters));
		Register(Structure(Hangar, "Hangar deck", 4, CompartmentKind::Hangar));
		Register(Structure(Hold, "Hold", 3, CompartmentKind::Hold));
		Register(Structure(Battery, "Battery magazine", 2, CompartmentKind::Battery));
		Register(Structure(Bow, "Bow section", 1, CompartmentKind::Bow));
		Register(Structure(Midships, "Midships", 1, CompartmentKind::Midships));
		// The team's seats inside a Breacher round (U.SPC-T466): what boarders storm out of once it has drilled in.
		Register(Structure(TroopCompartment, "Troop compartment", 2, CompartmentKind::BreacherBay));

		Register(FacilityKind::OfModule(ArmourPlating, ModuleKind::ArmourPlating, 1));
		Register(FacilityKind::OfModule(CargoRacks, ModuleKind::CargoRack, 1));
		Register(FacilityKind::OfModule(Scanner, ModuleKind::Scanner, 1));
		Register(FacilityKind::OfModule(Brig, ModuleKind::Brig, 1));

		// The marines' barracks (U.SPC-T465): berths, a drill deck and an armoury, one section, growing with the hull
		// (fifty marines on a destroyer, some two hundred on a carrier). See MarineHabitats.
		Register(cs::New<FacilityKind>(MarineHabitat, "Marine habitat", 1, 220, true,
			BillOf { { Good::Alloys, 70.0 }, { Good::Machinery, 15.0 }, { Good::Electronics, 8.0 } },
			MarineHulls(), true, cs::Nullable<CompartmentKind>(CompartmentKind::Barracks), std::nullopt, nullptr, MarineHabitats::BerthsPerScale));
		// The command ship's facilities (U.SPC-T477). Their effects are read by the systems that use them: the array by
		// SensorSuite.Of and the battle's shared picture, the suite and the computers by FleetCoordination.
		Register(cs::New<FacilityKind>(SensorArray, "Sensor array", 2, 180, false,
			BillOf { { Good::Electronics, 90.0 }, { Good::Alloys, 40.0 }, { Good::Machinery, 15.0 } }, Capitals()));
		Register(cs::New<FacilityKind>(CommsSuite, "Communications suite", 1, 60, false,
			BillOf { { Good::Electronics, 50.0 }, { Good::Alloys, 10.0 } }, Capitals()));
		Register(cs::New<FacilityKind>(TargetingComputers, "Targeting computers", 1, 40, false,
			BillOf { { Good::Electronics, 40.0 }, { Good::Machinery, 5.0 } }, Capitals()));
		// The Breacher bay (U.SPC-T466, U.SPC-T521): the breech, the loading cradle and the round magazine, two sections,
		// drawing on the reactor and manned by its own crew, so any ship with the space, power and hands may fit one. A
		// boarding fight inside the hull can be fought through it like any other space.
		{
			auto k = cs::New<FacilityKind>(BreacherBay, "Breacher bay", 2, 400, false,
				BillOf { { Good::Alloys, 160.0 }, { Good::Machinery, 60.0 }, { Good::Electronics, 20.0 } },
				BreacherHulls(), true, cs::Nullable<CompartmentKind>(CompartmentKind::BreacherBay));
			k->Requires = MarineHabitat;
			k->PowerMw = BreacherBays::PowerMw;
			k->Crew = BreacherBays::Crew;
			Register(k);
		}
		// Only the classes the owner's roster lets carry a drive when fitted (battleships, command cruisers) may fit it
		// (U.SPC-T472); a carrier has its own already, a cruiser or destroyer none.
		{
			auto k = cs::New<FacilityKind>(FtlDrive, "FTL drive", 2, 900, true,
				BillOf { { Good::Alloys, 260.0 }, { Good::Machinery, 90.0 }, { Good::Electronics, 60.0 }, { Good::HeliumThree, 20.0 } },
				cs::List<HullCategory> { HullCategory::Battleship, HullCategory::Warship });
			k->ClassFilter = [](cs::Nullable<ShipClass> c) { return c.has_value() && FtlRoster::Of(*c) == FtlAccess::Fitted; };
			Register(k);
		}

		// The spinal mounts' power plants (U.SPC-T490): a fusion plant and its capacitor banks, growing with the hull.
		{
			auto k = cs::New<FacilityKind>(SpinalPlant, "Spinal power plant", 3, 700, true,
				BillOf { { Good::Alloys, 220.0 }, { Good::Machinery, 120.0 }, { Good::Electronics, 70.0 }, { Good::HeliumThree, 15.0 } },
				cs::List<HullCategory> { HullCategory::Battleship }, true, std::nullopt, std::nullopt,
				[](cs::Ref<CombatSpec> spec, cs::Ref<Loadout> l, int n) { spec->SpinalPlants = cs::Math::Max(0, spec->SpinalPlants + n); });
			k->ClassFilter = [](cs::Nullable<ShipClass> c) { return SpinalMounts::CanCarry(c); };
			Register(k);
		}

		// The interdiction field projector (U.SPC-T507): the emitter ring, its capacitors and the reactor that feeds them,
		// one fixed fitting whatever the hull. Only a hull with the reactor load to spare may carry it (LaneInterdictors).
		{
			auto k = cs::New<FacilityKind>(FieldProjector, "Interdiction field projector", 2, LaneInterdictors::ProjectorMassTonnes, false,
				BillOf { { Good::Alloys, 180.0 }, { Good::Machinery, 90.0 }, { Good::Electronics, 110.0 }, { Good::HeliumThree, 10.0 } },
				LaneInterdictors::Hulls());
			k->ClassFilter = [](cs::Nullable<ShipClass> c) { return c.has_value() && LaneInterdictors::CanCarry(*c); };
			Register(k);
		}

		// The gate breaker's engineering teams (U.SPC-T478): structure, built into the frame, a working space boarders can
		// fight through like engineering.
		{
			auto k = cs::New<FacilityKind>(SiegeEngineering, "Siege engineering bay", 2, 650, false,
				BillOf { { Good::Alloys, 180.0 }, { Good::Machinery, 140.0 }, { Good::Electronics, 60.0 }, { Good::Munitions, 30.0 } },
				cs::List<HullCategory> { HullCategory::Battleship }, false, cs::Nullable<CompartmentKind>(CompartmentKind::Engineering));
			k->ClassFilter = [](cs::Nullable<ShipClass> c) { return c.has_value() && GateBreakers::IsClass(*c); };
			Register(k);
		}

		// A truss freighter's standard modules (U.SPC-T482): a pod is a share of its hold, a sphere a share of its tanks.
		Register(cs::New<FacilityKind>(CargoPod, "Cargo pod", 1, BulkFreighters::PodMassTonnes, false, BillOf { { Good::Alloys, 90.0 }, { Good::Machinery, 6.0 } },
			/*categories*/ std::nullopt, /*fittable:*/ true, /*compartment*/ std::nullopt, /*module*/ std::nullopt, /*effect*/ nullptr, /*berths*/ 0,
			/*cargoTonnes:*/ BulkFreighters::PodTonnes, /*holdRows:*/ BulkFreighters::PodRows, /*classes:*/ &BulkFreighters::TrussClasses()));
		Register(cs::New<FacilityKind>(FuelSphere, "Fuel sphere", 1, BulkFreighters::SphereMassTonnes, false, BillOf { { Good::Alloys, 60.0 }, { Good::Machinery, 4.0 } },
			/*categories*/ std::nullopt, /*fittable:*/ true, /*compartment*/ std::nullopt, /*module*/ std::nullopt,
			/*effect:*/ [](cs::Ref<CombatSpec> spec, cs::Ref<Loadout> l, int n) { spec->PropellantTonnes = cs::Math::Max(0.0, spec->PropellantTonnes + n * BulkFreighters::SphereTonnes); },
			/*berths*/ 0, /*cargoTonnes*/ 0, /*holdRows*/ 0, /*classes:*/ &BulkFreighters::TrussClasses()));
	}

	cs::List<cs::Ref<FacilityKind>>& FacilityKinds::All()
	{
		RunClassConstructor();
		static cs::List<cs::Ref<FacilityKind>> value;
		return value;
	}

	cs::Dictionary<cs::String, cs::Ref<FacilityKind>>& FacilityKinds::ById()
	{
		RunClassConstructor();
		static cs::Dictionary<cs::String, cs::Ref<FacilityKind>> value;
		return value;
	}

	const cs::List<HullCategory>& FacilityKinds::BreacherHulls()
	{
		RunClassConstructor();
		static const cs::List<HullCategory> value
		{
			HullCategory::Freighter, HullCategory::Warship, HullCategory::Battleship, HullCategory::Carrier, HullCategory::Support,
		};
		return value;
	}

	const cs::List<HullCategory>& FacilityKinds::MarineHulls()
	{
		RunClassConstructor();
		static const cs::List<HullCategory> value
		{
			HullCategory::Freighter, HullCategory::Warship, HullCategory::Battleship, HullCategory::Carrier, HullCategory::Station, HullCategory::Support,
		};
		return value;
	}

	const cs::List<HullCategory>& FacilityKinds::Capitals()
	{
		RunClassConstructor();
		static const cs::List<HullCategory> value { HullCategory::Warship, HullCategory::Battleship, HullCategory::Carrier };
		return value;
	}

	cs::Ref<FacilityKind> FacilityKinds::Structure(const cs::String& id, const cs::String& name, double size, CompartmentKind compartment)
	{
		return cs::New<FacilityKind>(id, name, size, 0, false, BillOf(), std::nullopt, false, cs::Nullable<CompartmentKind>(compartment));
	}

	void FacilityKinds::Register(const cs::Ref<FacilityKind>& kind)
	{
		if (kind == nullptr) throw cs::ArgumentNullException("kind");
		cs::Ref<FacilityKind> old;
		if (ById().TryGetValue(Key(kind->Id), old)) All().Remove(old);
		ById()[Key(kind->Id)] = kind;
		All().Add(kind);
	}

	const cs::List<cs::Ref<FacilityKind>>& FacilityKinds::Catalogue() { return All(); }

	cs::Ref<FacilityKind> FacilityKinds::Of(const cs::String& id)
	{
		if (id.empty()) return nullptr; // C# null (and "", which no kind has)
		cs::Ref<FacilityKind> k;
		if (ById().TryGetValue(Key(id), k)) return k;
		// A hull saved before the Assault Bay became the Breacher bay (U.SPC-T521) still names it by its old id.
		return cs::Strings::EqualsOrdinalIgnoreCase(id, OldBreacherBayId) && ById().TryGetValue(Key(BreacherBay), k) ? k : nullptr;
	}

	cs::Ref<FacilityKind> FacilityKinds::ForModule(ModuleKind module)
	{
		for (const auto& k : All()) if (k->Module == module) return k;
		return nullptr;
	}

	// ---------------------------------------------------------------- FacilitySite

	FacilitySite::FacilitySite(const cs::String& kind, const cs::String& name, double z, double y, int link, cs::Nullable<double> berths)
	{
		Kind = kind; // kind ?? throw new ArgumentNullException(nameof(kind)): a cs::String is never null
		if (!name.empty()) Name = name;
		else
		{
			auto def = FacilityKinds::Of(kind);
			Name = def ? def->Name : kind;
		}
		Z = z;
		Y = y;
		Link = link;
		Berths = berths;
	}

	double FacilitySite::BerthsOn(double scale)
	{
		if (Berths.has_value()) return cs::Math::Max(0.0, *Berths);
		auto d = Def();
		return d == nullptr ? 0 : d->Berths * (d->Scaled ? scale : 1);
	}

	cs::Ref<FacilitySite> FacilitySite::Fitted(const cs::Ref<FacilityKind>& kind) { return cs::New<FacilitySite>(kind->Id, kind->Name, 0, 0, Amidships); }

	cs::Ref<FacilityKind> FacilitySite::Def() const { return FacilityKinds::Of(Kind); }

	double FacilitySite::Size() const
	{
		auto d = Def();
		return d ? d->Size : 0;
	}

	// ---------------------------------------------------------------- FacilityLayouts

	cs::Ref<FacilitySite> FacilityLayouts::Buried(int link, const cs::String& name)
	{
		auto s = cs::New<FacilitySite>(FacilityKinds::Bridge, name, 0, 0, link);
		s->Buried = true;
		return s;
	}

	cs::Dictionary<ShipClass, cs::List<cs::Ref<FacilitySite>>>& FacilityLayouts::ByClass()
	{
		static cs::Dictionary<ShipClass, cs::List<cs::Ref<FacilitySite>>> value = []
		{
			using Sites = cs::List<cs::Ref<FacilitySite>>;
			cs::Dictionary<ShipClass, Sites> d;
			d[ShipClass::Carrier] = Sites
			{
				Site(FacilityKinds::Bow, "Bow", 0.4), Site(FacilityKinds::Hangar, "Hangar deck", 0.15),
				Site(FacilityKinds::Quarters, "Crew quarters", -0.02), Site(FacilityKinds::Midships, "Midships", -0.18),
				Site(FacilityKinds::Engineering, "Engineering", -0.4), Buried(3),
				Site(FacilityKinds::MarineHabitat, "Marine barracks", -0.02, -0.06, 2, 320),
			};
			d[ShipClass::Battleship] = Sites
			{
				Site(FacilityKinds::Bow, "Bow", 0.38), Site(FacilityKinds::Battery, "Forward battery", 0.16),
				Site(FacilityKinds::Quarters, "Crew quarters", 0.0), Site(FacilityKinds::Midships, "Midships", -0.16),
				Site(FacilityKinds::Battery, "Aft battery", -0.28), Site(FacilityKinds::Engineering, "Engineering", -0.42),
				Buried(3),
				Site(FacilityKinds::MarineHabitat, "Marine barracks", 0.0, -0.06, 2, 260),
				// Designed with its own FTL drive (U.SPC-T472): a fitting, so a yard may strip it for the space.
				Site(FacilityKinds::FtlDrive, "FTL drive", -0.36, 0, FacilitySite::Amidships),
				// A power plant for each of its two spinal mounts (U.SPC-T490), beside engineering.
				Site(FacilityKinds::SpinalPlant, "Spinal power plant", -0.3, 0.02, FacilitySite::Amidships),
				Site(FacilityKinds::SpinalPlant, "Spinal power plant", -0.3, -0.02, FacilitySite::Amidships),
			};
			d[ShipClass::Cruiser] = Sites
			{
				Site(FacilityKinds::Bow, "Bow", 0.3), Site(FacilityKinds::Quarters, "Crew quarters", 0.05),
				Site(FacilityKinds::Midships, "Midships", -0.12), Site(FacilityKinds::Engineering, "Engineering", -0.38),
				Buried(2),
				Site(FacilityKinds::MarineHabitat, "Marine barracks", 0.05, -0.05, 1, 140),
			};
			d[ShipClass::Destroyer] = Sites
			{
				Site(FacilityKinds::Bow, "Bow", 0.25), Site(FacilityKinds::Midships, "Midships", -0.05),
				Site(FacilityKinds::Engineering, "Engineering", -0.36), Buried(1),
				Site(FacilityKinds::MarineHabitat, "Marine barracks", -0.05, -0.04, 1, 50),
			};
			// The command cruiser (U.SPC-T477): the sensor array forward under its mast, the flag bridge buried at the centre with
			// the communications suite and the fire-control centre's targeting computers either side of it, and the staff's
			// quarters aft of them.
			d[ShipClass::CommandCruiser] = Sites
			{
				Site(FacilityKinds::Bow, "Bow", 0.36), Site(FacilityKinds::SensorArray, "Sensor array", 0.24),
				Site(FacilityKinds::Quarters, "Staff quarters", 0.12), Site(FacilityKinds::Midships, "Midships", -0.05),
				Site(FacilityKinds::Engineering, "Engineering", -0.36), Buried(3, "Flag bridge"),
				Site(FacilityKinds::CommsSuite, "Communications suite", 0.08),
				Site(FacilityKinds::TargetingComputers, "Fire-control centre", 0.0),
				Site(FacilityKinds::TargetingComputers, "Fire-control centre", -0.02),
				Site(FacilityKinds::MarineHabitat, "Marine barracks", 0.12, -0.05, 2, 120),
				// Designed with its own FTL drive, as a battleship is (U.SPC-T472): a fitting a yard may strip.
				Site(FacilityKinds::FtlDrive, "FTL drive", -0.3, 0, FacilitySite::Amidships),
			};
			// The frigate (U.SPC-T475): its torpedo magazine behind the bow launchers and the missile magazine under the racks
			// forward, the bridge buried at the centre off midships, and long engine rooms for its four big engines. No quarters for marines.
			d[ShipClass::Frigate] = Sites
			{
				Site(FacilityKinds::Bow, "Bow", 0.42), Site(FacilityKinds::Battery, "Torpedo magazine", 0.3),
				Site(FacilityKinds::Battery, "Missile magazine", 0.12), Site(FacilityKinds::Midships, "Midships", -0.05),
				Site(FacilityKinds::Engineering, "Engine rooms", -0.34), Buried(3),
			};
			// The corvette (U.SPC-T476): no FTL drive room; its space goes to an impound hold for what it seizes, crew
			// quarters for weeks on patrol and the big tanks round its engineering spaces.
			d[ShipClass::Corvette] = Sites
			{
				Site(FacilityKinds::Bow, "Bow", 0.38), Site(FacilityKinds::Hold, "Impound hold", 0.18),
				Site(FacilityKinds::Quarters, "Crew quarters", 0.0), Site(FacilityKinds::Midships, "Midships", -0.12),
				Site(FacilityKinds::Engineering, "Engineering", -0.34), Site(FacilityKinds::Bridge, "Bridge", 0.05, 0.05, 2),
			};
			// The gate breaker (U.SPC-T478): an armoured ram bow, the plasma batteries' magnetic bottles forward, the boarding
			// teams' barracks (the biggest marine complement afloat) off the crew quarters, the shuttle deck their two boarding
			// shuttles fly from, the engineering teams' siege bay, and the bridge buried at the centre. Its FTL drive is its own
			// (FtlAccess.Own), built into engineering like a carrier's, not a fitting a yard could strip.
			d[ShipClass::GateBreaker] = Sites
			{
				Site(FacilityKinds::Bow, "Ram bow", 0.4), Site(FacilityKinds::Battery, "Plasma batteries", 0.2),
				Site(FacilityKinds::Quarters, "Crew quarters", 0.04), Site(FacilityKinds::Midships, "Midships", -0.12),
				Site(FacilityKinds::Engineering, "Engineering", -0.4), Buried(3),
				Site(FacilityKinds::MarineHabitat, "Boarding teams' barracks", 0.04, -0.06, 2, 340),
				Site(FacilityKinds::Hangar, "Shuttle deck", -0.05, -0.05, 3),
				Site(FacilityKinds::SiegeEngineering, "Siege engineering bay", -0.26, 0, 3),
				// A Breacher bay beside the barracks (U.SPC-T521): the boarding ship fires its teams into a fort or a
				// crippled hull as well as ferrying them over in its shuttles.
				Site(FacilityKinds::BreacherBay, "Breacher bay", 0.2, -0.05, 6),
			};
			// A truss freighter (U.SPC-T482): the cockpit at the bow, the pod gantry down the spine, the pods and spheres clamped
			// along it (no space of their own: nobody walks into a pod) and the engine room astern.
			d[ShipClass::Freighter] = BulkFreighters::StockLayout();
			d[ShipClass::Miner] = Sites
			{
				Site(FacilityKinds::Bridge, "Cockpit", 0.3, 0.05), Site(FacilityKinds::Hold, "Ore hold", 0),
				Site(FacilityKinds::Engineering, "Engineering", -0.35),
			};
			// No hold: the claw gantry forward, the cockpit over it, then a long engine room for its big drive.
			d[ShipClass::OreTug] = Sites
			{
				Site(FacilityKinds::Bow, "Claw gantry", 0.35), Site(FacilityKinds::Bridge, "Cockpit", 0.15, 0.05),
				Site(FacilityKinds::Engineering, "Engineering", -0.3),
			};
			// A capture tug (U.SPC-T522): an ore tug's plan on a shorter frame.
			d[ShipClass::CaptureTug] = Sites
			{
				Site(FacilityKinds::Bow, "Claw gantry", 0.35), Site(FacilityKinds::Bridge, "Cockpit", 0.15, 0.05),
				Site(FacilityKinds::Engineering, "Engineering", -0.3),
			};
			// A work boat (U.SPC-T481): the bridge forward, the workshop that services a lane's rings, a big engine room.
			d[ShipClass::LaneTender] = Sites
			{
				Site(FacilityKinds::Bridge, "Bridge", 0.32, 0.05), Site(FacilityKinds::Midships, "Ring workshop", 0.05),
				Site(FacilityKinds::Engineering, "Engineering", -0.3),
			};
			// Multi-crew (U.SPC-T467): the flight deck, then the gun deck its turret crews work from.
			d[ShipClass::Gunship] = Sites
			{
				Site(FacilityKinds::Bridge, "Flight deck", 0.2, 0.03), Site(FacilityKinds::Battery, "Gun deck", 0),
				Site(FacilityKinds::Engineering, "Engineering", -0.3),
			};
			// A sensor mast with a drive (U.SPC-T480): the bridge, the sensor room under the mast, engineering.
			d[ShipClass::GatePicket] = Sites
			{
				Site(FacilityKinds::Bridge, "Bridge", 0.15, 0.04), Site(FacilityKinds::Midships, "Sensor room", -0.08),
				Site(FacilityKinds::Engineering, "Engineering", -0.3),
			};
			// A boarding shuttle (U.SPC-T486): the cockpit forward, then the troop bay its one team rides in (the docking collar
			// on its flank opens off it) and does not train in, and engineering astern.
			d[ShipClass::AssaultShuttle] = Sites
			{
				Site(FacilityKinds::Bridge, "Cockpit", 0.32, 0.04),
				Site(FacilityKinds::TroopCompartment, "Troop bay", 0.05, 0, FacilitySite::Keel, 24),
				Site(FacilityKinds::Engineering, "Engineering", -0.32),
			};
			return d;
		}();
		return value;
	}

	const cs::List<cs::Ref<FacilitySite>>& FacilityLayouts::Small()
	{
		static const cs::List<cs::Ref<FacilitySite>> value
		{
			Site(FacilityKinds::Bridge, "Cockpit", 0.1), Site(FacilityKinds::Engineering, "Engineering", -0.3),
		};
		return value;
	}

	const cs::List<cs::Ref<FacilitySite>>& FacilityLayouts::Platform()
	{
		static const cs::List<cs::Ref<FacilitySite>> value
		{
			Site(FacilityKinds::Battery, "Gun deck", 0.15, 0.15), Site(FacilityKinds::Bridge, "Control", 0, 0),
			Site(FacilityKinds::Engineering, "Reactor", -0.15, -0.15),
			Site(FacilityKinds::MarineHabitat, "Garrison barracks", 0, -0.08, 1, 60),
		};
		return value;
	}

	const cs::List<cs::Ref<FacilitySite>>& FacilityLayouts::Satellite()
	{
		static const cs::List<cs::Ref<FacilitySite>> value
		{
			Site(FacilityKinds::Battery, "Gun deck", 0.15, 0.15), Site(FacilityKinds::Bridge, "Control", 0, 0),
			Site(FacilityKinds::Engineering, "Reactor", -0.15, -0.15),
		};
		return value;
	}

	const cs::List<cs::Ref<FacilitySite>>& FacilityLayouts::Station()
	{
		static const cs::List<cs::Ref<FacilitySite>> value
		{
			Site(FacilityKinds::Battery, "Gun deck", 0.15, 0.15), Site(FacilityKinds::Bridge, "Control", 0, 0),
			Site(FacilityKinds::Engineering, "Reactor", -0.15, -0.15),
			Site(FacilityKinds::MarineHabitat, "Garrison barracks", 0, -0.08, 1, 180),
		};
		return value;
	}

	const cs::List<cs::Ref<FacilitySite>>& FacilityLayouts::For(ShipClass c)
	{
		auto& byClass = ByClass();
		return byClass.ContainsKey(c) ? byClass.At(c) : Small();
	}

	double FacilityLayouts::Complement(const cs::List<cs::Ref<FacilitySite>>& facilities, double scale)
	{
		double n = 0;
		for (const auto& s : facilities) n += s->BerthsOn(scale);
		return n;
	}

	int FacilityLayouts::CountOf(const cs::List<cs::Ref<FacilitySite>>& layout, const cs::String& kind)
	{
		int n = 0;
		for (const auto& s : layout)
			if (cs::Strings::EqualsOrdinalIgnoreCase(s->Kind, kind)) n++;
		return n;
	}

	double FacilityLayouts::Complement(ShipClass c, const cs::Ref<CombatSpec>& design) { return Complement(For(c), Loadout::HullScale(design)); }

	void FacilityLayouts::Register(ShipClass c, const cs::List<cs::Ref<FacilitySite>>& layout) { ByClass()[c] = layout; }

	cs::Dictionary<ShipClass, cs::HashSet<cs::String>>& FacilityLayouts::Barred()
	{
		static cs::Dictionary<ShipClass, cs::HashSet<cs::String>> value = []
		{
			cs::Dictionary<ShipClass, cs::HashSet<cs::String>> d;
			// Every kilogram of a truss freighter goes to cargo: no plate, and no racks, since its pods are its hold (U.SPC-T482).
			d[ShipClass::Freighter] = cs::HashSet<cs::String> { Key(FacilityKinds::ArmourPlating), Key(FacilityKinds::CargoRacks) };
			return d;
		}();
		return value;
	}

	void FacilityLayouts::Bar(ShipClass c, const cs::String& kind, bool barred)
	{
		auto& all = Barred();
		if (!all.ContainsKey(c)) all[c] = cs::HashSet<cs::String>();
		auto& set = all.At(c);
		if (barred) set.Add(Key(kind));
		else set.Remove(Key(kind));
	}

	bool FacilityLayouts::Bars(ShipClass c, const cs::String& kind)
	{
		auto& all = Barred();
		return !kind.empty() && all.ContainsKey(c) && all.At(c).Contains(Key(kind));
	}

	double FacilityLayouts::SpareSpace(ShipClass c)
	{
		switch (c)
		{
			case ShipClass::Carrier:
			case ShipClass::Battleship: return 6;
			case ShipClass::Cruiser:
			case ShipClass::CommandCruiser:
			// Its plate and siege bay fill most of a gate breaker (U.SPC-T478).
			case ShipClass::GateBreaker:
			case ShipClass::Freighter: return 4;
			case ShipClass::Destroyer:
			case ShipClass::Miner:
			case ShipClass::Explorer:
			case ShipClass::LaneTender: return 3;
			case ShipClass::AssaultShuttle:
			case ShipClass::Gunship:
			case ShipClass::Courier:
			case ShipClass::OreTug:
			case ShipClass::CaptureTug:
			case ShipClass::GatePicket:
			// Its magazines and engine rooms fill a frigate (U.SPC-T475).
			case ShipClass::Frigate:
			// The space a drive room would take, kept free for a corvette's patrol fit (U.SPC-T476).
			case ShipClass::Corvette: return 2;
			default: return 1;
		}
	}

	double FacilityLayouts::SpaceOf(const cs::List<cs::Ref<FacilitySite>>& layout)
	{
		double n = 0;
		for (const auto& s : layout) n += s->Size();
		return n;
	}
}
