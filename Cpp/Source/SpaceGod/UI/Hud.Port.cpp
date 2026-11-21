// US-T266: the port screen (declared in UI/Hud.Boards.inl). Docked at a port (Shift+M alongside it at a helm), one strip of
// tabs gathers what the port offers: the market, outfitting at a yard, Repair & Rearm, the contracts and bounty boards, the
// fleet and property, the trade desk, the crew and the news. A tab for one of the existing panels opens that panel (its own
// key still opens it directly); Repair & Rearm and Crew are drawn here. Repairs go through the co-op frames as the boards'
// purchases do ("repair" / "repairall", carried out in Hud.Coop.cpp).

#include "UI/Hud.h"
#include "UI/UiMotion.h" // US-T255

#include "UnitySpaceApp.h"

#include "Ships.h"
#include "World.h"
#include "Combat/Boarding.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Outfitting.h"
#include "Combat/PortRepairs.h"
#include "Economy/Economy.h"
#include "Economy/Holdings.h"
#include "Economy/Settlement.h"
#include "Law/Law.h"
#include "Astro.h"
#include "SimClock.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Culture.h"
#include "Systems/Civilization/Narrator.h"
#include "Systems/Society.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		using Tab = UnitySpaceApp::PortTab;

		const char* TabName(Tab t)
		{
			switch (t)
			{
				case Tab::Market: return "Market";
				case Tab::Outfitting: return "Outfitting";
				case Tab::Repair: return "Repair & Rearm";
				case Tab::Contracts: return "Contracts";
				case Tab::Bounties: return "Bounties";
				case Tab::Fleet: return "Fleet & property";
				case Tab::Trade: return "Trade desk";
				case Tab::Crew: return "Crew";
				case Tab::Locals: return "Locals";
				case Tab::News: return "News";
			}
			return "";
		}

		const char* TabKey(Tab t)
		{
			switch (t)
			{
				case Tab::Market: return "M";
				case Tab::Outfitting: return "F2";
				case Tab::Contracts: return "F7";
				case Tab::Bounties: return "F4";
				case Tab::Fleet: return "F10";
				case Tab::Trade: return "\\";
				case Tab::News: return "X";
				default: return "";
			}
		}
	}

	double Hud::PortPriceFactor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port)
	{
		auto law = app->World()->Law;
		return law != nullptr ? law->PriceFactorFor(ship, port) : 1.0;
	}

	void Hud::DrawDockPrompt()
	{
		if (app->ShowPort || !app->IsPiloting()) return;
		if (app->DockedAt != nullptr)
		{
			DrawBanner(cs::Cat("DOCKED at ", app->DockedAt->Name, "   ·   Shift+M  port screen"));
			return;
		}
		auto port = app->PortAlongside();
		if (port != nullptr) DrawBanner(cs::Cat("Alongside ", port->Name, "   ·   Shift+M  request docking"));
	}

	void Hud::DrawPort()
	{
		UiMotion::Scope motion("hud.port"); // US-T255
		auto port = app->DockedAt;
		auto ship = app->World()->PilotedShip;
		if (port == nullptr || ship == nullptr)
		{
			app->ShowPort = false;
			return;
		}
		bool yard = Outfitting::IsYard(port);
		if (!yard && app->PortTabOpen == Tab::Outfitting) app->OpenPortTab(Tab::Repair);
		cs::List<Tab> tabs;
		for (int t = 0; t <= static_cast<int>(Tab::News); t++)
			if (static_cast<Tab>(t) != Tab::Outfitting || yard) tabs.Add(static_cast<Tab>(t));

		double factor = PortPriceFactor(ship, port);
		cs::List<RepairItem> quote;
		if (app->PortTabOpen == Tab::Repair) quote = PortRepairs::Quote(app->Economy(), ship, port, factor);
		constexpr float w = 820, rowH = 20;
		constexpr int maxRows = 12;
		float body = 24;
		if (app->PortTabOpen == Tab::Repair) body = 26 + static_cast<float>(cs::Math::Max(1, cs::Math::Min(quote.Count(), maxRows))) * rowH + (quote.Count() > maxRows ? rowH : 0);
		else if (app->PortTabOpen == Tab::Crew) body = 5 * rowH;
		else if (app->PortTabOpen == Tab::Locals) body = 11 * rowH; // US-T313
		float h = 84 + body;
		auto r = Place(w, h); // US-T440: the port's drawer (floating, centred, its foot 104 up: clear of the status line and the bottom tabs)

		auto law = app->World()->Law;
		auto record = law != nullptr ? law->PlayerRecord() : nullptr;
		cs::String standing = record != nullptr && port->Owner != nullptr && !port->Owner->IsPirate ? StandingText(record, port->Owner) : cs::String();
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 160, 22), cs::Cat("PORT  ·  ", port->Name, "  ·  ", port->Owner->Name, yard ? "  ·  shipyard" : ""), title);
		if (GUI::Button(Rect(r.xMax() - 140, r.y + 6, 100, 22), "Undock", button)) app->Undock(cs::Cat("Undocked from ", port->Name));
		if (GUI::Button(Rect(r.xMax() - 34, r.y + 6, 24, 22), "X", button)) app->ShowPort = false;
		if (app->DockedAt == nullptr) return; // undocked by the button above

		// The strip of tabs.
		float tx = r.x + 8, tw = (w - 16) / tabs.Count();
		for (auto t : tabs)
		{
			if (GUI::Button(Rect(tx, r.y + 32, tw - 4, 22), TabName(t), app->PortTabOpen == t ? buttonActive : button)) app->OpenPortTab(t);
			tx += tw;
		}
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 12, r.y + 58, w - 24, 18), cs::Cat("Credits ", Credits(ship->Faction->Treasury), standing,
			"   ·   1-9, 0 pick a tab   ·   Shift+M closes, Esc too; flying off undocks"), small); // US-T335
		float y = r.y + 80;
		switch (app->PortTabOpen)
		{
			case Tab::Repair: DrawPortRepairs(ship, port, factor, quote, Rect(r.x, y, w, body), rowH, maxRows); break;
			case Tab::Crew: DrawPortCrew(ship, Rect(r.x, y, w, body), rowH); break;
			case Tab::Locals: DrawPortLocals(port, Rect(r.x, y, w, body), rowH); break;
			default:
			{
				cs::String key = TabKey(app->PortTabOpen);
				GUI::Label(Rect(r.x + 12, y, w - 24, 18), cs::Cat(TabName(app->PortTabOpen), " is open", key.empty() ? cs::String() : cs::Cat(" (", key, " opens it directly, anywhere)"),
					".   Pick another tab to change, or Repair & Rearm to mend the hull."), small);
				break;
			}
		}
		GUI::contentColor = old;
	}

	void Hud::DrawPortRepairs(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, double factor, const cs::List<RepairItem>& quote, Rect area,
		float rowH, int maxRows)
	{
		if (quote.Count() == 0)
		{
			GUI::contentColor = UiStyle::Good;
			GUI::Label(Rect(area.x + 12, area.y, area.width - 24, 18), cs::Cat("The ", ship->Name, " needs nothing: hull, hardpoints, shields, magazines and tanks are full."), small);
			return;
		}
		double total = 0;
		for (const auto& item : quote)
			if (item.Blocked.empty()) total += item.Cost * cs::Math::Min(1.0, item.InStock);
		GUI::contentColor = Dim;
		GUI::Label(Rect(area.x + 12, area.y, 330, 18), "Item", small);
		GUI::Label(Rect(area.x + 350, area.y, 240, 18), cs::Cat("Parts off ", port->Name, "'s shelves"), small);
		GUI::Label(Rect(area.x + 590, area.y, 100, 18), "Quote", small);
		GUI::contentColor = Color::white();
		if (GUI::Button(Rect(area.xMax() - 132, area.y - 2, 120, 20), cs::Cat("Repair all  ", Credits(total)), button) && !CoopSends("repairall", K(port)))
			RepairAllAt(port);
		float y = area.y + 24;
		for (int i = 0; i < quote.Count() && i < maxRows; i++)
		{
			const auto& item = quote[i];
			GUI::contentColor = item.Blocked.empty() ? Color::white() : Dim;
			GUI::Label(Rect(area.x + 12, y, 336, rowH), item.Label, small);
			cs::String parts;
			if (item.Kind == RepairItemKind::Propellant) parts = "reaction mass at the market's price";
			else
			{
				if (item.Alloys > 0) parts = cs::Cat("alloys ", Tonnes(item.Alloys));
				if (item.Electronics > 0) parts = cs::Cat(parts, parts.empty() ? "" : ", ", "electronics ", Tonnes(item.Electronics));
				if (item.Munitions > 0) parts = cs::Cat(parts, parts.empty() ? "" : ", ", "munitions ", Tonnes(item.Munitions));
				if (item.InStock < 1 - 1e-9) parts = cs::Cat(parts, "  (", Percent(item.InStock), " in stock)");
			}
			if (!item.Blocked.empty()) parts = item.Blocked;
			GUI::contentColor = item.InStock < 1 - 1e-9 ? UiStyle::Warning : item.Blocked.empty() ? Dim : Dim;
			GUI::Label(Rect(area.x + 350, y, 238, rowH), parts, small);
			GUI::contentColor = item.Cost > ship->Faction->Treasury ? UiStyle::Danger : Color::white();
			GUI::Label(Rect(area.x + 590, y, 110, rowH), Credits(item.Cost), small);
			GUI::contentColor = Color::white();
			bool was = GUI::enabled;
			GUI::enabled = was && item.Blocked.empty();
			if (GUI::Button(Rect(area.xMax() - 112, y - 1, 100, rowH - 2), item.Kind == RepairItemKind::Munitions ? "Rearm" : item.Kind == RepairItemKind::Propellant ? "Refuel" : "Repair", button) &&
				!CoopSends("repair", K(port), static_cast<int>(item.Kind), item.Index))
				RepairAt(port, item.Kind, item.Index);
			GUI::enabled = was;
			y += rowH;
		}
		if (quote.Count() > maxRows)
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(area.x + 12, y, area.width - 24, rowH), cs::Cat("... and ", quote.Count() - maxRows, " more (Repair all takes them too)"), small);
		}
		(void)factor;
	}

	void Hud::DrawPortCrew(const cs::Ref<Ship>& ship, Rect area, float rowH)
	{
		float y = area.y;
		GUI::contentColor = Color::white();
		if (ship->Combat != nullptr && ship->Combat->Interior != nullptr)
		{
			auto spec = ship->Combat->Spec;
			auto inside = ship->Combat->Interior;
			GUI::Label(Rect(area.x + 12, y, area.width - 24, rowH), cs::Cat("Crew aboard ", cs::Fmt(inside->Crew(), "0"), " of ", cs::Fmt(spec->Crew, "0"),
				"   ·   marines ", cs::Fmt(inside->Marines(), "0"), " of ", cs::Fmt(spec->Marines, "0")), small);
			y += rowH;
		}
		double brig = Outfitting::BrigCapacity(ship);
		if (brig > 0 || ship->Prisoners > 0)
		{
			GUI::Label(Rect(area.x + 12, y, area.width - 24, rowH), cs::Cat("Brig ", cs::Fmt(ship->Prisoners, "0"), " / ", cs::Fmt(brig, "0"),
				" prisoners", ship->Prisoners > 0 ? "   (hand them over on Outfitting)" : ""), small);
			y += rowH;
		}
		auto h = app->World()->Holdings;
		if (h != nullptr)
		{
			int ships = 0, captained = 0;
			for (const auto& o : h->Ships)
			{
				if (o->Lost) continue;
				ships++;
				if (o->Captain != nullptr) captained++;
			}
			GUI::Label(Rect(area.x + 12, y, area.width - 24, rowH), cs::Cat("Your fleet: ", ships, ships == 1 ? " ship, " : " ships, ", captained, " with a captain"), small);
			y += rowH;
		}
		GUI::contentColor = Dim;
		GUI::Label(Rect(area.x + 12, y, area.width - 160, rowH), "Captains are hired and dismissed for each ship on Fleet & property.", small);
		GUI::contentColor = Color::white();
		if (GUI::Button(Rect(area.xMax() - 150, y - 1, 138, rowH - 2), "Fleet & property", button)) app->OpenPortTab(Tab::Fleet);
	}

	void Hud::DrawPortLocals(const cs::Ref<Settlement>& port, Rect area, float rowH)
	{
		float y = area.y, x = area.x + 12, lw = area.width - 24;
		GUI::contentColor = Color::white();
		auto pct = [](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); };
		auto date = [](double jd) { return jd <= 0 ? cs::String("?") : Astro::ToDateTime(jd).ToString("yyyy-MM-dd"); };
		auto civ = app->Galaxy != nullptr ? Civilization::Find(*app->Galaxy) : nullptr;
		auto society = app->Galaxy != nullptr ? Society::Find(*app->Galaxy) : nullptr;
		double jd = app->Clock != nullptr ? app->Clock->Jd : 0;
		if (GUI::Button(Rect(area.xMax() - 150, y - 1, 138, rowH - 2), "Society screen", button)) app->OpenSocietyAt(CivPick::World(port->Name));
		if (civ == nullptr)
		{
			GUI::Label(Rect(x, y, lw - 150, rowH), cs::Cat("The people of ", port->Name, " have not woken yet: let a day pass and look again."), small);
			return;
		}

		cs::Ref<Civics> c;
		for (const auto& w : civ->Worlds)
			if (w != nullptr && w->Where != nullptr && w->Where->Name == port->Name) { c = w; break; }
		if (c != nullptr)
		{
			GUI::Label(Rect(x, y, lw - 150, rowH), cs::Cat("Approval of ", port->Owner != nullptr ? port->Owner->Name : cs::String("its flag"), " ", c->Approval >= 0 ? "+" : "",
				cs::Fmt(c->Approval, "0.00"), "   ·   unrest ", pct(port->Unrest), "   ·   crime ", pct(c->Crime), "   ·   sick ", pct(c->Infected),
				"   ·   war weariness ", pct(c->Weariness)), small);
			y += rowH;
			GUI::Label(Rect(x, y, lw, rowH), cs::Cat("Fashion: ", Culture::TrendName(c->Trend),
				c->Faith != nullptr ? cs::Cat("   ·   faith: ", Narrator::OrgName(*c->Faith), " ", pct(c->FaithShare)) : cs::String(),
				!c->Landmark.empty() ? cs::Cat("   ·   landmark: ", c->Landmark) : cs::String(),
				!c->Monument.empty() ? cs::Cat("   ·   monument: ", c->Monument) : cs::String()), small);
			y += rowH;
			if (c->Quarantined(jd))
			{
				GUI::contentColor = HostileColor;
				GUI::Label(Rect(x, y, lw, rowH), cs::Cat("QUARANTINED until ", date(c->QuarantineUntil), ": sickness is loose here"), small);
				GUI::contentColor = Color::white();
				y += rowH;
			}
		}
		else
		{
			GUI::Label(Rect(x, y, lw - 150, rowH), cs::Cat(port->Name, " keeps no civic record yet   ·   unrest ", pct(port->Unrest)), small);
			y += rowH;
		}

		if (society != nullptr)
		{
			auto houses = society->HousesOf(port);
			cs::String names;
			int shown = 0;
			for (const auto& h : houses)
			{
				if (h == nullptr || shown >= 4) continue;
				auto head = h->Head();
				names = cs::Cat(names, names.empty() ? "" : ",  ", "House ", h->Surname, head != nullptr ? cs::Cat(" (", head->Given, ")") : cs::String());
				shown++;
			}
			GUI::Label(Rect(x, y, lw, rowH), houses.Count() == 0 ? cs::String("No founding house lives here")
				: cs::Cat("Houses here: ", names, houses.Count() > shown ? cs::Cat(" and ", houses.Count() - shown, " more") : cs::String()), small);
			y += rowH;

			cs::String firms;
			int firmCount = 0;
			for (const auto& b : society->Businesses)
			{
				if (b == nullptr || !b->Trading()) continue;
				bool here = b->Home == port;
				for (const auto& br : b->Branches)
					if (br == port) here = true;
				if (!here) continue;
				if (firmCount++ < 4) firms = cs::Cat(firms, firms.empty() ? "" : ",  ", b->Name);
			}
			GUI::Label(Rect(x, y, lw, rowH), firmCount == 0 ? cs::String("No family firm keeps an office here")
				: cs::Cat("Firms with an office here: ", firms, firmCount > 4 ? cs::Cat(" and ", firmCount - 4, " more") : cs::String()), small);
			y += rowH;
		}

		GUI::contentColor = Dim;
		GUI::Label(Rect(x, y, lw, rowH), "Local news", small);
		y += rowH;
		GUI::contentColor = Color::white();
		int told = 0;
		for (int i = civ->Events.Count() - 1; i >= 0 && told < 5; i--)
		{
			const auto& e = civ->Events[i];
			if (e == nullptr || e->Where == nullptr || e->Where->Name != port->Name) continue;
			GUI::Label(Rect(x, y, lw, rowH), cs::Cat(date(e->Jd), "   ", Narrator::Render(*e)), small);
			y += rowH;
			told++;
		}
		if (told == 0) GUI::Label(Rect(x, y, lw, rowH), cs::Cat("Nothing of note has happened on ", port->Name, " lately."), small);
	}

	void Hud::RepairAt(const cs::Ref<Settlement>& port, RepairItemKind kind, int index)
	{
		auto ship = app->World()->PilotedShip;
		if (ship == nullptr || port == nullptr) return;
		double cost = 0;
		auto outcome = PortRepairs::Apply(app->Economy(), ship, port, PortPriceFactor(ship, port), kind, index, cost);
		app->ShowToast(RepairText(outcome, port, cost, kind == RepairItemKind::Munitions ? "Rearmed" : kind == RepairItemKind::Propellant ? "Refuelled" : "Repaired"));
	}

	void Hud::RepairAllAt(const cs::Ref<Settlement>& port)
	{
		auto ship = app->World()->PilotedShip;
		if (ship == nullptr || port == nullptr) return;
		int done = 0;
		double cost = 0;
		auto outcome = PortRepairs::RepairAll(app->Economy(), ship, port, PortPriceFactor(ship, port), done, cost);
		app->ShowToast(RepairText(outcome, port, cost, cs::Cat("Repair all: ", done, done == 1 ? " job" : " jobs", " done")));
	}

	cs::String Hud::RepairText(RepairOutcome outcome, const cs::Ref<Settlement>& port, double cost, const cs::String& done)
	{
		switch (outcome)
		{
			case RepairOutcome::Ok: return cs::Cat(done, " for ", Credits(cost));
			case RepairOutcome::Partial: return cs::Cat(done, " for ", Credits(cost), "  (as far as ", port->Name, "'s shelves and your credits went)");
			case RepairOutcome::NothingToDo: return "Nothing to mend there";
			case RepairOutcome::NotAYard: return cs::Cat(port->Name, " has no shipyard for structural work");
			case RepairOutcome::NoStock: return cs::Cat(port->Name, " has not the parts on its shelves");
			case RepairOutcome::CantAfford: return "Not enough credits";
			case RepairOutcome::CannotDock: return cs::Cat("Not welcome at ", port->Name);
		}
		return "";
	}
}
