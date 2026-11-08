#pragma once
// US-T105: objectives with lasting consequences. The galaxy's live wars (InterstellarWars) put objectives to the SpaceGod:
// help one side take a settlement it is fighting for (the Kesh taking the base on Eris), or help the other hold it. How
// the SpaceGod brings it about is its own affair (a helm in either navy, a word in a squad's ear, nothing at all): only
// the outcome is judged, by who holds the settlement. And wars are no longer forgotten at the treaty: a settlement that
// changes hands between enemies drives part of its people out as a refugee tribe that flees, resettles among its own (or
// wherever it is let in), remembers who beat it (and the SpaceGod, if the SpaceGod helped), and in the years after stages
// revenge plots against its old home and assassination attempts against the victor's governors or the SpaceGod itself.
//
// Everything here is galaxy state changed only by the simulation, with no random draws: the review runs once a day on a
// fixed grid of whole days, settlements are walked in system and settlement order, and each plot's odds are rolled from a
// fixed hash of its tribe, its count and its day, so every lockstep peer and every replay reaches the same consequences.
// Held on Territory::Consequences and made by the game (LastingConsequences::Of): a galaxy run without it (the Bench's
// Core-only parity runs, older saves until the game first updates them) is exactly as it was.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

#include <cstdint>

namespace SpaceGod::Core
{
	class Conflict;
	class Faction;
	class Galaxy;
	class Settlement;
	class StarSystem;
	class World;

	/// <summary>What an objective asks of the SpaceGod (US-T105).</summary>
	enum class GodObjectiveKind : int
	{
		/// <summary>See its side take the settlement from the other.</summary>
		Take,
		/// <summary>See its side still hold the settlement when the war ends.</summary>
		Hold,
	};
	SPACEGODCORE_API const char* ToString(GodObjectiveKind v);

	/// <summary>Where an objective stands (US-T105).</summary>
	enum class GodObjectiveStatus : int
	{
		/// <summary>Put to the SpaceGod, not yet taken up.</summary>
		Offered,
		/// <summary>Taken up: the SpaceGod has sided with <see cref="GodObjective.Side"/> in this war.</summary>
		Accepted,
		/// <summary>Turned down, or set aside when the SpaceGod took up the other side's.</summary>
		Declined,
		/// <summary>Carried through, however it came about.</summary>
		Completed,
		/// <summary>The war went the other way.</summary>
		Failed,
		/// <summary>Never taken up, and the war is over (or the settlement gone).</summary>
		Lapsed,
	};
	SPACEGODCORE_API const char* ToString(GodObjectiveStatus v);

	/// <summary>One objective a live war puts to the SpaceGod (US-T105).</summary>
	class SPACEGODCORE_API GodObjective final
	{
	public:
		/// <summary>Numbered from 1 in the order offered (the console's handle on it).</summary>
		int Id = 0;

		cs::Ref<Conflict> War;

		/// <summary>The side the SpaceGod would be helping, and the side it would be helping against.</summary>
		cs::Ref<Faction> Side, Against;

		GodObjectiveKind Kind = GodObjectiveKind::Take;

		/// <summary>The settlement fought over, and the system it is in.</summary>
		cs::Ref<Settlement> Target;
		cs::Ref<StarSystem> System;

		/// <summary>The settlement, its body and its system in words, as offered ("Eris Base (Eris) in Sol").</summary>
		cs::String Place;

		GodObjectiveStatus Status = GodObjectiveStatus::Offered;

		double OfferedJd = 0, AcceptedJd = cs::Double::NaN, ResolvedJd = cs::Double::NaN;

		bool Open() const { return Status == GodObjectiveStatus::Offered || Status == GodObjectiveStatus::Accepted; }
	};

	/// <summary>A wrong a refugee tribe remembers (US-T105): who did it, and whether it has been avenged.</summary>
	class SPACEGODCORE_API Grudge final
	{
	public:
		/// <summary>The faction that beat them, or null when the grudge is against the SpaceGod.</summary>
		cs::Ref<Faction> Against;

		/// <summary>Against the SpaceGod, which helped the victor.</summary>
		bool SpaceGod = false;

		double SinceJd = 0;

		/// <summary>Avenged by a plot that came off (NaN while it is still owed).</summary>
		double SettledJd = cs::Double::NaN;

		bool Owed() const { return cs::Double::IsNaN(SettledJd); }
	};

	/// <summary>Where a refugee tribe is in its story (US-T105).</summary>
	enum class TribeStage : int
	{
		/// <summary>On the road from the home it lost.</summary>
		Fleeing,
		/// <summary>Settled among others, nursing its grudges and plotting.</summary>
		Resettled,
		/// <summary>Home again: its old settlement rose and went back to its own people.</summary>
		Returned,
		/// <summary>Its grudges avenged or forgotten: it has become one with the people it settled among.</summary>
		Assimilated,
		/// <summary>Nowhere would take it in, or too few of it are left: scattered.</summary>
		Scattered,
	};
	SPACEGODCORE_API const char* ToString(TribeStage v);

