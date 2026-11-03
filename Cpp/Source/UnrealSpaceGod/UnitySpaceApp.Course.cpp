// Port of Unity.SpaceGod Runtime/UnitySpaceApp.Course.cs (US-T29): a plotted course over the gates, flown by the pilot
// or handed to the crew. Members are declared in UnitySpaceApp.Course.inl. A C# null string is the empty string here.

#include "UnitySpaceApp.h"

#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"
#include "Systems/TravelCourse.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	void UnitySpaceApp::PlotCourse(const cs::Ref<Core::StarSystem>& to)
	{
		if (to != nullptr && CoopSends("plot", to->Name)) return; // US-T44 (US-T43): the course is the shared helm's: into the frames
		auto ship = Piloted.Ship;
		if (ship == nullptr || to == nullptr) return;
		if (to == ActiveSystem) { Course = nullptr; return; }
		auto course = Core::TravelCourse::Plan(Galaxy->Network, ActiveSystem, to, ship->Faction, Clock->Jd);
		if (course == nullptr)
		{
			ShowToast(cs::Cat("No open way through the gates to ", to->Name));
			return;
		}
		Course = course;
		courseShip = ship;
		CourseByCrew = false;
		courseFollow = false;
		ShowToast(cs::Cat("Course plotted to ", cs::Strings::ToUpperInvariant(to->Name), ": ", course->Legs(), (course->Legs() == 1 ? " gate" : " gates"),
			", first ", cs::Strings::ToUpperInvariant(course->Next()->Name), "  (the autopilot button hands it to the crew)"));
	}

	bool UnitySpaceApp::EngageCourseAutopilot()
	{
		if (CoopSends("autopilot")) return true; // US-T44 (US-T43): into the frames (it hands the shared helm to the crew)
		if (Course == nullptr || Course->Done() || !IsPiloting() || Piloted.Ship != courseShip || CourseByCrew) return false;
		auto ship = courseShip;
		auto to = Course->Destination;
		ExitPiloting();
		if (!StartCrewCourse(ship, to, true))
		{
			ShowToast("The crew cannot take the course just now");
			Course = nullptr;
			return false;
		}
		return true;
	}

	bool UnitySpaceApp::StartCrewCourse(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::StarSystem>& to, bool followIn)
	{
		if (CoopSends("crew", cs::Cat(ship->Id, " ", followIn ? 1 : 0, " ", to->Name))) return true; // US-T44 (US-T43): into the frames (it sends the ship)
		bool follow = followIn && CoopActing(); // US-T44 (US-T43): another player's crew course: their view follows it, not this one
		auto course = Core::TravelCourse::Plan(Galaxy->Network, ActiveSystem, to, ship->Faction, Clock->Jd);
		if (course == nullptr || Galaxy->Traffic == nullptr || Galaxy->Traffic->SendPlotted(ship, ActiveSystem, to, Clock->Jd) == nullptr) return false;
		Course = course;
		courseShip = ship;
		CourseByCrew = true;
		courseFollow = follow;
		if (follow) FollowCourseShip(ship);
		ShowToast(cs::Cat("The ", ship->Name, " sets out for ", cs::Strings::ToUpperInvariant(to->Name), " by ", course->Legs(), (course->Legs() == 1 ? " gate" : " gates"),
			", first the ", cs::Strings::ToUpperInvariant(course->Next()->Name)));
		return true;
	}

	bool UnitySpaceApp::TrySendFollowedShip(const cs::Ref<Core::StarSystem>& to)
	{
		if (CoopExecuting()) return false; // US-T44 (US-T43): the asker's camera chose this (its "crew" entry), not this peer's
		if (SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Focus || !SpaceCamera->FocusTarget.IsShip()) return false;
		auto ship = SpaceCamera->FocusTarget.Ship;
		// A house ship is the player's own money: sent through the gates by a stray click on the map it could be lost
		// and written off (U.SPC-T553), so the click only moves the view; the ship is sent from its helm or its orders.
		if (ship->Faction != nullptr && ship->Faction->IsHouse)
		{
			ShowToast(cs::Cat("The ", ship->Name, " is yours: give it orders, or take the helm, to send it. The view goes alone"));
			return false;
		}
		return StartCrewCourse(ship, to, true);
	}

	void UnitySpaceApp::FollowCourseShip(const cs::Ref<Core::Ship>& ship)
	{
		auto t = Target::OfShip(ship);
		Select(t);
		SpaceCamera->FocusOn(t, Cameras::SpaceCamera::DefaultFraming(t));
	}

	cs::Ref<Core::StarSystem> UnitySpaceApp::SystemHolding(const cs::Ref<Core::Ship>& ship)
	{
		for (const auto& s : Galaxy->Systems)
		{
			const auto& ships = s->World->Fleet->Ships();
			if (ship->Id >= 0 && ship->Id < ships.Count() && ships[ship->Id] == ship) return s;
		}
		return nullptr;
	}

	void UnitySpaceApp::EndCourse(const cs::String& toastIn)
	{
		cs::String toast = toastIn;
		Course = nullptr;
		courseShip = nullptr;
		CourseByCrew = false;
		courseFollow = false;
		if (!toast.empty()) ShowToast(toast);
	}

	void UnitySpaceApp::UpdateCourseShip()
	{
		if (!CourseByCrew || courseShip == nullptr) return;
		auto ship = courseShip;
		if (ship->Destroyed) { EndCourse(cs::Cat("The ", ship->Name, " was lost on the way to ", Course->Destination->Name)); return; }
		if (IsPiloting() && Piloted.Ship == ship) { EndCourse("Helm taken: the course is yours to fly"); return; }
		auto v = ship->Voyage;
		if (v != nullptr && v->Purpose == Core::VoyagePurpose::Plotted) Course->SyncTo(v->Leg);
		auto where = SystemHolding(ship);
		// Inside a gate: nothing to see until it comes out.
		if (where == nullptr) return;
		if (where != ActiveSystem && courseFollow && !Travel->Active && !CoopIsClient()) // US-T44 (US-T43): the host's view takes the party (its Activate goes out with its frame)
		{
			Activate(where);
			SetSystemVisible(true);
			FollowCourseShip(ship);
			ShowToast(cs::Cat("The ", ship->Name, " comes out in ", cs::Strings::ToUpperInvariant(where->Name),
				(Course->Next() != nullptr ? cs::Cat(", on to the ", cs::Strings::ToUpperInvariant(Course->Next()->Name)) : cs::String())));
		}
		else if (where == ActiveSystem && courseFollow && !Travel->Active &&
			(SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Focus || !SpaceCamera->FocusTarget.IsShip() || SpaceCamera->FocusTarget.Ship != ship))
			courseFollow = false;
		if (v == nullptr || v->Purpose != Core::VoyagePurpose::Plotted)
			EndCourse(where == Course->Destination ? cs::Cat("The ", ship->Name, " has arrived at ", cs::Strings::ToUpperInvariant(where->Name)) : cs::String("The course was called off"));
	}

	void UnitySpaceApp::AdvanceCourse(const cs::Ref<Core::JumpGate>& gate, const cs::Ref<Core::Ship>& ship)
	{
		if (Course == nullptr || CourseByCrew) return;
		bool more = Course->Passed(gate, Galaxy->Network, ship->Faction, Clock->Jd);
		if (!more)
		{
			Course = nullptr;
			return;
		}
		ShowToast(cs::Cat("Next gate: ", cs::Strings::ToUpperInvariant(Course->Next()->Name), ", leg ", (Course->Taken() + 1), " of ", Course->Legs()));
	}

	cs::String UnitySpaceApp::CourseLine()
	{
		if (Course == nullptr) return cs::String();
		if (CourseByCrew) return Course->Done() ? cs::String() : cs::Cat("AUTOPILOT  ", cs::Strings::ToUpperInvariant(courseShip->Name), "  ", cs::Strings::ToUpperInvariant(Course->Describe()));
		// Another hull's helm ends the course; a system the hull came to some other way (FTL) replans from there.
		if (!IsPiloting() || Piloted.Ship != courseShip) { Course = nullptr; return cs::String(); }
		if (Course->Done() || Course->Here() != ActiveSystem)
		{
			auto replanned = Course->Done() ? nullptr : Core::TravelCourse::Plan(Galaxy->Network, ActiveSystem, Course->Destination, courseShip->Faction, Clock->Jd);
			Course = replanned;
			if (Course == nullptr) return cs::String();
		}
		auto next = Course->Next();
		cs::String line = cs::Cat("COURSE  ", cs::Strings::ToUpperInvariant(Course->Describe()));
		if (next->Home == ActiveSystem)
		{
			auto at = Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm();
			line += cs::Cat("   ·   ", DistanceText((next->PositionKm - at).Length()), " TO GATE");
		}
		return line;
	}

	bool UnitySpaceApp::CourseAutopilotOffered()
	{
		return Course != nullptr && !CourseByCrew && !Course->Done() && IsPiloting() && Piloted.Ship == courseShip;
	}
}
