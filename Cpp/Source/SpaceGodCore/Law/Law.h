#pragma once
// Port of Unity.SpaceGod Core/Law/Law.cs (US-T20).
//
// C++ notes:
// - A C# null string is an empty cs::String here (Crime.Witness, Crime.Detail, FindWitness's result).
// - LawRecord.Standing is shared with the event system's reputation table (Law's playerStanding argument), so it is
//   a cs::Ref<cs::Dictionary>; EventDirector.Reputation must be one too (World.cpp passes Adventures->Reputation).
// - OnBattleEnded's `Dictionary<object, Faction>` keyed by "the player's record or a captain's ship" is keyed by
//   OffenderKey (either a record or a ship), enumerated in the same insertion order.

#include <tuple>

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Ship;
	class Faction;
	class Settlement;
	class Battle;
	class BattleUnit;
	class CargoItem;
	struct LaneHop;
	enum class ContactLevel : int;

	/// <summary>The crimes a faction's law knows (see <see cref="Law"/>).</summary>
	enum class CrimeKind : int
	{
		/// <summary>Opening fire on a lawful ship: one whose faction the offender is not at war with, and not wanted there.</summary>
		Assault,

		/// <summary>Robbing or taking a lawful ship (its cargo ransomed, the hull boarded), or any attack by pirates.</summary>
		Piracy,

		/// <summary>Contraband in the hold when a patrol scans it: stolen goods, or a good the faction bans.</summary>
		Smuggling,

		/// <summary>Flying into a port's space while barred from docking there.</summary>
		Trespass,

		/// <summary>Running from a patrol's scan, or from an interdiction once hailed.</summary>
		FleeingScan,
	};
	SPACEGODCORE_API const char* ToString(CrimeKind v);

	class LawRecord;

	/// <summary>One crime: who, what, where and whose law it broke, and whether anyone saw it.</summary>
	class SPACEGODCORE_API Crime final
	{
	public:
		CrimeKind Kind = CrimeKind::Assault;
		cs::Ref<LawRecord> Offender;

		/// <summary>Whose space it was committed in (null for lawless space), and whose ship suffered it.</summary>
		cs::Ref<Faction> Jurisdiction, Victim;

		/// <summary>Who posted the bounty: the jurisdiction, or the victim's faction in lawless space.</summary>
		cs::Ref<Faction> PostedBy;

		int Body = -1;
		double Jd = 0;

		/// <summary>The ship or station that saw it, or null: an unwitnessed crime is never recorded.</summary>
		cs::String Witness; // (null is empty)
		cs::String Detail;  // (null is empty)
		double Bounty = 0;

		bool Recorded() const { return !Witness.empty() && PostedBy != nullptr; }

		cs::String ToString() const;
	};

	/// <summary>
	/// A party's standing in the eyes of the law: the player's (one record, whatever hull they fly) or an NPC
	/// captain's (the master of a ship or the leader of a group). Standing with each faction (positive is
	/// trusted, below <see cref="Law.DockMinStanding"/> barred from its ports), notoriety (how widely known a
	/// criminal it is, fading with time) and the bounties posted on it by each faction.
	/// </summary>
	class SPACEGODCORE_API LawRecord final
	{
	public:
		cs::String Name; // (readonly in C#; not const here so ObjectGraph can load it)

		/// <summary>The captain's ship (the group leader's), or null for the player.</summary>
		cs::Ref<Ship> Captain;
		bool IsPlayer() const { return Captain == nullptr; }

		/// <summary>Standing with each faction (0 neutral; crimes lower it, good deeds raise it).</summary>
		// Shared with the event system's reputation table for the player (see the file comment).
		cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> Standing;

		/// <summary>Bounties posted on this party, credits, by the faction that posted each.</summary>
		cs::Dictionary<cs::Ref<Faction>, double> Bounty = cs::Dictionary<cs::Ref<Faction>, double>();

		/// <summary>When each bounty lapses if nothing renews it (a crime against the same poster does).</summary>
		cs::Dictionary<cs::Ref<Faction>, double> BountyUntil = cs::Dictionary<cs::Ref<Faction>, double>();

		/// <summary>The crimes on record, newest last (only the witnessed ones).</summary>
		cs::List<cs::Ref<Crime>> Crimes = cs::List<cs::Ref<Crime>>();

		/// <summary>How notorious: each recorded crime adds to it, and it halves every <see cref="Law.NotorietyHalfLifeDays"/>.</summary>
		double Notoriety = 0;

		/// <summary>Crimes nobody saw: they never reached a record.</summary>
		int Unwitnessed = 0;

		LawRecord(cs::String name, cs::Ref<Ship> captain, cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> standing = nullptr);
		/// For ObjectGraph (C# makes a loaded object without running its constructor).
		LawRecord() = default;

		double StandingWith(const cs::Ref<Faction>& f) const;

		double BountyBy(const cs::Ref<Faction>& f) const;

		bool WantedBy(const cs::Ref<Faction>& f) const { return BountyBy(f) > 0; }

		bool Wanted() const { return Bounty.Count() > 0; }

		double TotalBounty() const;

		cs::String ToString() const { return Name; }
	};

	/// <summary>A patrol's hail of the player's ship: a scan to hold still for, or an order to heave to and pay up.</summary>
	class SPACEGODCORE_API PatrolHail final
	{
	public:
		cs::Ref<Ship> Patrol;
		cs::Ref<Faction> By;

		/// <summary>True for an interdiction (the player is wanted: pay the bounty or be fired on); false for a routine scan.</summary>
		bool Interdiction = false;

		double StartJd = 0, DueJd = 0;

		/// <summary>The fine an interdiction asks (the bounty the hailing faction has posted).</summary>
		double Fine = 0;

		double SecondsLeft(double jd) const;
	};

	/// <summary>
	/// Law, crime and reputation. Each faction has a jurisdiction: the bodies its settlements are on, the
	/// planetary systems whose hyperlane ramps it keeps (see <see cref="World.LaneKeeper"/>) and those lanes;
	/// a pirate haven, and space nobody keeps, are lawless. Attacking a lawful ship, piracy, smuggling
	/// contraband, trespassing and running from a scan are crimes -- recorded only if witnessed by a ship or
	/// station in sensor range (a merchant whose crew all died on an empty lane tells nobody). A recorded
	/// crime raises the offender's notoriety, lowers its standing with the jurisdiction and the victim's
	/// faction, and posts a bounty in that jurisdiction (the victim's own faction posts it in lawless space).
	/// Patrols hail the player's ship: routine scans in their own space (contraband is confiscated and is
	/// smuggling; running is a crime) and, for a wanted ship anywhere, an order to heave to and pay the bounty
	/// or be interdicted (<see cref="War.TryStartInterdiction"/>). Standing gates docking (and so trade),
	/// prices (see <see cref="PriceFactor"/>) and the contracts a faction will offer the player.
	/// </summary>
	class SPACEGODCORE_API Law final
	{
	public:
		/// <summary>The player's record: one, whatever hull they fly. Its standing is the event system's
		/// reputation table, so adventures seen through raise the same standing crimes lower.</summary>
		cs::Ref<LawRecord> Player;

		/// <summary>NPC captains' records, by the ship they command (a group's leader), made on their first crime.</summary>
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<LawRecord>> Captains = cs::Dictionary<cs::Ref<Ship>, cs::Ref<LawRecord>>();

		/// <summary>Recorded crimes system-wide, newest last.</summary>
		cs::List<cs::Ref<Crime>> Recent = cs::List<cs::Ref<Crime>>();

		/// <summary>The patrol hail the player's ship is answering, or null.</summary>
		cs::Ref<PatrolHail> Hail;

		/// <summary>Raised for every crime, witnessed or not (recorded ones carry their bounty).</summary>
		cs::Event<cs::Ref<Crime>> Committed;

		// ---------------------------------------------------------------- tuning

		/// <summary>How close a patrol comes before it hails the player's ship, km.</summary>
		double PatrolHailKm = 100000;

		/// <summary>Past this distance from the hailing patrol the player has run from it, km.</summary>
		double PatrolHoldKm = 180000;

		/// <summary>How long a scan takes to complete, seconds; and how long a hail to heave to waits for the fine.</summary>
		double ScanSeconds = 20, HailSeconds = 30;

		/// <summary>How long a faction's patrols leave the player be after a scan or a hail, days.</summary>
		double ScanCooldownDays = 0.5;

		/// <summary>How often trespass in one faction's space is charged again, days.</summary>
		double TrespassCooldownDays = 1;

		/// <summary>Standing below which a faction's ports refuse to dock the ship (and so to trade).</summary>
		static constexpr double DockMinStanding = -25;

		/// <summary>Standing below which a faction offers no contracts (distress calls, escorts, bounties).</summary>
		static constexpr double ContractMinStanding = -10;

		/// <summary>A bounty this large closes a faction's ports to the ship whatever its standing, credits.</summary>
		static constexpr double DockRefusedBounty = 100000;

		/// <summary>How long a bounty stands with nothing renewing it, days.</summary>
		static constexpr double BountyDays = 180;

		/// <summary>How fast notoriety fades: it halves in this many days.</summary>
		static constexpr double NotorietyHalfLifeDays = 90;

		/// <summary>The share of the standing a crime costs with its jurisdiction that it also costs with the
		/// victim's faction, when that is someone else.</summary>
		static constexpr double VictimStandingShare = 0.6;

		Law(cs::Ref<World> world, cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> playerStanding = nullptr);
		/// For ObjectGraph.
		Law() = default;

		/// <summary>What a crime costs a record: notoriety, standing with whose law it broke, and the bounty posted.</summary>
		static std::tuple<double, double, double> Severity(CrimeKind k);

		static cs::String Describe(CrimeKind k);

		// ---------------------------------------------------------------- records and standing

		/// <summary>The record of whoever commands <paramref name="ship"/>: the player's for the ship they fly,
		/// else its captain's (the group leader's), made the first time it is asked for.</summary>
		cs::Ref<LawRecord> RecordOf(const cs::Ref<Ship>& ship);

		/// <summary>The record of a ship's captain if one has been opened (never for the player's ship), else null.</summary>
		cs::Ref<LawRecord> FindRecord(const cs::Ref<Ship>& ship);

		/// <summary>The bounties posted on the captain of <paramref name="ship"/>, credits (0 if none).</summary>
		double BountyOn(const cs::Ref<Ship>& ship);

		/// <summary>Whether <paramref name="r"/> may dock at (and so trade with) <paramref name="s"/>: barred by
		/// standing below <see cref="DockMinStanding"/> with the owner, or by a bounty of <see cref="DockRefusedBounty"/>
		/// or more posted by it. A pirate haven asks no questions.</summary>
		static bool CanDock(const cs::Ref<LawRecord>& r, const cs::Ref<Settlement>& s);

		/// <summary>Docking for a ship: its commander's record, if it has one, at <paramref name="s"/>.</summary>
		bool AllowsDocking(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s);

		/// <summary>
		/// How standing moves prices at a faction's markets: what the party pays is multiplied by this and what
		/// it is paid divided by it. Trusted (+50 or better) buys 10% cheaper; distrusted pays up to 30% more
		/// (at -50 or worse); a haven deals with anyone at its own prices.
		/// </summary>
		static double PriceFactor(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& owner);

		/// <summary>A ship's price factor at a settlement (its commander's record).</summary>
		double PriceFactorFor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s);

		/// <summary>Whether <paramref name="patron"/> offers <paramref name="r"/> its contracts: not while wanted by it,
		/// nor with standing below <see cref="ContractMinStanding"/>.</summary>
		static bool OffersContracts(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& patron);

		// ---------------------------------------------------------------- jurisdiction

		/// <summary>
		/// Whose law holds at <paramref name="body"/>: the owner of its biggest lawful settlement, else whoever
		/// keeps its planetary system's lanes (<see cref="World.LaneKeeper"/>); null for lawless space (a haven's
		/// rock with no other settlement, or a system nobody keeps).
		/// </summary>
		cs::Ref<Faction> JurisdictionAt(int body);

		/// <summary>The owner of the biggest lawful settlement on <paramref name="body"/>, or null.</summary>
		cs::Ref<Faction> PortAuthority(int body);

		/// <summary>Whose law holds on a lane: the keeper of the system it was joined from, else of the far end.</summary>
		cs::Ref<Faction> JurisdictionOf(const LaneHop& hop);

		// ---------------------------------------------------------------- witnesses

		/// <summary>
		/// The first ship or station that could see something as bright as <paramref name="signature"/> (a battle's
		/// weapons fire, by default) at <paramref name="helioKm"/>: a lawful settlement or a lawful ship (not destroyed,
		/// adrift as a hulk, down on a planet, nor the player's own) that <paramref name="exclude"/> lets count, whose
		/// scanners reach that far (see <see cref="Sensors.FindWitness"/>: a station's traffic control sees a fight 3
		/// million km off, a merchant one million, a warship two). Pirates report nothing. Returns its name, or null
		/// when nobody saw.
		/// </summary>
		// (Signatures.Battle is 25: a default argument can't name the incomplete Signatures here.)
		cs::String FindWitness(DVec3 helioKm, double jd, cs::Func<bool, cs::Ref<Ship>> exclude = nullptr, double signature = 25);

		// ---------------------------------------------------------------- crimes

		/// <summary>
		/// Books a crime by <paramref name="offender"/>. Unwitnessed (<paramref name="witness"/> null) it goes
		/// no further. Witnessed, it is posted by the jurisdiction (or, in lawless space, by the victim's faction
		/// if it is a lawful one): notoriety up, standing down with the poster (and, by
		/// <see cref="VictimStandingShare"/>, with the victim's faction if that is someone else) and a bounty in
		/// the poster's space, renewed for <see cref="BountyDays"/>.
		/// </summary>
		cs::Ref<Crime> Commit(const cs::Ref<LawRecord>& offender, CrimeKind kind, cs::Ref<Faction> jurisdiction, const cs::Ref<Faction>& victim, int body, double jd, const cs::String& witness, const cs::String& detail = cs::String());

		/// <summary>Pays off the bounty <paramref name="f"/> has posted on the player, from the purse of the ship
		/// they fly: the bounty is cleared and standing is restored as far as docking there again.
		/// False (nothing paid) if there is no bounty or the purse cannot cover it.</summary>
		bool PayFine(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& f, double jd, double& paid);

		// (internal in C#)
		static void ClearBounty(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& f);

		/// <summary>Daily bookkeeping: notoriety fades and bounties nothing has renewed lapse.</summary>
		void Update(double jd);

		// ---------------------------------------------------------------- battles

		/// <summary>
		/// A battle has ended: whoever attacked lawful ships in it answers for it, if anyone saw. On each side, every
		/// commander (the player for a hull they flew, else each group's captain) on the side that started it whose
		/// foes included lawful units -- not pirates, not a faction it is at war with, not ships wanted in that
		/// jurisdiction -- has committed assault, or piracy if it is a pirate or took or ransomed one of them (pirates
		/// and robbers answer for it whichever side they were on; a lawful ship defending itself does not). A survivor among those it attacked
		/// is a witness; failing that, any ship or station in sensor range. The side enforcing the law in an
		/// interdiction commits nothing, and a wanted ship it stopped is charged only if it destroyed or took one of
		/// the patrol's ships. A wanted commander killed or taken by bounty hunters or the player is theirs to collect on, at
		/// the dead rate for a kill and in full alive (see <see cref="Outlaws.ClaimInBattle"/>); by a faction that posted a bounty on it, it has paid that one
		/// (a dead NPC captain's bounties all lapse).
		/// </summary>
		void OnBattleEnded(const cs::Ref<Battle>& b, double jd);

		/// <summary>Where a battle is fought, heliocentric km: beside its lane, or off its body.</summary>
		DVec3 BattleHelio(const cs::Ref<Battle>& b, double jd);

		// ---------------------------------------------------------------- the player's ship and the patrols

		/// <summary>The contraband a patrol of <paramref name="f"/> would find in a ship's grid hold: stolen goods
		/// (bought at a pirate haven) and any good <paramref name="f"/> bans (<see cref="Faction.Contraband"/>).</summary>
		static cs::List<cs::Ref<CargoItem>> ContrabandIn(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& f);

		/// <summary>
		/// Advances the law around the player's ship, flown at <paramref name="offsetKm"/> from <paramref name="body"/>:
		/// trespass while barred from the port it is over; and the patrols. A navy patrol (a group's flagship, not at
		/// war with the ship) within <see cref="PatrolHailKm"/> hails it: if its faction wants the player, to heave to
		/// and pay the bounty within <see cref="HailSeconds"/> or be interdicted; otherwise, in its own jurisdiction,
		/// to hold still for a <see cref="ScanSeconds"/> scan. Running past <see cref="PatrolHoldKm"/> before either is
		/// done is fleeing a scan; a scan that finds contraband confiscates it and charges smuggling.
		/// </summary>
		void UpdatePilot(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, DVec3 forward, double jd);

		/// <summary>Records a message for the player (shown as a toast by the HUD).</summary>
		void Notice(const cs::String& text);

		/// <summary>The player's messages since the last call, oldest first.</summary>
		cs::List<cs::String> TakeNotices();

	private:
		/// One commander in OnBattleEnded (C#: an `object` key, the player's record or a captain's ship).
		struct OffenderKey
		{
			cs::Ref<LawRecord> Record;
			cs::Ref<::SpaceGod::Core::Ship> Ship;
			bool operator==(const OffenderKey& o) const { return Record == o.Record && Ship == o.Ship; }
			size_t HashCode() const { return cs::HashCombine(std::hash<const void*>{}(Record.get()), std::hash<const void*>{}(Ship.get())); }
		};

		cs::Ref<World> world;

		/// <summary>Messages for the player (their own crimes, hails, scans), drained by the HUD (<see cref="TakeNotices"/>).</summary>
		cs::List<cs::String> notices = cs::List<cs::String>();

		double lastJd = cs::Double::NaN;
		cs::Dictionary<cs::Ref<Faction>, double> scannedUntil = cs::Dictionary<cs::Ref<Faction>, double>();
		cs::Dictionary<cs::Ref<Faction>, double> trespassUntil = cs::Dictionary<cs::Ref<Faction>, double>();

		static cs::String CaptainName(const cs::Ref<Ship>& s);

		static void Adjust(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& f, double delta);

		/// <summary>Drops a captain's record once nothing more can come of it: no bounty outstanding, notoriety
		/// faded to nothing, and the flagship it was opened for gone for good (destroyed, or through a gate) --
		/// so nothing will ever look the record up by that ship again. Keeps the books from growing forever over
		/// a long game (U.SPC-T321).</summary>
		void PruneCaptains();

		static void Fade(const cs::Ref<LawRecord>& r, double fade, double jd);

		bool HidesHaven(int body);

		/// <summary>The record of the captain commanding a ship in battle: its group leader's, opened if need be.</summary>
		cs::Ref<LawRecord> RecordOfCaptain(const cs::Ref<Ship>& ship);

		/// <summary>The units on <paramref name="side"/> that <paramref name="by"/> had no right to fight: lawful
		/// (not pirates), not at war with it, not of its own faction and not wanted in <paramref name="juris"/>.</summary>
		cs::List<cs::Ref<BattleUnit>> LawfulVictims(const cs::Ref<Battle>& b, int side, const cs::Ref<Faction>& by, const cs::Ref<Faction>& juris);

		/// <summary>What patrol <paramref name="p"/>'s group makes of the player's <paramref name="ship"/> at <paramref name="rangeKm"/>,
		/// by its signature as the helm last reported it (drive lit or not; silent running or not).</summary>
		ContactLevel PatrolSees(const cs::Ref<Ship>& p, const cs::Ref<Ship>& ship, double rangeKm, double jd);

		bool IsPatrol(const cs::Ref<Ship>& p, const cs::Ref<Ship>& player);

		void AnswerHail(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, DVec3 forward, DVec3 helio, double jd);

		/// <summary>Over a port barred to the player (standing too low, not an enemy at war): trespass, seen by the port.</summary>
		void CheckTrespass(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd);

		static cs::String Cr(double v);

		SG_SERIALIZABLE(Law); // ObjectGraph registration (Law.cpp)
	};
}
