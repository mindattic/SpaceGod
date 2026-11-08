#pragma once
// US-T96: a hull's broadcast array -- the comms mast it reports an attack with. Knock it out and the victim cannot tell
// the law who set on it.
//
// Every hull fought as a ship carries one (a fighter launched from a hangar does not, nor a platform or fort, whose
// traffic control always reports). It is not one of the hull's hardpoints (ShipCombat.Subsystems): no design lays it down
// and no crew aims at it, so nothing changes for a fight the player is not in. It is made, whole, the first time the
// player picks it as the weak point to go for on the hull locked (FighterSeat.CycleWeakPoint, key 6), and lives for the
// battle on its unit (BattleUnit.Broadcast). Only the player's own fire on that hull reaches it: guided rounds home on it
// as on any weak point picked, and the guns' hits land on it when they strike near enough to it and nearer it than to
// any hardpoint. A shield over its patch takes the hits first, as anywhere on the hull.
//
// When the battle ends (Law.OnBattleEnded), a victim whose array is down cannot be the one that reports the attack; a
// station or another ship that saw the fight from outside it still can (Law.FindWitness). With nobody else in range, the
// crime goes unwitnessed and the record of the pilot or ship the player is embodied in stays clean.
//
// No random draws and nothing read but the battle's own state: every lockstep peer and every replay agrees.

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class BattleUnit;
	class Subsystem;

	class SPACEGODCORE_API BroadcastArrays
	{
	public:
		/// <summary>The array's hit points: a mast and dish out on the spine, not armoured like a gun mount.</summary>
		static constexpr double Hp = 90;

		/// <summary>Where it stands on the hull, in hull lengths (right, up, forward): on the spine, a little forward.</summary>
		static constexpr double LocalX = 0;
		static constexpr double LocalY = 0.08;
		static constexpr double LocalZ = 0.15;

		/// <summary>How near the array a gun's hit must land to strike it, in hull lengths.</summary>
		static constexpr double ReachShare = 0.35;

		/// <summary>Whether <paramref name="u"/> carries a broadcast array (a hull fought as a ship).</summary>
		static bool Carries(const cs::Ref<BattleUnit>& u);

		/// <summary><paramref name="u"/>'s array, made whole the first time it is asked for; null for a unit that has none.</summary>
		static cs::Ref<Subsystem> Of(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="s"/> is <paramref name="u"/>'s array.</summary>
		static bool Is(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s);

		/// <summary>Whether <paramref name="u"/>'s array has been knocked out: it cannot report what was done to it.</summary>
		static bool Silenced(const cs::Ref<BattleUnit>& u);

		/// <summary><paramref name="u"/>'s array's health, 0 to 1 (whole while it has not been made).</summary>
		static double Health(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// What a gun's hit at <paramref name="at"/> on <paramref name="hit"/> strikes, given the hardpoint nearest it
		/// (<paramref name="nearest"/>, or null): the array, if <paramref name="shooter"/> is the player's craft with the array
		/// picked on that locked hull and the hit lands within <see cref="ReachShare"/> of it and nearer it than to the
		/// hardpoint; else the hardpoint.
		/// </summary>
		static cs::Ref<Subsystem> Struck(const cs::Ref<BattleUnit>& shooter, const cs::Ref<BattleUnit>& hit, DVec3 at, const cs::Ref<Subsystem>& nearest);

		/// <summary>The next (or previous) pick on <paramref name="target"/> after <paramref name="current"/>, wrapping: its
		/// weak points (<see cref="WeakPoints.On"/>), then its array while it stands. Null if there is nothing to pick.</summary>
		static cs::Ref<Subsystem> Cycle(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& current, int step);
	};
}
