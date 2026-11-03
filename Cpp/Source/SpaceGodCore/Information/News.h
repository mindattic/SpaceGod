#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Information/News.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "World.h"

#include <tuple>

namespace SpaceGod::Core
{
	class Faction;
	class Galaxy;
	class JumpGate;
	class Settlement;
	class StarSystem;

	/// <summary>How a report from another system got here.</summary>
	enum class NewsCarrier : int
	{
		/// <summary>Beamed to the gate, passed through it by the gates' own relay, and beamed in from the far gate.</summary>
		Relay,

		/// <summary>Carried by a ship through the gates (a courier with the mail, or any ship with its data cores).</summary>
		Ship,

		/// <summary>What a system knew of the others when the simulation began.</summary>
		Charts,

		/// <summary>Beamed relay to relay through open space by a power's deep-space relays (U.SPC-T421), where the gates
		/// cannot pass it.</summary>
		DeepSpaceRelay,
	};
	SPACEGODCORE_API const char* ToString(NewsCarrier v);

	/// <summary>One gate as a report saw it: who held it and until when it was shut (U.SPC-T103).</summary>
	struct SPACEGODCORE_API GateState
	{
		cs::Ref<JumpGate> Gate;
		cs::Ref<Faction> Owner;
		double BlockadeUntilJd = 0;

		/// <summary>Shut at <paramref name="jd"/>, as far as the report knew (a blockade lasts until its date unless lifted).</summary>
		bool Shut(double jd) const { return jd < BlockadeUntilJd; }

		/// <summary>How much of the ring had been knocked out of it (U.SPC-T97): 0 whole, 1 a wreck (see <see cref="JumpGate.Integrity"/>).</summary>
		double Damage = 0;

		/// <summary>The ring was out of action: too damaged to hold a crossing open until it is rebuilt.</summary>
		bool Down() const;
	};

	/// <summary>A port a report saw under a declared blockade or a siege (U.SPC-T103).</summary>
	struct PortThreat
	{
		cs::String Port;
		cs::Ref<Faction> Owner, BlockadedBy, BesiegedBy;
	};

	/// <summary>Two polities, as a pair (at war, say), either way round.</summary>
	struct FactionPair
	{
		cs::Ref<Faction> A, B;

		FactionPair() = default;
		FactionPair(cs::Ref<Faction> a, cs::Ref<Faction> b)
		{
			A = a;
			B = b;
		}

		bool Involves(const cs::Ref<Faction>& f) const { return f != nullptr && (A == f || B == f); }

		bool Same(const FactionPair& o) const { return (A == o.A && B == o.B) || (A == o.B && B == o.A); }
	};

	/// <summary>
	/// What one system's markets and news looked like when a report left it (U.SPC-T100): every market's board,
	/// copied as it stood (<see cref="Settlement.Snapshot"/>), and the notable things that had happened there since
	/// the last report. Nothing in it changes after it is taken, so by the time it reaches anyone it is as old as the
	/// trip that brought it, and the real markets may have moved a long way.
	/// </summary>
	class SPACEGODCORE_API SystemReport final
	{
	public:
		cs::Ref<StarSystem> Source;

		/// <summary>When the report left the source's markets (the moment its boards were copied).</summary>
		double AsOfJd = 0;

		/// <summary>When it reached the system it is held in.</summary>
		double ReceivedJd = 0;

		NewsCarrier Carrier = NewsCarrier::Relay;

		/// <summary>The ship that brought it, for a <see cref="NewsCarrier.Ship"/> report.</summary>
		/// (A null C# string is the empty string here.)
		cs::String CarriedBy;

		/// <summary>Every market's board in the source system as it stood at <see cref="AsOfJd"/> (no hidden havens).</summary>
		cs::List<cs::Ref<Settlement>> Markets;

		/// <summary>The source's own notable news up to <see cref="AsOfJd"/>, oldest first.</summary>
		cs::List<WorldEvent> Headlines;

		/// <summary>Who held the source at <see cref="AsOfJd"/> (<see cref="Polities.HolderOf"/>); null if nobody did.</summary>
		cs::Ref<Faction> Holder;

