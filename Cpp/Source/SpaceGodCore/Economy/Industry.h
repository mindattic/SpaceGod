#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Industry.cs (US-T19).

#include "Cs/Cs.h"
#include "Economy/Goods.h"

#include <tuple>
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	enum class IndustryKind : int
	{
		Mine,
		Farm,
		PharmaLab,
		Smelter,
		ElectronicsFab,
		MachineWorks,
		MunitionsPlant,
		Shipyard,
	};
	const char* ToString(IndustryKind v);

	/// <summary>A production recipe: tonnes per day at level 1. Mines take their output from the site.</summary>
	class Recipe final
	{
	public:
		IndustryKind Kind = static_cast<IndustryKind>(0);
		cs::String Name;
		cs::List<std::tuple<Good, double>> Inputs;
		cs::List<std::tuple<Good, double>> Outputs;

		/// <summary>
		/// Per input: true if it is a bulk feedstock that caps output outright; false for a consumable
		/// (fuel, spares) whose shortage only cuts efficiency, down to <see cref="MinorFloor"/>.
		/// </summary>
		cs::List<bool> Limiting;

		static constexpr double MinorFloor = 0.55;

		/// <summary>Workers needed to run one level at full capacity (see Economy/SOURCES.md).</summary>
		double WorkersPerLevel = 0;

		Recipe(IndustryKind kind, const cs::String& name, const cs::List<std::tuple<Good, double>>& inputs, const cs::List<std::tuple<Good, double>>& outputs, double workersPerLevel = 0);

	private:
		static const cs::Dictionary<IndustryKind, cs::Ref<Recipe>>& Fixed();

	public:
		static cs::Ref<Recipe> Of(IndustryKind kind);

		static cs::Ref<Recipe> Mine(Good ore, double perDay);

		/// <summary>
		/// Miners per tonne a day of output. Bulk ice, ore and gas: about 12,000 t per worker-year, the
		/// productivity of US coal mining (EIA). Concentrated metals take ten times the labour per tonne and
		/// the trace products (platinum-group metals, rare earths, helium-3 sifted from regolith) a thousand
		/// times: both assumed, scaled to how lean the deposits are.
		/// </summary>
		static double MineWorkersPerTonneDay(Good ore);
	private:
		Recipe() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Recipe); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>One industry at a settlement. Level scales the recipe; it drifts with profitability.</summary>
	class Facility final
	{
	public:
		cs::Ref<::SpaceGod::Core::Recipe> Recipe;
		double Level = 0;
		double BaseLevel = 0;

		/// <summary>Fraction of capacity achieved over the last tick (inputs, crew or storage permitting).</summary>
		double Utilisation = 0;

		/// <summary>Share of capacity a shock leaves working (an accident in a mine, a blight in the farms); 1 normally.</summary>
		double Impairment = 1;

		/// <summary>For a mine: tonnes taken from the current seam, and what it held (0 until <see cref="MarketShocks"/> surveys it).</summary>
		double Extracted = 0, Reserve = 0;

		/// <summary>Share of its crew it had over the last tick (see <see cref="People.AssignLabour"/>).</summary>
		double Staffing = 1;

		/// <summary>Workers it needs at its current level.</summary>
		double Workers() const;

		/// <summary>Farms, and water and nitrogen (air) extraction: kept running through a capture, and crewed first.</summary>
		bool LifeSupport() const;

		Facility(cs::Ref<::SpaceGod::Core::Recipe> recipe, double level);
	private:
		Facility() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Facility); // ObjectGraph registration (Persistence/Registrations)
	};
}
