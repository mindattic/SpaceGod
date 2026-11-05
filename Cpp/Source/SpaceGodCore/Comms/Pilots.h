#pragma once
// US-T138: named pilots with life stories and careers.
//
// Any pilot the SpaceGod deals with (hails, embodies, fights, rescues) becomes a named character (NamedPilot), met once
// and kept from then on in Characters (the world's register, held on Law so it saves and rewinds with the rest of the
// system). Their past up to the meeting is read out of the simulation's own history, deterministically (the hull's
// faction and ports, the system's settlements, the fleet's older hulls of that faction, the battles and crimes on the
// record that name the hull), so two lockstep peers or a rewound session meeting the same pilot at the same moment get
// the same person. From then on their life is written by what happens to them: careers move up the pirate ladder or the
// lawful one (detention, rehabilitation, fighter school, pilot, officer, captain of their own capital ship).
//
// The rationalization rule: whatever the SpaceGod did in a body, its pilot did. When the SpaceGod leaves, every act done
// while embodied becomes the pilot's own choice in their story, each with a motive drawn from their personality and
// situation, with no doubt and no hint of being steered. Nothing anywhere records the SpaceGod: the beats name only the
// pilot, the act and the people it touched.
//
// Text: a beat is stored as data (its kind, slots and seed) and turned into words when shown, by the story teller
// (IStoryTeller). The template teller (a large pool of pregenerated sentences, StoryTemplates.cpp) is the default and
// the fallback; a BYOKey LLM teller (US-T139) can be set with Characters::SetTeller and is only ever presentation, so
// it never makes peers or a rewind disagree.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

#include <functional>

namespace SpaceGod::Core
{
	class Battle;
	class Faction;
	class Settlement;
	class Ship;
	class World;

	/// <summary>How a pilot came to be met (US-T138).</summary>
	enum class MeetKind : int
	{
		Hailed,
		Embodied,
		Fought,
		Rescued,
		/// <summary>Named by someone else's story (a grudge, a betrayed captain) and met only through it.</summary>
		Mentioned,
	};
	SPACEGODCORE_API const char* ToString(MeetKind v);

	/// <summary>Which ladder a career climbs (US-T138, see <see cref="Careers"/>).</summary>
	enum class CareerTrack : int
	{
		/// <summary>Traders, miners, couriers, yacht owners: deckhand, pilot, master of their own hull.</summary>
		Civilian,
		/// <summary>A navy's: pilot, officer, captain of a capital ship.</summary>
		Navy,
		/// <summary>The pirate ladder: recruit, pirate fighter, lieutenant (by betrayal), captain, warlord, pirate king.</summary>
		Pirate,
		/// <summary>Taken alive and turned in: detention centre, rehabilitation, fighter school, pilot, officer, captain.</summary>
		Reform,
	};
	SPACEGODCORE_API const char* ToString(CareerTrack v);

	/// <summary>What one beat of a life is (US-T138): the story teller turns it into words.</summary>
	enum class LifeEventKind : int
	{
		Born,
		Joined,
		FlewShip,
		SurvivedBattle,
		LostShip,
		Grudge,
		Crime,
		Promoted,
		Demoted,
		Detained,
		Rehabilitated,
		Graduated,
		GivenCommand,
		Betrayal,
		TurnedPirate,
		Robbed,
		WasRobbed,
		Surrendered,
		Spared,
		Threatened,
		Traded,
		Met,
		/// <summary>An act done in their body, owned and justified afterwards (the rationalization rule).</summary>
		OwnedAct,
		CrownedKing,
	};
	SPACEGODCORE_API const char* ToString(LifeEventKind v);

	/// <summary>
	/// One beat of a pilot's life (US-T138): when, what, and the details the story teller fills its sentence with, as
	/// key/value pairs (place, ship, faction, rival, good, motive, ...). The words are made when it is shown.
	/// </summary>
	class SPACEGODCORE_API LifeEvent final
	{
	public:
		double Jd = 0;
		LifeEventKind Kind = LifeEventKind::Met;

		/// <summary>Which of the pool's sentences it is told with, and how an LLM teller seeds its wording.</summary>
		int Seed = 0;

		/// <summary>The details, as alternate keys and values ("place", "Ceres Hub", "ship", "Kestrel", ...).</summary>
		cs::List<cs::String> Slots;

