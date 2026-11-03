#pragma once
// Port of Unity.SpaceGod Core/Combat/FleetCoordination.cs (US-T10).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class Battle;
	class BattleUnit;
	class RangeSetting;
	enum class ShipClass : int;

	/// <summary>
	/// A fleet fought from a command ship (U.SPC-T477): the hull that carries the fleet's senior officers, its
	/// communications suite to give their orders, its targeting computers to lay a whole line's guns and its sensor array
	/// to share one picture of the fight. Any hull with a working communications suite inside it
	/// (<see cref="FacilityKinds.CommsSuite"/>) commands; the <see cref="ShipClass.CommandCruiser"/> is built for it, and a
	/// yard can fit the facilities into another capital's free space. Every ship on its side within its reach
	/// (<see cref="ReachOf"/>) fights coordinated: its gunners find their marks sooner (<see cref="RetargetFactor"/>), shoot
	/// truer (<see cref="AccuracyFactor"/>, one share per targeting computer), read a target's course from the shared
	/// picture rather than their own (<see cref="SharedPicture"/>, one share per sensor array) and concentrate on the hull
	/// the command ship is fighting (<see cref="FocusTarget"/>). Lose the command ship (sunk, taken, struck, its bridge shot
	/// away or its systems knocked out) and the ships it was coordinating are thrown into confusion for a while
	/// (<see cref="Disrupted"/>), so every hostile gunner favours it (<see cref="Priority"/>). Each figure is a setting in
	/// the menu's Broadsides group.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API FleetCoordination
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

	private:
		static cs::String Km(double v);

		static cs::String Percent(double v);

	public:
		static const cs::Ref<RangeSetting>& ReachKm();

		static const cs::Ref<RangeSetting>& AccuracyPerComputer();

		static const cs::Ref<RangeSetting>& RetargetCut();

		static const cs::Ref<RangeSetting>& Focus();

		static const cs::Ref<RangeSetting>& PicturePerArray();

		static const cs::Ref<RangeSetting>& DisruptionPenalty();

		static const cs::Ref<RangeSetting>& DisruptionSeconds();

		static const cs::Ref<RangeSetting>& Priority();

		/// <summary>The most of a tracking error the shared picture can take away.</summary>
		static constexpr double MaxPicture = 0.8;

		// ---------------------------------------------------------------- what a hull carries

		/// <summary>Facilities of kind <paramref name="id"/> inside <paramref name="s"/>: its loadout's if it has one, else
		/// its class's design.</summary>
		static int Count(const cs::Ref<Ship>& s, const cs::String& id);

		/// <summary>Whether <paramref name="s"/> is a command ship: a communications suite inside it.</summary>
		static bool IsCommandShip(const cs::Ref<Ship>& s);

		/// <summary>Whether hulls of <paramref name="cls"/> are designed as command ships.</summary>
		static bool IsCommandClass(ShipClass cls);

		// ---------------------------------------------------------------- in battle

		/// <summary>Whether <paramref name="u"/> is a command ship coordinating its side right now: present, still its own
		/// side's (not taken), not struck or disabled, its systems up and its flag bridge standing.</summary>
		static bool Coordinates(const cs::Ref<BattleUnit>& u);

		/// <summary>How far <paramref name="commander"/>'s orders reach, km: <see cref="ReachKm"/>, half again for each
		/// further communications suite.</summary>
		static double ReachOf(const cs::Ref<BattleUnit>& commander);

		/// <summary>
		/// A step of the battle's coordination, before anyone thinks: each unit is taken under the nearest working command
		/// ship of its side whose reach it is in (<see cref="BattleUnit.CommandedBy"/>). One whose command ship has just been
		/// lost, with no other to take it over, falls into confusion for <see cref="DisruptionSeconds"/>
		/// (<see cref="BattleUnit.Disrupted"/>); one merely out of reach just fights on its own.
		/// </summary>
		static void Step(const cs::Ref<Battle>& b, double dt);

		/// <summary>What <paramref name="u"/>'s accuracy is multiplied by: more under a command ship with targeting
		/// computers, less in the confusion after losing one.</summary>
		static double AccuracyFactor(const cs::Ref<BattleUnit>& u);

		/// <summary>What the time <paramref name="u"/>'s gunners take to choose a target again is multiplied by.</summary>
		static double RetargetFactor(const cs::Ref<BattleUnit>& u);

		/// <summary>The share of a target's tracking error and of its range falloff <paramref name="u"/>'s gunners are spared
		/// by the shared picture, 0 with none.</summary>
		static double SharedPicture(const cs::Ref<BattleUnit>& u);

		/// <summary>The hull <paramref name="u"/>'s command ship is fighting, for it to concentrate on, or null.</summary>
		static cs::Ref<BattleUnit> FocusTarget(const cs::Ref<BattleUnit>& u);
	};
}
