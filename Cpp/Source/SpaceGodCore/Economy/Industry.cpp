// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Industry.cs (US-T19).

#include "Economy/Industry.h"

namespace SpaceGod::Core
{
	const char* ToString(IndustryKind v)
	{
		switch (v)
		{
			case IndustryKind::Mine: return "Mine";
			case IndustryKind::Farm: return "Farm";
			case IndustryKind::PharmaLab: return "PharmaLab";
			case IndustryKind::Smelter: return "Smelter";
			case IndustryKind::ElectronicsFab: return "ElectronicsFab";
			case IndustryKind::MachineWorks: return "MachineWorks";
			case IndustryKind::MunitionsPlant: return "MunitionsPlant";
			case IndustryKind::Shipyard: return "Shipyard";
		}
		return "";
	}

	Recipe::Recipe(IndustryKind kind, const cs::String& name, const cs::List<std::tuple<Good, double>>& inputs, const cs::List<std::tuple<Good, double>>& outputs, double workersPerLevel)
	{
		Kind = kind;
		WorkersPerLevel = workersPerLevel;
		Name = name;
		Inputs = inputs;
		Outputs = outputs;
		double total = 0;
		for (const auto& [_, rate] : inputs) total += rate;
		Limiting = std::vector<bool>(static_cast<size_t>(inputs.Count()));
		for (int i = 0; i < inputs.Count(); i++) Limiting[static_cast<size_t>(i)] = kind != IndustryKind::Mine && std::get<1>(inputs[i]) >= total * 0.02;
	}

	const cs::Dictionary<IndustryKind, cs::Ref<Recipe>>& Recipe::Fixed()
	{
		static const cs::Dictionary<IndustryKind, cs::Ref<Recipe>> fixed = []
		{
			using T = std::tuple<Good, double>;
			cs::Dictionary<IndustryKind, cs::Ref<Recipe>> d;
			d[IndustryKind::Farm] = cs::New<Recipe>(IndustryKind::Farm, "Hydroponic farms",
				cs::List<T>{ T(Good::Water, 700.0), T(Good::Nitrogen, 60.0), T(Good::HeliumThree, 0.4) },
				cs::List<T>{ T(Good::Food, 420.0) }, 1440);
			d[IndustryKind::PharmaLab] = cs::New<Recipe>(IndustryKind::PharmaLab, "Pharmaceutical labs",
				cs::List<T>{ T(Good::Water, 60.0), T(Good::Hydrocarbons, 50.0), T(Good::RareEarths, 0.6) },
				cs::List<T>{ T(Good::MedicalSupplies, 22.0) }, 600);
			d[IndustryKind::Smelter] = cs::New<Recipe>(IndustryKind::Smelter, "Smelters",
				cs::List<T>{ T(Good::NickelIron, 2200.0), T(Good::Titanium, 90.0), T(Good::HeliumThree, 0.8) },
				cs::List<T>{ T(Good::Alloys, 800.0) }, 930);
			d[IndustryKind::ElectronicsFab] = cs::New<Recipe>(IndustryKind::ElectronicsFab, "Electronics fabs",
				cs::List<T>{ T(Good::Copper, 160.0), T(Good::Zinc, 40.0), T(Good::RareEarths, 9.0), T(Good::Platinum, 1.2), T(Good::Alloys, 25.0) },
				cs::List<T>{ T(Good::Electronics, 55.0) }, 2500);
			d[IndustryKind::MachineWorks] = cs::New<Recipe>(IndustryKind::MachineWorks, "Machine works",
				cs::List<T>{ T(Good::Alloys, 280.0), T(Good::Electronics, 18.0), T(Good::Copper, 40.0) },
				cs::List<T>{ T(Good::Machinery, 190.0) }, 1200);
			d[IndustryKind::MunitionsPlant] = cs::New<Recipe>(IndustryKind::MunitionsPlant, "Munitions plants",
				cs::List<T>{ T(Good::Alloys, 90.0), T(Good::Hydrocarbons, 60.0), T(Good::Electronics, 8.0) },
				cs::List<T>{ T(Good::Munitions, 55.0) }, 900);
			// A yard turns materials into build points (1 per tonne of alloys) rather than a good.
			d[IndustryKind::Shipyard] = cs::New<Recipe>(IndustryKind::Shipyard, "Shipyard",
				cs::List<T>{ T(Good::Alloys, 300.0), T(Good::Electronics, 25.0), T(Good::Machinery, 40.0) },
				cs::List<T>(), 4000);
			return d;
		}();
		return fixed;
	}

	cs::Ref<Recipe> Recipe::Of(IndustryKind kind) { return Fixed()[kind]; }

	cs::Ref<Recipe> Recipe::Mine(Good ore, double perDay)
	{
		using T = std::tuple<Good, double>;
		return cs::New<Recipe>(IndustryKind::Mine, cs::Cat(GoodInfo::Of(ore)->Name, " extraction"),
			cs::List<T>{ T(Good::Machinery, perDay * 0.0012), T(Good::HeliumThree, perDay * 0.00008) },
			cs::List<T>{ T(ore, perDay) }, perDay * MineWorkersPerTonneDay(ore));
	}

	double Recipe::MineWorkersPerTonneDay(Good ore)
	{
		const double bulk = 365.0 / 12000;
		switch (ore)
		{
			case Good::Copper: case Good::Zinc: case Good::Titanium: return bulk * 10;
			case Good::Platinum: case Good::RareEarths: case Good::HeliumThree: return bulk * 1000;
			default: return bulk;
		}
	}

	double Facility::Workers() const { return Recipe->WorkersPerLevel * Level; }

	bool Facility::LifeSupport() const
	{
		return Recipe->Kind == IndustryKind::Farm || (Recipe->Kind == IndustryKind::Mine && (std::get<0>(Recipe->Outputs[0]) == Good::Water || std::get<0>(Recipe->Outputs[0]) == Good::Nitrogen));
	}

	Facility::Facility(cs::Ref<::SpaceGod::Core::Recipe> recipe, double level)
	{
		Recipe = recipe;
		Level = BaseLevel = level;
	}
}
