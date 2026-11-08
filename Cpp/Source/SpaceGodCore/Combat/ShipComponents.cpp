// US-T89: modular ship construction, Mechlab style (see Combat/ShipComponents.h).
// US-T269: utility parts shot out with their component, heat sinks, power cells and bulkheads, and each component's tonnage.

#include "Combat/ShipComponents.h"

#include "Ships.h"
#include "Combat/Boarding.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/ModularHull.h"
#include "Combat/Outfitting.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"

namespace SpaceGod::Core
{
	const char* ToString(UtilityKind v)
	{
		switch (v)
		{
			case UtilityKind::ManeuveringThrusters: return "ManeuveringThrusters";
			case UtilityKind::LifeSupport: return "LifeSupport";
			case UtilityKind::HeatSink: return "HeatSink";
			case UtilityKind::PowerCell: return "PowerCell";
			case UtilityKind::ReinforcedBulkhead: return "ReinforcedBulkhead";
		}
		return "";
	}

	// ---------------------------------------------------------------- UtilitySpec

	UtilitySpec::UtilitySpec(UtilityKind kind, cs::String name, cs::String effect, double mass, cs::List<std::pair<Good, double>> bill)
	{
		Kind = kind;
		Name = name;
		Effect = effect;
		MassTonnes = mass;
		Bill = bill;
	}

	const cs::List<cs::Ref<UtilitySpec>>& UtilitySpec::Catalogue()
	{
		// In UtilityKind order: Of indexes by the kind's value.
		static const cs::List<cs::Ref<UtilitySpec>> all{
			// RCS quads, their valves and feed lines, and the plate round them.
			cs::New<UtilitySpec>(UtilityKind::ManeuveringThrusters, "Manoeuvring thrusters", "turn, roll and side thrust +12%", 30,
				cs::List<std::pair<Good, double>>{ { Good::Alloys, 20 }, { Good::Machinery, 14 }, { Good::Electronics, 4 } }),
			// Electrolysis cells, scrubbers, the reserve tanks and the ducting to the decks.
			cs::New<UtilitySpec>(UtilityKind::LifeSupport, "Life support plant", "air for 12% more crew", 45,
				cs::List<std::pair<Good, double>>{ { Good::Alloys, 18 }, { Good::Machinery, 12 }, { Good::Electronics, 8 } }),
			// US-T269: a wax-and-salt phase-change mass, its pumps and its own loop to the hull's radiators.
			cs::New<UtilitySpec>(UtilityKind::HeatSink, "Heat sink", "sheds 15% more heat", 35,
				cs::List<std::pair<Good, double>>{ { Good::Alloys, 16 }, { Good::Machinery, 10 }, { Good::Electronics, 6 } }),
			// US-T269: storage cells, their switchgear and a tap off the reactor bus.
			cs::New<UtilitySpec>(UtilityKind::PowerCell, "Power cell", "shield regen and boost +10%", 25,
				cs::List<std::pair<Good, double>>{ { Good::Alloys, 6 }, { Good::Machinery, 5 }, { Good::Electronics, 14 } }),
			// US-T269: doubled frames, thicker bulkheads and the welds through one component.
			cs::New<UtilitySpec>(UtilityKind::ReinforcedBulkhead, "Reinforced bulkhead", "component structure +50%, hull +4%", 40,
				cs::List<std::pair<Good, double>>{ { Good::Alloys, 34 }, { Good::Machinery, 5 } }),
		};
		return all;
	}

	const cs::Ref<UtilitySpec>& UtilitySpec::Of(UtilityKind k) { return Catalogue()[static_cast<int>(k)]; }

	// ---------------------------------------------------------------- ShipComponent

	int ShipComponent::Limit(const cs::Ref<Loadout>& l, MountType type) const
	{
		int n = 0;
		for (int s : Slots)
			if (s >= 0 && s < l->Slots.Count() && l->Slots[s]->Type == type) n++;
		return n;
	}