		/// <summary>The source's own gates as they stood: who held each and how long it was shut for (U.SPC-T103).</summary>
		cs::List<GateState> Gates;

		/// <summary>Its ports a besieger had declared blockaded, and those under siege, as they stood (U.SPC-T103).</summary>
		cs::List<PortThreat> PortThreats;

		/// <summary>Every pair of polities at war at <see cref="AsOfJd"/>, by the one table every system shares (pirates aside).</summary>
		cs::List<FactionPair> Wars;

		/// <summary>Armed raider hulls about the source, and the pirate havens known there (U.SPC-T103).</summary>
		int PirateShips = 0, Havens = 0;

		/// <summary>Whether the report has <paramref name="a"/> and <paramref name="b"/> at war.</summary>
		bool AtWar(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const;

		/// <summary>How old the news is at <paramref name="jd"/>, days.</summary>
		double AgeDays(double jd) const { return jd - AsOfJd; }

		/// <summary>The board of the market named <paramref name="name"/>, as the report has it; null if it has none.</summary>
		cs::Ref<Settlement> Market(const cs::String& name) const;

		/// <summary>The same report, as delivered somewhere else at another time by another carrier.</summary>
		cs::Ref<SystemReport> Delivered(double receivedJd, NewsCarrier carrier, const cs::String& carriedBy) const; // internal

		/// <summary>The news kinds worth sending beyond the gates: captures, diplomacy and economic shocks.</summary>
		static bool Notable(WorldEventKind k) { return k == WorldEventKind::Capture || k == WorldEventKind::Diplomacy || k == WorldEventKind::Economy; }

		/// <summary>The prefix news from abroad is logged under, so it is never sent on again as the receiver's own.</summary>
		static inline const cs::String ForeignPrefix = "News from ";

		/// <summary>Copies <paramref name="s"/>'s boards and its notable news now.</summary>
		static cs::Ref<SystemReport> Take(const cs::Ref<StarSystem>& s, double jd);

	private:
		/// <summary>
		/// The rest of what a report carries for the galaxy map (U.SPC-T103): who holds the system and its gates, which
		/// gates are shut, its ports under blockade or siege, the wars of the day, and how thick the raiders are there.
		/// </summary>
		static void TakeSituation(const cs::Ref<SystemReport>& r, const cs::Ref<StarSystem>& s, double jd);
	};

	/// <summary>
	/// The news between the star systems (U.SPC-T100). Nothing is known of another system but what has been brought
	/// from it, and nothing crosses faster than the gates: every <see cref="RelayIntervalDays"/> each settled system's
	/// boards and news are beamed out to its gates, passed through by the gates' relays (as long as a crossing takes a
	/// ship, see <see cref="GateNetwork.TransitDays"/>) and beamed in from the far gate (<see cref="RelayDays"/>); a
	/// gate under blockade passes nothing, so the news goes round by other gates or not at all. Every ship that crosses
	/// also carries what its system knew when it went in (<see cref="Bundle"/>), which can be fresher than the relay's.
	/// A system keeps only its newest report from each other (<see cref="Known"/>), and that is what traders there plan
	/// their runs on: stale by the trip, and wrong if the market moved.
	/// </summary>
	class SPACEGODCORE_API Newswire final
	{
		cs::Ref<Galaxy> galaxy;
		cs::Dictionary<cs::Ref<StarSystem>, cs::Dictionary<cs::Ref<StarSystem>, cs::Ref<SystemReport>>> known;
		// (report, to, arriveJd)
		cs::List<std::tuple<cs::Ref<SystemReport>, cs::Ref<StarSystem>, double>> inFlight;
		double nextRelayJd = 0;

	public:
		/// <summary>How often each settled system's relay sends its boards and news out, days.</summary>
		double RelayIntervalDays = 1;

		/// <summary>The most headlines from one report logged at the receiving end.</summary>
		static constexpr int HeadlinesPerReport = 4;

		/// <summary>Reports delivered so far.</summary>
		int Delivered = 0;

		/// <summary>Raised when a report reaches a system: (where it arrived, the report).</summary>
		cs::Event<cs::Ref<StarSystem>, cs::Ref<SystemReport>> Received;

