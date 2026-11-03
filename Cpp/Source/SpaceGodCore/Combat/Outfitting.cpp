// Port of Assets/UnitySpace/Core/Combat/Outfitting.cs (see docs/MIGRATION.md).

#include "Combat/Outfitting.h"

#include "Ships.h"
#include "Combat/Boarding.h"
#include "Combat/BreacherBays.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Construction.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/Facilities.h"
#include "Combat/FireCones.h"
#include "Combat/FleetPatterns.h"
#include "Combat/SpinalMounts.h"
#include "Combat/TachyonLances.h"
#include "Economy/CargoHold.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Sensors/Sensors.h"

namespace SpaceGod::Core
{
	const char* ToString(ModuleKind v)
	{
		switch (v)
		{
			case ModuleKind::LaserTurret: return "LaserTurret";
			case ModuleKind::HeavyLaser: return "HeavyLaser";
			case ModuleKind::Railgun: return "Railgun";
			case ModuleKind::MissileLauncher: return "MissileLauncher";
			case ModuleKind::PointDefense: return "PointDefense";
			case ModuleKind::ShieldGenerator: return "ShieldGenerator";
			case ModuleKind::HardenedShield: return "HardenedShield";
			case ModuleKind::Engine: return "Engine";
			case ModuleKind::TorchEngine: return "TorchEngine";
			case ModuleKind::ArmourPlating: return "ArmourPlating";
			case ModuleKind::CargoRack: return "CargoRack";
			case ModuleKind::Scanner: return "Scanner";
			case ModuleKind::Brig: return "Brig";
			case ModuleKind::Autocannon: return "Autocannon";
			case ModuleKind::LightRailgun: return "LightRailgun";
			case ModuleKind::EmpLauncher: return "EmpLauncher";
			case ModuleKind::MineLayer: return "MineLayer";
			case ModuleKind::AutoTurret: return "AutoTurret";
			case ModuleKind::TorpedoTube: return "TorpedoTube";
			case ModuleKind::TachyonLance: return "TachyonLance";
			case ModuleKind::PhaserArray: return "PhaserArray";
			case ModuleKind::PlasmaCannon: return "PlasmaCannon";
			case ModuleKind::AntimatterTorpedo: return "AntimatterTorpedo";
			case ModuleKind::FlakCannon: return "FlakCannon";
			case ModuleKind::InterceptorMissile: return "InterceptorMissile";
			case ModuleKind::Coilgun: return "Coilgun";
		}
		return "";
	}

	const char* ToString(MountType v)
	{
		switch (v)
		{
			case MountType::Ballistic: return "Ballistic";
			case MountType::Missile: return "Missile";
			case MountType::Energy: return "Energy";
			case MountType::Shield: return "Shield";
			case MountType::Engine: return "Engine";
			case MountType::Spinal: return "Spinal";
		}
		return "";
	}

	const char* ToString(OutfitOutcome v)
	{
		switch (v)
		{
			case OutfitOutcome::Ok: return "Ok";
			case OutfitOutcome::NotAYard: return "NotAYard";
			case OutfitOutcome::CannotDock: return "CannotDock";
			case OutfitOutcome::Busy: return "Busy";
			case OutfitOutcome::NoSuchSlot: return "NoSuchSlot";
			case OutfitOutcome::WontFit: return "WontFit";
			case OutfitOutcome::AlreadyFitted: return "AlreadyFitted";
			case OutfitOutcome::NoStock: return "NoStock";
			case OutfitOutcome::CantAfford: return "CantAfford";
			case OutfitOutcome::HoldTooFull: return "HoldTooFull";
			case OutfitOutcome::BrigOccupied: return "BrigOccupied";
			case OutfitOutcome::YardCannotBuild: return "YardCannotBuild";
			case OutfitOutcome::NoRoom: return "NoRoom";
			case OutfitOutcome::NoPower: return "NoPower";
			case OutfitOutcome::NoCrew: return "NoCrew";
		}
		return "";
	}

	// ---------------------------------------------------------------- ModuleSpec

	ModuleSpec::ModuleSpec(ModuleKind kind, cs::String name, double mass, bool scaled, cs::Nullable<MountType> mount, HardpointSize size, bool spinal,
		double coneHalfDeg, cs::List<std::pair<Good, double>> bill)
	{
		ConeHalfDeg = coneHalfDeg;
		Kind = kind;
		Name = name;
		MassTonnes = mass;
		Scaled = scaled;
		Mount = mount;
		Size = size;
		Spinal = spinal;
		Bill = bill;
	}

	namespace
	{
		// ModuleSpec's private constants.
		constexpr MountType B = MountType::Ballistic, Mi = MountType::Missile, E = MountType::Energy, Sh = MountType::Shield, En = MountType::Engine;
		constexpr HardpointSize S = HardpointSize::Small, M = HardpointSize::Medium, L = HardpointSize::Large;
		const cs::Nullable<MountType> NoMount = std::nullopt;
	}

