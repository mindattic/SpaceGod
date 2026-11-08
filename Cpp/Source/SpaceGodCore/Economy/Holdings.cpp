// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Holdings.cs (US-T18).

#include "Economy/Holdings.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Economy/Charters.h"
#include "Economy/Companies.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Industry.h"
#include "Economy/Propulsion.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(StandingOrderKind v)
	{
		switch (v)
		{
			case StandingOrderKind::Hold: return "Hold";
			case StandingOrderKind::TradeRoute: return "TradeRoute";
			case StandingOrderKind::Mining: return "Mining";
			case StandingOrderKind::Escort: return "Escort";
			case StandingOrderKind::Patrol: return "Patrol";
			case StandingOrderKind::FreeTrade: return "FreeTrade";
		}
		return "";
	}

	const char* ToString(PropertyKind v)
	{
		switch (v)
		{
			case PropertyKind::Warehouse: return "Warehouse";
			case PropertyKind::StationShare: return "StationShare";
			case PropertyKind::MiningClaim: return "MiningClaim";
			case PropertyKind::Plant: return "Plant";
		}
		return "";
	}

	const char* ToString(PlantStatus v)
	{
		switch (v)
		{
			case PlantStatus::Running: return "Running";
			case PlantStatus::Idle: return "Idle";
			case PlantStatus::Starved: return "Starved";
			case PlantStatus::Full: return "Full";
		}
		return "";
	}

	// ---------------------------------------------------------------- StandingOrder

	cs::Ref<StandingOrder> StandingOrder::Hold()
	{
		auto o = cs::New<StandingOrder>();
		o->Kind = StandingOrderKind::Hold;
		return o;
	}

	cs::Ref<StandingOrder> StandingOrder::TradeRoute(cs::List<int> ports)
	{
		auto o = cs::New<StandingOrder>();
		o->Kind = StandingOrderKind::TradeRoute;
		o->Stops = ports;
		return o;
	}

	cs::Ref<StandingOrder> StandingOrder::Patrol(cs::List<int> waypoints)
	{
		auto o = cs::New<StandingOrder>();
		o->Kind = StandingOrderKind::Patrol;
		o->Stops = waypoints;
		return o;
	}

	cs::Ref<StandingOrder> StandingOrder::Mining(cs::Ref<Property> claim)
	{
		auto o = cs::New<StandingOrder>();
		o->Kind = StandingOrderKind::Mining;
		o->Claim = claim;
		return o;
	}

	cs::Ref<StandingOrder> StandingOrder::Escort(cs::Ref<Ship> charge)
	{
		auto o = cs::New<StandingOrder>();
		o->Kind = StandingOrderKind::Escort;
		o->Charge = charge;
		return o;
	}

	cs::Ref<StandingOrder> StandingOrder::FreeTrade()
	{
		auto o = cs::New<StandingOrder>();
		o->Kind = StandingOrderKind::FreeTrade;
		return o;
	}

	cs::String StandingOrder::Describe(const cs::Ref<BodyCatalog>& catalog) const
	{
		auto stopsText = [&](const char* sep) -> cs::String
		{
			if (this->Stops.Count() == 0) return "nowhere";
			auto names = cs::List<cs::String>();
			for (int b : this->Stops) names.Add(catalog != nullptr ? (*catalog)[b]->Name : cs::Str(b));
			return cs::Strings::Join(sep, names);
		};
		switch (Kind)
		{
			case StandingOrderKind::TradeRoute: return cs::Cat("trade route ", stopsText(" - "));
			case StandingOrderKind::Patrol: return cs::Cat("patrol ", stopsText(", "));
			case StandingOrderKind::Mining: return Claim == nullptr ? cs::String("mining") : cs::Cat("mining ", Claim->Name);
			case StandingOrderKind::Escort: return Charge == nullptr ? cs::String("escort") : cs::Cat("escorting ", Charge->Name);
			case StandingOrderKind::FreeTrade: return "free trading";
			default: return "holding in port";
		}
	}

	cs::String StandingOrder::ToString() const { return Describe(nullptr); }

	// ---------------------------------------------------------------- OwnedShip, Property, LedgerEntry

	bool OwnedShip::OnCharter() const { return Charter != nullptr && Charter->Active; }

	cs::String OwnedShip::Name() const { return Ship->Name; }

	double Property::StoredTonnes() const
	{
		double t = 0;
		for (auto s : Stock) t += s;
		return t;
	}

	cs::String LedgerEntry::ToString() const { return cs::Cat(Text, " ", cs::Fmt(Amount, "+#,0;-#,0;0")); }

	// ---------------------------------------------------------------- Holdings

	const cs::List<cs::String>& Holdings::CaptainNames()
	{
		static const cs::List<cs::String> names =
		{
			"Ada Okonkwo", "Bram Teller", "Calla Voss", "Dmitri Hale", "Esme Rourke", "Farouk Bey", "Greta Lind", "Hollis Crane",
			"Ines Navarro", "Jory Kade", "Kaito Mbeki", "Lena Strand", "Marek Oduya", "Nell Quarry", "Oren Sallow", "Priya Haldane",
		};
		return names;
	}

	Holdings::Holdings(cs::Ref<World> world, int seed)
		: rng(seed)
	{
		this->world = world;
		House = cs::New<Faction>("Your fleet", "", Rgb(0.45f, 0.95f, 0.55f));
		House->IsHouse = true;
		// No trained reserve of its own: crews sign on in port.
		House->Manpower = cs::Double::PositiveInfinity;
	}

	double Holdings::Balance() const { return House->Treasury; }

	cs::Ref<::SpaceGod::Core::Economy> Holdings::Economy() const { return world->Economy; }

	cs::Ref<OwnedShip> Holdings::Of(const cs::Ref<Ship>& ship) const
	{
		cs::Ref<OwnedShip> o;
		return ship != nullptr && byShip.TryGetValue(ship, o) && !o->Lost && ship->Faction == House && !ship->Destroyed && !ship->Derelict && !ship->GateDeparted ? o : nullptr;
	}

	bool Holdings::Owns(const cs::Ref<Ship>& ship) const { return Of(ship) != nullptr; }

	cs::List<cs::Ref<Property>> Holdings::OfKind(PropertyKind kind) const
	{
		cs::List<cs::Ref<Property>> r;
		for (const auto& p : Properties)
			if (p->Kind == kind && !p->Lost) r.Add(p);
		return r;
	}

	// ---------------------------------------------------------------- the account

	bool Holdings::Deposit(const cs::Ref<Faction>& from, double amount, double jd)
	{
		if (from == nullptr || from == House || amount <= 0 || from->Treasury < amount) return false;
		from->Treasury -= amount;
		House->Treasury += amount;
		Book(jd, cs::Cat("Deposited from the ", from->Name, "'s purse"), amount, /*earned:*/ false);
		return true;
	}

	bool Holdings::Withdraw(const cs::Ref<Faction>& to, double amount, double jd)
	{
		if (to == nullptr || to == House || amount <= 0 || House->Treasury < amount) return false;
		House->Treasury -= amount;
		to->Treasury += amount;
		Book(jd, cs::Cat("Withdrawn to the ", to->Name, "'s purse"), -amount, /*earned:*/ false);
		return true;
	}

	bool Holdings::Transact(double amount, const cs::String& text, double jd, bool overdraw)
	{
		if (amount < 0 && !overdraw && House->Treasury < -amount) return false;
		House->Treasury += amount;
		Book(jd, text, amount);
		if (cs::Double::IsNaN(nextTickJd)) nextTickJd = jd + TickDays;
		return true;
	}

	void Holdings::Book(double jd, const cs::String& text, double amount, bool earned, bool cash)
	{
		auto e = cs::New<LedgerEntry>();
		e->Jd = jd;
		e->Text = text;
		e->Amount = amount;
		e->Cash = cash;
		Ledger.Add(e);
		if (cash) BookBalance += amount;
		if (Ledger.Count() > LedgerLength) Ledger.RemoveRange(0, Ledger.Count() - LedgerLength);
		if (!earned) return;
		if (!cash) TotalLost += -amount;
		else if (amount >= 0) TotalEarned += amount;
		else TotalCosts += -amount;
	}

	// ---------------------------------------------------------------- ships

	bool Holdings::CanBuyAt(const cs::Ref<Settlement>& yard) const
	{
		return yard != nullptr && yard->HasShipyard() && !yard->Fallen && yard->Owner != nullptr && !yard->Owner->IsPirate && world->Diplomacy()->CanDockAt(House, yard);
	}

	cs::Ref<OwnedShip> Holdings::Buy(const cs::Ref<Settlement>& yard, ShipClass cls, const cs::String& name, double jd)
	{
		if (!CanBuyAt(yard)) return nullptr;
		double price = Economy::HullPrice(cls);
		if (House->Treasury < price) return nullptr;
		House->Treasury -= price;
		yard->Owner->Treasury += price;
		auto ship = world->Fleet->AddShip(cs::Strings::IsNullOrEmpty(name) ? cs::Cat("Venture ", Ships.Count() + 1) : name, cls, House, RoleFor(StandingOrderKind::Hold, cls));
		ship->CapacityTonnes = Economy::CapacityOf(cls);
		ship->Ports = world->PortBodies();
		ship->Route = cs::List<int>{ yard->Body };
		// The house's before it is in space: its first plan is its (captainless) own, not a merchant's.
		auto owned = Add(ship, jd, price);
		world->Launch(ship, yard, jd);
		Book(jd, cs::Cat("Bought the ", cs::Strings::ToLowerInvariant(ship->Spec->DisplayName), " ", ship->Name, " at ", yard->Name), -price, /*earned:*/ false);
		world->Log(jd, cs::Cat(yard->Name, " sells the ", cs::Strings::ToLowerInvariant(ship->Spec->DisplayName), " ", ship->Name, " to your fleet"), WorldEventKind::Launch, yard->Body);
		world->NotifyAdded(ship);
		return owned;
	}

	cs::Ref<OwnedShip> Holdings::Enlist(const cs::Ref<Ship>& ship, double jd)
	{
		if (ship == nullptr || ship->Destroyed || ship->Derelict || ship->GateDeparted || ship->IsEscort() || ship->Battle != nullptr) return nullptr;
		if (Owns(ship) || ship->Faction->IsPirate) return nullptr;
		if (ship->Faction != House)
		{
			// Not a crew-less hull any more: whatever escorts flew with it stay with their own flag.
			for (const auto& s : world->Fleet->Ships())
				if (s->Leader == ship) s->Leader = nullptr;
			world->Economy->Release(ship);
			ship->CargoDestination = nullptr;
			// Whatever is in its hold the house came by for nothing: all it fetches is profit.
			ship->CargoCost = 0;
			// Out of any trading company's books (what it took since their last review is still theirs): its takings are the house's now.
			if (world->Companies) world->Companies->SettleOut(ship->Company, ship);
			if (ship->Company) ship->Company->Ships.Remove(ship);
			ship->Company = nullptr;
			ship->Faction = House;
		}
		if (ship->CapacityTonnes <= 0) ship->CapacityTonnes = Economy::CapacityOf(ship->Spec->Class);
		// C#'s `??=` on a null array: an empty list stands for null here.
		if (ship->Ports.Count() == 0) ship->Ports = world->PortBodies();
		ship->Role = RoleFor(StandingOrderKind::Hold, ship->Spec->Class);
		auto owned = Add(ship, jd, 0);
		Book(jd, cs::Cat("The ", ship->Name, " joins your fleet"), 0, /*earned:*/ false);
		world->Log(jd, cs::Cat("The ", ship->Name, " joins your fleet"), WorldEventKind::Info, ship->Orbit ? ship->Orbit->Body : -1);
		return owned;
	}

	cs::Ref<OwnedShip> Holdings::Add(const cs::Ref<Ship>& ship, double jd, double price)
	{
		auto owned = cs::New<OwnedShip>();
		owned->Ship = ship;
		owned->SinceJd = jd;
		owned->PricePaid = price;
		owned->SeenCredits = ship->Credits;
		Ships.Add(owned);
		byShip[ship] = owned;
		if (cs::Double::IsNaN(nextTickJd)) nextTickJd = jd + TickDays;
		return owned;
	}

	cs::Ref<Captain> Holdings::Hire(const cs::Ref<OwnedShip>& owned, double jd, double skill)
	{
		if (owned == nullptr || owned->Lost) return nullptr;
		if (cs::Double::IsNaN(skill)) skill = 0.6 + rng.NextDouble() * 0.8;
		skill = cs::Math::Max(0.5, cs::Math::Min(1.5, skill));
		int n = captainNames++;
		int len = CaptainNames().Count();
		auto c = cs::New<Captain>();
		c->Name = cs::Cat(CaptainNames()[n % len], (n >= len ? cs::Cat(" ", n / len + 1) : cs::String()));
		c->Skill = skill;
		c->WagePerDay = CaptainWage * skill * (owned->Ship->Spec->IsMilitary ? 2 : 1);
		c->HiredJd = jd;
		owned->Captain = c;
		Book(jd, cs::Cat("Captain ", c->Name, " takes command of the ", owned->Name()), 0, /*earned:*/ false);
		return c;
	}

	void Holdings::Dismiss(const cs::Ref<OwnedShip>& owned, double jd)
	{
		if ((owned ? owned->Captain : nullptr) == nullptr || owned->OnCharter()) return;
		Book(jd, cs::Cat("Captain ", owned->Captain->Name, " leaves the ", owned->Name()), 0, /*earned:*/ false);
		owned->Captain = nullptr;
	}

	bool Holdings::Order(const cs::Ref<OwnedShip>& owned, const cs::Ref<StandingOrder>& orders)
	{
		return !(owned ? owned->OnCharter() : false) && SetOrders(owned, orders);
	}

	bool Holdings::SetOrders(const cs::Ref<OwnedShip>& owned, const cs::Ref<StandingOrder>& orders)
	{
		LastRefusal = cs::String();
		if (owned == nullptr || owned->Lost || orders == nullptr || !CanFollow(owned->Ship, orders)) return false;
		if (orders->Kind == StandingOrderKind::TradeRoute)
		{
			int discard;
			cs::String why = UnreachableLeg(owned->Ship, orders->Stops, Economy()->Jd, discard);
			if (!why.empty()) { LastRefusal = why; return false; }
		}
		if (orders->Kind == StandingOrderKind::TradeRoute && !HasMarketAtEveryStop(orders)) return false;
		owned->Orders = orders;
		orders->StopIndex = 0;
		owned->MiningDays = 0;
		auto role = RoleFor(orders->Kind, owned->Ship->Spec->Class);
		// A warship never sells on arrival, so a load left in its hold (and booked inbound at a market) would be stranded: release the booking and dump it, booking the loss.
		if ((role == ShipRole::Patrol || role == ShipRole::Escort) && owned->Ship->HasCargo)
		{
			auto ship = owned->Ship;
			Economy()->Release(ship);
			BookBalance -= ship->CargoCost;
			ship->HasCargo = false;
			ship->CargoTonnes = 0;
			ship->CargoCost = 0;
			ship->CargoBonus = 0;
			ship->CargoDestination = nullptr;
			ship->Cargo = cs::String();
		}
		owned->Ship->Role = role;
		if (orders->Kind == StandingOrderKind::Patrol) owned->Ship->Route = orders->Stops;
		return true;
	}

	cs::String Holdings::UnreachableLeg(const cs::Ref<Ship>& ship, const cs::List<int>& stops, double jd, int& badFrom) const
	{
		badFrom = -1;
		if (stops.Count() == 0 || !world->BurnsPropellant(ship)) return cs::String();
		for (int i = 0; i < stops.Count(); i++)
		{
			int a = stops[i], b = stops[(i + 1) % stops.Count()];
			cs::String why = LegProblem(ship, a, b, jd);
			if (!why.empty()) { badFrom = i; return why; }
		}
		return cs::String();
	}

	cs::String Holdings::LegProblem(const cs::Ref<Ship>& ship, int a, int b, double jd) const
	{
		if (a == b) return cs::String();
		double dv = world->Fleet->EstimateTrip(ship, a, b, jd).DeltaVKmS;
		double need = Propulsion::PropellantFor(ship, dv, 0, ship->PropellantCapacity());
		if (need <= ship->PropellantCapacity()) return cs::String();
		return cs::Cat((*world->Catalog())[a]->Name, " to ", (*world->Catalog())[b]->Name, " needs ", cs::Fmt(need, "N0"),
			" t of propellant, more than the ", cs::Fmt(ship->PropellantCapacity(), "N0"), " t its tanks hold");
	}

	bool Holdings::HasMarketAtEveryStop(const cs::Ref<StandingOrder>& o) const
	{
		// C#'s null Stops is an empty list here (an order with no stops has no market at every stop). C# would answer
		// true for a non-null empty array, but no order ever has one: Stops is null unless set by TradeRoute/Patrol
		// (both called with stops), and the one caller, SetOrders, has already required two stops (CanFollow). So
		// empty-as-null gives C#'s answer for every order Core and the HUD can make (US-T35).
		if (o->Stops.Count() == 0) return false;
		for (auto stop : o->Stops)
		{
			bool found = false;
			for (const auto& s : Economy()->At(stop)) if (s->Discovered()) { found = true; break; }
			if (!found) return false;
		}
		return true;
	}

	int Holdings::MarketBodyFor(int body) const
	{
		for (const auto& s : Economy()->At(body)) if (s->Discovered()) return body;
		for (const auto& b : world->Catalog()->Bodies())
			if (b->ParentIndex == body)
				for (const auto& s : Economy()->At(b->Index)) if (s->Discovered()) return b->Index;
		return -1;
	}

	bool Holdings::CanFollow(const cs::Ref<Ship>& ship, const cs::Ref<StandingOrder>& o)
	{
		switch (o->Kind)
		{
			case StandingOrderKind::TradeRoute:
				return o->Stops.Count() >= 2 && Economy::CapacityOf(ship->Spec->Class) > 0;
			case StandingOrderKind::FreeTrade:
				return Economy::CapacityOf(ship->Spec->Class) > 0;
			case StandingOrderKind::Patrol:
				return o->Stops.Count() >= 1;
			case StandingOrderKind::Mining:
				return o->Claim != nullptr && !o->Claim->Lost && o->Claim->Kind == PropertyKind::MiningClaim && Economy::Carries(ship->Spec->Class, o->Claim->Ore);
			case StandingOrderKind::Escort:
				return o->Charge != nullptr && o->Charge != ship && !o->Charge->Destroyed;
			default:
				return true;
		}
	}

	ShipRole Holdings::RoleFor(StandingOrderKind kind, ShipClass cls)
	{
		switch (kind)
		{
			case StandingOrderKind::TradeRoute:
			case StandingOrderKind::FreeTrade: return ShipRole::Trader;
			case StandingOrderKind::Mining: return ShipRole::Miner;
			case StandingOrderKind::Patrol: return ShipRole::Patrol;
			case StandingOrderKind::Escort: return ShipRole::Escort;
			default: return Economy::CapacityOf(cls) > 0 ? ShipRole::Trader : ShipRole::Patrol;
		}
	}

	// ---------------------------------------------------------------- property

	cs::Ref<Property> Holdings::BuyWarehouse(const cs::Ref<Settlement>& port, double jd)
	{
		if (port == nullptr || port->Fallen || port->Owner == nullptr || port->Owner->IsPirate || !world->Diplomacy()->CanDockAt(House, port)) return nullptr;
		for (const auto& p : OfKind(PropertyKind::Warehouse))
			if (p->Settlement == port) return nullptr;
		if (House->Treasury < WarehousePrice) return nullptr;
		House->Treasury -= WarehousePrice;
		port->Owner->Treasury += WarehousePrice;
		auto w = cs::New<Property>();
		w->Kind = PropertyKind::Warehouse;
		w->Settlement = port;
		w->Body = port->Body;
		w->CapacityTonnes = WarehouseTonnes;
		w->PricePaid = WarehousePrice;
		w->SinceJd = jd;
		w->Name = cs::Cat("warehouse at ", port->Name);
		AddProperty(w, jd);
		return w;
	}

	double Holdings::SharePrice(const cs::Ref<Settlement>& s, double share) { return s == nullptr ? 0 : s->Population * ValuePerResident * share; }

	cs::Ref<Property> Holdings::BuyShare(const cs::Ref<Settlement>& s, double share, double jd)
	{
		if (s == nullptr || s->Fallen || s->Owner == nullptr || s->Owner->IsPirate || s->Kind == SettlementKind::Haven || share <= 0) return nullptr;
		cs::Ref<Property> held = nullptr;
		for (const auto& p : OfKind(PropertyKind::StationShare))
			if (p->Settlement == s) held = p;
		double already = held ? held->Share : 0;
		if (already + share > 0.5 + 1e-9) return nullptr;
		double price = SharePrice(s, share);
		if (House->Treasury < price) return nullptr;
		House->Treasury -= price;
		s->Owner->Treasury += price;
		if (held != nullptr)
		{
			held->Share += share;
			held->PricePaid += price;
			held->Name = ShareName(s, held->Share);
			Book(jd, cs::Cat("Bought ", cs::Fmt(share, "P0"), " more of ", s->Name), -price, /*earned:*/ false);
			return held;
		}
		auto prop = cs::New<Property>();
		prop->Kind = PropertyKind::StationShare;
		prop->Settlement = s;
		prop->Body = s->Body;
		prop->Share = share;
		prop->PricePaid = price;
		prop->SinceJd = jd;
		prop->SeenTradeVolume = s->TradeVolumeCredits;
		prop->Name = ShareName(s, share);
		AddProperty(prop, jd);
		return prop;
	}

	cs::String Holdings::ShareName(const cs::Ref<Settlement>& s, double share) { return cs::Cat(cs::Fmt(share, "P0"), " share of ", s->Name); }

	double Holdings::YieldOf(Good ore) { return BaseYieldPerDay * cs::Math::Sqrt(40 / cs::Math::Max(1.0, GoodInfo::Of(ore)->BasePrice)); }

	double Holdings::ClaimPrice(Good ore) { return YieldOf(ore) * GoodInfo::Of(ore)->BasePrice * ClaimPricePerYield; }

	cs::Ref<Property> Holdings::StakeClaim(int body, Good ore, double jd)
	{
		auto cat = GoodInfo::Of(ore)->Category;
		if (cat != GoodCategory::Mineral && cat != GoodCategory::Volatile && ore != Good::Water) return nullptr;
		if (body < 0 || body >= world->Catalog()->Bodies().Count() || (*world->Catalog())[body]->IsStar()) return nullptr;
		for (const auto& s : Economy()->At(body))
			if (s->Kind == SettlementKind::Haven) return nullptr;
		for (const auto& p : OfKind(PropertyKind::MiningClaim))
			if (p->Body == body && p->Ore == ore) return nullptr;
		double price = ClaimPrice(ore);
		if (House->Treasury < price) return nullptr;
		House->Treasury -= price;
		auto claim = cs::New<Property>();
		claim->Kind = PropertyKind::MiningClaim;
		claim->Body = body;
		claim->Ore = ore;
		claim->YieldPerDay = YieldOf(ore);
		claim->PricePaid = price;
		claim->SinceJd = jd;
		claim->Name = cs::Cat(cs::Strings::ToLowerInvariant(GoodInfo::Of(ore)->Name), " claim on ", (*world->Catalog())[body]->Name);
		AddProperty(claim, jd);
		return claim;
	}

	void Holdings::AddProperty(const cs::Ref<Property>& p, double jd)
	{
		Properties.Add(p);
		Book(jd, cs::Cat("Bought the ", p->Name), -p->PricePaid, /*earned:*/ false);
		world->Log(jd, cs::Cat("Your fleet buys the ", p->Name), WorldEventKind::Economy, p->Body);
		if (cs::Double::IsNaN(nextTickJd)) nextTickJd = jd + TickDays;
	}

	bool Holdings::CanTradeAt(const cs::Ref<Settlement>& port) const
	{
		return port != nullptr && port->Owner != nullptr && !port->Fallen && world->Diplomacy()->CanDockAt(House, port);
	}

	double Holdings::StoreFromMarket(const cs::Ref<Property>& warehouse, Good good, double tonnes, double jd)
	{
		if (warehouse == nullptr || warehouse->Lost || warehouse->Kind != PropertyKind::Warehouse || tonnes <= 0) return 0;
		auto s = warehouse->Settlement;
		if (!CanTradeAt(s)) return 0;
		tonnes = cs::Math::Min(tonnes, cs::Math::Min(s->Available(good), warehouse->CapacityTonnes - warehouse->StoredTonnes()));
		if (tonnes < 0.01) return 0;
		double price = s->PurchaseCost(good, tonnes);
		if (price * (1 + Economy::PortTax) > House->Treasury)
		{
			tonnes *= House->Treasury / (price * (1 + Economy::PortTax)) * 0.99;
			if (tonnes < 0.01) return 0;
			price = s->PurchaseCost(good, tonnes);
		}
		double cost = price * (1 + Economy::PortTax);
		s->Stock[static_cast<int>(good)] = cs::Math::Max(0.0, s->Stock[static_cast<int>(good)] - tonnes);
		s->Owner->Treasury += price * Economy::PortTax;
		s->TradeVolumeCredits += price;
		House->Treasury -= cost;
		warehouse->Stock[static_cast<int>(good)] += tonnes;
		s->RefreshPrices();
		Book(jd, cs::Cat("Stored ", cs::Fmt(tonnes, "N0"), " t of ", cs::Strings::ToLowerInvariant(GoodInfo::Of(good)->Name), " in the ", warehouse->Name), -cost);
		return tonnes;
	}

	double Holdings::SellFromStore(const cs::Ref<Property>& warehouse, Good good, double tonnes, double jd)
	{
		if (warehouse == nullptr || warehouse->Lost || warehouse->Kind != PropertyKind::Warehouse) return 0;
		return SellStock(warehouse, warehouse, good, tonnes, jd, true);
	}

	double Holdings::SellStock(const cs::Ref<Property>& store, const cs::Ref<Property>& credit, Good good, double tonnes, double jd, bool book)
	{
		tonnes = cs::Math::Min(tonnes, store->Stock[static_cast<int>(good)]);
		auto s = store->Settlement;
		if (!CanTradeAt(s)) return 0;
		if (tonnes < 0.01 || s->Demand[static_cast<int>(good)] <= 0) return 0;
		double value = s->SaleValue(good, tonnes);
		double tax = value * Economy::PortTax;
		s->Stock[static_cast<int>(good)] += tonnes;
		s->Owner->Treasury += tax;
		s->TradeVolumeCredits += value;
		store->Stock[static_cast<int>(good)] -= tonnes;
		House->Treasury += value - tax;
		credit->Income += value - tax;
		s->RefreshPrices();
		if (book) Book(jd, cs::Cat("Sold ", cs::Fmt(tonnes, "N0"), " t of ", cs::Strings::ToLowerInvariant(GoodInfo::Of(good)->Name), " out of the ", credit->Name), value - tax);
		return value - tax;
	}

	// ---------------------------------------------------------------- plants (US-T265)

	cs::List<IndustryKind> Holdings::PlantKindsAt(const cs::Ref<Settlement>& port)
	{
		cs::List<IndustryKind> kinds;
		if (port == nullptr) return kinds;
		for (const auto& f : port->Facilities)
		{
			if (f == nullptr || f->Recipe == nullptr) continue;
			auto k = f->Recipe->Kind;
			if (k == IndustryKind::Mine || k == IndustryKind::Shipyard || kinds.Contains(k)) continue;
			kinds.Add(k);
		}
		// In the order of the industries, not of the port's works.
		for (int i = 1; i < kinds.Count(); i++)
			for (int j = i; j > 0 && static_cast<int>(kinds[j]) < static_cast<int>(kinds[j - 1]); j--)
			{
				auto t = kinds[j];
				kinds[j] = kinds[j - 1];
				kinds[j - 1] = t;
			}
		return kinds;
	}

	double Holdings::PlantPrice(IndustryKind kind)
	{
		auto r = Recipe::Of(kind);
		double v = 0;
		for (const auto& [g, rate] : r->Outputs) v += rate * PlantLevel * GoodInfo::Of(g)->BasePrice;
		return v * PlantPriceDays;
	}

	cs::String Holdings::PlantWord(IndustryKind kind) { return kind == IndustryKind::Smelter ? cs::String("refinery") : cs::String("factory"); }

	cs::Ref<Property> Holdings::BuyPlant(const cs::Ref<Settlement>& port, IndustryKind kind, double jd)
	{
		if (port == nullptr || port->Fallen || port->Owner == nullptr || port->Owner->IsPirate || port->Kind == SettlementKind::Haven || !world->Diplomacy()->CanDockAt(House, port)) return nullptr;
		if (!PlantKindsAt(port).Contains(kind)) return nullptr;
		cs::Ref<Property> store = nullptr;
		for (const auto& p : OfKind(PropertyKind::Warehouse))
			if (p->Settlement == port) store = p;
		if (store == nullptr) return nullptr;
		for (const auto& p : OfKind(PropertyKind::Plant))
			if (p->Settlement == port && p->Recipe != nullptr && p->Recipe->Kind == kind) return nullptr;
		double price = PlantPrice(kind);
		if (House->Treasury < price) return nullptr;
		House->Treasury -= price;
		port->Owner->Treasury += price;
		auto plant = cs::New<Property>();
		plant->Kind = PropertyKind::Plant;
		plant->Settlement = port;
		plant->Body = port->Body;
		plant->Recipe = Recipe::Of(kind);
		plant->Level = PlantLevel;
		plant->Warehouse = store;
		plant->Landlord = port->Owner;
		plant->PricePaid = price;
		plant->SinceJd = jd;
		plant->Name = cs::Cat(PlantWord(kind), " at ", port->Name, " (", cs::Strings::ToLowerInvariant(plant->Recipe->Name), ")");
		AddProperty(plant, jd);
		return plant;
	}

	double Holdings::PlantRate(const cs::Ref<Property>& plant, Good good, bool output)
	{
		if (plant == nullptr || plant->Recipe == nullptr) return 0;
		double t = 0;
		for (const auto& [g, rate] : output ? plant->Recipe->Outputs : plant->Recipe->Inputs)
			if (g == good) t += rate * plant->Level;
		return t;
	}

	void Holdings::RunPlant(const cs::Ref<Property>& p, double jd)
	{
		auto s = p->Settlement;
		auto store = p->Warehouse;
		auto recipe = p->Recipe;
		if (recipe == nullptr || store == nullptr || store->Lost || s == nullptr)
		{
			p->Status = PlantStatus::Idle;
			p->Utilisation = 0;
			return;
		}
		double crew = (recipe->WorkersPerLevel * p->Level * PlantWagePerWorker + PlantFixedPerDay) * TickDays;
		auto Average = [&](double profit) { p->ProfitPerDay = p->TonnesMade <= 0 && p->ProfitPerDay == 0 ? profit : 0.7 * p->ProfitPerDay + 0.3 * profit; };

		// Raiders where the war runs hot: they carry off part of what it made, and it stands idle while it mends.
		if (p->DownUntilJd <= jd && world->Contracts != nullptr)
		{
			double danger = cs::Math::Max(0.0, cs::Math::Min(1.0, world->Contracts->DangerAt(p->Body)));
			if (danger > 0 && rng.NextDouble() < danger * PlantRaidPerDay * TickDays)
			{
				double tonnes = 0, value = 0;
				for (const auto& [g, rate] : recipe->Outputs)
				{
					double take = store->Stock[static_cast<int>(g)] * PlantRaidShare;
					store->Stock[static_cast<int>(g)] -= take;
					tonnes += take;
					value += take * GoodInfo::Of(g)->BasePrice;
				}
				p->TonnesRaided += tonnes;
				p->DownUntilJd = jd + PlantRaidDownDays;
				Book(jd, cs::Cat("Raiders hit the ", p->Name, tonnes >= 1 ? cs::Cat(": ", cs::Fmt(tonnes, "N0"), " t carried off") : cs::String()), -value, true, /*cash:*/ false);
				world->Log(jd, cs::Cat("Raiders hit your ", p->Name), WorldEventKind::Loss, p->Body);
			}
		}

		if (!p->Working || p->DownUntilJd > jd)
		{
			p->Status = PlantStatus::Idle;
			p->Utilisation = 0;
			Charge(p, crew * PlantIdleUpkeep, jd, cs::Cat("Upkeep on the idle ", p->Name));
			Average(-crew * PlantIdleUpkeep);
			return;
		}

		// How much of its rate it can make: its feedstock (from the warehouse, then the market), then room for the goods.
		bool market = p->BuyInputs && CanTradeAt(s);
		int n = recipe->Inputs.Count();
		double f = 1, eff = 1;
		for (int i = 0; i < n; i++)
		{
			auto [g, rate] = recipe->Inputs[i];
			double need = rate * p->Level * TickDays;
			if (need <= 0) continue;
			double have = store->Stock[static_cast<int>(g)] + (market ? s->Available(g) : 0);
			double share = cs::Math::Min(1.0, have / need);
			if (recipe->Limiting[i]) f = cs::Math::Min(f, share);
			else eff = cs::Math::Min(eff, Recipe::MinorFloor + (1 - Recipe::MinorFloor) * share);
		}
		double fed = f;
		double outFull = 0;
		for (const auto& [g, rate] : recipe->Outputs) outFull += rate * p->Level * TickDays * eff;
		double room = cs::Math::Max(0.0, store->CapacityTonnes - store->StoredTonnes());
		if (outFull > 0 && outFull * f > room) f = room / outFull;
		bool full = f < fed - 1e-9;

		// What it must buy at that rate, cut to what the account can pay.
		auto Use = [&](int i, double ff)
		{
			auto [g, rate] = recipe->Inputs[i];
			double use = rate * p->Level * TickDays * ff;
			return recipe->Limiting[i] ? use : cs::Math::Min(use, store->Stock[static_cast<int>(g)] + (market ? s->Available(g) : 0));
		};
		bool poor = false;
		if (market)
			for (int guard = 0; guard < 4 && f > 1e-6; guard++)
			{
				double cost = 0;
				for (int i = 0; i < n; i++)
				{
					auto g = std::get<0>(recipe->Inputs[i]);
					double buy = Use(i, f) - store->Stock[static_cast<int>(g)];
					if (buy > 0.01) cost += s->PurchaseCost(g, buy) * (1 + Economy::PortTax);
				}
				if (cost <= cs::Math::Max(0.0, House->Treasury)) break;
				poor = true;
				f *= cs::Math::Max(0.0, House->Treasury) / cost * 0.99;
			}
		if (f < 1e-6) f = 0;

		// Draw the feedstock and make the goods.
		double drawnValue = 0, bought = 0;
		for (int i = 0; i < n; i++)
		{
			auto g = std::get<0>(recipe->Inputs[i]);
			int gi = static_cast<int>(g);
			double use = Use(i, f);
			double fromStore = cs::Math::Min(use, store->Stock[gi]);
			store->Stock[gi] -= fromStore;
			drawnValue += fromStore * s->Price[gi];
			double buy = market ? cs::Math::Min(use - fromStore, s->Available(g)) : 0;
			if (buy <= 0.01) continue;
			double price = s->PurchaseCost(g, buy);
			s->Stock[gi] = cs::Math::Max(0.0, s->Stock[gi] - buy);
			s->Owner->Treasury += price * Economy::PortTax;
			s->TradeVolumeCredits += price;
			House->Treasury -= price * (1 + Economy::PortTax);
			bought += price * (1 + Economy::PortTax);
		}
		if (bought > 0)
		{
			s->RefreshPrices();
			p->Upkeep += bought;
			Book(jd, cs::Cat("Feedstock for the ", p->Name), -bought);
		}
		double made = 0, value = 0;
		for (const auto& [g, rate] : recipe->Outputs)
		{
			double t = rate * p->Level * TickDays * f * eff;
			if (t <= 0) continue;
			store->Stock[static_cast<int>(g)] += t;
			made += t;
			value += t * s->Price[static_cast<int>(g)];
		}
		p->TonnesMade += made;
		p->Utilisation = f * eff;
		p->Status = f * eff >= 0.999 ? PlantStatus::Running : full ? PlantStatus::Full : PlantStatus::Starved;
		if (poor && p->Status == PlantStatus::Running) p->Status = PlantStatus::Starved;

		// Straight onto the market, if the house wants it sold as it is made (more supply here, a lower price).
		if (p->SellOutputs && made > 0)
		{
			double sold = 0;
			for (const auto& [g, rate] : recipe->Outputs)
				sold += SellStock(store, p, g, rate * p->Level * TickDays * f * eff, jd, false);
			if (sold >= 1) Book(jd, cs::Cat("Sold what the ", p->Name, " made"), sold);
		}

		Charge(p, crew, jd, cs::Cat("Crew and power at the ", p->Name));
		Average(value - drawnValue - bought - crew);
	}

	bool Holdings::DeliverToPlant(const cs::Ref<Ship>& ship, int body, double jd)
	{
		auto o = Of(ship);
		if (o == nullptr || o->FeedingPlant == nullptr) return false;
		auto p = o->FeedingPlant;
		if (!ship->HasCargo)
		{
			o->FeedingPlant = nullptr;
			return false;
		}
		if (p->Body != body) return false; // not there yet
		o->FeedingPlant = nullptr;
		auto store = p->Warehouse;
		if (p->Lost || store == nullptr || store->Lost || PlantRate(p, ship->CargoGood, false) <= 0) return false;
		double t = cs::Math::Min(ship->CargoTonnes, cs::Math::Max(0.0, store->CapacityTonnes - store->StoredTonnes()));
		if (t < 0.01) return false; // no room: it goes to the market
		double cost = ship->CargoTonnes > 0 ? ship->CargoCost * t / ship->CargoTonnes : 0;
		auto good = ship->CargoGood;
		Economy()->Release(ship);
		store->Stock[static_cast<int>(good)] += t;
		ship->CargoTonnes -= t;
		ship->CargoCost -= cost;
		p->Upkeep += cost;
		// Paid for out of the account when it was loaded (cargo afloat until now): a cost of the plant's from here.
		Book(jd, cs::Cat("The ", o->Name(), " delivers ", cs::Fmt(t, "N0"), " t of ", cs::Strings::ToLowerInvariant(GoodInfo::Of(good)->Name), " to the ", p->Name), -cost);
		if (ship->CargoTonnes > 1e-6)
		{
			ship->Cargo = Economy::CargoText(ship);
			return false;
		}
		ship->HasCargo = false;
		ship->CargoTonnes = 0;
		ship->CargoCost = 0;
		ship->CargoBonus = 0;
		ship->CargoDestination = nullptr;
		ship->Cargo = cs::String();
		return true;
	}

	bool Holdings::LoadForPlant(const cs::Ref<OwnedShip>& o, int here, int to, double jd)
	{
		auto ship = o->Ship;
		if (ship->HasCargo || ship->CapacityTonnes <= 0) return false;
		for (const auto& p : Properties)
		{
			if (p->Lost || p->Kind != PropertyKind::Plant || p->Body != to || !p->Working || p->Recipe == nullptr) continue;
			auto store = p->Warehouse;
			if (store == nullptr || store->Lost) continue;
			double room = store->CapacityTonnes - store->StoredTonnes();
			if (room < 1) continue;
			// The feedstock furthest short of its days in stock (by value) that a market here sells and the hull can carry.
			cs::Ref<Settlement> from = nullptr;
			Good good = static_cast<Good>(0);
			double tonnes = 0, best = 0;
			for (int i = 0; i < p->Recipe->Inputs.Count(); i++)
			{
				if (!p->Recipe->Limiting[i]) continue;
				auto g = std::get<0>(p->Recipe->Inputs[i]);
				double want = PlantRate(p, g, false) * PlantStockDays - store->Stock[static_cast<int>(g)];
				if (want < 1 || !Economy::Carries(ship->Spec->Class, g)) continue;
				for (const auto& s : Economy()->At(here))
				{
					if (s == p->Settlement || !CanTradeAt(s) || !Economy()->CanTrade(ship, s)) continue;
					double t = cs::Math::Min(cs::Math::Min(want, room), cs::Math::Min(ship->CapacityTonnes, s->Available(g)));
					double v = t * GoodInfo::Of(g)->BasePrice;
					if (t >= 1 && v > best)
					{
						best = v;
						from = s;
						good = g;
						tonnes = t;
					}
				}
			}
			if (from == nullptr) continue;
			if (from->PurchaseCost(good, tonnes) * (1 + Economy::PortTax) > House->Treasury) continue;
			Economy()->LoadUnbooked(ship, from, good, tonnes, cs::Cat("the ", p->Name));
			o->FeedingPlant = p;
			return true;
		}
		return false;
	}

	// ---------------------------------------------------------------- the daily round

	void Holdings::Update(double jd)
	{
		if (cs::Double::IsNaN(nextTickJd)) return;
		for (int guard = 0; guard < 4000 && nextTickJd <= jd; guard++)
		{
			Tick(nextTickJd);
			nextTickJd += TickDays;
		}
	}

	void Holdings::Tick(double jd)
	{
		for (const auto& o : Ships)
		{
			if (o->Lost) continue;
			Settle(o, jd);
			if (CheckLost(o, jd)) continue;
			PayWages(o, jd);
			RepairInPort(o, jd);
		}
		for (const auto& p : Properties)
		{
			if (p->Lost) continue;
			switch (p->Kind)
			{
				case PropertyKind::Warehouse:
					if (Seized(p, jd)) break;
					Charge(p, WarehouseRentPerDay, jd, cs::Cat("Rent on the ", p->Name));
					break;
				case PropertyKind::StationShare:
					if (Seized(p, jd)) break;
					PayDividend(p, jd);
					break;
				case PropertyKind::MiningClaim:
					Charge(p, ClaimFeePerDay, jd, cs::Cat("Fee on the ", p->Name));
					break;
				case PropertyKind::Plant:
					if (Seized(p, jd)) break;
					RunPlant(p, jd);
					break;
			}
		}
		Reconcile(jd);
	}

	double Holdings::CargoAfloat() const
	{
		double t = 0;
		for (const auto& o : Ships)
			if (!o->Lost && o->Ship->HasCargo) t += o->Ship->CargoCost;
		return t;
	}

	double Holdings::Unsettled() const
	{
		double t = 0;
		for (const auto& o : Ships)
			if (!o->Lost) t += o->Ship->Credits - o->SeenCredits;
		return t;
	}

	void Holdings::Reconcile(double jd)
	{
		double diff = House->Treasury + CargoAfloat() - BookBalance;
		if (cs::Math::Abs(diff) < 1) return;
		Book(jd, diff > 0 ? "Other income (rescue fees, prizes, trade at the helm)" : "Other costs (ransoms, fines, trade at the helm)", diff);
	}

	void Holdings::Settle(const cs::Ref<OwnedShip>& o, double jd)
	{
		double delta = o->Ship->Credits - o->SeenCredits;
		o->SeenCredits = o->Ship->Credits;
		if (cs::Math::Abs(delta) < 0.5) return;
		// Out on charter: what it made or spent trading is the charterer's, who pays the house its hire instead.
		if (o->OnCharter() && o->Charter->Charterer != nullptr)
		{
			House->Treasury -= delta;
			o->Charter->Charterer->Cash += delta;
			o->Charter->TradeResult += delta;
			return;
		}
		if (delta > 0)
		{
			o->Earned += delta;
			if (o->Orders->Kind == StandingOrderKind::Mining && o->Orders->Claim != nullptr) o->Orders->Claim->Income += delta;
			Book(jd, cs::Cat("The ", o->Name(), "'s takings"), delta);
		}
		else
		{
			o->Spent += -delta;
			Book(jd, cs::Cat("The ", o->Name(), "'s propellant, tolls and losses on cargo"), delta);
		}
	}

	bool Holdings::CheckLost(const cs::Ref<OwnedShip>& o, double jd)
	{
		auto s = o->Ship;
		// null (no loss) is the empty string here.
		cs::String how = s->GateDeparted ? cs::String("left the system through a jump gate")
			: s->Destroyed ? cs::String("was destroyed")
			: s->Derelict ? cs::String("was boarded and cast adrift")
			: s->Faction != House ? cs::Cat("was taken by the ", s->Faction->Name)
			: cs::String();
		if (how.empty()) return false;
		o->Lost = true;
		o->LostJd = jd;
		o->LostHow = how;
		if (!s->GateDeparted)
		{
			double value = Economy::HullPrice(s->Spec->Class) * cs::Math::Max(0.1, 1 - (s->Combat ? s->Combat->Damage() : 0)) + (s->HasCargo ? s->CargoCost : 0);
			Book(jd, cs::Cat("The ", o->Name(), " ", how), -value, true, /*cash:*/ false);
			// The cargo it was carrying was paid for out of the account: that cash is gone with it.
			if (s->HasCargo) BookBalance -= s->CargoCost;
			// On charter the cargo was the charterer's, bought on the house's credit: it makes the house good for it.
			if (s->HasCargo && s->CargoCost > 0 && o->OnCharter() && o->Charter->Charterer != nullptr)
			{
				o->Charter->Charterer->Cash -= s->CargoCost;
				o->Charter->TradeResult -= s->CargoCost;
				House->Treasury += s->CargoCost;
				Book(jd, cs::Cat(o->Charter->Charterer->Name, " pays for its cargo lost aboard the ", o->Name()), s->CargoCost);
				value -= s->CargoCost;
			}
			world->Log(jd, cs::Cat("Your ship the ", o->Name(), " ", how), WorldEventKind::Loss, s->Orbit ? s->Orbit->Body : -1);
			ShipLost.Invoke(o, value, jd);
		}
		else Book(jd, cs::Cat("The ", o->Name(), " ", how), 0, /*earned:*/ false);
		// Anyone escorting it stands down.
		for (const auto& other : Ships)
			if (!other->Lost && other->Orders->Kind == StandingOrderKind::Escort && other->Orders->Charge == s) Order(other, StandingOrder::Hold());
		return true;
	}

	void Holdings::PayWages(const cs::Ref<OwnedShip>& o, double jd)
	{
		auto c = o->Captain;
		if (c == nullptr) return;
		double wage = (c->WagePerDay + Economy::RunningCostPerDay(o->Ship->Spec->Class)) * TickDays;
		if (House->Treasury < wage)
		{
			if (++c->DaysUnpaid >= QuitDays)
			{
				world->Log(jd, cs::Cat("Captain ", c->Name, " walks off the ", o->Name(), ": ", c->DaysUnpaid, " days unpaid"), WorldEventKind::Info, o->Ship->Orbit ? o->Ship->Orbit->Body : -1);
				Book(jd, cs::Cat("Captain ", c->Name, " quits the ", o->Name(), ", unpaid"), 0, /*earned:*/ false);
				o->Captain = nullptr;
			}
			return;
		}
		c->DaysUnpaid = 0;
		House->Treasury -= wage;
		o->Wages += wage;
		Book(jd, cs::Cat("Wages on the ", o->Name()), -wage);
	}

	void Holdings::RepairInPort(const cs::Ref<OwnedShip>& o, double jd)
	{
		auto s = o->Ship;
		if (s->Combat == nullptr || cs::Math::Max(s->Combat->Damage(), s->Combat->HullDamage()) <= 0.001 || s->Battle != nullptr || s == world->PilotedShip || (s->Phase != ShipPhase::Parked && s->Phase != ShipPhase::Landed)) return;
		auto yard = YardAt(s->Orbit->Body);
		if (yard == nullptr) return;
		// Sized by the hull's damage (the figure the ship waits on), not the part-diluted Damage (U.SPC-T549).
		double owed = cs::Math::Max(s->Combat->Damage(), s->Combat->HullDamage());
		double share = cs::Math::Min(RepairPerDay, owed);
		double unit = Economy::HullPrice(s->Spec->Class) * RepairRate;
		// Mend what the house can pay for rather than nothing at all.
		if (unit > 0 && House->Treasury < unit * share) share = cs::Math::Max(0.0, House->Treasury) / unit;
		if (share <= 1e-6) return;
		double bill = unit * share;
		s->Combat->Repair(share);
		House->Treasury -= bill;
		yard->Owner->Treasury += bill;
		o->Repairs += bill;
		Book(jd, cs::Cat("Repairs to the ", o->Name(), " at ", yard->Name), -bill);
	}

	cs::Ref<Settlement> Holdings::YardAt(int body) const
	{
		for (const auto& st : Economy()->At(body))
			if (CanBuyAt(st)) return st;
		return nullptr;
	}

	void Holdings::Charge(const cs::Ref<Property>& p, double amount, double jd, const cs::String& text)
	{
		House->Treasury -= amount;
		p->Upkeep += amount;
		Book(jd, text, -amount);
	}

	bool Holdings::Seized(const cs::Ref<Property>& p, double jd)
	{
		auto owner = p->Settlement ? p->Settlement->Owner : nullptr;
		// A port abandoned to ruin is gone for good too (nothing re-opens a ruin): no more rent on a store nobody
		// can sell from, nor a share of trade that will never come.
		bool abandoned = p->Settlement != nullptr && p->Settlement->Fallen && owner != nullptr && !owner->IsPirate;
		// A plant goes with its port: whoever takes the port in war takes the works (US-T265).
		bool takenOver = p->Kind == PropertyKind::Plant && p->Landlord != nullptr && owner != nullptr && owner != p->Landlord && !owner->IsPirate && !abandoned;
		if (owner != nullptr && !owner->IsPirate && !abandoned && !takenOver) return false;
		p->Lost = true;
		p->LostJd = jd;
		double value = p->PricePaid;
		if (p->Kind == PropertyKind::Warehouse)
			for (int g = 0; g < GoodInfo::Count; g++) value += p->Stock[g] * GoodInfo::Of(static_cast<Good>(g))->BasePrice;
		cs::String what = abandoned ? cs::Cat(" is lost with the abandoned ", p->Settlement->Name)
			: takenOver ? cs::Cat(" is seized by the ", owner->Name, ", who took ", p->Settlement->Name)
			: cs::String(" is seized");
		Book(jd, cs::Cat("The ", p->Name, what), -value, true, /*cash:*/ false);
		world->Log(jd, cs::Cat("Your ", p->Name, what), WorldEventKind::Loss, p->Body);
		PropertyLost.Invoke(p, value, jd);
		return true;
	}

	void Holdings::PayDividend(const cs::Ref<Property>& p, double jd)
	{
		auto s = p->Settlement;
		double traded = s->TradeVolumeCredits - p->SeenTradeVolume;
		p->SeenTradeVolume = s->TradeVolumeCredits;
		double dividend = cs::Math::Max(0.0, traded) * Economy::PortTax * p->Share;
		if (dividend < 1) return;
		dividend = cs::Math::Min(dividend, cs::Math::Max(0.0, s->Owner->Treasury));
		if (dividend < 1) return;
		s->Owner->Treasury -= dividend;
		House->Treasury += dividend;
		p->Income += dividend;
		Book(jd, cs::Cat("Dividend on the ", p->Name), dividend);
	}

	// ---------------------------------------------------------------- the captains' choices

	int Holdings::ChooseNext(const cs::Ref<Ship>& ship, int here, double jd)
	{
		auto o = Of(ship);
		if (o == nullptr) return -1;
		if (o->Captain == nullptr)
		{
			if (!ship->HasCargo) ship->Cargo = "Laid up, no captain";
			return here;
		}
		// Low on propellant with none to be had here: to the nearest port that sells it.
		if (world->BurnsPropellant(ship) && ship->PropellantFraction() < 0.3 && !Economy()->HasPropellantAt(ship, here))
		{
			int fuel = Economy()->NearestPropellant(ship, here, jd);
			if (fuel >= 0) return fuel;
		}
		// Badly hurt: to a yard, and wait there to be mended.
		if (ship->Combat != nullptr && ship->Combat->HullDamage() > RepairDamage)
		{
			// Wait at the yard only while the house can pay for the mending; otherwise trade on rather than sit unmended for good.
			if (YardAt(here) != nullptr && House->Treasury > Economy::HullPrice(ship->Spec->Class) * RepairRate * 0.001) return here;
			int yard = NearestYard(ship, here, jd);
			if (yard >= 0) return yard;
		}
		auto orders = o->Orders;
		switch (orders->Kind)
		{
			case StandingOrderKind::TradeRoute: return NextOnRoute(o, here, jd);
			case StandingOrderKind::Patrol: return NextWaypoint(o, here);
			case StandingOrderKind::Mining: return NextForMining(o, here, jd);
			case StandingOrderKind::Escort: return NextForEscort(o, here);
			case StandingOrderKind::FreeTrade: return NextFreeTrade(o, here, jd);
			default:
				if (!ship->HasCargo) ship->Cargo = "Holding in port";
				return here;
		}
	}

	double Holdings::Dwell(const cs::Ref<Ship>& ship, double normal)
	{
		auto o = Of(ship);
		if (o == nullptr) return normal;
		if (o->MiningDays > 0)
		{
			double d = o->MiningDays;
			o->MiningDays = 0;
			return d;
		}
		if (o->Captain == nullptr || o->Orders->Kind == StandingOrderKind::Hold) return 1;
		if (o->Orders->Kind == StandingOrderKind::Escort) return 0.1;
		return normal * (1.5 - 0.5 * o->Captain->Skill);
	}

	int Holdings::NextOnRoute(const cs::Ref<OwnedShip>& o, int here, double jd)
	{
		auto orders = o->Orders;
		const auto& stops = orders->Stops;
		auto ship = o->Ship;
		int n = stops.Count();
		int idx = orders->StopIndex % n;
		if (stops[idx] != here)
		{
			// Not where it was bound: if it is at another stop, go on from there; if off the route, make for the stop it was bound for.
			// A repeated stop: take the occurrence of here just before the stop it was bound for, not the first, so [A,B,A,C] bound for C does not jump back to B.
			int at = -1;
			for (int k = 1; k <= n && at < 0; k++) { int c = ((idx - k) % n + n) % n; if (stops[c] == here) at = c; }
			if (at < 0)
			{
				if (!ship->HasCargo) ship->Cargo = cs::Cat("Empty, joining its route at ", (*world->Catalog())[stops[idx]]->Name);
				return stops[idx];
			}
			idx = at;
		}
		// Skip a leg the full tanks can never cover (a lane gone down since the order was given): on to the next
		// stop that can be reached from here, else wait here and say why.
		int next = -1;
		cs::String unreachable;
		for (int k = 1; k <= n; k++)
		{
			int cand = (idx + k) % n;
			if (stops[cand] == here) continue;
			cs::String why = world->BurnsPropellant(ship) ? LegProblem(ship, here, stops[cand], jd) : cs::String();
			if (why.empty()) { next = cand; break; }
			if (unreachable.empty()) unreachable = why;
		}
		if (next < 0)
		{
			ship->Cargo = cs::Cat("Route unreachable: ", (!unreachable.empty() ? unreachable : cs::String("no other stop")));
			return here;
		}
		orders->StopIndex = next;
		int to = stops[next];
		if (ship->HasCargo)
		{
			// Cargo nobody here bought (an enlisted hold, free-trading leftovers, a market that lost its demand): sell it at the best reachable market, else dump it, then the route resumes.
			int sell = Economy()->PlanTrade(ship, here, jd);
			if (sell >= 0) return sell;
			DumpCargo(ship);
		}
		// Feedstock for a house plant at the next stop comes first (US-T265), then whatever pays best there.
		if (!ship->HasCargo) LoadForPlant(o, here, to, jd);
		if (!ship->HasCargo) Economy()->LoadFor(ship, here, to, jd);
		if (!ship->HasCargo) ship->Cargo = cs::Cat("Empty, on its route to ", (*world->Catalog())[to]->Name);
		return to;
	}

	void Holdings::DumpCargo(const cs::Ref<Ship>& ship)
	{
		Economy()->Release(ship);
		BookBalance -= ship->CargoCost;
		ship->HasCargo = false;
		ship->CargoTonnes = 0;
		ship->CargoCost = 0;
		ship->CargoBonus = 0;
		ship->CargoDestination = nullptr;
		ship->Cargo = cs::String();
	}

	int Holdings::NextFreeTrade(const cs::Ref<OwnedShip>& o, int here, double jd)
	{
		auto ship = o->Ship;
		int to = Economy()->PlanTrade(ship, here, jd);
		if (to >= 0) return to;
		if (!ship->HasCargo) ship->Cargo = o->OnCharter() ? cs::Cat("On charter to ", o->Charter->Charterer->Name, ", looking for a cargo") : cs::String("Free trading, looking for a cargo");
		return ship->HasCargo ? here : -1;
	}

	int Holdings::NextWaypoint(const cs::Ref<OwnedShip>& o, int here)
	{
		auto orders = o->Orders;
		const auto& stops = orders->Stops;
		int n = stops.Count();
		int idx = orders->StopIndex % n;
		if (stops[idx] == here) idx = (idx + 1) % n;
		orders->StopIndex = idx;
		o->Ship->Cargo = o->Ship->HasCargo ? o->Ship->Cargo : cs::String("On patrol");
		return stops[idx];
	}

	int Holdings::NextForMining(const cs::Ref<OwnedShip>& o, int here, double jd)
	{
		auto ship = o->Ship;
		auto claim = o->Orders->Claim;
		if (claim == nullptr || claim->Lost)
		{
			Order(o, StandingOrder::Hold());
			return here;
		}
		if (ship->HasCargo)
		{
			// Ore aboard nobody here bought: on to where it sells.
			int sell = Economy()->PlanTrade(ship, here, jd);
			if (sell >= 0) return sell;
			// No reachable market takes it: dump it (booking the loss) so the miner mines again rather than sit forever.
			DumpCargo(ship);
		}
		if (here != claim->Body)
		{
			ship->Cargo = cs::Cat("Empty, bound for the ", claim->Name);
			return claim->Body;
		}
		// On the claim: cut a holdful (as much as the tanks can still haul away) and sell it wherever it fetches most.
		double tonnes = ship->CapacityTonnes;
		if (tonnes <= 0) return here;
		// Size the load to what the tanks can haul to a market: halve it until some market is reachable with it aboard.
		int dest = -1;
		for (int tries = 0; tries < 5 && dest < 0; tries++)
		{
			dest = Economy()->LoadOwn(ship, claim->Ore, tonnes, here, jd);
			if (dest >= 0) break;
			ship->HasCargo = false;
			ship->CargoTonnes = 0;
			ship->Cargo = cs::String();
			if (tries < 4) tonnes /= 2;
		}
		if (dest < 0)
		{
			// No market it can haul to: the claim's own body may buy it where it lies.
			tonnes = ship->CapacityTonnes;
			if (Economy()->SellOwn(ship, claim->Ore, tonnes, here, jd))
			{
				o->TonnesMined += tonnes;
				o->MiningDays = tonnes / cs::Math::Max(1.0, claim->YieldPerDay);
				return here;
			}
			// Nowhere it can reach that takes it: leave it in the ground for now.
			ship->HasCargo = false;
			ship->CargoTonnes = 0;
			ship->Cargo = cs::Cat("Idle on the ", claim->Name, ": no market for its ore");
			return here;
		}
		o->TonnesMined += tonnes;
		o->MiningDays = tonnes / cs::Math::Max(1.0, claim->YieldPerDay);
		return dest;
	}

	int Holdings::NextForEscort(const cs::Ref<OwnedShip>& o, int here)
	{
		auto charge = o->Orders->Charge;
		if (charge == nullptr || charge->Destroyed || charge->Derelict || charge->GateDeparted)
		{
			Order(o, StandingOrder::Hold());
			return here;
		}
		o->Ship->Cargo = o->Ship->HasCargo ? o->Ship->Cargo : cs::Cat("Escorting ", charge->Name);
		int where = WhereBound(charge->Controller());
		return where >= 0 ? where : here;
	}

	int Holdings::WhereBound(const cs::Ref<Ship>& c) const
	{
		// The ship the player is flying keeps its simulated orbit until the helm is released: follow the body whose frame it is flown in.
		if (c == world->PilotedShip && PilotFrameBody >= 0) return PilotFrameBody;
		if (c->Battle != nullptr) return c->Battle->Body;
		if (c->Phase == ShipPhase::Transit || c->Phase == ShipPhase::Lane || c->Phase == ShipPhase::Lurk || c->Phase == ShipPhase::Adrift)
			// C#'s `Journey != null`: Ship.Journey is a List member (by value), so an empty list stands for null.
			return c->FinalBody >= 0 && c->Journey.Count() > 0 ? c->FinalBody : c->DestinationBody;
		if ((c->Phase == ShipPhase::Queued || c->Planetside()) && c->Ring != nullptr) return c->Ring->Body;
		return c->Orbit ? c->Orbit->Body : -1;
	}

	int Holdings::NearestYard(const cs::Ref<Ship>& ship, int here, double jd) const
	{
		int best = -1;
		double bestDays = cs::Double::MaxValue;
		for (const auto& st : Economy()->Settlements)
		{
			if (!CanBuyAt(st)) continue;
			if (st->Body != here && world->BurnsPropellant(ship) && !Economy()->CanReach(ship, here, st->Body, jd)) continue;
			double days = world->Fleet->EstimateTravelDays(ship, here, st->Body, jd);
			if (days < bestDays)
			{
				bestDays = days;
				best = st->Body;
			}
		}
		return best;
	}
}
