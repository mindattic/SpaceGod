#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Events/EventDirector.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Astro.h"
#include "DVec3.h"
#include "Events/Opportunity.h"

#include <tuple>

namespace SpaceGod::Core
{
	class EscapePod;
	class EventSources;
	class Faction;
	class Lane;
	class SalvageSession;
	class Ship;
	class Wreck;
	struct SalvageStack;
	enum class Good : int;

	/// <summary>
	/// The dynamic event system: watches the sim around the player and turns what is really happening
	/// there into timed, location-anchored adventures (<see cref="Opportunity"/>). Nothing is scripted:
	/// a distress call is a ship actually under attack, or actually drifting with dead drives after an
	/// interdiction pulse; a derelict is a hull pirates really stripped and left (<see cref="Ship.Derelict"/>);
	/// a wreck is one a battle really left (<see cref="Wreck"/>); an ambush is raiders really lurking off a
	/// lane with a record of trouble; an escort request is a merchant really riding a dangerous lane; a
	/// bounty names the captain of a real raider group. Only anomalies, rare, are rolled.
	///
	/// Each opportunity is offered while it is within reach (<see cref="SpawnRadiusKm"/>, wider for
	/// bounties), never twice at once, rested a while after it ends, and capped at <see cref="MaxActive"/>
	/// at a time. The player sees it through by flying there (within <see cref="ArriveKm"/>) and, for a
	/// fight, staying until the sim settles it; ignored, it fails when it expires or when the situation
	/// resolves without them.
	/// </summary>
	class SPACEGODCORE_API EventDirector final
	{
	public:
		/// <summary>How far from the player something can be and still be offered, km (0.3 AU).</summary>
		double SpawnRadiusKm = 0.3 * Astro::AuKm;

		/// <summary>Bounties travel further by word of mouth: captains this far away are still posted, km.</summary>
		double BountyRadiusKm = 2 * Astro::AuKm;

		/// <summary>How close the player's ship must come to be on the scene, km.</summary>
		double ArriveKm = 150000;

		/// <summary>How close the player's ship must come to drifting escape pods to take them aboard, km.</summary>
		double PodPickupKm = 50;

		/// <summary>Credits per person brought home from escape pods, paid by their owners.</summary>
		double PodRescueCredits = 8000;

		/// <summary>Standing with the owners for bringing their people home from the pods.</summary>
		double PodRescueReputation = 5;

		/// <summary>The most opportunities open at once.</summary>
		int MaxActive = 5;

		/// <summary>How often (sim days, about three minutes) the sim is scanned for new opportunities, so a fight
		/// lasting minutes is still caught at real time; resolutions are checked every update.</summary>
		double ScanIntervalDays = 0.002;

		/// <summary>Lane danger (see <see cref="EventSources.LaneDanger"/>) at which raiders lurking there are reported.</summary>
		double AmbushDanger = 2;

		/// <summary>Lane danger at which a merchant riding it asks for an escort.</summary>
		double EscortDanger = 1;

		/// <summary>An escort is only worth asking for with at least this long left on the ride, days.</summary>
		double MinEscortDays = 0.1;

		/// <summary>Anomalies turn up this often near the player, per day (rare by design).</summary>
		double AnomalyRatePerDay = 0.15;

		/// <summary>How many ended opportunities are kept for the HUD's history.</summary>
		int HistoryLength = 40;

		cs::List<cs::Ref<Opportunity>> Active;
		cs::List<cs::Ref<Opportunity>> History;

		/// <summary>The player's standing with each faction, raised by opportunities seen through.</summary>
		cs::Dictionary<cs::Ref<Faction>, double> Reputation;

		cs::Event<cs::Ref<Opportunity>> Spawned;
		cs::Event<cs::Ref<Opportunity>> Resolved;

	private:
		cs::Random rng;
		cs::Dictionary<cs::String, double> restUntil;
		double nextScanJd = cs::Double::NegativeInfinity;
		double lastAnomalyRollJd = cs::Double::NaN;
		int nextId = 0;

	public:
		EventDirector(int seed = 0);

		double ReputationWith(const cs::Ref<Faction>& f) const;

		/// <summary>
		/// Advances the director to <paramref name="jd"/>: refreshes every open opportunity's position,
		/// marks those the player's ship (<paramref name="playerShip"/>, null when only watching) has
		/// reached, settles any that are won, lost or expired, and -- every <see cref="ScanIntervalDays"/>
		/// -- offers new ones from what is happening near <paramref name="playerHelio"/>.
		/// </summary>
		void Update(double jd, const cs::Ref<EventSources>& src, DVec3 playerHelio, const cs::Ref<Ship>& playerShip);

		// ---------------------------------------------------------------- spawning

	private:
		void Scan(double jd, const cs::Ref<EventSources>& src, DVec3 player, const cs::Ref<Ship>& playerShip);

