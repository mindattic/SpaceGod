#pragma once
// US-T307: the player's "lock attacker" key, picking who to lock from who is engaging the hull.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;

	/// <summary>
	/// The hostiles engaging a hull (<see cref="BattleUnit.Target"/> on it), for the lock-attacker key (US-T307). Read only:
	/// it picks a lock for the player and never changes what anyone in the fight does, so AI fights and the Bench are unchanged.
	/// </summary>
	class SPACEGODCORE_API Attackers
	{
	public:
		/// <summary>The hostiles in <paramref name="battle"/> targeting <paramref name="me"/>, present and still fighting
		/// (not struck or taken), nearest first.</summary>
		static cs::List<cs::Ref<BattleUnit>> Of(const Battle& battle, const cs::Ref<BattleUnit>& me);

		/// <summary>
		/// The lock the key gives: with <paramref name="current"/> among <see cref="Of"/>, the next of them round (so the key
		/// cycles through everyone engaging the hull); else the nearest of them. With nobody engaging, the nearest hostile
		/// still fighting. <paramref name="engaging"/> says whether the pick is engaging the hull. Null when there is none.
		/// </summary>
		static cs::Ref<BattleUnit> Next(const Battle& battle, const cs::Ref<BattleUnit>& me, const cs::Ref<BattleUnit>& current, bool& engaging);
	};
}
