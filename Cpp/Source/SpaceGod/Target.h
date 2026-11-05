#pragma once

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class OrbitalStructure;
	class DockingRing;
	class Battle;
	class BattleUnit;
}

namespace SpaceGod
{
	/// Something the player can select and focus: a body, a ship, a structure in orbit or a docking ring; or, for the
	/// camera only, one unit in a battle that is no ship of the fleet (a launched fighter, a defence platform: U.SPC-T422).
	struct Target
	{
		enum class TargetKind : int
		{
			None,
			Body,
			Ship,
			Structure,
			Ring,
			Unit,
		};

		TargetKind Kind = TargetKind::None;
		int Body = 0;
		cs::Ref<Core::Ship> Ship;
		cs::Ref<Core::OrbitalStructure> Structure;
		cs::Ref<Core::DockingRing> Ring;

		/// For TargetKind::Unit: the battle and the unit in it.
		cs::Ref<Core::Battle> Battle;
		cs::Ref<Core::BattleUnit> Unit;

		/// default(Target): Kind None, Body 0 (C# zeroes a struct; Target::None() has Body -1).
		Target() = default;

	private:
		Target(TargetKind kind, int body, cs::Ref<Core::Ship> ship, cs::Ref<Core::OrbitalStructure> structure = nullptr,
			cs::Ref<Core::DockingRing> ring = nullptr, cs::Ref<Core::Battle> battle = nullptr, cs::Ref<Core::BattleUnit> unit = nullptr);

	public:
		static const Target& None();
		static Target OfBody(int index);
		static Target OfShip(const cs::Ref<Core::Ship>& ship);
		static Target OfStructure(const cs::Ref<Core::OrbitalStructure>& s);
		static Target OfRing(const cs::Ref<Core::DockingRing>& r);

		/// A unit in a battle, for the camera to ride with (a launched fighter the player flies, a platform whose gun they lay).
		static Target OfUnit(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& unit);

		bool IsNone() const { return Kind == TargetKind::None; }
		bool IsBody() const { return Kind == TargetKind::Body; }
		bool IsShip() const { return Kind == TargetKind::Ship; }
		bool IsStructure() const { return Kind == TargetKind::Structure; }
		bool IsRing() const { return Kind == TargetKind::Ring; }
		bool IsUnit() const { return Kind == TargetKind::Unit; }

		bool Equals(const Target& other) const;
		size_t HashCode() const;
		bool operator==(const Target& other) const { return Equals(other); }
		bool operator!=(const Target& other) const { return !Equals(other); }
	};

	const char* ToString(Target::TargetKind kind);
}
