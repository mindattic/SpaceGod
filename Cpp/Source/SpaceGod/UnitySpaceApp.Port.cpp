// US-T266: docking at a port and the port screen's tabs (declared in UnitySpaceApp.Main.inl). The screen itself is the
// HUD's (UI/Hud.Port.cpp). Docking is the player's own view of being alongside: nothing in the simulation reads it, so it
// is not saved and goes through no co-op frame; what is bought docked goes through the panels' own co-op entries.
// US-T435: docking takes one of the port's station's berths (Infrastructure::RequestBerth), the same capacity the traffic
// calling there fills; with every berth taken the hull holds alongside in line and is cleared in as one frees.

#include "UnitySpaceApp.h"
#include "Input/InputDevice.h" // US-T335

#include "World.h"
#include "BodyCatalog.h"
#include "Combat/Outfitting.h"
#include "Infrastructure/Infrastructure.h"
#include "OrbitalElements.h"
#include "ScaleModel.h"
#include "SimClock.h"
#include "Views/StructureViews.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Law/Law.h"
#include "Lore/LoreBooks.h"
#include "Systems/Galaxy.h" // StarSystem::Name (US-T261)

namespace SpaceGod
{
	using namespace Core;

	cs::Ref<Settlement> UnitySpaceApp::PortAlongside()
	{
		if (!IsPiloting() || World()->PilotedShip == nullptr) return nullptr;
		cs::Ref<Settlement> port = nullptr;
		for (const auto& s : Economy()->Settlements)
		{
			if (s->Fallen || !s->Discovered() || !NearPort(s)) continue;
			if (Outfitting::IsYard(s)) return s;
			if (port == nullptr) port = s;
		}
		return port;
	}

	void UnitySpaceApp::DockKey()
	{
		if (DockedAt != nullptr)
		{
			ShowPort = !ShowPort;
			if (ShowPort) OpenPortTab(PortTabOpen);
			return;
		}
		if (PendingDock != nullptr && PendingDockStation != nullptr && World()->PilotedShip != nullptr)
		{
			ShowToast(cs::Cat("Holding for a berth at ", PendingDockStation->Name, ": ", PendingDockStation->HoldingPlace(World()->PilotedShip) + 1,
				" in line for its ", PendingDockStation->BerthCount(), " berths"));
			return;
		}
		if (!IsPiloting())
		{
			ShowToast("Take a helm (P) and bring the hull alongside a port to dock");
			return;
		}
		auto port = PortAlongside();
		if (port == nullptr)
		{
			ShowToast("No port alongside: close on a settled world's port to ask for docking clearance");
			return;
		}
		Dock(port);
	}

	bool UnitySpaceApp::Dock(const cs::Ref<Settlement>& port)
	{
		auto ship = World()->PilotedShip;
		if (port == nullptr || ship == nullptr) return false;
		auto law = World()->Law;
		auto record = law != nullptr ? law->PlayerRecord() : nullptr;
		cs::String standing = record != nullptr && port->Owner != nullptr && !port->Owner->IsPirate
			? cs::Cat(" (your standing ", record->StandingWith(port->Owner) > 0 ? "+" : "", cs::Fmt(record->StandingWith(port->Owner), "0"), ")")
			: cs::String();
		if (!Economy()->CanTrade(ship, port))
		{
			ShowToast(Economy()->LawRefuses(ship, port)
				? cs::Cat("DOCKING REFUSED: ", port->Owner->Name, " will not clear you into ", port->Name, standing)
				: cs::Cat("DOCKING REFUSED: ", port->Name, " is closed to ", ship->Faction->Name));
			return false;
		}
		// US-T435: a berth at the port's station (its habitat, gateway, yard, depot or works), if it has one in orbit.
		auto infra = World()->Infrastructure;
		auto station = infra != nullptr ? infra->BerthingStationOf(port) : nullptr;
		cs::String berthNote;
		if (station != nullptr)
		{
			int berth = infra->RequestBerth(station, ship);
			if (berth < 0)
			{
				PendingDock = port;
				PendingDockStation = station;
				ShowToast(cs::Cat("DOCKING HELD: all ", station->BerthCount(), " berths at ", station->Name, " are taken; you are ",
					station->HoldingPlace(ship) + 1, " in line. Hold alongside and you will be cleared in as one frees"));
				return false;
			}
			DockedStation = station;
			DockedBerth = berth;
			DockedShip = ship;
			berthNote = cs::Cat(", berth ", berth + 1, " of ", station->BerthCount(), " at ", station->Name);
		}
		PendingDock = nullptr;
		PendingDockStation = nullptr;
		DockedAt = port;
		ShowPort = true;
		OpenPortTab(PortTabOpen);
		MoorAtBerth(true);
		ShowToast(cs::Cat("DOCKED at ", port->Name, berthNote, ": cleared by ", port->Owner->Name, standing, "   (Shift+M closes the port screen)"));
		// US-T261: cleared in, the port's records are open to read (when Prose wrote them).
		if (ActiveSystem != nullptr) RecoverLog(LoreBooks::StationRecords(ActiveSystem->Name, port->Name));
		return true;
	}

