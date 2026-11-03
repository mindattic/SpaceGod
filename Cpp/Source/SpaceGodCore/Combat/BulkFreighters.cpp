// Port of Assets/UnitySpace/Core/Combat/BulkFreighters.cs (see docs/MIGRATION.md).

#include "Combat/BulkFreighters.h"

#include "Combat/Facilities.h"
#include "Combat/Outfitting.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	cs::HashSet<ShipClass>& BulkFreighters::trusses()
	{
		static cs::HashSet<ShipClass> s{ ShipClass::Freighter };
		return s;
	}

	void BulkFreighters::RegisterTruss(ShipClass cls, bool truss)
	{
		if (truss) trusses().Add(cls);
		else trusses().Remove(cls);
	}

	bool BulkFreighters::IsTruss(ShipClass cls) { return trusses().Contains(cls); }

	bool BulkFreighters::NeverLands(const cs::Ref<Ship>& s) { return s != nullptr && s->Spec != nullptr && IsTruss(s->Spec->Class); }

	const cs::Ref<RangeSetting>& BulkFreighters::LaneTollShare()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("economy.freighterLaneToll", SettingGroups::Economy,
			"Bulk freighter lane toll", 0.5, 0.1, 1, 0.05,
			"The share of a hyperlane's toll a bulk freighter pays against any other ship of its mass: freighters are built to ride "
			"the lanes rather than burn their own fuel, and the lanes' keepers price them as bulk traffic.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v * 100, "0") + "%"; }), "freighter truss lane toll hyperlane bulk cargo");
		return s;
	}

	double BulkFreighters::LaneTollShareOf(ShipClass cls) { return IsTruss(cls) ? LaneTollShare()->Value : 1; }

	cs::List<cs::Ref<FacilitySite>> BulkFreighters::StockLayout()
	{
		cs::List<cs::Ref<FacilitySite>> list
		{
			cs::New<FacilitySite>(FacilityKinds::Bridge, "Cockpit", 0.43),
			cs::New<FacilitySite>(FacilityKinds::Hold, "Pod gantry", 0.05),
		};
		for (int i = 0; i < StockPods; i++)
			list.Add(cs::New<FacilitySite>(FacilityKinds::CargoPod, cs::Cat("Cargo pod ", i + 1), PodZ(i, StockPods)));
		for (int i = 0; i < StockSpheres; i++)
			list.Add(cs::New<FacilitySite>(FacilityKinds::FuelSphere, cs::Cat("Fuel sphere ", i + 1), -0.3));
		list.Add(cs::New<FacilitySite>(FacilityKinds::Engineering, "Engine room", -0.42));
		// Its few hands' security post off the gantry (marines live in a habitat, U.SPC-T465).
		list.Add(cs::New<FacilitySite>(FacilityKinds::MarineHabitat, "Security post", 0.05, -0.05, 1, cs::Nullable<double>(4)));
		return list.ToArray();
	}

	double BulkFreighters::PodZ(int i, int ofPods)
	{
		int rows = cs::Math::Max(1, (ofPods + 1) / 2);
		constexpr double front = 0.3, back = -0.2;
		return rows == 1 ? (front + back) / 2 : front - (front - back) * (i / 2) / (rows - 1);
	}

	int BulkFreighters::Pods(const cs::Ref<Ship>& s) { return Count(s, FacilityKinds::CargoPod); }

	int BulkFreighters::Spheres(const cs::Ref<Ship>& s) { return Count(s, FacilityKinds::FuelSphere); }

	int BulkFreighters::Count(const cs::Ref<Ship>& s, const cs::String& kind)
	{
		if (s == nullptr || s->Spec == nullptr) return 0;
		if (s->Loadout != nullptr) return s->Loadout->CountFacility(kind);
		int n = 0;
		for (const auto& f : FacilityLayouts::For(s->Spec->Class))
			if (cs::Strings::EqualsOrdinalIgnoreCase(f->Kind, kind)) n++;
		return n;
	}

	int BulkFreighters::LoadedPods(const cs::Ref<Ship>& s)
	{
		int pods = Pods(s);
		if (pods == 0 || s->CapacityTonnes <= 0) return 0;
		double share = cs::Math::Max(0.0, cs::Math::Min(1.0, s->CarriedTonnes() / s->CapacityTonnes));
		return share <= 0 ? 0 : cs::Math::Max(1, cs::Math::Min(pods, cs::ToInt(cs::Math::Ceiling(share * pods - 1e-9))));
	}
}
