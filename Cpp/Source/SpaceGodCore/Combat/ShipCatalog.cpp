#include "Combat/ShipCatalog.h"

#include "Ships.h"
#include "Combat/CombatSpecs.h"
#include "Combat/Construction.h"
#include "Combat/Facilities.h"
#include "Combat/FlightModels.h"
#include "Combat/Outfitting.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Economy/Economy.h"

namespace SpaceGod::Core
{
	const char* ToString(SlotCategory v)
	{
		switch (v)
		{
			case SlotCategory::Weapon: return "Weapon";
			case SlotCategory::Shield: return "Shield";
			case SlotCategory::Engine: return "Engine";
			case SlotCategory::Utility: return "Utility";
		}
		return "";
	}

	const char* ToString(CompartmentRole v)
	{
		switch (v)
		{
			case CompartmentRole::Cargo: return "Cargo";
			case CompartmentRole::Hangar: return "Hangar";
			case CompartmentRole::Crew: return "Crew";
			case CompartmentRole::Reactor: return "Reactor";
			case CompartmentRole::Command: return "Command";
			case CompartmentRole::Structure: return "Structure";
		}
		return "";
	}

	namespace
	{
		/// <summary>One authored row of the catalog: the class, how it is flown, what it is for and how it handles.</summary>
		struct Row
		{
			ShipClass Class;
			PilotControl Control;
			const char* Role;
			HullHandling Handling;
		};

		constexpr HullHandling H(double topSpeed, double turn, double roll, double boost, double boostS, double cruise, double cruiseRampS)
		{
			HullHandling h;
			h.TopSpeedKmS = topSpeed;
			h.TurnRateDegS = turn;
			h.RollRateDegS = roll;
			h.BoostSpeedMultiplier = boost;
			h.BoostDurationS = boostS;
			h.CruiseSpeedMultiplier = cruise;
			h.CruiseRampS = cruiseRampS;
			return h;
		}

