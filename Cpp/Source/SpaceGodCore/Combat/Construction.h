#pragma once
// Port of Assets/UnitySpace/Core/Combat/Construction.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	enum class Good : int;
	enum class ShipClass : int;
	enum class ModuleKind : int;
	enum class MountType : int;
	class CombatSpec;
	class SubsystemDef;
	class OutfitSlot;
	class Loadout;
	class FacilitySite;
	class FacilityKind;
	class IPartSpec;
	class ShipCombat;
	class Interior;
	class Ship;

	/// <summary>The broad kind of thing a hull frame builds (U.SPC-T377): everything that can be built in space.</summary>
	enum class HullCategory : int
	{
		Fighter, SmallCraft, Freighter, Warship, Battleship, Carrier, Station, Satellite,

		/// <summary>A polity's unarmed service hulls: the lane tenders that keep its hyperlanes running (U.SPC-T481).</summary>
		Support,
	};
	SPACEGODCORE_API const char* ToString(HullCategory v);

	/// <summary>How big a hardpoint is (a fighter's gun, a destroyer's turret, a battleship's or a station's), and so the
	/// biggest part it takes (U.SPC-T508): a part's own size (<see cref="ModuleSpec.Size"/>) must be no bigger. A model
	/// library sizes the part it sets in the socket by it too.</summary>
	enum class HardpointSize : int { Small, Medium, Large };
	SPACEGODCORE_API const char* ToString(HardpointSize v);

	/// <summary>
	/// One hardpoint of a hull frame (U.SPC-T377): a place on the hull's surface -- where it sits (hull lengths, nose +Z,
	/// up +Y), which way it fires (its fire cone, U.SPC-T533), its type and size (Ballistic, Missile or Energy, or a shield or
	/// engine mount, U.SPC-T508: the kind and the biggest part it takes) and what the design
	/// builds onto it. The positions are the same ones the battle fires from, so a model library that sets a turret,
	/// launcher or tube on each socket puts it exactly where the fight thinks it is. What is inside the hull is not a
	/// hardpoint but a facility (<see cref="FacilitySite"/>, U.SPC-T470), with a size and no fire cone.
	/// </summary>
	class SPACEGODCORE_API Hardpoint final
	{
	public:
		/// <summary>The slot's index in the frame's <see cref="Loadout.Slots"/>.</summary>
		int SlotIndex = 0;

		/// <summary>The hardpoint's index in the design's <see cref="CombatSpec.Layout"/>.</summary>
		int LayoutIndex = 0;

		/// <summary>The kind of part it is reserved for (U.SPC-T508).</summary>
		MountType Type{};

		/// <summary>The biggest part it takes.</summary>
		HardpointSize Size{};
		double X = 0, Y = 0, Z = 0;
		bool FacesAft = false;

		/// <summary>The hull frame's own fire cone for the hardpoint (U.SPC-T533, see <see cref="FireCones"/>): its aiming
		/// direction in the hull frame and half-angle, deg; null where the mount's normal and its module's cone stand.</summary>
		cs::Nullable<DVec3> ConeAxis;

		cs::Nullable<double> ConeHalfDeg;

		/// <summary>A mount that bears on the bow.</summary>
		bool BearsOnBow = false;

		/// <summary>A mount that takes a spinal or fixed part (a railgun, a torpedo tube, the lance): one that bears on the
		/// bow, or a fixed launcher.</summary>
		bool Spinal = false;

		/// <summary>What the design builds here.</summary>
		cs::Nullable<ModuleKind> Stock;

		/// <summary>A stable name for the socket within its frame ("energy-2", "missile-0"), for a model library's prefab
		/// bindings and for saves.</summary>
		cs::String Socket;

		Hardpoint(int slotIndex, const cs::Ref<OutfitSlot>& slot, const cs::Ref<SubsystemDef>& def, cs::String socket);

		/// <summary>Whether a gun, launcher or tube goes here (a Ballistic, Missile or Energy mount; not a shield or an engine).</summary>
		bool IsWeaponMount() const;

		/// <summary>The key a model library binds a part to: what is fitted (or the mount's type, when empty) and the
		/// socket's size, e.g. "TorpedoTube/Large" or "EnergyMount/Medium".</summary>
		static cs::String ArtKey(const cs::Ref<Hardpoint>& h, cs::Nullable<ModuleKind> fitted);
	};

	/// <summary>
	/// A buildable hull (U.SPC-T377): a design and the parts it offers, for anything built in space -- each ship class's
	/// designs (fighters, couriers and breachers, freighters and ore haulers, destroyers and cruisers, battleships and
	/// carriers), stations and satellites alike. A frame has two kinds of part (U.SPC-T470): its hardpoints on the hull's
	/// surface (<see cref="Hardpoints"/>: guns, launchers, torpedo tubes, point defence, shields, engines, each with a
	/// position and a fire cone) and its facilities inside the body (<see cref="Facilities"/>: bridge, engineering, quarters,
	/// hangar, holds), with the free internal space beyond them (<see cref="SpareSpace"/>) for the facilities a yard fits.
	/// A yard fits them exactly as it refits a ship (<see cref="Outfitting"/>), and a <see cref="ConstructionPlan"/>
	/// builds one from scratch.
	/// </summary>
	class SPACEGODCORE_API HullFrame final
	{
	public:
		cs::String Id;
		cs::String Name;
		HullCategory Category{};

		/// <summary>The ship class it builds, or null for a station or satellite.</summary>
		cs::Nullable<ShipClass> Class;

		cs::Ref<CombatSpec> Design;

		/// <summary>The facilities the hull is designed with, laid out as its interior has them.</summary>
		cs::List<cs::Ref<FacilitySite>> Facilities;

		/// <summary>Free internal space beyond its own facilities, sections, for the facilities a yard fits.</summary>
		double SpareSpace = 0;

		bool SmallCraft = false;

		/// <summary>Build points (tonnes of alloys through a yard) to lay the hull down as designed.</summary>
		double BuildPoints = 0;

		/// <summary>The trade hold it is built with, tonnes.</summary>
		double CapacityTonnes = 0;

	private:
		// `IReadOnlyList<Hardpoint> hardpoints;`, null until first asked for (`??=`).
		mutable cs::Nullable<cs::List<cs::Ref<Hardpoint>>> hardpoints;

	public:
		/// <param name="facilities">The facilities it is designed with; null for its class's (<see cref="FacilityLayouts.For"/>),
		/// or a platform's for a station or satellite.</param>
		HullFrame(cs::String id, cs::String name, HullCategory category, cs::Nullable<ShipClass> cls, cs::Ref<CombatSpec> design, double spareSpace, bool smallCraft,
			double buildPoints, double capacityTonnes = 0, cs::Nullable<cs::List<cs::Ref<FacilitySite>>> facilities = std::nullopt);

		/// <summary>A static structure (a station, a satellite): it holds its orbit and has no drive to refit.</summary>
		bool Static() const;

		/// <summary>A fresh loadout of this frame as designed.</summary>
		cs::Ref<SpaceGod::Core::Loadout> NewLoadout() const;

		/// <summary>The hull's whole internal space, sections: its own facilities and the free space beyond them.</summary>
		double InternalSpace() const;

		/// <summary>Whether a facility of <paramref name="kind"/> could ever go into this hull: one a yard fits, allowed on
		/// its category, and no bigger than its free space.</summary>
		bool CanCarry(const cs::Ref<FacilityKind>& kind) const;

		/// <summary>Every hardpoint the frame offers, in its loadout's slot order.</summary>
		const cs::List<cs::Ref<Hardpoint>>& Hardpoints() const;

	private:
		cs::List<cs::Ref<Hardpoint>> MakeHardpoints() const;
	};

	/// <summary>
	/// The catalogue of everything that can be built in space (U.SPC-T377). Every ship class's designs are listed as
	/// frames ("ship/Battleship/Battleship"), along with the stations (the defence platform settlements and gates
	/// field, and an orbital station) and satellites (a weapons satellite and a sensor satellite). New hulls plug in
	/// with <see cref="Register"/>: a frame, never an edit to the code that builds the first.
	/// </summary>
	class SPACEGODCORE_API HullFrames final
	{
		static cs::List<cs::Ref<HullFrame>>& All();
		// new Dictionary<string, HullFrame>(StringComparer.OrdinalIgnoreCase)
		static cs::Dictionary<cs::String, cs::Ref<HullFrame>, cs::OrdinalIgnoreCaseComparer>& ById();

		/// The C# static constructor: run once, before any static member is used (each entry point calls it).
		static void StaticConstructor();

	public:
		/// <summary>Credits a yard charges per build point (as <see cref="Economy.HullPrice"/> does).</summary>
		static constexpr double PricePerBuildPoint = 1200;

		/// <summary>Adds a frame to the catalogue (or replaces the one with its id).</summary>
		static void Register(const cs::Ref<HullFrame>& frame);

		static const cs::List<cs::Ref<HullFrame>>& Catalogue();

		static cs::Ref<HullFrame> Of(const cs::String& id);

		/// <summary>The frames of one category.</summary>
		static cs::List<cs::Ref<HullFrame>> InCategory(HullCategory c);

		/// <summary>The frame a ship class's first (stock) design builds.</summary>
		static cs::Ref<HullFrame> ForClass(ShipClass c);

		static HullCategory CategoryOf(ShipClass c);

		// ---------------------------------------------------------------- stations and satellites

		/// <summary>A trade and habitat station's armed hub: heavy turrets about its ring, launchers, torpedo tubes
		/// facing out along its axis, point defence all round and room for eight sections of plate, racks and scanners.</summary>
		static const cs::Ref<CombatSpec>& OrbitalStation();

		/// <summary>An orbital weapons satellite: one laser turret, one missile launcher and point defence on a small
		/// shielded bus, holding its slot in orbit.</summary>
		static const cs::Ref<CombatSpec>& WeaponsSatellite();

		/// <summary>A sensor satellite: point defence for its own protection and room for a scanner suite.</summary>
		static const cs::Ref<CombatSpec>& SensorSatellite();
		SG_SERIALIZABLE(HullFrames); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>Why a construction plan's fitting did or did not go through (<see cref="NoRoom"/>: the facility is bigger
	/// than the hull's free internal space; <see cref="NoPower"/>, <see cref="NoCrew"/>: the hull has not the reactor power
	/// or the hands to spare for it, U.SPC-T521).</summary>
	enum class ConstructionOutcome : int { Ok, NoSuchSocket, WontFit, NoRoom, NoPower, NoCrew };
	SPACEGODCORE_API const char* ToString(ConstructionOutcome v);

	/// <summary>
	/// Building a hull from scratch (U.SPC-T377): pick a <see cref="HullFrame"/>, fit what goes on each of its hardpoints
	/// (a torpedo tube in a bow missile mount, a heavy laser for a turret on an energy one) and the facilities that go into its free internal space
	/// (plate, racks, a scanner suite, a brig, U.SPC-T470), and the plan gives the hull's fighting spec, its bill of
	/// materials and its price. A plan launches as a new ship's loadout (<see cref="Apply"/>) or as a station's or
	/// satellite's combat state (<see cref="BuildCombat"/>), its interior built from its facilities.
	/// </summary>
	class SPACEGODCORE_API ConstructionPlan final
	{
	public:
		cs::Ref<HullFrame> Frame;
		cs::Ref<SpaceGod::Core::Loadout> Loadout;

		explicit ConstructionPlan(cs::Ref<HullFrame> frame);

		const cs::List<cs::Ref<Hardpoint>>& Hardpoints() const;

		/// <summary>The facilities inside the hull as planned: the frame's own, then those fitted.</summary>
		const cs::List<cs::Ref<FacilitySite>>& Facilities() const;

		/// <summary>Internal space still free for another facility, sections.</summary>
		double FreeSpace() const;

		/// <summary>What is fitted on a hardpoint now.</summary>
		cs::Nullable<ModuleKind> FittedAt(int socket) const;

		/// <summary>Fits <paramref name="module"/> onto hardpoint <paramref name="socket"/>.</summary>
		ConstructionOutcome Fit(int socket, cs::Nullable<ModuleKind> module);

		/// <summary>Fits a facility of <paramref name="kind"/> into the hull's free internal space.</summary>
		ConstructionOutcome FitFacility(const cs::Ref<FacilityKind>& kind);

		/// <summary>Takes facility number <paramref name="index"/> out of the plan (a fitted one, not the frame's structure).</summary>
		ConstructionOutcome RemoveFacility(int index);

		/// <summary>Fits <paramref name="module"/> where it goes: a facility module (plate, racks, a scanner, a brig) into
		/// the free space, any other onto the first hardpoint that takes it and is still as designed. Returns the
		/// hardpoint's socket or the facility's index, or -1.</summary>
		int FitFirst(ModuleKind module);

		/// <summary>The hull's fighting spec as planned.</summary>
		cs::Ref<CombatSpec> Build() const;

	private:
		// The C# `(IPartSpec part, double sign)` tuple.
		struct SPACEGODCORE_API PartChange
		{
			cs::Ref<IPartSpec> part;
			double sign = 0;
		};

		/// <summary>The parts fitted over the design (+1) and the design's own taken out (-1).</summary>
		// An eager list of what the C# iterator yields: its callers only walk it.
		cs::List<PartChange> Changes() const;

	public:
		/// <summary>
		/// The materials the build takes, tonnes by good: the frame's alloys (its build points, which cover the design
		/// as drawn), plus every part fitted over the design, less what the design's own parts taken out would have used.
		/// </summary>
		cs::Dictionary<Good, double> Bill() const;

		/// <summary>The build's price at base prices: the frame's yard price plus the base worth of the parts fitted over
		/// the design, less those taken out.</summary>
		double BasePrice() const;

	private:
		static double Worth(const cs::Ref<IPartSpec>& spec, double scale);

		/// <summary>The hull's inside as planned: its facilities' compartments, with the design's crew and marines.</summary>
		cs::Ref<Interior> InteriorOf(const cs::Ref<CombatSpec>& spec) const;

	public:
		/// <summary>A static structure's (or any hull's) combat state as planned, whole and loaded.</summary>
		cs::Ref<ShipCombat> BuildCombat() const;

		/// <summary>Makes a newly built ship this plan: its loadout, its combat state and its hold. False if the frame
		/// builds no ship (a station or satellite) or not one of this ship's class.</summary>
		bool Apply(const cs::Ref<Ship>& ship) const;
	};
}
