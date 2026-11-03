// Port of Assets/UnitySpace/Core/Combat/Construction.cs (see docs/MIGRATION.md).

#include "Combat/Construction.h"

#include "Ships.h"
#include "Combat/Boarding.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Facilities.h"
#include "Combat/FireCones.h"
#include "Combat/Outfitting.h"
#include "Combat/War.h"
#include "Economy/CargoHold.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"

namespace SpaceGod::Core
{
	const char* ToString(HullCategory v)
	{
		switch (v)
		{
			case HullCategory::Fighter: return "Fighter";
			case HullCategory::SmallCraft: return "SmallCraft";
			case HullCategory::Freighter: return "Freighter";
			case HullCategory::Warship: return "Warship";
			case HullCategory::Battleship: return "Battleship";
			case HullCategory::Carrier: return "Carrier";
			case HullCategory::Station: return "Station";
			case HullCategory::Satellite: return "Satellite";
			case HullCategory::Support: return "Support";
		}
		return "";
	}

	const char* ToString(HardpointSize v)
	{
		switch (v)
		{
			case HardpointSize::Small: return "Small";
			case HardpointSize::Medium: return "Medium";
			case HardpointSize::Large: return "Large";
		}
		return "";
	}

	const char* ToString(ConstructionOutcome v)
	{
		switch (v)
		{
			case ConstructionOutcome::Ok: return "Ok";
			case ConstructionOutcome::NoSuchSocket: return "NoSuchSocket";
			case ConstructionOutcome::WontFit: return "WontFit";
			case ConstructionOutcome::NoRoom: return "NoRoom";
			case ConstructionOutcome::NoPower: return "NoPower";
			case ConstructionOutcome::NoCrew: return "NoCrew";
		}
		return "";
	}

	// ---------------------------------------------------------------- Hardpoint

	Hardpoint::Hardpoint(int slotIndex, const cs::Ref<OutfitSlot>& slot, const cs::Ref<SubsystemDef>& def, cs::String socket)
	{
		SlotIndex = slotIndex;
		LayoutIndex = slot->LayoutIndex;
		Type = slot->Type;
		Size = slot->Size;
		Stock = slot->Stock;
		BearsOnBow = slot->BearsOnBow;
		Spinal = slot->Spinal;
		Socket = socket;
		if (def != nullptr)
		{
			X = def->X;
			Y = def->Y;
			Z = def->Z;
			FacesAft = def->FacesAft;
			ConeAxis = def->ConeAxis;
			ConeHalfDeg = def->ConeHalfDeg;
		}
	}

	bool Hardpoint::IsWeaponMount() const { return OutfitSlot::IsWeaponType(Type); }

	cs::String Hardpoint::ArtKey(const cs::Ref<Hardpoint>& h, cs::Nullable<ModuleKind> fitted)
	{
		return (fitted.has_value() ? cs::Str(*fitted) : cs::Cat(h->Type, "Mount")) + "/" + cs::Str(h->Size);
	}

	// ---------------------------------------------------------------- HullFrame

	HullFrame::HullFrame(cs::String id, cs::String name, HullCategory category, cs::Nullable<ShipClass> cls, cs::Ref<CombatSpec> design, double spareSpace,
		bool smallCraft, double buildPoints, double capacityTonnes, cs::Nullable<cs::List<cs::Ref<FacilitySite>>> facilities)
	{
		Id = id; // `id ?? throw new ArgumentNullException(nameof(id))`: a cs::String is never null
		Name = name;
		Category = category;
		Class = cls;
		if (design == nullptr) throw cs::ArgumentNullException("design");
		Design = design;
		SpareSpace = cs::Math::Max(0.0, spareSpace);
		Facilities = facilities.has_value() ? *facilities : (cls.has_value() ? FacilityLayouts::For(*cls) : FacilityLayouts::Platform());
		SmallCraft = smallCraft;
		BuildPoints = buildPoints;
		CapacityTonnes = capacityTonnes;
	}

	bool HullFrame::Static() const { return Design->MaxSpeed <= 0; }

	cs::Ref<Loadout> HullFrame::NewLoadout() const
	{
		return cs::New<SpaceGod::Core::Loadout>(Design, Class, Category, CapacityTonnes, SmallCraft, Facilities, SpareSpace);
	}