	void UnitySpaceApp::Undock(const cs::String& why)
	{
		if (DockedAt == nullptr) return;
		if (bool* panel = PortTabPanel(PortTabOpen)) *panel = false;
		DockedAt = nullptr;
		ShowPort = false;
		// US-T435: the berth is free for the next ship in line.
		if (DockedShip != nullptr && World()->Infrastructure != nullptr) World()->Infrastructure->ReleaseBerth(DockedShip);
		DockedStation = nullptr;
		DockedBerth = -1;
		DockedShip = nullptr;
		if (!why.empty()) ShowToast(why);
	}

	bool* UnitySpaceApp::PortTabPanel(PortTab tab)
	{
		switch (tab)
		{
			case PortTab::Market: return &ShowMarket;
			case PortTab::Outfitting: return &ShowOutfit;
			case PortTab::Contracts: return &ShowContracts;
			case PortTab::Bounties: return &ShowBounties;
			case PortTab::Fleet: return &ShowFleet;
			case PortTab::Trade: return &ShowTrade;
			case PortTab::News: return &ShowNews;
			default: return nullptr;
		}
	}

	void UnitySpaceApp::OpenPortTab(PortTab tab)
	{
		if (tab != PortTabOpen)
			if (bool* was = PortTabPanel(PortTabOpen)) *was = false;
		PortTabOpen = tab;
		// The boards and the market show the port in view: the docked port's world, selected.
		if (DockedAt != nullptr && (tab == PortTab::Market || tab == PortTab::Contracts || tab == PortTab::Bounties || tab == PortTab::Fleet))
			Select(Target::OfBody(DockedAt->Body));
		if (bool* panel = PortTabPanel(tab)) *panel = true;
	}

	bool UnitySpaceApp::UpdatePortKeys(bool keysFree)
	{
		if (!keysFree || !ShowPort || DockedAt == nullptr) return false;
		// A full screen over the port has the keys (its search fields take digits).
		if (ShowSociety || ShowEncyclopedia || ShowHelp || ShowSaves || ShowGalaxyMap) return false;
		using Key = ::SpaceGod::Input::Key;
		const ::SpaceGod::Input::InputDevice& kb = ::SpaceGod::Input::InputDevice::current();
		auto held = [&](Key a, Key b) { return kb[a].isPressed || kb[b].isPressed; };
		// Shift+digit is a helm ability (US-T332), Ctrl and Alt digits are the player's own bindings.
		if (held(Key::LeftShift, Key::RightShift) || held(Key::LeftCtrl, Key::RightCtrl) || held(Key::LeftAlt, Key::RightAlt))
			return false;
		static constexpr Key digits[] = { Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5,
			Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9, Key::Digit0 };
		// The tabs as the strip shows them (Outfitting only at a yard), numbered from the left.
		bool yard = Outfitting::IsYard(DockedAt);
		int n = 0;
		for (int t = 0; t <= static_cast<int>(PortTab::News); t++)
		{
			auto tab = static_cast<PortTab>(t);
			if (tab == PortTab::Outfitting && !yard) continue;
			if (n < 10 && kb[digits[n]].wasPressedThisFrame)
			{
				OpenPortTab(tab);
				return true;
			}
			n++;
		}
		return false;
	}

