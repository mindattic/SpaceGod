#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/GateBreakers.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class Faction;
	class JumpGate;
	class BattleUnit;
	class RangeSetting;
	enum class ShipClass : int;

	/// <summary>
	/// Gate breakers (U.SPC-T478): the siege capital built to take a jump gate intact (<see cref="ShipClass.GateBreaker"/>).
	/// It is the most heavily armoured hull afloat, so it can take a gate's forts' fire as it closes, and it fights them at
	/// close range with brawling batteries of plasma cannons; in a gate battle it goes for the forts before anything else
	/// (<see cref="FortFocus"/>), and its two boarding shuttles put marines aboard any fort the fight silences. It carries the
	/// teams a gate is taken with: boarding teams (the biggest marine complement afloat) and engineering teams (its siege
	/// engineering bay, <see cref="FacilityKinds.SiegeEngineering"/>). It always has its own FTL drive, since it attacks the
	/// gates the rest of the fleet would use.
	/// <para>On campaign (see <see cref="InterstellarWars"/>), a war fleet whose group has a gate breaker with its engineering
	/// bay and a boarding team's worth of marines aboard (<see cref="TeamMarines"/>) storms an enemy gate on the first day its
	/// forts are silenced and takes it intact: its engineers make good the damage the ring has taken (<see cref="FieldRepair"/>),
	/// so a ring the enemy sabotaged is back in service at once rather than waiting years for its owner's constructors. It
	/// never wrecks a gate (owner 2026-10-24: no gate wrecking yet).</para>
	/// <para>A navy at war lays down a gate breaker at its next keel when it has fewer than <see cref="PerNavy"/> afloat. Which
	/// classes break gates is data (<see cref="Classes"/>, <see cref="Register"/>). Every figure is a
	/// setting in the menu's Broadsides and AI groups.</para>
	/// </summary>
	/// [SettingsSource]
	class SPACEGODCORE_API GateBreakers
	{
		static cs::String Count(double v);

		static cs::String Percent(double v);

	public:
		static const cs::Ref<RangeSetting>& TeamMarines();

		static const cs::Ref<RangeSetting>& FieldRepair();

		static const cs::Ref<RangeSetting>& FortFocus();

		static const cs::Ref<RangeSetting>& PerNavy();

		// ---------------------------------------------------------------- who breaks gates

	private:
		static cs::HashSet<ShipClass>& classes();

	public:
		/// <summary>The classes that break gates: a hull of one with its siege engineering bay is a gate breaker.</summary>
		static const cs::HashSet<ShipClass>& Classes();

		/// <summary>Makes hulls of <paramref name="cls"/> gate breakers (or not): how a new siege class plugs in.</summary>
		static void Register(ShipClass cls, bool breaks = true);

		/// <summary>Whether hulls of <paramref name="cls"/> break gates.</summary>
		static bool IsClass(ShipClass cls);

		/// <summary>Whether <paramref name="s"/> is a gate breaker able to work on a gate: a hull of a gate-breaking class,
		/// afloat and in fighting order, with its siege engineering bay (its refit's, or its design's as laid down).</summary>
		static bool Works(const cs::Ref<Ship>& s);

		/// <summary>The gate breaker in <paramref name="group"/> whose boarding and engineering teams can storm a silenced
		/// gate and take it intact (<see cref="TeamMarines"/> aboard), or null.</summary>
		static cs::Ref<Ship> Stormer(const cs::List<cs::Ref<Ship>>& group);

		/// <summary>Days a war fleet with a gate breaker's teams holds a silenced gate before it is theirs: they storm it the
		/// first day, whatever <paramref name="holdDays"/> it would hold otherwise.</summary>
		static double GateHoldDays(double holdDays);

		/// <summary>
		/// A gate breaker's engineering teams make good <see cref="FieldRepair"/> of the damage <paramref name="gate"/>'s ring
		/// has taken as they seize it; returns how much of the ring they put back (0..1).
		/// </summary>
		static double MakeGood(const cs::Ref<JumpGate>& gate);

		// ---------------------------------------------------------------- in battle

		/// <summary>How much nearer (km) <paramref name="u"/>, a gate breaker under its crew's command, counts a gate's fort
		/// <paramref name="o"/> when picking its mark: it is there to break the gate's guns first.</summary>
		static double TargetBias(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o);

		// ---------------------------------------------------------------- building them

		/// <summary>Whether <paramref name="f"/>, at war, lays down a gate breaker at its next keel when it already has
		/// <paramref name="have"/> of them afloat (<see cref="PerNavy"/>).</summary>
		static bool Wanted(const cs::Ref<Faction>& f, bool atWar, int have);
	};
}
