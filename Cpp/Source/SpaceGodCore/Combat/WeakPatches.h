#pragma once
// Port of Assets/UnitySpace/Core/Combat/WeakPatches.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class Subsystem;
	class ShipCombat;
	class CombatSpec;
	class SubsystemDef;
	class Battle;
	class BattleUnit;
	class BroadsideCommand;
	class ToggleSetting;
	class RangeSetting;

	/// <summary>One shield emitter's patch of hull as its captain reads it (U.SPC-T502, see <see cref="WeakPatches"/>).</summary>
	struct HullPatch
	{
		/// <summary>The emitter whose field covers the patch (shot away when the patch is holed).</summary>
		cs::Ref<Subsystem> Emitter;

		/// <summary>Where on the hull it lies, as a captain names it: "bow", "starboard quarter", "port beam".</summary>
		cs::String Name;

		/// <summary>Its field, 0 (down) to 1 (full).</summary>
		double Field = 0;

		/// <summary>Its emitter is shot away: the hull is bare there until damage control brings it back.</summary>
		bool Holed = false;

		/// <summary>How weak it is, 0 (a full field over a whole emitter) to 1 (holed or its field down).</summary>
		double Weakness = 0;

		/// <summary>The share of the fire coming in on the hull that this patch faces, 0 to 1.</summary>
		double Fire = 0;

		HullPatch() = default;
		HullPatch(cs::Ref<Subsystem> emitter, cs::String name, double field, bool holed, double weakness, double fire)
		{
			Emitter = emitter;
			Name = name;
			Field = field;
			Holed = holed;
			Weakness = weakness;
			Fire = fire;
		}

		/// <summary>Weak enough for the helm to call it out: holed, or at least <see cref="WeakPatches.WeakAt"/> weak.</summary>
		bool Weak() const;
	};

	/// <summary>Fire coming in on a hull from one hostile: the way to it (unit, battle axes) and how heavy it is.</summary>
	struct IncomingFire
	{
		DVec3 Dir;
		double Weight = 0;

		IncomingFire() = default;
		IncomingFire(DVec3 dir, double weight)
		{
			Dir = dir;
			Weight = weight;
		}
	};

	/// <summary>
	/// Large ships roll to protect their weak spots (U.SPC-T502, docs/SHIP_ROSTER.md "Shields"). Shields are emitters on the
	/// hull, each covering its own patch of it (U.SPC-T501, <see cref="ShieldEmitters"/>), and capitals fight in full 3D
	/// (U.SPC-T499), so a captain can roll and turn its hull to keep a damaged, unshielded or holed patch away from the
	/// heaviest incoming fire and present fresh shields instead. It weighs where the fire is coming from (every armed hostile,
	/// heavier for its firepower, lighter beyond its reach, heavier with a broadside building on this hull,
	/// <see cref="IncomingFireOn"/>) against which patches face each way (<see cref="Exposure(BattleUnit, IReadOnlyList{IncomingFire})"/>),
	/// and trades that against keeping its own guns bearing (<see cref="Protection"/>): it rolls about the line to its
	/// target, which keeps the target where it lies in every gun's fire cone (U.SPC-T533), or turns its hull to another
	/// heading that brings other guns' cones onto the enemy if that shows it fresher shields for what firepower it gives up,
	/// and weighs too how slanted that attitude lies the hull to the fire, so hits glance off its armour (U.SPC-T534,
	/// <see cref="ArmourAngling"/>). An AI captain does this by doctrine (<see cref="AiRolls"/>); at the helm the player sees
	/// which patches are weak and which faces the fire (<see cref="Readout"/>), and can order the helm assist to roll them away
	/// (<see cref="BroadsideCommand.RollWeakPatches"/>), steering the nose themselves. Every figure is a setting in Broadsides.
	/// </summary>
	// [SettingsSource]
	class WeakPatches final
	{
	public:
		// ------------------------------------------------------------ settings

		static const cs::Ref<ToggleSetting>& AiRolls();

		static const cs::Ref<RangeSetting>& Protection();

		// ------------------------------------------------------------ constants

		/// <summary>How weak a patch must be for the helm to call it out.</summary>
		static constexpr double WeakAt = 0.5;

		/// <summary>How much a damaged emitter's own wounds count toward its patch's weakness (it is the next to go).</summary>
		static constexpr double EmitterDamageWeight = 0.25;

		/// <summary>How often a captain weighs the fire again, s, how much better a new attitude must be to be worth the
		/// roll, and how strongly it otherwise keeps to its rest (level, for an AI captain; where the helm has it, for the
		/// player).</summary>
		static constexpr double ReconsiderSeconds = 1, Hysteresis = 0.04, RestBias = 0.02;

		/// <summary>The rolls a captain weighs, evenly round the full turn.</summary>
		static constexpr int RollSteps = 12;

		/// <summary>No heading turned to a target: the hull rolls about its bow alone.</summary>
		static constexpr int NoBearing = cs::Int::MinValue;

		// ------------------------------------------------------------ who rolls

		/// <summary>A hull with two emitters or more: patches it can choose between.</summary>
		static bool HasPatches(const cs::Ref<ShipCombat>& c);

		/// <summary>Whether the battle's captain of <paramref name="u"/> rolls its weak patches away: a capital ship flown by
		/// its crew, with patches to choose between, under the doctrine and with protection on.</summary>
		static bool Steers(const cs::Ref<BattleUnit>& u);

		// ------------------------------------------------------------ the patches

		/// <summary>How weak an emitter's patch is: 1 holed, else its field's shortfall plus a share of the emitter's own
		/// damage, at most 1.</summary>
		static double Weakness(const cs::Ref<Subsystem>& emitter);

		/// <summary>The name a captain gives the patch around a hardpoint at <paramref name="d"/>: its side (or dorsal,
		/// ventral) and how far along the hull, as "starboard bow", "port beam", "stern".</summary>
		static cs::String NameOf(const cs::Ref<CombatSpec>& spec, const cs::Ref<SubsystemDef>& d);

		// ------------------------------------------------------------ the fire

	private:
		/// <summary>The longest reach of <paramref name="c"/>'s live guns and launchers, km.</summary>
		static double ReachKm(const cs::Ref<ShipCombat>& c);

	public:
		/// <summary>
		/// Where the fire on <paramref name="u"/> is coming from: every armed hostile still fighting, weighted by its
		/// firepower (<see cref="ShipCombat.Strength"/>), falling off with the square of the distance beyond its reach, and
		/// heavier for a broadside it is building on <paramref name="u"/>.
		/// </summary>
		static cs::List<IncomingFire> IncomingFireOn(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		// ------------------------------------------------------------ exposure

	private:
		/// <summary>A sample of the hull's surface (ship lengths, hull frame): where, its outward normal, and the area it stands for.</summary>
		struct Sample
		{
			DVec3 Point, Normal;
			double Area = 0;
		};

		/// <summary>The hull's surface sampled evenly, as the capsule <see cref="Collisions.SurfacePoint"/> strikes: rings
		/// along its length and its two rounded ends, each sample standing for its share of the area.</summary>
		static cs::List<Sample> SamplesOf(const cs::Ref<CombatSpec>& spec);

		/// <summary>Each sample's patch (its emitter's index in the hull's hardpoints, -1 for none).</summary>
		static cs::List<int> PatchesOf(const cs::Ref<ShipCombat>& c, const cs::List<Sample>& samples);

		/// <summary>How weak each sample's patch is.</summary>
		static cs::List<double> WeaknessOf(const cs::Ref<ShipCombat>& c, const cs::List<int>& patch);

		/// <summary>The weak share of the face a hull in frame (<paramref name="right"/>, <paramref name="up"/>,
		/// <paramref name="forward"/>) shows the fire, each hostile's by its weight: 0 when every patch the fire can see is
		/// whole, 1 when all are holed.</summary>
		static double Exposure(const cs::List<Sample>& samples, const cs::List<double>& weak, const cs::List<IncomingFire>& fire, DVec3 right, DVec3 up,
			DVec3 forward);

	public:
		/// <summary>The weak share of the face <paramref name="u"/>, as it lies now, shows <paramref name="fire"/>, 0 to 1.</summary>
		static double Exposure(const cs::Ref<BattleUnit>& u, const cs::List<IncomingFire>& fire);

		/// <summary>
		/// The helm's readout of <paramref name="u"/>'s patches (U.SPC-T502): each emitter's patch by name, its field,
		/// whether it is holed or weak, and the share of the fire coming in on the hull that it faces as the hull lies now.
		/// </summary>
		static cs::List<HullPatch> Readout(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		// ------------------------------------------------------------ the attitude

		/// <summary>The roof of a hull with its nose along <paramref name="forward"/> held level (toward ecliptic north), as
		/// <see cref="BattleUnit.Basis"/> holds one with no roll.</summary>
		static DVec3 LevelRoof(DVec3 forward);

	private:
		/// <summary>The frame of one attitude: with the hull turned so a target along <paramref name="toTarget"/> lies
		/// <paramref name="bearing"/> off its bow (<see cref="Battle.BowFor"/>), the level bow for it rolled
		/// <paramref name="rollDeg"/> about the line to the target (so the same guns' cones hold it all the same); with none,
		/// <paramref name="face"/> rolled about itself.</summary>
		static void Frame(DVec3 face, cs::Nullable<DVec3> toTarget, int bearing, double rollDeg, DVec3& forward, DVec3& roof);

	public:
		/// <summary>
		/// The attitude <paramref name="u"/>'s captain wants this step to keep its weak patches out of the heaviest fire,
		/// <paramref name="face"/> being where it would point its bow otherwise. With <paramref name="bearings"/> (the firepower
		/// its guns' fire cones would bring to bear from each heading, by the bearing the target would lie on,
		/// <see cref="Battle.ConeFirepower"/>) and a target along <paramref name="toTarget"/> it weighs every such heading, each
		/// rolled every way about the line to the target; with none, every roll about the bow alone. Each attitude scores the
		/// share of its guns' firepower that bears, less <see cref="Protection"/>
		/// times the weak share of the face it shows the fire (<see cref="IncomingFireOn"/>), less
		/// <see cref="ArmourAngling.AiWeight"/> times the share of the fire's harm its armour would let home at that slant
		/// (<see cref="ArmourAngling.Harm(CombatSpec, DVec3)"/>, U.SPC-T534), less a little for straying from
		/// <paramref name="rest"/> (null: level). The choice is weighed every <see cref="ReconsiderSeconds"/> and changed only
		/// for a better one by <see cref="Hysteresis"/>, so the hull settles instead of hunting.
		/// </summary>
		// `bearings` is C#'s nullable IReadOnlyDictionary reference: nullptr stands for null.
		static void Orient(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, DVec3 face, const cs::Dictionary<int, double>* bearings,
			cs::Nullable<DVec3> toTarget, cs::Nullable<DVec3> rest, double dt, DVec3& forward, DVec3& roof, cs::Nullable<DVec3> actual = std::nullopt);

	private:
		/// <summary>A hull still turning toward the heading it wants lies along <paramref name="actual"/>, not along
		/// <paramref name="forward"/>: carries the attitude chosen for the wanted heading over to the actual one (the same roll
		/// about the nose, by the shortest turn between them), so the roll is weighed and ordered for the heading the fire
		/// really meets, and blends into the wanted one as the hull comes round.</summary>
		static void Transport(cs::Nullable<DVec3> actual, DVec3& forward, DVec3& roof);

		static double Cost(const cs::Ref<ShipCombat>& c, const cs::List<Sample>& samples, const cs::List<double>& weak, const cs::List<IncomingFire>& fire,
			double protect, double slant, DVec3 f, DVec3 up);

		static void Choose(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BroadsideCommand>& cmd, DVec3 face,
			const cs::Dictionary<int, double>* bearings, cs::Nullable<DVec3> toTarget, cs::Nullable<DVec3> rest, cs::Nullable<DVec3> actual);

	public:
		/// <summary>The signed angle, radians, from <paramref name="from"/> to <paramref name="to"/> about the axis
		/// <paramref name="axis"/> (both taken square to it): positive is a right-handed roll about it.</summary>
		static double RollAngle(DVec3 from, DVec3 to, DVec3 axis);

		/// <summary>Rolls a hull the battle flies toward <paramref name="roof"/> about its nose, no faster than
		/// <paramref name="degPerSec"/>: the roll its captain orders (<see cref="BattleUnit.Roof"/>).</summary>
		static void RollToward(const cs::Ref<BattleUnit>& u, DVec3 roof, double degPerSec, double dt);

		/// <summary>
		/// At the helm, with the order given (<see cref="BroadsideCommand.RollWeakPatches"/>): the roll input, -1 to 1, the
		/// helm assist puts on to bring the hull (nose along <paramref name="forward"/>, roof <paramref name="up"/>) round to
		/// the roll that keeps its weak patches out of the heaviest fire. The player steers the nose; the assist only rolls,
		/// at up to <paramref name="rollRateDegS"/>, easing off so it stops on the roll at <paramref name="rollAlpha"/> (rad/s²).
		/// </summary>
		static double HelmRoll(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, DVec3 forward, DVec3 up, double rollRateDegS, double rollAlpha,
			double dt);

		/// <summary>Toggles the player's order to roll the weak patches away; returns whether it is now given.</summary>
		static bool ToggleOrder(const cs::Ref<BattleUnit>& u);
	};
}
