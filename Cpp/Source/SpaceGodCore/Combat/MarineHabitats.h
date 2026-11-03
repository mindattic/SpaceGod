#pragma once
// Port of Assets/UnitySpace/Core/Combat/MarineHabitats.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class RangeSetting;
	class FacilityKind;
	class Loadout;
	class HullFrame;
	class Interior;
	class Ship;

	/// <summary>
	/// Marine habitats (U.SPC-T465): where a ship's marines live and train. A habitat is a facility
	/// (<see cref="FacilityKinds.MarineHabitat"/>) fitted into a hull's internal space, and a hull's marine complement is
	/// what its habitats quarter (<see cref="FacilityLayouts.Complement(IEnumerable{FacilitySite}, double)"/>), not a flat
	/// figure per class: each stock design is drawn with the habitats that hold the marines it always had, and a yard fits
	/// more (fifty berths per unit of hull scale, <see cref="BerthsPerScale"/>) or strips them out for something else.
	/// Only hulls big enough for one may carry it (<see cref="FacilityKinds.MarineHulls"/>); fighters, small craft and
	/// satellites repel boarders with their crew alone. The habitat is the barracks compartment
	/// (<see cref="CompartmentKind.Barracks"/>) defenders muster from when the hull is boarded, and drilling in it raises
	/// the marines' quality day by day (<see cref="Interior.Drill"/>, <see cref="Factor"/>), repelling boarders and
	/// boarding alike. What a habitat has ready (<see cref="Ready(Interior)"/>, <see cref="Draw"/>) is what a hull's
	/// assault teams are drawn from. The tunables are in the settings menu's Broadsides group.
	/// </summary>
	// [SettingsSource]
	class MarineHabitats
	{
	public:
		/// <summary>Marines one fitted habitat quarters per unit of hull scale (<see cref="Loadout.Scale"/>): fifty on a destroyer.</summary>
		static constexpr double BerthsPerScale = 50;

		static const cs::Ref<RangeSetting>& DrillDays();

		static const cs::Ref<RangeSetting>& DrillBonus();

		/// <summary>The facility kind.</summary>
		static cs::Ref<FacilityKind> Kind();

		/// <summary>What marines drilled to <paramref name="training"/> (0 to 1) multiply their quality by.</summary>
		static double Factor(double training);

		/// <summary>The marines this loadout's facilities quarter: its marine complement.</summary>
		static double Complement(const cs::Ref<Loadout>& l);

		/// <summary>Habitats in the hull.</summary>
		static int Count(const cs::Ref<Loadout>& l);

		/// <summary>Whether a hull of this frame could ever carry marines in a habitat (it has one, or room and leave to fit one).</summary>
		static bool CanCarry(const cs::Ref<HullFrame>& frame);

		/// <summary>
		/// Marines in the habitats' barracks ready to be sent out (to board, or as an assault team): none while the hull
		/// is itself being boarded, when every one is needed at home.
		/// </summary>
		static double Ready(const cs::Ref<Interior>& interior);

		/// <summary>Marines the ship's habitats have ready.</summary>
		static double Ready(const cs::Ref<Ship>& ship);

		/// <summary>Whole teams of <paramref name="teamSize"/> the habitats have ready.</summary>
		static int TeamsReady(const cs::Ref<Interior>& interior, double teamSize);

		/// <summary>
		/// Takes up to <paramref name="marines"/> ready marines out of the habitats' barracks, evenly from each (an assault
		/// team leaving); returns how many went. Nothing is drawn while the hull is being boarded.
		/// </summary>
		static double Draw(const cs::Ref<Interior>& interior, double marines);
	};
}
