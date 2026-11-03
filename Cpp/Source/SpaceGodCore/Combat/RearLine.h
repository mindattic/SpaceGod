#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/RearLine.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class RangeSetting;
	struct DVec3;
	enum class ShipClass : int;

	/// <summary>
	/// The dreadnought's doctrine, the rear line (U.SPC-T490, docs/SHIP_ROSTER.md): a battleship built around its spinal
	/// mounts (<see cref="SpinalMounts"/>) does not close with the enemy. It holds the rear of its own side, behind the line
	/// its escorts fight on, and snipes from the extreme reach of its spinal guns: it keeps <see cref="Standoff"/> of the
	/// reach of its shortest-ranged spinal mount between itself and its mark, so every one of them bears, holds its bow on
	/// the mark (the hull lays the guns), and backs off from any hostile hull that closes inside <see cref="GuardKm"/>. It
	/// picks the capital ships first (<see cref="CapitalBias"/>): their size is what a slug crossing fifty km is sure to
	/// find. Once its spinal mounts are shot away it fights as any warship does. Which classes fly it is data
	/// (<see cref="Register"/>); the battle asks <see cref="Flies"/>, <see cref="Steer"/> and <see cref="TargetBias"/>.
	/// Every figure is a setting in the menu's Broadsides group.
	/// </summary>
	/// [SettingsSource]
	class SPACEGODCORE_API RearLine
	{
		static cs::String Km(double v);

		static cs::String Percent(double v);

	public:
		static const cs::Ref<RangeSetting>& Standoff();

		static const cs::Ref<RangeSetting>& GuardKm();

		static const cs::Ref<RangeSetting>& RearBias();

		static const cs::Ref<RangeSetting>& CapitalBias();

		// ---------------------------------------------------------------- who flies it

	private:
		static cs::HashSet<ShipClass>& classes();

	public:
		/// <summary>Has hulls of <paramref name="cls"/> hold the rear line (or not): how a new dreadnought plugs in.</summary>
		static void Register(ShipClass cls, bool flies = true);

		/// <summary>Whether hulls of <paramref name="cls"/> hold the rear line.</summary>
		static bool Doctrine(ShipClass cls);

		/// <summary>Whether <paramref name="u"/> fights by the rear line now: a hull of a class that flies it, under its crew's
		/// command (the player lays his own guns), with a spinal mount still standing.</summary>
		static bool Flies(const cs::Ref<BattleUnit>& u);

		/// <summary>How far off its mark's surface <paramref name="u"/> holds, km: <see cref="Standoff"/> of its shortest
		/// standing spinal mount's reach.</summary>
		static double StandoffKm(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// Where <paramref name="u"/> wants to go and which way it points this step, holding the rear line against its
		/// <see cref="BattleUnit.Target"/> with <paramref name="speed"/> as its top speed. It takes a firing station
		/// <see cref="StandoffKm"/> off its mark, swung round toward its own side's rear (<see cref="RearBias"/>), backs away
		/// from any hostile hull inside <see cref="GuardKm"/>, keeps inside the fight, and holds its bow on the mark.
		/// </summary>
		static void Steer(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double speed, DVec3& desired, DVec3& face);

		/// <summary>How much nearer than it is a dreadnought counts <paramref name="o"/> when it picks its mark, km: a capital
		/// ship is what its spinal guns are for.</summary>
		static double TargetBias(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o);
	};
}
