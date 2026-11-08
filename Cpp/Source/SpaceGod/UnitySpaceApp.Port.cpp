// US-T266: docking at a port and the port screen's tabs (declared in UnitySpaceApp.Main.inl). The screen itself is the
// HUD's (UI/Hud.Port.cpp). Docking is the player's own view of being alongside: nothing in the simulation reads it, so it
// is not saved and goes through no co-op frame; what is bought docked goes through the panels' own co-op entries.

#include "UnitySpaceApp.h"

#include "World.h"
#include "Combat/Outfitting.h"
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
		DockedAt = port;
		ShowPort = true;
		OpenPortTab(PortTabOpen);
		ShowToast(cs::Cat("DOCKED at ", port->Name, ": cleared by ", port->Owner->Name, standing, "   (Shift+M closes the port screen)"));
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

	void UnitySpaceApp::UpdateDocking()
	{
		if (DockedAt == nullptr) return;
		auto ship = World()->PilotedShip;
		if (!IsPiloting() || ship == nullptr) Undock(cs::Cat("Left the helm: undocked from ", DockedAt->Name));
		else if (DockedAt->Fallen || !NearPort(DockedAt)) Undock(cs::Cat("Undocked from ", DockedAt->Name));
		else if (!Economy()->CanTrade(ship, DockedAt)) Undock(cs::Cat(DockedAt->Name, " has revoked your docking clearance"));
	}
}
