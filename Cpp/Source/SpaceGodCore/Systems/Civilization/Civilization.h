#pragma once
// US-T274/US-T275: the Civilization layer, the social systems on top of Society (US-T273) and the shared machinery that makes
// them drive each other.
//
//  - SocialEvent: every module says what happened as a structured event (a kind, the day, the world, the factions, the
//    people, houses, businesses and organisations in it, an amount and a seed), never as prose. The events are saved; the
//    words are not. They feed the galaxy's feed, the news relays (WorldEventKind::Society) and every module that reacts
//    to another (a feud to a death, an election to a scandal, a plague to a trip).
//  - Narrator (Narrator.h): turns an event into words when it is shown: the Prose pool spacegod/social-<kind> (Data/Lore,
//    docs/PROSE.md), else the built-in template, the line picked by the event's seed. Display only.
//  - Persona: every citizen's traits, ambition, reputation, skill and ties to other people (friends, rivals, lovers,
//    patrons, enemies), weighting what each module has them do, so a galaxy's history shapes its people.
//  - Civics: one record per standing settlement, the world's mood every module reads and writes: approval of its
//    government, ideology, crime, sickness, war weariness, fashion, cultural drift, trust in the press.
//  - Org: one generic organisation (party, faith, syndicate, union, bank, outlet, team, charity, mercenary company, school,
//    agency, insurgent cell) with a seat, a leader, members and funds, instead of a dozen bespoke membership systems.
//
// Held on Territory::Civilization and made by the game (Civilization::Of) like Society: a galaxy run without it (the Bench's
// Core-only runs, older saves until the game first makes it) is exactly as it was. Its modules change the wider simulation
// (unrest, treasuries, opinions, populations) only from here, so Core-run code paths never move. Everything runs once a day
// on a fixed grid of whole days, choices rolled from fixed hashes of ids and days (CivHash.h): no random draws.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

#include <cstdint>

namespace SpaceGod::Core
{
	class Business;
	class Citizen;
	class Faction;
	class Galaxy;
	class Household;
	class Settlement;
	class Society;
	class Org;
	class StarSystem;
	enum class FeedKind : int;

	// Module groups (US-T276..US-T283), each its own optional field so older saves load.
	class Kinship;
	class Politics;
	class Strife;
	class Underworld;
	class Commerce;
	class Culture;
	class Health;
	class Press;

	/// <summary>What a social event is (US-T274). Append only: saves hold the numbers, Prose keys hold the slugs.</summary>
	enum class SocialKind : int
	{
		None,
		// Society (US-T273), mirrored as events.
		Birth, Death, Wedding, BusinessFounded, Bankruptcy, Buyout, Scandal,
		// Persona (US-T275).
		Friendship, Rivalry, FallingOut, Mood,
		// Kinship (US-T276).
		FeudDeclared, FeudSabotage, FeudEnded, InheritanceDispute, HouseSplit, Affair, Divorce, HiddenHeir, Graduation, SchoolFounded,
		CelebrityRise, CelebrityScandal, Endorsement,
		// Politics (US-T277).
		IdeologyShift, PartyFounded, ElectionCalled, ElectionWon, ElectionRigged, Coup, CoupFailed, SuccessionCrisis, TrialOpened,
		Verdict, JudgeBribed, PeaceSummit, SummitFailed, PollPublished,
		// Strife (US-T278).
		Protest, Riot, CellFormed, Sabotage, Uprising, SpyCaught, PactLeaked, TechStolen, WarWeariness, Conscription, PeaceForced,
		MercsHired, MercsTurned, AdmiralRises, PrisonersExchanged,
		// Underworld (US-T279).
		SyndicateRises, Protection, SmugglingRun, SmugglerCaught, PrisonOpened, PrisonBreak, Assassination, AssassinationFoiled,
		Fraud, FraudExposed, CrimeWave, Crackdown,
		// Commerce (US-T280).
		UnionFormed, WageDeal, StrikeCalled, Refugees, Takeover, SharesCrash, BankFounded, LoanDefault, BankRun, PremiumsRise,
		AdWar, LandmarkFound, TouristBoom, PatentFiled, PatentStolen,
		// Culture (US-T281).
		FaithFounded, Conversion, Schism, Pilgrimage, HolyWar, Festival, MatchPlayed, MatchRigged, Championship, TrendSweeps,
		CulturalDrift, MonumentRaised, Chronicle, Rumour, Propaganda,
		// Health (US-T284).
		Outbreak, Spread, Quarantine, Cured, HospitalBuilt, MedicineShortage, BabyBoom, Ageing, Disaster, Relief, Famine, FoodAid,
		// Press (US-T283).
		OutletFounded, SlantedStory, BoardPost, Investigation, JournalistSilenced, CharityFounded, Donation,
		Count_
	};
	SPACEGODCORE_API const char* ToString(SocialKind v);

