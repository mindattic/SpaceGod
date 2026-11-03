#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/ElectronicWarfare.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Missile;
	class RangeSetting;
	class Ship;

	/// <summary>
	/// Recon and electronic warfare (U.SPC-T485): what a <see cref="ShipClass.Recon"/> fighter does for its side. A carrier
	/// flies its recon flight (<see cref="CombatSpec.ReconCraft"/>) out ahead of its squadrons to a mark, the enemy capital its
	/// bombers are going for or else the biggest one in the fight, and holds off it at <see cref="StandoffKm"/>, outside the
	/// reach of its point defence. From there it
	/// <list type="bullet">
	/// <item><b>jams</b>: every enemy hull, platform and craft within <see cref="JamRangeKm"/> of it has its gunners' and point
	/// defence's accuracy cut by <see cref="JamAccuracy"/> (<see cref="JamFactor"/>; several jammers do not stack);</item>
	/// <item><b>paints</b> its mark for its own side's bombers while it is within <see cref="PaintRangeKm"/> of it
	/// (<see cref="Painted"/>): bombers go for a painted capital first, and their torpedoes home on it
	/// <see cref="PaintGuidance"/> times as hard and are drawn off by its decoys only a fraction as often
	/// (<see cref="PaintDecoyResist"/>), so a painted target is far easier for the torpedoes to hit;</item>
	/// <item><b>scouts</b>: berthed aboard, the recon flight stretches its carrier's own sensor reach
	/// (<see cref="SensorScanners"/>).</item>
	/// </list>
	/// A recon craft knocked out by a pulse or an EMP, struck or taken does none of it. Every figure is in the settings menu's
	/// Dogfighting group. The recon craft a battle has aloft are found once a step (<see cref="Sweep"/>), so a battle without
	/// one pays nothing for any of this.
	/// </summary>
	// [SettingsSource]
	class ElectronicWarfare
	{
		static cs::String Km(double v);

		static cs::String Percent(double v);

	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		static const cs::Ref<RangeSetting>& JamRangeKm();

		static const cs::Ref<RangeSetting>& JamAccuracy();

		static const cs::Ref<RangeSetting>& PaintRangeKm();

		static const cs::Ref<RangeSetting>& PaintGuidance();

		static const cs::Ref<RangeSetting>& PaintDecoyResist();

		/// <summary>How far off its mark a recon fighter holds, km: outside point defence's reach.</summary>
		static constexpr double StandoffKm = 6;

		/// <summary>How much nearer than it is a painted capital counts for a bomber choosing its target, km.</summary>
		static constexpr double PaintBonusKm = 10;

		/// <summary>What each recon craft berthed aboard a carrier is worth to its sensor reach, in scanner suites (see
		/// <see cref="SensorSuite.Of(Ship)"/>): the pair a carrier carries add one suite's half again of range.</summary>
		static constexpr double SensorScanners = 0.5;

		/// <summary>Whether <paramref name="u"/> is a recon fighter: a launched recon craft, or a recon hull flown as a ship.</summary>
		static bool IsRecon(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether recon craft <paramref name="r"/> is at work: there, its systems up, not struck and not taken.</summary>
		static bool Working(const cs::Ref<BattleUnit>& r);

		/// <summary>Finds the recon craft <paramref name="b"/> has in the fight, once a step.</summary>
		static void Sweep(const cs::Ref<Battle>& b);

		/// <summary>What <paramref name="u"/>'s accuracy is multiplied by: 1 less <see cref="JamAccuracy"/> while a working
		/// enemy recon craft is within <see cref="JamRangeKm"/> of it, else 1.</summary>
		static double JamFactor(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="u"/> is being jammed.</summary>
		static bool Jammed(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="target"/> is painted for side <paramref name="side"/>: a working recon craft of that
		/// side has it as its mark and is within <see cref="PaintRangeKm"/> of it.</summary>
		static bool Painted(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& target, int side);

		/// <summary>How much harder guided round <paramref name="m"/> steers: <see cref="PaintGuidance"/> for a strike craft's
		/// torpedo on a painted target, else 1.</summary>
		static double Guidance(const cs::Ref<Battle>& b, const cs::Ref<Missile>& m);

		/// <summary>The chance a flare-and-chaff cloud draws off <paramref name="m"/>: the countermeasures' own
		/// (<see cref="Countermeasures.SeduceChance"/>), cut by <see cref="PaintDecoyResist"/> for a strike torpedo on a
		/// painted target.</summary>
		static double SeduceChance(const cs::Ref<Battle>& b, const cs::Ref<Missile>& m);

		/// <summary>
		/// The mark a recon craft goes for: the enemy capital the nearest of its own side's bombers is attacking, else the
		/// hostile it would most help to paint (a capital before anything smaller, the biggest and nearest first), never a
		/// fighter, nor a hull that has struck or been taken; null with nothing to mark.
		/// </summary>
		static cs::Ref<BattleUnit> ChooseMark(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>Recon craft docked aboard <paramref name="s"/> (U.SPC-T526): its recon flight, those of it still berthed
		/// (the recon flight launches with the squadrons and lands with the rest).</summary>
		static int ReconAboard(const cs::Ref<Ship>& s);
	};
}
