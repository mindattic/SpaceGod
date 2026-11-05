#include "Target.h"
#include "Ships.h"
#include "Combat/Battle.h"
#include "Infrastructure/Infrastructure.h"

namespace SpaceGod
{
	using namespace SpaceGod::Core;

	Target::Target(TargetKind kind, int body, cs::Ref<Core::Ship> ship, cs::Ref<OrbitalStructure> structure,
		cs::Ref<DockingRing> ring, cs::Ref<Core::Battle> battle, cs::Ref<BattleUnit> unit)
		: Kind(kind), Body(body), Ship(std::move(ship)), Structure(std::move(structure)), Ring(std::move(ring)),
		  Battle(std::move(battle)), Unit(std::move(unit))
	{
	}

	const Target& Target::None()
	{
		static const Target none(TargetKind::None, -1, nullptr);
		return none;
	}

	Target Target::OfBody(int index) { return Target(TargetKind::Body, index, nullptr); }
	Target Target::OfShip(const cs::Ref<Core::Ship>& ship) { return ship == nullptr ? None() : Target(TargetKind::Ship, -1, ship); }
	Target Target::OfStructure(const cs::Ref<OrbitalStructure>& s) { return s == nullptr ? None() : Target(TargetKind::Structure, s->Body, nullptr, s); }
	Target Target::OfRing(const cs::Ref<DockingRing>& r) { return r == nullptr ? None() : Target(TargetKind::Ring, r->Body, nullptr, nullptr, r); }

	Target Target::OfUnit(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& unit)
	{
		return battle == nullptr || unit == nullptr ? None() : Target(TargetKind::Unit, battle->Body, nullptr, nullptr, nullptr, battle, unit);
	}

	bool Target::Equals(const Target& other) const
	{
		return Kind == other.Kind && Body == other.Body && Ship == other.Ship && Structure == other.Structure && Ring == other.Ring && Unit == other.Unit;
	}

	size_t Target::HashCode() const
	{
		// HashCode.Combine((int)Kind, Body, Ship?.Id ?? -1, Structure?.Name, Ring?.Id ?? -1, Unit?.Id ?? -1): only used
		// for hashing in the Runtime, so any good combination of the same parts does.
		size_t h = std::hash<int>{}(static_cast<int>(Kind));
		h = cs::HashCombine(h, std::hash<int>{}(Body));
		h = cs::HashCombine(h, std::hash<int>{}(Ship ? Ship->Id : -1));
		h = cs::HashCombine(h, std::hash<std::string>{}(Structure ? Structure->Name : std::string()));
		h = cs::HashCombine(h, std::hash<int>{}(Ring ? Ring->Id : -1));
		h = cs::HashCombine(h, std::hash<int>{}(Unit ? Unit->Id : -1));
		return h;
	}

	const char* ToString(Target::TargetKind kind)
	{
		switch (kind)
		{
		case Target::TargetKind::None: return "None";
		case Target::TargetKind::Body: return "Body";
		case Target::TargetKind::Ship: return "Ship";
		case Target::TargetKind::Structure: return "Structure";
		case Target::TargetKind::Ring: return "Ring";
		case Target::TargetKind::Unit: return "Unit";
		}
		return "";
	}
}
