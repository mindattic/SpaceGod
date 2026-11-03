#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Goods.cs (US-T19).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>Every traded commodity. Quantities are tonnes; prices are credits per tonne.</summary>
	enum class Good : int
	{
		Food,
		Water,
		MedicalSupplies,
		HeliumThree,
		NickelIron,
		Copper,
		Zinc,
		Platinum,
		RareEarths,
		Titanium,
		Hydrocarbons,
		Nitrogen,
		Alloys,
		Electronics,
		Machinery,
		Munitions,
	};
	SPACEGODCORE_API const char* ToString(Good v);

	enum class GoodCategory : int
	{
		Essential,
		Mineral,
		Volatile,
		Industrial,
		Military,
	};
	SPACEGODCORE_API const char* ToString(GoodCategory v);

	class SPACEGODCORE_API GoodInfo final
	{
	public:
		::SpaceGod::Core::Good Good = static_cast<::SpaceGod::Core::Good>(0);
		cs::String Name;
		double BasePrice = 0;
		GoodCategory Category = static_cast<GoodCategory>(0);

		// private in C# (only All builds them); public here so cs::New can reach it.
		GoodInfo(::SpaceGod::Core::Good good, const cs::String& name, double basePrice, GoodCategory category);

		/// Enum.GetValues(typeof(Good)).Length.
		static inline const int Count = 16;

	private:
		static const cs::List<cs::Ref<GoodInfo>>& All();

	public:
		static const cs::Ref<GoodInfo>& Of(::SpaceGod::Core::Good g); // US-T42: a const& into All()
	};
}
