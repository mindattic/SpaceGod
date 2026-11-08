// US-T258: the lane route the system map plots at a helm (declarations in UnitySpaceApp.Lanes.inl; the map itself is
// UI/SystemMapScreen). A route is the lanes to ride from the planetary system the hull is at to the one its destination is
// in, and on along a gate's spur when it is a gate: riding them, each off-ramp's exit is picked for the next lane (the
// player's number keys still override it), and the course banner shows the next ramp. It is plotted and cleared through
// the co-op frames, so every peer rides the same exits; it writes nothing the simulation reads.

#include "UnitySpaceApp.h"

#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	int UnitySpaceApp::LaneSystemHere()
	{
		auto lanes = Fleet()->Lanes;
		if (lanes == nullptr || lanes->Lanes.Count() == 0) return -1;
		auto at = PlayerHelioKm();
		int best = -1;
		double bestKm = cs::Double::PositiveInfinity;
		for (const auto& lane : lanes->Lanes)
		{
			if (lane == nullptr) continue;
			for (int end : { lane->A, lane->B })
			{
				double km = (Ephemeris()->Helio(end) - at).Length();
				if (km < bestKm)
				{
					bestKm = km;
					best = end;
				}
			}
		}
		return best;
	}

	cs::Ref<cs::List<LaneHop>> UnitySpaceApp::LaneRouteTo(int toSystem, const cs::Ref<JumpGate>& gate)
	{
		auto lanes = Fleet()->Lanes;
		if (lanes == nullptr) return nullptr;
		int from = LaneSystemHere();
		if (from < 0) return nullptr;
		double jd = Clock->Jd;
		cs::Ref<Lane> spur;
		if (gate != nullptr)
		{
			for (const auto& s : lanes->GateSpurs)
				if (s != nullptr && s->Gate == gate) { spur = s; break; }
			if (spur == nullptr) return nullptr; // no lanes lead on to it (a frontier system, a Void Base)
			toSystem = spur->A;
		}
		if (toSystem < 0) return nullptr;
		auto route = cs::New<cs::List<LaneHop>>();
		if (from != toSystem)
		{
			auto ship = IsPiloting() ? Piloted.Ship : nullptr;
			auto found = ship != nullptr ? lanes->RouteFor(ship, from, toSystem, jd) : lanes->Route(from, toSystem, jd);
			if (found == nullptr) return nullptr;
			route->AddRange(*found); // a copy: the planner hands out its cached list
		}
		if (spur != nullptr) route->Add(LaneHop(spur, spur->A, spur->B));
		return route->Count() > 0 ? route : nullptr;
	}

	void UnitySpaceApp::PlotLaneRoute(int toSystem, int gateIndex, const cs::String& name)
	{
		if (CoopSends("laneplot", cs::Cat(toSystem, " ", gateIndex, " ", name))) return; // the shared helm's: into the frames
		if (!IsPiloting())
		{
			ShowToast("No lane route: take a helm first");
			return;
		}
		const auto& gates = ActiveSystem->Gates();
		cs::Ref<JumpGate> gate = gateIndex >= 0 && gateIndex < gates.Count() ? gates[gateIndex] : nullptr;
		auto route = LaneRouteTo(toSystem, gate);
		if (route == nullptr)
		{
			ShowToast(cs::Cat("No lane route to ", name, ": cruise there instead (Shift+J, or CRUISE on the system map)"));
			return;
		}
		LaneRoute = route;
		LaneRouteName = name;
		laneRouteShip = Piloted.Ship;
		// Already riding one of its lanes: the exit is picked now.
		if (RidingLane())
		{
			int choice = LaneRouteChoice(Piloted.Ship->LaneLeg);
			if (choice >= 0) RideChoice = choice;
		}
		const auto& first = (*route)[0];
		int n = route->Count();
		ShowToast(cs::Cat("Lane route to ", cs::Strings::ToUpperInvariant(name), ": ", n, n == 1 ? " lane" : " lanes", ", in at the ",
			first.Lane->Name, first.Lane->IsGateSpur() ? " spur" : " lane", "'s ramp by ", Fleet()->Lanes->EndName(first.Lane, first.From),
			"  (the off-ramps' exits are picked for you)"));
	}

	void UnitySpaceApp::ClearLaneRoute()
	{
		if (CoopSends("laneclear")) return;
		LaneRoute = nullptr;
		LaneRouteName = cs::String();
		laneRouteShip = nullptr;
	}

	int UnitySpaceApp::LaneRouteChoice(const LaneHop& riding) const
	{
		if (LaneRoute == nullptr) return -1;
		int n = LaneRoute->Count();
		for (int k = 0; k < n; k++)
		{
			const auto& h = (*LaneRoute)[k];
			if (h.Lane != riding.Lane || h.To != riding.To) continue;
			if (k + 1 >= n) return -1; // the last lane: come off at its end
			const auto& next = (*LaneRoute)[k + 1];
			for (int j = 0; j < RideOnward.Count(); j++)
				if (RideOnward[j].Lane == next.Lane && RideOnward[j].To == next.To) return j;
			return -1;
		}
		return -1;
	}

	void UnitySpaceApp::LaneRouteRampReached(const LaneHop& hop)
	{
		if (LaneRoute == nullptr || LaneRoute->Count() == 0) return;
		const auto& last = (*LaneRoute)[LaneRoute->Count() - 1];
		if (last.Lane != hop.Lane || last.To != hop.To) return;
		cs::String name = LaneRouteName;
		// Every peer reaches the ramp in the same shared step, so each lets the route go alike (nothing to send).
		LaneRoute = nullptr;
		LaneRouteName = cs::String();
		laneRouteShip = nullptr;
		// A spur's far end already says the gate is in range to jump; anywhere else, the last leg is the cruise drive's.
		if (!hop.Lane->IsGateSpur())
			ShowToast(cs::Cat("Lane route flown: off at ", Fleet()->Lanes->EndName(hop.Lane, hop.To), ", ", cs::Strings::ToUpperInvariant(name),
				" a cruise away (CRUISE on the system map, or Shift+J with it ahead)"));
	}

	cs::String UnitySpaceApp::LaneRouteLine()
	{
		if (LaneRoute == nullptr || LaneRoute->Count() == 0) return cs::String();
		// Another hull's helm, or none, ends the route.
		if (!IsPiloting() || Piloted.Ship != laneRouteShip)
		{
			LaneRoute = nullptr;
			LaneRouteName = cs::String();
			laneRouteShip = nullptr;
			return cs::String();
		}
		auto lanes = Fleet()->Lanes;
		if (lanes == nullptr) return cs::String();
		int n = LaneRoute->Count();
		bool riding = RidingLane();
		int at = 0; // the hop being ridden, or flown to next
		if (riding)
		{
			const auto& leg = Piloted.Ship->LaneLeg;
			for (int k = 0; k < n; k++)
				if ((*LaneRoute)[k].Lane == leg.Lane && (*LaneRoute)[k].To == leg.To) { at = k; break; }
		}
		else
		{
			int here = LaneSystemHere();
			for (int k = 0; k < n; k++)
				if ((*LaneRoute)[k].From == here) { at = k; break; }
		}
		const auto& hop = (*LaneRoute)[at];
		int left = n - at;
		cs::String line = cs::Cat("LANE ROUTE  ", cs::Strings::ToUpperInvariant(LaneRouteName), "   ·   ", left, left == 1 ? " LANE" : " LANES");
		cs::String what = hop.Lane->IsGateSpur() ? cs::String(" SPUR") : cs::String(" LANE");
		if (riding)
		{
			if (at + 1 < n)
			{
				const auto& next = (*LaneRoute)[at + 1];
				line += cs::Cat("   ·   ON ALONG THE ", cs::Strings::ToUpperInvariant(next.Lane->Name), next.Lane->IsGateSpur() ? " SPUR" : " LANE");
			}
			else line += cs::Cat("   ·   OFF AT ", cs::Strings::ToUpperInvariant(lanes->EndName(hop.Lane, hop.To)));
		}
		else
		{
			auto ramp = lanes->RampHelio(hop.Lane, hop.From, Clock->Jd);
			line += cs::Cat("   ·   ", DistanceText((ramp - PlayerHelioKm()).Length()), " TO THE ", cs::Strings::ToUpperInvariant(hop.Lane->Name), what, " RAMP");
		}
		return line;
	}
}