	const cs::List<cs::Ref<ModuleSpec>>& ModuleSpec::All()
	{
		static const cs::List<cs::Ref<ModuleSpec>> all{
			cs::New<ModuleSpec>(ModuleKind::LaserTurret, "Laser turret", 120, false, E, S, false, 110, cs::List<std::pair<Good, double>>{ { Good::Alloys, 40 }, { Good::Electronics, 12 }, { Good::Machinery, 6 } }),
			cs::New<ModuleSpec>(ModuleKind::HeavyLaser, "Heavy laser", 300, false, E, M, false, 100, cs::List<std::pair<Good, double>>{ { Good::Alloys, 90 }, { Good::Electronics, 30 }, { Good::Machinery, 14 } }),
			cs::New<ModuleSpec>(ModuleKind::Railgun, "Railgun", 420, false, B, M, true, 10, cs::List<std::pair<Good, double>>{ { Good::Alloys, 120 }, { Good::Electronics, 20 }, { Good::Machinery, 30 } }),
			// A launcher comes with its first full load: 16 rounds at the 1.5 t a round a yard rearms at.
			cs::New<ModuleSpec>(ModuleKind::MissileLauncher, "Missile launcher", 160, false, Mi, S, false, 120, cs::List<std::pair<Good, double>>{ { Good::Alloys, 50 }, { Good::Electronics, 15 }, { Good::Machinery, 10 }, { Good::Munitions, 24 } }),
			cs::New<ModuleSpec>(ModuleKind::PointDefense, "Point defence", 40, false, E, S, false, 120, cs::List<std::pair<Good, double>>{ { Good::Alloys, 15 }, { Good::Electronics, 8 }, { Good::Machinery, 3 }, { Good::Munitions, 2 } }),
			cs::New<ModuleSpec>(ModuleKind::ShieldGenerator, "Shield generator", 60, true, Sh, S, false, 180, cs::List<std::pair<Good, double>>{ { Good::Alloys, 30 }, { Good::Electronics, 20 } }),
			cs::New<ModuleSpec>(ModuleKind::HardenedShield, "Hardened shield", 90, true, Sh, S, false, 180, cs::List<std::pair<Good, double>>{ { Good::Alloys, 50 }, { Good::Electronics, 45 }, { Good::Machinery, 5 } }),
			cs::New<ModuleSpec>(ModuleKind::Engine, "Fusion drive", 150, true, En, S, false, 180, cs::List<std::pair<Good, double>>{ { Good::Alloys, 60 }, { Good::Machinery, 30 }, { Good::Electronics, 5 } }),
			cs::New<ModuleSpec>(ModuleKind::TorchEngine, "Torch drive", 190, true, En, S, false, 180, cs::List<std::pair<Good, double>>{ { Good::Alloys, 90 }, { Good::Machinery, 55 }, { Good::Electronics, 12 } }),
			cs::New<ModuleSpec>(ModuleKind::ArmourPlating, "Armour plating", 450, true, NoMount, S, false, 180, cs::List<std::pair<Good, double>>{ { Good::Alloys, 300 }, { Good::Machinery, 4 } }),
			cs::New<ModuleSpec>(ModuleKind::CargoRack, "Cargo racks", 120, true, NoMount, S, false, 180, cs::List<std::pair<Good, double>>{ { Good::Alloys, 80 }, { Good::Machinery, 10 } }),
			cs::New<ModuleSpec>(ModuleKind::Scanner, "Scanner suite", 25, false, NoMount, S, false, 180, cs::List<std::pair<Good, double>>{ { Good::Electronics, 25 }, { Good::Alloys, 10 } }),
			cs::New<ModuleSpec>(ModuleKind::Brig, "Brig", 80, false, NoMount, S, false, 180, cs::List<std::pair<Good, double>>{ { Good::Alloys, 40 }, { Good::Electronics, 6 }, { Good::Machinery, 4 } }),
			cs::New<ModuleSpec>(ModuleKind::Autocannon, "Autocannon", 20, false, B, S, false, 110, cs::List<std::pair<Good, double>>{ { Good::Alloys, 8 }, { Good::Machinery, 3 }, { Good::Munitions, 2 } }),
			cs::New<ModuleSpec>(ModuleKind::LightRailgun, "Light railgun", 35, false, B, S, false, 90, cs::List<std::pair<Good, double>>{ { Good::Alloys, 12 }, { Good::Electronics, 6 }, { Good::Machinery, 5 } }),
			cs::New<ModuleSpec>(ModuleKind::EmpLauncher, "EMP launcher", 25, false, Mi, S, false, 110, cs::List<std::pair<Good, double>>{ { Good::Alloys, 6 }, { Good::Electronics, 14 }, { Good::Munitions, 2 } }),
			cs::New<ModuleSpec>(ModuleKind::MineLayer, "Mine layer", 40, false, Mi, S, false, 180, cs::List<std::pair<Good, double>>{ { Good::Alloys, 10 }, { Good::Machinery, 4 }, { Good::Munitions, 9 } }),
			cs::New<ModuleSpec>(ModuleKind::AutoTurret, "Auto turret", 30, false, B, S, false, 120, cs::List<std::pair<Good, double>>{ { Good::Alloys, 10 }, { Good::Electronics, 9 }, { Good::Machinery, 4 }, { Good::Munitions, 2 } }),
			// A tube comes with its six torpedoes at 6 t a round.
			cs::New<ModuleSpec>(ModuleKind::TorpedoTube, "Torpedo tube", 260, false, Mi, M, true, 15, cs::List<std::pair<Good, double>>{ { Good::Alloys, 70 }, { Good::Electronics, 18 }, { Good::Machinery, 16 }, { Good::Munitions, 36 } }),
			// The tachyon lance (U.SPC-T487): its own power plant and capacitor banks, and the platinum and rare earths of
			// its emitter, some eight times a railgun's worth.
			cs::New<ModuleSpec>(ModuleKind::TachyonLance, "Tachyon lance", 950, false, E, L, true, 10, cs::List<std::pair<Good, double>>{ { Good::Alloys, 260 }, { Good::Electronics, 180 }, { Good::Machinery, 90 }, { Good::Platinum, 30 }, { Good::RareEarths, 45 } }),
			// Energy weapons (U.SPC-T488): an array is mostly emitters and their power conditioning; a plasma cannon a
			// magnetic bottle, its injectors and heavy plate round both.
			cs::New<ModuleSpec>(ModuleKind::PhaserArray, "Phaser array", 200, false, E, S, false, 110, cs::List<std::pair<Good, double>>{ { Good::Alloys, 55 }, { Good::Electronics, 40 }, { Good::Machinery, 10 } }),
			cs::New<ModuleSpec>(ModuleKind::PlasmaCannon, "Plasma cannon", 380, false, E, M, false, 90, cs::List<std::pair<Good, double>>{ { Good::Alloys, 120 }, { Good::Electronics, 30 }, { Good::Machinery, 28 } }),

			// Four antimatter rounds at 9 t a round: the containment is most of the price.
			cs::New<ModuleSpec>(ModuleKind::AntimatterTorpedo, "Antimatter torpedo", 300, false, Mi, M, true, 15, cs::List<std::pair<Good, double>>{ { Good::Alloys, 80 }, { Good::Electronics, 40 }, { Good::Machinery, 18 }, { Good::Munitions, 36 } }),
			cs::New<ModuleSpec>(ModuleKind::FlakCannon, "Flak cannon", 70, false, B, S, false, 120, cs::List<std::pair<Good, double>>{ { Good::Alloys, 25 }, { Good::Electronics, 6 }, { Good::Machinery, 8 }, { Good::Munitions, 6 } }),
			// Twenty interceptors at 0.6 t a round.
			cs::New<ModuleSpec>(ModuleKind::InterceptorMissile, "Interceptor missiles", 90, false, Mi, S, false, 120, cs::List<std::pair<Good, double>>{ { Good::Alloys, 25 }, { Good::Electronics, 16 }, { Good::Machinery, 5 }, { Good::Munitions, 12 } }),
			// A spinal coilgun (U.SPC-T490): the coils and their capacitors down a battleship's keel, a little cheaper than a
			// spinal railgun's rails. Only a spinal mount takes one.
			cs::New<ModuleSpec>(ModuleKind::Coilgun, "Coilgun", 1400, false, B, L, true, 10, cs::List<std::pair<Good, double>>{ { Good::Alloys, 380 }, { Good::Electronics, 90 }, { Good::Machinery, 110 } }),
		};
		return all;
	}

	const cs::Ref<ModuleSpec>& ModuleSpec::Of(ModuleKind k) { return All()[static_cast<int>(k)]; }

	bool ModuleSpec::YardBuilds(ModuleKind m, const cs::Ref<Settlement>& yard) { return m != ModuleKind::TachyonLance || TachyonLances::YardBuilds(yard); }

	const cs::List<cs::Ref<ModuleSpec>>& ModuleSpec::Catalogue() { return All(); }

	double ModuleSpec::Tonnes(int i, double scale) { return Bill[i].second * (Scaled ? scale : 1); }

	double ModuleSpec::Mass(double scale) { return MassTonnes * (Scaled ? scale : 1); }