	bool ShipComponent::Takes(UtilityKind kind, bool smallCraft) const
	{
		bool keel = Kind != HullModuleKind::Sponson;
		switch (kind)
		{
			case UtilityKind::ManeuveringThrusters:
				return Kind == HullModuleKind::Bow || Kind == HullModuleKind::Engineering || Kind == HullModuleKind::Sponson;
			case UtilityKind::LifeSupport:
				return !smallCraft && (Kind == HullModuleKind::Midsection || Kind == HullModuleKind::Engineering);
			case UtilityKind::HeatSink:
			case UtilityKind::ReinforcedBulkhead:
				return keel;
			case UtilityKind::PowerCell:
				return true;
		}
		return false;
	}

	double ShipComponent::CapacityTonnes(double scale) const
	{
		switch (Kind)
		{
			case HullModuleKind::Bow: return ShipComponents::BowTonnes * scale;
			case HullModuleKind::Midsection: return ShipComponents::MidsectionTonnes * scale;
			case HullModuleKind::Engineering: return ShipComponents::EngineeringTonnes * scale;
			case HullModuleKind::Sponson: return ShipComponents::SponsonTonnes * scale;
		}
		return 0;
	}

	// ---------------------------------------------------------------- ShipComponents

	namespace
	{
		int Order(const cs::Ref<HullModule>& m)
		{
			// Bow first, the keel aft to engineering, then the sponsons fore to aft, port before starboard.
			if (m->Kind != HullModuleKind::Sponson) return -m->Section * 4;
			return 1000 - m->Section * 4 + (m->Side > 0 ? 1 : 0);
		}

		cs::String SectionName(HullModuleKind kind, int section, int sections)
		{
			if (kind == HullModuleKind::Bow) return "Bow";
			if (kind == HullModuleKind::Engineering) return "Engineering";
			return cs::Cat("Midsection ", sections - 1 - section);
		}

		int CountIn(const cs::List<int>& live, UtilityKind kind)
		{
			int n = 0;
			for (int v : live) if (v == static_cast<int>(kind)) n++;
			return n;
		}
	}

	int ShipComponents::SocketsOf(HullModuleKind kind, bool smallCraft)
	{
		switch (kind)
		{
			case HullModuleKind::Bow: return 1;
			case HullModuleKind::Midsection: return smallCraft ? 0 : 1;
			case HullModuleKind::Engineering: return smallCraft ? 1 : 2;
			case HullModuleKind::Sponson: return 1;
		}
		return 0;
	}

	cs::List<ShipComponent> ShipComponents::Of(const cs::Ref<Loadout>& l)
	{
		cs::List<ShipComponent> parts;
		if (l == nullptr || l->Base == nullptr) return parts;
		auto hull = ModularHull::For(l->Base);
		int sections = 0;
		for (const auto& m : hull->Modules) if (m->Kind != HullModuleKind::Sponson) sections++;

		cs::List<cs::Ref<HullModule>> modules;
		modules.AddRange(hull->Modules);
		// A stable insertion sort: the structure's own order breaks ties, so the same design always lays out the same.
		for (int i = 1; i < modules.Count(); i++)
			for (int j = i; j > 0 && Order(modules[j]) < Order(modules[j - 1]); j--)
				std::swap(modules[j], modules[j - 1]);

		int socket = 0;
		for (const auto& m : modules)
		{
			ShipComponent c;
			c.Module = m->Id;
			c.Kind = m->Kind;
			c.Section = m->Section;
			c.Side = m->Side;
			c.Name = m->Kind == HullModuleKind::Sponson
				? cs::Cat(m->Side < 0 ? "Port sponson, " : "Starboard sponson, ", cs::Strings::ToLowerInvariant(SectionName(hull->Modules[m->Section]->Kind, m->Section, sections)))
				: SectionName(m->Kind, m->Section, sections);
			for (int i = 0; i < l->Slots.Count(); i++)
				if (m->Hardpoints.Contains(l->Slots[i]->LayoutIndex)) c.Slots.Add(i);
			c.FirstSocket = socket;
			c.Sockets = SocketsOf(m->Kind, l->SmallCraft);
			socket += c.Sockets;
			parts.Add(c);
		}
		return parts;
	}