	/// <summary>The people a lost settlement drove out (US-T105).</summary>
	class SPACEGODCORE_API RefugeeTribe final
	{
	public:
		/// <summary>Numbered from 1 in the order they fled.</summary>
		int Id = 0;

		/// <summary>What they call themselves ("the Eris exiles").</summary>
		cs::String Name;

		/// <summary>The faction they were and are loyal to, and the home they lost (and the system it is in).</summary>
		cs::Ref<Faction> Origin;
		cs::Ref<Settlement> Home;
		cs::Ref<StarSystem> HomeSystem;

		/// <summary>The faction that took their home from them.</summary>
		cs::Ref<Faction> Victor;

		/// <summary>How many of them there are.</summary>
		double People = 0;

		TribeStage Stage = TribeStage::Fleeing;

		double FledJd = 0, ResettledJd = cs::Double::NaN, EndedJd = cs::Double::NaN;

		/// <summary>Where they settled (null while fleeing or scattered), and the system it is in.</summary>
		cs::Ref<Settlement> Refuge;
		cs::Ref<StarSystem> RefugeSystem;

		cs::List<cs::Ref<Grudge>> Grudges = cs::List<cs::Ref<Grudge>>();

		/// <summary>When they next strike (NaN: not yet planned).</summary>
		double NextPlotJd = cs::Double::NaN;

		/// <summary>Plots staged so far, and how many came off.</summary>
		int Plots = 0, PlotsSucceeded = 0;

		bool Active() const { return Stage == TribeStage::Fleeing || Stage == TribeStage::Resettled; }

		/// <summary>The oldest grudge still owed, the SpaceGod's last (they settle with the victor first), or null.</summary>
		cs::Ref<Grudge> NextGrudge() const;
	};

	/// <summary>What a refugee tribe's plot aims at (US-T105).</summary>
	enum class PlotKind : int
	{
		/// <summary>Stir their old home up against the victor's garrison: sabotage, riots and, when the garrison is thin, a rising.</summary>
		Revenge,
		/// <summary>Kill the victor's governor of their old home (or of its capital).</summary>
		Assassination,
		/// <summary>Strike at the SpaceGod itself, through the body it last wore.</summary>
		AssassinateSpaceGod,
	};
	SPACEGODCORE_API const char* ToString(PlotKind v);

	/// <summary>One plot a refugee tribe staged, and how it went (US-T105).</summary>
	class SPACEGODCORE_API RevengePlot final
	{
	public:
		cs::Ref<RefugeeTribe> Tribe;
		PlotKind Kind = PlotKind::Revenge;

		/// <summary>The faction struck at (null for the SpaceGod), and the settlement it was struck at (null for the SpaceGod).</summary>
		cs::Ref<Faction> Target;
		cs::Ref<Settlement> Where;

		double Jd = 0;

		/// <summary>The odds it had, and whether it came off.</summary>
		double Odds = 0;
		bool Succeeded = false;

		/// <summary>What happened, in a line (as logged).</summary>
		cs::String Outcome;
	};

	/// <summary>Who held a settlement at the last review (US-T105), and who it was occupied from, to see it change hands.</summary>
	class SPACEGODCORE_API OwnerMark final
	{
	public:
		cs::Ref<SpaceGod::Core::Settlement> Settlement;
		cs::Ref<Faction> Owner, OccupiedFrom;
	};

	/// <summary>
	/// Objectives with lasting consequences (US-T105, see the file comment), reviewed once a day from <see cref="Galaxy.Update"/>
	/// after the wars. Each review it:
	///  - offers the SpaceGod a pair of objectives for each live war that has none: the aggressor's to take the defender's
	///    most populous settlement in its first goal still held (else, every goal taken, the defender's to take back the
	///    most populous it has lost), and the other side's to hold (or keep) it;
	///  - judges every open objective by the outcome alone: a Take is done the day its side holds the settlement, a Hold
	///    the day the war ends with its side still holding it; an accepted one carried through earns Influence
	///    (<see cref="ObjectiveXp"/>), one never taken up lapses with the war;
	///  - sees every settlement that changed hands between two powers at war (taken, or ceded at the peace, but not taken
	///    back) drive <see cref="FleeShare"/> of its people out as a refugee tribe with a grudge against the victor, and one
	///    against the SpaceGod when the SpaceGod had sided with the victor in that war;
	///  - resettles each tribe after <see cref="FleeDays"/> at the nearest standing settlement of its own faction (else of
	///    anyone not the victor nor at war with them), and from <see cref="BrewDays"/> on stages a plot every
	///    <see cref="PlotEveryDays"/> or so against its oldest owed grudge: revenge on its old home, then the victor's
	///    governor, and last of all the SpaceGod. A revenge that leaves the victor's garrison thin and the people restive
	///    raises the home back to the tribe's own faction and the tribe goes home; an assassination of the SpaceGod that
	///    comes off costs it Influence. A tribe with nothing left owed, or after <see cref="MaxPlots"/>, settles in for good.
	/// </summary>
	class SPACEGODCORE_API LastingConsequences final
	{
	public:
		/// <summary>Share of a settlement's people that flee a victor, and the fewest that make a tribe.</summary>
		static constexpr double FleeShare = 0.15;
		static constexpr double MinTribe = 500;

