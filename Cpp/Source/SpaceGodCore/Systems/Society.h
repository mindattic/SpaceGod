#pragma once
// US-T273: the people of the galaxy, living with nobody at the controls.
//
// Polities, wars, pirate clans, colonisation, companies and the territory review already move the states and the money.
// This is the layer underneath them: named families on every standing world, the businesses they run, and what they get
// up to with each other.
//  - Households: each settlement keeps a few founding families (more the bigger it is), each with its members, its wealth
//    and its business. People are born, marry into other houses (in other systems too), grow old and die; heirs take the
//    business on. A settlement that falls drives its families off to another world of the same flag, or scatters them.
//  - Businesses: trading houses, shipwrights, mining concerns, refineries, brokers, shippers, media houses and hotels.
//    They earn with their world's wellbeing and lose with its unrest and its owner's wars; they open branches where their
//    owner's ships may dock and close them when the two go to war (trade cut off); a broke business is bought by the
//    richest rival in its system; a rich house sets a younger member up in a new one.
//  - Trips: people message each other, plan a trip (business, a conference, a family visit, a holiday, a honeymoon), set
//    off on the planned day, travel (days in-system, weeks between the stars), stay and come home. A trip to a world its
//    traveller's flag has gone to war with is called off; a trip through a pirate-held system may be robbed.
//  - Conferences: the big worlds host summits on a trade (announced months ahead and advertised); businesses in that line
//    send delegates; deals sealed there warm their owners to each other and pay the businesses.
//  - Bribery: a business buys a governor's favour now and then; it pays, until it is exposed (likelier on a restless
//    world): a scandal, the governor gone, the business fined, the world more restless.
//  - The feed: news of all this, the businesses' commercials, the scam spam pirate clans send, and the people's own
//    messages to each other, a rolling record the game shows; notable items go to the system's log, so the relays carry
//    them out as news (SystemReport::Notable).
//
// The SpaceGod can lay a hand on any of it (Expose, Bribe, Send, Fund, Ruin): what follows is the simulation's own.
//
// Everything is galaxy state changed only by the simulation, with no random draws: the step runs once a day on a fixed grid
// of whole days, settlements are walked in system and settlement order, and every choice is rolled from a fixed hash of
// the ids and the day, so every lockstep peer and every replay reaches the same society. Held on Territory::Society and
// made by the game (Society::Of): a galaxy run without it (the Bench's Core-only parity runs, older saves until the game
// first makes it) is exactly as it was.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

#include <cstdint>

namespace SpaceGod::Core
{
	class Faction;
	class Galaxy;
	class Settlement;
	class StarSystem;
	class World;
	class Household;
	class Business;
	class Conference;
	enum class WorldEventKind : int;
	// US-T275: the Civilization layer's persona and organisations (Systems/Civilization/Civilization.h).
	class Tie;
	class Org;
	class SocialEvent;
	enum class Ambition : int;

	/// <summary>What someone is in their house (US-T273).</summary>
	enum class CitizenRole : int
	{
		/// <summary>The head of the house, who runs its business.</summary>
		Head,
		Spouse,
		/// <summary>A grown son or daughter, who may marry out, take over or found a business of their own.</summary>
		Heir,
		Child,
		/// <summary>The governor of the house's world, for its owner (the one a bribe buys).</summary>
		Governor,
	};
	SPACEGODCORE_API const char* ToString(CitizenRole v);

	/// <summary>One named person (US-T273).</summary>
	class SPACEGODCORE_API Citizen final
	{
	public:
		/// <summary>Numbered from 1 in the order they were met (born or founded).</summary>
		int Id = 0;
		cs::String Given;
		cs::Ref<SpaceGod::Core::Household> House;
		CitizenRole Role = CitizenRole::Child;
		double BornJd = 0, DiedJd = cs::Double::NaN;

		/// <summary>Who they married (null: single), and the trip they are on (0: at home).</summary>
		cs::Ref<Citizen> Spouse;
		int TripId = 0;

		/// <summary>Credits taken in bribes (a governor's).</summary>
		double Bribes = 0;