	cs::Ref<FacilityKind> ModuleSpec::Facility() const { return FacilityKinds::ForModule(Kind); }

	bool ModuleSpec::Fits(ModuleKind m, MountType type, HardpointSize size, bool spinal)
	{
		// A spinal mount takes only a gun built to the hull's length, of whatever type (U.SPC-T490).
		if (type == MountType::Spinal) return SpinalMounts::Takes(m);
		const auto& spec = Of(m);
		return spec->Mount == type && spec->Size <= size && (!spec->Spinal || spinal);
	}

	bool ModuleSpec::Fits(ModuleKind m, const cs::Ref<OutfitSlot>& slot) { return slot != nullptr && Fits(m, slot->Type, slot->Size, slot->Spinal); }

	SubsystemKind ModuleSpec::SubsystemOf(ModuleKind m)
	{
		switch (m)
		{
			case ModuleKind::LaserTurret: return SubsystemKind::LaserTurret;
			case ModuleKind::HeavyLaser: return SubsystemKind::HeavyLaser;
			case ModuleKind::Railgun: return SubsystemKind::Railgun;
			case ModuleKind::MissileLauncher: return SubsystemKind::MissileLauncher;
			case ModuleKind::PointDefense: return SubsystemKind::PointDefense;
			case ModuleKind::Autocannon: return SubsystemKind::Autocannon;
			case ModuleKind::LightRailgun: return SubsystemKind::LightRailgun;
			case ModuleKind::EmpLauncher: return SubsystemKind::EmpLauncher;
			case ModuleKind::MineLayer: return SubsystemKind::MineLayer;
			case ModuleKind::AutoTurret: return SubsystemKind::AutoTurret;
			case ModuleKind::TorpedoTube: return SubsystemKind::TorpedoTube;
			case ModuleKind::TachyonLance: return SubsystemKind::TachyonLance;
			case ModuleKind::PhaserArray: return SubsystemKind::PhaserArray;
			case ModuleKind::PlasmaCannon: return SubsystemKind::PlasmaCannon;
			case ModuleKind::AntimatterTorpedo: return SubsystemKind::AntimatterTorpedo;
			case ModuleKind::FlakCannon: return SubsystemKind::FlakCannon;
			case ModuleKind::InterceptorMissile: return SubsystemKind::InterceptorMissile;
			case ModuleKind::Coilgun: return SubsystemKind::Coilgun;
			case ModuleKind::ShieldGenerator:
			case ModuleKind::HardenedShield: return SubsystemKind::ShieldGenerator;
			default: return SubsystemKind::Engine;
		}
	}

	// ---------------------------------------------------------------- OutfitSlot

	OutfitSlot::OutfitSlot(MountType type, HardpointSize size, int layoutIndex, cs::Nullable<ModuleKind> stock, bool bearsOnBow)
	{
		Type = type;
		Size = size;
		LayoutIndex = layoutIndex;
		Stock = stock;
		BearsOnBow = bearsOnBow;
		Spinal = bearsOnBow || (stock.has_value() && ModuleSpec::Of(*stock)->Spinal);
	}

	bool OutfitSlot::IsWeaponType(MountType t) { return t == MountType::Ballistic || t == MountType::Missile || t == MountType::Energy || t == MountType::Spinal; }

	cs::String OutfitSlot::Name() const
	{
		return IsWeapon() && Type != MountType::Spinal ? cs::Cat(Size, " ", cs::Strings::ToLowerInvariant(TypeName(Type)), " mount") : TypeName(Type) + " mount";
	}

	cs::String OutfitSlot::TypeName(MountType t) { return cs::Str(t); }

	// ---------------------------------------------------------------- Loadout

	Loadout::Loadout(cs::Ref<CombatSpec> design, ShipClass cls, double baseCapacityTonnes)
		: Loadout(design, cls, HullFrames::CategoryOf(cls), baseCapacityTonnes, IsSmallCraft(cls), FacilityLayouts::For(cls), FacilityLayouts::SpareSpace(cls)) { }

	Loadout::Loadout(cs::Ref<CombatSpec> design, cs::Nullable<ShipClass> cls, HullCategory category, double baseCapacityTonnes, bool smallCraft,
		const cs::List<cs::Ref<FacilitySite>>& facilities, double spareSpace)
	{
		Base = design;
		Class = cls;
		Category = category;
		BaseCapacityTonnes = baseCapacityTonnes;
		SmallCraft = smallCraft;
		for (int i = 0; i < design->Layout.Count(); i++)
		{
			const auto& d = design->Layout[i];
			auto stockOf = StockOf(d->Kind);
			if (!stockOf.has_value()) continue;
			ModuleKind stock = *stockOf;
			auto mountOf = ModuleSpec::Of(stock)->Mount;
			if (!mountOf.has_value()) continue;
			MountType type = *mountOf;
			// A spinal mount is its own type of hardpoint, whatever gun is in it, and large (U.SPC-T490).
			if (d->Spinal) Slots.Add(cs::New<OutfitSlot>(MountType::Spinal, HardpointSize::Large, i, stock, true));
			else Slots.Add(cs::New<OutfitSlot>(type, MountSize(d->Kind, stock, smallCraft, design->LengthKm), i, stock, d->Covers(design, FireCones::Bow)));
		}
		Fitted = cs::NewArray<cs::Nullable<ModuleKind>>(Slots.Count());
		for (int i = 0; i < Slots.Count(); i++) Fitted[i] = Slots[i]->Stock;
		StockFacilities = facilities; // `facilities ?? Array.Empty<FacilitySite>()`: a cs::List is never null
		Facilities.AddRange(StockFacilities);
		InternalSpace = FacilityLayouts::SpaceOf(StockFacilities) + cs::Math::Max(0.0, spareSpace);
	}

	bool Loadout::IsSmallCraft(ShipClass cls)
	{
		return cls == ShipClass::Fighter || cls == ShipClass::Interceptor || cls == ShipClass::Bomber || cls == ShipClass::Courier || cls == ShipClass::Gunship
		       || cls == ShipClass::Recon || cls == ShipClass::AssaultShuttle;
	}

	cs::Nullable<MountType> Loadout::MountOf(SubsystemKind k)
	{
		auto m = StockOf(k);
		return m.has_value() ? ModuleSpec::Of(*m)->Mount : cs::Nullable<MountType>();
	}

	bool Loadout::IsScreen(SubsystemKind k) { return k == SubsystemKind::PointDefense || k == SubsystemKind::FlakCannon || k == SubsystemKind::InterceptorMissile; }

	HardpointSize Loadout::MountSize(SubsystemKind k, ModuleKind stock, bool smallCraft, double lengthKm)
	{
		auto size = smallCraft || IsScreen(k) ? HardpointSize::Small : lengthKm >= 1 ? HardpointSize::Large : HardpointSize::Medium;
		auto built = ModuleSpec::Of(stock)->Size;
		return built > size ? built : size;
	}

