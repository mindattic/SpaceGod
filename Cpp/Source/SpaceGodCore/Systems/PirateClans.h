#pragma once
// Port of Assets/UnitySpace/Core/Systems/PirateClans.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Galaxy;
	class InterstellarTraffic;
	class Faction;
	class Settlement;
	class StarSystem;
	class Ship;
	class World;
	class Battle;
	class JumpGate;

	/// <summary>
	/// Interstellar piracy (U.SPC-T94): the pirate clans as powers that reach across the gates, run once a day by the
	/// <see cref="InterstellarTraffic"/>. A clan is one polity (<see cref="Polities"/>) wherever it holds havens, and:
	///
	///  - <b>raids where traffic bunches up.</b> Its bands lie in wait off a system's busiest jump gate as well as its
	///    hyperlanes and ports (see <see cref="War.GateAmbushes"/>), and what they take goes to the clan's own haven in
	///    that system, or home in their holds through the gates when they have none there;
	///  - <b>smuggles.</b> When a haven's stores fill with loot, a smuggler (one of the clan's own freighters, kept at
	///    the haven between runs) carries it through the gates to the black market of a lawless station in another
	///    system (<see cref="BlackMarket"/>) that pays more for it than the haven's own fences, and the clan is paid;
	///  - <b>grows and moves.</b> A rich clan sends a band through the gates to found a haven in an empty hide in a
	///    frontier belt or debris disc (<see cref="World.HavenSites"/>), and moves bands between its havens to keep each
	///    manned (in two systems at most); and a haven abroad that has stood a while may break away as a clan of its own;
	///  - <b>is hunted.</b> Every state it robs puts a price on it (<see cref="Bounty"/>), paid to whoever destroys its
	///    hulls, anywhere; those states send navy expeditions through the gates against its havens once they are found,
	///    and bounty hunters go after its bands in the systems where they work.
	///
	/// Kept small enough that a thin frontier system's merchants live through it: one smuggler out from a system at a
	/// time, a clan holds two havens at most, and founding, moving and splitting are rare.
	/// </summary>
	class PirateClans final
	{
	public:
		/// <summary>A gate route (GateNetwork.Route): null when there is no way through.</summary>
		using GateRoute = cs::Ref<cs::List<cs::Ref<JumpGate>>>;

		/// <summary>C#'s (StarSystem system, Settlement haven) tuple.</summary>
		struct HavenEntry
		{
			cs::Ref<StarSystem> system;
			cs::Ref<Settlement> haven;
		};

	private:
		// Back-references, not owned: InterstellarTraffic makes its PirateClans with its galaxy and `this`.
		Galaxy* galaxy;
		InterstellarTraffic* traffic;
		cs::Random rng;

		/// <summary>The price on each clan, by the state that posted it (credits).</summary>
		cs::Dictionary<cs::Ref<Faction>, cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>>> pledges;

		/// <summary>When each haven founded abroad was founded.</summary>
		cs::Dictionary<cs::Ref<Settlement>, double> founded;

		/// <summary>Clans that have resolved to found a haven abroad, and when: they send the first band fit to sail.</summary>
		cs::Dictionary<cs::Ref<Faction>, double> resolved;

	public:
		/// <summary>Smuggling runs set out, sales made, and what the sales brought in (credits).</summary>
		int Runs = 0;
		int Sales = 0;
		double SmuggledValue = 0;

		/// <summary>Bands sent to found a haven abroad, havens founded, bands moved between havens, clans split off, bounties paid
		/// (credits), hunts and expeditions sent.</summary>
		int Expansions = 0;
		int Foundings = 0;
		int Moves = 0;
		int Splits = 0;
		double BountiesPaid = 0;
		int Hunts = 0;
		int Expeditions = 0;

		/// <summary>A haven's loot worth (at base prices) this much is worth running to a black market.</summary>
		static constexpr double MinSmuggleValue = 3e6;

		/// <summary>A run must clear this over what the haven's own fences would pay, credits.</summary>
		static constexpr double MinSmuggleGain = 1e6;

		/// <summary>What a clan pays for a smuggler's hull (a used freighter, bought quietly), credits.</summary>
		static double RunnerPrice();

		/// <summary>The most systems one clan holds havens in.</summary>
		static constexpr int MaxSystems = 2;

		/// <summary>A clan founds a haven abroad only with this in its treasury, and pays <see cref="FoundingCost"/> for it.</summary>
		static constexpr double ExpandTreasury = 1.2e8;
		static constexpr double FoundingCost = 4e7;

		/// <summary>Daily chance a clan able to found a haven abroad resolves to (it sends the first band free to sail).</summary>
		static constexpr double ExpandChancePerDay = 0.02;

		/// <summary>Daily chance a clan moves a band to a haven of its that has none.</summary>
		static constexpr double MoveChancePerDay = 0.05;

		/// <summary>A haven abroad must have stood this long (days) before it may break away, with this daily chance.</summary>
		static constexpr double SplitMinDays = 180;
		static constexpr double SplitChancePerDay = 0.002;

		/// <summary>A robbed state puts this share of what was taken on the clan's head (all of it).</summary>
		static constexpr double PledgeShare = 1;

		/// <summary>The most one state keeps on one clan's head (credits), and how fast an old price is forgotten (days).</summary>
		static constexpr double MaxPledge = 3e8;
		static constexpr double PledgeMemoryDays = 180;

		/// <summary>What destroying one of a clan's hulls collects of the price on it: this share of the hull's price.</summary>
		static constexpr double HullBountyShare = 0.25;

		/// <summary>A state with this much on a clan sends an expedition after a found haven of its; hunters go after a clan
		/// with this much on it in all; each is considered with this daily chance.</summary>
		static constexpr double MinExpeditionPledge = 1e7;
		static constexpr double MinHuntBounty = 1e7;
		static constexpr double HuntChancePerDay = 0.05;

	private:
		static const cs::List<cs::String>& RunnerNames();

		static const cs::List<cs::String>& BreakawayNames();

		int runnersBuilt = 0;

	public:
		PirateClans(Galaxy* galaxy, InterstellarTraffic* traffic, int seed);

		/// <summary>Takes on <paramref name="system"/>'s raids and fights (the traffic registers every system, those opened later too).</summary>
		void Watch(cs::Ref<StarSystem> system);

		// ---------------------------------------------------------------- who and where

		/// <summary>Every pirate clan in the polities.</summary>
		cs::List<cs::Ref<Faction>> Clans();

		/// <summary>The standing havens of <paramref name="clan"/>, in every system.</summary>
		cs::List<HavenEntry> Havens(cs::Ref<Faction> clan);

	private:
		/// <summary>The systems those havens are in.</summary>
		static cs::HashSet<cs::Ref<StarSystem>> SystemsOf(const cs::List<HavenEntry>& havens);

	public:
		/// <summary><paramref name="clan"/>'s raider bands in <paramref name="system"/> (flagships, at home there: not on a voyage).</summary>
		cs::List<cs::Ref<Ship>> Bands(cs::Ref<Faction> clan, cs::Ref<StarSystem> system);

		/// <summary>The price on <paramref name="clan"/>'s head, credits: every robbed state's pledge.</summary>
		double Bounty(cs::Ref<Faction> clan);

		/// <summary>What <paramref name="state"/> has put on <paramref name="clan"/>'s head, credits.</summary>
		double Pledged(cs::Ref<Faction> state, cs::Ref<Faction> clan);

		/// <summary><paramref name="state"/> puts <paramref name="credits"/> more on <paramref name="clan"/>'s head.</summary>
		void Pledge(cs::Ref<Faction> state, cs::Ref<Faction> clan, double credits);

		// ---------------------------------------------------------------- the day

		/// <summary>A day of the clans' business (called by the traffic's daily round, with its voyages counted).</summary>
		void Daily(double jd);

	private:
		// ---------------------------------------------------------------- smuggling

		/// <summary>Sends a smuggler from each of <paramref name="clan"/>'s havens whose loot would fetch more at a black market abroad.</summary>
		void Smuggle(cs::Ref<Faction> clan, const cs::List<HavenEntry>& havens, double jd);

	public:
		/// <summary>
		/// What a run of up to <paramref name="capacity"/> tonnes of <paramref name="haven"/>'s loot to <paramref name="market"/>
		/// would clear over the haven's own fences' price (credits), the goods with the best margin first; fills
		/// <paramref name="load"/> (tonnes by good) when given.
		/// </summary>
		static double Plan(cs::Ref<Settlement> haven, cs::Ref<Settlement> market, double capacity, cs::List<double>* load);

		/// <summary>
		/// Loads a smuggler at <paramref name="haven"/> (the clan's idle one there, or a hull bought for it) with the loot that
		/// pays best at <paramref name="market"/> and sends it there through the gates. Returns the smuggler, or null if the
		/// clan has none and cannot buy or crew one.
		/// </summary>
		cs::Ref<Ship> SendRunner(cs::Ref<StarSystem> from, cs::Ref<Settlement> haven, cs::Ref<StarSystem> to, cs::Ref<Settlement> market, GateRoute route, double jd);

	private:
		cs::Ref<Ship> IdleRunner(cs::Ref<StarSystem> system, cs::Ref<Settlement> haven);

		/// <summary>Whether a smuggler of <paramref name="haven"/>'s is home in <paramref name="system"/> but not idle there: on
		/// its way in from the gate it came back through, or kept from port by a fight.</summary>
		static bool Returning(cs::Ref<StarSystem> system, cs::Ref<Settlement> haven);

		cs::Ref<Ship> BuildRunner(cs::Ref<StarSystem> system, cs::Ref<Settlement> haven, double jd);

	public:
		/// <summary>
		/// A smuggler abroad (the voyage's orders): to its black market; there, it sells everything aboard at the market's
		/// own prices, the clan is paid, and it turns for home.
		/// </summary>
		int SmugglerOrders(cs::Ref<StarSystem> system, cs::Ref<Ship> ship, int here, double jd);

		// ---------------------------------------------------------------- growing and moving

		/// <summary>
		/// <paramref name="clan"/> sends a band through the gates to found a haven in the nearest empty hide in a belt or disc
		/// of a system where it has none. Returns the band, or null if there is no hide in reach, no band free to go, a band
		/// of its is already on its way to found one (even one riding inside a gate right now), or its treasury has fallen
		/// under <see cref="FoundingCost"/> since it resolved to go.
		/// </summary>
		cs::Ref<Ship> Expand(cs::Ref<Faction> clan, double jd);

	private:
		/// <summary>Moves a band from the clan's best-manned haven to one with none. Returns the band, or null.</summary>
		cs::Ref<Ship> MoveBand(cs::Ref<Faction> clan, const cs::List<HavenEntry>& havens, double jd);

		/// <summary>A band of <paramref name="clan"/>'s in <paramref name="system"/> fit to sail: idle in port, whole and armed.</summary>
		cs::Ref<Ship> ReadyBand(cs::Ref<Faction> clan, cs::Ref<StarSystem> system);

	public:
		/// <summary>
		/// A band on a move comes out at its far end (the voyage's arrival): it founds the clan's haven at the hide it was
		/// sent to, if no haven of the clan stands there yet, and joins that system's raiders for good, based at the haven.
		/// If the hide has been taken meanwhile, it raids a while and goes home.
		/// </summary>
		void Settle(cs::Ref<StarSystem> system, cs::Ref<Ship> band, double jd);

		/// <summary>
		/// <paramref name="haven"/> breaks away from its clan: a new clan, its own polity, takes it over with the bands
		/// based there and a share of the old clan's treasury and hands. Returns the new clan.
		/// </summary>
		cs::Ref<Faction> Split(cs::Ref<Settlement> haven, double jd);

	private:
		// ---------------------------------------------------------------- the hunt

		void OnPlundered(cs::Ref<World> world, cs::Ref<Faction> clan, double value);

		/// <summary>Whoever won a fight in which a clan lost hulls (destroyed or taken) collects the price on them, from the states that posted it.</summary>
		void OnBattleEnded(cs::Ref<World> world, cs::Ref<Battle> b, double jd);

		/// <summary>Draws up to <paramref name="amount"/> from the price on <paramref name="clan"/>, each pledging state paying its share.</summary>
		double Collect(cs::Ref<Faction> clan, double amount);

	public:
		/// <summary>The hunt for <paramref name="clan"/> today (see <see cref="Hunt"/>), whatever the day's dice say.</summary>
		void Pursue(cs::Ref<Faction> clan, double jd);

	private:
		/// <summary>
		/// The states with a price on <paramref name="clan"/> send navy expeditions through the gates against its havens
		/// that have been found, and bounty hunters go after its bands where they work.
		/// </summary>
		void Hunt(cs::Ref<Faction> clan, const cs::List<HavenEntry>& havens, double jd);
	};
}
