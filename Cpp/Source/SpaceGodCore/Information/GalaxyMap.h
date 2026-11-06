#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Information/GalaxyMap.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Information/News.h"

namespace SpaceGod::Core
{
	class Faction;
	class FactionStanding;
	class Galaxy;
	class TerritoryChange;
	class GateCrossing;
	class GateLink;
	class JumpGate;
	class StarSystem;

	/// <summary>How thick the raiders are about a system, as far as the news has it.</summary>
	enum class PirateActivity : int
	{
		None,

		/// <summary>A few raider hulls about, or a haven known, or a raid or two through the gates.</summary>
		Low,

		/// <summary>Raiders in numbers, havens, raids coming through: a system to cross with an escort.</summary>
		High,
	};
	SPACEGODCORE_API const char* ToString(PirateActivity v);

	/// <summary>One polity's share of a system on the map: the standing markets it holds there and their people.</summary>
	class MapHolding final
	{
	public:
		cs::Ref<SpaceGod::Core::Faction> Faction;
		double Population = 0;
		int Ports = 0;
	};

	/// <summary>
	/// One star system as the galaxy map shows it (U.SPC-T103): only what the player knows of it, from the newest report
	/// the system they are in holds (<see cref="Newswire.Known"/>), and as old as that report; the system they are in is
	/// seen as it is.
	/// </summary>
	class SPACEGODCORE_API MapSystem final
	{
	public:
		cs::Ref<StarSystem> System;

		/// <summary>The system the player is in: seen as it is now.</summary>
		bool Here = false;

		/// <summary>What the map knows it by: a live look for <see cref="Here"/>, else the newest report held of it; null when no
		/// news of it has ever come (an unsettled system: its star and its gates are on the charts, nothing more).</summary>
		cs::Ref<SystemReport> Report;

		bool Known() const { return Report != nullptr; }

		/// <summary>How old what is known of it is, days (0 here, NaN when nothing is).</summary>
		double AgeDays = cs::Double::NaN;

		/// <summary>Who held it, as last heard; null if nobody (or nothing is known).</summary>
		cs::Ref<Faction> Holder;

		/// <summary>Every polity with standing markets there, the biggest first: the territory the map colours it by.</summary>
		cs::List<cs::Ref<MapHolding>> Holdings;

		/// <summary>Markets on the last report.</summary>
		int Markets = 0;

		/// <summary>Armed raider hulls there and havens known there, as last heard, and raids that came in through its gates in the
		/// <see cref="GalaxyMap.FlowWindowDays"/> before that.</summary>
		int PirateShips = 0, Havens = 0, Raids = 0;

		PirateActivity Pirates = PirateActivity::None;

		/// <summary>Its ports under a declared blockade or a siege, as last heard.</summary>
		cs::List<PortThreat> Threats;

		/// <summary>Two polities at war both hold markets there: the front runs through the system itself.</summary>
		bool Contested = false;

		/// <summary>Who is fighting over it, when <see cref="Contested"/>.</summary>
		FactionPair ContestedBy;

		/// <summary>
		/// The last time it changed hands as far as the news goes (US-T190, <see cref="Territory.Changes"/>): who held it before,
		/// when and how. Only a change at or before its report's date is known; NaN, null and empty when none is.
		/// </summary>
		cs::Ref<Faction> PreviousHolder;
		double HolderChangedJd = cs::Double::NaN;
		cs::String HowChanged;

		/// <summary>Whether it is known to have changed hands within <paramref name="days"/> of <paramref name="jd"/>.</summary>
		bool ChangedWithin(double jd, double days) const { return !cs::Double::IsNaN(HolderChangedJd) && jd - HolderChangedJd <= days; }

		cs::String ToString() const;
	};

	/// <summary>
	/// One gate link as the galaxy map shows it (U.SPC-T103): the traffic logged through it in the
	/// <see cref="GalaxyMap.FlowWindowDays"/> up to the freshest news of either end, whether either end is shut, who holds
	/// each, and whether it is a war front. A link still being built has no <see cref="Link"/> yet.
	/// </summary>
	class SPACEGODCORE_API MapLink final
	{
	public:
		/// <summary>The working link; null for a gate still under construction.</summary>
		cs::Ref<GateLink> Link;

		cs::Ref<StarSystem> A, B;

		/// <summary>Up to when the traffic through it is known: the newer of its two ends' news (NaN when neither is known).</summary>
		double KnownAsOfJd = cs::Double::NaN;

		bool Known() const { return !cs::Double::IsNaN(KnownAsOfJd); }

		/// <summary>Crossings (a group counts once) in the window, either way, and by what for.</summary>
		int Crossings = 0, Trade = 0, Couriers = 0, Raids = 0, Navy = 0, Surveys = 0;

		/// <summary>Crossings from <see cref="A"/> to <see cref="B"/>, and back.</summary>
		int TowardB = 0, TowardA = 0;

		/// <summary>Cargo carried through it in the window, tonnes.</summary>
		double Tonnes = 0;

		/// <summary>The traffic as a share of the busiest link's on the map (0..1).</summary>
		double Flow = 0;