		/// <summary>Done in the pilot's own body while the SpaceGod held it: told, from the moment it is left, as their own
		/// choice with <c>motive</c> filled in (empty until then, and the beat is told plainly).</summary>
		bool Owned = false;

		cs::String Slot(const cs::String& key) const;
		void Set(const cs::String& key, const cs::String& value);
	};

	/// <summary>A pilot's temperament (US-T138), each 0..1, fixed at birth from their seed; what they say and do reads it.</summary>
	struct SPACEGODCORE_API PilotTraits
	{
		/// <summary>Stands and fights (1) or folds (0).</summary>
		double Courage = 0.5;
		/// <summary>Wants more than they have.</summary>
		double Greed = 0.5;
		/// <summary>Keeps their word and the law.</summary>
		double Honour = 0.5;
		/// <summary>Talks easily and kindly.</summary>
		double Warmth = 0.5;
		/// <summary>Holds on to a slight.</summary>
		double Spite = 0.5;
		/// <summary>How they talk: 0 clipped and formal, 1 loose and chatty.</summary>
		double Chatter = 0.5;
	};

	/// <summary>
	/// A named character (US-T138): a pilot the SpaceGod has dealt with, with their past and their career. Their current
	/// hull is <see cref="Ship"/> (null once they are ashore: detained, at school, or between ships).
	/// </summary>
	class SPACEGODCORE_API NamedPilot final
	{
	public:
		int Id = 0;
		cs::String Name;
		/// <summary>What the lanes call them, or empty ("Lanebreaker").</summary>
		cs::String Callsign;
		int Seed = 0;
		/// <summary>0 she, 1 he: how the story speaks of them.</summary>
		int Pronoun = 0;
		double BornJd = 0;
		cs::String Birthplace;
		cs::Ref<Core::Faction> Faction;
		cs::Ref<Core::Ship> Ship;

		/// <summary>The hull they were met in, by name: kept when <see cref="Ship"/> is gone.</summary>
		cs::String ShipName;

		double Courage = 0.5, Greed = 0.5, Honour = 0.5, Warmth = 0.5, Spite = 0.5, Chatter = 0.5;
		PilotTraits Traits() const;

		CareerTrack Track = CareerTrack::Civilian;
		/// <summary>Their rung on <see cref="Track"/>'s ladder (<see cref="Careers.Title"/>).</summary>
		int Rank = 0;
		/// <summary>What they have done that the lanes talk about: battles, prizes, robberies, kills. Promotion reads it.</summary>
		double Renown = 0;
		/// <summary>When they took their present rung, and (on the reform ladder) when they were taken in.</summary>
		double RankJd = 0, DetainedJd = 0;
		/// <summary>The settlement they are held at or trained at while ashore, by name.</summary>
		cs::String Ashore;

		/// <summary>Has turned on their own: the pirate ladder's lieutenant needs it.</summary>
		bool Betrayed = false;

		/// <summary>Hulls flown, oldest first (names), battles lived through, and robberies done.</summary>
		cs::List<cs::String> ShipsFlown;
		int BattlesSurvived = 0, Robberies = 0, TimesRobbed = 0, Surrenders = 0;

		/// <summary>Who they hold a grudge against, by name (a pilot, a ship, a faction), newest last.</summary>
		cs::List<cs::String> Grudges;

		cs::List<cs::Ref<LifeEvent>> Story;

		MeetKind FirstMet = MeetKind::Hailed;
		double MetJd = 0;
		int TimesHailed = 0, TimesEmbodied = 0;

		/// <summary>Held by the SpaceGod now: the acts done meanwhile are marked <see cref="LifeEvent.Owned"/> and justified
		/// when it leaves (<see cref="Characters.Released"/>). Never shown to anyone.</summary>
		bool Held = false;

		/// <summary>Their age in years at <paramref name="jd"/>.</summary>
		int AgeAt(double jd) const;

		/// <summary>"Pirate fighter Sefa Voss", "Officer Lena Okafor".</summary>
		cs::String Styled() const;

		cs::String ToString() const { return Name; }

		cs::Ref<LifeEvent> Add(double jd, LifeEventKind kind, int seed);
	};

