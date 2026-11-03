// Port of Unity.SpaceGod's Runtime/UI/Hud.Trade.cs (see UI/Hud.Trade.inl for the declarations).

#include "UI/Hud.h"

#include <tuple>

#include "UnitySpaceApp.h"

#include "Astro.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Economy/CargoHold.h"
#include "Economy/Charters.h"
#include "Economy/Companies.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/MarketIntel.h"
#include "Economy/Settlement.h"
#include "Economy/Smuggling.h"
#include "Economy/TradeDesk.h"
#include "Economy/TradeInsurance.h"
#include "Settings/GameSetting.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	// ---------------------------------------------------------------- the trade desk (U.SPC-T426)

	cs::Ref<Settlement> Hud::TradePortInView()
	{
		cs::Ref<Settlement> port = nullptr;
		auto sel = app->Selected;
		if (sel.IsBody())
			for (const auto& s : app->Economy()->At(sel.Body))
				if (s->Discovered() && (port == nullptr || s->Population > port->Population)) port = s;
		if (port == nullptr && app->IsPiloting())
			for (const auto& s : app->Economy()->Settlements)
				if (s->Discovered() && app->NearPort(s)) return s;
		return port;
	}

	void Hud::DrawTradeDesk()
	{
		auto world = app->World();
		auto desk = world->Trade;
		if (desk == nullptr) return;
		auto h = world->Holdings;
		double jd = app->Clock->Jd;
		auto port = TradePortInView();
		auto flown = world->PilotedShip;
		auto purse = flown != nullptr ? flown->Faction : h->House;

		constexpr float w = 860, rowH = 20, listH = 300;
		bool backDoor = flown != nullptr && port != nullptr && app->NearPort(port) && Smuggling::HasBackDoor(port) && HasBackDoorCargo(desk, flown, port);
		float h0 = 112 + listH + (backDoor ? 26 + 3 * rowH : 0);
		auto r = Panel(Rect((Screen::width - w) * 0.5f, 100, w, h0));
		double worth = desk->NetWorth(purse != h->House ? purse : nullptr);
		cs::String next = cs::Double::IsNaN(desk->NextRankWorth()) ? cs::String() : cs::Cat("  ·  next rank at ", Credits(desk->NextRankWorth()));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 60, 22),
			cs::Cat("TRADE DESK  ·  ", cs::Strings::ToUpperInvariant(TradeDesk::RankName(desk->Rank)), "  ·  worth ", Credits(worth), next), title);
		if (GUI::Button(Rect(r.xMax() - 34, r.y + 6, 24, 22), "X", button)) app->ShowTrade = false;
		auto old = GUI::contentColor;

		// The pages.
		float x = r.x + 12, y = r.y + 32;
		// foreach (TradeTab t in Enum.GetValues(typeof(TradeTab))): every member, in value order.
		for (int ti = static_cast<int>(TradeTab::Runs); ti <= static_cast<int>(TradeTab::Warehouses); ti++)
		{
			auto t = static_cast<TradeTab>(ti);
			if (GUI::Button(Rect(x, y, 118, 22), ToString(t), t == tradeTab ? buttonActive : button)) tradeTab = t;
			x += 122;
		}
		GUI::contentColor = Dim;
		GUI::Label(Rect(x + 8, y + 2, r.xMax() - x - 20, 18),
			cs::Cat("account ", Credits(h->Balance()), purse != h->House ? cs::Cat("  ·  purse ", Credits(purse->Treasury)) : cs::String(), "   F12 to close"), small);

		y += 28;
		auto view = Rect(r.x + 8, y, w - 16, listH);
		switch (tradeTab)
		{
		case TradeTab::Runs: DrawTradeRuns(desk, port, purse, view, rowH, jd); break;
		case TradeTab::News: DrawTradeNews(desk, view, rowH, jd); break;
		case TradeTab::Charters: DrawTradeCharters(desk, view, rowH, jd); break;
		case TradeTab::Insurance: DrawTradeInsurance(desk, view, rowH, jd); break;
		case TradeTab::Warehouses: DrawTradeWarehouses(desk, view, rowH, jd); break;
		}

		// Career totals.
		GUI::contentColor = Dim;
		float by = view.yMax() + 6;
		GUI::Label(Rect(r.x + 12, by, w - 24, 18),
			cs::Cat("Charters served ", desk->ChartersServed, " (", Credits(desk->HireEarned), " hire)  ·  premiums ", Credits(desk->PremiumsPaid),
				", claims ", Credits(desk->ClaimsPaid), "  ·  back-door sales ", desk->Smuggled, " (", Credits(desk->SmugglingTakings), "), caught ", desk->TimesCaught,
				"  ·  price sheets bought ", desk->ReportsBought), small);

		if (backDoor) DrawBackDoor(desk, flown, port, Rect(r.x + 12, by + 24, w - 24, 3 * rowH + 22), rowH, jd);
		GUI::contentColor = old;
	}

	bool Hud::HasBackDoorCargo(const cs::Ref<TradeDesk>& desk, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port)
	{
		for (const auto& item : ship->PersonalHold()->Items())
			if (desk->Smuggling->ReasonFor(ship, port, item) != BackDoorReason::None) return true;
		return false;
	}

	void Hud::DrawTradeRuns(const cs::Ref<TradeDesk>& desk, const cs::Ref<Settlement>& port, const cs::Ref<Faction>& purse, Rect view, float rowH, double jd)
	{
		auto intel = desk->Intel;
		auto flown = app->World()->PilotedShip;
		cs::Ref<CargoHold> crates = flown != nullptr ? flown->PersonalHold() : nullptr;
		double hold = crates != nullptr ? 0 : Economy::CapacityOf(ShipClass::Freighter);
		auto leads = intel->Leads(hold, jd, 14, flown != nullptr ? flown->Faction : nullptr, crates);
		if (crates != nullptr) for (const auto& lead : leads) hold = cs::Math::Max(hold, lead->Tonnes);

		float x = view.x + 4, y = view.y;
		GUI::contentColor = Dim;
		GUI::Label(Rect(x, y, 520, rowH), cs::Cat("Prices held for ", intel->Count(), " markets  ·  ",
			crates != nullptr ? cs::Cat("runs for your free hold (up to ", Tonnes(hold), ")") : cs::Cat("runs for a hold of ", Tonnes(hold)), " (prices half-trusted after ",
			cs::Fmt(TradeRules::IntelHalfLife()->Value, "0"), " d)"), small);
		if (port != nullptr && intel->HasBroker(port))
		{
			GUI::contentColor = Color::white();
			if (GUI::Button(Rect(view.xMax() - 290, y, 286, rowH - 2), cs::Cat("Broker's price sheet at ", port->Name, "  ", Credits(intel->ReportFee())), button))
			{
				int n = intel->BuyReport(port, purse, jd);
				app->ShowToast(n >= 0 ? cs::Cat("The broker at ", port->Name, " sells you the prices of ", n, " markets") : cs::String("Not enough credits for the broker"));
			}
		}
		y += rowH + 2;
		if (leads.Count() == 0)
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(x, y, view.width - 8, rowH * 2), "No runs yet. Prices come from markets you have seen in port, your captains in port, and brokers.", small);
			return;
		}
		auto idle = IdleFreighter();
		for (const auto& lead : leads)
		{
			GUI::contentColor = lead->BackDoor ? Color(1.0f, 0.72f, 0.4f) : lead->Risk >= 0.35 ? Color(1.0f, 0.85f, 0.5f) : Color::white();
			cs::String line = cs::Cat(GoodInfo::Of(lead->Good)->Name, ":  ", lead->From->Name, " ", cs::Fmt(lead->BuyPrice, "N0"),
				"  ->  ", lead->To->Name, " ", cs::Fmt(lead->SellPrice, "N0"), "   ", Tonnes(lead->Tonnes),
				"  ·  ", Credits(lead->Profit), "  ·  ", MarketIntel::AgeText(lead->AgeDays),
				lead->Risk >= 0.05 ? cs::Cat("  ·  risk ", Percent(lead->Risk)) : cs::String(), lead->BackDoor ? "  ·  back door" : "");
			GUI::Label(Rect(x, y, view.width - 140, rowH), line, small);
			if (idle != nullptr && !lead->BackDoor && GUI::Button(Rect(view.xMax() - 132, y, 128, rowH - 2), cs::Cat("Send ", idle->Name()), button) &&
				!CoopSends("route", K(app->World()->Holdings->Ships, idle), lead->From->Body, lead->To->Body)) // US-T44 (US-T43): an entry in co-op
				app->ShowToast(app->World()->Holdings->Order(idle, StandingOrder::TradeRoute(cs::List<int>{ lead->From->Body, lead->To->Body }))
					? cs::Cat("The ", idle->Name(), " runs ", lead->From->Name, " - ", lead->To->Name)
					: cs::Cat("The ", idle->Name(), " cannot take that route"));
			y += rowH;
			if (y > view.yMax() - rowH) break;
		}
	}

	cs::Ref<OwnedShip> Hud::IdleFreighter()
	{
		for (const auto& o : app->World()->Holdings->Ships)
			if (!o->Lost && o->Captain != nullptr && !o->OnCharter() && o->Orders->Kind == StandingOrderKind::Hold && o->Ship != app->World()->PilotedShip &&
				Economy::CapacityOf(o->Ship->Spec->Class) > 0)
				return o;
		return nullptr;
	}

	void Hud::DrawTradeNews(const cs::Ref<TradeDesk>& desk, Rect view, float rowH, double jd)
	{
		auto tips = desk->Tips(jd, 14);
		float x = view.x + 4, y = view.y;
		if (tips.Count() == 0)
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(x, y, view.width - 8, rowH), "Quiet markets: no shocks, shortages or risks to what you own.", small);
			return;
		}
		for (const auto& t : tips)
		{
			GUI::contentColor = t->Weight >= 4 ? Color(1.0f, 0.72f, 0.4f) : Color::white();
			GUI::Label(Rect(x, y, view.width - 8, rowH), cs::Cat(Astro::ToDateTime(t->Jd).ToString("yyyy-MM-dd"), "  ", t->Text), small);
			y += rowH;
		}
	}

	void Hud::DrawTradeCharters(const cs::Ref<TradeDesk>& desk, Rect view, float rowH, double jd)
	{
		auto charters = desk->Charters;
		float x = view.x + 4, y = view.y;
		GUI::contentColor = Dim;
		GUI::Label(Rect(x, y, view.width - 8, rowH), cs::Cat("OFFERS  (a hull goes with its crew, trades for the charterer and comes back to its orders; recalling it early costs ",
			cs::Fmt(Charters::RecallPenaltyDays, "0"), " days' hire)"), small);
		y += rowH;
		if (charters->Offers.Count() == 0)
		{
			GUI::Label(Rect(x, y, view.width - 8, rowH), "No company is chartering just now.", small);
			y += rowH;
		}
		for (const auto& offer : cs::List<cs::Ref<CharterOffer>>(charters->Offers))
		{
			auto candidate = charters->Candidate(offer, jd);
			GUI::contentColor = Color::white();
			GUI::Label(Rect(x, y, view.width - 200, rowH), cs::Cat(offer->Title(), "  ·  ", Credits(offer->Total()), " in all  ·  up ",
				cs::Fmt(cs::Math::Max(0, offer->ExpiresJd - jd), "0"), " d more"), small);
			if (candidate != nullptr && GUI::Button(Rect(view.xMax() - 192, y, 188, rowH - 2), cs::Cat("Charter the ", candidate->Name()), button) &&
				!CoopSends("charter", K(charters->Offers, offer))) // US-T44 (US-T43): an entry in co-op
				app->ShowToast(charters->Take(offer, candidate, jd) != nullptr ? cs::Cat("The ", candidate->Name(), " goes out on charter to ", offer->Charterer->Name) : cs::String("No longer on offer"));
			y += rowH;
		}
		y += 4;
		GUI::contentColor = Dim;
		GUI::Label(Rect(x, y, view.width - 8, rowH), "OUT ON CHARTER", small);
		y += rowH;
		for (const auto& c : charters->Active()) // C# copies it first (new List<Charter>(charters.Active)); Active() is a fresh list already
		{
			GUI::contentColor = Color(0.6f, 0.9f, 1.0f);
			GUI::Label(Rect(x, y, view.width - 120, rowH), cs::Cat("The ", c->Ship->Name(), " to ", c->Charterer->Name, "  ·  ", Credits(c->RatePerDay), " a day  ·  ",
				cs::Fmt(c->DaysLeft(jd), "0"), " d left  ·  hire paid ", Credits(c->HirePaid), "  ·  ", c->Ship->Ship->Cargo), small);
			if (GUI::Button(Rect(view.xMax() - 112, y, 108, rowH - 2), "Recall", button) && !CoopSends("recall", K(charters->Active(), c))) // US-T44 (US-T43)
				app->ShowToast(charters->Recall(c, jd) ? cs::Cat("The ", c->Ship->Name(), " is recalled") : cs::String("The account cannot pay the recall"));
			y += rowH;
			if (y > view.yMax() - rowH) return;
		}
	}

	void Hud::DrawTradeInsurance(const cs::Ref<TradeDesk>& desk, Rect view, float rowH, double jd)
	{
		auto ins = desk->Insurance;
		auto h = app->World()->Holdings;
		float x = view.x + 4, y = view.y;
		GUI::contentColor = Dim;
		GUI::Label(Rect(x, y, view.width - 8, rowH), cs::Cat("Underwriters pay ", Percent(TradeRules::InsuranceCover()->Value),
			" of a hull lost, or a warehouse seized, for a premium a day that climbs with the risk"), small);
		y += rowH;
		// C#'s local function Row(string name, InsurancePolicy policy, (double cover, double premium, double risk) q, Func<InsurancePolicy> insure).
		auto Row = [&](const cs::String& name, const cs::Ref<InsurancePolicy>& policy, const std::tuple<double, double, double>& q,
			const cs::Func<cs::Ref<InsurancePolicy>>& insure, const cs::String& coopKey) // US-T44 (US-T43): coopKey, what it insures for a co-op entry
		{
			const auto& [cover, premium, risk] = q;
			GUI::contentColor = policy != nullptr ? Color(0.55f, 1.0f, 0.6f) : risk >= TradeInsurance::Uninsurable ? Color(1.0f, 0.55f, 0.45f) : Color::white();
			cs::String state = policy != nullptr ? cs::Cat("insured for ", Credits(policy->Cover), " at ", Credits(policy->PremiumPerDay), " a day")
				: risk >= TradeInsurance::Uninsurable ? cs::Cat("uninsurable here (risk ", Percent(risk), ")")
				: cs::Cat("quote: ", Credits(cover), " for ", Credits(premium), " a day (risk ", Percent(risk), ")");
			GUI::Label(Rect(x, y, view.width - 120, rowH), cs::Cat(name, "  ·  ", state), small);
			if (policy != nullptr)
			{
				if (GUI::Button(Rect(view.xMax() - 112, y, 108, rowH - 2), "Cancel", button) && !CoopSends("uninsure", coopKey)) ins->Cancel(policy, jd); // US-T44 (US-T43)
			}
			else if (risk < TradeInsurance::Uninsurable && GUI::Button(Rect(view.xMax() - 112, y, 108, rowH - 2), "Insure", button) && !CoopSends("insure", coopKey)) // US-T44 (US-T43)
				app->ShowToast(insure() != nullptr ? cs::Cat(name, " is insured") : cs::String("The underwriters will not write it"));
			y += rowH;
		};
		for (const auto& o : h->Ships)
		{
			if (o->Lost || y > view.yMax() - rowH) continue;
			auto owned = o;
			Row(cs::Cat("The ", o->Name()), ins->Of(o), ins->Quote(o), [&ins, owned, jd]() { return ins->Insure(owned, jd); }, cs::Cat("s ", K(h->Ships, o))); // US-T44 (US-T43): the key
		}
		for (const auto& p : h->Properties)
		{
			if (p->Lost || p->Kind != PropertyKind::Warehouse || y > view.yMax() - rowH) continue;
			auto prop = p;
			Row(cs::Cat("The ", p->Name), ins->Of(p), ins->Quote(p), [&ins, prop, jd]() { return ins->Insure(prop, jd); }, cs::Cat("p ", K(h->Properties, p))); // US-T44 (US-T43): the key
		}
		if (y == view.y + rowH)
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(x, y, view.width - 8, rowH), "Nothing to insure yet: buy a hull or a warehouse (F10).", small);
		}
	}

	void Hud::DrawTradeWarehouses(const cs::Ref<TradeDesk>& desk, Rect view, float rowH, double jd)
	{
		auto h = app->World()->Holdings;
		auto houses = cs::List<cs::Ref<Property>>();
		for (const auto& p : h->OfKind(PropertyKind::Warehouse))
			if (!p->Lost) houses.Add(p);
		float x = view.x + 4;
		if (houses.Count() == 0)
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(x, view.y, view.width - 8, rowH), "No warehouses yet: buy one at a port (F10). Store goods where they are cheap and sell them when the price is up.", small);
			return;
		}
		int lines = 0;
		for (const auto& p : houses) { (void)p; lines += 1 + GoodInfo::Count; }
		auto content = Rect(0, 0, view.width - 20, lines * rowH);
		tradeScroll = GUI::BeginScrollView(view, tradeScroll, content);
		float y = 0;
		for (const auto& p : houses)
		{
			auto s = p->Settlement;
			auto risk = desk->Assess(s);
			GUI::contentColor = risk.Total >= 0.35 ? Color(1.0f, 0.72f, 0.4f) : Accent;
			GUI::Label(Rect(4, y, content.width - 8, rowH), cs::Cat(p->Name, "  ·  ", Tonnes(p->StoredTonnes()), " of ", Tonnes(p->CapacityTonnes), "  ·  ", risk.Summary(),
				desk->Insurance->Of(p) != nullptr ? "  ·  insured" : "  ·  uninsured"), small);
			y += rowH;
			for (int g = 0; g < GoodInfo::Count; g++)
			{
				auto good = static_cast<Good>(g);
				double ratio = s->Price[g] / GoodInfo::Of(good)->BasePrice;
				GUI::contentColor = ratio > 1.5 ? Color(1.0f, 0.7f, 0.5f) : ratio < 0.7 ? Color(0.55f, 0.85f, 1.0f) : Color::white();
				GUI::Label(Rect(16, y, 380, rowH), cs::Cat(GoodInfo::Of(good)->Name, "  ·  stored ", Tonnes(p->Stock[g]), "  ·  ",
					cs::Fmt(s->Price[g], "N0"), " a t (x", cs::Fmt(ratio, "0.00"), ")"), small);
				if (GUI::Button(Rect(content.width - 220, y, 104, rowH - 2), "Store 1 kt", button))
				{
					double t = h->StoreFromMarket(p, good, 1000, jd);
					app->ShowToast(t > 0 ? cs::Cat("Stored ", Tonnes(t), " in the ", p->Name) : cs::Cat(s->Name, " has none to spare (or the warehouse is full)"));
				}
				if (p->Stock[g] > 0.01 && GUI::Button(Rect(content.width - 110, y, 104, rowH - 2), "Sell all", button))
				{
					double cr = h->SellFromStore(p, good, p->Stock[g], jd);
					app->ShowToast(cr > 0 ? cs::Cat("Sold out of the ", p->Name, " for ", Credits(cr)) : cs::Cat(s->Name, " does not buy it"));
				}
				y += rowH;
			}
		}
		GUI::EndScrollView();
	}

	void Hud::DrawBackDoor(const cs::Ref<TradeDesk>& desk, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, Rect r, float rowH, double jd)
	{
		auto sm = desk->Smuggling;
		GUI::contentColor = Color(1.0f, 0.72f, 0.4f);
		GUI::Label(Rect(r.x, r.y, r.width, rowH), cs::Cat("BACK DOOR  ·  ", port->Name, "  (off the books: no port tax, but customs may be waiting)"), small);
		float y = r.y + rowH + 2;
		auto seen = cs::HashSet<Good>();
		int rows = 0;
		auto items = ship->PersonalHold()->Items(); // C#: new List<CargoItem>(ship.PersonalHold.Items), a copy
		for (const auto& item : items)
		{
			if (rows >= 3 || !seen.Add(item->Good)) continue;
			auto reason = sm->ReasonFor(ship, port, item->Good);
			if (reason == BackDoorReason::None) continue;
			int crates = 0;
			for (const auto& it : ship->PersonalHold()->Items())
				if (it->Good == item->Good && sm->ReasonFor(ship, port, it) != BackDoorReason::None) crates++;
			auto good = item->Good;
			GUI::contentColor = Color::white();
			GUI::Label(Rect(r.x, y, r.width - 120, rowH), cs::Cat(crates, " crate", crates == 1 ? "" : "s", " of ", cs::Strings::ToLowerInvariant(GoodInfo::Of(good)->Name),
				" (", Smuggling::Describe(reason), ")  ·  pays ", cs::Fmt(Smuggling::PriceFor(port, good, reason), "N0"),
				" a t  ·  customs ", Percent(sm->CatchChance(port, reason))), small);
			if (GUI::Button(Rect(r.xMax() - 112, y, 108, rowH - 2), cs::Cat("Sell ", cs::Math::Min(crates, tradeCrates)), button))
			{
				int sold = 0;
				double credits = 0;
				auto outcome = sm->SellBackDoor(ship, port, good, cs::Math::Min(crates, tradeCrates), jd, sold, credits);
				cs::String message;
				switch (outcome)
				{
				case Smuggling::Outcome::Sold: message = cs::Cat("Sold ", sold, " crate", sold == 1 ? "" : "s", " out the back door for ", Credits(credits)); break;
				case Smuggling::Outcome::Caught: message = "Customs! The crates are seized and you are charged with smuggling"; break;
				default: message = "Nothing here for the back door"; break;
				}
				app->ShowToast(message);
			}
			y += rowH;
			rows++;
		}
	}
}
