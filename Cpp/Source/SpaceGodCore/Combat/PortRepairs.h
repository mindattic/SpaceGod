#pragma once
// US-T266: the port's Repair & Rearm service for the hull the player holds. Warships of a navy repair and rearm at their
// own yards in War::Refit, drawing alloys, electronics and munitions off the yard's shelves; this is the same service sold
// to the player item by item, with a quote for each: the hull (and its structure), each hardpoint that is hurt or shot
// away, the shield emitters' fields, the magazines and the tanks. The parts come off the yard's market at its prices (and
// the player's standing there), with the yard's labour on top; a job the shelves or the purse cannot cover in full is done
// as far as they go (the hull and the magazines) or not at all (a hardpoint, the shields). Nothing in the simulation calls
// it: only the player's port screen does, so a Core-only Bench run is unchanged.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Economy;
	class Settlement;
	class Ship;

	/// <summary>What one line of a repair quote mends.</summary>
	enum class RepairItemKind : int
	{
		Hull,
		Subsystem,
		Shields,
		Munitions,
		Propellant,
	};

	/// <summary>One line of a repair quote: what it mends, what it takes off the yard's shelves, and what it costs.</summary>
	struct SPACEGODCORE_API RepairItem
	{
		RepairItemKind Kind = RepairItemKind::Hull;
		/// <summary>The hardpoint's index in the hull's subsystems, for <see cref="RepairItemKind.Subsystem"/>; -1 otherwise.</summary>
		int Index = -1;
		/// <summary>A line for the quote: what it is and how hurt.</summary>
		cs::String Label;
		/// <summary>Tonnes of alloys, electronics and munitions the whole job takes off the shelves (propellant is the
		/// market's own refuel, priced by <see cref="Economy.PropellantPrice"/>).</summary>
		double Alloys = 0, Electronics = 0, Munitions = 0;
		/// <summary>Credits for the whole job at this port: the materials at its prices and the player's standing, port
		/// tax and the yard's labour.</summary>
		double Cost = 0;
		/// <summary>Share of the job the yard's shelves can cover now, 0 to 1.</summary>
		double InStock = 1;
		/// <summary>Why the port cannot do it at all (no yard for structural work), empty when it can.</summary>
		cs::String Blocked;
	};

	enum class RepairOutcome : int
	{
		Ok,
		/// <summary>Done, but only as far as the shelves or the purse went.</summary>
		Partial,
		NothingToDo,
		NotAYard,
		NoStock,
		CantAfford,
		CannotDock,
	};

	class SPACEGODCORE_API PortRepairs final
	{
	public:
		PortRepairs() = delete;

		/// <summary>Tonnes of alloys and electronics a hull a kilometre long takes to mend from nothing (War::Refit's rate).</summary>
		static constexpr double HullAlloysPerKm = 6000;
		static constexpr double HullElectronicsPerKm = 400;
		/// <summary>Tonnes a shot-away hardpoint takes to build anew, per kilometre of hull, and the least any hardpoint takes.</summary>
		static constexpr double MountAlloysPerKm = 60;
		static constexpr double MountElectronicsPerKm = 20;
		static constexpr double MountMinTonnes = 0.5;
		/// <summary>Tonnes of electronics (charge cells, emitter tuning) a hull's emptied shield fields take per kilometre.</summary>
		static constexpr double ShieldElectronicsPerKm = 4;
		static constexpr double ShieldMinTonnes = 0.1;
		/// <summary>Tonnes of munitions a missile or torpedo takes (War::Refit's rate), and a round of anything else.</summary>
		static constexpr double MissileTonnes = 1.5;
		static constexpr double RoundTonnes = 0.05;

		/// <summary>Whether <paramref name="yard"/> does structural work (the hull, a hardpoint): a shipyard or a haven.</summary>
		static bool DoesStructure(const cs::Ref<Settlement>& yard);

		/// <summary>The quote at <paramref name="port"/> for everything <paramref name="ship"/> needs, one line each: the hull,
		/// every hardpoint hurt or shot away, the shields, the magazines, the tanks. Empty for a hull with nothing to mend.
		/// <paramref name="priceFactor"/> is the player's standing there (<see cref="Law.PriceFactor"/>), 1 for none.</summary>
		static cs::List<RepairItem> Quote(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port,
			double priceFactor);

		/// <summary>Carries out one line of the quote (the kind and, for a hardpoint, its index), charging the hull's purse and
		/// drawing the parts off the port's shelves. <paramref name="cost"/> is what was paid.</summary>
		static RepairOutcome Apply(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port,
			double priceFactor, RepairItemKind kind, int index, double& cost);

		/// <summary>Every line of the quote in turn (the hull first, the tanks last), as far as the shelves and purse go.
		/// <paramref name="done"/> counts the lines carried out, <paramref name="cost"/> what was paid in all.</summary>
		static RepairOutcome RepairAll(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port,
			double priceFactor, int& done, double& cost);

	private:
		static bool Line(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, double priceFactor,
			RepairItemKind kind, int index, RepairItem& item);
		static double MaterialsCost(const cs::Ref<Settlement>& port, double alloys, double electronics, double munitions, double priceFactor);
		static void Charge(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, double alloys, double electronics, double munitions,
			double cost);
	};

	SPACEGODCORE_API const char* ToString(RepairOutcome v);
}