		// US-T275 Persona (Systems/Civilization): set by the Civilization layer, absent from older saves.
		/// <summary>Whether the persona has been given (traits rolled), its traits (a Trait bit set) and what they want most.</summary>
		bool Persona = false;
		int Traits = 0;
		SpaceGod::Core::Ambition Goal{};
		/// <summary>-1..1: what people think of them; 0..1: how good they are at what they do.</summary>
		double Reputation = 0, Skill = 0.5;
		/// <summary>Their ties to people outside the family (friends, rivals, lovers, patrons, enemies).</summary>
		cs::List<cs::Ref<SpaceGod::Core::Tie>> Ties = cs::List<cs::Ref<SpaceGod::Core::Tie>>();
		/// <summary>The faith they keep (null: none) and the organisations they belong to beyond it (a party, a union, a school).</summary>
		cs::Ref<SpaceGod::Core::Org> Faith;
		cs::List<cs::Ref<SpaceGod::Core::Org>> Orgs = cs::List<cs::Ref<SpaceGod::Core::Org>>();

		bool Alive() const { return cs::Double::IsNaN(DiedJd); }
		double AgeYears(double jd) const { return (jd - BornJd) / 365.25; }
		cs::String FullName() const;
		cs::String ToString() const { return FullName(); }
	};

	/// <summary>A family on one world (US-T273).</summary>
	class SPACEGODCORE_API Household final
	{
	public:
		int Id = 0;
		cs::String Surname;
		cs::Ref<SpaceGod::Core::Settlement> Home;
		cs::Ref<StarSystem> System;
		cs::List<cs::Ref<Citizen>> Members = cs::List<cs::Ref<Citizen>>();
		cs::Ref<SpaceGod::Core::Business> Business;

		/// <summary>The house's own money, credits (apart from its business's capital).</summary>
		double Wealth = 0;

		double FoundedJd = 0;

		/// <summary>Scattered: its world fell and nowhere of its flag would have it.</summary>
		bool Scattered = false;

		/// <summary>When it next has news of its own (a birth, a wedding, a death, a new venture).</summary>
		double NextEventJd = cs::Double::NaN;

		cs::Ref<Citizen> Head() const;
		int Living() const;
		cs::String ToString() const { return cs::Cat("House ", Surname); }
	};

	/// <summary>What line of work a business is in (US-T273).</summary>
	enum class BusinessKind : int
	{
		TradingHouse,
		Shipwright,
		MiningConcern,
		Refinery,
		Brokerage,
		Shipping,
		Media,
		Hospitality,
	};
	SPACEGODCORE_API const char* ToString(BusinessKind v);

	/// <summary>Where a business stands (US-T273).</summary>
	enum class BusinessStatus : int
	{
		Trading,
		/// <summary>Bought by a rival in its system.</summary>
		Acquired,
		/// <summary>Wound up with nobody to buy it.</summary>
		Bankrupt,
	};
	SPACEGODCORE_API const char* ToString(BusinessStatus v);

	/// <summary>A family business (US-T273).</summary>
	class SPACEGODCORE_API Business final
	{
	public:
		int Id = 0;
		cs::String Name;
		BusinessKind Kind = BusinessKind::TradingHouse;
		cs::Ref<Household> Owner;
		cs::Ref<SpaceGod::Core::Settlement> Home;
		cs::Ref<StarSystem> System;

		/// <summary>Capital, credits, and last month's earnings (negative: a loss).</summary>
		double Capital = 0, LastMonth = 0;

		/// <summary>The worlds it keeps offices on away from home (trade established while their owners may dock).</summary>
		cs::List<cs::Ref<SpaceGod::Core::Settlement>> Branches = cs::List<cs::Ref<SpaceGod::Core::Settlement>>();

		BusinessStatus Status = BusinessStatus::Trading;
		double FoundedJd = 0, EndedJd = cs::Double::NaN;

		/// <summary>The business that bought it (when acquired).</summary>
		cs::Ref<Business> Buyer;