	double HullFrame::InternalSpace() const { return FacilityLayouts::SpaceOf(Facilities) + SpareSpace; }

	bool HullFrame::CanCarry(const cs::Ref<FacilityKind>& kind) const
	{
		return kind != nullptr && kind->Fittable && kind->AllowedOn(Category, Class) && kind->Size <= SpareSpace + 1e-9;
	}

	const cs::List<cs::Ref<Hardpoint>>& HullFrame::Hardpoints() const
	{
		if (!hardpoints.has_value()) hardpoints = MakeHardpoints();
		return *hardpoints;
	}

	cs::List<cs::Ref<Hardpoint>> HullFrame::MakeHardpoints() const
	{
		auto l = NewLoadout();
		cs::List<cs::Ref<Hardpoint>> list;
		cs::Dictionary<MountType, int> counts;
		for (int i = 0; i < l->Slots.Count(); i++)
		{
			// Each hardpoint's type and size are its loadout slot's (Loadout.MountSize, U.SPC-T508).
			const auto& s = l->Slots[i];
			int n = 0;
			counts.TryGetValue(s->Type, n);
			counts[s->Type] = n + 1;
			list.Add(cs::New<Hardpoint>(i, s, Design->Layout[s->LayoutIndex], cs::Cat(cs::Strings::ToLowerInvariant(cs::Str(s->Type)), "-", n)));
		}
		return list;
	}

	// ---------------------------------------------------------------- HullFrames

	cs::List<cs::Ref<HullFrame>>& HullFrames::All()
	{
		static cs::List<cs::Ref<HullFrame>> all;
		return all;
	}

	cs::Dictionary<cs::String, cs::Ref<HullFrame>>& HullFrames::ById()
	{
		static cs::Dictionary<cs::String, cs::Ref<HullFrame>> byId;
		return byId;
	}

	void HullFrames::StaticConstructor()
	{
		static bool constructed = false;
		if (constructed) return;
		constructed = true;

		// Enum.GetValues(typeof(ShipClass)): every member, by value.
		static const ShipClass classes[] = {
			ShipClass::Carrier, ShipClass::Battleship, ShipClass::Cruiser, ShipClass::Destroyer, ShipClass::Freighter, ShipClass::Miner,
			ShipClass::Courier, ShipClass::Fighter, ShipClass::Explorer, ShipClass::Gunship, ShipClass::OreTug, ShipClass::Interceptor,
			ShipClass::Bomber, ShipClass::LaneTender, ShipClass::GatePicket, ShipClass::CommandCruiser, ShipClass::Frigate, ShipClass::Corvette,
			ShipClass::Recon, ShipClass::AssaultShuttle, ShipClass::GateBreaker, ShipClass::CaptureTug,
		};
		for (ShipClass c : classes)
			for (const auto& design : CombatSpec::DesignsOf(c))
				Register(cs::New<HullFrame>(cs::Cat("ship/", c, "/", design->DesignName), design->DesignName, CategoryOf(c), c, design,
					FacilityLayouts::SpareSpace(c), Loadout::IsSmallCraft(c), Economy::HullCost(c), Economy::CapacityOf(c)));

		Register(cs::New<HullFrame>("station/defence-platform", CombatSpec::Platform()->DesignName, HullCategory::Station, std::nullopt, CombatSpec::Platform(),
			2, false, War::PlatformPrice / PricePerBuildPoint));
		Register(cs::New<HullFrame>("station/orbital-station", OrbitalStation()->DesignName, HullCategory::Station, std::nullopt, OrbitalStation(), 8, false, 60000, 50000,
			FacilityLayouts::Station()));
		Register(cs::New<HullFrame>("satellite/weapons", WeaponsSatellite()->DesignName, HullCategory::Satellite, std::nullopt, WeaponsSatellite(), 0, false, 900, 0,
			FacilityLayouts::Satellite()));
		Register(cs::New<HullFrame>("satellite/sensor", SensorSatellite()->DesignName, HullCategory::Satellite, std::nullopt, SensorSatellite(), 1, false, 400, 0,
			FacilityLayouts::Satellite()));
	}

	void HullFrames::Register(const cs::Ref<HullFrame>& frame)
	{
		StaticConstructor();
		if (frame == nullptr) throw cs::ArgumentNullException("frame");
		auto key = cs::Strings::ToUpperInvariant(frame->Id);
		cs::Ref<HullFrame> old;
		if (ById().TryGetValue(key, old)) All().Remove(old);
		ById()[key] = frame;
		All().Add(frame);
	}