	cs::Nullable<ModuleKind> Loadout::StockOf(SubsystemKind k)
	{
		switch (k)
		{
			case SubsystemKind::PointDefense: return ModuleKind::PointDefense;
			case SubsystemKind::LaserTurret: return ModuleKind::LaserTurret;
			case SubsystemKind::HeavyLaser: return ModuleKind::HeavyLaser;
			case SubsystemKind::MissileLauncher: return ModuleKind::MissileLauncher;
			case SubsystemKind::Railgun: return ModuleKind::Railgun;
			case SubsystemKind::ShieldGenerator: return ModuleKind::ShieldGenerator;
			case SubsystemKind::Engine: return ModuleKind::Engine;
			case SubsystemKind::Autocannon: return ModuleKind::Autocannon;
			case SubsystemKind::LightRailgun: return ModuleKind::LightRailgun;
			case SubsystemKind::EmpLauncher: return ModuleKind::EmpLauncher;
			case SubsystemKind::MineLayer: return ModuleKind::MineLayer;
			case SubsystemKind::AutoTurret: return ModuleKind::AutoTurret;
			case SubsystemKind::TorpedoTube: return ModuleKind::TorpedoTube;
			case SubsystemKind::TachyonLance: return ModuleKind::TachyonLance;
			case SubsystemKind::PhaserArray: return ModuleKind::PhaserArray;
			case SubsystemKind::PlasmaCannon: return ModuleKind::PlasmaCannon;
			case SubsystemKind::AntimatterTorpedo: return ModuleKind::AntimatterTorpedo;
			case SubsystemKind::FlakCannon: return ModuleKind::FlakCannon;
			case SubsystemKind::InterceptorMissile: return ModuleKind::InterceptorMissile;
			case SubsystemKind::Coilgun: return ModuleKind::Coilgun;
			default: return std::nullopt;
		}
	}

	double Loadout::HullScale(const cs::Ref<CombatSpec>& design) { return cs::Math::Max(0.05, cs::Math::Sqrt(cs::Math::Max(0.0, design->DryMassTonnes) / 9000)); }

	double Loadout::UsedSpace() const { return FacilityLayouts::SpaceOf(Facilities); }

	double Loadout::FreeSpace() const { return cs::Math::Max(0.0, InternalSpace - UsedSpace()); }

	double Loadout::PowerOf(const cs::List<cs::Ref<FacilitySite>>& sites)
	{
		double mw = 0;
		for (const auto& s : sites)
		{
			auto def = s->Def();
			mw += def ? def->PowerMw : 0;
		}
		return mw;
	}

	double Loadout::CrewOf(const cs::List<cs::Ref<FacilitySite>>& sites)
	{
		double n = 0;
		for (const auto& s : sites)
		{
			auto def = s->Def();
			n += def ? def->Crew : 0;
		}
		return n;
	}

	double Loadout::ReactorMw() const { return ReactorReservePerScale * Scale() + PowerOf(StockFacilities); }

	double Loadout::PowerDrawMw() const { return PowerOf(Facilities); }

	double Loadout::FreePowerMw() const { return cs::Math::Max(0.0, ReactorMw() - PowerDrawMw()); }

	double Loadout::FittingCrew() const { return cs::Math::Max(0.0, Base->Crew) * CrewSpareShare + CrewOf(StockFacilities); }

	double Loadout::CrewManning() const { return CrewOf(Facilities); }

	double Loadout::FreeCrew() const { return cs::Math::Max(0.0, FittingCrew() - CrewManning()); }

	int Loadout::Count(ModuleKind k) const
	{
		int n = 0;
		for (const auto& f : Fitted) if (f == k) n++;
		for (const auto& f : Facilities)
		{
			auto def = f->Def();
			if ((def ? def->Module : cs::Nullable<ModuleKind>()) == k) n++;
		}
		return n;
	}

	int Loadout::CountFacility(const cs::String& id) const
	{
		int n = 0;
		for (const auto& f : Facilities) if (cs::Strings::EqualsOrdinalIgnoreCase(f->Kind, id)) n++;
		return n;
	}

	cs::List<cs::Ref<FacilitySite>> Loadout::RemovedFacilities() const
	{
		cs::List<cs::Ref<FacilitySite>> r;
		for (const auto& s : StockFacilities) if (!Facilities.Contains(s)) r.Add(s);
		return r;
	}

	cs::List<cs::Ref<FacilitySite>> Loadout::AddedFacilities() const
	{
		cs::List<cs::Ref<FacilitySite>> r;
		for (const auto& s : Facilities) if (!IsStockFacility(s)) r.Add(s);
		return r;
	}

	bool Loadout::IsStockFacility(const cs::Ref<FacilitySite>& s) const
	{
		for (const auto& x : StockFacilities) if (x == s) return true;
		return false;
	}

	bool Loadout::IsStock() const
	{
		for (int i = 0; i < Slots.Count(); i++) if (Fitted[i] != Slots[i]->Stock) return false;
		if (Facilities.Count() != StockFacilities.Count()) return false;
		for (int i = 0; i < Facilities.Count(); i++) if (Facilities[i] != StockFacilities[i]) return false;
		return true;
	}

	double Loadout::CapacityTonnes() const
	{
		double t = BaseCapacityTonnes + Outfitting::RackTonnes(Self(), Count(ModuleKind::CargoRack));
		for (const auto& f : AddedFacilities())
		{
			auto def = f->Def();
			t += def ? def->CargoTonnes : 0;
		}
		for (const auto& f : RemovedFacilities())
		{
			auto def = f->Def();
			t -= def ? def->CargoTonnes : 0;
		}
		return cs::Math::Max(0.0, t);
	}

	int Loadout::ExtraHoldRows() const
	{
		int rows = Count(ModuleKind::CargoRack) * RackRows;
		for (const auto& f : AddedFacilities())
		{
			auto def = f->Def();
			rows += def ? def->HoldRows : 0;
		}
		for (const auto& f : RemovedFacilities())
		{
			auto def = f->Def();
			rows -= def ? def->HoldRows : 0;
		}
		return rows;
	}

	bool Loadout::IsHold(const cs::Ref<FacilityKind>& kind)
	{
		return kind != nullptr && (kind->Module == ModuleKind::CargoRack || kind->CargoTonnes > 0 || kind->HoldRows > 0);
	}

	int Loadout::BrigBerthsEach() const { return cs::ToInt(cs::Math::Round(20 * cs::Math::Max(1.0, Scale()))); }

	double Loadout::AddedMassTonnes() const
	{
		double scale = Scale(), mass = 0;
		for (int i = 0; i < Slots.Count(); i++)
		{
			if (Fitted[i] == Slots[i]->Stock) continue;
			if (Fitted[i].has_value()) mass += PartAt(i, *Fitted[i])->Mass(scale);
			if (Slots[i]->Stock.has_value()) mass -= PartAt(i, *Slots[i]->Stock)->Mass(scale);
		}
		for (const auto& f : AddedFacilities())
		{
			auto def = f->Def();
			mass += def ? def->Mass(scale) : 0;
		}
		for (const auto& f : RemovedFacilities())
		{
			auto def = f->Def();
			mass -= def ? def->Mass(scale) : 0;
		}
		return mass;
	}

