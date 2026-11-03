// Port of Assets/UnitySpace/Core/Systems/TravelCourse.cs (US-T17).

#include "Systems/TravelCourse.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/JumpGate.h"

namespace SpaceGod::Core
{
	cs::Ref<StarSystem> TravelCourse::Here() const
	{
		auto next = Next();
		return next ? next->Home : nullptr;
	}

	cs::Ref<StarSystem> TravelCourse::NextSystem() const
	{
		auto next = Next();
		return next && next->Partner ? next->Partner->Home : nullptr;
	}

	TravelCourse::TravelCourse(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& destination, const cs::Ref<cs::List<cs::Ref<JumpGate>>>& route)
		: gates(route), From(from), Destination(destination)
	{
	}

	cs::Ref<TravelCourse> TravelCourse::Plan(const cs::Ref<GateNetwork>& network, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Faction>& traveller, double jd)
	{
		if (network == nullptr || from == nullptr || to == nullptr || from == to) return nullptr;
		auto route = network->Route(from, to, traveller, jd);
		if (route == nullptr || route->Count() == 0) return nullptr;
		return cs::New<TravelCourse>(from, to, route);
	}

	bool TravelCourse::Passed(const cs::Ref<JumpGate>& gate, const cs::Ref<GateNetwork>& network, const cs::Ref<Faction>& traveller, double jd)
	{
		if (Done()) return false;
		if (gate == Next())
		{
			index++;
			return !Done();
		}
		auto now = gate && gate->Partner ? gate->Partner->Home : nullptr;
		auto replanned = now == nullptr || now == Destination ? nullptr : (network ? network->Route(now, Destination, traveller, jd) : nullptr);
		if (replanned == nullptr || replanned->Count() == 0)
		{
			index = gates->Count();
			return false;
		}
		gates->Clear();
		gates->AddRange(*replanned);
		index = 0;
		return true;
	}

	cs::String TravelCourse::Describe() const
	{
		cs::StringBuilder sb;
		sb.Append(gates->Count() > 0 ? (*gates)[0]->Home->Name : From->Name);
		for (int i = 0; i < gates->Count(); i++) sb.Append(" > ").Append((*gates)[i]->Partner->Home->Name);
		if (Next() != nullptr)
			sb.Append("   ·   LEG ").Append(index + 1).Append(" OF ").Append(gates->Count()).Append(": ").Append(Next()->Name);
		return sb.ToString();
	}
}