	const cs::List<cs::Ref<HullFrame>>& HullFrames::Catalogue()
	{
		StaticConstructor();
		return All();
	}

	cs::Ref<HullFrame> HullFrames::Of(const cs::String& id)
	{
		StaticConstructor();
		cs::Ref<HullFrame> f;
		return ById().TryGetValue(cs::Strings::ToUpperInvariant(id), f) ? f : nullptr;
	}

	cs::List<cs::Ref<HullFrame>> HullFrames::InCategory(HullCategory c)
	{
		StaticConstructor();
		cs::List<cs::Ref<HullFrame>> r;
		for (const auto& f : All()) if (f->Category == c) r.Add(f);
		return r;
	}

	cs::Ref<HullFrame> HullFrames::ForClass(ShipClass c)
	{
		StaticConstructor();
		auto design = CombatSpec::Of(c);
		for (const auto& f : All()) if (f->Class == c && f->Design == design) return f;
		return nullptr;
	}

	HullCategory HullFrames::CategoryOf(ShipClass c)
	{
		switch (c)
		{
			case ShipClass::Fighter:
			case ShipClass::Gunship: return HullCategory::Fighter;
			case ShipClass::Interceptor:
			case ShipClass::Bomber:
			case ShipClass::Recon: return HullCategory::Fighter;
			case ShipClass::Courier:
			case ShipClass::AssaultShuttle:
			case ShipClass::GatePicket: return HullCategory::SmallCraft;
			case ShipClass::Freighter:
			case ShipClass::Miner:
			case ShipClass::OreTug:
			case ShipClass::CaptureTug: return HullCategory::Freighter;
			case ShipClass::Battleship:
			// A siege capital, as heavy as a battleship (U.SPC-T478).
			case ShipClass::GateBreaker: return HullCategory::Battleship;
			case ShipClass::Carrier: return HullCategory::Carrier;
			case ShipClass::LaneTender: return HullCategory::Support;
			default: return HullCategory::Warship;
		}
	}

	// ---------------------------------------------------------------- stations and satellites