		// THE TABLE (US-T193). Top speed km/s, turn and roll deg/s, boost x and seconds, cruise x and spool seconds. These
		// are the class tunings PilotTuning held before (U.SPC-T59 onward, each class's own ticket in the comments), moved
		// here so the catalog is their only home. Roll is authored per hull; each starts at the old one-and-a-half times
		// the turn, so every hull flies exactly as it did (and the Bench's digests hold) until a tuning pass moves it.
		const cs::List<Row>& Rows()
		{
			using C = ShipClass;
			using P = PilotControl;
			static const cs::List<Row> rows = {
				// Strike craft.
				// The space superiority fighter, the dedicated dogfighter: trades a hair of the Courier's top speed for a
				// turn only the interceptor beats.
				{ C::Fighter, P::FreeAim, "Space superiority fighter: the all-round dogfighter and escort", H(0.42, 165, 247.5, 2.3, 15, 9, 1.8) },
				// The interceptor (U.SPC-T484): the fastest strike craft and the tightest turn in the game, on a short boost.
				{ C::Interceptor, P::FreeAim, "Interceptor: runs fighters down and screens the bombers", H(0.48, 180, 270, 2.4, 12, 9, 1.6) },
				// The recon fighter (U.SPC-T485): an interceptor's pace on a longer boost, a little wider in the turn for its pods.
				{ C::Recon, P::FreeAim, "Recon / EW fighter: scouts, jams and paints capitals for the bombers", H(0.46, 160, 240, 2.4, 16, 9, 1.6) },
				// The heavy fighter (U.SPC-T467): a light fighter's pace less a little, stiffer in the turn, a longer boost.
				{ C::Gunship, P::DirectNose, "Heavy fighter: an armoured gunship that brawls with corvettes", H(0.4, 115, 172.5, 2.0, 20, 9, 2.2) },
				// The bomber: slow, steady, a short boost to get clear.
				{ C::Bomber, P::DirectNose, "Bomber: torpedoes for capital ships whose shields are down", H(0.3, 85, 127.5, 1.6, 12, 8, 2.4) },
				// The boarding shuttle (U.SPC-T486): slower and heavier on the stick than a bomber, built to come alongside gently.
				{ C::AssaultShuttle, P::Helm, "Assault shuttle: ferries a marine team aboard a disabled hull", H(0.24, 60, 90, 1.4, 10, 8, 2.6) },
				// The Courier (U.SPC-T444): a blockade runner, not a dogfighter. The highest top speed and the longest boost
				// in the game and the fastest cruise spool, on a turn wider than any strike craft's.
				{ C::Courier, P::FreeAim, "Courier: a blockade runner, the fastest hull in the game", H(0.52, 120, 180, 2.4, 45, 10, 1.2) },

				// Working hulls.
				// A truss freighter (U.SPC-T482): a weak drive for its mass, so its cruise drive takes twice a hauler's time to spool.
				{ C::Freighter, P::Hauler, "Freighter: the trade hauler, its hold the pods along its truss", H(0.18, 75, 112.5, 1.6, 36, 8, 6.0) },
				{ C::Miner, P::Hauler, "Miner: works a rock and hauls its ore", H(0.16, 70, 105, 1.6, 38, 8, 3.0) },
				// The ore tug (U.SPC-T483): flown like a hauler, a touch heavier on the stick.
				{ C::OreTug, P::Hauler, "Ore tug: pushes a captured asteroid in to the refinery", H(0.14, 60, 90, 1.5, 40, 8, 3.2) },
				// The capture tug (U.SPC-T522): an ore tug's controls on a lighter frame, handier for matching a tumbling rock.
				{ C::CaptureTug, P::Hauler, "Capture tug: a belt works' nimble claw boat for small rocks", H(0.18, 65, 97.5, 1.5, 34, 8, 3.0) },
				// The deep-space explorer (U.SPC-T419): handier than a hauler, nothing like a fighter.
				{ C::Explorer, P::Hauler, "Explorer: leaves the gates under its own FTL to chart new stars", H(0.22, 60, 90, 1.6, 26, 8, 3.0) },
				// The lane tender (U.SPC-T481): a work boat, steadier than an explorer and slower to spool.
				{ C::LaneTender, P::Hauler, "Lane tender: keeps a hyperlane's rings powered and aligned", H(0.18, 55, 82.5, 1.5, 30, 7, 3.5) },
				// The gate picket (U.SPC-T480): a small hull, flown nose-first like a heavy fighter but no dogfighter.
				{ C::GatePicket, P::DirectNose, "Gate picket: early warning at a held jump gate", H(0.3, 80, 120, 1.8, 22, 8, 2.6) },

				// Warships. Still weighty -- you're captaining a warship, not dogfighting in it -- but sharper than their
				// AI-battle turn rates so they're not sluggish to the point of being unplayable. Their huge pitch/yaw inertia
				// is what sells the weight. Cruise ramps take longest here.
				// The corvette (U.SPC-T476): a patrol hull, handier than a destroyer, for running down a smuggler in its system.
				{ C::Corvette, P::Helm, "Corvette: system police, hails and scans traffic on its beat", H(0.3, 34, 51, 1.5, 26, 7.5, 3.2) },
				// The frigate (U.SPC-T475): the fastest warship hull, quick to flip end for end, with a long, hard boost for its runs.
				{ C::Frigate, P::Helm, "Frigate: an alpha-striking missile and torpedo boat", H(0.38, 40, 60, 1.6, 30, 8, 3.0) },
				// Destroyer stays the nose-forward skirmisher (see Battle.BroadsideFacing).
				{ C::Destroyer, P::Helm, "Destroyer: the nose-forward skirmisher and screen", H(0.24, 28, 42, 1.5, 25, 7, 3.5) },
				{ C::Cruiser, P::Helm, "Cruiser: the line's broadside workhorse", H(0.14, 16, 24, 1.4, 28, 6, 4.0) },
				// The command cruiser (U.SPC-T477): a battlecruiser, between the cruiser and the battleship at the helm.
				{ C::CommandCruiser, P::Helm, "Command cruiser: coordinates every ship of its side in reach", H(0.12, 14, 21, 1.35, 30, 5.5, 4.5) },
				{ C::Battleship, P::Helm, "Battleship: spinal guns and the heaviest broadside", H(0.09, 10, 15, 1.3, 32, 5, 4.5) },
				{ C::Carrier, P::Helm, "Carrier: launches fighter, bomber and recon squadrons", H(0.07, 8, 12, 1.3, 30, 5, 5.0) },
				// The gate breaker (U.SPC-T478): a battleship's helm under more plate, slower and stiffer still.
				{ C::GateBreaker, P::Helm, "Gate breaker: the siege capital that takes a jump gate", H(0.08, 9, 13.5, 1.25, 30, 4.5, 4.5) },
			};
			return rows;
		}