		bool Trading() const { return Status == BusinessStatus::Trading; }
		cs::String ToString() const { return Name; }
	};

	/// <summary>Why someone travels (US-T273).</summary>
	enum class TripPurpose : int
	{
		Business,
		Conference,
		FamilyVisit,
		Holiday,
		Honeymoon,
		// US-T285: the Civilization layer's journeys.
		Pilgrimage,
		Study,
		Festival,
		Match,
	};
	SPACEGODCORE_API const char* ToString(TripPurpose v);

	/// <summary>Where a trip stands (US-T273).</summary>
	enum class TripStage : int
	{
		/// <summary>Talked over and booked: they leave on <see cref="Trip.DepartJd"/>.</summary>
		Planned,
		Outbound,
		There,
		Homeward,
		Done,
		Cancelled,
	};
	SPACEGODCORE_API const char* ToString(TripStage v);

	/// <summary>One person's journey (US-T273).</summary>
	class SPACEGODCORE_API Trip final
	{
	public:
		int Id = 0;
		cs::Ref<Citizen> Traveller;
		/// <summary>Who they go to see (a relative, a contact), or null.</summary>
		cs::Ref<Citizen> Meeting;
		TripPurpose Purpose = TripPurpose::Business;
		cs::Ref<Settlement> From, To;
		cs::Ref<StarSystem> FromSystem, ToSystem;
		cs::Ref<SpaceGod::Core::Conference> Conference;

		double PlannedJd = 0, DepartJd = 0, ArriveJd = 0, LeaveJd = 0, HomeJd = 0;
		TripStage Stage = TripStage::Planned;

		/// <summary>Why it was called off, or what befell it on the way.</summary>
		cs::String Note;

		/// <summary>Planned by the SpaceGod's hand (<see cref="Society.Send"/>).</summary>
		bool Steered = false;

		bool Active() const { return Stage != TripStage::Done && Stage != TripStage::Cancelled; }
	};

	/// <summary>A summit one world hosts on one line of trade (US-T273).</summary>
	class SPACEGODCORE_API Conference final
	{
	public:
		int Id = 0;
		cs::String Name;
		BusinessKind Trade = BusinessKind::TradingHouse;
		cs::Ref<Settlement> Host;
		cs::Ref<StarSystem> System;
		double AnnouncedJd = 0, OpensJd = 0, ClosesJd = 0;
		int Delegates = 0, Deals = 0;
		bool Invited = false, Closed = false;
		/// <summary>The businesses that sent someone.</summary>
		cs::List<cs::Ref<SpaceGod::Core::Business>> Attending = cs::List<cs::Ref<SpaceGod::Core::Business>>();
	};

	/// <summary>A governor bought (US-T273).</summary>
	class SPACEGODCORE_API Bribe final
	{
	public:
		int Id = 0;
		cs::Ref<SpaceGod::Core::Business> Briber;
		cs::Ref<Citizen> Official;
		cs::Ref<Faction> Government;
		cs::Ref<Settlement> Where;
		double Amount = 0, Jd = 0;
		/// <summary>What it bought ("the dock licences", ...).</summary>
		cs::String Favour;
		bool Exposed = false;
		double ExposedJd = cs::Double::NaN;
		/// <summary>Paid by the SpaceGod's hand (<see cref="Society.BribeFor"/>), or brought to light by it (<see cref="Society.Expose"/>).</summary>
		bool Steered = false;
	};

	/// <summary>What one feed item is (US-T273).</summary>
	enum class FeedKind : int
	{
		News,
		Commercial,
		Spam,
		/// <summary>One person writing to another.</summary>
		Message,
	};
	SPACEGODCORE_API const char* ToString(FeedKind v);

	/// <summary>One item in the galaxy's feed (US-T273).</summary>
	class SPACEGODCORE_API FeedItem final
	{
	public:
		double Jd = 0;
		FeedKind Kind = FeedKind::News;
		cs::String From, To;
		cs::Ref<StarSystem> System;
		cs::String Text;
		/// <summary>US-T275: the Civilization event it shows (worded by the Narrator when shown; Text is then empty), or null.</summary>
		cs::Ref<SpaceGod::Core::SocialEvent> Event;
		cs::String ToString() const;
	};