	cs::Ref<IPartSpec> Loadout::PartAt(int slot, ModuleKind m) const
	{
		return slot >= 0 && slot < Slots.Count() && Slots[slot]->Type == MountType::Spinal ? SpinalMounts::Part(m) : cs::Ref<IPartSpec>(ModuleSpec::Of(m));
	}

	cs::Ref<CombatSpec> Loadout::Build()
	{
		if (IsStock()) return Base;
		auto spec = Base->Clone();
		double scale = Scale();
		int baseGenerators = 0, engines = 0;
		double ratings = 0, thrust = 0;
		for (const auto& d : Base->Layout) if (d->Kind == SubsystemKind::ShieldGenerator) baseGenerators++;
		for (int i = 0; i < Slots.Count(); i++)
		{
			const auto& slot = Slots[i];
			if (!Fitted[i].has_value()) continue;
			ModuleKind m = *Fitted[i];
			double rating = m == ModuleKind::HardenedShield ? HardenedRating : m == ModuleKind::TorchEngine ? TorchRating : 1;
			if (slot->Type == MountType::Shield) ratings += rating;
			if (slot->Type == MountType::Engine) { engines++; thrust += rating; }
			if (m == slot->Stock) continue;
			const auto& was = Base->Layout[slot->LayoutIndex];
			// A spinal mount takes the ship-length version of the gun, laid by the hull (U.SPC-T490).
			if (slot->Type == MountType::Spinal)
			{
				spec->Layout[slot->LayoutIndex] = SpinalMounts::Mount(m, was);
				continue;
			}
			// The hull frame's own cone for the hardpoint carries over; where it has none, the new part's module gives it (U.SPC-T533).
			auto def = cs::New<SubsystemDef>(ModuleSpec::SubsystemOf(m), was->X, was->Y, was->Z, was->FacesAft, was->ConeAxis, was->ConeHalfDeg);
			def->Rating = rating;
			if (m == ModuleKind::HardenedShield) def->MaxHp *= 1.4;
			if (m == ModuleKind::TorchEngine) def->MaxHp *= 1.2;
			// A lance is spinal: whatever the mount's cone was, it fires only down the keel (U.SPC-T487).
			if (m == ModuleKind::TachyonLance)
			{
				def->ConeAxis = FireCones::Bow;
				def->ConeHalfDeg = cs::Math::Min(was->ConeHalfDeg.value_or(TachyonLances::ConeHalfDeg), TachyonLances::ConeHalfDeg);
			}
			spec->Layout[slot->LayoutIndex] = def;
		}
		if (baseGenerators > 0)
		{
			spec->Shield = Base->Shield / baseGenerators * ratings;
			spec->ShieldRegen = Base->ShieldRegen / baseGenerators * ratings;
		}
		int plates = Count(ModuleKind::ArmourPlating);
		spec->Armor = Base->Armor + plates * ArmourPerPlate;
		spec->Hull = Base->Hull * (1 + plates * HullPerPlate);
		// The marine complement is what the habitats quarter (U.SPC-T465): each one fitted adds its berths, each of the
		// design's own taken out its own.
		for (const auto& f : AddedFacilities()) spec->Marines += f->BerthsOn(scale);
		for (const auto& f : RemovedFacilities()) spec->Marines -= f->BerthsOn(scale);
		spec->Marines = cs::Math::Max(0.0, spec->Marines);
		for (const auto& f : AddedFacilities())
		{
			auto def = f->Def();
			if (def && def->Effect) def->Effect(spec, Self(), 1);
		}
		for (const auto& f : RemovedFacilities())
		{
			auto def = f->Def();
			if (def && def->Effect) def->Effect(spec, Self(), -1);
		}

		double dry = cs::Math::Max(1.0, Base->DryMassTonnes + AddedMassTonnes());
		double heavier = Base->DryMassTonnes > 0 ? dry / Base->DryMassTonnes : 1;
		double drive = engines > 0 ? thrust / engines : 1;
		spec->DryMassTonnes = dry;
		spec->MassKt = Base->MassKt * heavier;
		spec->MainThrustMN = Base->MainThrustMN * drive;
		spec->RetroThrustMN = Base->RetroThrustMN * drive;
		spec->Accel = Base->Accel * drive / heavier;
		spec->Decel = Base->Decel * drive / heavier;
		spec->MaxSpeed = Base->MaxSpeed * cs::Math::Sqrt(drive / heavier);
		spec->TurnRateDegS = Base->TurnRateDegS / cs::Math::Sqrt(heavier);
		return spec;
	}

	OutfitOutcome Loadout::CanAdd(const cs::Ref<FacilityKind>& kind) const
	{
		if (kind == nullptr || !kind->Fittable || !kind->AllowedOn(Category, Class)) return OutfitOutcome::WontFit;
		// A facility that needs another to work goes in only beside it (a Breacher bay beside a habitat, U.SPC-T466).
		// (An empty Requires stands for C#'s null.)
		if (!kind->Requires.empty() && CountFacility(kind->Requires) == 0) return OutfitOutcome::WontFit;
		// Only with the reactor power and the crew to spare for it (U.SPC-T521), and then the space.
		if (kind->PowerMw > FreePowerMw() + 1e-9) return OutfitOutcome::NoPower;
		if (kind->Crew > FreeCrew() + 1e-9) return OutfitOutcome::NoCrew;
		if (kind->Size > FreeSpace() + 1e-9) return OutfitOutcome::NoRoom;
		return OutfitOutcome::Ok;
	}

	bool Loadout::CanRemove(int index) const
	{
		if (index < 0 || index >= Facilities.Count()) return false;
		auto def = Facilities[index]->Def();
		if (!(def && def->Fittable)) return false;
		// Nor the last of a kind another fitted facility needs (the habitat that supplies a Breacher bay, U.SPC-T466).
		cs::String kind = Facilities[index]->Kind;
		if (CountFacility(kind) > 1) return true;
		for (const auto& f : Facilities)
		{
			auto fd = f->Def();
			if (fd && !fd->Requires.empty() && cs::Strings::EqualsOrdinalIgnoreCase(fd->Requires, kind)) return false;
		}
		return true;
	}

	cs::Ref<Loadout> Loadout::Copy() const
	{
		auto l = cs::New<Loadout>(Base, Class, Category, BaseCapacityTonnes, SmallCraft, StockFacilities, InternalSpace - FacilityLayouts::SpaceOf(StockFacilities));
		for (int i = 0; i < Fitted.Count(); i++) l->Fitted[i] = Fitted[i]; // Array.Copy
		l->Facilities.Clear();
		l->Facilities.AddRange(Facilities);
		return l;
	}

	// ---------------------------------------------------------------- Outfitting

	bool Outfitting::IsYard(const cs::Ref<Settlement>& s) { return s != nullptr && (s->HasShipyard() || s->Kind == SettlementKind::Haven) && !s->Fallen; }