	int ShipComponents::SocketCount(const cs::Ref<Loadout>& l)
	{
		int n = 0;
		for (const auto& c : Of(l)) n += c.Sockets;
		return n;
	}

	cs::Nullable<UtilityKind> ShipComponents::At(const cs::Ref<Loadout>& l, int socket)
	{
		if (l == nullptr || socket < 0 || socket >= l->Utility.Count() || l->Utility[socket] < 0) return std::nullopt;
		int v = l->Utility[socket];
		if (v >= UtilitySpec::Catalogue().Count()) return std::nullopt;
		return static_cast<UtilityKind>(v);
	}

	int ShipComponents::Count(const cs::Ref<Loadout>& l, UtilityKind kind) { return l != nullptr ? CountIn(l->Utility, kind) : 0; }

	int ShipComponents::Fitted(const cs::Ref<Loadout>& l)
	{
		int n = 0;
		if (l != nullptr)
			for (int i = 0; i < l->Utility.Count(); i++) if (At(l, i).has_value()) n++;
		return n;
	}

	bool ShipComponents::Any(const cs::Ref<Loadout>& l)
	{
		if (l != nullptr)
			for (int v : l->Utility) if (v >= 0) return true;
		return false;
	}

	double ShipComponents::AddedMassTonnes(const cs::Ref<Loadout>& l)
	{
		if (l == nullptr) return 0;
		double scale = l->Scale(), mass = 0;
		for (int i = 0; i < l->Utility.Count(); i++)
		{
			auto k = At(l, i);
			if (k.has_value()) mass += UtilitySpec::Of(*k)->Mass(scale);
		}
		return mass;
	}

	double ShipComponents::UsedTonnes(const cs::Ref<Loadout>& l, const ShipComponent& c, int except)
	{
		if (l == nullptr) return 0;
		double scale = l->Scale(), used = 0;
		for (int s = c.FirstSocket; s < c.FirstSocket + c.Sockets; s++)
		{
			if (s == except) continue;
			auto k = At(l, s);
			if (k.has_value()) used += UtilitySpec::Of(*k)->Mass(scale);
		}
		return used;
	}

	void ShipComponents::Apply(const cs::Ref<CombatSpec>& spec, const cs::Ref<Loadout>& l)
	{
		if (spec == nullptr || l == nullptr || !Any(l)) return;
		// What is fitted where, and the spec as it is without any of it, so a part shot out can come off again (US-T269).
		cs::List<int> fitted, modules;
		for (const auto& c : Of(l))
			for (int s = c.FirstSocket; s < c.FirstSocket + c.Sockets; s++)
			{
				auto k = At(l, s);
				fitted.Add(k.has_value() ? static_cast<int>(*k) : -1);
				modules.Add(ModuleKey(c.Section, c.Side));
			}
		spec->UtilityBase = nullptr;
		spec->UtilityBase = spec->Clone();
		spec->UtilityFitted = fitted;
		spec->Utility = fitted;
		spec->UtilityModule = modules;
		spec->UtilityCrewBase = cs::Math::Max(0.0, l->Base->Crew);
		Effects(spec, fitted, spec->UtilityCrewBase);
	}

	void ShipComponents::Effects(const cs::Ref<CombatSpec>& spec, const cs::List<int>& live, double crewBase)
	{
		int thrusters = CountIn(live, UtilityKind::ManeuveringThrusters);
		if (thrusters > 0)
		{
			double agile = 1 + cs::Math::Min(ThrusterCap, thrusters * ThrusterShare);
			spec->TurnRateDegS *= agile;
			spec->LateralThrustMN *= agile;
			spec->VerticalThrustMN *= agile;
			spec->PitchYawTorqueMNKm *= agile;
			spec->RollTorqueMNKm *= agile;
		}
		int plants = CountIn(live, UtilityKind::LifeSupport);
		if (plants > 0) spec->Crew += crewBase * cs::Math::Min(LifeSupportCap, plants * LifeSupportShare);
		int sinks = CountIn(live, UtilityKind::HeatSink);
		spec->HeatShedBonus = cs::Math::Min(HeatSinkCap, sinks * HeatSinkShare);
		int cells = CountIn(live, UtilityKind::PowerCell);
		if (cells > 0)
		{
			double more = 1 + cs::Math::Min(PowerCellCap, cells * PowerCellShare);
			spec->ShieldRegen *= more;
			spec->BoostDurationS *= more;
		}
		int bulkheads = CountIn(live, UtilityKind::ReinforcedBulkhead);
		if (bulkheads > 0) spec->Hull *= 1 + cs::Math::Min(BulkheadHullCap, bulkheads * BulkheadHullShare);
	}

