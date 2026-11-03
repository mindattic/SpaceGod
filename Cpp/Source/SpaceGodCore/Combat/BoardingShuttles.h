#pragma once
// Port of Assets/UnitySpace/Core/Combat/BoardingShuttles.cs (see docs/MIGRATION.md).
//
// The C# file also holds part of `partial class Battle` (the battle's side of the boarding shuttles). Those members
// are defined in BoardingShuttles.cpp and must be declared in Combat/Battle.h (the one header the partial class
// shares) as:
//
//   private:
//     void SendShuttles(const cs::Ref<BattleUnit>& u, double dt);
//     static bool HangarWorks(const cs::Ref<BattleUnit>& u);
//     bool BoardsByShuttle(const cs::Ref<BattleUnit>& carrier);
//     bool ShuttleBound(const cs::Ref<BattleUnit>& seeker, const cs::Ref<BattleUnit>& o);
//   public:
//     bool ShuttleMayBoard(const cs::Ref<BattleUnit>& seeker, const cs::Ref<BattleUnit>& o);
//     cs::Ref<BattleUnit> ShuttleTarget(const cs::Ref<BattleUnit>& seeker);
//     void ShuttleLatch(const cs::Ref<BattleUnit>& shuttle, const cs::Ref<BattleUnit>& t);
//
// SendShuttles passes the battle itself to FighterAI::LaunchShuttle, so Battle needs
// std::enable_shared_from_this<Battle> (or LaunchShuttle a Battle* / Battle&).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class RangeSetting;
	class BattleUnit;
	class Ship;

	/// <summary>
	/// Boarding shuttles (U.SPC-T486): a piloted, armoured, unarmed craft (<see cref="ShipClass.AssaultShuttle"/>) a carrier
	/// keeps in its hangars beside its squadrons (<see cref="CombatSpec.Shuttles"/>) to take prizes and seize stations
	/// intact. When there is a hull to take (one lying disabled, stunned, or struck on terms that open it to boarding) or a
	/// station or gate fort whose guns have been silenced, and no armed enemy close by it, the carrier sends a shuttle with a
	/// marine team (<see cref="TeamSize"/>) drawn from its barracks. The shuttle flies across, comes alongside, matches the
	/// prize's drift, docks and latches on (<see cref="LatchSeconds"/>) and lets its team in through the airlock into the
	/// compartment it docked against, where the compartment-by-compartment boarding fight (<see cref="Interior"/>) takes
	/// over. It is slower and gentler than a Breacher round fired from a Breacher bay (U.SPC-T466): no drill, no breach,
	/// no damage to the hull, and it can only take what has stopped fighting. It casts off once the fight aboard is
	/// decided and flies home to land, its team's survivors aboard if the fight was lost. A carrier with shuttles sends
	/// them rather than closing to board with its own hull. On campaign, a war fleet whose group carries shuttles and the
	/// marines for a team boards a jump gate whose forts it has silenced and takes it sooner (<see cref="GateSeizeDays"/>).
	/// Every figure is in the settings menu's Broadsides group.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API BoardingShuttles
	{
		static cs::String Seconds(double v);

	public:
		static const cs::Ref<RangeSetting>& TeamSize();

		static const cs::Ref<RangeSetting>& LatchSeconds();

		static const cs::Ref<RangeSetting>& RangeKm();

		static const cs::Ref<RangeSetting>& GateSeizeDays();

		/// <summary>Marines a carrier keeps aboard to repel boarders, beyond a shuttle's team.</summary>
		static constexpr double Reserve = 15;

		/// <summary>Seconds between one shuttle leaving a carrier's hangar and the next.</summary>
		static constexpr double LaunchSeconds = 4;

		/// <summary>How near its docking point (km) a shuttle must be, and how closely matched to the prize's drift (km/s), to latch on.</summary>
		static constexpr double DockKm = 0.06; static constexpr double MatchKmS = 0.01;

		/// <summary>How far off the prize's side (km) a shuttle docks.</summary>
		static constexpr double StandOffKm = 0.02;

		/// <summary>How near a prize (km) an armed enemy keeps a carrier from sending a shuttle to it.</summary>
		static constexpr double ThreatKm = 6;

		/// <summary>Whether <paramref name="u"/> is a boarding shuttle.</summary>
		static bool Is(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether any of <paramref name="group"/> carries a shuttle and the marines for a team to go in it: enough
		/// to board a jump gate whose forts are silenced and take it intact (<see cref="GateSeizeDays"/>).</summary>
		static bool CanSeizeGate(const cs::List<cs::Ref<Ship>>& group);

		/// <summary>Days a war fleet holds a silenced enemy gate before it is theirs: <paramref name="holdDays"/>, or fewer
		/// (<see cref="GateSeizeDays"/>) with boarding shuttles to send.</summary>
		static double GateHoldDays(double holdDays, bool shuttles);
	};
}