	cs::Ref<Loadout> Outfitting::LoadoutOf(const cs::Ref<Ship>& ship)
	{
		if (ship->Loadout == nullptr && ship->Combat != nullptr)
			ship->Loadout = cs::New<Loadout>(ship->Combat->Spec, ship->Spec->Class, ship->CapacityTonnes);
		return ship->Loadout;
	}

	double Outfitting::ScanRangeKm(const cs::Ref<Ship>& ship) { return Sensors::HavenReachKm(ship); }

	double Outfitting::BrigCapacity(const cs::Ref<Ship>& ship)
	{
		return (ship ? ship->Loadout : nullptr) == nullptr ? 0 : ship->Loadout->Count(ModuleKind::Brig) * ship->Loadout->BrigBerthsEach();
	}

	double Outfitting::BrigRoom(const cs::Ref<Ship>& ship)
	{
		return ship == nullptr ? 0 : cs::Math::Max(0.0, BrigCapacity(ship) - ship->Prisoners - ship->CaptainsHeld);
	}

	bool Outfitting::Install(const cs::Ref<Ship>& ship, ModuleKind module)
	{
		auto facility = FacilityKinds::ForModule(module);
		if (facility != nullptr) return Install(ship, facility);
		auto l = LoadoutOf(ship);
		if (l == nullptr) return false;
		for (int i = 0; i < l->Slots.Count(); i++)
		{
			if (l->Fitted[i].has_value() || !CanFit(l, i, module)) continue;
			l->Fitted[i] = module;
			ship->Combat = ship->Combat->Refitted(l->Build(), l->Class, l->Slots[i]->LayoutIndex);
			return true;
		}
		return false;
	}

	bool Outfitting::Install(const cs::Ref<Ship>& ship, const cs::Ref<FacilityKind>& kind)
	{
		auto l = LoadoutOf(ship);
		if (l == nullptr || l->CanAdd(kind) != OutfitOutcome::Ok) return false;
		l->Facilities.Add(FacilitySite::Fitted(kind));
		Rebuild(ship, l, kind, nullptr);
		return true;
	}

	double Outfitting::RackTonnes(const cs::Ref<Loadout>& l, int racks) { return l->BaseCapacityTonnes * Loadout::RackCapacityShare * racks; }

	OutfitQuote Outfitting::Quote(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int slot, cs::Nullable<ModuleKind> module)
	{
		auto l = LoadoutOf(ship);
		if (l == nullptr || yard == nullptr || slot < 0 || slot >= l->Slots.Count()) return OutfitQuote();
		auto add = module.has_value() ? l->PartAt(slot, *module) : nullptr;
		auto remove = l->Fitted[slot].has_value() ? l->PartAt(slot, *l->Fitted[slot]) : nullptr;
		return Price(yard, l->Scale(), add, remove, PartHealth(ship, l->Slots[slot]));
	}

	OutfitQuote Outfitting::QuoteFacility(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, const cs::Ref<FacilityKind>& kind)
	{
		auto l = LoadoutOf(ship);
		if (l == nullptr || yard == nullptr || kind == nullptr) return OutfitQuote();
		return Price(yard, l->Scale(), kind, nullptr, 1);
	}

	OutfitQuote Outfitting::QuoteStrip(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int index)
	{
		auto l = LoadoutOf(ship);
		if (l == nullptr || yard == nullptr || index < 0 || index >= l->Facilities.Count()) return OutfitQuote();
		return Price(yard, l->Scale(), nullptr, l->Facilities[index]->Def(), 1);
	}

	OutfitQuote Outfitting::Price(const cs::Ref<Settlement>& yard, double scale, const cs::Ref<IPartSpec>& add, const cs::Ref<IPartSpec>& remove, double health)
	{
		double materials = 0, labour = 0, tradeIn = 0;
		bool inStock = true;
		Good shortOf = Good::Alloys;
		if (add != nullptr)
			for (int i = 0; i < add->PartBill().Count(); i++)
			{
				auto g = add->PartBill()[i].first;
				double t = add->Tonnes(i, scale);
				materials += yard->PurchaseCost(g, t) * (1 + Economy::PortTax);
				labour += t * GoodInfo::Of(g)->BasePrice * LabourShare;
				if (inStock && yard->Available(g) < t) { inStock = false; shortOf = g; }
			}
		if (remove != nullptr)
			for (int i = 0; i < remove->PartBill().Count(); i++)
				tradeIn += yard->SaleValue(remove->PartBill()[i].first, remove->Tonnes(i, scale) * TradeInShare * health) * (1 - Economy::PortTax);
		return OutfitQuote(materials, labour, tradeIn, inStock, shortOf);
	}

	double Outfitting::PartHealth(const cs::Ref<Ship>& ship, const cs::Ref<OutfitSlot>& slot)
	{
		if (slot->LayoutIndex < 0 || ship->Combat == nullptr || slot->LayoutIndex >= ship->Combat->Subsystems.Count()) return 1;
		return ship->Combat->Subsystems[slot->LayoutIndex]->Health();
	}

	bool Outfitting::CanFit(const cs::Ref<Loadout>& l, int slot, cs::Nullable<ModuleKind> module)
	{
		if (l == nullptr || slot < 0 || slot >= l->Slots.Count() || !module.has_value()) return false;
		ModuleKind m = *module;
		const auto& s = l->Slots[slot];
		// A tachyon lance goes only into a capital hull (U.SPC-T487).
		if (m == ModuleKind::TachyonLance && !TachyonLances::CanCarry(l->Class)) return false;
		// A spinal mount is only on a hull built around one, and a role-bound weapon there is the spinal version, not the
		// role's turret (U.SPC-T490).
		if (s->Type == MountType::Spinal) return SpinalMounts::CanCarry(l->Class) && ModuleSpec::Fits(m, s);
		// A role-bound weapon (a phaser array, a plasma cannon) goes only on the hulls of its role (U.SPC-T488).
		return ModuleSpec::Fits(m, s) && EnergyWeapons::RoleAllows(m, l->Class);
	}

	OutfitOutcome Outfitting::Ready(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard)
	{
		if (!IsYard(yard)) return OutfitOutcome::NotAYard;
		if (ship == nullptr || ship->Combat == nullptr || ship->Destroyed || ship->Derelict) return OutfitOutcome::Busy;
		if (economy != nullptr && !economy->CanTrade(ship, yard)) return OutfitOutcome::CannotDock;
		if (ship->Battle != nullptr || ship->Combat->Interior->UnderBoarding()) return OutfitOutcome::Busy;
		return OutfitOutcome::Ok;
	}