		/// <summary>Days a tribe is on the road before it resettles.</summary>
		static constexpr double FleeDays = 30;

		/// <summary>Days after resettling before a tribe's first plot, and between plots (each plus up to <see cref="PlotJitterDays"/>).</summary>
		static constexpr double BrewDays = 180;
		static constexpr double PlotEveryDays = 240;
		static constexpr double PlotJitterDays = 120;

		/// <summary>Plots a tribe stages at most before it settles in for good.</summary>
		static constexpr int MaxPlots = 4;

		/// <summary>Influence XP an accepted objective carried through earns (a contract is worth tens).</summary>
		static constexpr double ObjectiveXp = 150;

		/// <summary>Influence XP an assassination of the SpaceGod that comes off knocks back (never below its level's floor).</summary>
		static constexpr double AssassinationXpLoss = 120;

		/// <summary>The objectives offered so far, oldest first.</summary>
		cs::List<cs::Ref<GodObjective>> Objectives = cs::List<cs::Ref<GodObjective>>();

		/// <summary>Every refugee tribe so far, oldest first.</summary>
		cs::List<cs::Ref<RefugeeTribe>> Tribes = cs::List<cs::Ref<RefugeeTribe>>();

		/// <summary>Every plot staged so far, oldest first.</summary>
		cs::List<cs::Ref<RevengePlot>> Plots = cs::List<cs::Ref<RevengePlot>>();

		/// <summary>The galaxy's consequences, made the first time they are asked for (the game does, every frame).</summary>
		static cs::Ref<LastingConsequences> Of(Galaxy& galaxy);

		/// <summary>The galaxy's consequences if it has them, never making them.</summary>
		static cs::Ref<LastingConsequences> Find(const Galaxy& galaxy);

		/// <summary>The objective numbered <paramref name="id"/>, or null.</summary>
		cs::Ref<GodObjective> ObjectiveById(int id) const;

		/// <summary>The objectives still open (offered or accepted), oldest first.</summary>
		cs::List<cs::Ref<GodObjective>> OpenObjectives() const;

		/// <summary>The tribes still fleeing or plotting.</summary>
		cs::List<cs::Ref<RefugeeTribe>> ActiveTribes() const;

		/// <summary>
		/// The SpaceGod takes up <paramref name="o"/>: it sides with <see cref="GodObjective.Side"/> in that war, and the other
		/// side's offer is set aside. Returns null, or why it cannot (not offered, or already sided in that war).
		/// </summary>
		cs::String Accept(Galaxy& galaxy, const cs::Ref<GodObjective>& o, double jd);

		/// <summary>The SpaceGod turns <paramref name="o"/> down. Returns null, or why it cannot.</summary>
		cs::String Decline(const cs::Ref<GodObjective>& o, double jd);

		/// <summary>Whether the SpaceGod sided with <paramref name="side"/> in a war against <paramref name="against"/> (accepted, carried through or not).</summary>
		bool SidedWith(const cs::Ref<Faction>& side, const cs::Ref<Faction>& against) const;

		/// <summary>What <paramref name="o"/> asks, in words ("Help the Kesh take Eris Base (Eris) in Sol").</summary>
		static cs::String Describe(const cs::Ref<GodObjective>& o);

		/// <summary>A tribe at a glance, in words.</summary>
		static cs::String Describe(const cs::Ref<RefugeeTribe>& t);

		/// <summary>Runs the daily review when its day has come (see the class summary).</summary>
		void Update(Galaxy& galaxy, double jd);

	private:
		cs::List<cs::Ref<OwnerMark>> marks;
		double nextDailyJd = cs::Double::NaN;
		int nextObjectiveId = 1, nextTribeId = 1;

		void Offer(Galaxy& galaxy, double jd);
		void Judge(Galaxy& galaxy, double jd);
		void Resolve(Galaxy& galaxy, const cs::Ref<GodObjective>& o, GodObjectiveStatus status, double jd);
		void Watch(Galaxy& galaxy, double jd, bool quiet);
		void Flee(Galaxy& galaxy, const cs::Ref<Settlement>& s, const cs::Ref<Faction>& loser, const cs::Ref<Faction>& victor, double jd);
		void ReviewTribes(Galaxy& galaxy, double jd);
		bool Resettle(Galaxy& galaxy, const cs::Ref<RefugeeTribe>& t, double jd);
		void StagePlot(Galaxy& galaxy, const cs::Ref<RefugeeTribe>& t, double jd);

		/// <summary>A fixed roll in [0, 1) for a tribe's <paramref name="n"/>th draw on day <paramref name="jd"/>.</summary>
		static double Roll(int tribe, int n, double jd, std::uint32_t salt);

		SG_SERIALIZABLE(LastingConsequences); // ObjectGraph registration (LastingConsequences.cpp)
	};
}