		static int Priority(OpportunityKind k);

		bool IsOpen(const cs::String& key) const;

		bool IsResting(const cs::String& key, double jd) const;

		/// <summary>Drops a rested source once its rest is over: <see cref="IsResting"/> would say no to it anyway,
		/// so nothing is lost keeping the book from growing forever over a long game (U.SPC-T321). Most sources
		/// (a wreck, a pod, an anomaly) never repeat, so without this their rest entries would simply pile up.</summary>
		void PruneRest(double jd);

		/// <summary>How long a source is left alone after its opportunity ends, days.</summary>
		static double RestDays(OpportunityKind k);

		static cs::Ref<Lane> LaneOf(const cs::Ref<Ship>& s);

		static cs::String Key(const cs::String& kind, const cs::Ref<Ship>& s);

		static double CargoValue(const cs::Ref<Ship>& s);

		static double RoundThousand(double v);

		static cs::String Cr(double v);

		cs::Ref<Opportunity> DistressUnderAttack(const cs::Ref<Ship>& s, double jd, const cs::Ref<EventSources>& src);

		cs::Ref<Opportunity> DistressAdrift(const cs::Ref<Ship>& s, double jd, const cs::Ref<EventSources>& src);

	public:
		/// <summary>The share of a dry ship's tanks a pilot answering its call pumps across (as much as they can spare).</summary>
		double DryTanksTransfer = 0.3;

	private:
		cs::Ref<Opportunity> DistressDry(const cs::Ref<Ship>& s, double jd, const cs::Ref<EventSources>& src);

		cs::Ref<Opportunity> DerelictOf(const cs::Ref<Ship>& s, double jd);

		cs::Ref<Opportunity> WreckOf(const cs::Ref<Wreck>& w, double jd);

		/// <summary>What a hulk's scanned hold shows, for its hook.</summary>
		cs::Ref<Opportunity> PodsOf(const cs::Ref<EscapePod>& pod, double jd, const cs::Ref<EventSources>& src);

		static cs::String HoldReads(const cs::List<SalvageStack>& salvage);

		cs::Ref<Opportunity> Ambush(const cs::Ref<Ship>& s, const cs::Ref<Lane>& lane, double jd, const cs::Ref<EventSources>& src);

		cs::Ref<Opportunity> Escort(const cs::Ref<Ship>& s, const cs::Ref<Lane>& lane, double jd, const cs::Ref<EventSources>& src);

		cs::Ref<Opportunity> BountyOn(const cs::Ref<Ship>& s, double jd, const cs::Ref<EventSources>& src);

		// C# `static readonly (string title, string hook, double credits, Good? good)[] Anomalies`.
		static const cs::List<std::tuple<cs::String, cs::String, double, cs::Nullable<Good>>>& Anomalies();

		cs::Ref<Opportunity> AnomalyNear(DVec3 player, double jd, const cs::Ref<EventSources>& src);

		// ---------------------------------------------------------------- following and settling

		static void Refresh(const cs::Ref<Opportunity>& op, const cs::Ref<EventSources>& src);

		void Settle(const cs::Ref<Opportunity>& op, double jd, const cs::Ref<EventSources>& src, const cs::Ref<Ship>& player, DVec3 playerHelio);

		static cs::String Cap(const cs::String& s);

		static bool Contains(const cs::List<cs::Ref<Wreck>>& list, const cs::Ref<Wreck>& w);

	public:
		/// <summary>Credits per survivor brought out of a derelict, paid by the ship's owners.</summary>
		double SurvivorRescueCredits = 15000;

		/// <summary>Standing with a derelict's owners for bringing its survivors out.</summary>
		double SurvivorRescueReputation = 4;

		/// <summary>
		/// The player latched on to a hulk and got something out of it (<see cref="SalvageSession.Worked"/>):
		/// the derelict or wreck opportunity on it, if one is open, is seen through -- by that ship, whether or
		/// not it was seen arriving -- and paid. Survivors brought out add their owners' thanks to the reward.
		/// Returns the opportunity completed, or null if none was open on it.
		/// </summary>
		cs::Ref<Opportunity> HulkWorked(const cs::Ref<SalvageSession>& session, double jd);

	private:
		void Complete(const cs::Ref<Opportunity>& op, double jd, const cs::Ref<Ship>& player, const cs::String& how);

		void Fail(const cs::Ref<Opportunity>& op, double jd, const cs::String& how);

		void End(const cs::Ref<Opportunity>& op, OpportunityState state, double jd, const cs::String& how);

		/// <summary>Credits go to the player's purse (their faction's treasury, as the market board shows it),
		/// cargo into their grid hold as far as it fits, and standing to the patron.</summary>
		void Pay(const cs::Ref<Opportunity>& op, cs::Ref<Ship> player);
	};
}