	/// <summary>The words for one beat (US-T138): what a story teller is handed.</summary>
	struct SPACEGODCORE_API StoryBeat
	{
		cs::Ref<NamedPilot> Pilot;
		cs::Ref<LifeEvent> Event;
		/// <summary>True: in their own voice ("I ..."), for a hail; false: told of them ("She ...").</summary>
		bool FirstPerson = false;
		double NowJd = 0;
	};

	/// <summary>
	/// What turns a beat into a sentence (US-T138). The template teller is the default and the fallback; an LLM teller
	/// (US-T139, BYOKey) implements this and is set with <see cref="Characters.SetTeller"/>. A teller returns empty when it
	/// cannot answer (no key, offline, timed out), and the template teller is used instead. Presentation only: the beats
	/// themselves are the state, so whatever a teller says, the simulation, a rewind and every co-op peer agree.
	/// </summary>
	class SPACEGODCORE_API IStoryTeller
	{
	public:
		virtual ~IStoryTeller() = default;
		virtual cs::String Name() const = 0;
		virtual cs::String Tell(const StoryBeat& beat) = 0;
		/// <summary>A motive for an act owned after the SpaceGod left (the rationalization rule), in the pilot's voice.</summary>
		virtual cs::String Motive(const cs::Ref<NamedPilot>& pilot, const cs::Ref<LifeEvent>& act) = 0;
	};

	/// <summary>The default teller (US-T138): a large pool of pregenerated sentences per beat, picked by the beat's seed.</summary>
	class SPACEGODCORE_API TemplateStoryTeller final : public IStoryTeller
	{
	public:
		cs::String Name() const override { return "templates"; }
		cs::String Tell(const StoryBeat& beat) override;
		cs::String Motive(const cs::Ref<NamedPilot>& pilot, const cs::Ref<LifeEvent>& act) override;

		/// <summary>How many sentences the pool holds, all beats together (the manual quotes it).</summary>
		static int PoolSize();
	};

	/// <summary>The career ladders (US-T138).</summary>
	class SPACEGODCORE_API Careers final
	{
	public:
		/// <summary>The rung's name: "recruit", "pirate fighter", ..., "pirate king"; "detainee", ..., "captain".</summary>
		static cs::String Title(CareerTrack track, int rank);
		static int TopRank(CareerTrack track);

		/// <summary>The renown a rung asks for (the reform ladder's first rungs ask for time served instead).</summary>
		static double RenownFor(CareerTrack track, int rank);

		/// <summary>What stands between the pilot and the next rung, or empty when nothing does but renown and time.</summary>
		static cs::String NextStep(const cs::Ref<NamedPilot>& p, double jd);

		/// <summary>Days served in detention before rehabilitation, then before fighter school, then before the first posting.</summary>
		static constexpr double DetentionDays = 30;
		static constexpr double RehabilitationDays = 90;
		static constexpr double SchoolDays = 120;
	};

	/// <summary>
	/// The register of named characters in one system (US-T138), held on <see cref="Law.Pilots"/> so it saves, loads and
	/// rewinds with the system. Everything here is deterministic: a pilot met at a moment is the same person on every peer.
	/// Nothing in it runs unless the SpaceGod deals with someone, so the simulation and the Bench never see it.
	/// </summary>
	class SPACEGODCORE_API Characters final
	{
	public:
		cs::List<cs::Ref<NamedPilot>> All;
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<NamedPilot>> ByShip;
		int NextId = 1;

		/// <summary>What one faction's crews hold against another's over what was done to them through comms (US-T137): robbery,
		/// threats, refused quarter, keyed "victim|offender", in credits' worth of harm. Their hails read it (a frostier
		/// stance), so the faction of the body the SpaceGod used is left holding the consequences.</summary>
		cs::Dictionary<cs::String, double> Grievance;

		double GrievanceOf(const cs::Ref<Core::Faction>& victim, const cs::Ref<Core::Faction>& offender) const;
		void AddGrievance(const cs::Ref<Core::Faction>& victim, const cs::Ref<Core::Faction>& offender, double harm);

		/// <summary>The world's register, made the first time it is asked for (absent from older saves).</summary>
		static cs::Ref<Characters> Of(const cs::Ref<World>& world);

		/// <summary>The named pilot of <paramref name="ship"/> if one has been met, else null.</summary>
		cs::Ref<NamedPilot> Find(const cs::Ref<Ship>& ship) const;

