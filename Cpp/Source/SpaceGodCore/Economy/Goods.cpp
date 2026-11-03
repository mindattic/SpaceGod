// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Goods.cs (US-T19).

#include "Economy/Goods.h"

namespace SpaceGod::Core
{
	const char* ToString(Good v)
	{
		switch (v)
		{
			case Good::Food: return "Food";
			case Good::Water: return "Water";
			case Good::MedicalSupplies: return "MedicalSupplies";
			case Good::HeliumThree: return "HeliumThree";
			case Good::NickelIron: return "NickelIron";
			case Good::Copper: return "Copper";
			case Good::Zinc: return "Zinc";
			case Good::Platinum: return "Platinum";
			case Good::RareEarths: return "RareEarths";
			case Good::Titanium: return "Titanium";
			case Good::Hydrocarbons: return "Hydrocarbons";
			case Good::Nitrogen: return "Nitrogen";
			case Good::Alloys: return "Alloys";
			case Good::Electronics: return "Electronics";
			case Good::Machinery: return "Machinery";
			case Good::Munitions: return "Munitions";
		}
		return "";
	}

	const char* ToString(GoodCategory v)
	{
		switch (v)
		{
			case GoodCategory::Essential: return "Essential";
			case GoodCategory::Mineral: return "Mineral";
			case GoodCategory::Volatile: return "Volatile";
			case GoodCategory::Industrial: return "Industrial";
			case GoodCategory::Military: return "Military";
		}
		return "";
	}

	GoodInfo::GoodInfo(::SpaceGod::Core::Good good, const cs::String& name, double basePrice, GoodCategory category)
	{
		Good = good;
		Name = name;
		BasePrice = basePrice;
		Category = category;
	}

	const cs::List<cs::Ref<GoodInfo>>& GoodInfo::All()
	{
		using G = ::SpaceGod::Core::Good;
		static const cs::List<cs::Ref<GoodInfo>> all =
		{
			cs::New<GoodInfo>(G::Food, "Food", 120, GoodCategory::Essential),
			cs::New<GoodInfo>(G::Water, "Water", 30, GoodCategory::Essential),
			cs::New<GoodInfo>(G::MedicalSupplies, "Medical supplies", 2500, GoodCategory::Essential),
			cs::New<GoodInfo>(G::HeliumThree, "Helium-3", 3000, GoodCategory::Volatile),
			cs::New<GoodInfo>(G::NickelIron, "Nickel-iron ore", 40, GoodCategory::Mineral),
			cs::New<GoodInfo>(G::Copper, "Copper", 300, GoodCategory::Mineral),
			cs::New<GoodInfo>(G::Zinc, "Zinc", 150, GoodCategory::Mineral),
			cs::New<GoodInfo>(G::Platinum, "Platinum-group metals", 20000, GoodCategory::Mineral),
			cs::New<GoodInfo>(G::RareEarths, "Rare earth elements", 4000, GoodCategory::Mineral),
			cs::New<GoodInfo>(G::Titanium, "Titanium", 600, GoodCategory::Mineral),
			cs::New<GoodInfo>(G::Hydrocarbons, "Hydrocarbons", 90, GoodCategory::Volatile),
			cs::New<GoodInfo>(G::Nitrogen, "Nitrogen", 40, GoodCategory::Volatile),
			cs::New<GoodInfo>(G::Alloys, "Refined alloys", 900, GoodCategory::Industrial),
			cs::New<GoodInfo>(G::Electronics, "Electronics", 6000, GoodCategory::Industrial),
			cs::New<GoodInfo>(G::Machinery, "Machinery", 3500, GoodCategory::Industrial),
			cs::New<GoodInfo>(G::Munitions, "Munitions", 5000, GoodCategory::Military),
		};
		return all;
	}

	cs::Ref<GoodInfo> GoodInfo::Of(::SpaceGod::Core::Good g) { return All()[static_cast<int>(g)]; }
}
