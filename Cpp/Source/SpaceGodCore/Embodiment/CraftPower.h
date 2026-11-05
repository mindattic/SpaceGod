#pragma once
// US-T94: power distribution on whatever the player embodies. C++-only (no Unity.SpaceGod counterpart).

#include "Cs/Cs.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	class PowerDistribution;

	/// <summary>A key at the controls that moves reactor power (US-T94), as the Runtime reads it.</summary>
	enum class PowerKey : int
	{
		None = -1,
		/// <summary>A pip to weapons, shields or engines, from whichever other system has the most.</summary>
		Weapons = 0, Shields = 1, Engines = 2,
		/// <summary>The presets: all the reactor can spare to one system (4/1/1), or an even share (2/2/2).</summary>
		AllWeapons = 3, AllShields = 4, AllEngines = 5, Balance = 6,
	};

	/// <summary>
	/// The reactor split of whatever the player embodies (US-T94), the classic three-way pips (<see cref="PowerDistribution"/>:
	/// six pips, at most four to one system, two each at an even share):
	/// <list type="bullet">
	/// <item>Weapons: every gun's reload (its rate of fire) and, for the player's guns, how fast their heat bleeds away, a
	/// quarter faster per pip over two (x0.5 .. x1.5).</item>
	/// <item>Shields: how fast the emitters regrow, the same quarter a pip, and how much field they hold, an eighth a pip
	/// (x0.75 .. x1.25 of the design's cap; power taken off bleeds the excess away).</item>
	/// <item>Engines: top speed (flight assist's set point), every thruster's push (acceleration) and the boost's recharge,
	/// a quarter a pip.</item>
	/// </list>
	/// A fighter seat shares the craft's own reactor (<see cref="BattleUnit.Power"/>, U.SPC-T424); a ship's helm, a gun on a
	/// hull and a station's controls share the hull's (<see cref="ShipCombat.Reactor"/>), a gun drawing on the hull it is
	/// mounted on. The hull's split counts only while the player holds the hull (its helm, one of its guns, the station), so
	/// a crew always fights at the even share, as it always has. The split travels as a whole (<see cref="EmbodimentInput.PowerSplit"/>)
	/// in the pilot's input, so co-op and a replay land it the same, and it is saved with the craft.
	/// </summary>
	class SPACEGODCORE_API CraftPower final
	{
	public:
		/// <summary>The split <paramref name="seat"/> draws on, made (even) when <paramref name="create"/> and there is none
		/// yet; null for a seat with no reactor to share (a satellite).</summary>
		static cs::Ref<PowerDistribution> Of(const cs::Ref<IEmbodiable>& seat, bool create);

		/// <summary>Whether <paramref name="seat"/> has a reactor to share at all (with no split set yet it runs even).</summary>
		static bool Shares(const cs::Ref<IEmbodiable>& seat);

		/// <summary>The split <paramref name="key"/> (a <see cref="PowerKey"/>) asks for on <paramref name="seat"/>, as a
		/// <see cref="PowerDistribution.Code"/>, or -1 for no change (no key, no reactor, a pip it cannot move).</summary>
		static int After(const cs::Ref<IEmbodiable>& seat, int key);

		/// <summary>Sets <paramref name="seat"/>'s split to <paramref name="code"/> (no change for -1 or an illegal code).</summary>
		static void Apply(const cs::Ref<IEmbodiable>& seat, int code);
	};
}