		const Row* Find(ShipClass c)
		{
			for (const auto& r : Rows())
				if (r.Class == c) return &r;
			return nullptr;
		}
	}

	bool EquipmentSlot::Accepts(ModuleKind m) const
	{
		const auto& spec = ModuleSpec::Of(m);
		if (spec == nullptr) return false;
		if (Category == SlotCategory::Utility) return !spec->Mount.has_value();
		return Mount.has_value() && ModuleSpec::Fits(m, *Mount, Size, Spinal);
	}

	CompartmentRole CatalogCompartment::RoleOf(const cs::String& id)
	{
		if (id == FacilityKinds::Hold || id == FacilityKinds::CargoPod || id == FacilityKinds::FuelSphere || id == FacilityKinds::CargoRacks)
			return CompartmentRole::Cargo;
		if (id == FacilityKinds::Hangar) return CompartmentRole::Hangar;
		if (id == FacilityKinds::Quarters || id == FacilityKinds::MarineHabitat || id == FacilityKinds::Brig || id == FacilityKinds::TroopCompartment)
			return CompartmentRole::Crew;
		if (id == FacilityKinds::Engineering || id == FacilityKinds::SpinalPlant || id == FacilityKinds::FtlDrive || id == FacilityKinds::FieldProjector)
			return CompartmentRole::Reactor;
		if (id == FacilityKinds::Bridge || id == FacilityKinds::SensorArray || id == FacilityKinds::CommsSuite || id == FacilityKinds::TargetingComputers
			|| id == FacilityKinds::Scanner)
			return CompartmentRole::Command;
		return CompartmentRole::Structure;
	}

	CatalogDesign CatalogDesign::Of(const cs::Ref<CombatSpec>& spec)
	{
		CatalogDesign d;
		if (spec == nullptr) return d;
		auto hull = HullDynamics::Of(spec);
		d.Name = spec->DesignName;
		d.LengthKm = spec->LengthKm;
		d.MassKt = spec->MassKt;
		d.AccelKmS2 = hull.MainAccel;
		d.BrakeKmS2 = hull.RetroAccel;
		d.StrafeKmS2 = hull.LateralAccel;
		d.BattleAccelKmS2 = spec->Accel;
		d.BattleBrakeKmS2 = spec->Decel;
		d.Hull = spec->Hull;
		d.Shield = spec->Shield;
		d.Armor = spec->Armor;
		return d;
	}

	int ShipCatalogEntry::SlotCount(SlotCategory c) const
	{
		int n = 0;
		for (const auto& s : Slots)
			if (s.Category == c) n++;
		return n;
	}

	int ShipCatalogEntry::CompartmentCount(CompartmentRole r) const
	{
		int n = 0;
		for (const auto& c : Compartments)
			if (c.Role == r) n++;
		return n;
	}

	bool ShipCatalog::TryHandling(ShipClass c, HullHandling& handling)
	{
		const Row* r = Find(c);
		if (r == nullptr) return false;
		handling = r->Handling;
		return true;
	}

	PilotControl ShipCatalog::ControlOf(ShipClass c)
	{
		const Row* r = Find(c);
		return r != nullptr ? r->Control : PilotControl::Helm;
	}

