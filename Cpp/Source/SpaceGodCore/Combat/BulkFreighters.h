#pragma once
// Port of Assets/UnitySpace/Core/Combat/BulkFreighters.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class RangeSetting;
	class FacilitySite;
	class Ship;
	enum class ShipClass : int;

	/// <summary>
	/// The bulk freighter as the roster has it (U.SPC-T482, docs/SHIP_ROSTER.md): a skeletal truss with its engine at one
	/// end and standard modules clamped along it, cargo pods and fuel spheres, built in space and never landing on a world.
	/// <list type="bullet">
	/// <item>Its hold is its pods. Each <see cref="FacilityKinds.CargoPod"/> is a facility of one section carrying
	/// <see cref="PodTonnes"/> of trade cargo and one row of the grid hold, so its capacity is the pods it carries
	/// (<see cref="Loadout.CapacityTonnes"/>); a yard clamps more pods into its free space or takes them off. The stock
	/// design's <see cref="StockPods"/> carry what its holds did before (<see cref="Economy.CapacityOf"/>).</item>
	/// <item>Its tanks are its spheres: each <see cref="FacilityKinds.FuelSphere"/> holds <see cref="SphereTonnes"/> of
	/// reaction mass, so trading a sphere for a pod is trading range for cargo.</item>
	/// <item>No armour and no weapons: every kilogram goes to cargo. Its design has no gun mount, launcher or mine layer, and
	/// a yard will fit it neither plate nor racks (<see cref="FacilityLayouts.Bars"/>). What guards it is the guns of
	/// others: the fighter wings merchants hire in port (<see cref="MerchantEscorts"/>) and a navy's supply-convoy escorts.</item>
	/// <item>It rides the lanes cheaply: its lane toll is <see cref="LaneTollShare"/> of a ship of its mass
	/// (<see cref="Economy.LaneToll(ShipClass, double)"/>), and off the lanes its drive is weak, so it is slow to speed up
	/// and slow down (its class's low acceleration and its design's slow battle drive).</item>
	/// <item>It never lands: at a ringed world it trades from orbit instead of queueing to go down to the surface
	/// (<see cref="NeverLands"/>).</item>
	/// </list>
	/// Another class is made a truss with <see cref="RegisterTruss"/>.
	/// </summary>
	// [SettingsSource]
	class BulkFreighters
	{
	public:
		/// <summary>Trade cargo one pod carries, tonnes: the stock design's pods carry what a freighter's holds did.</summary>
		static constexpr double PodTonnes = 2500;

		/// <summary>Pods on the stock design: 8 x 2,500 t = 20,000 t, a freighter's hold as before (the economy's throughput unchanged).</summary>
		static constexpr int StockPods = 8;

		/// <summary>Rows of the grid hold each pod gives (a freighter's hold is six cells wide, one row a pod).</summary>
		static constexpr int PodRows = 1;

		/// <summary>Reaction mass one fuel sphere holds, tonnes: the stock design's two hold a freighter's tanks as before.</summary>
		static constexpr double SphereTonnes = 3000;

		static constexpr int StockSpheres = 2;

		/// <summary>An empty pod's and an empty sphere's own mass, tonnes.</summary>
		static constexpr double PodMassTonnes = 150; static constexpr double SphereMassTonnes = 90;

	private:
		// C# `static readonly HashSet<ShipClass> trusses`: mutable (RegisterTruss), so a non-const function-local static.
		static cs::HashSet<ShipClass>& trusses();

	public:
		/// <summary>The classes built as a truss: the ones a pod or a sphere may clamp to.</summary>
		static cs::HashSet<ShipClass>& TrussClasses() { return trusses(); }

		/// <summary>Makes every hull of <paramref name="cls"/> a truss (or, with <paramref name="truss"/> false, not one).</summary>
		static void RegisterTruss(ShipClass cls, bool truss = true);

		static bool IsTruss(ShipClass cls);

		/// <summary>Whether a ship is never taken down to a world's surface: a truss is built in space and stays there.</summary>
		static bool NeverLands(const cs::Ref<Ship>& s);

		static const cs::Ref<RangeSetting>& LaneTollShare();

		/// <summary>The share of a lane's toll a ship of <paramref name="cls"/> pays.</summary>
		static double LaneTollShareOf(ShipClass cls);

		/// <summary>
		/// The stock freighter's facilities, bow to stern: the cockpit, the pod gantry down the spine (the hold boarders
		/// break into: the walkway the pods' hatches open on, since nobody walks into a pod itself), the pods clamped along
		/// the truss, the spheres astern of them and the engine room at the drive.
		/// </summary>
		static cs::List<cs::Ref<FacilitySite>> StockLayout();

		/// <summary>Where along the truss (ship lengths, nose +0.5) the clamp for pod <paramref name="i"/> of
		/// <paramref name="ofPods"/> lies: in pairs, one each side of the spine, from behind the cockpit back to the spheres.</summary>
		static double PodZ(int i, int ofPods);

		/// <summary>The cargo pods clamped to a ship's truss (its loadout's, or its design's).</summary>
		static int Pods(const cs::Ref<Ship>& s);

		/// <summary>The fuel spheres clamped to a ship's truss.</summary>
		static int Spheres(const cs::Ref<Ship>& s);

	private:
		static int Count(const cs::Ref<Ship>& s, const cs::String& kind);

	public:
		/// <summary>How many of its pods are full, as its model shows them: what it carries against what its pods hold,
		/// rounded up (any load at all fills a pod).</summary>
		static int LoadedPods(const cs::Ref<Ship>& s);
	};
}
