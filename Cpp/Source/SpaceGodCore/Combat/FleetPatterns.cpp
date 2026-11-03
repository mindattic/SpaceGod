// Port of Unity.SpaceGod Core/Combat/FleetPatterns.cs (US-T10).

#include "Combat/FleetPatterns.h"

#include <optional>

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Facilities.h"
#include "Combat/Outfitting.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Systems/StrategicCommand.h"

namespace SpaceGod::Core
{
	WeaponPattern::WeaponPattern(cs::String name, ShipClass cls, ModuleKind weapon, ModuleKind replaces, int count)
		: Name(std::move(name)), Class(cls), Weapon(weapon), Replaces(replaces), Count(cs::Math::Max(1, count))
	{
	}

	void FleetPatterns::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		Wealth();
		Spread();
		LanceShare();
		patterns();
		doctrine();
		raiders();
		StaticConstructor();
	}

	// ------------------------------------------------------------ settings

	cs::String FleetPatterns::Times(double v) { return cs::Cat(cs::Fmt(v, "0"), "x"); }
	cs::String FleetPatterns::Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	const cs::Ref<RangeSetting>& FleetPatterns::Wealth()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("patterns.wealth", SettingGroups::Ai,
			"Role weapons wealth", 25, 0, 200, 5,
			"How many times what its role weapons add to a new hull's price a navy or raider band must hold before it builds the hull to its pattern (phaser arrays, plasma cannons, a tachyon lance); a poorer one lays down the plain stock design.",
			Times, "phaser plasma tachyon lance pattern doctrine wealth navy raider fit new hull");
		return value;
	}

	const cs::Ref<RangeSetting>& FleetPatterns::Spread()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("patterns.spread", SettingGroups::Ai,
			"Navy weapon leanings", 0.5, 0, 1, 0.05,
			"How far each navy's own taste pulls its liking for each role weapon from its doctrine's either way, so two navies under the same build plan still field different mixes (0 makes every navy under a plan alike).",
			Percent, "phaser plasma tachyon lance doctrine faction leaning mix navy");
		return value;
	}

	const cs::Ref<RangeSetting>& FleetPatterns::LanceShare()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("patterns.lanceShare", SettingGroups::Ai,
			"Lance capitals", 0.25, 0, 1, 0.05,
			"The most of the capitals a navy lays down at a yard able to build one that carry a tachyon lance (a navy whose doctrine leans wholly to the lance); the lance stays rare.",
			Percent, "tachyon lance capital rare doctrine navy");
		return value;
	}

	// ------------------------------------------------------------ the patterns

	cs::List<cs::Ref<WeaponPattern>>& FleetPatterns::patterns()
	{
		RunClassConstructor();
		static cs::List<cs::Ref<WeaponPattern>> value = {
			// A destroyer's and a cruiser's point-defence mounts take a phaser array: swept wide it is their point defence,
			// laid narrow it burns hulls at medium range (U.SPC-T488).
			cs::New<WeaponPattern>("Phaser destroyer", ShipClass::Destroyer, ModuleKind::PhaserArray, ModuleKind::PointDefense, 2),
			cs::New<WeaponPattern>("Phaser cruiser", ShipClass::Cruiser, ModuleKind::PhaserArray, ModuleKind::PointDefense, 2),
			// A brawling battleship's two bow heavy lasers become plasma cannons, for whatever closes on it.
			cs::New<WeaponPattern>("Plasma battleship", ShipClass::Battleship, ModuleKind::PlasmaCannon, ModuleKind::HeavyLaser, 2),
			// The rare lance capital: its spinal coilgun becomes a tachyon lance (U.SPC-T487, U.SPC-T490).
			cs::New<WeaponPattern>("Lance battleship", ShipClass::Battleship, ModuleKind::TachyonLance, ModuleKind::Coilgun, 1),
		};
		return value;
	}

	const cs::List<cs::Ref<WeaponPattern>>& FleetPatterns::Patterns() { return patterns(); }

	void FleetPatterns::Register(const cs::Ref<WeaponPattern>& p)
	{
		if (p == nullptr) throw cs::ArgumentNullException("p");
		patterns().RemoveAll([&](const cs::Ref<WeaponPattern>& x) { return x->Name == p->Name; });
		patterns().Add(p);
	}

	cs::List<cs::Ref<WeaponPattern>> FleetPatterns::For(ShipClass cls)
	{
		cs::List<cs::Ref<WeaponPattern>> list;
		for (const auto& p : patterns())
			if (p->Class == cls) list.Add(p);
		return list;
	}

	// ------------------------------------------------------------ doctrine

	cs::Dictionary<cs::String, cs::Dictionary<ModuleKind, double>>& FleetPatterns::doctrine()
	{
		RunClassConstructor();
		static cs::Dictionary<cs::String, cs::Dictionary<ModuleKind, double>> value;
		return value;
	}

	cs::Dictionary<ModuleKind, double>& FleetPatterns::raiders()
	{
		RunClassConstructor();
		static cs::Dictionary<ModuleKind, double> value = []
		{
			cs::Dictionary<ModuleKind, double> d;
			d[ModuleKind::PhaserArray] = 0.45;
			d[ModuleKind::PlasmaCannon] = 0.6;
			d[ModuleKind::TachyonLance] = 0;
			return d;
		}();
		return value;
	}

	void FleetPatterns::StaticConstructor()
	{
		Lean(BuildPlan::Balanced(), 0.5, 0.45, 0.45);
		Lean(BuildPlan::Mobilise(), 0.5, 0.45, 0.45);
		Lean(BuildPlan::BattleLine(), 0.35, 0.8, 0.8);
		Lean(BuildPlan::Carriers(), 0.75, 0.25, 0.3);
		Lean(BuildPlan::Strike(), 0.6, 0.3, 0.2);
		Lean(BuildPlan::Screens(), 0.7, 0.3, 0.2);
		Lean(BuildPlan::LaneSecurity(), 0.65, 0.3, 0.2);
	}

	void FleetPatterns::Lean(const cs::Ref<BuildPlan>& plan, double phasers, double plasma, double lance)
	{
		if (plan == nullptr) return;
		cs::Dictionary<ModuleKind, double> likes;
		likes[ModuleKind::PhaserArray] = phasers;
		likes[ModuleKind::PlasmaCannon] = plasma;
		likes[ModuleKind::TachyonLance] = lance;
		doctrine()[plan->Name] = likes;
	}

	double FleetPatterns::Leaning(const cs::Ref<Faction>& f, ModuleKind weapon, const cs::Ref<BuildPlan>& plan)
	{
		if (f == nullptr) return 0;
		const cs::Dictionary<ModuleKind, double>* likes;
		if (f->IsPirate) likes = &raiders();
		else
		{
			const auto& key = (plan ? plan : BuildPlan::Balanced())->Name;
			if (doctrine().ContainsKey(key)) likes = &doctrine().At(key);
			else likes = &doctrine().At(BuildPlan::Balanced()->Name);
		}
		double like;
		if (!likes->TryGetValue(weapon, like)) return 0;
		if (like <= 0) return 0;
		double taste = (Taste(f->Name, weapon) - 0.5) * Spread()->Value;
		return cs::Math::Max(0.0, cs::Math::Min(1.0, like + taste));
	}

	double FleetPatterns::Taste(const cs::String& name, ModuleKind weapon)
	{
		uint32_t h = 2166136261u;
		// C# walks UTF-16 chars; names here are ASCII, where a byte is a char.
		for (unsigned char c : cs::Cat(name, "/", weapon))
		{
			h ^= c;
			h *= 16777619u;
		}
		h ^= h >> 15;
		h *= 2246822519u;
		h ^= h >> 13;
		return (h % 10007) / 10006.0;
	}

	// ------------------------------------------------------------ fitting

	cs::List<int> FleetPatterns::Slots(const cs::Ref<Loadout>& l, const cs::Ref<WeaponPattern>& p)
	{
		cs::List<int> list;
		if (l == nullptr || p == nullptr || l->Class != p->Class) return list;
		int places = 0;
		for (int i = 0; i < l->Slots.Count() && places < p->Count; i++)
		{
			if (l->Slots[i]->Stock != p->Replaces || !Outfitting::CanFit(l, i, p->Weapon)) continue;
			places++;
			if (l->Fitted[i] == p->Replaces) list.Add(i);
		}
		return list;
	}

	double FleetPatterns::ExtraCost(const cs::Ref<Loadout>& l, const cs::List<int>& slots, ModuleKind weapon)
	{
		double cost = 0;
		for (int i : slots)
		{
			cost += Worth(l->PartAt(i, weapon), l->Scale());
			if (l->Fitted[i].has_value())
			{
				ModuleKind was = *l->Fitted[i];
				cost -= Worth(l->PartAt(i, was), l->Scale());
			}
		}
		return cs::Math::Max(0.0, cost);
	}

	double FleetPatterns::Worth(const cs::Ref<IPartSpec>& part, double scale)
	{
		double v = 0;
		for (int i = 0; i < part->Bill().Count(); i++) v += part->Tonnes(i, scale) * GoodInfo::Of(part->Bill()[i].good)->BasePrice;
		return v;
	}

	bool FleetPatterns::YardBuilds(ModuleKind weapon, const cs::Ref<Settlement>& yard)
	{
		return yard != nullptr ? ModuleSpec::YardBuilds(weapon, yard) : weapon != ModuleKind::TachyonLance;
	}

	cs::List<cs::Ref<WeaponPattern>> FleetPatterns::Commission(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, const cs::Ref<BuildPlan>& plan, cs::Random* rng)
	{
		cs::List<cs::Ref<WeaponPattern>> built;
		if (ship == nullptr || ship->Combat == nullptr || ship->Faction == nullptr || ship->Destroyed) return built;
		auto f = ship->Faction;
		if (!f->IsMilitary && !f->IsPirate) return built;
		auto cls = ship->Spec->Class;
		std::optional<cs::Random> own;
		if (rng == nullptr)
		{
			// unchecked((int)((uint)(Taste(...) * int.MaxValue) ^ (uint)(ship.Id * 7919 + (int)cls * 104729)))
			uint32_t taste = static_cast<uint32_t>(cs::ToInt(Taste(f->Name, ModuleKind::LaserTurret) * cs::Int::MaxValue));
			uint32_t hull = static_cast<uint32_t>(cs::WrapAdd(cs::WrapMul(ship->Id, 7919), cs::WrapMul(static_cast<int>(cls), 104729)));
			own.emplace(static_cast<int>(taste ^ hull));
			rng = &*own;
		}
		cs::Ref<Loadout> l = nullptr;
		for (const auto& p : For(cls))
		{
			double chance = Leaning(f, p->Weapon, plan) * (p->Weapon == ModuleKind::TachyonLance ? LanceShare()->Value : 1);
			// The draw is taken whatever follows, so one pattern's outcome never shifts another's.
			if (rng->NextDouble() >= chance) continue;
			if (!YardBuilds(p->Weapon, yard)) continue;
			if (!l) l = Outfitting::LoadoutOf(ship);
			auto slots = Slots(l, p);
			if (slots.Count() == 0) continue;
			double cost = ExtraCost(l, slots, p->Weapon);
			if (f->Treasury < cost * Wealth()->Value) continue;
			for (int i : slots) l->Fitted[i] = p->Weapon;
			f->Treasury -= cost;
			built.Add(p);
		}
		// A new hull, whole and loaded: its combat spec is built from the loadout, as any refit's is.
		if (built.Count() > 0) ship->Combat = cs::New<ShipCombat>(l->Build(), cs::Nullable<ShipClass>(cls));
		return built;
	}

	cs::List<std::pair<int, ModuleKind>> FleetPatterns::Wishes(const cs::Ref<Ship>& ship, const cs::Ref<Loadout>& l, const cs::Ref<BuildPlan>& plan)
	{
		cs::List<std::pair<int, ModuleKind>> wishes;
		if (ship == nullptr || ship->Faction == nullptr || l == nullptr || !l->Class.has_value()) return wishes;
		ShipClass cls = *l->Class;
		for (const auto& p : For(cls))
		{
			if (p->Weapon == ModuleKind::TachyonLance || Leaning(ship->Faction, p->Weapon, plan) < RefitLeaning) continue;
			for (int i : Slots(l, p)) wishes.Add({ i, p->Weapon });
		}
		return wishes;
	}

	bool FleetPatterns::FavoursLance(const cs::Ref<Faction>& f, const cs::Ref<BuildPlan>& plan) { return Leaning(f, ModuleKind::TachyonLance, plan) >= RefitLeaning; }
}
