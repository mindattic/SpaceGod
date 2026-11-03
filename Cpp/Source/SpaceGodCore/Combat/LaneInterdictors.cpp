// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/LaneInterdictors.cs (see docs/MIGRATION.md).

#include "Combat/LaneInterdictors.h"

#include "Ships.h"
#include "Combat/CombatSpecs.h"
#include "Combat/Construction.h"
#include "Combat/Facilities.h"
#include "Combat/FleetCoordination.h"
#include "Combat/Outfitting.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	const cs::List<HullCategory>& LaneInterdictors::Hulls()
	{
		static const cs::List<HullCategory> s =
		{
			HullCategory::Warship, HullCategory::Battleship, HullCategory::Carrier, HullCategory::Freighter,
		};
		return s;
	}

	cs::Ref<FacilityKind> LaneInterdictors::Kind() { return FacilityKinds::Of(FacilityKinds::FieldProjector); }

	bool LaneInterdictors::CanCarry(ShipClass cls)
	{
		const auto& designs = CombatSpec::DesignsOf(cls);
		return designs.Count() > 0 && designs[0]->DryMassTonnes >= MinHullTonnes - 1e-9;
	}

	bool LaneInterdictors::CanFit(const cs::Ref<Ship>& ship)
	{
		if ((ship == nullptr || ship->Spec == nullptr) || Kind() == nullptr) return false;
		auto l = Outfitting::LoadoutOf(ship);
		return l != nullptr && l->CanAdd(Kind()) == OutfitOutcome::Ok;
	}

	bool LaneInterdictors::Fit(const cs::Ref<Ship>& ship) { return ship != nullptr && Kind() != nullptr && Outfitting::Install(ship, Kind()); }

	int LaneInterdictors::Count(const cs::Ref<Ship>& ship) { return FleetCoordination::Count(ship, FacilityKinds::FieldProjector); }

	bool LaneInterdictors::Carries(const cs::Ref<Ship>& ship) { return Count(ship) > 0; }

	const cs::Ref<RangeSetting>& LaneInterdictors::RechargeDays()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.interdictorRechargeDays", SettingGroups::Ai,
			"Interdictor recharge", 1, 0.1, 5, 0.1,
			"How long (days) a lane interdictor's field projector takes to charge again after it has pulsed and thrown ships out of a hyperlane.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.0"), " days"); }), "interdictor interdiction pulse lane hyperlane recharge pirate ambush projector");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneInterdictors::PicketChance()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.lanePicketChance", SettingGroups::Ai,
			"Navy lane pickets", 0.35, 0, 1, 0.05,
			cs::String("The chance a navy group with a field projector fitted to one of its hulls, with nothing more pressing to do, lies in ") +
			"wait on the most troubled hyperlane out of its system, to pull the raiders and enemies riding it out into a fight.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }), "interdictor lane picket navy patrol ambush hyperlane projector");
		return s;
	}

	cs::Ref<Ship> LaneInterdictors::EmitterOf(const cs::Ref<Ship>& leader, const cs::List<cs::Ref<Ship>>& ships)
	{
		if (leader == nullptr || leader->Destroyed) return nullptr;
		if (Working(leader)) return leader;
		// (C#'s `ships != null` cannot fail: the list is passed by reference.)
		for (const auto& s : ships)
			if (s != nullptr && s->Leader == leader && Working(s)) return s;
		return nullptr;
	}

	bool LaneInterdictors::Fielded(const cs::Ref<Ship>& leader, const cs::List<cs::Ref<Ship>>& ships) { return EmitterOf(leader, ships) != nullptr; }

	bool LaneInterdictors::Working(const cs::Ref<Ship>& s) { return !s->Destroyed && !s->Derelict && s->Spec != nullptr && Carries(s); }
}
