#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/DriftingRounds.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class Battle;
	class Captives;
	class Crews;
	class Diplomacy;
	class EscapePod;
	class Faction;
	class Missile;
	class Ship;

	/// <summary>
	/// What becomes of a Breacher round that fails (U.SPC-T519). A round that misses, or glances off shields still too strong
	/// to drill through, is not the instant loss of its team: it careens on through space, or tumbles away from the shields
	/// (<see cref="Glance"/>), a derelict with the marines still alive inside (<see cref="Missile.Adrift"/>). Point defence
	/// can still shoot it out of the sky, killing the team, though it spends itself on a tumbling round only with nothing
	/// live in reach. Once the fight is over every round still in space is left drifting where it was
	/// (<see cref="CastAdrift"/>), its team breathing the round's own air for <see cref="AirDays"/>: tow units of its own
	/// side (warships, tugs) search for it and bring the marines home, an enemy warship takes them prisoner, and pirates
	/// salvage it for the marines inside, whom they ransom back to their owners or sell as slaves to the side they were
	/// fighting (<see cref="SlavePricePerHead"/>, <see cref="Crews.Daily"/>). A round nobody reaches runs out of air.
	/// </summary>
	class DriftingRounds
	{
	public:
		/// <summary>Days of air a round carries for its team once it is adrift.</summary>
		static constexpr double AirDays = 6;

		/// <summary>The share of its speed a round keeps as it glances off a shield and tumbles away.</summary>
		static constexpr double GlanceKeep = 0.6;

		/// <summary>How often a tow unit (a warship, or a tug) sharing a body with a drifting round finds it, per day: an
		/// armoured round is towed in, not lifted aboard like an escape pod, so a merchant passing by cannot take it.</summary>
		static constexpr double TowFindRate = 2.5;

		/// <summary>How often a pirate ship sharing a body with a drifting round finds it, per day: raiders comb a
		/// battlefield for marines worth a ransom.</summary>
		static constexpr double PirateFindRate = 2.5;

		/// <summary>Credits per head the side the marines were fighting pays pirates for them as slaves.</summary>
		static constexpr double SlavePricePerHead = 35000;

		/// <summary>The chance pirates holding marines from a round sell them to their enemy as slaves even when the
		/// owners could pay the ransom.</summary>
		static constexpr double SellChance = 0.35;

		/// <summary>
		/// The relative velocity a round keeps as it glances off a shield whose outward face is <paramref name="normal"/>:
		/// what drove it into the shield turned back out, everything slowed to <see cref="GlanceKeep"/>.
		/// </summary>
		static DVec3 Glance(DVec3 relVel, DVec3 normal);

		/// <summary>Whether <paramref name="s"/> can tow in a drifting round: a warship, a raider or a tug.</summary>
		static bool CanTow(const cs::Ref<Ship>& s);

		/// <summary>The round's name for the log: whose bay fired it.</summary>
		static cs::String NameOf(const cs::Ref<Missile>& m);

		/// <summary>Whether <paramref name="m"/> is a Breacher round with a team still alive inside.</summary>
		static bool Manned(const cs::Ref<Missile>& m);

		/// <summary>
		/// The fight is over: every Breacher round of <paramref name="b"/> still in space with its team alive (adrift, or
		/// still flying at nothing) is left drifting where it is, for <paramref name="crews"/> to see found or lost. The side
		/// the team was fighting is noted, as the buyer pirates may sell them to. Returns the drifting rounds.
		/// </summary>
		static cs::List<cs::Ref<EscapePod>> CastAdrift(const cs::Ref<Battle>& b, const cs::Ref<Crews>& crews, double jd);

		/// <summary>
		/// Who pirates holding marines <paramref name="c"/> would sell them to as slaves, and at what price: the side the
		/// marines were fighting, if it is still hostile to their owners, is no raider itself and can pay. Null if nobody.
		/// </summary>
		static cs::Ref<Faction> Buyer(const cs::Ref<Captives>& c, const cs::Ref<Diplomacy>& diplomacy, double& price);
	};
}
