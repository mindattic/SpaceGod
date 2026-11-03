#pragma once
// Port of Assets/UnitySpace/Core/Systems/GateNetwork.cs (US-T17).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class StarSystem;
	class JumpGate;
	class Faction;
	class Polities;
	class GateNetwork;

	/// <summary>One jump-gate link: a gate in each of two systems, paired, and the light-years between them.</summary>
	class SPACEGODCORE_API GateLink final
	{
	public:
		cs::Ref<StarSystem> A;
		cs::Ref<StarSystem> B;
		cs::Ref<JumpGate> AtA;
		cs::Ref<JumpGate> AtB;
		double Ly = 0;

		/// <summary>How long a ship spends in the gate crossing this link (days); see <see cref="GateNetwork.TransitDays"/>.</summary>
		double TransitDays() const;

		// internal
		GateLink(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b, const cs::Ref<JumpGate>& atA, const cs::Ref<JumpGate>& atB);

		cs::Ref<StarSystem> Other(const cs::Ref<StarSystem>& s) const { return s == A ? B : s == B ? A : nullptr; }

		/// <summary>This link's gate in <paramref name="s"/>, or null if it does not touch it.</summary>
		cs::Ref<JumpGate> GateIn(const cs::Ref<StarSystem>& s) const { return s == A ? AtA : s == B ? AtB : nullptr; }

		/// <summary>Both rings whole enough to hold a crossing open (see <see cref="JumpGate.Working"/>).</summary>
		bool Working() const;

		/// <summary>Open for <paramref name="faction"/> leaving <paramref name="from"/>: both rings working, and both ends let it through.</summary>
		bool Open(const cs::Ref<StarSystem>& from, const cs::Ref<Faction>& faction, double jd) const;

		/// <summary>
		/// Open for <paramref name="faction"/>'s war fleet leaving <paramref name="from"/> (U.SPC-T92): each end either lets
		/// it through or is held by a power it is at war with, and so may be forced (see <see cref="JumpGate.HostileTo"/>).
		/// </summary>
		bool Forcible(const cs::Ref<StarSystem>& from, const cs::Ref<Faction>& faction, double jd) const;

		cs::String ToString() const;
	private:
		GateLink() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(GateLink); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// The jump gates between the simulated systems: a sparse lattice, not all-pairs. Each system is
	/// linked to its <see cref="Neighbours"/> nearest systems within <see cref="MaxLinkLy"/> (and they to
	/// it), and any islands that leaves are joined by their shortest links (a minimum spanning tree over
	/// the distances), so every system can reach every other, often only by way of others. A bridge
	/// longer than <see cref="MaxLinkLy"/> is never built direct: one or more Void Bases
	/// (<see cref="StarSystem.CreateVoidBase"/>) are laid along the way instead, purely to relay it
	/// (U.SPC-T372). Each link is a pair of <see cref="JumpGate"/>s, one at each end, set at that
	/// system's edge facing the other star. Routes are planned over the graph by crossing time
	/// (<see cref="Route"/>), skipping gates that will not open for the traveller (an enemy's, or one
	/// under blockade).
	/// </summary>
	// A route is a C# List that may be null (no way through): it is a cs::Ref<cs::List<cs::Ref<JumpGate>>> here.
	class SPACEGODCORE_API GateNetwork final
	{
		cs::List<cs::Ref<GateLink>> links;
		cs::List<cs::Ref<StarSystem>> systems;

	public:
		using RouteList = cs::Ref<cs::List<cs::Ref<JumpGate>>>;

		const cs::List<cs::Ref<GateLink>>& Links() const { return links; }
		const cs::List<cs::Ref<StarSystem>>& Systems() const { return systems; }

		/// <summary>How many nearest neighbours each system is linked to.</summary>
		static constexpr int Neighbours = 2;

		/// <summary>
		/// The farthest a single gate link reaches (U.SPC-T372): the same limit a state's own new gates are built
		/// within once the game is under way (<see cref="GateConstruction.MaxLinkLy"/>), kept in the one place so the
		/// initial lattice and everything grown onto it later share the same reach. Past it, two systems are only
		/// ever joined by a chain of hops through others' gates, or, where no real star sits near enough along the
		/// way, through Void Bases built in open space purely to relay it.
		/// </summary>
		// C#: = GateConstruction.MaxLinkLy (7). Spelled out to keep GateConstruction.h out of this header (it includes this one);
		// GateNetwork.cpp static_asserts the two agree.
		static constexpr double MaxLinkLy = 7;

		/// <summary>Forts a state with a navy keeps at each gate it holds (U.SPC-T92).</summary>
		static constexpr int GateForts = 2;

		/// <summary>What a route planner reckons a gate held against it costs to force (days), so a war fleet goes round by a
		/// friendly or neutral way where there is one (U.SPC-T92).</summary>
		static constexpr double ForcingDays = 6;

		/// <summary>A crossing: six hours to spin the gate up and line up, plus two hours a light-year.</summary>
		static double TransitDays(double ly) { return 0.25 + ly / 12.0; }

		/// <summary>Allowance for the flight from one gate to the next inside a system a route passes through (days).</summary>
		static constexpr double CrossingDays = 0.5;

		/// <summary>
		/// Builds the lattice over <paramref name="systems"/> (<paramref name="jd"/> and <paramref name="polities"/>
		/// for any Void Base a gap wider than <paramref name="maxLinkLy"/> needs) and gives each system its gates
		/// (see <see cref="StarSystem.Gates"/>). Any Void Base built is appended to <paramref name="systems"/> itself,
		/// so the caller's own list picks it up.
		/// </summary>
		static cs::Ref<GateNetwork> Build(cs::List<cs::Ref<StarSystem>>& systems, double jd, const cs::Ref<Polities>& polities, int neighbours = Neighbours, double maxLinkLy = MaxLinkLy);

	private:
		/// <summary>
		/// Joins <paramref name="a"/> and <paramref name="b"/>, too far apart for one gate, by a chain of Void Bases
		/// (U.SPC-T372) laid along the straight line between them, each hop within <paramref name="maxLinkLy"/>; the
		/// new systems are appended to <paramref name="systems"/> (the caller's own list) as they are built.
		/// </summary>
		static void BridgeWithRelays(const cs::Ref<GateNetwork>& net, cs::List<cs::Ref<StarSystem>>& systems, const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b, double jd, const cs::Ref<Polities>& polities,
			double maxLinkLy, int& nextVoidBase);

	public:
		/// <summary>
		/// Light-year positions for a chain of Void Bases along the straight line from <paramref name="aLy"/> to
		/// <paramref name="bLy"/> (U.SPC-T372), as evenly spaced as the gap needs so no hop is longer than
		/// <paramref name="maxLy"/>; empty when the two already are.
		/// </summary>
		static cs::List<DVec3> RelayPositions(DVec3 aLy, DVec3 bLy, double maxLy);

		/// <summary>Pairs a new gate in each of the two systems, each at its own edge facing the other.</summary>
		cs::Ref<GateLink> Link(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b);

	private:
		/// <summary>A gate is named for where it leads: the Centauri Gate in Sol, the Sol Gate at Alpha Centauri.</summary>
		static cs::String GateName(const cs::Ref<StarSystem>& to);

	public:
		/// <summary>Each gate belongs to its system's leading power (Sol's Federation, the Centauri Accord, a survey service).</summary>
		void AssignOwners();

		/// <summary>
		/// Arms the gates (U.SPC-T92): each one a state with a navy holds gets <see cref="GateForts"/> forts, which a war
		/// fleet must fight to force it; a survey service's gates in empty systems stand unguarded.
		/// </summary>
		void Fortify();

		/// <summary>
		/// Arms one gate by the same rule as <see cref="Fortify()"/>: its owner's full complement of forts if a state with a
		/// navy holds it, none otherwise. Used for a gate the builders have just finished or rebuilt (U.SPC-T294), so it
		/// opens guarded like every other held gate.
		/// </summary>
		static void Fortify(const cs::Ref<JumpGate>& g);

		/// <summary>
		/// Whether a war fleet of <paramref name="faction"/> can take <paramref name="near"/> to its partner (U.SPC-T92): both
		/// ends let it through, or are held by an enemy it may force.
		/// </summary>
		static bool Passable(const cs::Ref<JumpGate>& near, const cs::Ref<Faction>& faction, double jd);

		cs::List<cs::Ref<GateLink>> LinksOf(const cs::Ref<StarSystem>& s) const;

		/// <summary>The link joining two systems directly, or null.</summary>
		cs::Ref<GateLink> Between(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b) const;

		/// <summary>
		/// The quickest way from <paramref name="from"/> to <paramref name="to"/> over the gates
		/// (Dijkstra on crossing time, plus <see cref="CrossingDays"/> for each system passed through),
		/// as the gates to take in order, each in the system the ship is in when it takes it. Gates
		/// that will not open for <paramref name="traveller"/> at <paramref name="jd"/> are left out
		/// (null traveller: any gate still working), and a link with a ring out of action (see
		/// <see cref="GateLink.Working"/>) for everyone. Empty for the same system; null when there is no way through.
		/// <paramref name="force"/> plans a war fleet's way (U.SPC-T92): a gate held by an enemy counts too, at
		/// <see cref="ForcingDays"/> more, as one it can fight its way through.
		/// </summary>
		RouteList Route(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Faction>& traveller = nullptr, double jd = cs::Double::NegativeInfinity, bool force = false) const;

		/// <summary>Time in the gates along a route, plus the crossings between them (days).</summary>
		static double RouteDays(const RouteList& route);

		/// <summary>Light-years along a route.</summary>
		static double RouteLy(const RouteList& route);
		SG_SERIALIZABLE(GateNetwork); // ObjectGraph registration (Persistence/Registrations)
	};
}