	const cs::Ref<CombatSpec>& HullFrames::OrbitalStation()
	{
		static const cs::Ref<CombatSpec> field = []
		{
			auto s = cs::New<CombatSpec>();
			s->DesignName = "Orbital station";
			s->LengthKm = 2.0;
			s->Hull = 34000;
			s->Shield = 14000;
			s->ShieldRegen = 70;
			s->Armor = 9;
			s->MaxSpeed = 0;
			s->Accel = 0;
			s->Decel = 0;
			s->TurnRateDegS = 0;
			// Its garrison is what its barracks quarter (U.SPC-T465).
			s->Crew = 1400;
			s->Marines = FacilityLayouts::Complement(FacilityLayouts::Station(), 1);
			s->DryMassTonnes = 400000;
			s->Layout = cs::List<cs::Ref<SubsystemDef>>{
				cs::New<SubsystemDef>(SubsystemKind::HeavyLaser, 0.35, 0.05, 0, false, FireCones::Starboard, 90),
				cs::New<SubsystemDef>(SubsystemKind::HeavyLaser, -0.35, 0.05, 0, false, FireCones::Port, 90),
				cs::New<SubsystemDef>(SubsystemKind::HeavyLaser, 0, 0.05, 0.35, false, FireCones::Bow, 90),
				cs::New<SubsystemDef>(SubsystemKind::HeavyLaser, 0, 0.05, -0.35, true, FireCones::Stern, 90),
				cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0.25, 0.08, 0.25),
				cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, -0.25, 0.08, -0.25),
				cs::New<SubsystemDef>(SubsystemKind::TorpedoTube, 0, 0, 0.42, false, FireCones::Bow, 45),
				cs::New<SubsystemDef>(SubsystemKind::TorpedoTube, 0, 0, -0.42, true, FireCones::Stern, 45),
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.3, 0.1, 0.3),
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.3, 0.1, 0.3),
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.3, 0.1, -0.3),
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.3, 0.1, -0.3),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.2, 0.2),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.2, 0.2, 0),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.2, -0.2),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.2, 0.2, 0),
				cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.25, 0),
				cs::New<SubsystemDef>(SubsystemKind::Hangar, 0, -0.1, 0),
				cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, 0),
			};
			return s;
		}();
		return field;
	}

	const cs::Ref<CombatSpec>& HullFrames::WeaponsSatellite()
	{
		static const cs::Ref<CombatSpec> field = []
		{
			auto s = cs::New<CombatSpec>();
			s->DesignName = "Weapons satellite";
			s->LengthKm = 0.04;
			s->Hull = 900;
			s->Shield = 600;
			s->ShieldRegen = 8;
			s->Armor = 2;
			s->MaxSpeed = 0;
			s->Accel = 0;
			s->Decel = 0;
			s->TurnRateDegS = 0;
			s->Crew = 0;
			s->Marines = 0;
			s->DryMassTonnes = 120;
			s->Layout = cs::List<cs::Ref<SubsystemDef>>{
				cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0, 0.2, 0.1),
				cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0, -0.2, 0),
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.25, 0, -0.1),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0, 0),
				cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.3),
			};
			return s;
		}();
		return field;
	}

	const cs::Ref<CombatSpec>& HullFrames::SensorSatellite()
	{
		static const cs::Ref<CombatSpec> field = []
		{
			auto s = cs::New<CombatSpec>();
			s->DesignName = "Sensor satellite";
			s->LengthKm = 0.03;
			s->Hull = 500;
			s->Shield = 300;
			s->ShieldRegen = 5;
			s->Armor = 1;
			s->MaxSpeed = 0;
			s->Accel = 0;
			s->Decel = 0;
			s->TurnRateDegS = 0;
			s->Crew = 0;
			s->Marines = 0;
			s->DryMassTonnes = 60;
			s->Layout = cs::List<cs::Ref<SubsystemDef>>{
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0, 0.25, 0),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0, 0),
				cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.3),
			};
			return s;
		}();
		return field;
	}

	// ---------------------------------------------------------------- ConstructionPlan

	ConstructionPlan::ConstructionPlan(cs::Ref<HullFrame> frame)
	{
		if (frame == nullptr) throw cs::ArgumentNullException("frame");
		Frame = frame;
		Loadout = frame->NewLoadout();
	}

	const cs::List<cs::Ref<Hardpoint>>& ConstructionPlan::Hardpoints() const { return Frame->Hardpoints(); }

	const cs::List<cs::Ref<FacilitySite>>& ConstructionPlan::Facilities() const { return Loadout->Facilities; }

	double ConstructionPlan::FreeSpace() const { return Loadout->FreeSpace(); }

	cs::Nullable<ModuleKind> ConstructionPlan::FittedAt(int socket) const
	{
		return socket >= 0 && socket < Loadout->Fitted.Count() ? Loadout->Fitted[socket] : cs::Nullable<ModuleKind>();
	}

	ConstructionOutcome ConstructionPlan::Fit(int socket, cs::Nullable<ModuleKind> module)
	{
		if (socket < 0 || socket >= Loadout->Slots.Count()) return ConstructionOutcome::NoSuchSocket;
		if (!Outfitting::CanFit(Loadout, socket, module)) return ConstructionOutcome::WontFit;
		Loadout->Fitted[socket] = module;
		return ConstructionOutcome::Ok;
	}

	ConstructionOutcome ConstructionPlan::FitFacility(const cs::Ref<FacilityKind>& kind)
	{
		auto can = Loadout->CanAdd(kind);
		if (can == OutfitOutcome::NoRoom) return ConstructionOutcome::NoRoom;
		if (can == OutfitOutcome::NoPower) return ConstructionOutcome::NoPower;
		if (can == OutfitOutcome::NoCrew) return ConstructionOutcome::NoCrew;
		if (can != OutfitOutcome::Ok) return ConstructionOutcome::WontFit;
		Loadout->Facilities.Add(FacilitySite::Fitted(kind));
		return ConstructionOutcome::Ok;
	}

	ConstructionOutcome ConstructionPlan::RemoveFacility(int index)
	{
		if (index < 0 || index >= Loadout->Facilities.Count()) return ConstructionOutcome::NoSuchSocket;
		if (!Loadout->CanRemove(index)) return ConstructionOutcome::WontFit;
		Loadout->Facilities.RemoveAt(index);
		return ConstructionOutcome::Ok;
	}

	int ConstructionPlan::FitFirst(ModuleKind module)
	{
		if (auto kind = FacilityKinds::ForModule(module))
			return FitFacility(kind) == ConstructionOutcome::Ok ? Loadout->Facilities.Count() - 1 : -1;
		for (int i = 0; i < Loadout->Slots.Count(); i++)
			if (Loadout->Fitted[i] == Loadout->Slots[i]->Stock && Loadout->Fitted[i] != module && Outfitting::CanFit(Loadout, i, module))
			{
				Loadout->Fitted[i] = module;
				return i;
			}
		return -1;
	}

	cs::Ref<CombatSpec> ConstructionPlan::Build() const { return Loadout->Build(); }

	cs::List<ConstructionPlan::PartChange> ConstructionPlan::Changes() const
	{
		cs::List<PartChange> r;
		for (int i = 0; i < Loadout->Slots.Count(); i++)
		{
			auto stock = Loadout->Slots[i]->Stock;
			auto now = Loadout->Fitted[i];
			if (now == stock) continue;
			// A spinal mount's gun is built to the hull's length (U.SPC-T490).
			if (now.has_value()) r.Add(PartChange{ Loadout->PartAt(i, *now), 1 });
			if (stock.has_value()) r.Add(PartChange{ Loadout->PartAt(i, *stock), -1 });
		}
		for (const auto& f : Loadout->AddedFacilities()) if (f->Def() != nullptr) r.Add(PartChange{ f->Def(), 1 });
		for (const auto& f : Loadout->RemovedFacilities()) if (f->Def() != nullptr) r.Add(PartChange{ f->Def(), -1 });
		return r;
	}

	cs::Dictionary<Good, double> ConstructionPlan::Bill() const
	{
		cs::Dictionary<Good, double> bill;
		bill[Good::Alloys] = Frame->BuildPoints;
		double scale = Loadout->Scale();
		for (const auto& [part, sign] : Changes())
			for (int i = 0; i < part->PartBill().Count(); i++)
			{
				auto g = part->PartBill()[i].first;
				double t = 0;
				bill.TryGetValue(g, t);
				bill[g] = t + sign * part->Tonnes(i, scale);
			}
		auto goods = bill.Keys();
		for (auto g : goods) if (bill[g] < 0) bill[g] = 0;
		return bill;
	}

	double ConstructionPlan::BasePrice() const
	{
		double price = Frame->BuildPoints * HullFrames::PricePerBuildPoint;
		double scale = Loadout->Scale();
		for (const auto& [part, sign] : Changes())
			price += sign > 0 ? Worth(part, scale) * (1 + Outfitting::LabourShare) : -Worth(part, scale);
		return cs::Math::Max(0.0, price);
	}

	double ConstructionPlan::Worth(const cs::Ref<IPartSpec>& spec, double scale)
	{
		double w = 0;
		for (int i = 0; i < spec->PartBill().Count(); i++) w += spec->Tonnes(i, scale) * GoodInfo::Of(spec->PartBill()[i].first)->BasePrice;
		return w;
	}

	cs::Ref<Interior> ConstructionPlan::InteriorOf(const cs::Ref<CombatSpec>& spec) const
	{
		return Interior::For(Loadout->Facilities, spec->Crew, spec->Marines);
	}

	cs::Ref<ShipCombat> ConstructionPlan::BuildCombat() const
	{
		auto spec = Build();
		return cs::New<ShipCombat>(spec, Frame->Class, InteriorOf(spec));
	}

	bool ConstructionPlan::Apply(const cs::Ref<Ship>& ship) const
	{
		if (ship == nullptr || !Frame->Class.has_value() || ship->Spec == nullptr || ship->Spec->Class != Frame->Class) return false;
		ship->Loadout = Loadout->Copy();
		auto spec = ship->Loadout->Build();
		ship->Combat = cs::New<ShipCombat>(spec, Frame->Class, InteriorOf(spec));
		if (Frame->CapacityTonnes > 0) ship->CapacityTonnes = ship->Loadout->CapacityTonnes();
		if (ship->Loadout->ExtraHoldRows() != 0)
			if (auto hold = ship->ResizedHold(ship->Loadout->ExtraHoldRows())) ship->SetHold(hold);
		return true;
	}
}
