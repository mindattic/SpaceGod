#pragma once
// US-T164: battles that look like two nests of hornets -- the AI's manoeuvres, layered on the battle's own flight
// (Battle::Move, Battle::Dogfight, FighterAI) rather than replacing it.

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;

	/// <summary>
	/// How the battle's crews fly a fight so it reads as one (US-T164: "two nests of hornets", not two parked fleets).
	/// Every piece is AI only (a hull the player holds is flown by the player) and deterministic: phases and weaves come
	/// from the battle clock and each unit's own seed, never from wall time.
	/// <list type="bullet">
	/// <item>Fighters on a capital fly the strafing run (the owner's note on US-T164, Expanse/BSG style): burn hard for a
	/// point off one end of the hull, cut the drive and coast down its whole length with flight assist off (the US-T80/81
	/// free fall), the nose kept on the hull so the guns rake it end to end, then flip, burn and come round for the next pass
	/// from whichever end is nearer (<see cref="StrafeRun"/>).</item>
	/// <item>Fighters on fighters chase tails, and through the merge pull a loop or break into a rolling turn
	/// (<see cref="FighterTurn"/>); every small craft banks into its turns and rolls through its breaks (<see cref="Bank"/>).</item>
	/// <item>Corvettes, frigates and destroyers make passes at speed instead of parking at range, their noses weaving
	/// off the line of fire (<see cref="LightPass"/>, <see cref="Weave"/>).</item>
	/// <item>Capitals steam across the enemy's line broadside-on at their limited turn rate and come about from time to time,
	/// or when the side they have been fighting with is worn, to bring the fresh broadside and its shield arrays to bear
	/// (<see cref="Crossing"/>); WeakPatches (U.SPC-T502) rolls the hull about the line of fire as before, and since a hit
	/// lands on the shield patch facing it (BattleUnit::ShieldStrike in the hull's rolled frame), turning a face away
	/// really spares it.</item>
	/// </list>
	/// Each step is O(1) per unit apart from <see cref="Crossing"/>'s look at the worn face, which a capital takes once every
	/// <see cref="ComeAboutCheckS"/> seconds.
	/// </summary>
	class SPACEGODCORE_API Choreography final
	{
	public:
		/// <summary>From this length a hull fights as a capital: steams broadside-on and comes about.</summary>
		static constexpr double CapitalKm = 1.0;

		/// <summary>From this length up to <see cref="CapitalKm"/> an armed hull fights as a light warship: passes and weaves.</summary>
		static constexpr double LightMinKm = 0.2;

		/// <summary>The strafing run's line off the keel: this share of the hull's length plus this many km, so a pass reads
		/// clear of the hull at either scale (a small craft is drawn several times its size at game scale).</summary>
		static constexpr double StandoffShare = 0.15; static constexpr double StandoffKm = 0.3;

		/// <summary>How far past the hull's end the run lines up before it cuts the drive, km plus a share of the length.</summary>
		static constexpr double EntryLeadKm = 1.0; static constexpr double EntryLeadShare = 0.3;

		/// <summary>Seconds a strafer burns back after its flip before lining up the next pass.</summary>
		static constexpr double FlipSeconds = 3.5;

		/// <summary>The least speed a strafer cuts its drive at (km/s): slower, it would hang beside the hull, not pass it;
		/// and the longest it coasts, s.</summary>
		static constexpr double MinCoastKmS = 0.15; static constexpr double MaxCoastS = 30;

		/// <summary>How long a capital holds one broadside before it thinks of coming about, s (plus its own seed's share),
		/// how often it looks at how worn the engaged side is, and how worn (the WeakPatches exposure) sends it about early.</summary>
		static constexpr double ComeAboutS = 75; static constexpr double ComeAboutCheckS = 4; static constexpr double WornExposure = 0.35;

		/// <summary>How much a crossing capital's captain (WeakPatches' choice of attitude) favours a heading along its course,
		/// as a share of its battery: just over the choice's hysteresis, so coming about does swing the other broadside in,
		/// and far under what a holed face costs, so a worn side is still turned away whatever the course.</summary>
		static constexpr double CourseBias = 0.06;

		/// <summary>One step of a crew's stick: where it wants the nose, its throttle and boost, the drive cut (flight assist
		/// off, coasting), and a roll input.</summary>
		struct Flight
		{
			DVec3 Goal;
			double Throttle = 1;
			bool Boost = false;
			bool Coast = false;
			double Roll = 0;
		};

		/// <summary>A hull fighting as a capital (<see cref="CapitalKm"/> and up, a ship, not a platform).</summary>
		static bool IsCapital(const cs::Ref<BattleUnit>& u);

		/// <summary>An armed ship from <see cref="LightMinKm"/> up to <see cref="CapitalKm"/>: corvettes, frigates, destroyers.</summary>
		static bool IsLightWarship(const cs::Ref<BattleUnit>& u);

		/// <summary>A target a fighter strafes rather than dogfights: a hull big enough to run down the length of.</summary>
		static bool Strafeable(const cs::Ref<BattleUnit>& t);

		/// <summary>The unit is somewhere in a strafing run (burning in, coasting down the hull, or flipping out).</summary>
		static bool Strafing(const cs::Ref<BattleUnit>& u);

		/// <summary>The unit is coasting down a hull with its drive cut and its guns on it.</summary>
		static bool Coasting(const cs::Ref<BattleUnit>& u);

		/// <summary>Drops whatever run the unit was in (a new target, a break): it starts the next from the top.</summary>
		static void EndRun(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// One step of <paramref name="u"/>'s strafing run on <paramref name="t"/> (its state in BattleUnit::Maneuver,
		/// ManeuverLeft and BreakSide, the end of the hull the pass starts from). Burn: boosting for a point
		/// <see cref="EntryLeadKm"/> off the nearer end and <see cref="StandoffShare"/> off the keel, then along the pass line.
		/// Coast: drive cut, nose on the stretch of hull it is level with (a little ahead), until past the far end. Flip: nose
		/// onto the retrograde, burning back, for <see cref="FlipSeconds"/>; then the next pass. <paramref name="turnDegS"/>
		/// is how fast the craft swings its nose, which a flip waits on.
		/// </summary>
		static Flight StrafeRun(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double turnDegS, double dt);

		/// <summary>Where a coasting strafer's guns are laid: the point on <paramref name="t"/>'s keel level with
		/// <paramref name="from"/>, a little ahead of it along <paramref name="runDir"/>, kept on the hull.</summary>
		static DVec3 RakePoint(const cs::Ref<BattleUnit>& t, DVec3 from, DVec3 runDir);

		/// <summary>
		/// The point a small craft's fixed guns must bear on to engage <paramref name="target"/> (Battle::Fire's cone test):
		/// for a capital, the point on its keel nearest the line down the shooter's nose, so a strafer raking the hull's far
		/// end is firing at the hull and not holding fire for its centre; any other target, its centre.
		/// </summary>
		static DVec3 BearingPoint(const cs::Ref<BattleUnit>& shooter, const cs::Ref<BattleUnit>& target);

		/// <summary>
		/// A dogfighter on a fighter: whether it starts a loop or a rolling break now, out of the merge or with nothing on
		/// its tail for a while (state in BattleUnit::Maneuver); the loop pulls the nose up over the top until it points back
		/// at the target, the break turns hard one way rolling. Returns false (fly the ordinary chase) when neither is on.
		/// </summary>
		static bool FighterTurn(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double turnDegS, double dt, Flight& out);

		/// <summary>Banks a small craft the battle flies into its turns (its roof, BattleUnit::Roof, leaning toward the turn)
		/// and rolls it at <paramref name="rollDegS"/> on top, so it reads as flown, not slid.</summary>
		static void Bank(const cs::Ref<BattleUnit>& u, double rollDegS, double dt);

		/// <summary>
		/// A light warship's pass on its target, as a wanted velocity (km/s): round it at speed rather than holding at range,
		/// drawn in to <paramref name="rangeKm"/> and out again on a slow breathing cycle so it makes runs across the target.
		/// </summary>
		static DVec3 LightPass(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double rangeKm, double maxSpeed);

		/// <summary>A light warship's jink: its wanted facing swung off <paramref name="face"/> by a few degrees, back and
		/// forth and up and down on its own phase, as far as its turn rate makes visible.</summary>
		static DVec3 Weave(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, DVec3 face);

		/// <summary>
		/// A capital's crossing: the wanted velocity (km/s) that steams it across the line to its target, at its preferred
		/// range, broadside-on. It comes about (its BattleUnit::Circle reversed) once it has held one course for
		/// <see cref="ComeAboutS"/> or so, or sooner when the face it has been showing the enemy is worn past
		/// <see cref="WornExposure"/>, so its fresh side and shield arrays take the fight.
		/// </summary>
		static DVec3 Crossing(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double rangeKm, double maxSpeed, double dt);

		/// <summary>The way a crossing capital is steaming (unit), for Battle::BroadsideFacing to lay the bow along between
		/// broadsides that bear alike, so coming about turns the other side to the enemy; zero for any other hull.</summary>
		static DVec3 Course(const cs::Ref<BattleUnit>& u);
	};
}