		/// <summary>
		/// The pilot of <paramref name="ship"/> as a named character, met now if not before: named, and their past up to
		/// <paramref name="jd"/> read from the simulation's history. Later calls give the same person and only advance
		/// their career to <paramref name="jd"/>.
		/// </summary>
		cs::Ref<NamedPilot> Meet(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, MeetKind how, double jd);

		/// <summary>The SpaceGod has taken <paramref name="ship"/>: its pilot is met (if need be) and held.</summary>
		cs::Ref<NamedPilot> Embodied(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd);

		/// <summary>
		/// The SpaceGod has left <paramref name="ship"/>'s pilot (the rationalization rule): every act marked
		/// <see cref="LifeEvent.Owned"/> while held is given a motive from their personality and situation and becomes their
		/// own choice, with no doubt in it. Their career moves on what they did.
		/// </summary>
		void Released(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd);

		/// <summary>Records an act by <paramref name="pilot"/> (owned if they are held now) and its renown; returns the beat.</summary>
		cs::Ref<LifeEvent> Act(const cs::Ref<NamedPilot>& pilot, LifeEventKind kind, double jd, double renown, std::initializer_list<cs::String> slots);

		/// <summary>The pilot fought in <paramref name="battle"/> and came out of it (counted once per battle).</summary>
		void SurvivedBattle(const cs::Ref<NamedPilot>& pilot, const cs::Ref<Battle>& battle, double jd);

		/// <summary>The pilot turned on their own side or captain (the pirate ladder's way up).</summary>
		void Betrayal(const cs::Ref<NamedPilot>& pilot, const cs::String& whom, double jd);

		/// <summary>
		/// The pilot is taken alive and turned in at <paramref name="at"/> (the reform ladder's first rung): out of their
		/// hull, held at the detention centre there. Time served carries them up the ladder (<see cref="Advance"/>).
		/// </summary>
		void Detain(const cs::Ref<NamedPilot>& pilot, const cs::String& at, const cs::Ref<Core::Faction>& by, double jd);

		/// <summary>Grudge held by <paramref name="pilot"/> against <paramref name="whom"/> (most recent last, no repeats).</summary>
		void Grudge(const cs::Ref<NamedPilot>& pilot, const cs::String& whom, double jd, const cs::String& why);

		/// <summary>
		/// Brings the pilot's career up to <paramref name="jd"/>: renown earns the next rung, the pirate lieutenant's needs a
		/// betrayal, the reform ladder's early rungs are time served (each beat dated when it fell due, so the result never
		/// depends on when it was looked at). A posting, on the reform ladder, puts them in a hull of their faction's.
		/// </summary>
		void Advance(const cs::Ref<World>& world, const cs::Ref<NamedPilot>& pilot, double jd);

		/// <summary>One beat in words, by the teller (falling back on the templates).</summary>
		static cs::String Tell(const cs::Ref<NamedPilot>& pilot, const cs::Ref<LifeEvent>& e, bool firstPerson, double jd);

		/// <summary>Their life so far in words, oldest first, one sentence a beat (at most <paramref name="max"/>, the newest).</summary>
		static cs::List<cs::String> LifeStory(const cs::Ref<NamedPilot>& pilot, double jd, bool firstPerson = false, int max = 12);

		/// <summary>The story teller (US-T139 sets an LLM one); null puts the template teller back.</summary>
		static void SetTeller(cs::Ref<IStoryTeller> teller);
		static const cs::Ref<IStoryTeller>& Teller();

		/// <summary>The pilot name generator: deterministic in its seed (pirates take the name the law already uses).</summary>
		static cs::String NameFor(const cs::Ref<Ship>& ship, int seed);

		/// <summary>A stable 32-bit mix, the one every draw here goes through.</summary>
		static uint32_t Mix(uint32_t x);
		static uint32_t HashText(const cs::String& s);
		/// <summary>A draw in [0, 1) from a seed and a salt.</summary>
		static double Roll(int seed, uint32_t salt);

	private:
		void Backstory(const cs::Ref<World>& world, const cs::Ref<NamedPilot>& p, double jd);
		void Promote(const cs::Ref<World>& world, const cs::Ref<NamedPilot>& p, double jd, const cs::String& why);
		void Post(const cs::Ref<World>& world, const cs::Ref<NamedPilot>& p, double jd, bool capital);

		static cs::Ref<IStoryTeller>& tellerSlot();

		SG_SERIALIZABLE(Characters);
	};
}