	private:
		/// <summary>The relay's own traffic through the gates: nobody's enemy, but shut out by a blockade like any outsider.</summary>
		static const cs::Ref<Faction>& RelayTraffic();

	public:
		/// <summary>When the relays next send.</summary>
		double NextRelayJd() const { return nextRelayJd; }
		void SetNextRelayJd(double value) { nextRelayJd = value; }

		/// <summary>Reports on their way right now.</summary>
		int InFlight() const { return inFlight.Count(); }

		Newswire(cs::Ref<Galaxy> galaxy, double jd);

	private:
		cs::Dictionary<cs::Ref<StarSystem>, cs::Ref<SystemReport>>& Book(const cs::Ref<StarSystem>& at);

	public:
		/// <summary>The newest report <paramref name="at"/> holds of <paramref name="about"/>; null if none has ever come.
		/// A system's own markets are not news: null for the same system.</summary>
		cs::Ref<SystemReport> Known(const cs::Ref<StarSystem>& at, const cs::Ref<StarSystem>& about) const;

		/// <summary>Every report <paramref name="at"/> holds.</summary>
		cs::List<cs::Ref<SystemReport>> KnownAt(const cs::Ref<StarSystem>& at) const;

		/// <summary>
		/// How long light takes between a system's gates and its nearest market, days: the relay beams the news in to that
		/// port (and out from it), and the system's own traffic carries it on from there. Minutes in Sol (Mercury's Caloris
		/// Works), over an hour at Alpha Centauri (Rigil Gate, 9.5 AU out). Zero for a system with no markets.
		/// </summary>
		static double InnerLightDays(const cs::Ref<StarSystem>& s);

		/// <summary>
		/// How long the relay takes from <paramref name="from"/>'s markets to <paramref name="to"/>'s, days: light out to
		/// the gate, the quickest open way through the gates (<see cref="GateNetwork.RouteDays"/>, blockaded gates left
		/// out), and light in from the far gate; or, where that is slower or shut, by a deep-space relay on the route between
		/// the two (U.SPC-T421, <see cref="DeepSpace.RelayLagDays"/>). Infinity when no way is open.
		/// </summary>
		double RelayDays(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd) const;

	private:
		double RelayDays(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd, bool& deepSpace) const;

	public:
		/// <summary>Sends each settled system's relay report when one is due, and delivers the reports that have arrived.</summary>
		void Update(double jd);

		/// <summary>Every settled system's boards and news, beamed out now to every system the gates can reach.</summary>
		void Relay(double jd);

	private:
		void Send(const cs::Ref<SystemReport>& report, const cs::Ref<StarSystem>& to, double arriveJd, NewsCarrier carrier, const cs::String& by);

	public:
		/// <summary>
		/// What a ship going into a gate from <paramref name="from"/> carries: that system's own boards and news now, and
		/// every report it held of the others.
		/// </summary>
		cs::List<cs::Ref<SystemReport>> Bundle(const cs::Ref<StarSystem>& from, double jd);

		/// <summary>A ship has come out of a gate into <paramref name="to"/> with <paramref name="bundle"/>: it is beamed
		/// in from the gate, arriving at the markets after the light time.</summary>
		// (C#'s `if (bundle == null) return;`: a null List is an empty one here, which sends nothing either.)
		void Carried(const cs::Ref<StarSystem>& to, const cs::List<cs::Ref<SystemReport>>& bundle, const cs::String& ship, double jd);

		/// <summary>
		/// A report reaches <paramref name="to"/>: kept if it is newer than what was known, and its headlines newer than the
		/// last report's are logged there as news from abroad, dated as they arrive and saying how old they are.
		/// </summary>
		void Deliver(const cs::Ref<StarSystem>& to, const cs::Ref<SystemReport>& r, double jd);

	private:
		static cs::String CarrierText(const cs::Ref<SystemReport>& r);

	public:
		/// <summary>A line for the HUD: how old the news from <paramref name="about"/> is here, and how it came.</summary>
		cs::String Describe(const cs::Ref<StarSystem>& at, const cs::Ref<StarSystem>& about, double jd) const;
	};
}
