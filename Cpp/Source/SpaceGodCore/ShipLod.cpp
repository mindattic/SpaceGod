// Port of Unity.SpaceGod Core/ShipLod.cs (see docs/MIGRATION.md).

#include "ShipLod.h"
#include "RenderLod.h"
#include "Ships.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Economy/BeltMining.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	struct ShipLod::State
	{
		cs::Ref<RangeSetting> IconBelowPx;
		cs::Ref<ChoiceSetting> IconNames;
	};

	ShipLod::State& ShipLod::S()
	{
		// C#'s static field initialisers, in textual order.
		static State* s = []
		{
			auto* st = new State();
			st->IconBelowPx = cs::New<RangeSetting>("lod.shipIconPx", SettingGroups::RenderBudget,
				"Ship model to icon", 5, 2, 40, 1,
				"A ship shorter than this on screen is drawn as a flat faction icon with its name instead of its 3D model. Higher is faster in a crowded system.",
				cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " px"); }), "lod icon billboard model ship distance performance");
			st->IconNames = cs::New<ChoiceSetting>("lod.shipIconNames", SettingGroups::RenderBudget,
				"Ship icon names", cs::List<cs::String>{ "Off", "Warships", "All" }, NamesAll,
				"Which far-off ships drawn as icons are named beside their icon. A name that would overlap one already drawn is left off.",
				"label name icon ship tag");
			return st;
		}();
		return *s;
	}

	const cs::Ref<RangeSetting>& ShipLod::IconBelowPx() { return S().IconBelowPx; }
	const cs::Ref<ChoiceSetting>& ShipLod::IconNames() { return S().IconNames; }

	double ShipLod::ApparentPx(double length, double depth, double pixelsPerUnit)
	{
		return RenderLod::ApparentPx(length, depth, pixelsPerUnit);
	}

	bool ShipLod::DrawsModel(double px) { return RenderLod::DrawsModel(RenderLod::Tier(RenderLod::Ship(), px)); }

	bool ShipLod::DrawsFullModel(double px) { return RenderLod::Tier(RenderLod::Ship(), px) == LodTier::Full; }

	double ShipLod::ExtentFactor(const cs::Ref<Ship>& ship)
	{
		auto load = ship ? ship->Tow : nullptr;
		double hull = (ship ? ship->Combat : nullptr) != nullptr ? ship->LengthKm() : 0;
		return load == nullptr || hull <= 0 ? 1 : 1 + cs::Math::Max(0.0, load->DiameterM) / 1000 / hull;
	}

	bool ShipLod::NamesIcon(bool military, bool escort) { return NamesIcon(IconNames()->Index, military, escort); }

	bool ShipLod::NamesIcon(int mode, bool military, bool escort)
	{
		if (escort) return false;
		return mode == NamesAll || (mode == NamesWarships && military);
	}

	cs::String ShipLod::Armament(const cs::List<cs::Ref<Subsystem>>& subsystems)
	{
		cs::List<SubsystemKind> order;
		cs::Dictionary<SubsystemKind, int> mounted;
		cs::Dictionary<SubsystemKind, int> lost;
		for (const auto& s : subsystems)
		{
			if (s == nullptr || !s->Def->IsWeapon()) continue;
			if (!mounted.ContainsKey(s->Kind()))
			{
				order.Add(s->Kind());
				mounted[s->Kind()] = 0;
				lost[s->Kind()] = 0;
			}
			mounted[s->Kind()]++;
			if (s->Destroyed()) lost[s->Kind()]++;
		}
		if (order.Count() == 0) return "Unarmed";
		cs::StringBuilder sb;
		for (const auto& k : order)
		{
			if (sb.Length() > 0) sb.Append(", ");
			sb.Append(mounted[k]).Append(' ').Append(cs::Strings::ToLowerInvariant(SubsystemDef::Name(k)));
			if (lost[k] > 0) sb.Append(" (").Append(lost[k]).Append(" lost)");
		}
		return sb.ToString();
	}
}