	void UnitySpaceApp::UpdateDocking()
	{
		if (PendingDock != nullptr)
		{
			// US-T435: holding in line for a berth: cleared in as one frees, or out of the line on flying off.
			auto ship = World()->PilotedShip;
			auto infra = World()->Infrastructure;
			int berth = -1;
			if (!IsPiloting() || ship == nullptr || infra == nullptr || PendingDock->Fallen || !NearPort(PendingDock))
			{
				if (ship != nullptr && infra != nullptr) infra->ReleaseBerth(ship);
				ShowToast(cs::Cat("Left the line for a berth at ", PendingDockStation != nullptr ? PendingDockStation->Name : PendingDock->Name));
				PendingDock = nullptr;
				PendingDockStation = nullptr;
			}
			else if (infra->BerthedAt(ship, berth) != nullptr)
			{
				auto port = PendingDock;
				PendingDock = nullptr;
				Dock(port);
			}
			return;
		}
		if (DockedAt == nullptr) return;
		auto ship = World()->PilotedShip;
		if (!IsPiloting() || ship == nullptr) Undock(cs::Cat("Left the helm: undocked from ", DockedAt->Name));
		else if (DockedAt->Fallen || !NearPort(DockedAt)) Undock(cs::Cat("Undocked from ", DockedAt->Name));
		else if (!Economy()->CanTrade(ship, DockedAt)) Undock(cs::Cat(DockedAt->Name, " has revoked your docking clearance"));
		else MoorAtBerth(false);
	}

	void UnitySpaceApp::MoorAtBerth(bool seed)
	{
		auto ship = World()->PilotedShip;
		auto station = DockedStation;
		if (station == nullptr || DockedBerth < 0 || ship == nullptr || ship != DockedShip || Structures == nullptr) return;
		// Any stick input casts off (the berth's clamps let go and the hull flies on from where it lay).
		if (!seed && (Input.Move.sqrMagnitude() > 0.0025f || Input.Boost))
		{
			Undock(cs::Cat("Cast off from berth ", DockedBerth + 1, " at ", station->Name));
			return;
		}
		if (station->AtLagrange() || station->Orbit == nullptr || station->Body != pilotFrameBody) return;
		Core::DVec3 scene;
		Quaternion rotation;
		if (!Structures->BerthPose(station, DockedBerth, ShipDisplayLength(ship), scene, rotation)) return;
		double jd = Clock->Jd;
		Core::DVec3 km;
		if (!SceneToFrameKm(station->Body, scene, station->Orbit->OffsetAt(jd), km)) return;
		// Moving with the station: its orbital velocity (km/s), from its orbit a minute either side.
		const double dt = 60.0 / 86400.0;
		auto velocity = (station->Orbit->OffsetAt(jd + dt) - station->Orbit->OffsetAt(jd - dt)) / (2 * 60.0);
		if (seed)
		{
			auto nose = rotation * Vector3::forward();
			SeedPilot(km, velocity, Core::DVec3(nose.x, nose.y, nose.z).SwapYZ().Normalized());
		}
		else
		{
			SetpilotOffsetKm(km);
			SetpilotVelKmS(velocity);
		}
	}

	bool UnitySpaceApp::SceneToFrameKm(int body, Core::DVec3 scene, Core::DVec3 guessKm, Core::DVec3& km)
	{
		auto def = (*Catalog())[body];
		auto target = scene - BodyScene(body);
		auto at = [&](Core::DVec3 x) { return Scale()->LocalOffset(def, x) - target; };
		Core::DVec3 x = guessKm;
		for (int it = 0; it < 8; it++)
		{
			auto f = at(x);
			if (f.Length() <= 1e-9 * cs::Math::Max(1.0, target.Length())) break;
			double h = cs::Math::Max(1e-3, x.Length() * 1e-7);
			auto cx = (at(x + Core::DVec3(h, 0, 0)) - f) / h;
			auto cy = (at(x + Core::DVec3(0, h, 0)) - f) / h;
			auto cz = (at(x + Core::DVec3(0, 0, h)) - f) / h;
			// Solve [cx cy cz] d = f by Cramer's rule.
			double det = Core::DVec3::Dot(cx, Core::DVec3::Cross(cy, cz));
			if (cs::Math::Abs(det) < 1e-30) return false;
			Core::DVec3 d(Core::DVec3::Dot(f, Core::DVec3::Cross(cy, cz)) / det,
				Core::DVec3::Dot(cx, Core::DVec3::Cross(f, cz)) / det,
				Core::DVec3::Dot(cx, Core::DVec3::Cross(cy, f)) / det);
			x = x - d;
		}
		if (cs::Double::IsNaN(x.X) || cs::Double::IsNaN(x.Y) || cs::Double::IsNaN(x.Z)) return false;
		km = x;
		return true;
	}
}
