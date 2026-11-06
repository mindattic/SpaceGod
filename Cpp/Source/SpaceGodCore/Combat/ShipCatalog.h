#pragma once
// The full ship catalog (US-T193): one row per hull, the single table its handling is read from.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	enum class ShipClass : int;
	enum class PilotControl : int;
	enum class FlightFamily : int;
	enum class HullCategory : int;
	enum class HardpointSize : int;
	enum class MountType : int;
	enum class ModuleKind : int;
	class CombatSpec;
	class FacilityKind;

	/// <summary>
	/// How a hull handles (US-T193): the authored numbers flight assist holds it to, under a player's hands and a crew's
	/// alike (<see cref="PilotTuning.Of"/> reads them). Speeds are km/s and rates deg/s: true units, the same at either
	/// scale, since the gameplay scale (US-T179) is a linear map applied when the scene is drawn.
	/// </summary>
	struct HullHandling
	{
		/// <summary>Flight assist's set point with the throttle floored.</summary>
		double TopSpeedKmS = 0;

		/// <summary>The fastest the nose swings in pitch and yaw.</summary>
		double TurnRateDegS = 0;

		/// <summary>The fastest flight assist lets the hull roll.</summary>
		double RollRateDegS = 0;

		/// <summary>The afterburner's set-point multiplier and how long a full charge burns, s.</summary>
		double BoostSpeedMultiplier = 0, BoostDurationS = 0;

		/// <summary>The cruise drive's thrust and set-point multiplier, and its spool from rest, s.</summary>
		double CruiseSpeedMultiplier = 0, CruiseRampS = 0;
	};

	/// <summary>What an <see cref="EquipmentSlot"/> takes (US-T193): the general slot model the weapon systems (US-T189)
	/// and every later fitting plug into.</summary>
	enum class SlotCategory : int
	{
		/// <summary>A gun, launcher or spinal mount on the hull's skin.</summary>
		Weapon,
		/// <summary>A shield generator's mount.</summary>
		Shield,
		/// <summary>A drive's mount.</summary>
		Engine,
		/// <summary>A section of free internal space a yard fits a facility into (plate, racks, a scanner, a brig).</summary>
		Utility,
	};
	SPACEGODCORE_API const char* ToString(SlotCategory v);

	/// <summary>
	/// One place a part goes on a hull (US-T193): a hardpoint on its skin (<see cref="LayoutIndex"/> into the design's
	/// layout, with its <see cref="Mount"/> type and <see cref="Size"/>) or a section of its free internal space. Whether a
	/// part goes here is <see cref="Accepts"/>, which defers to the outfitting rules (<see cref="ModuleSpec.Fits"/>), so a
	/// new module kind is fittable by its own data, never by an edit here.
	/// </summary>
	struct SPACEGODCORE_API EquipmentSlot
	{
		SlotCategory Category{};

		/// <summary>The hardpoint's mount type; null for a utility slot.</summary>
		cs::Nullable<MountType> Mount;

		/// <summary>The biggest part it takes.</summary>
		HardpointSize Size{};

		/// <summary>It takes a spinal or fixed part (a mount bearing on the bow, or a spinal mount).</summary>
		bool Spinal = false;

		/// <summary>The hardpoint's index in the design's layout; -1 for a utility slot.</summary>
		int LayoutIndex = -1;

		/// <summary>What the design builds here; null for an empty utility section.</summary>
		cs::Nullable<ModuleKind> Stock;

		/// <summary>"Medium energy mount", "Utility section".</summary>
		cs::String Name;

		/// <summary>Whether module <paramref name="m"/> goes in this slot.</summary>
		bool Accepts(ModuleKind m) const;
	};

	/// <summary>What a <see cref="CatalogCompartment"/> is for (US-T193).</summary>
	enum class CompartmentRole : int { Cargo, Hangar, Crew, Reactor, Command, Structure };
	SPACEGODCORE_API const char* ToString(CompartmentRole v);

	/// <summary>One space inside a hull as designed (US-T193): a facility of its layout (<see cref="FacilityLayouts.For"/>).</summary>
	struct SPACEGODCORE_API CatalogCompartment
	{
		CompartmentRole Role{};

		/// <summary>The facility kind's id ("hold", "hangar").</summary>
		cs::String Kind;

		/// <summary>The name the space goes by aboard.</summary>
		cs::String Name;

		/// <summary>Internal space it takes, sections.</summary>
		double Size = 0;

		/// <summary>The role of a facility of kind <paramref name="id"/>.</summary>
		static CompartmentRole RoleOf(const cs::String& id);
	};

	/// <summary>One design of a hull class (US-T193): its own drive, so two designs of a class accelerate and brake differently.</summary>
	struct SPACEGODCORE_API CatalogDesign
	{
		cs::String Name;
		double LengthKm = 0, MassKt = 0;

		/// <summary>The piloted drive (<see cref="HullDynamics"/>): main thrust, retros and manoeuvre thrusters, km/s².</summary>
		double AccelKmS2 = 0, BrakeKmS2 = 0, StrafeKmS2 = 0;

		/// <summary>The battle AI's kinematics (<see cref="CombatSpec.Accel"/>, <see cref="CombatSpec.Decel"/>), km/s².</summary>
		double BattleAccelKmS2 = 0, BattleBrakeKmS2 = 0;

		double Hull = 0, Shield = 0, Armor = 0;

		static CatalogDesign Of(const cs::Ref<CombatSpec>& spec);
	};

	/// <summary>
	/// One hull in the catalog (US-T193): its authored row (<see cref="Handling"/>, <see cref="Control"/>, <see cref="Role"/>)
	/// and everything read from the hull's own data -- its designs' drives, the slots of its lead design's hardpoints and
	/// free space (<see cref="Loadout"/>), and the compartments of its layout.
	/// </summary>
	class SPACEGODCORE_API ShipCatalogEntry final
	{
	public:
		ShipClass Class{};
		cs::String Name;

		/// <summary>One line on what it is for.</summary>
		cs::String Role;

		HullHandling Handling;
		PilotControl Control{};
		FlightFamily Family{};
		HullCategory Category{};
		bool SmallCraft = false;

		/// <summary>Its designs, the lead (<see cref="CombatSpec.Of"/>) first.</summary>
		cs::List<CatalogDesign> Designs;

		/// <summary>The lead design's slots: its hardpoints in layout order, then a utility slot per section of free space.</summary>
		cs::List<EquipmentSlot> Slots;

		/// <summary>The compartments it is designed with, in layout order.</summary>
		cs::List<CatalogCompartment> Compartments;

		/// <summary>Totals over the lead design: trade hold, tonnes; hangar berths (craft and shuttles); crew and marines;
		/// the reactor's spare output for fittings, MW; internal space and the free part of it, sections.</summary>
		double CargoTonnes = 0;
		int HangarCraft = 0;
		double Crew = 0, Marines = 0, ReactorMw = 0, InternalSpace = 0, SpareSpace = 0;

		/// <summary>The lead design (the catalog's acceleration and braking figures).</summary>
		const CatalogDesign& Lead() const { return Designs[0]; }

		int SlotCount(SlotCategory c) const;

		int CompartmentCount(CompartmentRole r) const;
	};

	/// <summary>
	/// Every hull in the game in one table (US-T193): each class's handling (top speed, turn, roll, boost and cruise),
	/// how it is flown at the helm and what it is for are authored here once, and nowhere else -- <see cref="PilotTuning"/>
	/// reads them for the player and for every AI crew. Acceleration and braking are each design's own drive (thrust over
	/// mass, <see cref="CombatSpec"/>), equipment slots its hardpoints and free space (<see cref="Loadout"/>), compartments
	/// its facility layout (<see cref="FacilityLayouts"/>): the catalog gathers them rather than keeping a second copy.
	/// Pure data, built once, so it is deterministic. The Help screen's ship catalog page browses it.
	/// </summary>
	class SPACEGODCORE_API ShipCatalog final
	{
	public:
		/// <summary>The authored row of class <paramref name="c"/>; false (and nothing set) for a class with none.</summary>
		static bool TryHandling(ShipClass c, HullHandling& handling);

		/// <summary>How class <paramref name="c"/> is flown at the helm; a class with no row flies as a capital.</summary>
		static PilotControl ControlOf(ShipClass c);

		/// <summary>Every class in the table, in catalog order (strike craft, then working hulls, then warships).</summary>
		static const cs::List<ShipClass>& Classes();

		/// <summary>Every hull's entry, in catalog order (built the first time asked).</summary>
		static const cs::List<cs::Ref<ShipCatalogEntry>>& Entries();

		/// <summary>The entry of class <paramref name="c"/>, or null.</summary>
		static cs::Ref<ShipCatalogEntry> Of(ShipClass c);

	private:
		static cs::Ref<ShipCatalogEntry> Build(ShipClass c);
	};
}
