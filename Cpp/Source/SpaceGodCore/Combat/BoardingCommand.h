#pragma once
// Port of Assets/UnitySpace/Core/Combat/BoardingCommand.cs (see docs/MIGRATION.md).

#include <tuple>

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Interior;

	/// <summary>Which side of a fight aboard a player commands: the party that came aboard, or the ship's own defenders.</summary>
	enum class BoardingRole : int
	{
		Boarders,
		Defenders,
	};
	const char* ToString(BoardingRole v);

	/// <summary>
	/// A standing order to one side of a fight aboard (U.SPC-T468, docs/BOARDING.md). <see cref="Auto"/> is the
	/// resolver's own choice and the auto-resolve: boarders push for the bridge and engineering, defenders muster
	/// on the fighting. The other orders change only where people move; every exchange of fire in a compartment is
	/// still the resolver's weighted roll.
	/// </summary>
	enum class BoardingOrder : int
	{
		Auto,

		/// <summary>Boarders: push for the objective compartment, then press on as the resolver would.
		/// Defenders: muster on the objective (to hold it before the boarders get there, or to retake it).</summary>
		Push,

		/// <summary>Stand fast where they are: boarders dig in on what they hold (waiting for the next wave),
		/// defenders hold their posts instead of mustering.</summary>
		Hold,
	};
	const char* ToString(BoardingOrder v);

	/// <summary>
	/// The player's side of a fight aboard (U.SPC-T468): which fight a battle unit can command, and giving it orders.
	/// The fight itself stays <see cref="Interior.Step"/>; with no orders given it runs exactly as it always has.
	/// </summary>
	class BoardingCommand
	{
	public:
		/// <summary>
		/// The fight aboard <paramref name="unit"/> can command, and on which side: its own hull's, defending, while
		/// it is being boarded; otherwise the hull its marines are boarding (the one it is latched to first, else any
		/// in the battle its faction is boarding). Null when there is no such fight.
		/// </summary>
		/// C# `(BattleUnit hull, BoardingRole role)?`: a nullable tuple (unpack with `auto [hull, role] = *r;`).
		static cs::Nullable<std::tuple<cs::Ref<BattleUnit>, BoardingRole>> FightFor(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit);

		/// <summary>The order standing for one side of a fight aboard, and its objective compartment (-1 for none).</summary>
		static std::tuple<BoardingOrder, int> OrdersOf(const cs::Ref<Interior>& interior, BoardingRole role);

		/// <summary>The next compartment after <paramref name="current"/> to pick as an objective, round the ship.</summary>
		static int NextObjective(const cs::Ref<Interior>& interior, int current);

		/// <summary>Plain words for an order, as the boarding panel shows it.</summary>
		static cs::String Label(const cs::Ref<Interior>& interior, BoardingRole role);
	};
}
