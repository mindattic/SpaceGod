// Port of Unity.SpaceGod's Runtime/UI/Hud.cs lines 2383-3745 (see UI/Hud.Boards.inl for the declarations).

#include "UI/Hud.h"
#include "UI/UiMotion.h" // US-T255
#include "CaptureEvents.h" // US-T72

#include "UnitySpaceApp.h"
#include "Coop/CoopPeer.h" // US-T265: plants are bought and run outside co-op
#include "Camera/SpaceCamera.h"
#include "Rendering/ShipThumbnails.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Hyperlanes.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/BoardingCommand.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Construction.h"
#include "Combat/Facilities.h"
#include "Combat/FlightDecks.h"
#include "Combat/Outfitting.h"
#include "Combat/Salvage.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Economy/CargoHold.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/Industry.h" // US-T265: a plant's recipe
#include "Economy/MarketIntel.h"
#include "Economy/MarketShocks.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Economy/TradeInsurance.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/FighterSeat.h"
#include "Camera/StarTravel.h" // US-T106: the helm's bar stands aside for a trip
#include "Embodiment/HelmAbilities.h" // US-T106
#include "Embodiment/ShipHelm.h"
#include "Information/LightSpeed.h"
#include "Information/News.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Sensors/Sensors.h"
#include "Sensors/SensorPosture.h"
#include "Systems/CruiseDrive.h"
#include "Systems/Exploration.h" // US-T260: survey data sold to cartographers
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"
#include "UI/Icons.h" // US-T263: the market board's sparklines

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	// ---------------------------------------------------------------- bounty board

	void Hud::DrawBounties()
	{
		UiMotion::Scope motion("hud.bounties"); // US-T255
		auto outlaws = app->World()->Outlaws;
		if (outlaws == nullptr) return;
		cs::Ref<Settlement> port = nullptr;
		auto t = app->Selected;
		if (t.IsBody())
			for (const auto& s : app->Economy()->At(t.Body))
				if (s->Discovered() && (port == nullptr || s->Population > port->Population)) port = s;
		if (port == nullptr && app->IsPiloting())
			for (const auto& s : app->Economy()->Settlements)
				if (s->Discovered() && app->NearPort(s)) { port = s; break; }
		constexpr float w = 760, rowH = 18;
		auto board = port != nullptr ? outlaws->BoardAt(port) : cs::List<cs::Ref<BountyPosting>>();
		int rows = Mathf::Min(board.Count(), 14);
		// Captains in the player's brig, and kills waiting on a scan of the wreck (U.SPC-T63).
		cs::Ref<Ship> ship = app->IsPiloting() ? app->World()->PilotedShip : nullptr;
		auto held = outlaws->HeldAboard(ship);
		auto claims = ship != nullptr ? outlaws->Claims : cs::List<cs::Ref<KillClaim>>();
		int extra = held.Count() + claims.Count();
		float h = 54 + Mathf::Max(1, rows) * rowH + (extra > 0 ? 8 + extra * 22 : 0);
		auto r = Panel(Rect((Screen::width - w) * 0.5f, 120, w, h));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 22),
			cs::Cat("BOUNTY BOARD  ·  ", port != nullptr ? cs::Cat(port->Name, "  ·  ", port->Owner->Name) : cs::String("no port in view")), title);
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		cs::String note = port == nullptr ? cs::String("Select a world with a port, or fly alongside one.   F4 to close")
			: port->Owner->IsPirate ? cs::String("Nobody here answers to the law: a haven keeps no board.   F4 to close")
			: board.Count() == 0 ? cs::String("Nobody is wanted here.   F4 to close")
			: cs::Cat("Bounties posted by ", port->Owner->Name, ", biggest first; the price pays in full alive, half dead on a scanned wreck.   F4 to close");
		GUI::Label(Rect(r.x + 12, r.y + 28, w - 24, 18), note, small);
		for (int i = 0; i < rows; i++)
		{
			auto p = board[i];
			GUI::contentColor = p->IsPlayer() ? UiStyle::Danger : p->Outlaw != nullptr && p->Outlaw->Feared ? UiStyle::Warning : Color::white();
			GUI::Label(Rect(r.x + 12, r.y + 48 + i * rowH, w - 24, rowH), Outlaws::Describe(p, app->Clock->Jd), small);
		}
		float y = r.y + 56 + Mathf::Max(1, rows) * rowH;
		bool alongside = port != nullptr && app->NearPort(port);
		for (const auto& o : held)
		{
			GUI::contentColor = Color(0.6f, 0.9f, 1.0f);
			GUI::Label(Rect(r.x + 12, y + 2, w - 200, rowH),
				cs::Cat("IN YOUR BRIG  ·  ", o->Title(), "  ·  ", Credits(o->Record->TotalBounty()), " alive  ·  ransom ", Credits(Outlaws::RansomFor(o))), small);
			GUI::contentColor = Color::white();
			if (alongside && !port->Owner->IsPirate && (!o->Record->Wanted() || o->Record->WantedBy(port->Owner)) &&
				GUI::Button(Rect(r.xMax() - 180, y, 168, 20), cs::Cat("Hand over  +", Credits(o->Record->TotalBounty())), button))
			{
				if (!CoopSends("deliver", K(port), K(held, o))) DeliverOutlaw(outlaws, o, port); // US-T44 (US-T43): an entry in co-op; the original's lines in DeliverOutlaw (Hud.Coop.cpp)
			}
			else if (alongside && port->Owner->IsPirate && GUI::Button(Rect(r.xMax() - 180, y, 168, 20), cs::Cat("Ransom  +", Credits(Outlaws::RansomFor(o))), button))
			{
				if (!CoopSends("ransom", K(port), K(held, o))) RansomOutlaw(outlaws, o, port); // US-T44 (US-T43): likewise
			}
			y += 22;
		}
		for (const auto& c : claims)
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(r.x + 12, y + 2, w - 24, rowH), cs::Cat("KILL UNPROVEN  ·  ", c->Outlaw->Title(), "  ·  scan the wreck of the ",
				c->Wreck != nullptr ? c->Wreck->Name : cs::String("flagship"), " for ", Credits(Outlaws::DeadRate(c->Outlaw->Record)), ", the dead rate"), small);
			y += 22;
		}
		GUI::contentColor = old;
	}

	// ---------------------------------------------------------------- contracts board (U.SPC-T101)

	void Hud::DrawContracts()
	{
		UiMotion::Scope motion("hud.contracts"); // US-T255
		auto contracts = app->World()->Contracts;
		if (contracts == nullptr) return;
		cs::Ref<Settlement> port = nullptr;
		auto t = app->Selected;
		if (t.IsBody())
			for (const auto& s : app->Economy()->At(t.Body))
				if (s->Discovered() && (port == nullptr || s->Population > port->Population)) port = s;
		if (port == nullptr && app->IsPiloting())
			for (const auto& s : app->Economy()->Settlements)
				if (s->Discovered() && app->NearPort(s)) { port = s; break; }
		auto board = port != nullptr ? contracts->BoardAt(port) : cs::List<cs::Ref<Contract>>();
		auto mine = contracts->PlayerActive();
		constexpr float w = 860, rowH = 20;
		int rows = Mathf::Min(board.Count(), 12), ownRows = Mathf::Min(mine.Count(), 6);
		float h = 54 + Mathf::Max(1, rows) * rowH + (ownRows > 0 ? 24 + ownRows * rowH : 0);
		auto r = Panel(Rect((Screen::width - w) * 0.5f, 120, w, h));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 22),
			cs::Cat("CONTRACTS  ·  ", port != nullptr ? cs::Cat(port->Name, "  ·  ", port->Owner->Name) : cs::String("no port in view")), title);
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		auto ship = app->World()->PilotedShip;
		bool alongside = port != nullptr && ship != nullptr && app->NearPort(port);
		cs::String note = port == nullptr ? "Select a world with a port, or fly alongside one.   F7 to close"
			: port->Owner->IsPirate ? "A haven posts no contracts.   F7 to close"
			: board.Count() == 0 ? "Nothing posted here today.   F7 to close"
			: alongside ? "Jobs posted from what the system needs. Freight: buy the crates at any market and bring them in.   F7 to close"
			: "Fly alongside to take a job (P to pilot a ship).   F7 to close";
		GUI::Label(Rect(r.x + 12, r.y + 28, w - 24, 18), note, small);
		double jd = app->Clock->Jd;
		for (int i = 0; i < rows; i++)
		{
			auto c = board[i];
			float y = r.y + 48 + i * rowH;
			GUI::contentColor = c->Urgency >= 0.6 ? UiStyle::Warning : Color::white();
			GUI::Label(Rect(r.x + 12, y, w - 110, rowH), Contracts::Describe(c, jd), small);
			if (alongside && GUI::Button(Rect(r.xMax() - 90, y, 78, rowH - 2), "Take", button) &&
				!CoopSends("ctake", K(port), i)) // US-T44 (US-T43): an entry in co-op
				TakeContract(contracts, c, ship, jd); // US-T44 (US-T43): the original's lines, moved into TakeContract (Hud.Coop.cpp)
		}
		if (ownRows > 0)
		{
			float y0 = r.y + 52 + Mathf::Max(1, rows) * rowH;
			GUI::contentColor = Dim;
			GUI::Label(Rect(r.x + 12, y0, w - 24, 18), "YOUR CONTRACTS  (giving one up costs standing with whoever posted it)", small);
			for (int i = 0; i < ownRows; i++)
			{
				auto c = mine[i];
				float y = y0 + 22 + i * rowH;
				GUI::contentColor = c->DueJd - jd < 1 ? UiStyle::Danger : Color(0.6f, 0.9f, 1.0f);
				GUI::Label(Rect(r.x + 12, y, w - 110, rowH), Contracts::Describe(c, jd), small);
				if (GUI::Button(Rect(r.xMax() - 90, y, 78, rowH - 2), "Abandon", button) && !CoopSends("cabandon", i) && contracts->Abandon(c, jd)) // US-T44 (US-T43): an entry in co-op
					app->ShowToast(cs::Cat("Contract abandoned: ", c->Title()));
			}
		}
		GUI::contentColor = old;
	}

	// ---------------------------------------------------------------- the player's fleet (U.SPC-T102)

	void Hud::DrawFleet()
	{
		UiMotion::Scope motion("hud.fleet"); // US-T255
		auto world = app->World();
		auto h = world->Holdings;
		if (h == nullptr) return;
		double jd = app->Clock->Jd;
		auto sel = app->Selected;
		cs::Ref<Settlement> port = nullptr;
		if (sel.IsBody())
			for (const auto& s : app->Economy()->At(sel.Body))
				if (s->Discovered() && (port == nullptr || s->Population > port->Population)) port = s;
		if (port == nullptr && app->IsPiloting())
			for (const auto& s : app->Economy()->Settlements)
				if (s->Discovered() && app->NearPort(s)) { port = s; break; }
		auto flown = world->PilotedShip;
		cs::Ref<Faction> purse = flown != nullptr && flown->Faction != h->House ? flown->Faction : nullptr;

		constexpr float w = 820, rowH = 20;
		int shipRows = 0, propRows = 0;
		for (const auto& o : h->Ships) if (!o->Lost) shipRows++;
		for (const auto& p : h->Properties) if (!p->Lost) propRows += p->Kind == PropertyKind::Plant ? 2 : 1; // a plant: its state, then its goods (US-T265)
		float listH = Mathf::Min((shipRows + propRows + 2) * rowH, 260.0f);
		float h0 = 128 + 24 + listH + 6 * 16;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, 110, w, h0));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 22), cs::Cat("YOUR FLEET  ·  account ", Credits(h->Balance()), "  ·  earned ", Credits(h->TotalEarned),
			"  ·  costs ", Credits(h->TotalCosts), "  ·  lost ", Credits(h->TotalLost)), title);
		auto old = GUI::contentColor;

		// The account.
		float x = r.x + 12, y = r.y + 30;
		if (purse != nullptr)
		{
			if (GUI::Button(Rect(x, y, 150, 20), "Deposit 10 M cr", button) && !CoopSends("deposit")) // US-T44 (US-T43): an entry in co-op (as each button below)
				app->ShowToast(h->Deposit(purse, 1e7, jd) ? cs::String("10 M cr into the house account") : cs::String("The purse cannot spare it"));
			if (GUI::Button(Rect(x + 156, y, 150, 20), "Withdraw 10 M cr", button) && !CoopSends("withdraw"))
				app->ShowToast(h->Withdraw(purse, 1e7, jd) ? cs::Cat("10 M cr out to the ", purse->Name) : cs::String("Not that much in the account"));
			if (!h->Owns(flown) && GUI::Button(Rect(x + 312, y, 220, 20), cs::Cat("Take ", flown->Name, " into the fleet"), button) && !CoopSends("enlist"))
				app->ShowToast(h->Enlist(flown, jd) != nullptr ? cs::Cat(flown->Name, " flies your flag now") : cs::String("It cannot join the fleet"));
		}
		else
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(x, y + 2, w - 24, 18), flown != nullptr ? cs::Cat("Flying your own ", flown->Name, ": its purse is the house account.")
				: cs::String("Pilot a ship (P) to move money between its purse and the house account."), small);
		}

		// Buying at the port in view, and staking a claim on the selected world.
		y += 24;
		GUI::contentColor = Color::white();
		if (port != nullptr)
		{
			GUI::Label(Rect(x, y + 2, 170, 18), cs::Cat(port->Name, ":"), small);
			float bx = x + 170;
			if (h->CanBuyAt(port))
				for (auto cls : { ShipClass::Freighter, ShipClass::Miner, ShipClass::Courier, ShipClass::Destroyer })
				{
					if (GUI::Button(Rect(bx, y, 112, 20), cs::Cat(ShipClassSpec::Of(cls)->DisplayName, " ", Credits(Economy::HullPrice(cls))), button) &&
						!CoopSends("hull", K(port), static_cast<int>(cls))) // US-T44 (US-T43)
					{
						auto bought = h->Buy(port, cls, cs::String(), jd);
						app->ShowToast(bought != nullptr ? cs::Cat("Bought the ", bought->Name(), " at ", port->Name) : cs::String("Cannot afford it here"));
					}
					bx += 116;
				}
			if (GUI::Button(Rect(bx, y, 96, 20), "Warehouse", button) && !CoopSends("warehouse", K(port))) // US-T44 (US-T43)
				app->ShowToast(h->BuyWarehouse(port, jd) != nullptr ? cs::Cat("A warehouse at ", port->Name, " for ", Credits(Holdings::WarehousePrice)) : cs::String("Not for sale"));
			bx += 100;
			if (GUI::Button(Rect(bx, y, 108, 20), cs::Cat("5% share ", Credits(Holdings::SharePrice(port, 0.05))), button) && !CoopSends("share", K(port))) // US-T44 (US-T43)
				app->ShowToast(h->BuyShare(port, 0.05, jd) != nullptr ? cs::Cat("5% of ", port->Name, " is yours") : cs::String("Not for sale"));
		}
		else
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(x, y + 2, 420, 18), "Select a world with a port (or fly alongside one) to buy there.", small);
		}

		// A refinery or factory at the port, running one of its own works off the house warehouse there (US-T265).
		y += 24;
		if (port != nullptr)
		{
			bool stored = false;
			for (const auto& p : h->OfKind(PropertyKind::Warehouse))
				if (p->Settlement == port) stored = true;
			auto kinds = Holdings::PlantKindsAt(port);
			GUI::contentColor = Dim;
			if (Coop::CoopPeer::Current() != nullptr)
				GUI::Label(Rect(x, y + 2, w - 24, 18), "Plants are bought and run outside co-op.", small);
			else if (kinds.Count() == 0)
				GUI::Label(Rect(x, y + 2, w - 24, 18), cs::Cat(port->Name, " runs no works a refinery or factory could copy."), small);
			else if (!stored)
				GUI::Label(Rect(x, y + 2, w - 24, 18), cs::Cat("Buy a warehouse at ", port->Name, " to build a refinery or factory there: it feeds the plant and takes what it makes."), small);
			else
			{
				GUI::Label(Rect(x, y + 2, 170, 18), "Build a plant:", small);
				GUI::contentColor = Color::white();
				float bx = x + 170;
				for (auto kind : kinds)
				{
					auto recipe = Recipe::Of(kind);
					if (bx + 150 > r.xMax() - 12) break;
					if (GUI::Button(Rect(bx, y, 146, 20), cs::Cat(recipe->Name, " ", Credits(Holdings::PlantPrice(kind))), button))
					{
						auto plant = h->BuyPlant(port, kind, jd);
						app->ShowToast(plant != nullptr ? cs::Cat("The ", plant->Name, " is yours") : cs::String("Not for sale (one of each per port, and the account must cover it)"));
					}
					bx += 150;
				}
			}
		}
		if (sel.IsBody() && !(*world->Catalog())[sel.Body]->IsStar())
		{
			GUI::contentColor = Color::white();
			float cx = r.xMax() - 12 - 2 * 150;
			for (auto ore : { Good::NickelIron, Good::Water })
			{
				if (GUI::Button(Rect(cx, y + 24, 146, 20), cs::Cat("Claim ", cs::Strings::ToLowerInvariant(GoodInfo::Of(ore)->Name), " ", Credits(Holdings::ClaimPrice(ore))), button) &&
					!CoopSends("claim", sel.Body, static_cast<int>(ore))) // US-T44 (US-T43)
					app->ShowToast(h->StakeClaim(sel.Body, ore, jd) != nullptr ? cs::Cat("Claim staked on ", (*world->Catalog())[sel.Body]->Name) : cs::String("Cannot stake that claim"));
				cx += 150;
			}
		}
		GUI::contentColor = Dim;
		GUI::Label(Rect(x, y + 26, 460, 18), "Orders use the selected world (a route or patrol to it) or the selected ship (escort).   F10 to close", small);

		// Ships and property.
		auto view = Rect(r.x + 8, y + 48, w - 16, listH);
		auto content = Rect(0, 0, w - 36, (shipRows + propRows + 2) * rowH);
		fleetScroll = GUI::BeginScrollView(view, fleetScroll, content);
		float ly = 0;
		bool coop = Coop::CoopPeer::Current() != nullptr; // US-T265: a plant's switches are the host's alone
		// A plant's two rows (US-T265): its state and switches, then its goods.
		auto DrawPlantRows = [&](const cs::Ref<Property>& p)
		{
			auto store = p->Warehouse;
			auto was = GUI::contentColor;
			// Its state: what it made of its rate, the profit a day it is running at, and its switches.
			cs::String status = p->Status == PlantStatus::Running ? "running"
				: p->Status == PlantStatus::Starved ? "STARVED of feedstock"
				: p->Status == PlantStatus::Full ? "FULL: no room in the warehouse"
				: p->DownUntilJd > jd ? "idle, mending after a raid"
				: !p->Working ? "idle (stood down)" : "idle";
			GUI::contentColor = p->Status == PlantStatus::Running ? UiStyle::Good : p->Status == PlantStatus::Idle ? Dim : UiStyle::Warning;
			GUI::Label(Rect(4, ly, w - 300, rowH), cs::Cat(p->Name, "  ·  ", status, "  ·  ", cs::Fmt(p->Utilisation, "P0"), " of rate  ·  ",
				p->ProfitPerDay >= 0 ? "+" : "", Credits(p->ProfitPerDay), " a day"), small);
			GUI::contentColor = Color::white();
			if (!coop)
			{
				if (GUI::Button(Rect(w - 36 - 256, ly, 80, rowH - 2), p->Working ? "Stand down" : "Start up", button)) p->Working = !p->Working;
				if (GUI::Button(Rect(w - 36 - 172, ly, 84, rowH - 2), p->BuyInputs ? "Buys feed" : "Store feed", button)) p->BuyInputs = !p->BuyInputs;
				if (GUI::Button(Rect(w - 36 - 84, ly, 84, rowH - 2), p->SellOutputs ? "Sells out" : "Stores out", button)) p->SellOutputs = !p->SellOutputs;
			}
			ly += rowH;
			// Its goods: each feedstock's need a day against the warehouse's stock, and each product's rate and stock.
			auto Stock = [&](Good g) { return store != nullptr && !store->Lost ? store->Stock[static_cast<int>(g)] : 0.0; };
			cs::String in, out;
			if (p->Recipe != nullptr)
			{
				for (const auto& [g, rate] : p->Recipe->Inputs)
					in = cs::Cat(in, in.empty() ? "" : ", ", cs::Strings::ToLowerInvariant(GoodInfo::Of(g)->Name), " ", Tonnes(rate * p->Level), "/d (", Tonnes(Stock(g)), ")");
				for (const auto& [g, rate] : p->Recipe->Outputs)
					out = cs::Cat(out, out.empty() ? "" : ", ", cs::Strings::ToLowerInvariant(GoodInfo::Of(g)->Name), " ", Tonnes(rate * p->Level), "/d (", Tonnes(Stock(g)), ")");
			}
			GUI::contentColor = Dim;
			GUI::Label(Rect(16, ly, w - 52, rowH), cs::Cat("in: ", in, "  ->  out: ", out, "  ·  stock in ", store != nullptr ? store->Name : cs::String("no warehouse"),
				h != nullptr && store != nullptr ? cs::Cat(" ", Tonnes(store->StoredTonnes()), "/", Tonnes(store->CapacityTonnes)) : cs::String()), small);
			ly += rowH;
			GUI::contentColor = was;
		};

		GUI::contentColor = Dim;
		GUI::Label(Rect(4, ly, w, rowH), shipRows == 0 ? "No ships yet." : "SHIPS", small);
		ly += rowH;
		for (const auto& o : h->Ships)
		{
			if (o->Lost) continue;
			GUI::contentColor = o->Ship == flown ? UiStyle::Good : Color::white();
			cs::String captain = cs::Cat(o->Captain != nullptr ? cs::Cat("Capt. ", o->Captain->Name) : cs::String("no captain"),
				world->Trade != nullptr && world->Trade->Insurance->Of(o) != nullptr ? "  ·  insured" : "");
			GUI::Label(Rect(4, ly, 540, rowH), cs::Cat(o->Name(), "  (", o->Ship->Spec->DisplayName, ")  ·  ", captain, "  ·  ", o->Orders->Describe(world->Catalog()),
				"  ·  net ", Credits(o->Net())), small);
			if (GUI::Button(Rect(w - 36 - 236, ly, 70, rowH - 2), o->Captain == nullptr ? "Hire" : "Dismiss", button) && !CoopSends("captain", K(h->Ships, o))) // US-T44 (US-T43)
			{
				if (o->Captain == nullptr)
				{
					auto hired = h->Hire(o, jd);
					app->ShowToast(cs::Cat("Captain ", hired->Name, " takes the ", o->Name()));
				}
				else h->Dismiss(o, jd);
			}
			if (o->OnCharter())
			{
				GUI::contentColor = Dim;
				GUI::Label(Rect(w - 36 - 162, ly, 160, rowH), "on charter (\\)", small);
			}
			else if (GUI::Button(Rect(w - 36 - 162, ly, 160, rowH - 2), "Orders: next", button) &&
				!CoopSends("orders", K(h->Ships, o), CoopKeyText(sel))) // US-T44 (US-T43)
			{
				auto next = NextOrders(o, sel);
				app->ShowToast(cs::Cat(o->Name(), ": ", next->Describe(world->Catalog())));
			}
			ly += rowH;
		}
		GUI::contentColor = Dim;
		GUI::Label(Rect(4, ly, w, rowH), propRows == 0 ? "No property yet." : "PROPERTY", small);
		ly += rowH;
		GUI::contentColor = Color::white();
		for (const auto& p : h->Properties)
		{
			if (p->Lost) continue;
			if (p->Kind == PropertyKind::Plant)
			{
				DrawPlantRows(p);
				continue;
			}
			cs::String extra = p->Kind == PropertyKind::Warehouse ? cs::Cat("  ·  ", Tonnes(p->StoredTonnes()), " of ", Tonnes(p->CapacityTonnes), " stored")
				: p->Kind == PropertyKind::MiningClaim ? cs::Cat("  ·  ", Tonnes(p->YieldPerDay), " a day") : cs::String();
			GUI::Label(Rect(4, ly, w - 40, rowH), cs::Cat(p->Name, extra, "  ·  income ", Credits(p->Income), "  ·  upkeep ", Credits(p->Upkeep)), small);
			ly += rowH;
		}
		GUI::EndScrollView();

		// The latest lines in the books.
		GUI::contentColor = Dim;
		float by = view.yMax() + 4;
		int n = h->Ledger.Count();
		for (int i = cs::Math::Max(0, n - 6); i < n; i++, by += 16)
		{
			auto e = h->Ledger[i];
			GUI::Label(Rect(r.x + 12, by, w - 24, 16), cs::Cat(Astro::ToDateTime(e->Jd).ToString("yyyy-MM-dd"), "  ", e->Text,
				cs::Math::Abs(e->Amount) >= 1 ? cs::Cat("  ", e->Amount > 0 ? "+" : "", Credits(e->Amount), e->Cash ? "" : " (loss)") : cs::String()), small);
		}
		GUI::contentColor = old;
	}

	cs::Ref<StandingOrder> Hud::NextOrders(const cs::Ref<OwnedShip>& o, const Target& sel)
	{
		auto h = app->World()->Holdings;
		int here = o->Ship->Orbit != nullptr ? o->Ship->Orbit->Body : -1;
		int there = sel.IsBody() ? sel.Body : -1;
		int tradeThere = there >= 0 ? h->MarketBodyFor(there) : -1; // a planet's port moon stands in for it
		cs::Ref<Property> claim = nullptr;
		for (const auto& p : h->OfKind(PropertyKind::MiningClaim))
			if (Economy::Carries(o->Ship->Spec->Class, p->Ore)) { claim = p; break; }
		auto charge = sel.IsShip() && sel.Ship != o->Ship ? sel.Ship : app->World()->PilotedShip;
		cs::List<cs::Ref<StandingOrder>> choices
		{
			StandingOrder::Hold(),
			here >= 0 && tradeThere >= 0 && here != tradeThere ? StandingOrder::TradeRoute(cs::List<int>{ here, tradeThere }) : nullptr,
			claim != nullptr ? StandingOrder::Mining(claim) : nullptr,
			charge != nullptr && charge != o->Ship ? StandingOrder::Escort(charge) : nullptr,
			there >= 0 ? (here >= 0 && here != there ? StandingOrder::Patrol(cs::List<int>{ here, there }) : StandingOrder::Patrol(cs::List<int>{ there })) : nullptr,
			StandingOrder::FreeTrade(),
		};
		int start = static_cast<int>(o->Orders->Kind);
		for (int k = 1; k <= choices.Count(); k++)
		{
			auto c = choices[(start + k) % choices.Count()];
			if (c != nullptr && h->Order(o, c)) return c;
		}
		h->Order(o, StandingOrder::Hold());
		return o->Orders;
	}

	// ---------------------------------------------------------------- market board

	namespace
	{
		/// <summary>A price's last days as a row of thin bars, scaled from the stretch's low to its high; a fixed day width,
		/// so a short history sits at the right (US-T263).</summary>
		void Sparkline(Rect area, const cs::List<double>& trail, Color colour)
		{
			auto old = GUI::color;
			GUI::color = Color(colour.r, colour.g, colour.b, 0.12f);
			GUI::DrawTexture(area, Icons::White());
			if (trail.Count() > 0)
			{
				double lo = trail[0], hi = trail[0];
				for (double p : trail) { lo = cs::Math::Min(lo, p); hi = cs::Math::Max(hi, p); }
				GUI::color = colour;
				float step = area.width / Settlement::HistoryDays;
				for (int k = 0; k < trail.Count(); k++)
				{
					double f = hi - lo > 1e-6 * hi ? (trail[k] - lo) / (hi - lo) : 0.5;
					float bh = 1 + static_cast<float>(f) * (area.height - 1);
					float x = area.xMax() - (trail.Count() - k) * step;
					GUI::DrawTexture(Rect(x, area.yMax() - bh, step < 1 ? 1.0f : step, bh), Icons::White());
				}
			}
			GUI::color = old;
		}

		/// <summary>"112-180 / 150": a stretch's low-high and mean, or a dash with nothing held (US-T263).</summary>
		cs::String HistoryRangeText(const PriceRange& r)
		{
			return r.Samples == 0 ? cs::String("-")
				: cs::Cat(cs::Fmt(r.Low, "N0"), "-", cs::Fmt(r.High, "N0"), " / ", cs::Fmt(r.Mean, "N0"));
		}
	}

	void Hud::DrawMarket()
	{
		UiMotion::Scope motion("hud.market"); // US-T255
		cs::Ref<Settlement> market = nullptr;
		auto t = app->Selected;
		if (t.IsBody()) for (const auto& s : app->Economy()->At(t.Body)) if (s->Discovered()) { market = s; break; }
		if (t.IsShip() && t.Ship->Controller()->CargoDestination != nullptr) market = t.Ship->Controller()->CargoDestination;

		// Buy/Sell only make sense for a market the player's own ship could actually hand goods
		// over at right now -- not one merely being looked at from across the system.
		bool canTrade = market != nullptr && app->CanTradeNow(market);
		// Prices seen in port go into the trade desk's intelligence (U.SPC-T426).
		if (canTrade && Event::current().type == EventType::Layout && !CoopObserve(market) && app->World()->Trade != nullptr) // US-T44 (US-T43): an entry in co-op
			app->World()->Trade->Intel->Observe(market, app->Clock->Jd, "seen in port");
		auto law = app->World()->Law;
		cs::Ref<LawRecord> pilotRecord = app->IsPiloting() && law != nullptr ? law->PlayerRecord() : nullptr; // the embodied pilot's (US-T148)
		bool lawful = market != nullptr && market->Owner != nullptr && !market->Owner->IsPirate;
		// A row for the law at this port: the bounty to pay off here, or why it will not dock the ship.
		bool atPort = market != nullptr && pilotRecord != nullptr && lawful && app->NearPort(market);
		bool wantedHere = atPort && pilotRecord->WantedBy(market->Owner);
		bool refused = atPort && !canTrade && app->Economy()->LawRefuses(app->World()->PilotedShip, market);
		float lawRow = wantedHere || refused ? 22.0f : 0.0f;

		// What is shaking this market right now (a strike, a blight, a blockade, rationing), from the sim's own events.
		cs::Nullable<cs::String> shocks; // C# null: no shock
		if (market != nullptr)
			for (const auto& e : app->World()->Shocks->At(market))
				shocks = !shocks.has_value() ? e->Label : cs::Cat(*shocks, "  ·  ", e->Label);
		float extra = shocks.has_value() ? 18.0f : 0.0f;
		// US-T260: survey data aboard the piloted hull, and what this port's cartographer (if it keeps one) would pay.
		auto exploration = canTrade && app->Galaxy != nullptr ? Exploration::Find(*app->Galaxy) : nullptr;
		auto surveyHull = app->World()->PilotedShip;
		int surveyHeld = exploration != nullptr ? exploration->HeldBy(surveyHull).Count() : 0;
		float surveyRow = surveyHeld > 0 ? 20.0f : 0.0f;

		constexpr float rowH = 17;
		// US-T263: the board's price history (a 90-day sparkline and the 30- and 90-day low-high / average) for a port.
		float histW = market != nullptr ? 360.0f : 0.0f;
		float w = (canTrade ? 600.0f : 430.0f) + histW;
		// US-T208: 78 = the 50 above the column header, its row, and 11 of padding under the last good (it was 62: Munitions sat on the border).
		float h = marketOpen ? 78 + GoodInfo::Count * rowH + (canTrade ? 20 : 0) + extra + lawRow + surveyRow : 34;
		auto r = Panel(layout.Take(HudRegion::TopRight, w, h)); // TopRight's first slot (US-T153)
		cs::String heading = market != nullptr ? cs::Cat("MARKET  ·  ", market->Name) : cs::String("MARKET  ·  system average");
		if (GUI::Button(Rect(r.x + 8, r.y + 6, w - 16, 22), UiStyle::Fold(marketOpen, heading), title)) marketOpen = !marketOpen;
		if (!marketOpen) return;
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 12, r.y + 30, w - 24, 18), market != nullptr
			? cs::Cat(market->Owner->Name, pilotRecord != nullptr && lawful ? StandingText(pilotRecord, market->Owner) : cs::String("   (select a body or a trader to change, M to close)"))
			: cs::String("Select a port to see its market.   M to close"), small);
		// At a yard the ship can refit: open the outfitting panel (F2) from here too.
		if (canTrade && Outfitting::IsYard(market) && GUI::Button(Rect(r.xMax() - 110, r.y + 28, 98, 18), app->ShowOutfit ? "Outfit  [-]" : "Outfit", button))
			app->ShowOutfit = !app->ShowOutfit;
		if (shocks.has_value())
		{
			GUI::contentColor = UiStyle::Warning;
			GUI::Label(Rect(r.x + 12, r.y + 48, w - 24, 18), cs::Cat("NOW:  ", *shocks), small);
		}
		if (lawRow > 0)
		{
			GUI::contentColor = UiStyle::Danger;
			GUI::Label(Rect(r.x + 12, r.y + 50 + extra, w - 230, 18), refused ? cs::Cat("DOCKING REFUSED: ", market->Owner->Name, " will not let you in")
				: cs::Cat("WANTED by ", market->Owner->Name), small);
			// Fines are paid at the port's office even when it will not let the ship dock to trade.
			if (wantedHere && GUI::Button(Rect(r.xMax() - 210, r.y + 48 + extra, 196, 20), cs::Cat("Pay bounty  ", Credits(pilotRecord->BountyBy(market->Owner))), button) &&
				!CoopSends("bounty", K(market))) // US-T44 (US-T43): an entry in co-op
				PayBounty(market); // US-T44 (US-T43): the original's call, moved into PayBounty (Hud.Coop.cpp)
			GUI::contentColor = Dim;
		}
		float top = r.y + 50 + extra + lawRow;
		if (canTrade)
		{
			GUI::contentColor = Color::white();
			auto pilotShip = app->World()->PilotedShip;
			GUI::Label(Rect(r.x + 12, top, 200, 18), cs::Cat("Your credits: ", Credits(pilotShip->Faction->Treasury)), small);
			// Fill the tanks here: water or hydrocarbons and their helium-3, at this market's price.
			GUI::Label(Rect(r.x + 215, top, 110, 18), cs::Cat("Tanks ", Percent(pilotShip->PropellantFraction())), small);
			cs::String refuel = cs::Cat("Refuel  ~", Credits(Economy::PropellantPrice(market) * (pilotShip->PropellantCapacity() - pilotShip->Propellant())));
			if (GUI::Button(Rect(r.x + 300, top - 2, 140, 20), refuel, button)) ClickRefuel(market);
			GUI::Label(Rect(r.xMax() - 150, top, 40, 18), "Qty", small);
			if (GUI::Button(Rect(r.xMax() - 95, top - 2, 22, 20), "-", button)) tradeCrates = cs::Math::Max(1, tradeCrates - 1);
			GUI::Label(Rect(r.xMax() - 70, top, 30, 18), cs::Str(tradeCrates), small);
			if (GUI::Button(Rect(r.xMax() - 40, top - 2, 22, 20), "+", button)) tradeCrates++;
		}
		float y = top + (canTrade ? 20 : 0);
		if (surveyRow > 0)
		{
			int reports = 0;
			double offer = exploration->OfferAt(market, surveyHull, reports);
			GUI::contentColor = Color(0.6f, 0.85f, 1.0f);
			GUI::Label(Rect(r.x + 12, y, w - 240, 18), cs::Cat("Survey data aboard: ", surveyHeld, " report", surveyHeld == 1 ? "" : "s", ", worth ",
				Credits(exploration->ValueHeldBy(surveyHull)), !Exploration::IsCartographer(market) ? cs::String("   (no cartographer here)")
				: reports == 0 ? cs::Cat("   (", market->Owner->Name, "'s maps have it all)") : cs::String()), small);
			if (reports > 0 && GUI::Button(Rect(r.xMax() - 222, y - 2, 210, 20), cs::Cat("Sell ", reports, " to cartographer  ", Credits(offer)), button))
				ClickSellSurvey(market);
			y += surveyRow;
		}
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 12, y, 150, rowH), "Good", small);
		GUI::Label(Rect(r.x + 170, y, 80, rowH), market != nullptr ? "Stock" : "", small);
		GUI::Label(Rect(r.x + 250, y, 80, rowH), "Price", small);
		GUI::Label(Rect(r.x + 340, y, 80, rowH), market != nullptr ? "Flow/day" : "vs base", small);
		float hx = r.x + 430; // the history columns (US-T263)
		if (histW > 0)
		{
			GUI::Label(Rect(hx, y, 90, rowH), "90 days", small);
			GUI::Label(Rect(hx + 100, y, 125, rowH), "30 d low-high / avg", small);
			GUI::Label(Rect(hx + 230, y, 125, rowH), "90 d low-high / avg", small);
		}
		y += rowH;
		for (int g = 0; g < GoodInfo::Count; g++)
		{
			auto good = static_cast<Good>(g);
			auto info = GoodInfo::Of(good);
			double ratio = market != nullptr ? market->Price[g] / info->BasePrice : app->Economy()->PriceIndex(good);
			GUI::contentColor = Color::white();
			GUI::Label(Rect(r.x + 12, y, 160, rowH), info->Name, small);
			if (market != nullptr)
			{
				GUI::Label(Rect(r.x + 170, y, 80, rowH), Tonnes(market->Stock[g]), small);
				double net = market->Supply[g] - market->Demand[g];
				GUI::contentColor = net > 0 ? Color(0.55f, 0.9f, 0.6f) : net < 0 ? Color(1.0f, 0.7f, 0.45f) : Dim;
				GUI::Label(Rect(r.x + 340, y, 90, rowH), cs::Cat(net > 0 ? "+" : "", Tonnes(net)), small);
			}
			GUI::contentColor = ratio > 1.5 ? UiStyle::Danger : ratio < 0.6 ? Color(0.55f, 0.85f, 1.0f) : Color::white();
			cs::String price = cs::Fmt(market != nullptr ? market->Price[g] : info->BasePrice * ratio, "N0");
			GUI::Label(Rect(r.x + 250, y, 90, rowH), price, small);
			if (market == nullptr) GUI::Label(Rect(r.x + 340, y, 90, rowH), cs::Cat("x", cs::Fmt(ratio, "0.00")), small);
			if (histW > 0)
			{
				// Today's price against the month's average: dearer than usual warm, cheaper cool (US-T263).
				auto month = market->PriceRangeOver(good, 30), season = market->PriceRangeOver(good, Settlement::HistoryDays);
				double vs = month.VsMean(market->Price[g]);
				Color trend = month.Samples < 2 || cs::Math::Abs(vs) < 0.03 ? Color(0.7f, 0.75f, 0.85f) : vs > 0 ? Color(1.0f, 0.7f, 0.45f) : Color(0.55f, 0.85f, 1.0f);
				Sparkline(Rect(hx, y + 2, 90, rowH - 4), market->PriceTrail(good, Settlement::HistoryDays), trend);
				GUI::contentColor = trend;
				GUI::Label(Rect(hx + 100, y, 125, rowH), HistoryRangeText(month), small);
				GUI::contentColor = Dim;
				GUI::Label(Rect(hx + 230, y, 125, rowH), HistoryRangeText(season), small);
			}
			if (canTrade)
			{
				if (GUI::Button(Rect(r.x + 440 + histW, y - 1, 68, rowH), "Buy", button)) ClickBuy(market, good);
				if (GUI::Button(Rect(r.x + 515 + histW, y - 1, 68, rowH), "Sell", button)) ClickSell(market, good);
			}
			y += rowH;
		}
		GUI::contentColor = old;
	}

	cs::String Hud::StandingText(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& owner)
	{
		double s = r->StandingWith(owner);
		return cs::Cat("   ·   your standing ", s > 0 ? "+" : "", cs::Fmt(s, "0"),
			", prices x", cs::Fmt(Law::PriceFactor(r, owner), "0.00"));
	}

	void Hud::ClickBuy(const cs::Ref<Settlement>& market, Good good)
	{
		if (CoopSends("buy", K(market), static_cast<int>(good), tradeCrates)) return; // US-T44 (US-T43): an entry in co-op
		auto ship = app->World()->PilotedShip;
		int bought = 0;
		Economy::TradeOutcome stoppedBy{};
		auto outcome = app->Economy()->BuyIntoHold(ship, market, good, tradeCrates, bought, stoppedBy);
		if (bought > 0) CaptureEvents::Trade("buy", GoodInfo::Of(good)->Name, bought, market->Name); // US-T72
		if (bought > 0)
		{
			cs::String why;
			if (bought < tradeCrates)
				switch (stoppedBy)
				{
				case Economy::TradeOutcome::NoStock: why = cs::Cat("  (", market->Name, " ran out of stock)"); break;
				case Economy::TradeOutcome::CantAfford: why = "  (out of credits)"; break;
				case Economy::TradeOutcome::NoRoom: why = "  (cargo hold is full)"; break;
				default: why = ""; break;
				}
			app->ShowToast(cs::Cat("Bought ", bought, " crate", bought == 1 ? "" : "s", " of ", GoodInfo::Of(good)->Name, why));
		}
		else
		{
			cs::String message;
			switch (outcome)
			{
			case Economy::TradeOutcome::NoStock: message = cs::Cat(market->Name, " has none to sell"); break;
			case Economy::TradeOutcome::CantAfford: message = "Not enough credits"; break;
			case Economy::TradeOutcome::NoRoom: message = "Cargo hold is full"; break;
			case Economy::TradeOutcome::CannotDock: message = "Not welcome to trade here"; break;
			case Economy::TradeOutcome::Refused: message = cs::Cat("Docking refused: your standing with ", market->Owner->Name, " is too low"); break;
			case Economy::TradeOutcome::Contraband: message = "No lawful market will take stolen goods"; break;
			default: message = "Can't buy that right now"; break;
			}
			app->ShowToast(message);
		}
	}

	void Hud::ClickRefuel(const cs::Ref<Settlement>& market)
	{
		if (CoopSends("refuel", K(market))) return; // US-T44 (US-T43): an entry in co-op
		auto ship = app->World()->PilotedShip;
		double tonnes = 0, cost = 0;
		auto outcome = app->Economy()->RefuelAt(ship, market, tonnes, cost);
		cs::String message;
		switch (outcome)
		{
		case Economy::TradeOutcome::Ok:
			message = cs::Cat("Took on ", Tonnes(tonnes), " of propellant for ", Credits(cost),
				ship->PropellantFraction() < 0.99
					? cs::Cat("  (", market->Available(Good::Water) + market->Available(Good::Hydrocarbons) < 1 ? "the port ran out" : "credits ran out", ")")
					: cs::String());
			break;
		case Economy::TradeOutcome::NoRoom: message = "Tanks are already full"; break;
		case Economy::TradeOutcome::NoStock: message = cs::Cat(market->Name, " has no reaction mass to spare"); break;
		case Economy::TradeOutcome::CantAfford: message = "Not enough credits"; break;
		case Economy::TradeOutcome::CannotDock: message = "Not welcome to trade here"; break;
		case Economy::TradeOutcome::Refused: message = cs::Cat("Docking refused: your standing with ", market->Owner->Name, " is too low"); break;
		default: message = "Can't refuel right now"; break;
		}
		app->ShowToast(message);
	}

	void Hud::ClickSell(const cs::Ref<Settlement>& market, Good good)
	{
		if (CoopSends("sell", K(market), static_cast<int>(good), tradeCrates)) return; // US-T44 (US-T43): an entry in co-op
		auto ship = app->World()->PilotedShip;
		int sold = 0;
		auto outcome = app->Economy()->SellFromHold(ship, market, good, tradeCrates, sold);
		if (sold > 0) CaptureEvents::Trade("sell", GoodInfo::Of(good)->Name, sold, market->Name); // US-T72
		if (sold > 0)
			app->ShowToast(cs::Cat("Sold ", sold, " crate", sold == 1 ? "" : "s", " of ", GoodInfo::Of(good)->Name));
		else
		{
			cs::String message;
			switch (outcome)
			{
			case Economy::TradeOutcome::NothingToSell: message = "None of that in the hold"; break;
			case Economy::TradeOutcome::CannotDock: message = "Not welcome to trade here"; break;
			case Economy::TradeOutcome::Refused: message = cs::Cat("Docking refused: your standing with ", market->Owner->Name, " is too low"); break;
			case Economy::TradeOutcome::Contraband: message = "No lawful market will take stolen goods"; break;
			default: message = "Can't sell that right now"; break;
			}
			app->ShowToast(message);
		}
	}

	void Hud::ClickSellSurvey(const cs::Ref<Settlement>& market)
	{
		if (CoopSends("survey", K(market))) return; // an entry in co-op, as every trade at the board is
		auto ship = app->World()->PilotedShip;
		auto exploration = app->Galaxy != nullptr ? Exploration::Find(*app->Galaxy) : nullptr;
		if (ship == nullptr || exploration == nullptr || market == nullptr || !app->CanTradeNow(market)) return;
		int reports = 0;
		double paid = exploration->SellAt(app->World(), market, ship, app->Clock->Jd, reports);
		app->ShowToast(reports > 0
			? cs::Cat("Sold ", reports, " survey report", reports == 1 ? "" : "s", " to ", market->Name, "'s cartographers for ", Credits(paid),
				": ", market->Owner->Name, "'s maps gain ", reports == 1 ? "it" : "them")
			: cs::String("The cartographers here have nothing to buy"));
	}

	// ---------------------------------------------------------------- outfitting (U.SPC-T65)

	void Hud::DrawOutfit()
	{
		UiMotion::Scope motion("hud.outfit"); // US-T255
		auto ship = app->World()->PilotedShip;
		constexpr float w = 600;
		if (ship == nullptr || ship->Combat == nullptr)
		{
			auto r0 = Panel(Rect((Screen::width - w) * 0.5f, 190, w, 52));
			GUI::Label(Rect(r0.x + 12, r0.y + 6, w - 24, 22), "OUTFITTING", title);
			GUI::Label(Rect(r0.x + 12, r0.y + 28, w - 24, 18), "Pilot a ship (P) and bring it to a shipyard to refit it.   F2 to close", small);
			return;
		}
		cs::Ref<Settlement> yard = nullptr, port = nullptr;
		for (const auto& st : app->Economy()->Settlements)
		{
			if (!st->Discovered() || !app->CanTradeNow(st)) continue;
			if (port == nullptr) port = st;
			if (yard == nullptr && Outfitting::IsYard(st)) yard = st;
		}
		auto loadout = Outfitting::LoadoutOf(ship);
		auto spec = ship->Combat->Spec;
		constexpr float rowH = 18;
		// Rows: the hardpoints, then the facilities, then the free space.
		int hardpoints = loadout->Slots.Count(), facilities = loadout->Facilities.Count(), rows = hardpoints + facilities + 1;
		int options = 0;
		if (yard != nullptr && outfitSlot >= 0 && outfitSlot < rows) options = OutfitOptions(loadout, outfitSlot).Count();
		else outfitSlot = -1;
		float listH = Mathf::Min(rows * rowH, 230.0f);
		float h = 112 + listH + (options > 0 ? 26 + options * rowH : 0);
		auto r = Panel(Rect((Screen::width - w) * 0.5f, 190, w, h));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 22), cs::Cat("OUTFITTING  ·  ", ship->Name, "  ·  ",
			yard != nullptr ? yard->Name : port != nullptr ? cs::Cat(port->Name, " (no yard)") : cs::String("not alongside a port")), title);
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 12, r.y + 28, w - 24, 18),
			cs::Cat("Hull ", cs::Fmt(spec->Hull, "N0"), "  ·  shield ", cs::Fmt(spec->Shield, "N0"),
				"  ·  armour ", cs::Fmt(spec->Armor, "0"), "  ·  dry mass ", Tonnes(spec->DryMassTonnes),
				"  ·  delta-v ", cs::Fmt(ship->DeltaVBudgetKmS(), "N0"), " km/s  ·  scan ", cs::Fmt(Outfitting::ScanRangeKm(ship) / 1e6, "0.#"), " M km"), small);
		GUI::contentColor = Color::white();
		GUI::Label(Rect(r.x + 12, r.y + 46, 220, 18), cs::Cat("Credits: ", Credits(ship->Faction->Treasury)), small);
		double brig = Outfitting::BrigCapacity(ship);
		if (brig > 0 || ship->Prisoners > 0)
		{
			GUI::Label(Rect(r.x + 230, r.y + 46, 200, 18), cs::Cat("Brig ", cs::Fmt(ship->Prisoners, "0"), " / ", cs::Fmt(brig, "0"), " prisoners"), small);
			if (ship->Prisoners > 0 && port != nullptr && GUI::Button(Rect(r.xMax() - 170, r.y + 44, 158, 20), cs::Cat("Hand over  +", Credits(ship->PrisonerBounty)), button) &&
				!CoopSends("prisoners", K(port))) // US-T44 (US-T43): an entry in co-op
				HandOverPrisoners(ship, port); // US-T44 (US-T43): the original's lines, moved into HandOverPrisoners (Hud.Coop.cpp)
		}
		// A jump ring for a small craft (US-T86): docked to it, the craft jumps between the stars like a capital hull.
		bool ringable = yard != nullptr && CruiseDrives::JumpRingRefusal(ship).empty();
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 12, r.y + 64, w - (ringable ? 224 : 24), 18), yard != nullptr
			? (ringable ? "Pick a hardpoint, a facility or the free space, then what to fit.   F2 to close"
				: "Pick a hardpoint, a facility or the free space, then what to fit.   Prices are this yard's market.   F2 to close")
			: "Bring the ship alongside a shipyard (or a haven) to refit.   F2 to close", small);
		if (ringable)
		{
			GUI::contentColor = Color::white();
			cs::String ringLabel = ship->JumpRing ? cs::Cat("Let jump ring go  +", Credits(CruiseDrives::JumpRingPrice / 2))
				: cs::Cat("Dock a jump ring  ", Credits(CruiseDrives::JumpRingPrice));
			if (GUI::Button(Rect(r.xMax() - 208, r.y + 62, 196, 20), ringLabel, ship->JumpRing ? buttonActive : button))
			{
				if (ship->JumpRing)
				{
					ship->JumpRing = false;
					ship->Faction->Treasury += CruiseDrives::JumpRingPrice / 2;
					app->ShowToast(cs::Cat("Jump ring let go at ", yard->Name, ": the ", ship->Name, " goes by the gates again"));
				}
				else if (ship->Faction->Treasury < CruiseDrives::JumpRingPrice)
					app->ShowToast(cs::Cat("Not enough credits for a jump ring: ", Credits(CruiseDrives::JumpRingPrice)));
				else
				{
					ship->Faction->Treasury -= CruiseDrives::JumpRingPrice;
					ship->JumpRing = true;
					auto ring = CruiseDrives::JumpRing();
					app->ShowToast(cs::Cat("JUMP RING docked at ", yard->Name, ": fly beyond the rim, nose on a star, Enter to spool (", cs::Fmt(ring->MaxJumpLy, "0"),
						" ly a jump, ", cs::Fmt(ring->RatedSpoolDays * 24, "0"), " h spool, ", cs::Fmt(ring->RatedCooldownDays * 24, "0"), " h cooldown)"));
				}
			}
		}

		// The hardpoints and what is on them, the facilities inside the hull, and the space left.
		auto view = Rect(r.x + 8, r.y + 84, w - 16, listH);
		auto content = Rect(0, 0, w - 36, rows * rowH);
		outfitScroll = GUI::BeginScrollView(view, outfitScroll, content);
		for (int i = 0; i < rows; i++)
		{
			cs::String rowLabel;
			if (i < hardpoints)
			{
				auto slot = loadout->Slots[i];
				cs::String fitted = loadout->Fitted[i].has_value() ? loadout->PartAt(i, *loadout->Fitted[i])->PartName() : cs::String("(empty)");
				if (slot->LayoutIndex >= 0 && slot->LayoutIndex < ship->Combat->Subsystems.Count())
				{
					auto sub = ship->Combat->Subsystems[slot->LayoutIndex];
					fitted += sub->Destroyed() ? cs::String("  (shot away)") : sub->Health() < 0.99 ? cs::Cat("  (", Percent(sub->Health()), ")") : cs::String();
				}
				if (slot->Stock.has_value() && loadout->Fitted[i] != slot->Stock)
					fitted += cs::Cat("   was ", cs::Strings::ToLowerInvariant(loadout->PartAt(i, *slot->Stock)->PartName()));
				// Each mount's type and size (U.SPC-T508), and where a turret mount takes a spinal part (a railgun, a tube,
				// the lance): one bearing on the bow, or a fixed tube.
				cs::String where = slot->IsWeapon() && slot->Type != MountType::Spinal && slot->Spinal && slot->Size != HardpointSize::Small
					? cs::String(slot->BearsOnBow ? " (bow)" : " (fixed)") : cs::String();
				rowLabel = cs::Cat("  ", slot->Name(), where, "   ", fitted);
			}
			else if (i < hardpoints + facilities)
			{
				auto f = loadout->Facilities[i - hardpoints];
				rowLabel = cs::Cat("  Facility   ", f->Name, "   ", FacilityCost(f->Def(), f->Size()), loadout->IsStockFacility(f) ? "" : "   fitted");
			}
			// The space, reactor power and hands left for fittings (U.SPC-T521).
			else rowLabel = cs::Cat("  Free internal space   ", Sections(loadout->FreeSpace()), " of ", Sections(loadout->InternalSpace),
				"   ·   reactor ", Megawatts(loadout->FreePowerMw()), " of ", Megawatts(loadout->ReactorMw()),
				"   ·   crew ", cs::Fmt(loadout->FreeCrew(), "0"), " of ", cs::Fmt(loadout->FittingCrew(), "0"), " spare");
			auto rr = Rect(0, i * rowH, content.width, rowH - 1);
			if (GUI::Button(rr, rowLabel, i == outfitSlot ? rowSelected : row))
				outfitSlot = i == outfitSlot ? -1 : i;
		}
		GUI::EndScrollView();

		if (options == 0) { GUI::contentColor = old; return; }
		float y = r.y + 92 + listH;
		GUI::contentColor = Accent;
		cs::String into = outfitSlot < hardpoints ? cs::Cat("Fit onto ", cs::Strings::ToLowerInvariant(loadout->Slots[outfitSlot]->Name()), ":")
			: outfitSlot < hardpoints + facilities ? cs::Cat(loadout->Facilities[outfitSlot - hardpoints]->Name, ":") : cs::String("Fit into the free space:");
		GUI::Label(Rect(r.x + 12, y, w - 24, 18), into, small);
		y += 22;
		for (const auto& option : OutfitOptions(loadout, outfitSlot))
		{
			OutfitOption(ship, yard, loadout, option, r.x, y, w, rowH);
			y += rowH;
		}
		GUI::contentColor = old;
	}

	cs::String Hud::Sections(double n) { return cs::Cat(cs::Fmt(n, "0.#"), cs::Math::Abs(n - 1) < 1e-9 ? " section" : " sections"); }

	cs::String Hud::Megawatts(double mw) { return cs::Cat(cs::Fmt(mw, "0"), " MW"); }

	cs::String Hud::FacilityCost(const cs::Ref<FacilityKind>& k, double size)
	{
		cs::String s = Sections(size);
		if (k == nullptr) return s;
		if (k->PowerMw > 0) s += cs::Cat(", ", Megawatts(k->PowerMw));
		if (k->Crew > 0) s += cs::Cat(", ", cs::Fmt(k->Crew, "0"), " crew");
		return s;
	}

	cs::List<Hud::OutfitChoice> Hud::OutfitOptions(const cs::Ref<Loadout>& loadout, int rowIndex)
	{
		auto list = cs::List<OutfitChoice>();
		int hardpoints = loadout->Slots.Count(), facilities = loadout->Facilities.Count();
		if (rowIndex < hardpoints)
		{
			for (const auto& m : ModuleSpec::Catalogue()) if (Outfitting::CanFit(loadout, rowIndex, m->Kind)) list.Add(OutfitChoice{ m->Kind, nullptr, loadout->PartAt(rowIndex, m->Kind)->PartName() });
		}
		else if (rowIndex < hardpoints + facilities)
		{
			if (loadout->CanRemove(rowIndex - hardpoints)) list.Add(OutfitChoice{ std::nullopt, nullptr, "Strip it out" });
		}
		else
			for (const auto& k : FacilityKinds::Catalogue())
			{
				// One the hull has room for but not the reactor power or the hands (U.SPC-T521) is listed too, with why.
				auto can = loadout->CanAdd(k);
				cs::String why = can == OutfitOutcome::NoPower ? "  · not enough power" : can == OutfitOutcome::NoCrew ? "  · not enough crew" : "";
				if (can == OutfitOutcome::Ok || why.size() > 0) list.Add(OutfitChoice{ std::nullopt, k, cs::Cat(k->Name, "  (", FacilityCost(k, k->Size), ")", why) });
			}
		return list;
	}

	void Hud::OutfitOption(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, const cs::Ref<Loadout>& loadout, const OutfitChoice& option,
		float x, float y, float w, float rowH)
	{
		int hardpoints = loadout->Slots.Count();
		bool onHardpoint = outfitSlot < hardpoints, strip = !onHardpoint && option.facility == nullptr;
		bool current = onHardpoint && loadout->Fitted[outfitSlot] == option.module;
		auto q = onHardpoint ? Outfitting::Quote(ship, yard, outfitSlot, option.module)
			: strip ? Outfitting::QuoteStrip(ship, yard, outfitSlot - hardpoints)
			: Outfitting::QuoteFacility(ship, yard, option.facility);
		GUI::contentColor = current ? Accent : Color::white();
		GUI::Label(Rect(x + 20, y, 200, rowH), option.name, small);
		GUI::contentColor = Dim;
		// Only the greatest yards build a tachyon lance (U.SPC-T487).
		bool builds = !option.module.has_value() || ModuleSpec::YardBuilds(*option.module, yard);
		cs::String price = current ? cs::String("fitted")
			: !builds ? cs::String("this yard cannot build it")
			: !q.InStock ? cs::Cat("yard is short of ", cs::Strings::ToLowerInvariant(GoodInfo::Of(q.Short)->Name))
			: q.Net() >= 0 ? Credits(q.Net()) : cs::Cat("+", Credits(-q.Net()), " back");
		GUI::Label(Rect(x + 220, y, 250, rowH), price, small);
		GUI::contentColor = Color::white();
		if (current || !q.InStock || !builds) return;
		if (!GUI::Button(Rect(x + w - 100, y - 1, 84, rowH - 1), strip ? "Strip" : "Fit", button)) return;
		if (CoopSendsOutfit(yard, option)) return; // US-T44 (US-T43): an entry in co-op
		OutfitApply(ship, yard, loadout, option); // US-T44 (US-T43): the original's lines below, moved into OutfitApply so a frame can run them too
	}

	void Hud::OutfitApply(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, const cs::Ref<Loadout>& loadout, const OutfitChoice& option)
	{
		int hardpoints = loadout->Slots.Count();
		bool onHardpoint = outfitSlot < hardpoints, strip = !onHardpoint && option.facility == nullptr;
		cs::String what = strip ? loadout->Facilities[outfitSlot - hardpoints]->Name : option.name;
		OutfitQuote paid;
		auto outcome = onHardpoint ? Outfitting::Fit(app->Economy(), ship, yard, outfitSlot, option.module, paid)
			: strip ? Outfitting::StripFacility(app->Economy(), ship, yard, outfitSlot - hardpoints, paid)
			: Outfitting::FitFacility(app->Economy(), ship, yard, option.facility, paid);
		if (outcome == OutfitOutcome::Ok && strip) outfitSlot = -1;
		cs::String message;
		switch (outcome)
		{
		case OutfitOutcome::Ok:
			message = cs::Cat(strip ? cs::Cat(what, " stripped out") : cs::Cat(onHardpoint ? option.name : option.facility->Name, " fitted"),
				paid.Net() >= 0 ? cs::Cat(" for ", Credits(paid.Net())) : cs::Cat(", ", Credits(-paid.Net()), " back"));
			break;
		case OutfitOutcome::CantAfford: message = "Not enough credits"; break;
		case OutfitOutcome::NoStock: message = cs::Cat(yard->Name, " is short of the materials"); break;
		case OutfitOutcome::NoRoom: message = "Not enough internal space: strip something out first"; break;
		case OutfitOutcome::NoPower: message = "Not enough reactor power to spare: strip something that draws on it first"; break;
		case OutfitOutcome::NoCrew: message = "Not enough crew to spare to man it"; break;
		case OutfitOutcome::HoldTooFull: message = "Empty the hold before taking the racks out"; break;
		case OutfitOutcome::BrigOccupied: message = "Hand over the prisoners before taking the brig out"; break;
		case OutfitOutcome::Busy: message = "Not while fighting or boarded"; break;
		case OutfitOutcome::CannotDock: message = "Not welcome to refit here"; break;
		case OutfitOutcome::YardCannotBuild: message = cs::Cat(yard->Name, " cannot build that"); break;
		default: message = "Can't fit that here"; break;
		}
		app->ShowToast(message);
	}

	// ---------------------------------------------------------------- spawn (dev/sandbox)

	const cs::List<ShipClass>& Hud::SpawnClasses()
	{
		// Enum.GetValues(typeof(ShipClass)): every member, in value order (7, the old Breacher hull, is unused).
		static const cs::List<ShipClass> classes
		{
			ShipClass::Carrier, ShipClass::Battleship, ShipClass::Cruiser, ShipClass::Destroyer, ShipClass::Freighter, ShipClass::Miner,
			ShipClass::Courier, ShipClass::Fighter, ShipClass::Explorer, ShipClass::Gunship, ShipClass::OreTug, ShipClass::Interceptor,
			ShipClass::Bomber, ShipClass::LaneTender, ShipClass::GatePicket, ShipClass::CommandCruiser, ShipClass::Frigate,
			ShipClass::Corvette, ShipClass::Recon, ShipClass::AssaultShuttle, ShipClass::GateBreaker, ShipClass::CaptureTug,
		};
		return classes;
	}

	void Hud::DrawSpawn()
	{
		UiMotion::Scope motion("hud.spawn", 0); // US-T255: the whole screen, faded in
		GUI::Box(Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height)), GUIContent::none(), panel);
		RegisterUi(Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height)));

		GUI::Label(Rect(40, 24, 500, 30), "SPAWN & PILOT", title);
		GUI::contentColor = Dim;
		GUI::Label(Rect(40, 56, 700, 20), "Click a hull to spawn and pilot it at the camera   ·   V to close", small);
		GUI::contentColor = Color::white();

		if (GUI::Button(Rect(Screen::width - 210, 28, 160, 26), spawnHostile ? "FACTION: HOSTILE" : "FACTION: HOME", spawnHostile ? buttonActive : button))
			spawnHostile = !spawnHostile;
		// U.SPC-T601: the system's one practice fighter, at the camera, the player at its helm (Shift+V does the same).
		if (GUI::Button(Rect(Screen::width - 470, 28, 250, 26), "PRACTICE FIGHTER HERE  (Shift+V)", button))
			app->SpawnPracticeFighter();
		// US-T88: the dogfight demo, at a light fighter's helm (Shift-click: a heavy fighter's) against four pirates.
		if (GUI::Button(Rect(Screen::width - 730, 28, 250, 26), "DOGFIGHT DEMO  (Shift: heavy)", button))
			app->StartDogfightDemo(Event::current().shift);
		if (GUI::Button(Rect(Screen::width - 40, 24, 30, 30), "X", button))
			app->ShowSpawn = false;

		auto faction = spawnHostile ? app->World()->Pirates : app->World()->Sol;
		constexpr float cell = 176.0f, gap = 20.0f, top = 96.0f, labelH = 30.0f;
		int perRow = Mathf::Max(1, Mathf::FloorToInt((Screen::width - 80 + gap) / (cell + gap)));
		float gridW = perRow * (cell + gap) - gap;
		float startX = (Screen::width - gridW) * 0.5f;

		for (int i = 0; i < SpawnClasses().Count(); i++)
		{
			auto cls = SpawnClasses()[i];
			int col = i % perRow, line = i / perRow;
			auto r = Rect(startX + col * (cell + gap), top + line * (cell + labelH + gap), cell, cell + labelH);
			bool hover = r.Contains(Event::current().mousePosition);
			GUI::Box(r, GUIContent::none(), hover ? rowSelected : row);
			auto tex = ::SpaceGod::Rendering::ShipThumbnails::For(cls, faction);
			if (tex != nullptr) GUI::DrawTexture(Rect(r.x + 6, r.y + 6, cell - 12, cell - 12), tex, ScaleMode::ScaleToFit);
			GUI::Label(Rect(r.x, r.yMax() - labelH, cell, labelH), ShipClassSpec::Of(cls)->DisplayName, title);
			if (GUI::Button(r, GUIContent::none(), GUIStyle::none()))
			{
				app->SpawnShip(cls, spawnHostile);
				app->ShowSpawn = false;
			}
		}
	}

	// ---------------------------------------------------------------- cargo hold

	void Hud::DrawCargo()
	{
		UiMotion::Scope motion("hud.cargo"); // US-T255
		auto ship = app->World()->PilotedShip;
		if (ship == nullptr)
		{
			cargoSelected = nullptr;
			return;
		}
		auto hold = ship->PersonalHold();
		if (cargoSelectedHold != hold) cargoSelected = nullptr;
		cargoSelectedHold = hold;
		// A picked-up crate that has left the hold (sold at the market above, say) is no longer held.
		if (cargoSelected != nullptr && !InHold(hold, cargoSelected)) cargoSelected = nullptr;
		constexpr float cell = 28.0f;
		float w = cs::Math::Max(260, hold->Width * cell + 24);
		float h = cargoOpen ? 74 + hold->Height * cell + (cargoSelected != nullptr ? 28 : 0) : 34;
		auto r = Panel(Rect(SafeLeft() + 10, Screen::height - h - 56, w, h));

		if (GUI::Button(Rect(r.x + 8, r.y + 6, w - 16, 22), UiStyle::Fold(cargoOpen, cs::Cat("CARGO HOLD", UiStyle::Glyph::Separator, ship->Name)), title)) cargoOpen = !cargoOpen;
		if (!cargoOpen) return;
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 12, r.y + 28, w - 24, 32),
			cargoSelected != nullptr ? "Click a cell to move the crate there  (click it again to cancel)" : "Click a crate to pick it up   [I to close]", small);
		GUI::contentColor = Color::white();

		// C#'s CargoItem[hold.Width, hold.Height]: [x, y] is atCell[x * hold->Height + y].
		auto atCell = cs::NewArray<cs::Ref<CargoItem>>(hold->Width * hold->Height);
		for (const auto& item : hold->Items())
			for (int dx = 0; dx < item->Width(); dx++)
				for (int dy = 0; dy < item->Height(); dy++)
					atCell[(item->X + dx) * hold->Height + (item->Y + dy)] = item;

		float gx = r.x + 12, gy = r.y + 48;
		for (int y = 0; y < hold->Height; y++)
		{
			for (int x = 0; x < hold->Width; x++)
			{
				auto occupant = atCell[x * hold->Height + y];
				auto cellRect = Rect(gx + x * cell, gy + y * cell, cell - 2, cell - 2);
				cs::String cellLabel = occupant != nullptr ? GoodAbbreviation(occupant->Good) : cs::String();
				bool isSelected = occupant != nullptr && occupant == cargoSelected;
				if (GUI::Button(cellRect, cellLabel, isSelected ? buttonActive : button))
					ClickCargoCell(hold, x, y, occupant);
			}
		}

		if (cargoSelected != nullptr)
		{
			float by = gy + hold->Height * cell + 6;
			if (GUI::Button(Rect(r.x + 12, by, 90, 22), "Rotate", button))
				hold->Move(cargoSelected, cargoSelected->X, cargoSelected->Y, !cargoSelected->Rotated);
		}
	}

	bool Hud::InHold(const cs::Ref<CargoHold>& hold, const cs::Ref<CargoItem>& item)
	{
		for (const auto& held : hold->Items())
			if (held == item) return true;
		return false;
	}

	void Hud::ClickCargoCell(const cs::Ref<CargoHold>& hold, int x, int y, const cs::Ref<CargoItem>& occupant)
	{
		if (cargoSelected == nullptr)
		{
			if (occupant != nullptr) cargoSelected = occupant;
			return;
		}
		if (occupant == cargoSelected)
		{
			cargoSelected = nullptr;
			return;
		}
		if (hold->Move(cargoSelected, x, y, cargoSelected->Rotated)) cargoSelected = nullptr;
		else app->ShowToast("Doesn't fit there");
	}

	cs::String Hud::GoodAbbreviation(Good g)
	{
		cs::String name = GoodInfo::Of(g)->Name;
		return static_cast<int>(name.size()) <= 3 ? name : cs::Strings::Substring(name, 0, 3);
	}

	cs::String Hud::Tonnes(double t)
	{
		double a = cs::Math::Abs(t);
		if (a >= 1e6) return cs::Cat(cs::Fmt(t / 1e6, "0.0"), " Mt");
		if (a >= 1e4) return cs::Cat(cs::Fmt(t / 1e3, "0"), " kt");
		return cs::Cat(cs::Fmt(t, "N0"), " t");
	}

	cs::String Hud::Credits(double c)
	{
		double a = cs::Math::Abs(c);
		if (a >= 1e9) return cs::Cat(cs::Fmt(c / 1e9, "0.00"), " B cr");
		if (a >= 1e6) return cs::Cat(cs::Fmt(c / 1e6, "0.0"), " M cr");
		if (a >= 1e3) return cs::Cat(cs::Fmt(c / 1e3, "0"), " k cr");
		return cs::Cat(cs::Fmt(c, "0"), " cr");
	}

	cs::String Hud::People(double p)
	{
		return p >= 1e6 ? cs::Cat(cs::Fmt(p / 1e6, "0.0"), " M") : cs::Cat(cs::Fmt(p / 1e3, "0"), " k");
	}

	cs::String Hud::SpaceText(const cs::Ref<Compartment>& c)
	{
		cs::String who = c->Taken ? cs::Cat("TAKEN, ", cs::Fmt(c->Boarders, "0"), " boarders")
			: c->Contested() ? cs::Cat("FIGHTING: ", cs::Fmt(c->Boarders, "0"), " boarders v ", cs::Fmt(c->Marines + c->Crew, "0"))
			: cs::Cat(cs::Fmt(c->Crew + c->Marines, "0"), " aboard");
		return cs::Cat(who, c->Vented ? (c->SealIn > 0 ? "  ·  VENTING" : "  ·  sealed, in vacuum") : "");
	}

	cs::String Hud::OrderText(const cs::Ref<GroupOrder>& o)
	{
		cs::String where = o->Body >= 0 ? (*app->Catalog())[o->Body]->Name : cs::String();
		switch (o->Kind)
		{
		case OrderKind::Ambush: return cs::Cat("Lie in wait on the hyperlanes (toward ", where, ")");
		case OrderKind::Attack: return cs::Cat("Attack ", where);
		case OrderKind::Defend: return cs::Cat("Defend ", where);
		case OrderKind::Siege: return cs::Cat("Besiege ", where);
		case OrderKind::Repair: return cs::Cat("Refit at ", where);
		case OrderKind::Raid: return cs::Cat("Raid shipping at ", where);
		case OrderKind::Hunt: return cs::Cat("Hunt pirates at ", where);
		case OrderKind::GateWatch: return cs::Cat("Lie in wait off the jump gates (", where, ")");
		case OrderKind::HoldGate: return cs::Cat("Hold the jump gates against raiders (", where, ")");
		case OrderKind::Assault: return cs::Cat("Storm ", where);
		case OrderKind::Blockade: return cs::Cat("Blockade ", where);
		case OrderKind::Escort: return cs::Cat("Escort, at ", where);
		case OrderKind::Explore: return cs::Cat("Explore, next ", where);
		default: return cs::Cat("Patrol, next ", where);
		}
	}

	cs::String Hud::Percent(double f) { return cs::Cat(cs::Fmt(cs::Math::Max(0, f) * 100, "0"), "%"); }

	void Hud::DrawTicker()
	{
		const auto& events = app->World()->Events;
		int n = cs::Math::Min(4, events.Count());
		if (n == 0) return;
		// US-T255: when the newest report came in (told apart by its date and text: the list is trimmed at its cap).
		static cs::String tickerNewest;
		static double tickerArrivedAt = 0;
		{
			const auto& newest = events[events.Count() - 1];
			cs::String key = cs::Cat(cs::Fmt(newest.Jd, "F6"), " ", newest.Text);
			if (key != tickerNewest)
			{
				tickerNewest = key;
				tickerArrivedAt = UiMotion::Now();
			}
		}
		float w = 640, rowH = 17;
		auto r = Panel(newsRect); // TopCentre's first slot, taken in LayoutNews (US-T154)
		auto old = GUI::contentColor;
		for (int i = 0; i < n; i++)
		{
			const auto& e = events[events.Count() - n + i];
			GUI::contentColor = KindColor(e.Kind);
			if (!tickerStyle)
			{
				tickerStyle = cs::New<GUIStyle>(*small);
				tickerStyle->wordWrap = false;
				tickerStyle->clipping = TextClipping::Clip;
			}
			// US-T255: the newest report fades and rises into its row as it comes in.
			UiMotion::Scope arriving(i == n - 1 ? UiMotion::Enter(static_cast<float>(UiMotion::Now() - tickerArrivedAt)) : 1.0f, rowH);
			GUI::Label(Rect(r.x + 10, r.y + 5 + i * rowH, w - 20, rowH), NewsLine(e), tickerStyle);
		}
		GUI::contentColor = old;
	}

	Color Hud::KindColor(WorldEventKind k)
	{
		switch (k)
		{
		case WorldEventKind::Battle: return Color(1.0f, 0.7f, 0.6f);
		case WorldEventKind::Capture: return Color(1.0f, 0.85f, 0.4f);
		case WorldEventKind::Opportunity: return Color(0.65f, 1.0f, 0.75f);
		case WorldEventKind::Diplomacy: return Color(0.7f, 0.9f, 1.0f);
		case WorldEventKind::Economy: return Color(1.0f, 0.72f, 0.4f);
		case WorldEventKind::Market: return Color(0.95f, 0.95f, 0.55f);
		default: return Dim;
		}
	}

	cs::String Hud::NewsLine(const WorldEvent& e)
	{
		return app->Clock->Utc().Year() == Astro::ToDateTime(e.Jd).Year()
			? cs::Cat(Astro::ToDateTime(e.Jd).ToString("MMM dd"), "   ", e.Text)
			: cs::Cat(Astro::ToDateTime(e.Jd).ToString("yyyy MMM dd"), "   ", e.Text);
	}

	// ---------------------------------------------------------------- news feed

	const cs::List<cs::String>& Hud::NewsFilters()
	{
		static const cs::List<cs::String> filters{ "ALL", "MARKETS", "WAR", "SHIPS & OTHER" };
		return filters;
	}

	bool Hud::InFilter(WorldEventKind k, int filter)
	{
		switch (filter)
		{
		case 1: return k == WorldEventKind::Economy || k == WorldEventKind::Market || k == WorldEventKind::Diplomacy;
		case 2: return k == WorldEventKind::Battle || k == WorldEventKind::Capture || k == WorldEventKind::Loss || k == WorldEventKind::Diplomacy;
		case 3: return k == WorldEventKind::Info || k == WorldEventKind::Launch || k == WorldEventKind::Opportunity;
		default: return true;
		}
	}

	void Hud::DrawNews()
	{
		UiMotion::Scope motion("hud.news"); // US-T255
		constexpr float w = 760;
		float h = Mathf::Min(Screen::height * 0.5f, 420.0f);
		auto r = Panel(newsRect); // TopCentre's first slot, taken in LayoutNews (US-T154)
		const auto& events = app->World()->Events;
		if (GUI::Button(Rect(r.x + 8, r.y + 6, 120, 22), UiStyle::Fold(true, "NEWS"), title)) app->ShowNews = false;
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 130, r.y + 9, w - 140, 18), "as it happens  ·  click a report to select where it happened  ·  X to close", small);
		GUI::contentColor = old;
		for (int i = 0; i < NewsFilters().Count(); i++)
			if (GUI::Button(Rect(r.x + 10 + i * 128, r.y + 32, 122, 20), NewsFilters()[i], newsFilter == i ? buttonActive : button))
			{
				newsFilter = i;
				newsScroll = Vector2::zero();
			}

		if (!newsRow)
		{
			newsRow = cs::New<GUIStyle>(*row);
			newsRow->fontSize = UiStyle::Type::Caption;
			newsRow->wordWrap = true;
			newsRow->padding = RectOffset(UiStyle::Space::TextPad, 4, 2, 2);
		}
		newsShown.Clear();
		for (int i = events.Count() - 1; i >= 0 && newsShown.Count() < 200; i--)
			if (InFilter(events[i].Kind, newsFilter)) newsShown.Add(events[i]);

		auto view = Rect(r.x + 4, r.y + 58, w - 8, h - 62);
		float width = view.width - 20, total = 0;
		auto heights = cs::NewArray<float>(newsShown.Count());
		for (int i = 0; i < newsShown.Count(); i++)
		{
			heights[i] = newsRow->CalcHeight(GUIContent(NewsLine(newsShown[i])), width);
			total += heights[i];
		}
		newsScroll = GUI::BeginScrollView(view, newsScroll, Rect(0, 0, width, Mathf::Max(total, 1.0f)));
		float y = 0;
		for (int i = 0; i < newsShown.Count(); i++)
		{
			const auto& e = newsShown[i];
			auto rr = Rect(0, y, width, heights[i]);
			y += heights[i];
			GUI::contentColor = KindColor(e.Kind);
			if (e.Body < 0 || e.Body >= app->Catalog()->Count())
			{
				GUI::Label(rr, NewsLine(e), newsRow);
				continue;
			}
			if (GUI::Button(rr, NewsLine(e), newsRow))
			{
				app->Select(Target::OfBody(e.Body));
				if (e.Kind == WorldEventKind::Economy || e.Kind == WorldEventKind::Market) app->ShowMarket = true;
			}
		}
		GUI::contentColor = old;
		if (newsShown.Count() == 0) GUI::Label(Rect(8, 4, width, 18), "Nothing to report yet.", small);
		GUI::EndScrollView();
	}

	cs::String Hud::NewsAge(const cs::Ref<StarSystem>& system, double jd)
	{
		cs::Ref<SystemReport> r = system->Settled() && app->Galaxy->Traffic != nullptr ? app->Galaxy->Traffic->News->Known(app->ActiveSystem, system) : nullptr;
		return r == nullptr ? cs::String() : cs::Cat("  · news ", LightSpeed::Describe(r->AgeDays(jd)), " old");
	}

	cs::String Hud::JourneyText(const cs::Ref<Ship>& s)
	{
		auto names = cs::List<cs::String>();
		for (const auto& hop : s->Journey) names.Add(hop.Lane->Name);
		return cs::Strings::Join(", ", names);
	}

	cs::String Hud::RouteText(const cs::Ref<Ship>& s)
	{
		if (s->Route.Count() == 0) return ""; // C#'s null route
		auto names = cs::NewArray<cs::String>(s->Route.Count());
		for (int i = 0; i < names.Count(); i++) names[i] = (*app->Catalog())[s->Route[i]]->Name;
		return cs::Strings::Join(" › ", names);
	}

	void Hud::DrawStatus()
	{
		auto cam = app->SpaceCamera;
		cs::String s;
		// US-T155: the key list goes with the facts only while the whole line fits the bottom band; past that it is cut to
		// "H all keys" (Help and ControlsHelp list every one), and the facts themselves are shortened with an ellipsis.
		cs::String keys, shortKeys;
		if (cam->Mode == Cameras::SpaceCamera::CameraMode::Focus && cam->PilotLocked)
		{
			auto pilotRecord = app->World()->Law != nullptr ? app->World()->Law->PlayerRecord() : nullptr; // the embodied pilot's (US-T148)
			s = cs::Cat("PILOTING  ·  ", TargetName(cam->FocusTarget),
				app->Piloted.Ship != nullptr ? cs::Cat("  ·  PROPELLANT ", app->Piloted.Ship->Propellant() <= 1e-6 ? cs::String("DRY") : Percent(app->Piloted.Ship->PropellantFraction())) : cs::String(),
				pilotRecord != nullptr && pilotRecord->Wanted() ? cs::Cat("  ·  WANTED ", Credits(pilotRecord->TotalBounty())) : cs::String(),
				SensorText());
			keys = cs::Cat("   (", ControlHint(app->PilotControlMode), ", Z/X roll, F flight assist, Shift boost, F3 silent running, P to release)");
			shortKeys = "   (H all keys, P to release)";
		}
		else if (cam->Mode == Cameras::SpaceCamera::CameraMode::Focus)
		{
			s = cs::Cat("FOCUS  ·  ", TargetName(cam->FocusTarget));
			keys = "   (WASD to swim free, wheel to zoom, right-drag to orbit)";
			shortKeys = "   (H all keys)";
		}
		else if (cam->Mode == Cameras::SpaceCamera::CameraMode::Travel)
		{
			s = cs::Cat("TRAVELLING  ·  ", TargetName(cam->FocusTarget));
		}
		else
		{
			double kmPerSec = SceneSpeedToKmS(cam->CurrentSpeed);
			s = cs::Cat("FREE  ·  swim x", cs::Fmt(cam->SpeedMultiplier, "0.##"), "  ·  ", SpeedText(kmPerSec),
				"  ·  near ", (*app->Catalog())[cam->FrameBody()]->Name);
		}
		// The band the bottom line has (HudLayout::BottomBand): beside the right-hand column, less the tanker call and the
		// reorient toggle either side of it. Centred on the screen when it fits there, else in the band.
		cs::Ref<Ship> flown = app->IsPiloting() ? app->Piloted.Ship : nullptr;
		bool tanker = flown != nullptr && flown->Battle == nullptr && (flown->PropellantFraction() < 0.15 || app->TankerDueS >= 0);
		constexpr float tankerW = 170, levelW = 200, sideGap = 8;
		float bx0 = 0, bx1 = 0;
		layout.BottomBand(bx0, bx1);
		float leftExtra = tanker ? tankerW + sideGap : 0, rightExtra = levelW + sideGap;
		float maxText = Mathf::Max(80.0f, bx1 - bx0 - leftExtra - rightExtra - 28);
		cs::String line = s + keys;
		if (text->CalcSize(GUIContent(line)).x > maxText) line = s + shortKeys;
		line = FitText(line, text, maxText);
		auto content = GUIContent(line);
		float w = text->CalcSize(content).x + 28;
		float total = leftExtra + w + rightExtra;
		float gx = (Screen::width - total) * 0.5f;
		if (gx < bx0 || gx + total > bx1) gx = Mathf::Max(bx0, bx0 + (bx1 - bx0 - total) * 0.5f);
		auto r = Panel(Rect(gx + leftExtra, Screen::height - 40, w, 30));
		GUI::Label(Rect(r.x + 14, r.y + 6, w, 20), content, text);

		// Tanks running low under the player's own hand: an emergency tanker can be called out.
		if (tanker)
		{
			auto call = Panel(Rect(r.x - tankerW - sideGap, r.y, tankerW, 30));
			cs::String callLabel = app->TankerDueS >= 0 ? cs::Cat("TANKER IN ", Mathf::CeilToInt(app->TankerDueS), " s") : cs::String("CALL A TANKER");
			if (GUI::Button(Rect(call.x + 4, call.y + 4, call.width - 8, 22), callLabel, app->TankerDueS >= 0 ? buttonActive : button)) app->CallTanker();
		}

		// Auto-reorient: keep the active system's own plane level (a smooth swing, not a snap).
		auto level = Panel(Rect(r.xMax() + sideGap, r.y, levelW, 30));
		cs::String reorient = cam->Levelling() ? "REORIENTING…" : app->AutoReorient() ? "AUTO-REORIENT  ON  [R]" : "AUTO-REORIENT  OFF  [R]";
		if (GUI::Button(Rect(level.x + 4, level.y + 4, level.width - 8, 22), reorient, app->AutoReorient() ? buttonActive : button))
			app->ToggleAutoReorient();
	}

	cs::String Hud::SensorText()
	{
		auto sensors = app->World()->Sensors;
		auto ship = app->Piloted.Ship;
		if (sensors == nullptr || ship == nullptr) return "";
		auto book = sensors->Player;
		int id = book->CountLive(ContactLevel::Identified), cls = book->CountLive(ContactLevel::Class), blips = book->CountLive(ContactLevel::Blip);
		// US-T93: dark or restarting says so before silent running (which it outdoes).
		auto power = SensorPosture::StateOf(ship->Combat);
		const char* posture = power == PowerState::Dark ? "  ·  DARK" : power == PowerState::Restarting ? "  ·  RESTARTING"
			: SensorPosture::Lit(ship->Combat.get()) ? "  ·  LIT UP" : ship->SilentRunning ? "  ·  SILENT RUNNING" : "";
		return cs::Cat(posture,
			"  ·  SIG ", cs::Fmt(sensors->PilotSignature(), sensors->PilotSignature() < 10 ? "0.00" : "0"),
			"  ·  CONTACTS ", id, " id / ", cls, " class / ", blips, " unknown");
	}

	void Hud::DrawHail()
	{
		cs::Ref<PatrolHail> hail = app->IsPiloting() && app->World()->Law != nullptr ? app->World()->Law->Hail : nullptr;
		if (hail == nullptr || app->CommsOpen()) return; // US-T215: the comms panel takes precedence over the patrol hail line (it drew behind it)
		UiMotion::Scope motion("hud.hail"); // US-T255
		int seconds = Mathf::CeilToInt(static_cast<float>(hail->SecondsLeft(app->Clock->Jd)));
		cs::String s = cs::Cat("HAILED  ·  ", cs::Strings::ToUpperInvariant(hail->By->Name), " PATROL ", cs::Strings::ToUpperInvariant(hail->Patrol->Name), "  ·  ",
			hail->Interdiction ? cs::Cat("HEAVE TO:  1 pay the ", Credits(hail->Fine), " fine, or be fired on in ", seconds, " s")
				: cs::Cat("CARGO SCAN: hold position, ", seconds, " s  (running is a crime)"), "    F12 parley");
		auto line = GUIContent(s);
		float lw = text->CalcSize(line).x + 28;
		auto r = Panel(Rect((Screen::width - lw) * 0.5f, Screen::height - 112, lw, 30));
		GUI::contentColor = hail->Interdiction ? UiStyle::Danger : Color(1.0f, 0.85f, 0.4f);
		GUI::Label(Rect(r.x + 14, r.y + 6, lw, 20), line, text);
		GUI::contentColor = Color::white();
	}

	cs::String Hud::RecordText(const cs::Ref<LawRecord>& r)
	{
		auto parts = cs::List<cs::String>();
		if (r->Wanted())
		{
			auto by = cs::List<cs::String>();
			for (auto [key, value] : r->Bounty) by.Add(cs::Cat(key->Name, " ", Credits(value)));
			parts.Add(cs::Cat("WANTED: ", cs::Strings::Join(", ", by)));
		}
		parts.Add(cs::Cat("notoriety ", cs::Fmt(r->Notoriety, "0")));
		parts.Add(cs::Cat(r->Crimes.Count(), " crime", r->Crimes.Count() == 1 ? "" : "s", " on file"));
		return cs::Strings::Join(",  ", parts);
	}

	void Hud::DrawSalvage()
	{
		auto s = app->Salvage;
		if (s == nullptr)
		{
			if (app->SalvageApproach.empty()) return;
			UiMotion::Scope motion("hud.salvage-approach"); // US-T255
			auto line = GUIContent(app->SalvageApproach);
			float lw = text->CalcSize(line).x + 28;
			auto lr = Panel(Rect((Screen::width - lw) * 0.5f, Screen::height - 76, lw, 30));
			GUI::contentColor = Accent;
			GUI::Label(Rect(lr.x + 14, lr.y + 6, lw, 20), line, text);
			GUI::contentColor = Color::white();
			return;
		}

		auto rows = cs::List<cs::String>();
		for (const auto& c : s->Interior->Compartments)
		{
			auto who = cs::List<cs::String>();
			if (c->Marines > 0.5) who.Add(cs::Cat(cs::Fmt(c->Marines, "0"), " raiders"));
			if (c->Boarders > 0.5) who.Add(cs::Cat(cs::Fmt(c->Boarders, "0"), " our marines"));
			if (c->Survivors > 0.5) who.Add(cs::Cat(cs::Fmt(c->Survivors, "0"), " survivors"));
			if (c->Vented) who.Add("open to space");
			rows.Add(cs::Cat(c->Name, ":  ", who.Count() == 0 ? cs::String("empty") : cs::Strings::Join(", ", who)));
		}
		auto aboard = cs::List<cs::String>();
		for (const auto& stack : s->Manifest())
			aboard.Add(cs::Cat(GoodInfo::Of(stack.Good)->Name, " ", cs::Fmt(stack.Tonnes, "0"), " t"));
		rows.Add(cs::Cat("Aboard:  ", aboard.Count() == 0 ? cs::String("nothing left") : cs::Strings::Join(", ", aboard)));
		auto part = s->BestPart();
		if (part != nullptr)
			rows.Add(cs::Cat("Best part:  ", SalvageSession::KindName(part->Kind()), " (", cs::Fmt(part->Health(), "P0"), ")",
				s->FitFor(part) != nullptr ? cs::String(", fits our own shot-away mount")
					: cs::Cat(", scrap for ", cs::Strings::ToLowerInvariant(GoodInfo::Of(SalvageSession::ScrapOf(part->Kind()))->Name))));
		if (s->SurvivorsRescued > 0.5) rows.Add(cs::Cat("Rescued:  ", cs::Fmt(s->SurvivorsRescued, "0"), " survivors"));

		constexpr float rowH = 16;
		float w = 520, h = 62 + rows.Count() * rowH;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, Screen::height - h - 48, w, h));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 20), cs::Cat("LATCHED  ·  ", cs::Strings::ToUpperInvariant(s->Name()), s->Hostile() ? "  ·  HOSTILES ABOARD" : ""), title);
		for (int i = 0; i < rows.Count(); i++)
			GUI::Label(Rect(r.x + 12, r.y + 30 + i * rowH, w - 24, rowH), rows[i], small);
		GUI::contentColor = Accent;
		GUI::Label(Rect(r.x + 12, r.yMax() - 26, w - 24, 20), app->SalvageChoices(), text);
		GUI::contentColor = Color::white();
	}

	void Hud::DrawBoarding()
	{
		if (!app->BoardingPanelOpen()) return;
		UiMotion::Scope motion("hud.boarding"); // US-T255
		auto [hull, role] = *app->BoardingFight;
		auto inside = hull->Combat->Interior;
		auto [order, objective] = BoardingCommand::OrdersOf(inside, role);
		constexpr float rowH = 16;
		// US-T82: a row for each party out on the hull, with how far across it is.
		int rows = inside->Compartments.Count() + inside->HullParties.Count();
		float w = 560, h = 82 + rows * rowH;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, Screen::height - h - 48, w, h));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 20), cs::Cat("BOARDING  ·  ", cs::Strings::ToUpperInvariant(hull->Name()), "  ·  ",
			role == BoardingRole::Boarders ? cs::String("OUR MARINES ABOARD") : cs::Cat("REPELLING ", cs::Strings::ToUpperInvariant(inside->BoarderNames()))), title);
		for (int i = 0; i < inside->Compartments.Count(); i++)
		{
			auto c = inside->Compartments[i];
			cs::String mark = cs::Cat(i == app->BoardingPick ? "> " : "   ", order == BoardingOrder::Push && i == objective ? "[objective] " : "");
			GUI::contentColor = c->Contested() ? UiStyle::Danger : c->Taken ? Color(1.0f, 0.85f, 0.4f) : Color::white();
			GUI::Label(Rect(r.x + 12, r.y + 30 + i * rowH, w - 24, rowH), cs::Cat(mark, c->Name, ":  ", SpaceText(c)), small);
		}
		GUI::contentColor = Accent;
		for (int i = 0; i < inside->HullParties.Count(); i++)
		{
			auto p = inside->HullParties[i];
			int across = (int)cs::Math::Round(p->Progress() * 10);
			cs::String bar = cs::Cat("[", cs::String(across, '='), cs::String(10 - across, ' '), "]");
			GUI::Label(Rect(r.x + 12, r.y + 30 + (inside->Compartments.Count() + i) * rowH, w - 24, rowH),
				cs::Cat("   On the hull:  ", cs::Fmt(p->Marines, "0"), " marines, ", inside->Compartments[p->From]->Name, " to ",
					inside->Compartments[p->To]->Name, "  ", bar, " ", cs::Fmt(p->Progress(), "P0")), small);
		}
		GUI::contentColor = Color::white();
		float y = r.y + 34 + rows * rowH;
		GUI::Label(Rect(r.x + 12, y, w - 24, rowH), cs::Cat("Orders:  ", BoardingCommand::Label(inside, role),
			inside->Log.Count() > 0 ? cs::Cat("   ·   ", inside->Log[inside->Log.Count() - 1].Text) : cs::String()), small);
		GUI::contentColor = Accent;
		GUI::Label(Rect(r.x + 12, r.yMax() - 26, w - 24, 20),
			cs::Cat("1 pick   2 ", role == BoardingRole::Boarders ? "push for it" : "muster on it", "   3 hold   ",
				role == BoardingRole::Boarders ? "4 over the hull   " : "", "0 auto-resolve   B close"), text);
		GUI::contentColor = Color::white();
	}

	double Hud::SceneSpeedToKmS(double sceneSpeed)
	{
		auto cam = app->SpaceCamera;
		auto body = (*app->Catalog())[cam->FrameBody()];
		double sceneDist = (cam->ScenePosition - app->BodyScene(cam->FrameBody())).Length();
		double trueDist = app->Scale()->InverseLocalDistance(body, sceneDist);
		double perUnit = sceneDist > 0 ? trueDist / sceneDist : ScaleModel::KmPerUnit;
		return sceneSpeed * perUnit;
	}

	void Hud::DrawSeat()
	{
		cs::String line = app->SeatLine();
		if (line.empty()) return; // C#: line == null
		DrawSeatSight();
		DrawDeckGuide();
		DrawRolePanel(); // US-T135
		auto content = GUIContent(line);
		float w = toast->CalcSize(content).x + 40;
		auto r = Rect((Screen::width - w) * 0.5f, Screen::height - 96, w, 34);
		RegisterUi(r); // US-T212
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 7, w, 22), content, toast);
	}

	// ---------------------------------------------------------------- the helm's abilities (US-T106)

	void Hud::DrawHelmAbilities()
	{
		if (!app->IsPiloting() || app->Travel->Active || app->ShowPicker) return;
		auto helm = cs::As<ShipHelm>(app->HeldSeat());
		if (helm == nullptr || helm->Ship == nullptr) return;
		auto bar = HelmAbilities::Of(helm->Ship, app->World()); // US-T140: a band captain's squad order too
		if (bar.Count() == 0) return;
		constexpr float pad = 10, rowH = 20, gap = 18;
		float maxW = Mathf::Min(static_cast<float>(Screen::width) - 40, 1100.0f);
		// Lay the entries out in rows, each "Shift+n  what  (how it stands)".
		cs::List<cs::String> texts;
		cs::List<float> widths;
		for (int i = 0; i < bar.Count() && i < 10; i++)
		{
			cs::String key = cs::Cat("Shift+", i == 9 ? 0 : i + 1);
			cs::String t = cs::Cat(key, "  ", bar[i].Label, "  (", bar[i].State, ")");
			texts.Add(t);
			widths.Add(small->CalcSize(GUIContent(t)).x);
		}
		cs::List<int> rowOf;
		int rows = 1;
		float x = 0, widest = 0;
		for (int i = 0; i < texts.Count(); i++)
		{
			if (x > 0 && x + widths[i] > maxW - 2 * pad)
			{
				rows++;
				x = 0;
			}
			rowOf.Add(rows - 1);
			x += widths[i] + gap;
			widest = Mathf::Max(widest, x - gap);
		}
		cs::String heading = cs::Cat(HelmAbilities::Heading(helm->Ship), "  \xC2\xB7  Shift+/ jump into a launched craft (again: the next, then back here)");
		widest = Mathf::Max(widest, small->CalcSize(GUIContent(heading)).x);
		float w = widest + 2 * pad, h = pad * 2 + rowH * (rows + 1);
		auto r = Rect((Screen::width - w) * 0.5f, Screen::height - 104 - h, w, h);
		RegisterUi(r); // US-T212
		GUI::Box(r, GUIContent::none(), panel);
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + pad, r.y + pad - 2, w - 2 * pad, rowH), heading, small);
		x = 0;
		int onRow = 0;
		for (int i = 0; i < texts.Count(); i++)
		{
			if (rowOf[i] != onRow)
			{
				onRow = rowOf[i];
				x = 0;
			}
			GUI::contentColor = bar[i].Ready ? Color(0.55f, 1.0f, 0.65f, 1.0f) : Dim;
			GUI::Label(Rect(r.x + pad + x, r.y + pad - 2 + rowH * (onRow + 1), widths[i] + 4, rowH), texts[i], small);
			x += widths[i] + gap;
		}
		GUI::contentColor = old;
	}

	// ---------------------------------------------------------------- the take-charge picker (U.SPC-T446)

	void Hud::DrawPicker()
	{
		auto seats = app->PickerSeats();
		auto held = app->HeldSeat();
		constexpr float w = 560, rowH = 22;
		float listH = Mathf::Clamp(seats.Count() * rowH, rowH, 360.0f);
		auto r = Panel(Rect((Screen::width - w) * 0.5f, 110, w, 64 + listH + 24));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 22), cs::Cat("TAKE CHARGE  ·  ", seats.Count(), seats.Count() == 1 ? " seat" : " seats"), title);
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 12, r.y + 30, w - 24, 18),
			held != nullptr ? cs::Cat("Holding ", held->Name(), ": pick another seat and it goes back to its AI.") : cs::String("Select a hull for its guns, or look at a fight."), small);
		auto view = Rect(r.x + 8, r.y + 52, w - 16, listH);
		auto content = Rect(0, 0, w - 36, Mathf::Max(seats.Count(), 1) * rowH);
		pickerScroll = GUI::BeginScrollView(view, pickerScroll, content);
		if (seats.Count() == 0)
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(4, 2, w - 40, rowH), "Nothing to take charge of here.", small);
		}
		cs::Ref<IEmbodiable> chosen = nullptr;
		for (int i = 0; i < seats.Count(); i++)
		{
			auto s = seats[i];
			bool isHeld = EmbodiableEquals(s, held);
			bool mounted = s->Host() != nullptr && EmbodiableIndexOf(seats, s->Host()) >= 0;
			auto rr = Rect(0, i * rowH, w - 36, rowH - 2);
			GUI::contentColor = Color::white();
			if (GUI::Button(rr, GUIContent::none(), isHeld ? rowSelected : row)) chosen = s;
			auto fc = s->Faction() != nullptr ? Color(s->Faction()->Color.R, s->Faction()->Color.G, s->Faction()->Color.B) : Dim;
			GUI::contentColor = fc;
			float x = mounted ? 22.0f : 6.0f;
			GUI::Label(Rect(x, rr.y + 2, 70, 18), cs::Strings::ToUpperInvariant(cs::Str(s->Kind())), small);
			GUI::contentColor = isHeld ? DeckLinedUpColor : Color::white();
			GUI::Label(Rect(x + 72, rr.y + 2, 300 - x, 18), cs::Cat(s->Name(), isHeld ? "  (held)" : ""), small);
			GUI::contentColor = Dim;
			GUI::Label(Rect(380, rr.y + 2, w - 420, 18), s->Faction() != nullptr ? s->Faction()->Name : cs::String(), small);
		}
		GUI::EndScrollView();
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 12, r.yMax() - 22, 300, 18), "Click a seat to take it.   F9 or Esc to close", small);
		GUI::contentColor = old;
		if (app->AtControls() && GUI::Button(Rect(r.xMax() - 172, r.yMax() - 24, 160, 20), "Back to command (P)", button)) app->PickCommand();
		if (chosen != nullptr) app->PickSeat(chosen);
	}

	void Hud::DrawDeckGuide()
	{
		WorldLabels wl(worldLabelPass); // US-T214
		if (Event::current().type != EventType::Repaint) return;
		auto f = cs::As<FighterSeat>(app->Seat());
		if (f == nullptr) return;
		auto b = f->Battle;
		auto deck = FlightDecks::GuideDeck(b, f->Unit);
		if (deck == nullptr) return;
		auto visit = FlightDecks::VisitOf(f->Unit);
		bool lined = visit != nullptr || FlightDecks::Capturing(b, f->Unit) == deck;
		auto color = lined ? DeckLinedUpColor : LockColor;
		Vector2 gate, stern;
		float ignored = 0; // C#'s `out _`
		bool gateOn = app->ToGui(app->BattleScene(b, deck->Gate()), gate, ignored);
		bool sternOn = app->ToGui(app->BattleScene(b, deck->Stern()), stern, ignored);
		if (visit != nullptr && visit->Stage == DeckStage::Launching)
		{
			Vector2 bow;
			if (app->ToGui(app->BattleScene(b, deck->Bow()), bow, ignored))
			{
				DrawBrackets(Rect(bow.x - 12, bow.y - 8, 24, 16), color, 1.5f);
				DrawLabel("LAUNCH", bow + Vector2(16, -8), color, true);
			}
			return;
		}
		if (gateOn) DrawRing(gate, 9, color);
		if (gateOn && sternOn) DrawLine(gate, stern, Color(color.r, color.g, color.b, 0.5f), 1.0f);
		if (sternOn)
		{
			DrawBrackets(Rect(stern.x - 12, stern.y - 8, 24, 16), color, 1.5f);
			cs::String tag = visit == nullptr ? cs::Cat("DECK ", RangeText((deck->Gate() - f->Unit->Pos).Length()))
				: visit->Stage == DeckStage::Final ? cs::String("LANDING") : cs::String("ON DECK");
			DrawLabel(tag, stern + Vector2(16, -8), color, true);
		}
	}

	void Hud::DrawSeatSight()
	{
		auto old = GUI::color;
		GUI::color = Color(0.6f, 0.95f, 1.0f, 0.9f);
		float cx = Screen::width * 0.5f, cy = Screen::height * 0.5f;
		const auto& tex = Texture2D::whiteTexture();
		GUI::DrawTexture(Rect(cx - 14, cy - 1, 9, 2), tex);
		GUI::DrawTexture(Rect(cx + 5, cy - 1, 9, 2), tex);
		GUI::DrawTexture(Rect(cx - 1, cy - 14, 2, 9), tex);
		GUI::DrawTexture(Rect(cx - 1, cy + 5, 2, 9), tex);
		GUI::color = old;
		DrawSeatNose(); // US-T129: a fighter's nose, swinging over to the crosshair
	}

	void Hud::DrawToast()
	{
		if (app->Toast.empty() || app->ToastAge > 3.5f) return;
		float a = Mathf::Clamp01(3.5f - app->ToastAge);
		auto content = GUIContent(app->Toast);
		float w = toast->CalcSize(content).x + 40;
		auto old = GUI::color;
		GUI::color = Color(1, 1, 1, a);
		UiMotion::Scope arriving(UiMotion::Enter(app->ToastAge), -UiMotion::ToastSlide); // US-T255: eases down into its slot
		auto r = layout.Take(HudRegion::TopCentre, w, 34); // the next slot under the news and alerts (US-T154)
		RegisterUi(r); // US-T211
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 7, w, 22), content, toast);
		GUI::color = old;
	}
}