	/// <summary>What an organisation is (US-T275).</summary>
	enum class OrgKind : int
	{
		Party, Faith, Syndicate, Union, Bank, Outlet, Team, Charity, Mercenaries, School, Agency, Insurgents,
	};
	SPACEGODCORE_API const char* ToString(OrgKind v);

	/// <summary>A person's traits (US-T275), a bit set on <see cref="Citizen.Traits"/>.</summary>
	enum class Trait : int
	{
		Greedy = 1 << 0, Pious = 1 << 1, Reckless = 1 << 2, Ambitious = 1 << 3, Loyal = 1 << 4, Vengeful = 1 << 5,
		Charming = 1 << 6, Honest = 1 << 7, Cruel = 1 << 8, Curious = 1 << 9, Brave = 1 << 10, Gifted = 1 << 11,
	};
	SPACEGODCORE_API const char* ToString(Trait v);

	/// <summary>What a person wants most (US-T275).</summary>
	enum class Ambition : int
	{
		Wealth, Power, Fame, Faith, Family, Revenge, Knowledge, Peace,
	};
	SPACEGODCORE_API const char* ToString(Ambition v);

	/// <summary>How two people stand (US-T275); kin are read from the houses, not kept here.</summary>
	enum class TieKind : int
	{
		Friend, Rival, Lover, Patron, Enemy, Mentor,
	};
	SPACEGODCORE_API const char* ToString(TieKind v);

	/// <summary>One person's tie to another (US-T275).</summary>
	class SPACEGODCORE_API Tie final
	{
	public:
		cs::Ref<Citizen> Other;
		TieKind Kind = TieKind::Friend;
		/// <summary>0..1: how much it weighs.</summary>
		double Strength = 0.5;
		double SinceJd = 0;
	};

	/// <summary>One thing that happened, structured (US-T274). The Narrator words it when shown.</summary>
	class SPACEGODCORE_API SocialEvent final
	{
	public:
		int Id = 0;
		SocialKind Kind = SocialKind::None;
		double Jd = 0;
		cs::Ref<StarSystem> System;
		cs::Ref<Settlement> Where;
		cs::Ref<Faction> Faction, Rival;
		/// <summary>The people in it: who did it and to whom.</summary>
		cs::Ref<Citizen> Who, Whom;
		cs::Ref<Household> House, OtherHouse;
		cs::Ref<SpaceGod::Core::Business> Business;
		cs::Ref<SpaceGod::Core::Org> Org, OtherOrg;
		/// <summary>Credits, people, a share: whatever the kind counts.</summary>
		double Amount = 0;
		/// <summary>Picks the line the Narrator reads it with.</summary>
		std::uint32_t Seed = 0;
		/// <summary>A rumour or propaganda: told, not true (US-T281).</summary>
		bool Unverified = false;
		/// <summary>The outlet that ran it slanted (US-T283), and which way: -1 against, +1 for <see cref="Faction"/>.</summary>
		cs::Ref<SpaceGod::Core::Org> RunBy;
		int Slant = 0;
		/// <summary>Brought about by the SpaceGod's hand.</summary>
		bool Steered = false;

		/// <summary>The event in words, for a log or the console (Narrator::Render).</summary>
		cs::String ToString() const;
	};

