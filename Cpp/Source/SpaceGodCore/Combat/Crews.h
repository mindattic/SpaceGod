#pragma once
// Port of Assets/UnitySpace/Core/Combat/Crews.cs (see docs/MIGRATION.md).
//
// Order: C# declares EscapePod (whose LifeSupportDays initialiser reads Crews.LifeSupportDays) before Crews; C++
// needs Crews' constants first, so Crews is declared ahead of EscapePod and Captives here (forward-declared).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Faction;
	class Settlement;
	class Outlaw;
	class LaneSite;
	class Ship;
	class Diplomacy;
	class EscapePod;
	class Captives;
	enum class ShipClass : int;

	/// <summary>What became of an escape pod cluster.</summary>
	enum class PodState : int
	{
		/// <summary>Drifting where its hull died, broadcasting, its air running down.</summary>
		Drifting,

		/// <summary>Picked up by its own side or a friendly ship: the crew go home.</summary>
		Rescued,

		/// <summary>Picked up by pirates: the crew are held for ransom.</summary>
		Hostage,

		/// <summary>Picked up by an enemy: the crew are prisoners of war (or, for raiders, in irons).</summary>
		Prisoner,

		/// <summary>Nobody came before the air ran out.</summary>
		Lost,
	};
	const char* ToString(PodState v);

	/// <summary>How a hull's last moments came, which decides how many of its people reach the pods.</summary>
	enum class AbandonCause : int
	{
		/// <summary>Shot to pieces: the hull goes over a few minutes, and most who can run for the pods make it.</summary>
		Destroyed,

		/// <summary>The reactor went: seconds, not minutes, and few get out.</summary>
		ReactorLost,

		/// <summary>Scuttled or cast adrift deliberately: the crew are put off in the pods in good order.</summary>
		Scuttled,
	};
	const char* ToString(AbandonCause v);

	/// <summary>
	/// Crew survival and the manpower behind every hull. Every crewed hull carries escape pods, one per
	/// <see cref="PodCapacity"/> of its complement; as it dies (or is scuttled) its people make for them
	/// (<see cref="EscapeShare"/>: most when it goes slowly, few when the reactor goes, nearly all when the
	/// crew are put off in good order), and the pods drift and broadcast for <see cref="LifeSupportDays"/>.
	/// Whoever finds them decides their fate: their own side or a friendly ship brings them home (and is
	/// paid <see cref="RescueRewardPerHead"/> by their owners), pirates take them as hostages for ransom,
	/// and an enemy takes them prisoner until the war ends. Every death, in the hull or in a pod nobody
	/// reached, wears down the owners' <see cref="Faction.CrewMorale"/> and is gone from their
	/// <see cref="Faction.Manpower"/> for good; new hulls and refits draw their crews from that pool, which
	/// recruiting from the owners' settlements tops up.
	/// </summary>
	class Crews final
	{
	public:
		/// <summary>People per escape pod (a lifeboat-sized capsule).</summary>
		static constexpr double PodCapacity = 20;

		/// <summary>Days of air and water a pod carries.</summary>
		static constexpr double LifeSupportDays = 10;

		/// <summary>Credits per head a pod's owners pay whoever brings their people home.</summary>
		static constexpr double RescueRewardPerHead = 5000;

		/// <summary>Credits per head pirates demand for hostages.</summary>
		static constexpr double RansomPerHead = 60000;

		/// <summary>Credits per head the pirate hunters pay for raiders handed over from their pods.</summary>
		static constexpr double PrisonerBountyPerHead = 20000;

		/// <summary>Morale lost per death (a capital ship's crew lost outright costs a navy a tenth or so).</summary>
		static constexpr double MoraleLossPerDeath = 1 / 20000.0;

		/// <summary>Morale regained per person brought home.</summary>
		static constexpr double MoraleGainPerRescue = 1 / 40000.0;

		/// <summary>Share of the way back to 1 that morale recovers each day.</summary>
		static constexpr double MoraleRecoveryPerDay = 0.02;

		static constexpr double MinMorale = 0.5; static constexpr double MaxMorale = 1.15;

		/// <summary>Spacers recruited a day per person in a faction's settlements.</summary>
		static constexpr double RecruitPerPersonPerDay = 2e-5;

		/// <summary>The trained reserve a faction keeps, and tops up to, as a share of its settlements' people.</summary>
		static constexpr double ReserveShare = 0.004;

		/// <summary>The pirates' reserve (they recruit from the desperate and the wanted, not from a census), and its daily intake.</summary>
		static constexpr double PirateReserve = 6000; static constexpr double PirateRecruitPerDay = 25;

		/// <summary>How often a ship sharing a body with drifting pods finds them, per day: patrols search,
		/// merchants happen on them, raiders hunt for hostages.</summary>
		static constexpr double PatrolFindRate = 3; static constexpr double MerchantFindRate = 0.8; static constexpr double PirateFindRate = 2;

		cs::List<cs::Ref<EscapePod>> Pods;
		cs::List<cs::Ref<Captives>> Held;

		/// <summary>Raised when a hull launches its pods.</summary>
		cs::Event<cs::Ref<EscapePod>> Launched;

		/// <summary>Raised when pods are picked up (by whom, and the ship, if any) or their air runs out.</summary>
		cs::Event<cs::Ref<EscapePod>, cs::Ref<Ship>> Resolved;

		/// <summary>Raised when captives go home (ransomed, exchanged, freed) or are lost to their captors; the text says how.</summary>
		cs::Event<cs::Ref<Captives>, cs::String, double> CaptivesReleased;

		/// <summary>The haven a pirate ship sails from (hostages are held there), or null.</summary>
		cs::Func<cs::Ref<Settlement>, cs::Ref<Ship>> HavenOf = [](cs::Ref<Ship>) { return cs::Ref<Settlement>(); };

		/// <summary>Pirates' pods picked up by a navy: the prisoners may give their haven away.</summary>
		cs::Action<cs::Ref<EscapePod>, cs::Ref<Faction>, double> RaidersTaken;

	private:
		cs::Random rng;
		int nextId = 0;

	public:
		explicit Crews(int seed = 0) : rng(seed) {}

		// ---------------------------------------------------------------- pods

		/// <summary>Pods a hull of this complement carries: one per <see cref="PodCapacity"/> people, at least one.</summary>
		static int PodsFitted(double complement);

		/// <summary>Pods launched to carry <paramref name="people"/> off a hull of <paramref name="complement"/>: as many as they fill, no more than it carries.</summary>
		static int PodsFor(double people, double complement);

		/// <summary>The share of the people aboard who reach the pods, for a roll in [0, 1).</summary>
		static double EscapeShare(AbandonCause cause, double roll);

		/// <summary>
		/// A hull is going: of the <paramref name="aboard"/> people still on it, the share that reaches the pods
		/// (<see cref="EscapeShare"/>) launches in them from <paramref name="local"/> (battle-local km from
		/// <paramref name="anchor"/> at <paramref name="body"/>, or beside a lane at <paramref name="site"/>); the
		/// rest die with it. Returns the pod cluster, or null if nobody got off. Pods sharing a
		/// <paramref name="mergeKey"/> with a cluster still drifting join it (a battle's ejected fighter pilots).
		/// </summary>
		/// A null C# string is the empty string here: an empty <paramref name="mergeKey"/> merges with nothing.
		cs::Ref<EscapePod> Launch(const cs::String& name, const cs::Ref<Faction>& owners, double aboard, double complement, AbandonCause cause,
			int body, DVec3 anchor, DVec3 local, const cs::Ref<LaneSite>& site, double jd, const cs::String& mergeKey = cs::String(), const cs::Ref<Settlement>& haven = nullptr);

		/// <summary>
		/// A Breacher round is left drifting after a fight with <paramref name="marines"/> of <paramref name="owners"/> alive
		/// inside (U.SPC-T519, <see cref="DriftingRounds.CastAdrift"/>): at <paramref name="local"/> (battle-local km from
		/// <paramref name="anchor"/> at <paramref name="body"/>, or beside a lane at <paramref name="site"/>), tumbling on along
		/// <paramref name="heading"/>, with <see cref="DriftingRounds.AirDays"/> of air. <paramref name="enemy"/> is the side the
		/// team was fighting. Returns the drifting round, or null with nobody aboard.
		/// </summary>
		cs::Ref<EscapePod> Adrift(const cs::String& name, const cs::Ref<Faction>& owners, double marines, const cs::Ref<Faction>& enemy, int body, DVec3 anchor, DVec3 local,
			DVec3 heading, const cs::Ref<LaneSite>& site, double jd);

		/// <summary>
		/// <paramref name="ship"/> (of <paramref name="rescuer"/>; null for a ship that isn't named) picks up the
		/// pods. Their own side or a friend brings the crew home (paid <see cref="RescueRewardPerHead"/> by the
		/// owners when <paramref name="payRescuer"/>; the player is paid through the adventure instead); pirates
		/// take them as hostages; an enemy takes them prisoner. Returns what became of them.
		/// </summary>
		PodState Collect(const cs::Ref<EscapePod>& pod, const cs::Ref<Faction>& rescuer, const cs::Ref<Ship>& ship, const cs::Ref<Diplomacy>& diplomacy, double jd, bool payRescuer = true);

		/// <summary>
		/// Every ship sharing a body (parked there) or a lane with drifting pods may find them over
		/// <paramref name="days"/>; the first that does picks them up (<see cref="Collect"/>). The player's ship
		/// and ships in a fight, adrift, derelict or gone never do.
		/// </summary>
		void Sweep(const cs::List<cs::Ref<Ship>>& ships, const cs::Ref<Diplomacy>& diplomacy, const cs::Ref<Ship>& piloted, double jd, double days);

		/// <summary>Pods whose air has run out by <paramref name="jd"/> are lost with everyone aboard.</summary>
		void Expire(double jd);

		// ---------------------------------------------------------------- captives

		/// <summary>Takes people captive: pirates' hostages (ransomed after a while) or an enemy's prisoners of war.</summary>
		cs::Ref<Captives> Hold(const cs::Ref<Faction>& owners, const cs::Ref<Faction>& captors, double people, bool prisoners, const cs::Ref<Settlement>& haven, double jd, const cs::String& from);

		/// <summary>People held by <paramref name="captors"/> (or by anyone, for null) who belong to <paramref name="owners"/>.</summary>
		double HeldFrom(const cs::Ref<Faction>& owners, const cs::Ref<Faction>& captors = nullptr) const;

		/// <summary>
		/// A day's dealings over captives: hostages whose haven has fallen are freed; a ransom that falls due is
		/// paid (the owners pay <see cref="RansomPerHead"/> and have them back) or, if the owners cannot pay, the
		/// hostages are pressed into the pirates' crews; prisoners go home once their captors are no longer at
		/// war with their owners.
		/// </summary>
		void Daily(const cs::Ref<Diplomacy>& diplomacy, double jd);

		// ---------------------------------------------------------------- manpower and morale

		/// <summary>Crew died: lost to their faction for good, and a blow to the survivors' morale.</summary>
		static void RecordDeaths(const cs::Ref<Faction>& f, double people);

		/// <summary>Crew gone for good without dying in action (pressed into pirate crews, raiders in irons): no hit to morale.</summary>
		static void Lose(const cs::Ref<Faction>& f, double people);

		/// <summary>Crew come home (rescued from pods, ransomed, exchanged): back in the pool, and a lift to morale.</summary>
		static void Return(const cs::Ref<Faction>& f, double people);

		/// <summary>People a hull of <paramref name="cls"/> needs aboard: its crew and marines.</summary>
		static double Complement(ShipClass cls);

		/// <summary>Whether <paramref name="f"/> has the hands to crew a new hull of <paramref name="cls"/>.</summary>
		static bool CanCrew(const cs::Ref<Faction>& f, ShipClass cls);

		/// <summary>Draws up to <paramref name="people"/> from <paramref name="f"/>'s pool; returns how many it had.</summary>
		static double Draft(const cs::Ref<Faction>& f, double people);

		/// <summary>The reserve a faction keeps for these settlements' people (pirates keep their own).</summary>
		static double ReserveFor(const cs::Ref<Faction>& f, const cs::List<cs::Ref<Settlement>>& settlements);

		/// <summary>Gives every faction its starting reserve of trained spacers (see <see cref="ReserveFor"/>).</summary>
		static void MusterReserves(const cs::List<cs::Ref<Faction>>& factions, const cs::List<cs::Ref<Settlement>>& settlements);

		/// <summary>
		/// <paramref name="days"/> of recruiting (each faction from its own settlements' people, the pirates from
		/// whoever will sign, while a haven stands), up to its reserve; and morale recovering toward its baseline.
		/// </summary>
		static void Recruit(const cs::List<cs::Ref<Faction>>& factions, const cs::List<cs::Ref<Settlement>>& settlements, double days);
		SG_SERIALIZABLE(Crews); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// The pods one hull launched: <see cref="Pods"/> capsules with <see cref="People"/> aboard between them,
	/// drifting as a loose cluster from where the hull died and broadcasting a distress beacon until someone
	/// picks them up or their air runs out. Placed like a <see cref="Wreck"/>: <see cref="Anchor"/> is the
	/// battle's anchor in <see cref="Body"/>'s frame (or beside a lane, <see cref="Site"/>), and
	/// <see cref="LocalAt"/> the cluster's battle-local position, drifting slowly away from the hull.
	/// </summary>
	class EscapePod final
	{
	public:
		int Id = 0;

		/// <summary>The hull the pods came from ("SFS Valiant", "Ceres Hub defence platform").</summary>
		cs::String Name;

		/// <summary>Whose people are aboard (and who pays to have them back).</summary>
		cs::Ref<Faction> Owners;

		double People = 0;
		int Pods = 0;

		int Body = 0;
		DVec3 Anchor, Local;
		cs::Ref<LaneSite> Site;

		/// <summary>The way the cluster drifts from the hull (unit).</summary>
		DVec3 DriftDir = DVec3::UnitX;

		double LaunchedJd = 0;

		/// <summary>Days of air the pods carry from launch.</summary>
		double LifeSupportDays = Crews::LifeSupportDays;

		PodState State = PodState::Drifting;

		/// <summary>Who picked them up (the rescuers, the pirates, the enemy), or null.</summary>
		cs::Ref<Faction> TakenBy;
		double ResolvedJd = 0;

		/// <summary>Groups pods launched together into one cluster (a battle's ejected fighter pilots), or null.</summary>
		/// (A null C# string is the empty string here.)
		cs::String MergeKey;

		/// <summary>For raiders' pods: the haven they sail from, which prisoners taken off them may give away.</summary>
		cs::Ref<Settlement> Haven;

		/// <summary>A named outlaw captain who got away in these pods (their flagship going down, or a brig they
		/// broke out of), or null: whoever tractors the pods in has them (see <see cref="Outlaws.OnPodResolved"/>).</summary>
		cs::Ref<Outlaw> Captain;

		/// <summary>Not escape pods but a Breacher round left drifting after a fight with its team alive inside (U.SPC-T519,
		/// <see cref="DriftingRounds"/>): <see cref="People"/> are its marines, and only a tow unit can bring it in.</summary>
		bool Breacher = false;

		/// <summary>For a <see cref="Breacher"/> round: the side its team was fighting, whom pirates who salvage it may
		/// sell the marines to as slaves; or null.</summary>
		cs::Ref<Faction> Enemy;

		/// <summary>The cluster drifts this far from the hull at most, km.</summary>
		static constexpr double MaxDriftKm = 300;

		/// <summary>How fast the drift tails off, days (the pods' launch impulse, then nothing).</summary>
		static constexpr double DriftDays = 3;

		bool Drifting() const { return State == PodState::Drifting; }

		double AirLeftDays(double jd) const;

		/// <summary>How far the cluster has drifted from the hull by <paramref name="jd"/>, km.</summary>
		double DriftKm(double jd) const;

		/// <summary>The cluster's battle-local position at <paramref name="jd"/>, km (from the battle anchor).</summary>
		DVec3 LocalAt(double jd) const;

		/// <summary>Its offset from <see cref="Body"/>'s centre at <paramref name="jd"/>, km (for a pod at a body, not a lane).</summary>
		DVec3 PosAt(double jd) const;
	};

	/// <summary>People held by another faction: pirates' hostages, or an enemy's prisoners of war.</summary>
	class Captives final
	{
	public:
		cs::Ref<Faction> Owners, Captors;
		double People = 0;

		/// <summary>Prisoners of war (released when the war ends) rather than hostages (ransomed).</summary>
		bool Prisoners = false;

		/// <summary>Where they are held: a pirate haven (freed if it falls), or null.</summary>
		cs::Ref<Settlement> Haven;

		double SinceJd = 0;

		/// <summary>When the hostages' ransom falls due.</summary>
		double RansomJd = 0;

		/// <summary>The hull they came off, for the log.</summary>
		cs::String From;

		/// <summary>Marines salvaged from a drifting Breacher round (U.SPC-T519): their captors may sell them to
		/// <see cref="Buyer"/> as slaves instead of ransoming them.</summary>
		bool Marines = false;

		/// <summary>The side the marines were fighting, who would buy them as slaves; or null.</summary>
		cs::Ref<Faction> Buyer;
	};
}