	cs::Ref<HullModule> ShipComponents::ModuleFor(const cs::Ref<ModularHull>& hull, int key)
	{
		if (hull != nullptr)
			for (const auto& m : hull->Modules)
				if (ModuleKey(m->Section, m->Side) == key) return m;
		return nullptr;
	}

	bool ShipComponents::ShotOut(const cs::Ref<CombatSpec>& spec, int socket)
	{
		if (spec == nullptr || socket < 0 || socket >= spec->UtilityFitted.Count() || spec->UtilityFitted[socket] < 0) return false;
		return socket >= spec->Utility.Count() || spec->Utility[socket] < 0;
	}

	cs::Ref<CombatSpec> ShipComponents::Rebuilt(const cs::Ref<CombatSpec>& spec, const cs::List<int>& live)
	{
		if (spec == nullptr || spec->UtilityBase == nullptr) return spec;
		auto s = spec->UtilityBase->Clone();
		s->UtilityBase = spec->UtilityBase;
		s->UtilityFitted = spec->UtilityFitted;
		s->UtilityModule = spec->UtilityModule;
		s->UtilityCrewBase = spec->UtilityCrewBase;
		s->Utility = live;
		Effects(s, live, s->UtilityCrewBase);
		return s;
	}

	int ShipComponents::KnockOut(const cs::Ref<ShipCombat>& combat)
	{
		if (combat == nullptr || combat->Spec == nullptr || combat->Spec->UtilityBase == nullptr || !combat->HasStructure()) return 0;
		auto hull = combat->Structure();
		auto live = combat->Spec->Utility;
		int lost = 0;
		for (int s = 0; s < live.Count(); s++)
		{
			if (live[s] < 0) continue;
			auto m = ModuleFor(hull, s < combat->Spec->UtilityModule.Count() ? combat->Spec->UtilityModule[s] : -1);
			if (m != nullptr && !m->Detached && !m->Shattered() && m->Condition() >= KnockOutAt) continue;
			live[s] = -1;
			lost++;
		}
		if (lost == 0) return 0;
		combat->Spec = Rebuilt(combat->Spec, live);
		// A bulkhead shot out takes the hull points it added with it (never the last of them).
		if (combat->Hull > combat->Spec->Hull) combat->Hull = cs::Math::Max(1.0, combat->Spec->Hull);
		return lost;
	}

	cs::Ref<CombatSpec> ShipComponents::Restored(const cs::Ref<CombatSpec>& spec)
	{
		if (spec == nullptr || spec->UtilityBase == nullptr) return spec;
		bool whole = spec->Utility.Count() == spec->UtilityFitted.Count();
		for (int s = 0; whole && s < spec->Utility.Count(); s++) whole = spec->Utility[s] == spec->UtilityFitted[s];
		return whole ? spec : Rebuilt(spec, spec->UtilityFitted);
	}

	void ShipComponents::Reinforce(const cs::Ref<ModularHull>& hull, const cs::Ref<CombatSpec>& spec)
	{
		if (hull == nullptr || spec == nullptr || spec->Utility.Count() == 0) return;
		for (const auto& m : hull->Modules)
		{
			int key = ModuleKey(m->Section, m->Side), n = 0;
			for (int s = 0; s < spec->Utility.Count() && s < spec->UtilityModule.Count(); s++)
				if (spec->Utility[s] == static_cast<int>(UtilityKind::ReinforcedBulkhead) && spec->UtilityModule[s] == key) n++;
			if (n == 0) continue;
			double f = 1 + n * BulkheadStructure;
			m->MaxStructure *= f;
			m->Structure = m->MaxStructure;
			for (const auto& j : hull->Joints)
				if (j->Joins(m->Id))
				{
					j->MaxStrength *= f;
					j->Strength = j->MaxStrength;
				}
		}
	}

