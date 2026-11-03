#pragma once
// Port of Unity.SpaceGod Core/Law/Outlaws.cs (US-T20).
//
// C++ notes:
// - A C# null string is an empty cs::String here (Sighting.By, Outlaw.FateBy, Victor's result).
// - Several members share their type's name (Outlaw.Ship, BountyHunter.Faction, KillClaim.Outlaw / Wreck / Battle);
//   such types are spelled ::SpaceGod::Core::T inside those classes.
// - Outlaws' private `Law Law => world.Law` is the method Law(); inside Outlaws the class itself is ::SpaceGod::Core::Law.
// - The C# named tuple `(int body, string where)` is the private struct Place.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Ship;
	class Faction;
	class Settlement;
	class EscapePod;
	class Wreck;
	class Battle;
	class BattleUnit;
	class LawRecord;
	class Crime;
	class Law;

	/// <summary>What became of an outlaw captain.</summary>
	enum class OutlawFate : int
	{
		/// <summary>Still out there, still raiding.</summary>
		AtLarge,

		/// <summary>Died with their flagship.</summary>
		Killed,

		/// <summary>Taken alive with their flagship (boarded, or it struck its colours to boarders).</summary>
		Captured,

		/// <summary>Gone without a fight anyone saw: the hull lost outside a battle (a collision, a scuttling).</summary>
		Vanished,

		/// <summary>Taken alive and locked in a captor's brig, not yet handed over (U.SPC-T63): delivered to a lawful
		/// port of a faction that wants them it pays the full bounty; ransomed back, or breaking out, they go free.</summary>
		Held,
	};
	SPACEGODCORE_API const char* ToString(OutlawFate v);

	/// <summary>Where a wanted party was last seen: the body (or -1 when nobody can say which), when, by whom, in words.</summary>
	class SPACEGODCORE_API Sighting final
	{
	public:
		int Body = -1;
		double Jd = 0;
		cs::String By; // (null is empty)

		/// <summary>"at Mars", "bound for Ceres", "on the Earth-Mars lane", "somewhere in the Belt".</summary>
		cs::String Where;

		double DaysAgo(double jd) const { return cs::Math::Max(0, jd - Jd); }
	};

	/// <summary>
	/// A named pirate captain the law has on its books for as long as they live: the flagship they command, the
	/// haven they sail from and a criminal record built from what their band actually did in the sim -- ships
	/// robbed, ships and platforms destroyed, cargoes ransomed -- on top of the crimes the law recorded
	/// (<see cref="LawRecord"/>). One who survives long enough at large becomes a feared name.
	/// </summary>
	class SPACEGODCORE_API Outlaw final
	{
	public:
		cs::Ref<LawRecord> Record; // (readonly in C#)

		/// <summary>The flagship they command (the group leader the law's record is keyed by): the last one, while
		/// they are adrift in its pods, ashore or held.</summary>
		cs::Ref<::SpaceGod::Core::Ship> Ship;

		/// <summary>The haven they sail from, or null.</summary>
		cs::Ref<Settlement> Haven;

		/// <summary>The escape pods they got away in, while those drift (see <see cref="EscapePod.Captain"/>), or null.</summary>
		cs::Ref<EscapePod> Pod;

		/// <summary>Free again (ransomed, broken out of a brig, picked up by their own) and back with the raiders,
		/// waiting for a new band to command (see <see cref="Outlaws.Sync"/>).</summary>
		bool Ashore = false;

		/// <summary>Ashore only because the flagship is abroad (SettleFates): it clears when the hull comes home.</summary>
		bool FlagshipAway = false;

		/// <summary>The ship whose brig holds them, while <see cref="OutlawFate.Held"/>.</summary>
		cs::Ref<::SpaceGod::Core::Ship> HeldBy;

		/// <summary>When the law first had them on its books (their band sailed, or they took it over).</summary>
		double SinceJd = 0; // (readonly in C#)

		/// <summary>What the record is built from: lawful hulls robbed (ransomed, boarded and taken), hulls and
		/// platforms destroyed, cargoes ransomed, fights fought, and recorded crimes (all of them; the record's
		/// own list keeps only the latest).</summary>
		int ShipsRobbed = 0, Kills = 0, Ransoms = 0, Battles = 0, CrimeCount = 0;

		OutlawFate Fate = OutlawFate::AtLarge;
		double FateJd = 0;

		/// <summary>Who ended their career ("the bounty hunter MV Nemesis", "the Sol Federation"), or null.</summary>
		cs::String FateBy; // (null is empty)

		/// <summary>A feared name: survived <see cref="Outlaws.FearedDays"/> at large with a long record.</summary>
		bool Feared = false;
		double FearedJd = 0;

		Outlaw(cs::Ref<LawRecord> record, cs::Ref<::SpaceGod::Core::Ship> ship, cs::Ref<Settlement> haven, double sinceJd);
		/// For ObjectGraph.
		Outlaw() = default;

		cs::String Name() const;

		/// <summary>The name as the boards and the news give it: "the feared Captain ..." once it is.</summary>
		cs::String Title() const { return Feared ? cs::Cat("the feared ", Name()) : Name(); }

		bool AtLarge() const { return Fate == OutlawFate::AtLarge; }

		/// <summary>At large and in command of their flagship (not adrift in its pods, nor ashore waiting for a band).</summary>
		bool Aboard() const { return AtLarge() && Pod == nullptr && !Ashore; }

		double DaysAtLarge(double jd) const { return cs::Math::Max(0, (AtLarge() ? jd : FateJd) - SinceJd); }

		/// <summary>The record in a line: "3 ships robbed, 2 destroyed, 1 cargo ransomed".</summary>
		cs::String Deeds() const;

		cs::String ToString() const { return Title(); }
	};

	class BountyContract;

	/// <summary>A bounty hunter's group (a flagship and its escort) and what it has earned.</summary>
	class SPACEGODCORE_API BountyHunter final
	{
	public:
		cs::Ref<::SpaceGod::Core::Ship> Ship; // (readonly in C#)
		double SinceJd = 0;                  // (readonly in C#)

		/// <summary>The contract it has taken, or null.</summary>
		cs::Ref<BountyContract> Contract;

		/// <summary>Bounties it has collected, credits, and how many.</summary>
		double Collected = 0;
		int Takes = 0;

		BountyHunter(cs::Ref<::SpaceGod::Core::Ship> ship, double sinceJd, cs::Ref<World> world);
		/// For ObjectGraph.
		BountyHunter() = default;

		cs::String Name() const;

		/// <summary>Handed on to another flagship (its own lost, or taken over by the player).</summary>
		bool Retired = false;

		/// <summary>Away on a hunt voyage beyond the gates (U.SPC-T585): its row is kept even if the hull is lost abroad,
		/// until the heir comes home and <see cref="Outlaws.OnSuccession"/> hands the row on.</summary>
		bool Away = false;

		/// <summary>
		/// Still hunting: not handed on, lost, cast adrift or taken (flying other colours). A flagship through a
		/// gate or under its own cruise drive is the same live hull abroad (U.SPC-T453), never <see cref="Ship.Destroyed"/>
		/// or <see cref="Ship.GateDeparted"/> for it, so this also checks it is still this world's own fleet's hull to
		/// read a position from (<see cref="Fleet.IsPresent"/>) before anything here tries to.
		/// </summary>
		bool Alive() const;

		/// <summary>The colours it was commissioned under (the free traders').</summary>
		cs::Ref<::SpaceGod::Core::Faction> Faction; // (readonly in C#)

	private:
		cs::Ref<World> world;

		SG_SERIALIZABLE(BountyHunter);
	};

	/// <summary>A contract a hunter has taken from the boards: the party it is after, and how long it keeps at it.</summary>
	class SPACEGODCORE_API BountyContract final
	{
	public:
		cs::Ref<LawRecord> Target;
		double TakenJd = 0, UntilJd = 0;
	};

	/// <summary>A kill the player made of a wanted outlaw: the dead rate is paid once they scan the wreck (U.SPC-T63).</summary>
	class SPACEGODCORE_API KillClaim final
	{
	public:
		cs::Ref<::SpaceGod::Core::Outlaw> Outlaw;

		/// <summary>The flagship's wreck, to be scanned for proof (null: there is none, and the claim lapses).</summary>
		cs::Ref<::SpaceGod::Core::Wreck> Wreck;

		double Jd = 0;
		cs::String Battle;
	};

	/// <summary>One line on a station's bounty board.</summary>
	class SPACEGODCORE_API BountyPosting final
	{
	public:
		cs::Ref<LawRecord> Target;

		/// <summary>The outlaw it is on, or null (the player, or a lawful captain wanted for an assault).</summary>
		cs::Ref<::SpaceGod::Core::Outlaw> Outlaw;

		/// <summary>Who posts it here, and how much; and the price on the target's head across every board.</summary>
		cs::Ref<Faction> PostedBy;
		double Bounty = 0, TotalBounty = 0;

		/// <summary>Where the target was last seen, or null if nobody has.</summary>
		cs::Ref<Sighting> LastSeen;

		/// <summary>The hunter that has taken a contract on the target, or null.</summary>
		cs::Ref<BountyHunter> HuntedBy;

		bool IsPlayer() const;
	};

	/// <summary>
	/// Named outlaws, the bounty boards and the hunters who work them (U.SPC-T62). Every raider band's captain is
	/// on the law's books from the day the band sails: a name, a flagship and a home haven, and a criminal record
	/// built from what the band actually does -- every lawful hull it robs, destroys or ransoms raises the price on
	/// the captain's head on top of the crime itself (<see cref="Law.Commit"/>). Each lawful port's bounty board
	/// lists everyone its owner has posted a bounty on, with where they were last seen (by a witness: a ship or
	/// station in sensor range; never a haven nobody has found). Bounty hunters -- independent warships the
	/// free traders commission while there is money on the boards -- take contracts on outlaws they can match
	/// (and on the player, when wanted), fly to the target's last-known position, fight it there and collect every
	/// bounty on it. A captain who survives <see cref="FearedDays"/> at large with a long record becomes a feared
	/// name: their bounties rise by half, and crews facing them strike their colours sooner.
	/// </summary>
	class SPACEGODCORE_API Outlaws final
	{
	public:
		/// <summary>Every outlaw ever on the books, oldest first (the dead and taken too: a persistent rogues' gallery).</summary>
		cs::List<cs::Ref<Outlaw>> All = cs::List<cs::Ref<Outlaw>>();

		/// <summary>Every hunter group commissioned, oldest first.</summary>
		cs::List<cs::Ref<BountyHunter>> Hunters = cs::List<cs::Ref<BountyHunter>>();

		/// <summary>Where each wanted party (the player included) was last seen.</summary>
		cs::Dictionary<cs::Ref<LawRecord>, cs::Ref<Sighting>> LastSeen = cs::Dictionary<cs::Ref<LawRecord>, cs::Ref<Sighting>>();

		// ---------------------------------------------------------------- tuning

		/// <summary>What each deed adds to the bounty a recorded crime posts, credits: a lawful hull or platform
		/// destroyed, a hull robbed (ransomed or taken), and a cargo ransomed on top of that.</summary>
		static constexpr double KillBounty = 40000, RobbedBounty = 30000, RansomBounty = 20000;

		/// <summary>Days at large, and the record (recorded crimes, or kills), that make a feared name.</summary>
		static constexpr double FearedDays = 120;
		static constexpr int FearedCrimes = 4, FearedKills = 6;

		/// <summary>How a feared name's bounties grow (once when it becomes feared, and on every crime after).</summary>
		static constexpr double FearedBountyFactor = 1.5;

		/// <summary>How much of their resolve crews facing a feared name keep (see <see cref="Battle.Dread"/>).</summary>
		static constexpr double FearedResolve = 0.7;

		/// <summary>The smallest price on a head a hunter takes a contract for, credits.</summary>
		static constexpr double MinContract = 100000;

		/// <summary>How long a hunter keeps at a contract, days, and how stale a sighting it will still follow.</summary>
		static constexpr double ContractDays = 60, StaleSightingDays = 30;

		/// <summary>Of its target's strength a hunter group must match to take a contract on it (all of it for a feared name).</summary>
		static constexpr double MatchShare = 0.7;

		/// <summary>How close a hunter comes to the player's wanted ship before it moves on it, km.</summary>
		static constexpr double EngageKm = 250000;

		/// <summary>Chance a day the free traders commission a hunter while a bounty goes unhunted.</summary>
		static constexpr double CommissionChance = 0.05;

		/// <summary>What the free traders keep in their treasury whatever the boards offer: a hunter is fitted out only from above it, credits.</summary>
		static constexpr double GuildReserve = 3e8;

		/// <summary>A hunter this hurt goes to a yard first.</summary>
		static constexpr double RepairDamage = 0.3;

		Outlaws(cs::Ref<World> world, int seed);

		// ---------------------------------------------------------------- lookups

		/// <summary>The outlaw commanding <paramref name="ship"/> (its group leader), or null.</summary>
		cs::Ref<Outlaw> Of(const cs::Ref<Ship>& ship);

		/// <summary>The outlaw a record belongs to, or null.</summary>
		cs::Ref<Outlaw> Of(const cs::Ref<LawRecord>& r);

		/// <summary>The hunter group <paramref name="ship"/> flies in (it leads it or escorts its leader), or null.
		/// A hunter taken as a prize (pressed into pirate service, say) is a hunter no more.</summary>
		cs::Ref<BountyHunter> HunterOf(const cs::Ref<Ship>& ship);

		/// <summary>Whether <paramref name="ship"/> leads or flies in a hunter group.</summary>
		bool IsHunter(const cs::Ref<Ship>& ship) { return HunterOf(ship) != nullptr; }

		cs::Ref<Sighting> SeenLast(const cs::Ref<LawRecord>& r);

		// ---------------------------------------------------------------- the clock

		/// <summary>Once a day: new captains on the books, fates settled, sightings, feared names, hunters' contracts and commissions.</summary>
		void Update(double jd);

		/// <summary>Every raider band's captain goes on the books: its flagship, its haven, its record.</summary>
		void Sync(double jd);

		// ---------------------------------------------------------------- crimes and battles

		/// <summary>
		/// The law has booked a crime (<see cref="Law.Committed"/>): a recorded one goes on an outlaw's record, places
		/// the offender where it was done, and costs a feared name half again its bounty.
		/// </summary>
		void OnCrime(const cs::Ref<Crime>& c);

		/// <summary>
		/// Before a battle starts in earnest: crews facing a feared name lose some of their nerve
		/// (<see cref="Battle.Dread"/>).
		/// </summary>
		void OnBattleStarted(const cs::Ref<Battle>& b);

		/// <summary>
		/// A battle has ended and the law has judged it (<see cref="Law.OnBattleEnded"/>): each outlaw who fought in it
		/// adds what their band did to lawful ships to their record -- hulls robbed, destroyed, cargoes ransomed -- and,
		/// where the law recorded the crime, each deed raises the bounty it posted. An outlaw whose flagship died or
		/// was taken is finished. A fight on a lane places them by its near end.
		/// </summary>
		void OnBattleEnded(const cs::Ref<Battle>& b, double jd);

		/// <summary>
		/// A wanted party (<paramref name="rec"/>, commanding <paramref name="u"/>) has been killed or taken in a battle:
		/// if bounty hunters fought on the other side, they collect every bounty on it -- in full taken, at
		/// <see cref="DeadShare"/> dead (their scanners on the wreck where it died are the proof) -- each poster paying
		/// its own out of its treasury. True if they did (the bounties are then cleared and the law has nothing more to do).
		/// </summary>
		bool ClaimByHunter(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<LawRecord>& rec, double jd);

		// ---------------------------------------------------------------- alive or dead (U.SPC-T63)

		/// <summary>The share of the price on a head paid for the captain dead, on proof of kill (a scanned wreck); alive pays it all.</summary>
		static constexpr double DeadShare = 0.5;

		/// <summary>Chance a captain reaches the pods as their flagship goes: shot apart over minutes, or when the reactor goes.</summary>
		static constexpr double EjectChance = 0.8, EjectChanceReactor = 0.35;

		/// <summary>A holding hull beaten below this share of its hull has its brig breached: the captain gets away.</summary>
		static constexpr double BrigBreachHull = 0.25;

		/// <summary>What the raiders pay to have a captain back: this share of the price on their head, and at least <see cref="MinRansom"/> credits.</summary>
		static constexpr double RansomShare = 0.75, MinRansom = 50000;

		/// <summary>How close the player's ship must come to a wreck to scan it for proof of kill, km.</summary>
		static constexpr double ProofScanKm = 2000;

		/// <summary>Kills the player has made that wait on a scan of the wreck before they pay.</summary>
		cs::List<cs::Ref<KillClaim>> Claims = cs::List<cs::Ref<KillClaim>>();

		/// <summary>What the price on <paramref name="r"/>'s head pays dead.</summary>
		static double DeadRate(const cs::Ref<LawRecord>& r);

		/// <summary>What the raiders pay to have <paramref name="o"/> back (see <see cref="RansomShare"/>).</summary>
		static double RansomFor(const cs::Ref<Outlaw>& o);

		/// <summary>The captains locked in <paramref name="ship"/>'s brig.</summary>
		cs::List<cs::Ref<Outlaw>> HeldAboard(const cs::Ref<Ship>& ship);

		/// <summary>The player's kills of <paramref name="o"/> waiting on a scan of the wreck, or null.</summary>
		cs::Ref<KillClaim> ClaimOn(const cs::Ref<Outlaw>& o);

		/// <summary>
		/// The law's side of a battle's end for a wanted party (see <see cref="Law.OnBattleEnded"/>): alive or dead. The
		/// player or a lawful captain wanted for an assault is collected on by hunters (<see cref="ClaimByHunter"/>). An
		/// outlaw's flagship taken: the captain goes into a captor's brig, the bounty paid in full when they are handed
		/// over (<see cref="Deliver"/>). Destroyed: a captain who got away in the pods is still wanted; one who died with
		/// it pays hunters there the dead rate, or the player once they scan the wreck (<see cref="ProveKills"/>). True
		/// if it was settled here and the law has nothing more to do.
		/// </summary>
		bool ClaimInBattle(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<LawRecord>& rec, double jd);

		/// <summary>Locks <paramref name="o"/> in <paramref name="ship"/>'s brig. False if it has no berth free.</summary>
		bool Hold(const cs::Ref<Outlaw>& o, const cs::Ref<Ship>& ship, double jd, const cs::String& how);

		/// <summary>
		/// Hands a held captain over at <paramref name="port"/>, a lawful port of a faction that posted a bounty on them
		/// (any lawful port, if none stands any more): every bounty on them is paid in full to the holder. Returns the credits.
		/// </summary>
		double Deliver(const cs::Ref<Outlaw>& o, const cs::Ref<Settlement>& port, double jd);

		/// <summary>Ransoms a held captain back to the raiders at <paramref name="haven"/>: they pay <see cref="RansomFor"/> and
		/// the captain goes free, still wanted. Returns the credits (0 if the haven will not deal or cannot pay).</summary>
		double Ransom(const cs::Ref<Outlaw>& o, const cs::Ref<Settlement>& haven, double jd);

		/// <summary>
		/// A hull is going down in battle (from <see cref="War"/>, as its people make for <paramref name="pod"/>): an
		/// outlaw commanding it reaches the pods with <see cref="EjectChance"/> (<see cref="EjectChanceReactor"/> when the
		/// reactor went), and a captain in its brig breaks out into them. Either is then adrift, to be tractored in by
		/// whoever finds the pods, or lost with them if nobody does.
		/// </summary>
		void OnHullLost(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<EscapePod>& pod, bool reactor, double jd);

		void OnHullLost(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<EscapePod>& pod, bool reactor, double jd, double roll);

		/// <summary>
		/// Pods have been picked up or have fallen silent (<see cref="Crews.Resolved"/>): a captain aboard them is taken by
		/// whoever tractored them in -- into that ship's brig, or as the rescuers' prisoner if it has no berth -- goes
		/// free if raiders hauled them in, or dies when the air runs out (and their bounties lapse with nobody to claim them).
		/// </summary>
		void OnPodResolved(const cs::Ref<EscapePod>& pod, const cs::Ref<Ship>& ship);

		/// <summary>Whether <paramref name="hunter"/> wants <paramref name="target"/>'s captain alive: a hunter group facing a
		/// wanted outlaw's flagship (see <see cref="Battle.WantsAlive"/>).</summary>
		bool WantsAlive(const cs::Ref<Ship>& hunter, const cs::Ref<Ship>& target);

		/// <summary>The record of the outlaw whose own flagship <paramref name="ship"/> is, even when it now sails in another's group.</summary>
		cs::Ref<LawRecord> RecordCommanding(const cs::Ref<Ship>& ship);

		// ---------------------------------------------------------------- the boards

		/// <summary>
		/// The bounty board at <paramref name="port"/>: everyone its owner has a bounty posted on (outlaws still at
		/// large, the player, a lawful captain wanted for an assault), the biggest first, with where each was last
		/// seen and who is hunting them. A haven keeps no board.
		/// </summary>
		cs::List<cs::Ref<BountyPosting>> BoardAt(const cs::Ref<Settlement>& port);

		/// <summary>Every bounty on the boards system-wide, one posting per target (its total price), the biggest first.</summary>
		cs::List<cs::Ref<BountyPosting>> Postings();

		/// <summary>The hunter with a contract on <paramref name="r"/>, or null.</summary>
		cs::Ref<BountyHunter> HunterOn(const cs::Ref<LawRecord>& r);

		/// <summary>A board line in words: who, how much, what they did, where last seen, and who is after them.</summary>
		static cs::String Describe(const cs::Ref<BountyPosting>& p, double jd);

		// ---------------------------------------------------------------- hunters

		/// <summary>Puts a new hunter group into service out of <paramref name="yard"/> (a cruiser with a destroyer escort).</summary>
		cs::Ref<BountyHunter> Commission(const cs::Ref<Settlement>& yard, double jd);

		/// <summary>A hunter group leaves (or comes home from) a hunt beyond the gates: while away, its row survives the
		/// flagship being lost abroad (U.SPC-T585).</summary>
		void SetAway(const cs::Ref<Ship>& flagship, bool away);

		/// <summary>A hunter flagship lost: its escort carries on as the group (see <see cref="War"/>'s succession).</summary>
		void OnSuccession(const cs::Ref<Ship>& dead, const cs::Ref<Ship>& heir);

		/// <summary>
		/// Fleet hook (through <see cref="War.ChooseNext"/>): where a hunter group goes next. Hurt, to the nearest
		/// lawful yard. On a contract, to where its target was last seen (and it waits there for it). Without one,
		/// it takes the best it can match from the boards -- the most money for the fewest days away -- or, with
		/// nothing worth taking, makes the round of the ports.
		/// </summary>
		int ChooseNext(const cs::Ref<Ship>& leader, int here, double jd);

		/// <summary>The contract a hunter takes from the boards, or null: a target worth <see cref="MinContract"/>, seen
		/// lately somewhere it can go, nobody else is after, and that it can match (<see cref="MatchShare"/>).</summary>
		cs::Ref<BountyContract> TakeContract(const cs::Ref<BountyHunter>& h, int here, double jd);

		/// <summary>
		/// Advances the hunt around the player's ship, flown at <paramref name="offsetKm"/> from <paramref name="body"/>:
		/// where it is (for the next sighting), and a hunter with a contract on it that comes within
		/// <see cref="EngageKm"/> moves on it (<see cref="War.TryStartHunt"/>). Returns that battle, or null.
		/// </summary>
		cs::Ref<Battle> UpdatePilot(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, DVec3 forward, double jd);

	private:
		/// The C# named tuple (int body, string where).
		struct Place
		{
			int body = -1;
			cs::String where;
		};

		/// For ObjectGraph.
		Outlaws() = default;

		cs::Ref<World> world;
		cs::Random rng;

		cs::Dictionary<cs::Ref<Ship>, cs::Ref<Outlaw>> byShip = cs::Dictionary<cs::Ref<Ship>, cs::Ref<Outlaw>>();
		cs::Dictionary<cs::Ref<LawRecord>, cs::Ref<Outlaw>> byRecord = cs::Dictionary<cs::Ref<LawRecord>, cs::Ref<Outlaw>>();
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<BountyHunter>> byHunter = cs::Dictionary<cs::Ref<Ship>, cs::Ref<BountyHunter>>();

		// C#: static readonly string[] HunterNames
		static const cs::List<cs::String>& HunterNames();

		double nextDailyJd = cs::Double::NaN;
		int hunterNames = 0;

		/// <summary>The player's ship as last flown: where it is (for sightings and hunters), while it is.</summary>
		int pilotBody = -1;
		DVec3 pilotHelio;
		double pilotJd = cs::Double::NaN;

		cs::Ref<::SpaceGod::Core::Law> Law() const;

		/// <summary>Who pays and crews the hunters: the system's free traders.</summary>
		cs::Ref<Faction> Guild() const;

		void Daily(double jd);

		/// <summary>Outlaws whose flagship is gone outside any battle (lost in a collision, scuttled) or flies under other colours.
		/// A flagship through a gate or under its own cruise drive (a raid, a move, a smuggling run) is not gone: it is the
		/// same hull carrying on abroad (U.SPC-T453), so <see cref="Ship.Destroyed"/> and <see cref="Ship.GateDeparted"/> stay
		/// false on it; it is simply no longer in this world's own fleet (<see cref="Fleet.IsPresent"/>), which (a hull some
		/// other code marks both ways instead, a scrapping say) reads the same way -- the captain is merely away, ashore
		/// until a band sails again (<see cref="Sync"/>), with their record and bounties intact (U.SPC-T434).</summary>
		void SettleFates(double jd);

		void End(const cs::Ref<Outlaw>& o, OutlawFate fate, double jd, const cs::String& by);

		/// <summary>
		/// Every outlaw at large (and the player's wanted ship) that a lawful ship or station can see is sighted: the
		/// boards and the hunters learn where. Over a haven nobody has found, the sighting names no rock.
		/// </summary>
		void Sightings(double jd);

		/// <summary>Where a ship is, as a board would put it: the body it is at or bound for, or the lane it rides.</summary>
		Place Locate(const cs::Ref<Ship>& s);

		/// <summary>A sighting at a haven nobody has found names no rock.</summary>
		Place Hide(int body, const cs::String& where);

		void Sight(const cs::Ref<LawRecord>& r, const Place& at, double jd, const cs::String& by);

		cs::String BodyName(int body);

		static void AddBounty(const cs::Ref<LawRecord>& r, const cs::Ref<Crime>& c, double extra);

		static cs::String Deeds(int kills, int robbed, int ransoms);

		/// <summary>Who won a fight on a side, as the news gives it: a hunter by name, else the side's faction.</summary>
		cs::String Victor(const cs::Ref<Battle>& b, int side);

		bool PlayerFoughtAgainst(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// An outlaw's flagship has been taken by boarders: the captain is locked in the brig of a ship on the captors'
		/// side with a berth free (the player's first, then a hunter's, then anyone's). False if none has room.
		/// </summary>
		bool Seize(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Outlaw>& o, double jd);

		static cs::String Posters(const cs::Ref<LawRecord>& rec);

		/// <summary>The captain no longer commands their old flagship (taken, or left behind): the hull's books entries go, so its
		/// later loss under another's colours is not read as the captain's death (U.SPC-T579).</summary>
		void Unbind(const cs::Ref<Outlaw>& o);

		/// <summary>Lets go of a held captain's berth.</summary>
		static void Release(const cs::Ref<Outlaw>& o);

		/// <summary>The captain is free and back with the raiders: at large, ashore until a new band sails (<see cref="Sync"/>).</summary>
		void Free(const cs::Ref<Outlaw>& o, double jd, const cs::String& how);

		/// <summary>A freed captain takes command of a band that has just sailed: their record, and the law's books, follow them to it.</summary>
		void Rebind(const cs::Ref<Outlaw>& o, const cs::Ref<Ship>& ship, double jd);

		/// <summary>Pays out the bounties on <paramref name="rec"/> at <paramref name="share"/> to <paramref name="payee"/>
		/// (a hunter's group, or whoever's ship it is), each poster paying its own; all of them are cleared. Returns the credits.</summary>
		double Pay(const cs::Ref<LawRecord>& rec, double share, const cs::Ref<Ship>& payee);

		void Lapse(const cs::Ref<LawRecord>& rec, double jd, const cs::String& why);

		/// <summary>
		/// A captain held aboard a ship that fought (<see cref="OnBattleEnded"/>): the hull lost with them aboard kills
		/// them; taken by raiders, they are freed; by anyone else, they change hands; beaten below
		/// <see cref="BrigBreachHull"/>, the brig is breached and they get away.
		/// </summary>
		void Breach(const cs::Ref<Outlaw>& o, const cs::Ref<BattleUnit>& hu, double jd);

		/// <summary>
		/// Daily: captains held aboard ships the player is not flying are handed over when the holder lies at a lawful
		/// port that pays for them; one carried through a gate is gone from this system's books, and one aboard a hull
		/// lost outside a fight dies with it.
		/// </summary>
		void SettleCaptives(double jd);

		/// <summary>The nearest body with a lawful port that pays for <paramref name="rec"/> (any lawful port once nothing is posted) and docks <paramref name="ship"/>, or -1.</summary>
		int NearestPortFor(const cs::Ref<LawRecord>& rec, const cs::Ref<Ship>& ship, int here, double jd);

		/// <summary>The player's ship within <see cref="ProofScanKm"/> of a wreck it has a kill claim on scans it: the dead rate is paid.</summary>
		void ProveKills(const cs::Ref<Ship>& ship, double jd);

		/// <summary>Kill claims whose wreck broke up before anyone scanned it lapse, and the bounties with them.</summary>
		void LapseClaims(double jd);

		// ---------------------------------------------------------------- feared names

		void Fear(double jd);

		static cs::String Ago(double days);

		// ---------------------------------------------------------------- hunters

		/// <summary>
		/// While a bounty worth hunting goes unhunted and there are fewer hunter groups than <see cref="World.HunterCap"/>,
		/// the free traders now and then commission one at the biggest lawful shipyard that will dock them: a cruiser
		/// and a destroyer, paid for out of their treasury above <see cref="GuildReserve"/> (their crews sign on in port).
		/// </summary>
		void Commission(double jd);

		/// <summary>Contracts lapse when the target is no longer wanted, taken, dead or long lost.</summary>
		void ReviewContracts(double jd);

		/// <summary>Drops hunter rows nothing can use any more: handed on to an heir (<see cref="OnSuccession"/>
		/// already took it out of <see cref="byHunter"/>), or a flagship destroyed with no heir to carry it on.
		/// A hunter gone through a gate keeps its row: it takes it up again when it comes home from a hunt
		/// abroad (see <see cref="InterstellarTraffic"/>). Keeps the roster from growing for ever with retired
		/// rows over a long game (U.SPC-T321).</summary>
		void PruneHunters();

		bool Huntable(const cs::Ref<LawRecord>& r);

		/// <summary>Hunters idle at a lawful shipyard are patched up there, at the free traders' expense.</summary>
		void RepairHunters();

		/// <summary>Whether what would fight the hunter where its target was seen (the target's band, a haven's guns,
		/// other raiders there) is now more than the hunter group can take on: it drops the contract.</summary>
		bool Outmatched(const cs::Ref<BountyHunter>& h, const cs::Ref<Sighting>& seen);

		int NearestYard(int here, double jd);

		static cs::String Cap(const cs::String& s);

		static cs::String Cr(double v);

		SG_SERIALIZABLE(Outlaws);
	};
}