	/// <summary>One standing settlement's mood (US-T275), the world every module reads and writes.</summary>
	class SPACEGODCORE_API Civics final
	{
	public:
		cs::Ref<Settlement> Where;
		cs::Ref<StarSystem> System;
		/// <summary>-1..1: how its people feel about the flag that rules it.</summary>
		double Approval = 0.2;
		/// <summary>-1 (dove) .. 1 (hawk), and -1 (the crown) .. 1 (the vote): its people's politics.</summary>
		double Hawk = 0, Liberty = 0;
		/// <summary>0..1: crime, how sick it is (share infected), how immune, how sick of war, how far its press is trusted.</summary>
		double Crime = 0.1, Infected = 0, Immune = 0, Weariness = 0, PressTrust = 0.6;
		/// <summary>0..1: its hospitals (lower deaths, faster cures) and its schools.</summary>
		double Care = 0.3, Learning = 0.3;
		/// <summary>The fashion sweeping it (an index into the trends), and how hot it runs (0..1).</summary>
		int Trend = 0;
		double TrendHeat = 0;
		/// <summary>0..1: how far it has drifted from its flag's core worlds in speech and custom.</summary>
		double Drift = 0;
		/// <summary>Its dominant faith (null: none to speak of) and that faith's share of its people.</summary>
		cs::Ref<SpaceGod::Core::Org> Faith;
		double FaithShare = 0;
		/// <summary>Under quarantine until (NaN: open), and how many days of war its flag has fought running.</summary>
		double QuarantineUntil = cs::Double::NaN;
		double WarDays = 0;
		/// <summary>Its landmark, if travellers come to see one ("" none), and a monument to a past event.</summary>
		cs::String Landmark, Monument;
		bool Quarantined(double jd) const { return !cs::Double::IsNaN(QuarantineUntil) && jd < QuarantineUntil; }
	};

	/// <summary>An organisation (US-T275).</summary>
	class SPACEGODCORE_API Org final
	{
	public:
		int Id = 0;
		OrgKind Kind = OrgKind::Party;
		/// <summary>Its name as the simulation made it; the game may show a Prose name in its place (Narrator::OrgName).</summary>
		cs::String Name;
		std::uint32_t NameSeed = 0;
		cs::Ref<Settlement> Seat;
		cs::Ref<StarSystem> System;
		/// <summary>The flag it works under or for (a party's state, an agency's master); null if it answers to none.</summary>
		cs::Ref<SpaceGod::Core::Faction> Faction;
		cs::Ref<Citizen> Leader;
		cs::List<cs::Ref<Citizen>> Members = cs::List<cs::Ref<Citizen>>();
		/// <summary>The house that owns or backs it (an outlet's owners, a charity's founders, a team's backers).</summary>
		cs::Ref<Household> Owner;
		/// <summary>Credits it holds, and how strong it is (0..1: a party's vote, a faith's reach, a syndicate's grip).</summary>
		double Funds = 0, Strength = 0.1;
		/// <summary>-1..1 on its own axis: a party's hawk/dove line, a faith's orthodoxy, an outlet's slant toward its flag.</summary>
		double Stance = 0;
		/// <summary>The one it is set against (a rival party, team, syndicate), and the one it split from (a schism).</summary>
		cs::Ref<Org> Rival, Parent;
		/// <summary>A running count the kind uses: a team's wins, a party's seats, a bank's loans out.</summary>
		int Score = 0;
		double FoundedJd = 0, EndedJd = cs::Double::NaN;
		bool Active() const { return cs::Double::IsNaN(EndedJd); }
		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// The galaxy's civilization (US-T274, see the file comment), run from <see cref="Galaxy.Update"/> once a day after
	/// <see cref="Society"/>. Held on <see cref="Territory.Civilization"/>; made by the game (<see cref="Of"/>).
	/// </summary>
	class SPACEGODCORE_API Civilization final
	{
	public:
		static constexpr double MonthDays = 30;
		static constexpr int MaxCatchUpDays = 400;
		/// <summary>Events kept (oldest dropped first), and ties one person keeps.</summary>
		static constexpr int MaxEvents = 800;
		static constexpr int MaxTies = 6;

		int NextId = 1;
		cs::List<cs::Ref<SocialEvent>> Events = cs::List<cs::Ref<SocialEvent>>();
		cs::List<cs::Ref<Civics>> Worlds = cs::List<cs::Ref<Civics>>();
		cs::List<cs::Ref<Org>> Orgs = cs::List<cs::Ref<Org>>();

		// The module groups (US-T276..US-T283), made on the first update, absent from saves made before them.
		cs::Ref<SpaceGod::Core::Kinship> Kinship;
		cs::Ref<SpaceGod::Core::Politics> Politics;
		cs::Ref<SpaceGod::Core::Strife> Strife;
		cs::Ref<SpaceGod::Core::Underworld> Underworld;
		cs::Ref<SpaceGod::Core::Commerce> Commerce;
		cs::Ref<SpaceGod::Core::Culture> Culture;
		cs::Ref<SpaceGod::Core::Health> Health;
		cs::Ref<SpaceGod::Core::Press> Press;