	/// <summary>
	/// The galaxy's people (US-T273, see the file comment), run from <see cref="Galaxy.Update"/> once a day. Held on
	/// <see cref="Territory.Society"/>; made by the game (<see cref="Of"/>), so a Core-only run has none.
	/// </summary>
	class SPACEGODCORE_API Society final
	{
	public:
		/// <summary>Days between censuses (new houses, fallen worlds) and business books.</summary>
		static constexpr double MonthDays = 30;

		/// <summary>Houses a settlement keeps: one per order of magnitude of its people past a thousand, from one to this.</summary>
		static constexpr int MaxHouses = 6;

		/// <summary>Trips under way at once across the galaxy, and conferences open or announced at once.</summary>
		static constexpr int MaxTrips = 240;
		static constexpr int MaxConferences = 6;

		/// <summary>Feed items kept (oldest dropped first), and ended trips kept for the record.</summary>
		static constexpr int MaxFeed = 400;
		static constexpr int KeepEndedTrips = 120;

		/// <summary>A year's share of capital a business earns on a world at full wellbeing and no unrest (assumed).</summary>
		static constexpr double YieldPerYear = 0.08;

		/// <summary>Days of travel between two systems, per light-year (gates and drives, assumed), and the least in-system.</summary>
		static constexpr double DaysPerLy = 4;
		static constexpr double InSystemDays = 3;

		int NextId = 1;

		cs::List<cs::Ref<Household>> Houses = cs::List<cs::Ref<Household>>();
		cs::List<cs::Ref<Business>> Businesses = cs::List<cs::Ref<Business>>();
		cs::List<cs::Ref<Trip>> Trips = cs::List<cs::Ref<Trip>>();
		cs::List<cs::Ref<Conference>> Conferences = cs::List<cs::Ref<Conference>>();
		cs::List<cs::Ref<Bribe>> Bribes = cs::List<cs::Ref<Bribe>>();
		cs::List<cs::Ref<FeedItem>> Feed = cs::List<cs::Ref<FeedItem>>();

		/// <summary>Running tallies (for the log and the tests).</summary>
		int Births = 0, Deaths = 0, Weddings = 0, TripsTaken = 0, TripsCancelled = 0, Robberies = 0, Scandals = 0,
			Founded = 0, Acquisitions = 0, Bankruptcies = 0, BranchesOpened = 0, BranchesClosed = 0;

		/// <summary>The galaxy's society (made on first use; the game makes it).</summary>
		static cs::Ref<Society> Of(Galaxy& galaxy);
		/// <summary>The galaxy's society, or null if the game has not made it.</summary>
		static cs::Ref<Society> Find(const Galaxy& galaxy);

		/// <summary>Runs every day due up to <paramref name="jd"/>.</summary>
		void Update(Galaxy& galaxy, double jd);

		cs::Ref<Citizen> CitizenById(int id) const;
		cs::Ref<Business> BusinessById(int id) const;
		cs::Ref<Bribe> BribeById(int id) const;
		cs::Ref<Household> HouseById(int id) const;

		/// <summary>The houses of one settlement (all standing, if null).</summary>
		cs::List<cs::Ref<Household>> HousesOf(const cs::Ref<Settlement>& s) const;
		/// <summary>The trips under way or planned.</summary>
		cs::List<cs::Ref<Trip>> ActiveTrips() const;
		/// <summary>The last <paramref name="n"/> feed items, newest last.</summary>
		cs::List<cs::Ref<FeedItem>> Latest(int n) const;

		static cs::String Describe(const cs::Ref<Trip>& t, double jd);
		static cs::String Describe(const cs::Ref<Business>& b);
		static cs::String Describe(const cs::Ref<Bribe>& b);

		// ---------------------------------------------------------------- the SpaceGod's hand (each returns "" or why not)

