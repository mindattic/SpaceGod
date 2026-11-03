// Port of Assets/UnitySpace/Core/Combat/MarineHabitats.cs (see docs/MIGRATION.md).

#include "Combat/MarineHabitats.h"

#include "Combat/Boarding.h"
#include "Combat/CombatState.h"
#include "Combat/Construction.h"
#include "Combat/Facilities.h"
#include "Combat/Outfitting.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	const cs::Ref<RangeSetting>& MarineHabitats::DrillDays()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.marineDrillDays", SettingGroups::Broadsides,
			"Marine drill time", 180, 10, 720, 10,
			"How many days raw marines drill in their ship's marine habitat to be fully trained. Recruits who make up losses "
			"start raw and bring the complement's training down with them.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0") + " days"; }), "marines habitat barracks training drill boarding");
		return s;
	}

	const cs::Ref<RangeSetting>& MarineHabitats::DrillBonus()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.marineDrillBonus", SettingGroups::Broadsides,
			"Trained marine edge", 0.3, 0, 1, 0.05,
			"How much better, man for man, fully trained marines fight than raw ones, both repelling boarders and boarding "
			"from their ship.",
			cs::Func<cs::String, double>([](double v) { return "+" + cs::Fmt(v * 100, "0") + "%"; }), "marines habitat barracks training drill boarding quality");
		return s;
	}

	cs::Ref<FacilityKind> MarineHabitats::Kind() { return FacilityKinds::Of(FacilityKinds::MarineHabitat); }

	double MarineHabitats::Factor(double training) { return 1 + DrillBonus()->Value * cs::Math::Max(0.0, cs::Math::Min(1.0, training)); }

	double MarineHabitats::Complement(const cs::Ref<Loadout>& l) { return l == nullptr ? 0 : FacilityLayouts::Complement(l->Facilities, l->Scale()); }

	int MarineHabitats::Count(const cs::Ref<Loadout>& l) { return l ? l->CountFacility(FacilityKinds::MarineHabitat) : 0; }

	bool MarineHabitats::CanCarry(const cs::Ref<HullFrame>& frame)
	{
		if (frame == nullptr) return false;
		for (const auto& s : frame->Facilities) if (s->Kind == FacilityKinds::MarineHabitat) return true;
		return frame->CanCarry(Kind());
	}

	double MarineHabitats::Ready(const cs::Ref<Interior>& interior)
	{
		if (interior == nullptr || interior->UnderBoarding() || interior->Captured) return 0;
		double n = 0;
		for (const auto& c : interior->Barracks()) n += c->Marines;
		return n;
	}

	double MarineHabitats::Ready(const cs::Ref<Ship>& ship)
	{
		return Ready(ship != nullptr && ship->Combat != nullptr ? ship->Combat->Interior : cs::Ref<Interior>());
	}

	int MarineHabitats::TeamsReady(const cs::Ref<Interior>& interior, double teamSize)
	{
		return teamSize <= 0 ? 0 : cs::ToInt(cs::Math::Floor(Ready(interior) / teamSize + 1e-9));
	}

	double MarineHabitats::Draw(const cs::Ref<Interior>& interior, double marines)
	{
		double have = Ready(interior);
		if (have <= 0 || marines <= 0) return 0;
		double share = cs::Math::Min(1.0, marines / have);
		for (const auto& c : interior->Barracks()) c->Marines -= c->Marines * share;
		return have * share;
	}
}