		/// <summary>Running tallies of events by kind (index = SocialKind).</summary>
		cs::List<int> Tally = cs::List<int>();

		static cs::Ref<Civilization> Of(Galaxy& galaxy);
		static cs::Ref<Civilization> Find(const Galaxy& galaxy);

		void Update(Galaxy& galaxy, double jd);

		/// <summary>
		/// Records an event: kept on <see cref="Events"/>, put on the society's feed, and (when its kind is notable) logged on its
		/// system so the relays carry it. Fills the id and, when 0, the seed. Returns it.
		/// </summary>
		cs::Ref<SocialEvent> Publish(Galaxy& galaxy, const cs::Ref<SocialEvent>& e, bool feed = true, bool log = true);
		/// <summary>A new event of <paramref name="kind"/> on <paramref name="jd"/> (filled in by the caller, then <see cref="Publish"/>ed).</summary>
		static cs::Ref<SocialEvent> Make(SocialKind kind, double jd, const cs::Ref<Settlement>& where, const cs::Ref<StarSystem>& sys);

		cs::Ref<Civics> CivicsOf(const cs::Ref<Settlement>& s) const;
		cs::Ref<Org> OrgById(int id) const;
		cs::List<cs::Ref<Org>> OrgsOf(OrgKind kind, bool activeOnly = true) const;
		/// <summary>The last <paramref name="n"/> events (of one kind, unless None), newest last.</summary>
		cs::List<cs::Ref<SocialEvent>> Latest(int n, SocialKind kind = SocialKind::None) const;
		int Count(SocialKind k) const { int i = static_cast<int>(k); return i < Tally.Count() ? Tally[i] : 0; }

		/// <summary>Founds an organisation (named from the fallback pools by its seed) and records it.</summary>
		cs::Ref<Org> Found(OrgKind kind, const cs::Ref<Settlement>& seat, const cs::Ref<StarSystem>& sys, const cs::Ref<Faction>& faction,
			const cs::Ref<Citizen>& leader, double funds, double jd, std::uint32_t seed);

		// ---------------------------------------------------------------- Persona (US-T275)
		static bool Has(const cs::Ref<Citizen>& c, Trait t);
		/// <summary>The tie <paramref name="a"/> keeps to <paramref name="b"/>, or null.</summary>
		static cs::Ref<Tie> TieOf(const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b);
		/// <summary>Ties two people both ways (replacing a weaker tie, keeping <see cref="MaxTies"/> each).</summary>
		static void Bind(const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b, TieKind kind, double strength, double jd);
		static void Unbind(const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b);
		/// <summary>Every living adult of the society (in house and member order).</summary>
		static cs::List<cs::Ref<Citizen>> Adults(const Society& society, double jd);

		/// <summary>Moves a world's people: shifts its unrest (0..1) and its approval of its flag.</summary>
		void Stir(const cs::Ref<Settlement>& s, double unrest, double approval);

		/// <summary>Whether a faction is at war with any other (an occupation or an open war).</summary>
		static bool AtWar(Galaxy& galaxy, const cs::Ref<Faction>& f);
		/// <summary>The first faction <paramref name="f"/> is at war with (in war order), or null.</summary>
		static cs::Ref<Faction> EnemyOf(Galaxy& galaxy, const cs::Ref<Faction>& f);
		static bool Hostile(Galaxy& galaxy, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b);
		static bool Stands(const cs::Ref<Settlement>& s);
		static cs::Ref<StarSystem> SystemOf(Galaxy& galaxy, const cs::Ref<Settlement>& s);
		static double Wellbeing(const cs::Ref<Settlement>& s);

	private:
		double nextDayJd = cs::Double::NaN;
		double nextMonthJd = cs::Double::NaN;
		int personaUpTo = 0;

		void Day(Galaxy& galaxy, double jd);
		void Month(Galaxy& galaxy, double jd);
		void Census(Galaxy& galaxy, double jd);
		void Personas(Society& society, double jd);
		void Ties(Galaxy& galaxy, Society& society, double jd);
		void Moods(Galaxy& galaxy, double jd);
		void Prune();

		SG_SERIALIZABLE(Civilization);
	};
}
