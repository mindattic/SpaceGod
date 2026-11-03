// Port of Assets/UnitySpace/Core/Combat/BuriedBridges.cs (see docs/MIGRATION.md).

#include "Combat/BuriedBridges.h"

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	const cs::Ref<RangeSetting>& BuriedBridges::Depth()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("broadside.bridgeDepth", SettingGroups::Broadsides,
			"Buried bridge depth", 0.5, 0.1, 1, 0.05,
			"How much of a warship's hull must be shot away before fire reaches its bridge, buried at the centre of the hull; "
			"past that, more of each hit aimed at it gets through the deeper the hull is holed, all of it by the time the hull is gone.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v * 100, "0") + "%"; }), "bridge buried armour depth penetration capital warship");
		return s;
	}

	double BuriedBridges::Reach(const cs::Ref<ShipCombat>& c, const cs::Ref<Subsystem>& s)
	{
		if (s == nullptr || !s->Def->Buried) return 1;
		if (c == nullptr) return 0;
		double depth = cs::Math::Max(1e-6, Depth()->Value);
		double holed = 1 - c->HullFraction();
		return cs::Math::Max(0.0, cs::Math::Min(1.0, (holed - (1 - depth)) / depth));
	}
}
