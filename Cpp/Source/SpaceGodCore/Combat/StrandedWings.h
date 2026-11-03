#pragma once
#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Settlement;
	class Faction;
	class Economy;
	class Diplomacy;

	/// <summary>
	/// A lost carrier strands its wings (U.SPC-T473; docs/SHIP_ROSTER.md, "Carrier"). Fighters and bombers out of their
	/// hangars when their carrier is destroyed, taken, or leaves the field (runs, or jumps away) have nowhere to land: each
	/// looks for another friendly carrier on the field with room in its hangars, or a friendly station (a defence platform
	/// or gate fort fighting on its side), and lands there when the shooting stops. One that finds nowhere on the field
	/// can still put in at a friendly station at the world the fight was over when it ends; one that cannot is lost, its
	/// pilot ejecting into the pods. A carrier's hangars hold no more than its own squadrons
	/// (<see cref="CombatSpec.Squadrons"/>, six craft each), so a carrier can take in only as many strays as its own losses
	/// have left room for.
	/// </summary>
	class SPACEGODCORE_API StrandedWings
	{
	public:
		/// <summary>How often a stranded craft looks again for somewhere to land, seconds.</summary>
		static constexpr double HavenCheckSeconds = 5;

		/// <summary>Whether a craft can still land on <paramref name="home"/>: it is on the field and has not been taken.</summary>
		static bool CanLandOn(const cs::Ref<BattleUnit>& home);

		/// <summary>Whether <paramref name="haven"/> is a station rather than a carrier: a defence platform or a gate fort.</summary>
		static bool IsStation(const cs::Ref<BattleUnit>& haven);

		/// <summary>Whether <paramref name="haven"/> could take in a stray: a station, or a carrier with a working hangar,
		/// still fighting on its side (not running, struck or taken).</summary>
		static bool TakesStrays(const cs::Ref<BattleUnit>& haven);

		/// <summary>How many more craft <paramref name="haven"/> can take in: a station any number; a carrier what its hangars
		/// hold less the craft aboard and those already flying back to land on it.</summary>
		static int Room(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& haven);

		/// <summary>The nearest friendly carrier with room, or station, on the field that <paramref name="craft"/> could land on
		/// instead, or null.</summary>
		static cs::Ref<BattleUnit> Haven(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& craft);

		/// <summary>
		/// A craft's step (from <see cref="FighterAI.Step"/>): while it can still land where it was going, nothing; once that
		/// carrier is lost or gone, it looks (every <see cref="HavenCheckSeconds"/>) for a friendly carrier or station on the
		/// field to land on instead, and is <see cref="FighterState.Stranded"/> until it finds one.
		/// </summary>
		static void Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>A friendly station at the world <paramref name="b"/> was fought over that a stray from it can put in at once
		/// the fight is over: its own side's first, else one of a friendly power's it may dock at; or null, and none for a
		/// fight out on a lane, far from any port.</summary>
		static cs::Ref<Settlement> Port(const cs::Ref<Battle>& b, const cs::Ref<Faction>& owners, const cs::Ref<Economy>& economy, const cs::Ref<Diplomacy>& diplomacy);
	};
}