	const cs::List<ShipClass>& ShipCatalog::Classes()
	{
		static const cs::List<ShipClass> classes = []
		{
			cs::List<ShipClass> l;
			for (const auto& r : Rows()) l.Add(r.Class);
			return l;
		}();
		return classes;
	}

	const cs::List<cs::Ref<ShipCatalogEntry>>& ShipCatalog::Entries()
	{
		static const cs::List<cs::Ref<ShipCatalogEntry>> entries = []
		{
			cs::List<cs::Ref<ShipCatalogEntry>> l;
			for (const auto& r : Rows()) l.Add(Build(r.Class));
			return l;
		}();
		return entries;
	}

	cs::Ref<ShipCatalogEntry> ShipCatalog::Of(ShipClass c)
	{
		for (const auto& e : Entries())
			if (e->Class == c) return e;
		return nullptr;
	}

	static EquipmentSlot SlotOf(const cs::Ref<OutfitSlot>& o)
	{
		EquipmentSlot s;
		s.Category = o->Type == MountType::Shield ? SlotCategory::Shield : o->Type == MountType::Engine ? SlotCategory::Engine : SlotCategory::Weapon;
		s.Mount = o->Type;
		s.Size = o->Size;
		s.Spinal = o->Spinal || o->Type == MountType::Spinal;
		s.LayoutIndex = o->LayoutIndex;
		s.Stock = o->Stock;
		s.Name = o->Name();
		return s;
	}

	cs::Ref<ShipCatalogEntry> ShipCatalog::Build(ShipClass c)
	{
		const Row* r = Find(c);
		auto e = cs::New<ShipCatalogEntry>();
		e->Class = c;
		auto spec = ShipClassSpec::Of(c);
		e->Name = spec != nullptr ? spec->DisplayName : cs::String(ToString(c));
		e->Role = r != nullptr ? cs::String(r->Role) : cs::String();
		if (r != nullptr)
		{
			e->Handling = r->Handling;
			e->Control = r->Control;
		}
		else
		{
			auto t = PilotTuning::Of(c);
			e->Handling = H(t.MaxSpeed, t.TurnRateDegS, t.RollRateDegS(), t.BoostSpeedMultiplier, t.BoostDurationS, t.CruiseSpeedMultiplier, t.CruiseRampS);
			e->Control = PilotControl::Helm;
		}
		e->Family = FlightModelRegistry::Default()->FamilyOf(c);
		e->Category = HullFrames::CategoryOf(c);
		e->SmallCraft = Loadout::IsSmallCraft(c);

		const auto& designs = CombatSpec::DesignsOf(c);
		const auto& lead = CombatSpec::Of(c);
		e->Designs.Add(CatalogDesign::Of(lead));
		for (const auto& d : designs)
			if (d != lead) e->Designs.Add(CatalogDesign::Of(d));
		if (lead == nullptr) return e;

		auto loadout = cs::New<Loadout>(lead, c, Economy::CapacityOf(c));
		for (const auto& o : loadout->Slots) e->Slots.Add(SlotOf(o));
		double spare = loadout->FreeSpace();
		for (int i = 0; i + 1 <= spare + 1e-9; i++)
		{
			EquipmentSlot s;
			s.Category = SlotCategory::Utility;
			s.Size = static_cast<HardpointSize>(0);
			s.Name = "Utility section";
			e->Slots.Add(s);
		}
		for (const auto& site : loadout->StockFacilities)
		{
			CatalogCompartment k;
			k.Kind = site->Kind;
			auto def = site->Def();
			k.Name = !site->Name.empty() ? site->Name : def != nullptr ? def->Name : site->Kind;
			k.Size = site->Size();
			k.Role = CatalogCompartment::RoleOf(site->Kind);
			e->Compartments.Add(k);
		}
		e->CargoTonnes = loadout->CapacityTonnes();
		e->HangarCraft = lead->HangarCraft() + lead->Shuttles;
		e->Crew = lead->Crew;
		e->Marines = lead->Marines;
		e->ReactorMw = loadout->ReactorMw();
		e->InternalSpace = loadout->InternalSpace;
		e->SpareSpace = spare;
		return e;
	}
}
