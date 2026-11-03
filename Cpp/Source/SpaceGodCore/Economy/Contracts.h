#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Contracts.cs (US-T18).

#include "Cs/Cs.h"
#include "DVec3.h" // pilotOffset (by value)
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Settlement;
	class Faction;
	class Ship;
	class LawRecord;
	class Battle;
	class Economy;
	class Law;
	enum class Good : int;

	/// <summary>The jobs a station's contracts board posts (see <see cref="Contracts"/>).</summary>
	enum class ContractKind : int
	{
		/// <summary>Carry a lot of a good a settlement is short of from a port that has it.</summary>
		Freight,

		/// <summary>Carry a small party of people moving for a better life (migrants travelling on their own account).</summary>
		Passengers,

		/// <summary>Carry a colony party: a settlement people are leaving in numbers sends them where people are going.</summary>
		Colonists,

		/// <summary>See a valuable cargo safely in to a dangerous port: be there when it arrives.</summary>
		Escort,

		/// <summary>Hold station over a settlement that has seen fighting or raiders lately.</summary>
		Patrol,

		/// <summary>Go and look at a body no ship has been to.</summary>
		Survey,

		/// <summary>Kill or take a wanted captain (the board's copy of a bounty, see <see cref="Outlaws"/>).</summary>
		Bounty,
	};
	const char* ToString(ContractKind v);

	enum class ContractState : int
	{
		/// <summary>On the board, waiting for a taker.</summary>
		Open,

		/// <summary>Taken: the taker is working it.</summary>
		Taken,

		Completed,

		/// <summary>Taken and not done: the deadline passed, the cargo or the passengers were lost, the client died.</summary>
		Failed,

		/// <summary>Taken and given up by the taker.</summary>
		Abandoned,

		/// <summary>Never taken: the offer lapsed, or the need it was posted for went away.</summary>
		Expired,
	};
	const char* ToString(ContractState v);

	/// <summary>One job on a contracts board: what, for whom, where, what it pays, and who has taken it.</summary>
	class Contract final
	{
	public:
		int Id = 0;
		ContractKind Kind = ContractKind::Freight;
		ContractState State = ContractState::Open;

		/// <summary>The settlement whose board it is posted on (where a taker signs, and where freight is loaded and people board).</summary>
		cs::Ref<Settlement> Board;

		/// <summary>Who pays (and whose standing it moves).</summary>
		cs::Ref<Faction> Patron;

		/// <summary>Where freight, passengers and colonists go; null for the other kinds.</summary>
		cs::Ref<Settlement> Destination;

		/// <summary>The body to patrol, survey or escort the client to; for freight and people, the destination's body.</summary>
		int Body = -1;

		/// <summary>Freight: the good, the lot and what has been handed over so far, tonnes.</summary>
		::SpaceGod::Core::Good Good = static_cast<::SpaceGod::Core::Good>(0);
		double Tonnes = 0, Delivered = 0;

		/// <summary>For freight, how short the destination was when it was posted: its stock (with what was on the way) over the
		/// stock its market wants (<see cref="Settlement.Target"/>).</summary>
		double CoverAtPost = 0;

		/// <summary>Passengers and colonists: how many; and whether they are aboard the taker's hull.</summary>
		int People = 0;
		bool Aboard = false;

		/// <summary>Escort: the merchant to see in, and when it got there (NaN until it does).</summary>
		cs::Ref<Ship> Client;
		double ClientArrivedJd = cs::Double::NaN;

		/// <summary>Bounty: whose head.</summary>
		cs::Ref<LawRecord> Target;

		/// <summary>Patrol: days on station needed, and done.</summary>
		double PatrolDays = 0, OnStation = 0;

		/// <summary>What it pays on completion, credits; and what went into the price: distance (km from the board),
		/// danger (0..1 at the far end) and urgency (0..1).</summary>
		double Pay = 0, DistanceKm = 0, Danger = 0, Urgency = 0;

		/// <summary>When it was posted, when the offer lapses untaken, how long a taker has once it signs, and when it is due.</summary>
		double PostedJd = 0, ExpiresJd = 0, DeadlineDays = 0, DueJd = cs::Double::NaN;

		double TakenJd = cs::Double::NaN, ClosedJd = cs::Double::NaN;

		/// <summary>The hull that took it (the group leader for a hunter group), the record of whoever commands it, and
		/// whether that is the player.</summary>
		cs::Ref<Ship> Taker;
		cs::Ref<LawRecord> TakerRecord;

		/// <summary>The colours the taker flew when it took the job: a hull taken as a prize since is lost to it.</summary>
		cs::Ref<Faction> TakerFaction;
		bool ByPlayer = false;

		bool IsOpen() const { return State == ContractState::Open; }
		bool IsActive() const { return State == ContractState::Taken; }
		bool IsClosed() const { return State != ContractState::Open && State != ContractState::Taken; }

		/// <summary>The job in a few words: "120 t Food to Ares Prime".</summary>
		cs::String Title() const;

		/// <summary>The body's name, set when posted (the contract holds no catalog).</summary>
		cs::String BodyLabel;

		cs::String ToString() const { return Title(); }
	};

	/// <summary>
	/// Contracts boards (U.SPC-T101). Every lawful station posts jobs from what the simulation actually needs:
	/// freight runs of a good a settlement is short of (from the nearest port that has it to spare), passengers
	/// and colony parties from places people are leaving to where they are going (<see cref="People.Migrate"/>),
	/// escorts for valuable cargoes bound into danger, patrols over settlements that have seen fighting or raiders,
	/// surveys of bodies no ship has been to, and the bounties on the law's books. Pay reflects distance, danger at
	/// the far end and urgency (how short the settlement is, how fast the people are leaving). The player takes
	/// jobs at the board of the port they are alongside (F7); NPC captains take them too -- merchants freight and
	/// people, free ships surveys, bounty hunters patrols, escorts and bounties -- and every contract either
	/// completes (the patron pays and the taker's standing rises), or fails or is abandoned (and standing with
	/// the patron falls; below <see cref="Law.ContractMinStanding"/> the patron offers no more). An offer nobody
	/// takes lapses, and one whose need has gone (the shortage filled, the client arrived) is taken down.
	/// </summary>
	class Contracts final
	{
		cs::Ref<World> world;
		cs::Random rng;

	public:
		/// <summary>Every contract posted and not yet forgotten: open, taken, and the latest closed ones (newest last).</summary>
		cs::List<cs::Ref<Contract>> All = cs::List<cs::Ref<Contract>>();

		/// <summary>Bodies some ship has been to, and when it was last there (every settled body counts as visited).</summary>
		cs::Dictionary<int, double> Visited = cs::Dictionary<int, double>();

	private:
		int nextId = 1;
		double nextDailyJd = cs::Double::NaN;

		/// <summary>The player's ship as last flown (for deliveries, patrols and surveys at the helm).</summary>
		int pilotBody = -1;
		DVec3 pilotOffset;
		double pilotJd = cs::Double::NaN;

	public:
		// ---------------------------------------------------------------- tuning

		/// <summary>A market with less than this share of the stock it wants (counting what is on the way) is short, and posts freight.</summary>
		static constexpr double ShortCover = 0.5;

		/// <summary>The cover freight contracts restore a short market to (and past which an open one is taken down).</summary>
		static constexpr double RestoreCover = 0.7;

		/// <summary>The biggest bulk lot one freight contract asks for, tonnes; and the crates a parcel lot (sized for a pilot's hold) asks for.</summary>
		static constexpr double BulkLotTonnes = 8000;
		static constexpr int ParcelCrates = 8;

		/// <summary>How long an untaken offer stays up, days, by kind.</summary>
		static constexpr double OfferDays = 12, SurveyOfferDays = 30;

		/// <summary>The most open contracts one board holds.</summary>
		static constexpr int BoardCap = 14;

		/// <summary>A settlement losing people faster than this (a day) posts a colony party; any loss at all posts passengers.</summary>
		static constexpr double ColonistOutflow = 20;

		/// <summary>The share of its founding people a settlement keeps whatever passage is posted from it.</summary>
		static constexpr double MinPeopleShare = 0.6;

		/// <summary>Danger (0..1) at a settlement past which its owner posts a patrol, and into which a valuable cargo wants an escort.</summary>
		static constexpr double PatrolDanger = 0.3, EscortDanger = 0.3;

		/// <summary>A cargo worth this much at base prices is worth escorting, credits.</summary>
		static constexpr double EscortCargoValue = 2e6;

		/// <summary>How long after the client arrives the escort may still be there to count, days.</summary>
		static constexpr double EscortWindowDays = 1;

		/// <summary>Days on station a patrol asks for.</summary>
		static constexpr double PatrolStationDays = 1;

		/// <summary>Standing a completed contract earns (more for an urgent one), and what failing or abandoning one costs.</summary>
		static constexpr double CompleteStanding = 2, UrgentStanding = 3, FailStanding = 6, AbandonStanding = 4;

		/// <summary>How many closed contracts are kept for the record.</summary>
		static constexpr int HistoryCap = 200;

		/// <summary>The chance an idle NPC captain looks at the board before it goes about its usual business.</summary>
		static constexpr double NpcTakeChance = 0.6;

		Contracts(cs::Ref<World> world, int seed);

	private:
		cs::Ref<::SpaceGod::Core::Economy> Economy() const;
		cs::Ref<::SpaceGod::Core::Law> Law() const;

	public:
		// ---------------------------------------------------------------- the clock

		/// <summary>Once a day: needs posted as contracts, lapsed offers taken down, deadlines and losses judged.</summary>
		void Update(double jd);

		// ---------------------------------------------------------------- the boards

		/// <summary>The open contracts on <paramref name="port"/>'s board, best paid first. A haven keeps no board.</summary>
		cs::List<cs::Ref<Contract>> BoardAt(const cs::Ref<Settlement>& port) const;

		/// <summary>The contracts the player has taken and not yet finished, oldest first.</summary>
		cs::List<cs::Ref<Contract>> PlayerActive() const;

		/// <summary>The contract <paramref name="ship"/> (or the group it flies in) is working, or null.</summary>
		cs::Ref<Contract> ActiveFor(const cs::Ref<Ship>& ship) const;

	private:
		static bool Lawful(const cs::Ref<Settlement>& s);

		int OpenAt(const cs::Ref<Settlement>& board) const;

		cs::Ref<Contract> Add(const cs::Ref<Contract>& c, double jd);

	public:
		// ---------------------------------------------------------------- pricing

		/// <summary>A flip-and-burn's days for <paramref name="km"/> at a working merchant's 0.3 g, and a day in port.</summary>
		static double TravelDays(double km);

		/// <summary>
		/// What a job pays, credits: its base (the goods' worth for freight, a fare a head for people, a fee for the
		/// rest), with a carriage premium for every AU from the board, doubled at full danger at the far end and
		/// raised by half again at full urgency.
		/// </summary>
		static double Price(double basePay, double perAu, double km, double danger, double urgency);

	private:
		static double Clamp01(double v);

	public:
		/// <summary>How dangerous a body is now, 0..1: the war's tally of fights and raids there, fading over weeks.</summary>
		double DangerAt(int body) const;

	private:
		double Km(int a, int b, double jd) const;

	public:
		// ---------------------------------------------------------------- posting

		/// <summary>Posts what the system needs today, kind by kind.</summary>
		void Post(double jd);

	private:
		/// <summary>Tonnes of a good already on contract to a settlement and not yet delivered by the player (an NPC's
		/// contract cargo is in the market's inbound book already).</summary>
		double OnContract(const cs::Ref<Settlement>& to, Good g) const;

		bool OpenFreight(const cs::Ref<Settlement>& to, Good g) const;

		/// <summary>
		/// Every lawful market short of a good it uses (its stock and what is on the way below <see cref="ShortCover"/>
		/// of what it wants) posts freight for it at the nearest port with it to spare: a bulk lot for a merchant's hold
		/// and a parcel for a pilot's, together no more than it takes to bring it back to <see cref="RestoreCover"/>.
		/// The shorter it is, the more urgent, the better it pays and the sooner it is due.
		/// </summary>
		void PostFreight(double jd);

		void PostFreightLot(const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to, Good g, double tonnes, double cover, double urgency, double jd);

		/// <summary>The nearest lawful port (on another body) that will sell <paramref name="tonnes"/> of a good, or null.</summary>
		cs::Ref<Settlement> Supplier(const cs::Ref<Settlement>& to, Good g, double tonnes, double jd) const;

		/// <summary>
		/// Where people are leaving (<see cref="Settlement.NetMigration"/> below zero) they want passage to where people
		/// are going: a small party at a time on their own account, and, from a place losing more than
		/// <see cref="ColonistOutflow"/> a day, a colony party the destination's owner pays to bring in.
		/// </summary>
		void PostPeople(double jd);

		bool HasOpen(ContractKind kind, const cs::Ref<Settlement>& board, const cs::Ref<Settlement>& to) const;

		void PostPeopleLot(ContractKind kind, const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to, int people, const cs::Ref<Faction>& patron, double urgency, double jd);

		/// <summary>
		/// A merchant under way with a cargo worth <see cref="EscortCargoValue"/> into a port as dangerous as
		/// <see cref="EscortDanger"/> wants an escort there to see it in: posted on the board it sailed from, paid by
		/// its owners, open until it gets there.
		/// </summary>
		void PostEscorts(double jd);

		bool HasEscort(const cs::Ref<Ship>& client) const;

		/// <summary>A lawful settlement that has seen fighting or raiders lately (danger past <see cref="PatrolDanger"/>) posts a patrol over itself.</summary>
		void PostPatrols(double jd);

		/// <summary>Each lawful power's biggest port posts surveys of the two nearest bodies no ship has been to.</summary>
		void PostSurveys(double jd);

		bool SurveyPosted(int body) const;

		/// <summary>Every price on a head worth a hunter's while (<see cref="Outlaws.MinContract"/>) goes up as a bounty
		/// contract at the biggest port of whoever posted the most of it, paying the whole price.</summary>
		void PostBounties(double jd);

	public:
		// ---------------------------------------------------------------- taking

		enum class TakeOutcome : int
		{
			Ok,
			NotOpen,

			/// <summary>The patron offers this taker nothing (wanted by it, or standing below <see cref="Law.ContractMinStanding"/>).</summary>
			NotOffered,

			/// <summary>Not alongside the board's port.</summary>
			NotHere,

			/// <summary>No room for the people (or no hold for the freight).</summary>
			NoRoom,

			/// <summary>An escort, patrol or bounty wants guns.</summary>
			Unarmed,

			/// <summary>Freight: the port has not the goods to sell, or the taker cannot pay for them.</summary>
			NoGoods,
		};
		friend const char* ToString(TakeOutcome v);

		/// <summary>People a hull can carry: berths on a courier, a colony deck in a freighter, ten to a cell of a pilot's hold.</summary>
		static int Berths(const cs::Ref<Ship>& ship, bool player);

	private:
		cs::Ref<LawRecord> RecordFor(const cs::Ref<Ship>& ship, bool player) const;

	public:
		/// <summary>Whether <paramref name="ship"/> could take <paramref name="c"/> (alongside the board for people,
		/// with the hold, berths or guns it needs). The player takes jobs at the board of the port they are at.</summary>
		TakeOutcome CanTake(const cs::Ref<Contract>& c, const cs::Ref<Ship>& ship, bool player, double jd) const;

		/// <summary>
		/// <paramref name="ship"/> takes <paramref name="c"/>: the clock starts, people board, and a merchant buys the
		/// lot at the board's port and books it for the destination. (The player buys a lot's crates at the market
		/// themselves, here or anywhere, and hands them over at the destination.)
		/// </summary>
		TakeOutcome Take(const cs::Ref<Contract>& c, const cs::Ref<Ship>& ship, bool player, double jd);

		/// <summary>The player gives up <paramref name="c"/>: no pay, standing with the patron falls, and anyone aboard is put ashore where they boarded.</summary>
		bool Abandon(const cs::Ref<Contract>& c, double jd);

		// ---------------------------------------------------------------- NPC captains

		/// <summary>
		/// Fleet hook for merchants, couriers and free ships (before their usual choice): a ship working a contract
		/// goes where it takes it; an idle one in port now and then looks at the boards here and takes the job that
		/// pays best for the days it costs -- freight it can carry and pay for, people it has berths for, a survey for
		/// a free ship or a courier. Returns the body to go to, or -1 to go about its usual business.
		/// </summary>
		int ChooseNext(const cs::Ref<Ship>& ship, int here, double jd);

	private:
		/// <summary>Which NPC hulls work which jobs: merchants freight and people, free ships and couriers surveys.</summary>
		static bool Suits(const cs::Ref<Ship>& ship, const cs::Ref<Contract>& c);

	public:
		/// <summary>
		/// Through <see cref="Outlaws.ChooseNext"/>: a bounty hunter group with no bounty to chase works the guns'
		/// jobs on the boards -- a patrol over a troubled port, an escort into one -- taking the best paid it can
		/// reach in time, and goes where it takes it. Returns the body, or -1.
		/// </summary>
		int ChooseForHunter(const cs::Ref<Ship>& leader, int here, double jd);

		/// <summary>
		/// Fleet hook (from <see cref="World"/>, before the market sees the ship): a ship has arrived at
		/// <paramref name="body"/>. The body is visited; contract freight and people for here are delivered, a survey
		/// of it is done, an escorted client is in. True when the ship carries contract cargo the market here must
		/// not buy (delivered, or bound elsewhere).
		/// </summary>
		bool OnArrived(const cs::Ref<Ship>& ship, int body, double jd);

	private:
		/// <summary>Whether a merchant on a freight contract still has its lot aboard, booked for the destination.</summary>
		static bool CarriesLot(const cs::Ref<Contract>& c);

		void DeliverNpcFreight(const cs::Ref<Contract>& c, double jd);

	public:
		// ---------------------------------------------------------------- the player at the helm

		/// <summary>
		/// The player's ship (<paramref name="ship"/>) flown at <paramref name="offsetKm"/> from <paramref name="body"/>:
		/// close in, the body is visited, a survey of it is done, freight crates for a port here are handed over (and
		/// the job paid once the whole lot is in), passengers put ashore, and a patrol here counts its time on station.
		/// </summary>
		void UpdatePilot(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd);

		/// <summary>Hands over the crates of a freight contract's good (not stolen ones) from the player's hold at its destination.</summary>
		double HandOver(const cs::Ref<Contract>& c, const cs::Ref<Ship>& ship, double jd);

		/// <summary>Whether the player's ship is close in at <paramref name="body"/> (within its neighbourhood) as last flown.</summary>
		bool PlayerNear(const cs::Ref<Ship>& ship, int body, double jd) const;

	private:
		/// <summary>Whether an NPC hull (its group) is in port or parked at <paramref name="body"/>.</summary>
		static bool At(const cs::Ref<Ship>& ship, int body);

		bool TakerAt(const cs::Ref<Contract>& c, int body, double jd) const;

	public:
		// ---------------------------------------------------------------- review

		/// <summary>
		/// Once a day: offers past their time, or whose need has gone, come down; taken contracts whose deadline has
		/// passed, whose taker was lost or taken, whose cargo went elsewhere or whose client died, fail; a patrol on
		/// station counts its day; a bounty whose target is dead or taken is done (by the hunter that took it) or void.
		/// </summary>
		void Review(double jd);

	private:
		/// <summary>Whether what an open offer was posted for still stands.</summary>
		bool StillNeeded(const cs::Ref<Contract>& c) const;

		/// <summary>A hunter that has taken the contract on a bounty's target (in <see cref="Outlaws"/>) has taken this one too.</summary>
		void MirrorHunter(const cs::Ref<Contract>& c, double jd);

		/// <summary>A taken bounty: done once the target is killed or taken (by its taker: a hunter's take is paid by the
		/// law's books, the player's by the contract), void if someone else got there first, given up if a hunter dropped it.</summary>
		bool ReviewBounty(const cs::Ref<Contract>& c, double jd);

	public:
		/// <summary>
		/// A battle has ended: a bounty target the player took a contract on, killed or taken with the player's hull on
		/// the other side, completes it (the patron pays the contract's price).
		/// </summary>
		void OnBattleEnded(const cs::Ref<Battle>& b, double jd);

	private:
		// ---------------------------------------------------------------- closing

		/// <summary>
		/// Ends a contract. Completed: the patron pays (unless the law's books already did) and the taker's standing
		/// with it rises. Failed or abandoned: no pay, standing falls, people still aboard go ashore where they boarded
		/// (or die with a lost hull), and NPC contract freight is the taker's to sell.
		/// </summary>
		/// <summary>A freight or passage contract whose destination has fallen (abandoned, a ruin): it cannot be delivered.</summary>
		static bool DestinationGone(const cs::Ref<Contract>& c);

		void Close(const cs::Ref<Contract>& c, ContractState state, double jd, const cs::String& why, bool pay = true, bool blame = true);

		static void Standing(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& f, double delta);

		/// <summary>Forgets the oldest closed contracts past <see cref="HistoryCap"/>.</summary>
		void Trim();

	public:
		/// <summary>A board line in words: the job, the pay, the deadline, and where the money comes from.</summary>
		static cs::String Describe(const cs::Ref<Contract>& c, double jd);

	private:
		static cs::String Cr(double v);
	private:
		Contracts() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Contracts); // ObjectGraph registration (Persistence/Registrations)
	};

	const char* ToString(Contracts::TakeOutcome v);
}
