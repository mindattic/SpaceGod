// US-T89: modular ship construction, Mechlab style (see Combat/ShipComponents.h).

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
		switch (kind)
		{
			case UtilityKind::ManeuveringThrusters:
				return Kind == HullModuleKind::Bow || Kind == HullModuleKind::Engineering || Kind == HullModuleKind::Sponson;
			case UtilityKind::LifeSupport:
				return !smallCraft && (Kind == HullModuleKind::Midsection || Kind == HullModuleKind::Engineering);
		}
		return false;
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

	int ShipComponents::Count(const cs::Ref<Loadout>& l, UtilityKind kind)
	{
		int n = 0;
		if (l != nullptr)
			for (int v : l->Utility) if (v == static_cast<int>(kind)) n++;
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

	void ShipComponents::Apply(const cs::Ref<CombatSpec>& spec, const cs::Ref<Loadout>& l)
	{
		if (spec == nullptr || l == nullptr) return;
		int thrusters = Count(l, UtilityKind::ManeuveringThrusters);
		if (thrusters > 0)
		{
			double agile = 1 + cs::Math::Min(ThrusterCap, thrusters * ThrusterShare);
			spec->TurnRateDegS *= agile;
			spec->LateralThrustMN *= agile;
			spec->VerticalThrustMN *= agile;
			spec->PitchYawTorqueMNKm *= agile;
			spec->RollTorqueMNKm *= agile;
		}
		int plants = Count(l, UtilityKind::LifeSupport);
		if (plants > 0) spec->Crew += cs::Math::Max(0.0, l->Base->Crew) * cs::Math::Min(LifeSupportCap, plants * LifeSupportShare);
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
		return parts[c].Takes(*kind, l->SmallCraft);
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