		/// <summary>Each end shut (under blockade, or its ring out of action), as last heard of that end.</summary>
		bool ShutAtA = false, ShutAtB = false;

		/// <summary>Each end's ring damaged out of action in a war (U.SPC-T97), as last heard of that end: shut to everyone until rebuilt.</summary>
		bool DownAtA = false, DownAtB = false;

		bool Down() const { return DownAtA || DownAtB; }

		bool Shut() const { return ShutAtA || ShutAtB; }

		/// <summary>Who holds each end's gate, as last heard (null where unknown or nobody).</summary>
		cs::Ref<Faction> OwnerA, OwnerB;

		/// <summary>The systems at its ends are held by polities at war with each other.</summary>
		bool WarFront = false;

		/// <summary>The two sides, when <see cref="WarFront"/>.</summary>
		FactionPair Front;

		/// <summary>A gate still being built (see <see cref="GateWork"/>), how far along, and by whom.</summary>
		bool UnderConstruction = false;
		double Progress = 0;
		cs::Ref<Faction> Builder;

		cs::Ref<StarSystem> Other(const cs::Ref<StarSystem>& s) const { return s == A ? B : s == B ? A : nullptr; }

		cs::String ToString() const;
	};

	/// <summary>A gate link being built between two systems, for the map to show (fed from whatever builds them).</summary>
	class GateWork final
	{
	public:
		cs::Ref<StarSystem> A, B;

		/// <summary>How far along, 0..1.</summary>
		double Progress = 0;

		cs::Ref<Faction> Builder;
	};

	/// <summary>The galaxy map's whole picture at one moment, from one system's point of view (see <see cref="GalaxyMap.Build"/>).</summary>
	class SPACEGODCORE_API GalaxyMapModel final
	{
	public:
		cs::Ref<StarSystem> Here;
		double Jd = 0;
		cs::List<cs::Ref<MapSystem>> Systems;
		cs::List<cs::Ref<MapLink>> Links;

		/// <summary>The wars the player knows of: every pair of polities at war, as the newest news has it.</summary>
		cs::List<FactionPair> Wars;

		/// <summary>The most crossings any link on the map has in the window (what <see cref="MapLink.Flow"/> is a share of).</summary>
		int BusiestCrossings = 0;

		/// <summary>The systems known to have changed hands in the last <see cref="Territory.RecentDays"/>, newest first (US-T190).</summary>
		cs::List<cs::Ref<TerritoryChange>> Lately;

		/// <summary>Every polity's strength and its trend at the last daily review (US-T190); empty before the first.</summary>
		cs::List<cs::Ref<FactionStanding>> Standings;

		/// <summary>The standing of <paramref name="f"/>, or null.</summary>
		cs::Ref<FactionStanding> StandingOf(const cs::Ref<Faction>& f) const;

		cs::Ref<MapSystem> Of(const cs::Ref<StarSystem>& s) const;

		cs::Ref<MapLink> Between(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b) const;

		/// <summary>Whether <paramref name="a"/> and <paramref name="b"/> are at war, as far as the player knows.</summary>
		bool AtWar(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const;

		/// <summary>The links that are war fronts.</summary>
		cs::List<cs::Ref<MapLink>> Fronts() const;
	};

	/// <summary>
	/// The interstellar map's picture (U.SPC-T103), engine-free: every simulated system with the gate network between
	/// them, each system coloured by the polity that holds it, the traffic through each gate, the war fronts and
	/// blockades, the raiders, and any gates being built -- limited to what the player knows. The system the player is in
	/// is seen as it is; every other only by the newest report that system holds of it (<see cref="Newswire.Known"/>),
	/// so a siege far off shows only once word of it has come through the gates, and a system never heard from shows
	/// only as a star on the charts. A link's traffic is what its gates' owners logged
	/// (<see cref="InterstellarTraffic.CrossingLog"/>) up to the freshest news of either end.
	/// </summary>
	class SPACEGODCORE_API GalaxyMap
	{
	public:
		/// <summary>The window traffic and raids are counted over, days.</summary>
		static constexpr double FlowWindowDays = 60;

		/// <summary>
		/// The map as the player in <paramref name="here"/> knows it at <paramref name="jd"/>. <paramref name="works"/> are gates
		/// under construction to show (none if null).
		/// </summary>
		// (C#'s `IEnumerable<GateWork> works = null`: null is the empty list here, which shows none either.)
		static cs::Ref<GalaxyMapModel> Build(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, double jd,
			const cs::List<cs::Ref<GateWork>>& works = cs::List<cs::Ref<GateWork>>());

	private:
		// `log` is null in C# when the galaxy has no traffic: a null pointer here.
		static cs::Ref<MapLink> LinkOf(const cs::Ref<GalaxyMapModel>& model, const cs::Ref<GateLink>& link, const cs::List<cs::Ref<GateCrossing>>* log);

		static void Gate(const cs::Ref<SystemReport>& r, const cs::Ref<JumpGate>& gate, double jd, bool& shut, bool& down, cs::Ref<Faction>& owner);
	};
}