	int ShipComponents::ComponentOf(const cs::List<ShipComponent>& parts, int socket)
	{
		for (int i = 0; i < parts.Count(); i++)
			if (socket >= parts[i].FirstSocket && socket < parts[i].FirstSocket + parts[i].Sockets) return i;
		return -1;
	}

	bool ShipComponents::CanFit(const cs::Ref<Loadout>& l, int socket, cs::Nullable<UtilityKind> kind)
	{
		if (l == nullptr) return false;
		auto parts = Of(l);
		int c = ComponentOf(parts, socket);
		if (c < 0) return false;
		if (!kind.has_value()) return At(l, socket).has_value();
		if (!parts[c].Takes(*kind, l->SmallCraft)) return false;
		// US-T269: the component's tonnage, with what is in this socket now taken out.
		double scale = l->Scale();
		return UsedTonnes(l, parts[c], socket) + UtilitySpec::Of(*kind)->Mass(scale) <= parts[c].CapacityTonnes(scale) + 1e-9;
	}

	OutfitQuote ShipComponents::Quote(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int socket, cs::Nullable<UtilityKind> kind)
	{
		auto l = ship != nullptr ? Outfitting::LoadoutOf(ship) : nullptr;
		if (l == nullptr || yard == nullptr || socket < 0 || socket >= SocketCount(l)) return OutfitQuote();
		auto now = At(l, socket);
		cs::Ref<IPartSpec> add = kind.has_value() ? cs::Ref<IPartSpec>(UtilitySpec::Of(*kind)) : nullptr;
		cs::Ref<IPartSpec> remove = now.has_value() ? cs::Ref<IPartSpec>(UtilitySpec::Of(*now)) : nullptr;
		return Outfitting::Price(yard, l->Scale(), add, remove, 1);
	}

	OutfitOutcome ShipComponents::Fit(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int socket,
		cs::Nullable<UtilityKind> kind, OutfitQuote& quote)
	{
		quote = OutfitQuote();
		auto ready = Outfitting::Ready(economy, ship, yard);
		if (ready != OutfitOutcome::Ok) return ready;
		auto l = Outfitting::LoadoutOf(ship);
		int sockets = SocketCount(l);
		if (socket < 0 || socket >= sockets) return OutfitOutcome::NoSuchSlot;
		if (At(l, socket) == kind) return OutfitOutcome::AlreadyFitted;
		if (!CanFit(l, socket, kind)) return OutfitOutcome::WontFit;

		quote = Quote(ship, yard, socket, kind);
		auto now = At(l, socket);
		cs::Ref<IPartSpec> add = kind.has_value() ? cs::Ref<IPartSpec>(UtilitySpec::Of(*kind)) : nullptr;
		cs::Ref<IPartSpec> remove = now.has_value() ? cs::Ref<IPartSpec>(UtilitySpec::Of(*now)) : nullptr;
		auto paid = Outfitting::Charge(ship, yard, l->Scale(), add, remove, 1, quote);
		if (paid != OutfitOutcome::Ok) return paid;

		while (l->Utility.Count() < sockets) l->Utility.Add(-1);
		l->Utility[socket] = kind.has_value() ? static_cast<int>(*kind) : -1;
		double crewWas = ship->Combat->Spec->Crew;
		ship->Combat = ship->Combat->Refitted(l->Build(), l->Class, -1);
		// The hands a new oxygen plant has air for come aboard from the owner's reserve, as a yard's muster brings them.
		double more = ship->Combat->Spec->Crew - crewWas;
		auto inside = ship->Combat->Interior;
		if (more > 0 && inside != nullptr && !inside->UnderBoarding())
		{
			double drafted = Crews::Draft(ship->Faction, more);
			if (drafted > 0) inside->Muster(inside->Crew() + drafted, inside->Marines());
		}
		return OutfitOutcome::Ok;
	}
}
