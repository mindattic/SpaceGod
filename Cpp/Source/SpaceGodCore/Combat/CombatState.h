#pragma once
// Port of Unity.SpaceGod Core/Combat/CombatState.cs (US-T10).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class SubsystemDef;
	class CombatSpec;
	class Ship;
	class Interior;
	class ModularHull;
	class PowerDistribution;
	enum class SubsystemKind : int;
	enum class ShipClass : int;
	enum class PhaserTuning : int;

	/// <summary>A hardpoint's live state: health, reload, what it is shooting at and where it points.</summary>
	class SPACEGODCORE_API Subsystem final
	{
	public:
		cs::Ref<SubsystemDef> Def;
		int Index = 0;
		double Hp = 0;
		double Cooldown = 0;
		int Ammo = 0;

		/// <summary>This shield emitter's own field charge and its cap (its rating's share of the hull's total),
		/// meaningful only for <see cref="SubsystemKind.ShieldGenerator"/>; set by <see cref="ShipCombat"/>. The field
		/// covers the emitter's own patch of hull (U.SPC-T501, see <see cref="ShieldEmitters"/>).</summary>
		double Shield = 0, MaxShield = 0;

		/// <summary>Battle-clock seconds this emitter's patch was last hit, for its own regen pause.</summary>
		double LastHitSeconds = -999;

		/// <summary>Work (hit points' worth) emergency repair crews have put into bringing this mount back since it was
		/// shot away (U.SPC-T501, see <see cref="Broadsides.StepDamageControl"/>); 0 while it stands.</summary>
		double Restoring = 0;

		/// <summary>World direction (ecliptic) the mount is aimed, for turret rendering; zero when idle.</summary>
		DVec3 Aim;

		Subsystem(const cs::Ref<SubsystemDef>& def, int index);

		SubsystemKind Kind() const;
		bool Destroyed() const { return Hp <= 0; }
		double Health() const;

		/// <summary>
		/// This emitter's live field as a fraction of its cap, 0 to 1: what its field glow on the hull and the bubble a
		/// hit raises on its patch are shown at (U.SPC-T40, U.SPC-T501). Zero for a collapsed or shot-away emitter,
		/// whose patch then shows no bubble, and for anything that is not a shield emitter.
		/// </summary>
		double ShieldFraction() const;
	private:
		Subsystem() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Subsystem); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// The damage a hull carries between battles: hull points, shields and every hardpoint. Repairs
	/// at a friendly yard restore it, paid for in alloys and electronics from the yard's market.
	/// </summary>
	/// <remarks>C# hands `this` to <see cref="CarrierWings"/>, which takes a cs::Ref, hence enable_shared_from_this (the
	/// constructor, before anything owns it, passes a non-owning alias instead).</remarks>
	class SPACEGODCORE_API ShipCombat final : public std::enable_shared_from_this<ShipCombat>
	{
	public:
		cs::Ref<CombatSpec> Spec;
		cs::List<cs::Ref<Subsystem>> Subsystems;
		double Hull = 0;

		/// <summary>
		/// The craft docked in the hangar (carriers, U.SPC-T526, <see cref="CarrierWings"/>), in berth order: each a hull of
		/// its own class with its own damage and pilot, launched as itself when a fight starts and docked again when it lands.
		/// Losses are made good at a yard. Empty for a hull with no hangar.
		/// </summary>
		/// <remarks>A shared list: a refit hands the same list on to the refitted hull (<see cref="Refitted"/>).</remarks>
		cs::Ref<cs::List<cs::Ref<Ship>>> Hangar = cs::New<cs::List<cs::Ref<Ship>>>();

		/// <summary>How many craft are docked aboard (<see cref="Hangar"/>). Setting it builds new craft for the first empty
		/// berths, or takes craft out from the last berths, until that many are aboard.</summary>
		int Fighters() const { return Hangar->Count(); }
		void SetFighters(int value);

		/// <summary>
		/// The boarding shuttles docked in the shuttle berths (carriers, U.SPC-T486, <see cref="BoardingShuttles"/>), in berth
		/// order: each an assault shuttle hull of its own with its own damage and pilot (U.SPC-T590, <see cref="CarrierWings"/>),
		/// sent out as itself and docked again when it lands. Losses are made good at a yard. Empty for a hull with no shuttles.
		/// </summary>
		/// <remarks>A shared list, as <see cref="Hangar"/>.</remarks>
		cs::Ref<cs::List<cs::Ref<Ship>>> ShuttleBay = cs::New<cs::List<cs::Ref<Ship>>>();

		/// <summary>How many boarding shuttles are docked aboard (<see cref="ShuttleBay"/>). Setting it builds new shuttles for
		/// the first empty berths, or takes shuttles out from the last berths, until that many are aboard.</summary>
		int Shuttles() const { return ShuttleBay->Count(); }
		void SetShuttles(int value);

		/// <summary>The player holds this station (a settlement's defence platform or a gate fort) at its
		/// <see cref="StationSeat"/> (U.SPC-T447): its guns are laid on the player's aim, in a fight or out of one. Kept on
		/// the station itself, so it stays held as a fight starts or ends around it.</summary>
		bool StationHeld = false;

		/// <summary>How the player has shared the hull's reactor between weapons, shields and engines at its helm, at one of its
		/// guns or at the station's controls (US-T94, <see cref="CraftPower"/>); null until the player first moves power, and
		/// read only while the player holds the hull, so a crew always fights at the even share it was designed for. Kept on
		/// the hull (and saved, an optional field older saves lack) so the split is still there when the player comes back.
		/// A small craft shares its own reactor through <see cref="BattleUnit.Power"/> instead.</summary>
		cs::Ref<PowerDistribution> Reactor;

		/// <summary>US-T97: seconds left on the self-destruct the player has armed aboard (<see cref="SelfDestruct"/>), below 0
		/// when none is; and set once it has gone off. Only the player ever arms one (no crew does), and neither is saved: a
		/// countdown under way when the game is saved is not one when it is loaded.</summary>
		double SelfDestructS = -1;
		bool SelfDestructed = false;

		/// <summary>Boost charge, 0-1: drains over <see cref="CombatSpec.BoostDurationS"/> while held and
		/// recharges (twice as slowly) otherwise. Shared by AI and player flight so boost behaves the same
		/// whoever is flying.</summary>
		double BoostFraction = 1;

		/// <summary>The inside of the hull: compartments, who is in them, and any boarding fight.</summary>
		cs::Ref<Core::Interior> Interior;

		/// <summary>
		/// How fit to fight the hull's war fleet is for what it has left of its propellant, munitions and spares (U.SPC-T93),
		/// 1 in supply down to <see cref="FleetSupply.ReadinessFloor"/> with nothing left: its guns reload that much slower in
		/// battle, its crew strike their colours sooner, and it mends itself in the field that much slower. Set each day by
		/// <see cref="MilitaryLogistics"/> for a fleet on campaign; 1 again at any port of its own.
		/// </summary>
		double Readiness = 1;

		/// <summary>How the captain has tuned the hull's phaser arrays (U.SPC-T488, <see cref="EnergyWeapons"/>): a narrow lance
		/// for hulls or a wide sweep for fighters and missiles, all its arrays at once.</summary>
		Core::PhaserTuning PhaserTuning{};

	private:
		cs::Ref<ModularHull> structure;

	public:
		/// <summary>The hull as the modules it is built from and the joints holding them together (U.SPC-T596, see
		/// <see cref="ModularHull"/>): where it is coming apart, beside <see cref="Hull"/>, which says whether it lives. Laid
		/// out from <see cref="Spec"/> the first time it is asked for, whole.</summary>
		cs::Ref<ModularHull> Structure();

		/// <summary>Whether <see cref="Structure"/> has been laid out yet (a hull nothing has asked about is whole).</summary>
		bool HasStructure() const { return structure != nullptr; }

		/// <param name="hull">The ship class, or null for a defence platform.</param>
		ShipCombat(const cs::Ref<CombatSpec>& spec, cs::Nullable<ShipClass> hull = std::nullopt);

		/// <param name="interior">The hull's inside, built from its facilities (<see cref="Interior.For(IReadOnlyList{FacilitySite}, double, double)"/>);
		/// null for its class's (or a platform's) as designed.</param>
		ShipCombat(const cs::Ref<CombatSpec>& spec, cs::Nullable<ShipClass> hull, const cs::Ref<Core::Interior>& interior);

		/// <summary>
		/// The same hull after a yard refit (U.SPC-T65) to <paramref name="spec"/>, whose hardpoint list lines up
		/// with the old one slot for slot: its people (the same <see cref="Interior"/>), the share of its hull
		/// still standing, its fighters and boost carry over, and so does every hardpoint but
		/// <paramref name="changed"/> (its health, rounds and its share of shield charge); the part fitted at
		/// <paramref name="changed"/> is new, whole and loaded. A refit that changes the hull's spaces (a facility that is a
		/// compartment of its own, U.SPC-T470) passes the new <paramref name="interior"/>.
		/// </summary>
		cs::Ref<ShipCombat> Refitted(const cs::Ref<CombatSpec>& spec, cs::Nullable<ShipClass> hull, int changed, const cs::Ref<Core::Interior>& interior = nullptr);

	private:
		/// <summary>The hull's shield generators' grades summed: each holds and regenerates its rating's share.</summary>
		double GeneratorRatings() const;

	public:
		/// <summary>
		/// Crippled but not destroyed: no working engines, a hull nearly gone, or a lost bridge on a
		/// badly holed ship. A disabled ship drifts and can be boarded.
		/// </summary>
		bool Disabled() const;

		/// <summary>Marines aboard and free to go boarding (those not already fighting off boarders).</summary>
		double MarinesAvailable() const;

		double HullFraction() const;
		bool Alive() const { return Hull > 0; }

		bool HasShieldGenerator() const;

		/// <summary>Total shield charge across every emitter — a coarse summary (HUD display, AI
		/// heuristics); combat itself drains the emitter whose patch is struck, via <see cref="EmitterFor"/>.</summary>
		double Shield() const;

		/// <summary>
		/// The shield emitter whose patch a point on the hull (ship lengths, hull frame: right, up, forward) lies in, live
		/// or shot away (U.SPC-T501): the emitter nearest to it in 3D, so each emitter's field covers the part of the hull
		/// nearer to it than to any other. Null for a hull with no emitters.
		/// </summary>
		cs::Ref<Subsystem> PatchAt(DVec3 local) const;

		/// <summary>The live shield emitter whose field covers a point on the hull (see <see cref="PatchAt"/>), or null when
		/// that patch's emitter has been shot away (the hull is holed there, however whole the rest is) or there is none.</summary>
		cs::Ref<Subsystem> EmitterFor(DVec3 local) const;

		void ZeroAllShields();

		/// <summary>Resets every generator's regen-pause timer (a ship entering a fresh battle starts able to regen).</summary>
		void ResetShieldTimers();

		/// <summary>Regrows every live emitter's field whose patch has not been hit for <see cref="ShieldEmitters.RegenDelay"/>
		/// battle-seconds, each toward its own cap, sharing the hull's total regen rate (times <see cref="ShieldEmitters.RegenRate"/>)
		/// across its emitters by their ratings (evenly, on a stock hull). The patch under fire stays down while the rest recover.</summary>
		/// <para><paramref name="capacity"/> (US-T94) scales each emitter's cap: the player's reactor share for the shields
		/// (<see cref="PowerDistribution.ShieldCapacity"/>), above 1 an overcharge, below 1 a field that bleeds down to it.
		/// A crew's hull always passes 1.</para>
		void RegenShields(double nowSeconds, double dt, double capacity = 1);

		bool Armed() const;

		/// <summary>Share of engines still working (1 if the hull has none).</summary>
		double EngineFactor() const;

		bool BridgeIntact() const;

		/// <summary>How hurt the hull is, 0 = pristine: the figure for repair and retreat decisions. Unlike <see cref="Damage"/> it does not
		/// thin out as more parts are fitted, so a hull at 30% reads 0.7 whatever it carries (U.SPC-T506).</summary>
		double HullDamage() const;

		/// <summary>Fraction of everything (hull and hardpoints) that is damaged, 0 = pristine; diluted by part count, so use <see cref="HullDamage"/> to decide how hurt a ship is.</summary>
		double Damage() const;

		/// <summary>Rough fighting strength for strategic planning: weapon damage per second, scaled by health.</summary>
		double Strength() const;

		/// <summary>Repairs up to <paramref name="share"/> of the hull and every hardpoint by the same share.
		/// Does not restock ammo or crew: callers that owe a cost for those (<see cref="War"/>'s yard refit)
		/// gate them separately; callers with no such cost (defence platforms) call <see cref="FreeResupply"/> too.</summary>
		void Repair(double share);

		/// <summary>Fully restocks ammo and musters crew/marines back to spec, free. Only for callers (defence
		/// platforms) with no costed resupply logic of their own; ships are resupplied via <see cref="War"/>'s refit.</summary>
		void FreeResupply();

		/// <summary>Builds new craft for up to <paramref name="count"/> of the hangar's empty berths (U.SPC-T526).</summary>
		void RestoreFighters(int count);

		/// <summary>Builds new boarding shuttles for up to <paramref name="count"/> of the empty shuttle berths (U.SPC-T486, U.SPC-T590).</summary>
		void RestoreShuttles(int count);
	private:
		ShipCombat() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(ShipCombat); // ObjectGraph registration (Persistence/Registrations)
	};
}
