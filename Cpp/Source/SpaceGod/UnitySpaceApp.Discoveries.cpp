// US-T259: the discovery log as the player meets it (declared in UnitySpaceApp.Discoveries.inl; the screen itself is
// UI/DiscoveriesScreen, the log Core/Systems/DiscoveryLog). Display only: the log reads the simulation, nothing reads the log.

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Comms/Pilots.h"
#include "Law/Law.h"
#include "Systems/DiscoveryLog.h"
#include "Systems/Galaxy.h"
#include "Systems/HelmFtl.h"
#include "UI/DiscoveriesScreen.h"

namespace SpaceGod
{
	using namespace Core;

	DiscoveryLog& UnitySpaceApp::Discoveries()
	{
		if (Galaxy->DiscoveryLog == nullptr) Galaxy->DiscoveryLog = cs::New<DiscoveryLog>();
		return *Galaxy->DiscoveryLog;
	}

	void UnitySpaceApp::UpdateDiscoveries()
	{
		if (Galaxy == nullptr || ActiveSystem == nullptr || Clock == nullptr) return;
		int scanned = Selected.IsBody() ? Selected.Body : -1;
		int nearBody = IsPiloting() ? PilotFrameBody() : -1;
		bool changed = ActiveSystem.get() != discoverySystem || scanned != discoveryScanned || nearBody != discoveryNear || Galaxy.get() != discoveryGalaxy;
		if (!changed && ++discoveryFrames < 120) return;
		discoveryFrames = 0;
		discoverySystem = ActiveSystem.get();
		discoveryScanned = scanned;
		discoveryNear = nearBody;
		bool first = Galaxy.get() != discoveryGalaxy;
		discoveryGalaxy = Galaxy.get();

		auto& log = Discoveries();
		int before = log.Entries.Count();
		DiscoveryLog::Place place;
		place.Here = ActiveSystem;
		place.NearBody = nearBody;
		place.ScannedBody = scanned;
		if (log.Sweep(*Galaxy, place, Clock->Jd) == 0 || first) return;

		// A notable find gets a toast (the last one, when a sweep finds several); a body scanned, a settlement on the map or a
		// pilot met says nothing more than the panel that showed it already did.
		for (int i = log.Entries.Count() - 1; i >= before; i--)
		{
			const auto& e = log.Entries[i];
			switch (e->Kind)
			{
				case DiscoveryKind::Star:
				case DiscoveryKind::Haven:
				case DiscoveryKind::Graveyard:
				case DiscoveryKind::Derelict:
				case DiscoveryKind::VoidSight:
				case DiscoveryKind::Sighting:
					ShowToast(cs::Cat("DISCOVERY LOGGED: ", e->Name, "   (Shift+O for the discoveries)"));
					return;
				default:
					break;
			}
		}
	}

	bool UnitySpaceApp::DiscoveryTarget(const DiscoveryEntry& e, Target& target)
	{
		if (ActiveSystem == nullptr || e.System != ActiveSystem->Name) return false;
		if (e.Kind == DiscoveryKind::Pilot)
		{
			auto world = World();
			if (world == nullptr || world->Law == nullptr || world->Law->Pilots == nullptr) return false;
			for (const auto& p : world->Law->Pilots->All)
				if (p != nullptr && p->Id == e.PilotId)
				{
					if (p->Ship == nullptr || p->Ship->Destroyed || !Fleet()->IsPresent(p->Ship)) return false;
					target = Target::OfShip(p->Ship);
					return true;
				}
			return false;
		}
		if (e.Kind == DiscoveryKind::Graveyard || e.Kind == DiscoveryKind::Derelict || e.Kind == DiscoveryKind::Sighting) return false;
		auto cat = Catalog();
		if (cat == nullptr || e.Body < 0 || e.Body >= cat->Count()) return false;
		if ((e.Kind == DiscoveryKind::Body || e.Kind == DiscoveryKind::VoidSight) && (*cat)[e.Body]->Name != e.Name) return false;
		target = Target::OfBody(e.Body);
		return true;
	}

	bool UnitySpaceApp::GoToDiscovery(const DiscoveryEntry& e, bool focus)
	{
		Target target = Target::None();
		if (!DiscoveryTarget(e, target)) return false;
		Select(target);
		if (focus) FocusSelected();
		return true;
	}

	cs::String UnitySpaceApp::DiscoveryCourseRefusal(const DiscoveryEntry& e)
	{
		if (e.System.empty()) return "Out between the stars: no place to plot a course to.";
		if (ActiveSystem != nullptr && e.System == ActiveSystem->Name) return "Here, in this system.";
		if (!IsPiloting()) return "Take a helm (P) to plot a course there.";
		if (Galaxy->Find(e.System) != nullptr) return "";
		for (const auto& s : Galaxy->NearbyStars)
			if (s.Name == e.System) return CanPlotFtl() ? cs::String() : cs::String("No gate goes there: only a hull with its own FTL drive can fly to it.");
		return "Not on the charts now.";
	}

	bool UnitySpaceApp::PlotDiscoveryCourse(const DiscoveryEntry& e)
	{
		cs::String why = DiscoveryCourseRefusal(e);
		if (!why.empty())
		{
			ShowToast(why);
			return false;
		}
		if (auto sys = Galaxy->Find(e.System))
		{
			PlotCourse(sys);
			return true;
		}
		for (const auto& s : Galaxy->NearbyStars)
			if (s.Name == e.System) return PlotFtlCourse(FtlTarget::OfStar(s));
		return false;
	}

	void UnitySpaceApp::DrawDiscoveries()
	{
		if (!discoveriesScreen) discoveriesScreen = cs::New<UI::DiscoveriesScreen>(this);
		if (discoveriesScreen->Draw("Esc or Shift+O to close")) ShowDiscoveries = false;
	}

	void UnitySpaceApp::DestroyDiscoveriesTextures()
	{
		if (discoveriesScreen) discoveriesScreen->DestroyOwnedTextures();
	}
}
