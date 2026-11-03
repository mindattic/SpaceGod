#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/MarketShocks.cs (US-T19).

#include "Cs/Cs.h"
#include "Economy/Goods.h" // GoodInfo::Count (array lengths)

#include <tuple>
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Settlement;
	class Faction;
	class Facility;
	class Embargo;
	class Ship;
	class Economy;
	class Diplomacy;

	enum class ShockKind : int
	{
		/// <summary>A mine shut down by an accident; likelier the harder it is worked and the shorter of machinery.</summary>
		MineAccident,

		/// <summary>A seam worked out: the mine's capacity falls for good (or prospectors find a new one).</summary>
		Depletion,

		/// <summary>A blight in the hydroponic farms; likelier when they are short of nitrogen.</summary>
		Blight,

		/// <summary>Workers down tools because wages have fallen behind the cost of living.</summary>
		Strike,

		/// <summary>A navy at war short of essentials or munitions puts its people on rations.</summary>
		Rationing,

		/// <summary>A besieger holds the space over a port and declares it blockaded through <see cref="Diplomacy"/>.</summary>
		Blockade,

		/// <summary>A navy closes its ports to a neutral's ships for supplying its enemy, through <see cref="Diplomacy"/>.</summary>
		Embargo,
	};
	const char* ToString(ShockKind v);

	/// <summary>One economic event in force (or just over), with what it did and where.</summary>
	class EconomicShock final
	{
	public:
		ShockKind Kind = ShockKind::MineAccident;

		/// <summary>The port it hit, or null for one that covers a faction (rationing, an embargo).</summary>
		cs::Ref<::SpaceGod::Core::Settlement> Settlement;

		/// <summary>The faction that declared it (rationing, a blockade, an embargo), or whose port was hit.</summary>
		cs::Ref<::SpaceGod::Core::Faction> Faction;

		/// <summary>The facility it shut, for an accident or a blight.</summary>
		cs::Ref<::SpaceGod::Core::Facility> Facility;

		/// <summary>The good it bears on most directly, if one.</summary>
		cs::Nullable<::SpaceGod::Core::Good> Good;

		double StartJd = 0, EndJd = 0;
		cs::String Text;

		/// <summary>For a strike: how far the cost of living had run ahead of wages when it began (1.4 = 40%).</summary>
		double Severity = 0;

		/// <summary>A short name for the market board ("Strike", "Blockade by the Outer Reach Compact").</summary>
		cs::String Label;

		cs::Ref<::SpaceGod::Core::Embargo> Embargo;
	};

	/// <summary>
	/// Economic events driven by what the simulation is actually doing, never by a script: mines that are
	/// worked hard have accidents and work their seams out, hydroponic farms short of nitrogen catch a
	/// blight, workers strike when wages fall behind prices, a navy at war that runs short puts its
	/// people on rations, a besieger holding a port declares it blockaded and a navy closes its ports to
	/// a neutral caught running munitions to its enemy (both through <see cref="Diplomacy"/>). Each moves
	/// stock and prices through the ordinary markets, so traders (and the player) react. The news (the
	/// world's event log) reports each one, and every price spike, as it happens.
	/// </summary>
	class MarketShocks final
	{
		cs::Ref<World> world;
		cs::Random rng;
		double nextDailyJd = cs::Double::NaN, firstJd = 0;

	public:
		/// <summary>Shocks in force now, oldest first.</summary>
		cs::List<cs::Ref<EconomicShock>> Active = cs::List<cs::Ref<EconomicShock>>();

		/// <summary>Raised when a shock starts (an instantaneous one, like a worked-out seam, too).</summary>
		cs::Event<cs::Ref<EconomicShock>> Started;

		// ---------------------------------------------------------------- tuning

		/// <summary>Days for wages to close most of a gap to the cost of living (they lag prices).</summary>
		static constexpr double WageLagDays = 120;

		/// <summary>Cost of living over what wages buy at which strikes become possible.</summary>
		static constexpr double StrikeThreshold = 1.3;

		/// <summary>A local price this many times base is a spike worth reporting, and one back under the lower figure is over.</summary>
		static constexpr double SpikeRatio = 2.5, SpikeClear = 1.6;

		/// <summary>The same for a good's average across the system.</summary>
		static constexpr double SystemSpikeRatio = 1.8, SystemSpikeClear = 1.3;

		/// <summary>Daily chance of an accident in a mine at its base level, fully worked; and of a blight in a farm.</summary>
		double AccidentPerDay = 0.0006, BlightPerDay = 0.00025;

		/// <summary>Tonne-equivalents of war goods (munitions; electronics and alloys count part) a neutral can sell a
		/// navy's enemy, decaying over about <see cref="ContrabandMemoryDays"/>, before that navy embargoes it.</summary>
		double EmbargoThreshold = 4000;

		static constexpr double ContrabandMemoryDays = 45;

		/// <summary>Who holds the space over a port with a besieging group (by default the war's own siege bookkeeping,
		/// <see cref="War.Besieger"/>); such a besieger declares the port blockaded.</summary>
		cs::Func<cs::Ref<::SpaceGod::Core::Faction>, cs::Ref<Settlement>> BesiegerOf;

		// ---------------------------------------------------------------- state

	private:
		cs::Dictionary<cs::Ref<Facility>, double> surveyedLevel = cs::Dictionary<cs::Ref<Facility>, double>();
		cs::Dictionary<cs::Ref<Settlement>, double> lastStrikeEndJd = cs::Dictionary<cs::Ref<Settlement>, double>();
		cs::HashSet<std::tuple<cs::Ref<Settlement>, int>> spiking = cs::HashSet<std::tuple<cs::Ref<Settlement>, int>>();
		cs::Dictionary<std::tuple<cs::Ref<Settlement>, int>, double> spikeClearedJd = cs::Dictionary<std::tuple<cs::Ref<Settlement>, int>, double>();
		cs::List<bool> systemSpiking = cs::NewArray<bool>(GoodInfo::Count);
		cs::List<double> systemIndex = cs::NewArray<double>(GoodInfo::Count);
		cs::Dictionary<std::tuple<cs::Ref<Settlement>, int>, double> smoothed = cs::Dictionary<std::tuple<cs::Ref<Settlement>, int>, double>();
		cs::Dictionary<cs::Ref<Settlement>, int> besiegerAbsentDays = cs::Dictionary<cs::Ref<Settlement>, int>();
		// (Faction by, Faction against)
		cs::Dictionary<std::tuple<cs::Ref<::SpaceGod::Core::Faction>, cs::Ref<::SpaceGod::Core::Faction>>, double> contraband;
		cs::Dictionary<std::tuple<cs::Ref<::SpaceGod::Core::Faction>, cs::Ref<::SpaceGod::Core::Faction>>, cs::Ref<::SpaceGod::Core::Faction>> contrabandFor;

	public:
		MarketShocks(cs::Ref<World> world, int seed);

	private:
		cs::Ref<::SpaceGod::Core::Economy> Economy() const;
		cs::Ref<::SpaceGod::Core::Diplomacy> Diplomacy() const;

	public:
		/// <summary>The shocks bearing on <paramref name="s"/> now: its own, and its owner's rationing and embargoes.</summary>
		cs::List<cs::Ref<EconomicShock>> At(const cs::Ref<Settlement>& s) const;

		/// <summary>Runs a day at a time up to <paramref name="jd"/>.</summary>
		void Update(double jd);

	private:
		void Daily(double jd);

		// ---------------------------------------------------------------- mines

		static bool IsOre(const cs::Ref<::SpaceGod::Core::Facility>& f);

		void Mine(const cs::Ref<Settlement>& s, const cs::Ref<::SpaceGod::Core::Facility>& f, double jd);

		void Deplete(const cs::Ref<Settlement>& s, const cs::Ref<::SpaceGod::Core::Facility>& f, ::SpaceGod::Core::Good ore, double jd);

		// ---------------------------------------------------------------- farms

		void Farm(const cs::Ref<Settlement>& s, const cs::Ref<::SpaceGod::Core::Facility>& f, double jd);

		// ---------------------------------------------------------------- wages and strikes

		void Wages(const cs::Ref<Settlement>& s, double jd);

		/// <summary>The everyday need dearest against its base price at <paramref name="s"/>.</summary>
		static ::SpaceGod::Core::Good DearestNeed(const cs::Ref<Settlement>& s);

		// ---------------------------------------------------------------- war rationing

		void Rationing(double jd);

		bool AtWar(const cs::Ref<::SpaceGod::Core::Faction>& f) const;

		// ---------------------------------------------------------------- blockades

		void Blockades(double jd);

		// ---------------------------------------------------------------- embargoes

		static double WarWeight(::SpaceGod::Core::Good g);

		void OnSold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, ::SpaceGod::Core::Good good, double tonnes);

	public:
		/// <summary>Tonne-equivalents of war goods <paramref name="against"/>'s traders have lately sold <paramref name="by"/>'s enemies.</summary>
		double Contraband(const cs::Ref<::SpaceGod::Core::Faction>& by, const cs::Ref<::SpaceGod::Core::Faction>& against) const;

	private:
		void Embargoes(double jd);

		/// <summary>A faction's merchant fleet as the news names it: "the Belt Consortium", "independent merchants".</summary>
		static cs::String MerchantsOf(const cs::Ref<::SpaceGod::Core::Faction>& f);

		// ---------------------------------------------------------------- price spikes

	public:
		/// <summary>Days over which a price is smoothed before it counts as a spike (a navy refitting for an afternoon is not news).</summary>
		static constexpr double SpikeSmoothingDays = 3;

	private:
		void PriceSpikes(double jd);

	public:
		/// <summary>
		/// A good's price against base across the system, weighted by how much each port uses (people and
		/// industry; a navy passing through does not move it), so a handful of thin markets do not swing it.
		/// </summary>
		double SystemIndex(::SpaceGod::Core::Good g) const;

	private:
		/// <summary>What lies behind a price at <paramref name="s"/>, if a shock does: a blockade, a strike, a blight.</summary>
		// (Empty for C#'s null.)
		cs::String CauseAt(const cs::Ref<Settlement>& s, ::SpaceGod::Core::Good g) const;

		// ---------------------------------------------------------------- bookkeeping

		cs::Ref<EconomicShock> Find(ShockKind kind, const cs::Ref<::SpaceGod::Core::Faction>& f) const;

		cs::Ref<EconomicShock> Find(ShockKind kind, const cs::Ref<Settlement>& s) const;

		void Start(const cs::Ref<EconomicShock>& e);

		void Announce(const cs::Ref<EconomicShock>& e);

		/// <summary>Shocks that run their course (an accident cleared, a blight over, a strike settled) end on their own.</summary>
		void EndExpired(double jd);

		void End(const cs::Ref<EconomicShock>& e, double jd, const cs::String& text);

		// ---------------------------------------------------------------- text

		cs::String Where(const cs::Ref<Settlement>& s) const;
		static cs::String Name(::SpaceGod::Core::Good g);
		static cs::String Ratio(const cs::Ref<Settlement>& s, ::SpaceGod::Core::Good g);
		static cs::String Percent(double f);
		static cs::String Tonnes(double t);
		static cs::String Days(double d);
	private:
		MarketShocks() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(MarketShocks); // ObjectGraph registration (Persistence/Registrations)
	};
}