		/// <summary>Brings a bribe to light: the scandal follows as if it had come out on its own.</summary>
		cs::String Expose(Galaxy& galaxy, const cs::Ref<Bribe>& b, double jd);
		/// <summary>Has a business buy its world's governor now.</summary>
		cs::String BribeFor(Galaxy& galaxy, const cs::Ref<Business>& b, double jd);
		/// <summary>Puts a trip in someone's head: they write to say so and go on business to <paramref name="to"/>.</summary>
		cs::String Send(Galaxy& galaxy, const cs::Ref<Citizen>& c, const cs::Ref<Settlement>& to, double jd);
		/// <summary>An anonymous backer puts <paramref name="credits"/> into a business.</summary>
		cs::String Fund(Galaxy& galaxy, const cs::Ref<Business>& b, double credits, double jd);

		/// <summary>
		/// US-T285: books a trip for the Civilization layer (a pilgrimage, a festival, a match, a course of study): null when the
		/// traveller is away already, too young, or the galaxy has as many trips under way as it keeps.
		/// </summary>
		/// <summary>US-T288: a bribe brought to light by the simulation itself (a journalist's digging), not the SpaceGod's hand; false if already public.</summary>
		bool Reveal(Galaxy& galaxy, const cs::Ref<Bribe>& b, double jd);

		cs::Ref<Trip> Book(Galaxy& galaxy, const cs::Ref<Citizen>& c, const cs::Ref<Settlement>& to, TripPurpose why, const cs::Ref<Citizen>& meeting, double jd);
		/// <summary>A run on a business: its capital is gone and it goes to the wall (or a rival) at once.</summary>
		cs::String Ruin(Galaxy& galaxy, const cs::Ref<Business>& b, double jd);

	private:
		double nextDayJd = cs::Double::NaN;
		double nextMonthJd = cs::Double::NaN;

		void Day(Galaxy& galaxy, double jd);
		void Month(Galaxy& galaxy, double jd);
		void Census(Galaxy& galaxy, double jd);
		void Rehome(Galaxy& galaxy, const cs::Ref<Household>& h, double jd);
		cs::Ref<Household> FoundHouse(Galaxy& galaxy, const cs::Ref<StarSystem>& sys, const cs::Ref<Settlement>& s, double jd, int seed);
		cs::Ref<Citizen> Born(const cs::Ref<Household>& h, CitizenRole role, double bornJd, std::uint32_t seed);
		cs::Ref<Business> Open(const cs::Ref<Household>& h, double jd, std::uint32_t seed, double capital);
		void Life(Galaxy& galaxy, const cs::Ref<Household>& h, double jd);
		void Books(Galaxy& galaxy, const cs::Ref<Business>& b, double jd);
		void Branches(Galaxy& galaxy, const cs::Ref<Business>& b, double jd);
		void Close(Galaxy& galaxy, const cs::Ref<Business>& b, double jd, bool forced);
		void PlanTrips(Galaxy& galaxy, double jd);
		cs::Ref<Trip> Plan(Galaxy& galaxy, const cs::Ref<Citizen>& c, const cs::Ref<Settlement>& to, TripPurpose why, const cs::Ref<Citizen>& meeting, double jd, std::uint32_t h);
		void Travel(Galaxy& galaxy, const cs::Ref<Trip>& t, double jd);
		void Conferences_(Galaxy& galaxy, double jd);
		void Bribery(Galaxy& galaxy, double jd);
		cs::Ref<Bribe> Pay(Galaxy& galaxy, const cs::Ref<Business>& b, double jd, std::uint32_t h, bool steered);
		void Scandal(Galaxy& galaxy, const cs::Ref<Bribe>& b, double jd);
		void Media(Galaxy& galaxy, double jd);
		cs::Ref<Citizen> GovernorOf(Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd);
		void Post(FeedKind kind, double jd, const cs::Ref<StarSystem>& sys, cs::String from, cs::String to, cs::String text);
		void Notable(Galaxy& galaxy, const cs::Ref<StarSystem>& sys, double jd, const cs::String& text, WorldEventKind kind);
		void Prune();

		SG_SERIALIZABLE(Society); // ObjectGraph registration (Society.cpp)
	};
}