	OutfitOutcome Outfitting::Fit(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int slot,
		cs::Nullable<ModuleKind> module, OutfitQuote& quote)
	{
		quote = OutfitQuote();
		auto ready = Ready(economy, ship, yard);
		if (ready != OutfitOutcome::Ok) return ready;
		auto l = LoadoutOf(ship);
		if (slot < 0 || slot >= l->Slots.Count()) return OutfitOutcome::NoSuchSlot;
		if (!CanFit(l, slot, module)) return OutfitOutcome::WontFit;
		if (l->Fitted[slot] == module) return OutfitOutcome::AlreadyFitted;
		if (module.has_value() && !ModuleSpec::YardBuilds(*module, yard)) return OutfitOutcome::YardCannotBuild;

		quote = Quote(ship, yard, slot, module);
		auto add = l->PartAt(slot, *module);
		auto remove = l->Fitted[slot].has_value() ? l->PartAt(slot, *l->Fitted[slot]) : nullptr;
		auto paid = Charge(ship, yard, l->Scale(), add, remove, PartHealth(ship, l->Slots[slot]), quote);
		if (paid != OutfitOutcome::Ok) return paid;

		l->Fitted[slot] = module;
		ship->Combat = ship->Combat->Refitted(l->Build(), l->Class, l->Slots[slot]->LayoutIndex);
		return OutfitOutcome::Ok;
	}

	OutfitOutcome Outfitting::FitFacility(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard,
		const cs::Ref<FacilityKind>& kind, OutfitQuote& quote)
	{
		quote = OutfitQuote();
		auto ready = Ready(economy, ship, yard);
		if (ready != OutfitOutcome::Ok) return ready;
		auto l = LoadoutOf(ship);
		auto can = l->CanAdd(kind);
		if (can != OutfitOutcome::Ok) return can;

		quote = QuoteFacility(ship, yard, kind);
		auto paid = Charge(ship, yard, l->Scale(), kind, nullptr, 1, quote);
		if (paid != OutfitOutcome::Ok) return paid;

		l->Facilities.Add(FacilitySite::Fitted(kind));
		Rebuild(ship, l, kind, nullptr);
		return OutfitOutcome::Ok;
	}

	OutfitOutcome Outfitting::StripFacility(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int index,
		OutfitQuote& quote)
	{
		quote = OutfitQuote();
		auto ready = Ready(economy, ship, yard);
		if (ready != OutfitOutcome::Ok) return ready;
		auto l = LoadoutOf(ship);
		if (index < 0 || index >= l->Facilities.Count()) return OutfitOutcome::NoSuchSlot;
		if (!l->CanRemove(index)) return OutfitOutcome::WontFit;
		auto kind = l->Facilities[index]->Def();

		// What the hull would carry afterwards: racks, pods and brig cells taken out must leave room for what is in them.
		cs::Ref<CargoHold> resized = nullptr;
		if (Loadout::IsHold(kind))
		{
			auto after = l->Copy();
			after->Facilities.RemoveAt(index);
			if (ship->HasCargo && ship->CargoTonnes > after->CapacityTonnes() + 1e-9) return OutfitOutcome::HoldTooFull;
			resized = ship->ResizedHold(after->ExtraHoldRows());
			if (resized == nullptr) return OutfitOutcome::HoldTooFull;
		}
		if (kind->Module == ModuleKind::Brig && ship->Prisoners + ship->CaptainsHeld > (l->Count(ModuleKind::Brig) - 1) * l->BrigBerthsEach() + 1e-9)
			return OutfitOutcome::BrigOccupied;

		quote = QuoteStrip(ship, yard, index);
		auto paid = Charge(ship, yard, l->Scale(), nullptr, kind, 1, quote);
		if (paid != OutfitOutcome::Ok) return paid;

		l->Facilities.RemoveAt(index);
		Rebuild(ship, l, kind, resized);
		return OutfitOutcome::Ok;
	}

	OutfitOutcome Outfitting::Charge(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, double scale, const cs::Ref<IPartSpec>& add,
		const cs::Ref<IPartSpec>& remove, double health, const OutfitQuote& quote)
	{
		if (!quote.InStock) return OutfitOutcome::NoStock;
		if (quote.Net() > 0 && ship->Faction->Treasury < quote.Net()) return OutfitOutcome::CantAfford;
		if (add != nullptr)
			for (int i = 0; i < add->PartBill().Count(); i++)
			{
				auto g = add->PartBill()[i].first;
				double t = add->Tonnes(i, scale);
				double price = yard->PurchaseCost(g, t);
				yard->Stock[static_cast<int>(g)] = cs::Math::Max(0.0, yard->Stock[static_cast<int>(g)] - t);
				yard->ExtraDemand[static_cast<int>(g)] += t;
				yard->TradeVolumeCredits += price;
				yard->Owner->Treasury += price * Economy::PortTax;
			}
		if (remove != nullptr)
			for (int i = 0; i < remove->PartBill().Count(); i++)
			{
				auto g = remove->PartBill()[i].first;
				double t = remove->Tonnes(i, scale) * TradeInShare * health;
				double value = yard->SaleValue(g, t);
				yard->Stock[static_cast<int>(g)] += t;
				yard->Owner->Treasury += value * Economy::PortTax;
			}
		yard->Owner->Treasury += quote.Labour;
		ship->Faction->Treasury -= quote.Net();
		yard->RefreshPrices();
		return OutfitOutcome::Ok;
	}

	void Outfitting::Rebuild(const cs::Ref<Ship>& ship, const cs::Ref<Loadout>& l, const cs::Ref<FacilityKind>& kind, const cs::Ref<CargoHold>& resized)
	{
		auto spec = l->Build();
		auto interior = InteriorAfter(ship, l, kind, spec);
		ship->Combat = ship->Combat->Refitted(spec, l->Class, -1, interior);
		if (!Loadout::IsHold(kind)) return;
		ship->CapacityTonnes = l->CapacityTonnes();
		ship->SetHold(resized ? resized : ship->ResizedHold(l->ExtraHoldRows()));
	}

	cs::Ref<Interior> Outfitting::InteriorAfter(const cs::Ref<Ship>& ship, const cs::Ref<Loadout>& l, const cs::Ref<FacilityKind>& kind,
		const cs::Ref<CombatSpec>& spec)
	{
		if (kind == nullptr || !kind->Compartment.has_value() || ship->Combat == nullptr) return nullptr;
		auto was = ship->Combat->Interior;
		auto now = Interior::For(l->Facilities, was->Crew(), cs::Math::Max(0.0, was->Marines() + spec->Marines - ship->Combat->Spec->Marines));
		// The marines who were aboard keep their drill; recruits for a new habitat start raw (U.SPC-T465).
		now->DefenderQuality = was->DefenderQuality;
		now->Training = now->Marines() > 0 ? was->Training * cs::Math::Min(1.0, was->Marines() / now->Marines()) : 0;
		return now;
	}

	// ---------------------------------------------------------------- the AI's refits

	cs::Ref<IPartSpec> Outfitting::AiRefit(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, const cs::Ref<BuildPlan>& plan)
	{
		auto l = LoadoutOf(ship);
		if (l == nullptr) return nullptr;
		for (const auto& [slot, module, facility] : AiWishes(ship, l, plan))
		{
			cs::Ref<IPartSpec> part = facility != nullptr ? cs::Ref<IPartSpec>(facility) : l->PartAt(slot, module);
			auto q = facility != nullptr ? QuoteFacility(ship, yard, facility) : Quote(ship, yard, slot, module);
			if (!q.InStock || !Surplus(yard, part, l->Scale())) continue;
			if (ship->Faction->Treasury < q.Net() * AiFlush + 2 * Economy::HullPrice(ship->Spec->Class)) continue;
			OutfitQuote unused;
			auto outcome = facility != nullptr ? FitFacility(economy, ship, yard, facility, unused) : Fit(economy, ship, yard, slot, module, unused);
			if (outcome == OutfitOutcome::Ok) return part;
		}
		return nullptr;
	}

