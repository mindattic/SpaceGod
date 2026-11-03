#pragma once
// Port of Unity.SpaceGod Core/Systems/Statecraft.cs (US-T15, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Faction;
	class Polities;
	class Galaxy;
	class World;
	class Statecraft;
	class Pact;
	class Proposal;
	class Appraisal;
	class ToggleSetting;
	class RangeSetting;

	/// <summary>
	/// A standing agreement between two polities (U.SPC-T427): a non-aggression pact, a trade pact, an alliance, tribute or
	/// vassalage. What it does is its kind's: whether it keeps the two at peace, whether each joins the other's wars, and its
	/// daily upkeep (trade, tribute, a vassal's levy). A new kind is one <see cref="Register"/> call.
	/// </summary>
	class SPACEGODCORE_API PactKind final
	{
	public:
		static const cs::List<cs::Ref<PactKind>>& All();

		cs::String Name, Description;

		/// <summary>One side gives and the other takes (<see cref="Pact.A"/> pays or serves <see cref="Pact.B"/>); else both alike.</summary>
		bool Directed = false;

		/// <summary>Neither side may go to war with the other while it is in force, short of breaking it.</summary>
		bool KeepsPeace = false;

		/// <summary>Each side joins the wars the other is attacked in (a vassal joins every war of its overlord).</summary>
		bool CallsToArms = false;

		/// <summary>How long it runs (days); NaN for until it is broken.</summary>
		double Days = 0;

		/// <summary>How much warmer it makes each side to the other (the opinion it draws toward, see <see cref="Statecraft.Opinion"/>).</summary>
		double Warmth = 0;

		/// <summary>Its upkeep each day; false when it can no longer be kept (the payer cannot pay), and it lapses.</summary>
		cs::Func<bool, Statecraft*, cs::Ref<Pact>, double> Daily;

		static cs::Ref<PactKind> Register(cs::String name, cs::String description, bool directed, bool keepsPeace, bool callsToArms, double days,
			double warmth, cs::Func<bool, Statecraft*, cs::Ref<Pact>, double> daily = nullptr);

		static cs::Ref<PactKind> Find(const cs::String& name);

		static const cs::Ref<PactKind>& NonAggression();

		static const cs::Ref<PactKind>& Trade();

		static const cs::Ref<PactKind>& Alliance();

		static const cs::Ref<PactKind>& Tribute();

		static const cs::Ref<PactKind>& Vassalage();

		cs::String ToString() const { return Name; }

	private:
		PactKind(cs::String name, cs::String description, bool directed, bool keepsPeace, bool callsToArms, double days, double warmth,
			cs::Func<bool, Statecraft*, cs::Ref<Pact>, double> daily);

		static cs::List<cs::Ref<PactKind>>& all();
		struct Statics;
		static const Statics& statics();
		static cs::Ref<PactKind> Add(cs::String name, cs::String description, bool directed, bool keepsPeace, bool callsToArms, double days,
			double warmth, cs::Func<bool, Statecraft*, cs::Ref<Pact>, double> daily);
	private:
		PactKind() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(PactKind); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>A pact in force between two polities (U.SPC-T427). For a directed kind <see cref="A"/> pays or serves <see cref="B"/>.</summary>
	class SPACEGODCORE_API Pact final
	{
	public:
		cs::Ref<PactKind> Kind;
		cs::Ref<Faction> A, B;
		double SinceJd = 0, UntilJd = cs::Double::NaN;

		/// <summary>Tribute a month (credits).</summary>
		double Amount = 0;

		bool Involves(const cs::Ref<Faction>& f) const { return f != nullptr && (A == f || B == f); }

		bool Between(const cs::Ref<Faction>& x, const cs::Ref<Faction>& y) const { return (A == x && B == y) || (A == y && B == x); }

		cs::Ref<Faction> Other(const cs::Ref<Faction>& f) const { return f == A ? B : f == B ? A : nullptr; }

		cs::String Describe() const;

		cs::String ToString() const { return Describe(); }
	};

	class Deal;

	/// <summary>What one polity puts to another (U.SPC-T427): a <see cref="Deal"/> from <see cref="From"/> to <see cref="To"/>, with a sum for tribute.</summary>
	class SPACEGODCORE_API Proposal final
	{
	public:
		cs::Ref<::SpaceGod::Core::Deal> Deal;
		cs::Ref<Faction> From, To;
		double Amount = 0;
		double Jd = 0;

		cs::String Describe() const;

		cs::String ToString() const { return Describe(); }
	};

	/// <summary>How a polity weighs a proposal: a score (above zero it agrees) and its reasons, the weightiest first.</summary>
	class SPACEGODCORE_API Appraisal final
	{
	public:
		/// <summary>C#'s (double Weight, string Why).</summary>
		struct Reason
		{
			double Weight = 0;
			cs::String Why;
		};

		double Score = 0;
		cs::List<Reason> Reasons = cs::List<Reason>();

		bool Accepted() const { return Score > 0; }

		Appraisal* Add(double weight, const cs::String& why);

		/// <summary>The reason that weighed most on the side it came down on.</summary>
		cs::String Main() const;
	};

	/// <summary>
	/// Something one polity can put to another across the table (U.SPC-T427): peace, a pact, tribute offered or demanded,
	/// fealty offered or demanded. Each says when it cannot be offered, how the other side weighs it (the same for a polity
	/// the AI runs and for an answer the player's envoy reads), and what it does when agreed. A new deal is one
	/// <see cref="Register"/> call.
	/// </summary>
	class SPACEGODCORE_API Deal final
	{
	public:
		static const cs::List<cs::Ref<Deal>>& All();

		cs::String Name, Description;

		/// <summary>Whether it carries a sum (tribute a month).</summary>
		bool HasAmount = false;

		/// <summary>Why it cannot be put from one to the other now, or null.</summary>
		cs::Func<cs::String, Statecraft*, cs::Ref<Faction>, cs::Ref<Faction>, double> Unavailable;

		/// <summary>How the side it is put to weighs it.</summary>
		cs::Func<cs::Ref<Appraisal>, Statecraft*, cs::Ref<Proposal>, double> Appraise;

		/// <summary>What it does once agreed.</summary>
		cs::Action<Statecraft*, cs::Ref<Proposal>, double> Enact;

		/// <summary>It in words, for the news and the envoy screen.</summary>
		cs::Func<cs::String, const Proposal&> Words;

		static cs::Ref<Deal> Register(cs::String name, cs::String description, bool hasAmount, cs::Func<cs::String, Statecraft*, cs::Ref<Faction>, cs::Ref<Faction>, double> unavailable,
			cs::Func<cs::Ref<Appraisal>, Statecraft*, cs::Ref<Proposal>, double> appraise, cs::Action<Statecraft*, cs::Ref<Proposal>, double> enact,
			cs::Func<cs::String, const Proposal&> words = nullptr);

		static cs::Ref<Deal> Find(const cs::String& name);

		cs::String Describe(const Proposal& p) const;
		cs::String Describe(const cs::Ref<Proposal>& p) const { return Describe(*p); }

		static const cs::Ref<Deal>& Peace();

		static const cs::Ref<Deal>& NonAggression();

		static const cs::Ref<Deal>& TradePact();

		static const cs::Ref<Deal>& Alliance();

		static const cs::Ref<Deal>& OfferTribute();

		static const cs::Ref<Deal>& DemandTribute();

		static const cs::Ref<Deal>& OfferFealty();

		static const cs::Ref<Deal>& DemandFealty();

		cs::String ToString() const { return Name; }

	private:
		Deal(cs::String name, cs::String description, bool hasAmount, cs::Func<cs::String, Statecraft*, cs::Ref<Faction>, cs::Ref<Faction>, double> unavailable,
			cs::Func<cs::Ref<Appraisal>, Statecraft*, cs::Ref<Proposal>, double> appraise, cs::Action<Statecraft*, cs::Ref<Proposal>, double> enact,
			cs::Func<cs::String, const Proposal&> words);

		static cs::String Cr(double v);

		static cs::List<cs::Ref<Deal>>& all();
		struct Statics;
		static const Statics& statics();
		static cs::Ref<Deal> Add(cs::String name, cs::String description, bool hasAmount, cs::Func<cs::String, Statecraft*, cs::Ref<Faction>, cs::Ref<Faction>, double> unavailable,
			cs::Func<cs::Ref<Appraisal>, Statecraft*, cs::Ref<Proposal>, double> appraise, cs::Action<Statecraft*, cs::Ref<Proposal>, double> enact,
			cs::Func<cs::String, const Proposal&> words);
	private:
		Deal() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Deal); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>One line of the envoy screen (U.SPC-T427): a deal, its sum, why it cannot be put (or null), and how the other side would take it.</summary>
	class EnvoyOption final
	{
	public:
		cs::Ref<::SpaceGod::Core::Deal> Deal;
		double Amount = 0;
		cs::String Unavailable;
		cs::Ref<Appraisal> Read;
	};

	/// <summary>A polity fighting at a friend's side (U.SPC-T427): it went to war with <see cref="Enemy"/> for <see cref="Friend"/>'s sake.</summary>
	class CoBelligerence final
	{
	public:
		cs::Ref<Faction> Ally, Friend, Enemy;
		double SinceJd = 0;
	};

	/// <summary>
	/// The diplomacy game (U.SPC-T427): every state, the player's own among them, holds an opinion of every other, signs pacts
	/// (non-aggression, trade, alliance, tribute, vassalage, see <see cref="PactKind"/>), puts deals to the others
	/// (<see cref="Deal"/>), declares war and makes peace. One set of rules for all: a proposal to a polity the AI runs is
	/// weighed by <see cref="Deal.Appraise"/> and answered at once; one to the player's polity waits in its <see cref="Inbox"/>,
	/// and the player's own proposals are weighed by the other side exactly as an AI's would be. The AI puts deals of its own
	/// each month (<see cref="AutoDiplomacy"/>). Pacts bind the wars (<see cref="InterstellarWars.Declare"/> and the states'
	/// ambitions respect them), allies and vassals are called to arms when their friend is attacked, tribute and a vassal's levy
	/// move money between treasuries and stir unrest among the people who pay it, and trade pacts pay both sides.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API Statecraft final
	{
	public:
		/// <summary>C#'s (Faction, Faction) tuple key.</summary>
		struct FactionPair
		{
			cs::Ref<Faction> Item1, Item2;
			bool operator==(const FactionPair& o) const { return Item1 == o.Item1 && Item2 == o.Item2; }
			size_t HashCode() const { return cs::HashCombine(cs::Hash<cs::Ref<Faction>>{}(Item1), cs::Hash<cs::Ref<Faction>>{}(Item2)); }
		};

		/// <summary>C#'s (Faction By, double Since).</summary>
		struct WarBegun
		{
			cs::Ref<Faction> By;
			double Since = 0;
		};

		cs::Ref<::SpaceGod::Core::Polities> Polities;

	private:
		cs::Random rng = cs::Random(4271);
		cs::Dictionary<FactionPair, double> opinions = cs::Dictionary<FactionPair, double>();
		cs::List<cs::Ref<Pact>> pacts = cs::List<cs::Ref<Pact>>();
		cs::List<cs::Ref<Proposal>> inbox = cs::List<cs::Ref<Proposal>>();
		cs::List<cs::Ref<CoBelligerence>> coBelligerents = cs::List<cs::Ref<CoBelligerence>>();
		cs::Dictionary<FactionPair, WarBegun> warsBegun = cs::Dictionary<FactionPair, WarBegun>();
		cs::List<cs::String> record = cs::List<cs::String>();
		double nextDailyJd = cs::Double::NaN, nextRoundJd = cs::Double::NaN;

	public:
		/// <summary>The galaxy whose wars a declaration starts and a peace settles (null for a system on its own).</summary>
		cs::Ref<::SpaceGod::Core::Galaxy> Galaxy;

		/// <summary>Whether the polities the AI runs put deals of their own each month. The default galaxy turns it on; a bare one (a test) leaves it off.</summary>
		bool AutoDiplomacy = false;

		static const cs::Ref<ToggleSetting>& AiDiplomacy();

		static const cs::Ref<RangeSetting>& OpinionPace();

		static const cs::Ref<RangeSetting>& TradePactIncome();

		static const cs::Ref<RangeSetting>& VassalLevy();

		/// <summary>Credits a day a trade pact pays each side, per thousand people of the smaller of the two (as a square root).</summary>
		static constexpr double TradePerDay = 2e4;

		/// <summary>Unrest a day among the people of a polity paying tribute or a vassal's levy, up to <see cref="TributeUnrestCap"/>.</summary>
		static constexpr double TributeUnrestPerDay = 0.002; static constexpr double TributeUnrestCap = 0.4;

		/// <summary>Days between the AI's rounds of proposals.</summary>
		static constexpr double RoundDays = 30;

		/// <summary>Days a proposal to the player waits for an answer before it is taken as declined.</summary>
		static constexpr double InboxDays = 30;

		explicit Statecraft(cs::Ref<::SpaceGod::Core::Polities> polities) { Polities = polities; }

		/// <summary>The polity the player speaks for (the one commanded from the galaxy map), or null: then every polity is the AI's.</summary>
		cs::Ref<Faction> Player() const;

		const cs::List<cs::Ref<Pact>>& Pacts() const { return pacts; }

		/// <summary>Proposals put to the player's polity, waiting for an answer.</summary>
		const cs::List<cs::Ref<Proposal>>& Inbox() const { return inbox; }

		const cs::List<cs::Ref<CoBelligerence>>& CoBelligerents() const { return coBelligerents; }

		/// <summary>What has passed between the polities, newest last.</summary>
		const cs::List<cs::String>& Record() const { return record; }

		/// <summary>Whether <paramref name="f"/> plays the diplomacy game: a state (no company, free trader or raider clan).</summary>
		static bool Party(const cs::Ref<Faction>& f);

		/// <summary>Every state in play.</summary>
		cs::List<cs::Ref<Faction>> Parties() const;

		// ---------------------------------------------------------------- opinion

		/// <summary>What <paramref name="of"/> thinks of <paramref name="toward"/>, -100 (hatred) to 100 (fast friends).</summary>
		double Opinion(const cs::Ref<Faction>& of, const cs::Ref<Faction>& toward) const;

		/// <summary>Moves <paramref name="of"/>'s opinion of <paramref name="toward"/> by <paramref name="by"/>.</summary>
		void Sway(const cs::Ref<Faction>& of, const cs::Ref<Faction>& toward, double by);

		/// <summary>What <paramref name="of"/>'s opinion of <paramref name="toward"/> drifts toward: its pacts, wars, rivals and losses.</summary>
		double Baseline(const cs::Ref<Faction>& of, const cs::Ref<Faction>& toward);

		/// <summary>The strength of <paramref name="f"/>'s whole navy (at least 1).</summary>
		double Power(const cs::Ref<Faction>& f);

	private:
		/// <summary>Each navy's strength, held while a day is reckoned (null outside it, so a change shows at once).</summary>
		cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> powerCache;

	public:
		// ---------------------------------------------------------------- pacts

		cs::Ref<Pact> PactBetween(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, const cs::Ref<PactKind>& kind) const;

		cs::List<cs::Ref<Pact>> PactsOf(const cs::Ref<Faction>& f) const;

		cs::Ref<Pact> TributeBetween(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const { return PactBetween(a, b, PactKind::Tribute()); }

		/// <summary><paramref name="vassal"/>'s overlord, or null.</summary>
		cs::Ref<Faction> OverlordOf(const cs::Ref<Faction>& vassal) const;

		/// <summary>Whether one of the two is the other's vassal.</summary>
		bool Liege(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const { return OverlordOf(a) == b || OverlordOf(b) == a; }

		/// <summary>Whether a pact keeps <paramref name="a"/> and <paramref name="b"/> from war (the states' ambitions and <see cref="InterstellarWars.Declare"/> respect it).</summary>
		bool BoundToPeace(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const;

		/// <summary>Signs a pact of <paramref name="kind"/> (<paramref name="a"/> the payer or vassal of a directed one) from <paramref name="jd"/>.</summary>
		cs::Ref<Pact> Sign(const cs::Ref<PactKind>& kind, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd, double amount = 0);

		/// <summary>
		/// <paramref name="by"/> breaks <paramref name="p"/>: the other side resents it, the more for a pact that kept the peace
		/// or called it to arms, and every other state trusts <paramref name="by"/> a little less. A vassal breaking its vassalage
		/// declares its independence.
		/// </summary>
		bool Break(const cs::Ref<Pact>& p, const cs::Ref<Faction>& by, double jd, const cs::String& why = cs::String());

	private:
		/// <summary>A timed pact runs out.</summary>
		void Expire(const cs::Ref<Pact>& p, double jd);

	public:
		// internal in C#
		bool KeepTrade(const cs::Ref<Pact>& p);

		bool KeepTribute(const cs::Ref<Pact>& p, double jd);

		bool KeepVassalage(const cs::Ref<Pact>& p, double jd);

	private:
		/// <summary>Paying another power is resented by the people who pay it (civ dynamics: unrest rises in its ports).</summary>
		void Stir(const cs::Ref<Faction>& payer);

		double People(const cs::Ref<Faction>& f) const;

	public:
		// ---------------------------------------------------------------- proposals

		/// <summary>Why <paramref name="deal"/> cannot be put from <paramref name="from"/> to <paramref name="to"/> now, or null.</summary>
		cs::String Unavailable(const cs::Ref<Deal>& deal, const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd);

		/// <summary>How <paramref name="to"/> would weigh <paramref name="deal"/> from <paramref name="from"/> (what an envoy reads of the mood).</summary>
		cs::Ref<Appraisal> Weigh(const cs::Ref<Deal>& deal, const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd, double amount = 0);

		/// <summary>
		/// <paramref name="from"/> puts <paramref name="deal"/> to <paramref name="to"/>. A polity the AI runs weighs it and
		/// answers at once (agreed, it is enacted); the player's polity gets it in its <see cref="Inbox"/>. Returns null and the
		/// answer (<paramref name="answer"/>: agreed, refused or waiting, with the main reason), or why it could not be put.
		/// </summary>
		cs::String Propose(const cs::Ref<Deal>& deal, const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd, cs::Ref<Appraisal>& answer, double amount = 0);

		/// <summary>The player answers a proposal in the <see cref="Inbox"/>: agreed, it is enacted; refused, the other side remembers.</summary>
		bool Answer(const cs::Ref<Proposal>& p, bool accept, double jd);

		// ---------------------------------------------------------------- acts

		/// <summary>
		/// <paramref name="by"/> declares war on <paramref name="against"/>: every pact that kept the two at peace is broken, and
		/// across the galaxy it is a war with goals (<see cref="InterstellarWars.Declare"/>) when one can be fought, else a war
		/// in the one diplomacy table, fought wherever their ships meet. The defender's allies and vassals are called to arms.
		/// Returns null, or why it cannot be declared.
		/// </summary>
		cs::String DeclareWar(const cs::Ref<Faction>& by, const cs::Ref<Faction>& against, double jd);

		/// <summary>
		/// <paramref name="a"/> and <paramref name="b"/> make peace: the war between them ends (a war across the galaxy is settled
		/// by its treaty the next day, see <see cref="InterstellarWars"/>; one only in the table gets a white peace and a truce),
		/// and their friends who fought at their side make peace too.
		/// </summary>
		void MakePeace(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd, const cs::String& why);

		/// <summary><paramref name="by"/> gives <paramref name="to"/> <paramref name="amount"/> credits: goodwill, the more the bigger it is to them.</summary>
		cs::String Gift(const cs::Ref<Faction>& by, const cs::Ref<Faction>& to, double amount, double jd);

		/// <summary>A sum worth offering or demanding as tribute from <paramref name="payer"/> a month.</summary>
		static double SuggestedTribute(const cs::Ref<Faction>& payer);

		/// <summary>A gift worth sending from <paramref name="giver"/>: a hundredth of its treasury, rounded.</summary>
		static double SuggestedGift(const cs::Ref<Faction>& giver);

		// ---------------------------------------------------------------- the envoy

		/// <summary>
		/// What <paramref name="from"/>'s envoy can put to <paramref name="to"/> now (the envoy screen): every <see cref="Deal"/>,
		/// with the sum it would carry, why it cannot be put (or null), and, when it can, how the other side would weigh it:
		/// the same <see cref="Weigh"/> the answer comes from, so what the envoy reads is what <see cref="Propose"/> gets.
		/// </summary>
		cs::List<cs::Ref<EnvoyOption>> Envoy(const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd);

		/// <summary>An opinion in a word.</summary>
		static cs::String Mood(double opinion);

		/// <summary>
		/// <paramref name="from"/> hails <paramref name="to"/> (one of its ships at the helm, or its envoy from the map): its answer
		/// in words, by how it stands with and thinks of the hailer, and any proposals of its own waiting on the hailer's answer.
		/// </summary>
		cs::String Hail(const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd);

		// ---------------------------------------------------------------- weighing

		// internal in C#
		cs::String NotAtPeace(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const;

		/// <summary>How many days <paramref name="a"/> and <paramref name="b"/> have been at war.</summary>
		double WarDays(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd) const;

	private:
		static FactionPair Key(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b);

		void Began(const cs::Ref<Faction>& by, const cs::Ref<Faction>& against, double jd);

		/// <summary>Whether <paramref name="f"/> has a common enemy with <paramref name="g"/> (both at war with, or both loathing, a third).</summary>
		bool CommonEnemy(const cs::Ref<Faction>& f, const cs::Ref<Faction>& g) const;

		double Ratio(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) { return Power(a) / Power(b); }

		static double Clamp(double v, double lo, double hi) { return cs::Math::Max(lo, cs::Math::Min(hi, v)); }

	public:
		// internal in C#
		cs::Ref<Appraisal> AppraisePeace(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them, double jd);

		cs::Ref<Appraisal> AppraiseNonAggression(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them);

		cs::Ref<Appraisal> AppraiseTrade(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them);

		cs::Ref<Appraisal> AppraiseAlliance(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them);

		cs::Ref<Appraisal> AppraiseTributeReceived(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them, double amount);

		cs::Ref<Appraisal> AppraiseTributePaid(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them, double amount);

		cs::Ref<Appraisal> AppraiseVassalTaken(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them);

		cs::Ref<Appraisal> AppraiseFealty(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them, double jd);

		// ---------------------------------------------------------------- the day

		void Update(double jd);

	private:
		void Daily(double jd);

		bool Alive(const cs::Ref<Faction>& f) const;

		/// <summary>
		/// Allies and vassals join the wars their friend is attacked in (a vassal every war of its overlord): each is at war
		/// with the attacker in the diplomacy table, so their fleets fight wherever they meet, until their friend makes peace.
		/// An ally that loathes the friend it is called for refuses, and the alliance is broken.
		/// </summary>
		void CallToArms(double jd);

		/// <summary>A friend at peace again with the enemy its ally fought for: the ally makes peace too.</summary>
		void StandDown(double jd);

		/// <summary>A demand refused (or tribute not paid): a power strong enough, not bound to peace and with no war on, may take it by force.</summary>
		void Spurned(const cs::Ref<Faction>& by, const cs::Ref<Faction>& refuser, double jd);

		/// <summary>
		/// The AI's round (monthly): each state it runs looks at each other state and puts the one deal that suits it best (the
		/// same deals the player has, weighed by the same rules on the other side): peace when weary of a war, an alliance with a
		/// friend against a common enemy, a non-aggression pact with a power that worries it, a trade pact with a state it likes,
		/// tribute from a weak neighbour it dislikes; and a vassal grown strong throws off its overlord.
		/// </summary>
		void Round(double jd);

		// ---------------------------------------------------------------- the news

		void Tell(double jd, const cs::String& text, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b);

		static cs::String Capitalise(const cs::String& s);

		static cs::String Lower(const cs::String& s);

	public:
		/// <summary>How <paramref name="f"/> stands with <paramref name="g"/>, in words: war, peace, and the pacts between them.</summary>
		cs::String Standing(const cs::Ref<Faction>& f, const cs::Ref<Faction>& g) const;
	private:
		Statecraft() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Statecraft); // ObjectGraph registration (Persistence/Registrations)
	};
}
