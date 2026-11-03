#pragma once
// Port of Assets/UnitySpace/Core/Systems/InterstellarColonisation.cs (US-T17).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Galaxy;
	class StarSystem;
	class ColonyShip;
	class ForeignSite;
	class Faction;
	class Settlement;

	/// <summary>
	/// Colony ships through the gates (U.SPC-T293). A system a new gate opens (<see cref="Galaxy.SystemOpened"/>,
	/// <see cref="GateConstruction"/>) has nobody in it to send settlers, so each system's <see cref="Colonisation"/> is
	/// shown, beside its own free sites, the free sites of every empty system open to settlement that its colonists can
	/// reach by gates that will let them through: a faction with people to spare and the money for a voyage then sends
	/// its colony ship to whichever is best for the time it takes. The voyage is reckoned as its legs are flown: from
	/// the port out to the gate, through the gates (<see cref="GateNetwork.RouteDays"/>), and in from the far gate to the
	/// site, each in-system leg at a freighter's flip-and-burn (<see cref="Contracts.TravelDays"/>). On landing the
	/// colonists found their outpost in that system's markets, where it lives as any other settlement: the system is
	/// settled, its founder holds it, and it runs (at the detail the player's whereabouts give it, U.SPC-T104) with
	/// the gate traffic that now trades with it. One colony ship at a time goes to a system still waiting for its first.
	/// </summary>
	class InterstellarColonisation final
	{
		cs::Ref<Galaxy> galaxy;

	public:
		explicit InterstellarColonisation(const cs::Ref<Galaxy>& galaxy);

	private:
		void Wire(const cs::Ref<StarSystem>& home);

		/// <summary>
		/// Sends a colony ship's hull (U.SPC-T524), just fitted out at its port in <paramref name="home"/>, off through the gates
		/// to the system its site is in: it flies out to the gate, through, and in to the site, where it lands its colonists
		/// (<see cref="VoyagePurpose.Colonise"/>). False if it cannot go.
		/// </summary>
		bool Sail(const cs::Ref<StarSystem>& home, const cs::Ref<ColonyShip>& v, double jd);

	public:
		/// <summary>
		/// The free sites <paramref name="f"/>'s colonists at <paramref name="from"/> (in <paramref name="home"/>) could
		/// settle in the empty systems open to settlement, by the quickest way through the gates open to them, with the
		/// days each would take. A system another colony ship is already bound for is left to it.
		/// </summary>
		cs::List<cs::Ref<ForeignSite>> SitesFrom(const cs::Ref<StarSystem>& home, const cs::Ref<Faction>& f, const cs::Ref<Settlement>& from, double jd);
	};
}