	cs::List<cs::String>& Outfitting::NavyFacilities()
	{
		static cs::List<cs::String> list{
			FacilityKinds::ArmourPlating, FacilityKinds::Scanner, FacilityKinds::ArmourPlating, FacilityKinds::Brig, FacilityKinds::BreacherBay,
			FacilityKinds::ArmourPlating, FacilityKinds::ArmourPlating,
		};
		return list;
	}

	cs::List<cs::String>& Outfitting::RaiderFacilities()
	{
		static cs::List<cs::String> list{
			FacilityKinds::ArmourPlating, FacilityKinds::Brig, FacilityKinds::CargoRacks, FacilityKinds::BreacherBay, FacilityKinds::ArmourPlating,
			FacilityKinds::ArmourPlating, FacilityKinds::ArmourPlating,
		};
		return list;
	}

	bool Outfitting::Surplus(const cs::Ref<Settlement>& yard, const cs::Ref<IPartSpec>& part, double scale)
	{
		for (int i = 0; i < part->PartBill().Count(); i++)
			if (yard->Available(part->PartBill()[i].first) < part->Tonnes(i, scale) * AiStockMargin) return false;
		return true;
	}

	cs::List<Outfitting::AiWish> Outfitting::AiWishes(const cs::Ref<Ship>& ship, const cs::Ref<Loadout>& l, const cs::Ref<BuildPlan>& plan)
	{
		cs::List<AiWish> wishes;
		// The facilities it wants that it has not got yet, in order, so far as each still fits.
		cs::Dictionary<cs::String, int, cs::OrdinalIgnoreCaseComparer> have;
		for (const auto& f : l->AddedFacilities())
		{
			int n = 0;
			have.TryGetValue(f->Kind, n);
			have[f->Kind] = n + 1;
		}
		for (const auto& id : ship->Faction->IsPirate ? RaiderFacilities() : NavyFacilities())
		{
			int n = 0;
			if (have.TryGetValue(id, n) && n > 0) { have[id] = n - 1; continue; }
			auto kind = FacilityKinds::Of(id);
			// A Breacher bay is only worth its space where the habitats can fill a whole team (U.SPC-T521).
			if (kind == BreacherBays::Kind() && (BreacherBays::Count(ship) > 0 || !BreacherBays::CanMan(l))) continue;
			if (l->CanAdd(kind) == OutfitOutcome::Ok) wishes.Add(AiWish{ -1, ModuleKind{}, kind });
		}
		bool big = ship->Spec->Class == ShipClass::Cruiser || ship->Spec->Class == ShipClass::Battleship || ship->Spec->Class == ShipClass::Carrier
		           || ship->Spec->Class == ShipClass::CommandCruiser;
		// Every upgrade obeys the hardpoint's type and size (U.SPC-T508): a heavy laser only on an energy mount big
		// enough for it, never on a point-defence mount.
		for (int i = 0; i < l->Slots.Count(); i++)
			if (l->Fitted[i] == ModuleKind::ShieldGenerator && CanFit(l, i, ModuleKind::HardenedShield)) wishes.Add(AiWish{ i, ModuleKind::HardenedShield, nullptr });
		// The role weapons its owner's doctrine favours, where its class's patterns put them (U.SPC-T505).
		for (const auto& [slot, module] : FleetPatterns::Wishes(ship, l, plan)) wishes.Add(AiWish{ slot, module, nullptr });
		if (big)
			for (int i = 0; i < l->Slots.Count(); i++)
				if (l->Fitted[i] == ModuleKind::LaserTurret && CanFit(l, i, ModuleKind::HeavyLaser)) wishes.Add(AiWish{ i, ModuleKind::HeavyLaser, nullptr });
		// A navy whose doctrine leans to the lance wants one before better drives (U.SPC-T505); any other last of all.
		bool lanceFirst = FleetPatterns::FavoursLance(ship->Faction, plan);
		if (lanceFirst)
			if (auto early = LanceWish(l)) wishes.Add(AiWish{ *early, ModuleKind::TachyonLance, nullptr });
		for (int i = 0; i < l->Slots.Count(); i++)
			if (l->Fitted[i] == ModuleKind::Engine && CanFit(l, i, ModuleKind::TorchEngine)) wishes.Add(AiWish{ i, ModuleKind::TorchEngine, nullptr });
		if (!lanceFirst)
			if (auto late = LanceWish(l)) wishes.Add(AiWish{ *late, ModuleKind::TachyonLance, nullptr });
		return wishes;
	}

	cs::Nullable<int> Outfitting::LanceWish(const cs::Ref<Loadout>& l)
	{
		if (!TachyonLances::CanCarry(l->Class) || l->Count(ModuleKind::TachyonLance) > 0) return std::nullopt;
		for (int i = 0; i < l->Slots.Count(); i++)
			if (l->Slots[i]->Type == MountType::Spinal && l->Fitted[i] == ModuleKind::Coilgun && CanFit(l, i, ModuleKind::TachyonLance)) return i;
		for (int i = 0; i < l->Slots.Count(); i++)
			if (l->Fitted[i] == ModuleKind::HeavyLaser && CanFit(l, i, ModuleKind::TachyonLance)) return i;
		return std::nullopt;
	}

	// ---------------------------------------------------------------- prisoners

	double Outfitting::TakePrisoners(const cs::Ref<Ship>& prize, const cs::List<cs::Ref<Ship>>& captors)
	{
		if ((prize ? prize->Combat : nullptr) == nullptr) return 0;
		double taken = 0;
		// The share is of the crew there were before anyone took any, not of those the last captor left.
		double crew = prize->Combat->Interior->Crew();
		for (const auto& c : captors)
		{
			if (c == nullptr || c == prize || c->Destroyed) continue;
			double room = BrigRoom(c);
			double want = cs::Math::Floor(crew * PrisonerShare - taken + 1e-9);
			double n = cs::Math::Floor(cs::Math::Min(room, want) + 1e-9);
			if (n < 1) continue;
			n = cs::Math::Floor(prize->Combat->Interior->TakeCrew(n) + 1e-9);
			if (n < 1) continue;
			c->Prisoners += n;
			c->PrisonerBounty += n * (prize->Faction->IsPirate && !c->Faction->IsPirate ? PirateBounty : Ransom);
			taken += n;
		}
		return taken;
	}

	double Outfitting::HandOverPrisoners(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port)
	{
		if (ship == nullptr || port == nullptr || ship->Prisoners <= 0 || port->Fallen) return 0;
		if (economy != nullptr && !economy->CanTrade(ship, port)) return 0;
		double paid = ship->PrisonerBounty;
		ship->Faction->Treasury += paid;
		ship->Prisoners = 0;
		ship->PrisonerBounty = 0;
		return paid;
	}
}
