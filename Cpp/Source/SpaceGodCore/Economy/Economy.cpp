// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Economy.cs (US-T18).

#include "Economy/Economy.h"

#include "Combat/BulkFreighters.h"
#include "Combat/Diplomacy.h"
#include "Economy/BlackMarket.h"
#include "Economy/CargoHold.h"
#include "Economy/Colonisation.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/People.h"
#include "Economy/Propulsion.h"
#include "Economy/Settlement.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	namespace
	{
		void FillList(cs::List<double>& list, double v)
		{
			for (auto& x : list) x = v;
		}
	}

	double Economy::RouteFactorOf(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to) const
	{
		return RouteFactor == nullptr ? 1 : RouteFactor(ship, from, to);
	}

	Economy::Economy(cs::Ref<::SpaceGod::Core::Diplomacy> diplomacy, double startJd, int seed)
		: rng(seed)
	{
		Diplomacy = diplomacy;
		Jd = startJd;
		nextReviewJd = startJd + 10;
		lastReviewJd = startJd;
	}

	cs::Ref<Settlement> Economy::Add(const cs::String& name, int body, SettlementKind kind, cs::Ref<Faction> owner, double population)
	{
		auto s = cs::New<Settlement>();
		s->Id = Settlements.Count();
		s->Name = name;
		s->Body = body;
		s->Kind = kind;
		s->Owner = owner;
		s->Population = population;
		s->InitialPopulation = population;
		Settlements.Add(s);
		return s;
	}

	/// <summary>Recomputes every settlement's full-capacity demand and supply and stocks it at its target cover.</summary>
	void Economy::Initialise()
	{
		for (const auto& s : Settlements)
		{
			RecomputeFlows(s);
			for (int i = 0; i < GoodInfo::Count; i++)
			{
				s->Stock[i] = s->Target(static_cast<Good>(i)) * (0.8 + rng.NextDouble() * 0.5);
				s->Satisfaction[i] = 1;
			}
			s->RefreshPrices();
			People::AssignLabour(s);
		}
		People::Migrate(Settlements, Diplomacy, 0);
	}

	void Economy::Open(const cs::Ref<Settlement>& s)
	{
		RecomputeFlows(s);
		for (int i = 0; i < GoodInfo::Count; i++) s->Satisfaction[i] = 1;
		s->RefreshPrices();
		People::AssignLabour(s);
	}

	cs::List<cs::Ref<Settlement>> Economy::At(int body) const
	{
		cs::List<cs::Ref<Settlement>> r;
		for (const auto& s : Settlements)
			if (s->Body == body) r.Add(s);
		return r;
	}

	// ---------------------------------------------------------------- production and consumption

	void Economy::AdvanceTo(double jd)
	{
		double gap = jd - Jd;
		if (gap <= 0) return;
		// Very large jumps (a year per second) take coarser ticks; every update is stable at any step.
		double tick = cs::Math::Max(TickDays, gap / 600);
		while (Jd < jd)
		{
			double dt = cs::Math::Min(tick, jd - Jd);
			Tick(dt);
			Jd += dt;
			// Each market's board takes down the day's prices once a day (US-T263): read-only, so the sim is unchanged.
			for (const auto& s : Settlements) s->SamplePrices(Jd);
			if (Jd >= nextReviewJd)
			{
				Review();
				nextReviewJd = Jd + 10;
			}
		}
	}

	void Economy::Tick(double dt)
	{
		for (const auto& s : Settlements)
		{
			// A fallen haven is an empty shell until it is rebuilt: nobody there to work, eat or fence
			// (and the population floor below would otherwise bring its people straight back).
			if (s->Fallen)
			{
				// ... and nobody there going without.
				FillList(s->Satisfaction, 1.0);
				s->DaysSinceCapture += dt;
				continue;
			}
			FoldPropellantSales(s, dt);
			RecomputeFlows(s);
			People::AssignLabour(s);
			Produce(s, dt);
			Consume(s, dt);
			if (s->Kind == SettlementKind::Haven) Fence(s, dt);
			// A lawless station's middlemen pass what they bought on the black market into its own stores (U.SPC-T94).
			if (s->Lawless) BlackMarket::Launder(s, dt);
			s->DaysSinceCapture += dt;
			s->RefreshPrices();
		}
	}

	void Economy::RecomputeFlows(const cs::Ref<Settlement>& s)
	{
		FillList(s->Demand, 0);
		FillList(s->Supply, 0);
		for (const auto& [g, perPerson] : s->PersonNeeds()) s->Demand[static_cast<int>(g)] += perPerson * s->Population * s->RationOf(g);
		for (int i = 0; i < GoodInfo::Count; i++) s->Demand[i] += s->ExtraDemand[i] + s->PropellantDemand[i];
		for (const auto& f : s->Facilities)
		{
			for (const auto& [g, rate] : f->Recipe->Inputs) s->Demand[static_cast<int>(g)] += rate * f->Level;
			for (const auto& [g, rate] : f->Recipe->Outputs) s->Supply[static_cast<int>(g)] += rate * f->Level;
		}
	}

	void Economy::FoldPropellantSales(const cs::Ref<Settlement>& s, double dt)
	{
		double k = 1 - cs::Math::Exp(-dt / PropellantDemandDays);
		for (int i = 0; i < GoodInfo::Count; i++)
		{
			double rate = s->PropellantSold[i] / dt;
			s->PropellantDemand[i] += (rate - s->PropellantDemand[i]) * k;
			s->PropellantSold[i] = 0;
		}
	}

	void Economy::Produce(const cs::Ref<Settlement>& s, double dt)
	{
		// A recently captured settlement is in disarray: output recovers over 60 days.
		double disruption = cs::Math::Min(1.0, 0.25 + s->DaysSinceCapture / 60);
		// ... and restive under an occupier: unrest idles up to half of it (U.SPC-T92).
		disruption *= 1 - 0.5 * cs::Math::Max(0.0, cs::Math::Min(1.0, s->Unrest));
		double wages = 0;
		for (const auto& f : s->Facilities)
		{
			// Life support keeps running through a change of owner; industry does not. Neither runs
			// past its crew: an understaffed facility makes less, and below a minimum crew nothing.
			bool lifeSupport = f->LifeSupport();
			// A shock to the facility itself (an accident, a blight) and a strike (which life support works through).
			double run = (lifeSupport ? 1 : disruption * s->IndustryFactor) * f->Impairment * People::CrewFactor(f->Staffing), efficiency = 1;
			const auto& inputs = f->Recipe->Inputs;
			for (int k = 0; k < inputs.Count(); k++)
			{
				double need = std::get<1>(inputs[k]) * f->Level * dt;
				if (need <= 0) continue;
				double have = cs::Math::Min(1.0, s->Stock[static_cast<int>(std::get<0>(inputs[k]))] / need);
				if (f->Recipe->Limiting[k]) run = cs::Math::Min(run, have);
				else if (!lifeSupport) efficiency = cs::Math::Min(efficiency, Recipe::MinorFloor + (1 - Recipe::MinorFloor) * have);
			}
			for (const auto& [g, rate] : f->Recipe->Outputs)
			{
				// Full warehouses throttle production rather than overflow.
				double room = s->Capacity(g) - s->Stock[static_cast<int>(g)];
				double make = rate * f->Level * dt * efficiency;
				if (make > 0) run = cs::Math::Min(run, cs::Math::Max(0.0, room) / make);
			}
			run = cs::Math::Max(0.0, cs::Math::Min(1.0, run));
			f->Utilisation = run * efficiency;
			for (const auto& [g, rate] : f->Recipe->Inputs) s->Stock[static_cast<int>(g)] = cs::Math::Max(0.0, s->Stock[static_cast<int>(g)] - rate * f->Level * dt * run * efficiency);
			for (const auto& [g, rate] : f->Recipe->Outputs) s->Stock[static_cast<int>(g)] += rate * f->Level * dt * run * efficiency;
			if (f->Recipe->Kind == IndustryKind::Mine) f->Extracted += std::get<1>(f->Recipe->Outputs[0]) * f->Level * dt * run * efficiency;
			if (f->Recipe->Kind == IndustryKind::Shipyard)
			{
				double points = std::get<1>(f->Recipe->Inputs[0]) * f->Level * dt * run;
				s->BuildPoints = cs::Math::Min(s->BuildPoints + points, 400000.0);
			}
			// Workers take their share of what the facility actually added.
			wages += People::ValueAdded(s, f) * f->Utilisation * People::LabourShare;
		}
		// Havens pay no taxes: whatever their crews earn stays off the books.
		s->WageBill = s->Kind == SettlementKind::Haven ? 0 : wages;
		s->WageTax = s->WageBill * People::WageTaxRate;
		if (s->Owner != nullptr) s->Owner->Treasury += s->WageTax * dt;
	}

	void Economy::Consume(const cs::Ref<Settlement>& s, double dt)
	{
		for (int i = 0; i < s->Satisfaction.Count(); i++) s->Satisfaction[i] = 1;
		for (const auto& [g, perPerson] : s->PersonNeeds())
		{
			int i = static_cast<int>(g);
			double need = perPerson * s->Population * s->RationOf(g) * dt;
			if (need <= 0) continue;
			double eaten = cs::Math::Min(need, s->Stock[i]);
			s->Stock[i] -= eaten;
			s->Satisfaction[i] = eaten / need;
		}

		// Fed and watered settlements grow up to 4% a year, less without medicine (down to a slow
		// decline of about 5% a year with none); hunger or thirst shrinks them fast, to a hard core.
		double sustenance = cs::Math::Min(s->Satisfaction[static_cast<int>(Good::Food)], s->Satisfaction[static_cast<int>(Good::Water)]);
		double health = s->Satisfaction[static_cast<int>(Good::MedicalSupplies)];
		double rate = People::GrowthRate(sustenance, health);
		double grown = s->Population * (1 + rate * dt), cap = s->InitialPopulation * People::MaxGrowth;
		// Growth with no room to grow into is people with nowhere to go: the pressure that founds colonies (U.SPC-T98).
		if (grown > cap) People::AddOverflow(s, grown - cs::Math::Max(s->Population, cap));
		// ... and in a crowded place, a share of those born would rather go than squeeze in.
		else if (grown > s->Population) People::AddOverflow(s, (grown - s->Population) * People::Crowding(s));
		s->Population = cs::Math::Max(s->InitialPopulation * People::MinShare, cs::Math::Min(cap, grown));

		// How people have been living, remembered for about a month: what draws or drives migrants.
		double k = 1 - cs::Math::Exp(-dt / People::WellbeingDays);
		s->Wellbeing += (sustenance - s->Wellbeing) * k;
		s->HealthWellbeing += (health - s->HealthWellbeing) * k;
	}

	void Economy::Fence(const cs::Ref<Settlement>& s, double dt)
	{
		double share = 1 - cs::Math::Exp(-0.03 * dt);
		for (int i = 0; i < GoodInfo::Count; i++)
		{
			double moved = cs::Math::Max(0.0, s->Stock[i] - s->Target(static_cast<Good>(i))) * share;
			if (moved <= 0) continue;
			s->Stock[i] -= moved;
			s->Owner->Treasury += moved * GoodInfo::Of(static_cast<Good>(i))->BasePrice * 0.5;
		}
	}

	void Economy::Review()
	{
		// People move toward food, water and work, then the facilities follow the market.
		People::Migrate(Settlements, Diplomacy, Jd - lastReviewJd);
		lastReviewJd = Jd;
		for (const auto& s : Settlements)
		{
			// A fallen haven makes and trades nothing until it is rebuilt: its last prices are no market to follow.
			if (s->Fallen) continue;
			for (const auto& f : s->Facilities)
			{
				if (f->Recipe->Kind == IndustryKind::Shipyard) continue;
				double margin = Margin(s, f->Recipe);
				double step = 0;
				// An industry its owner favours grows on a thinner margin and is kept up through a glut (U.SPC-T423).
				auto [favoured, bonus] = Priority != nullptr ? Priority(s, f->Recipe->Kind) : std::tuple<bool, double>(false, 0.0);
				double grow = favoured ? 1.0 : 1.4, shrink = favoured ? 0.4 : 0.7;
				// Expanding takes hands: a facility short of crew does not grow.
				if (margin > grow && f->Utilisation > 0.8 && f->Staffing >= 0.95) step = 0.03 + bonus;
				else if (margin < shrink || f->Utilisation < 0.2) step = -0.02;
				f->Level = cs::Math::Max(f->BaseLevel * 0.4, cs::Math::Min(f->BaseLevel * 3, f->Level * (1 + step)));
			}

			auto yard = s->Shipyard();
			if (yard == nullptr || ChooseBuild == nullptr) continue;
			auto cls = ChooseBuild(s);
			s->OnSlip = cls;
			if (!cls.has_value()) continue;
			double cost = HullCost(*cls);
			if (s->BuildPoints < cost) continue;
			double price = HullPrice(*cls);
			// A hull a company ordered is already paid for; the owner pays for its own.
			bool prepaid = PaidFor != nullptr && PaidFor(s, *cls, price);
			if (!prepaid && s->Owner->Treasury < price) continue;
			// Nor is a hull laid down that nobody could crew.
			if (CanCrew != nullptr && !CanCrew(s, *cls)) continue;
			s->BuildPoints -= cost;
			if (!prepaid) s->Owner->Treasury -= price;
			if (ShipBuilt) ShipBuilt(s, *cls, Jd);
		}
		if (Colonisation) Colonisation->Review(shared_from_this(), Jd);
	}

	double Economy::Margin(const cs::Ref<Settlement>& s, const cs::Ref<Recipe>& r)
	{
		double input = 0, output = 0;
		for (const auto& [g, rate] : r->Inputs) input += rate * s->Price[static_cast<int>(g)];
		for (const auto& [g, rate] : r->Outputs) output += rate * s->Price[static_cast<int>(g)];
		return input <= 0 ? 2 : output / input;
	}

	double Economy::HullCost(ShipClass c)
	{
		switch (c)
		{
			case ShipClass::Carrier: return 90000;
			case ShipClass::Battleship: return 70000;
			case ShipClass::Cruiser: return 30000;
			case ShipClass::Destroyer: return 12000;
			case ShipClass::Freighter: return 9000;
			case ShipClass::Miner: return 7000;
			case ShipClass::Courier: return 1500;
			case ShipClass::Explorer: return 5000;
			case ShipClass::OreTug: return 6000;
			// Half an ore tug's frame and one drive fewer (U.SPC-T522).
			case ShipClass::CaptureTug: return 2800;
			case ShipClass::Interceptor: return 2200;
			case ShipClass::Bomber: return 4500;
			// An interceptor's airframe round a sensor and jamming suite: the electronics are the price (U.SPC-T485).
			case ShipClass::Recon: return 3800;
			case ShipClass::LaneTender: return 6000;
			case ShipClass::GatePicket: return 2500;
			// A battlecruiser's hull, its sensor array, communications suite and targeting computers dearer than guns (U.SPC-T477).
			case ShipClass::CommandCruiser: return 52000;
			// Smaller than a destroyer, but its engines and antimatter magazines are dear (U.SPC-T475).
			case ShipClass::Frigate: return 10000;
			// A small hull with no drive room: cheap enough to keep one at every port (U.SPC-T476).
			case ShipClass::Corvette: return 6000;
			// A bomber's armour round a troop bay and a docking collar (U.SPC-T486).
			case ShipClass::AssaultShuttle: return 4000;
			// More plate than a battleship, its own FTL drive and a siege engineering bay (U.SPC-T478).
			case ShipClass::GateBreaker: return 80000;
			default: return 3000;
		}
	}

	double Economy::HullPrice(ShipClass c) { return HullCost(c) * 1200; }

	// ---------------------------------------------------------------- trade

	double Economy::CapacityOf(ShipClass c)
	{
		switch (c)
		{
			// A truss freighter's hold is its stock design's pods (U.SPC-T482): 8 x 2,500 t.
			case ShipClass::Freighter: return BulkFreighters::StockPods * BulkFreighters::PodTonnes;
			case ShipClass::Miner: return 30000;
			case ShipClass::Courier: return 400;
			default: return 0;
		}
	}

	bool Economy::Carries(ShipClass c, Good g)
	{
		auto cat = GoodInfo::Of(g)->Category;
		switch (c)
		{
			case ShipClass::Freighter: return true;
			case ShipClass::Miner: return cat == GoodCategory::Mineral || g == Good::Water || g == Good::Nitrogen || g == Good::Hydrocarbons;
			case ShipClass::Courier:
				return g == Good::MedicalSupplies || g == Good::Electronics || g == Good::Platinum || g == Good::RareEarths || g == Good::HeliumThree;
			default: return false;
		}
	}

	double Economy::RunningCostPerDay(ShipClass c) { return c == ShipClass::Courier ? 400 : c == ShipClass::Miner ? 1200 : 1500; }

	double Economy::LaneToll(double massTonnes) { return LaneTollBase + LaneTollPerTonne * cs::Math::Max(0.0, massTonnes); }

	double Economy::LaneToll(ShipClass cls, double massTonnes) { return LaneToll(massTonnes) * BulkFreighters::LaneTollShareOf(cls); }

	Economy::Run Economy::PriceRun(const cs::Ref<Ship>& ship, int from, int to, double jd, double cargoTonnes, double tanks, double fuelPrice) const
	{
		auto cls = ship->Spec->Class;
		if (EstimateTrip == nullptr)
		{
			double d = TravelDays(ship, from, to, jd);
			Run r;
			r.Days = d;
			r.Cost = d * RunningCostPerDay(cls);
			r.Reachable = true;
			return r;
		}
		auto trip = EstimateTrip(ship, from, to, jd);
		double days = cs::Math::Max(0.05, trip.Days) + 1.0;
		double burn = Propulsion::PropellantFor(ship, trip.DeltaVKmS, cargoTonnes, tanks);
		double mass = ship->DryMassTonnes() + cargoTonnes + tanks;
		Run r;
		r.Days = days;
		r.Propellant = burn;
		r.Cost = days * RunningCostPerDay(cls) + burn * fuelPrice + trip.LaneHops * LaneToll(cls, mass);
		r.Reachable = burn * (1 + Propulsion::PlanningReserve) <= tanks;
		return r;
	}

	bool Economy::CanReach(const cs::Ref<Ship>& ship, int from, int to, double jd) const
	{
		return PriceRun(ship, from, to, jd, ship->HasCargo ? ship->CargoTonnes : 0, ship->Propellant(), 0).Reachable;
	}

	bool Economy::CanTrade(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) const
	{
		return !s->Fallen && Diplomacy->CanDockAt(ship->Faction, s) && (DockAllowed == nullptr || DockAllowed(ship, s));
	}

	bool Economy::LawRefuses(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) const
	{
		return DockAllowed != nullptr && Diplomacy->CanDockAt(ship->Faction, s) && !DockAllowed(ship, s);
	}

	double Economy::PriceFactorOf(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) const
	{
		return PriceFactor == nullptr ? 1 : cs::Math::Max(0.1, PriceFactor(ship, s));
	}

	void Economy::OnArrive(const cs::Ref<Ship>& ship, int body, double /*jd*/)
	{
		if (!ship->HasCargo) return;
		cs::Ref<Settlement> best = nullptr;
		double bestScore = 0, bestValue = 0;
		for (const auto& s : At(body))
		{
			if (!CanTrade(ship, s) || s->Demand[static_cast<int>(ship->CargoGood)] <= 0) continue;
			double v = s->SaleValue(ship->CargoGood, ship->CargoTonnes);
			// The booked market wins a near tie; it still pays only what the cargo is worth there.
			double score = s == ship->CargoDestination ? v * 1.05 : v;
			if (score > bestScore)
			{
				bestScore = score;
				bestValue = v;
				best = s;
			}
		}
		Release(ship);
		if (best == nullptr)
		{
			ship->CargoDestination = nullptr;
			ship->Cargo = CargoText(ship); // the label must not keep naming the port that refused it
			return; // Nobody here will buy; the trader carries it on.
		}
		Sell(ship, best, bestValue);
	}

	void Economy::Sell(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, double value)
	{
		int g = static_cast<int>(ship->CargoGood);
		s->Stock[g] += ship->CargoTonnes;
		double tax = value * PortTax;
		s->Owner->Treasury += tax;
		// Paid by the owner who offered it: a port taken since the load was booked does not pay its old owner's bonus.
		if (s == ship->CargoDestination && ship->CargoBonus > 0 && (ship->CargoBonusPayer == nullptr || s->Owner == ship->CargoBonusPayer))
		{
			s->Owner->Treasury -= ship->CargoBonus;
			value += ship->CargoBonus;
		}
		ship->CargoBonus = 0;
		double profit = value - tax - ship->CargoCost;
		ship->Credits += profit;
		ship->Faction->Treasury += value - tax;
		s->TradeVolumeCredits += value;
		if (Sold) Sold(ship, s, ship->CargoGood, ship->CargoTonnes);
		ship->HasCargo = false;
		ship->CargoTonnes = 0;
		ship->CargoCost = 0;
		ship->CargoDestination = nullptr;
		ship->Cargo = cs::String();
		s->RefreshPrices();
	}

	void Economy::Release(const cs::Ref<Ship>& ship)
	{
		if (ship->CargoDestination != nullptr && ship->HasCargo)
			ship->CargoDestination->Inbound[static_cast<int>(ship->CargoGood)] = cs::Math::Max(0.0, ship->CargoDestination->Inbound[static_cast<int>(ship->CargoGood)] - ship->CargoTonnes);
	}

	double Economy::LoseCargo(const cs::Ref<Ship>& ship, double tonnes, const cs::String& cause, const cs::Ref<Faction>& by)
	{
		if (ship == nullptr || !ship->HasCargo || !(tonnes > 0)) return 0;
		double lost = cs::Math::Min(tonnes, ship->CargoTonnes);
		auto good = ship->CargoGood;
		Release(ship);
		ship->CargoTonnes -= lost;
		if (ship->CargoTonnes <= 1e-6)
		{
			ship->HasCargo = false;
			ship->CargoDestination = nullptr;
			ship->Cargo = cs::String();
		}
		else if (ship->CargoDestination != nullptr)
			ship->CargoDestination->Inbound[static_cast<int>(good)] += ship->CargoTonnes; // what is left is still on its way
		if (CargoLost) CargoLost(ship, good, lost, cause, by);
		return lost;
	}

	void Economy::RecountInbound(const cs::List<cs::Ref<Ship>>& ships)
	{
		for (const auto& s : Settlements) FillList(s->Inbound, 0);
		for (const auto& ship : ships)
			if (!ship->Destroyed && ship->HasCargo && ship->CargoDestination != nullptr)
				ship->CargoDestination->Inbound[static_cast<int>(ship->CargoGood)] += ship->CargoTonnes;
	}

	int Economy::PlanTrade(const cs::Ref<Ship>& ship, int here, double jd)
	{
		auto cls = ship->Spec->Class;
		auto options = cs::List<Option>();

		double fuelHere = PropellantPriceAt(ship, here);
		if (ship->HasCargo)
		{
			// Already loaded (nobody here wanted it): find where it sells best.
			for (const auto& to : Settlements)
			{
				if (to->Body == here || !CanTrade(ship, to) || to->Demand[static_cast<int>(ship->CargoGood)] <= 0) continue;
				auto run = PriceRun(ship, here, to->Body, jd, ship->CargoTonnes, ship->Propellant(), fuelHere);
				if (!run.Reachable) continue;
				double factor = RouteFactorOf(ship, nullptr, to);
				if (factor <= 0) continue;
				double value = to->SaleValue(ship->CargoGood, ship->CargoTonnes) * (1 - DangerAt(to->Body));
				double score = (value - run.Cost) / run.Days;
				Option o;
				o.To = to;
				o.Good = ship->CargoGood;
				o.Tonnes = ship->CargoTonnes;
				o.Score = score > 0 ? score * factor : score;
				options.Add(o);
			}
			if (options.Count() == 0) return LowOnPropellant(ship, here, jd);
			auto pick = Choose(options);
			Release(ship);
			ship->CargoBonus = 0;
			ship->CargoDestination = pick.To;
			pick.To->Inbound[static_cast<int>(pick.Good)] += pick.Tonnes;
			ship->Cargo = CargoText(ship);
			return pick.To->Body;
		}

		for (const auto& from : At(here))
		{
			if (!CanTrade(ship, from)) continue;
			AddLoadedOptions(ship, from, here, jd, 0, 0, ship->Propellant(), fuelHere, options);
		}

		if (options.Count() == 0 || Best(options) <= 0)
		{
			// Nothing worth carrying from here: reposition, empty, to where the trades are.
			for (const auto& from : Settlements)
			{
				if (from->Body == here || !CanTrade(ship, from)) continue;
				auto leg = PriceRun(ship, here, from->Body, jd, 0, ship->Propellant(), fuelHere);
				if (!leg.Reachable) continue;
				// It fills up again there if the port sells propellant; else it goes on with what is left.
				double fuelThere = PropellantPriceAt(ship, from->Body);
				double tanks = HasPropellant(ship, from) ? ship->PropellantCapacity() : ship->Propellant() - leg.Propellant;
				auto onward = cs::List<Option>();
				AddLoadedOptions(ship, from, from->Body, jd + leg.Days, leg.Days, leg.Cost - leg.Days * RunningCostPerDay(cls), tanks, fuelThere, onward);
				double best = Best(onward);
				if (best > 0)
				{
					Option o;
					o.To = from;
					o.Score = best * 0.85;
					o.Empty = true;
					options.Add(o);
				}
			}
		}

		options.RemoveAll([](const Option& o) { return o.Score <= 0; });
		if (options.Count() == 0) return LowOnPropellant(ship, here, jd);
		auto choice = Choose(options);
		if (!choice.Empty) Load(ship, choice);
		else ship->Cargo = cs::Cat("Empty, repositioning to ", choice.To->Name);
		return choice.To->Body;
	}

	bool Economy::LoadFor(const cs::Ref<Ship>& ship, int here, int toBody, double jd)
	{
		if (ship->HasCargo || ship->CapacityTonnes <= 0) return false;
		double fuelHere = PropellantPriceAt(ship, here);
		auto options = cs::List<Option>();
		for (const auto& from : At(here))
		{
			if (!CanTrade(ship, from)) continue;
			AddLoadedOptions(ship, from, here, jd, 0, 0, ship->Propellant(), fuelHere, options);
		}
		Option best{};
		bool found = false;
		for (const auto& o : options)
			if (o.To->Body == toBody && o.Score > 0 && (!found || o.Score > best.Score))
			{
				best = o;
				found = true;
			}
		if (!found) return false;
		Load(ship, best);
		return true;
	}

	int Economy::LoadOwn(const cs::Ref<Ship>& ship, Good good, double tonnes, int here, double jd)
	{
		if (ship->HasCargo || tonnes <= 0) return -1;
		ship->HasCargo = true;
		ship->CargoGood = good;
		ship->CargoTonnes = tonnes;
		ship->CargoCost = 0;
		ship->CargoBonus = 0;
		ship->CargoDestination = nullptr;
		ship->Cargo = CargoText(ship);
		return PlanTrade(ship, here, jd);
	}

	bool Economy::SellOwn(const cs::Ref<Ship>& ship, Good good, double tonnes, int here, double jd)
	{
		if (ship->HasCargo || tonnes <= 0) return false;
		bool wanted = false;
		for (const auto& s : At(here))
			if (CanTrade(ship, s) && s->Demand[static_cast<int>(good)] > 0) { wanted = true; break; }
		if (!wanted) return false;
		ship->HasCargo = true;
		ship->CargoGood = good;
		ship->CargoTonnes = tonnes;
		ship->CargoCost = 0;
		ship->CargoBonus = 0;
		ship->CargoDestination = nullptr;
		OnArrive(ship, here, jd);
		if (!ship->HasCargo) return true;
		ship->HasCargo = false;
		ship->CargoTonnes = 0;
		ship->Cargo = cs::String();
		return false;
	}

	void Economy::AddLoadedOptions(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, int fromBody, double jd, double extraDays, double extraCost, double tanks, double fuelPrice, cs::List<Option>& into)
	{
		auto cls = ship->Spec->Class;
		double capacity = ship->CapacityTonnes;
		for (int g = 0; g < GoodInfo::Count; g++)
		{
			auto good = static_cast<Good>(g);
			if (!Carries(cls, good)) continue;
			double available = from->Available(good);
			double minimum = cs::Math::Min(capacity * 0.02, 20.0);
			if (available < cs::Math::Min(minimum, MinLoadTonnes)) continue;
			for (const auto& to : Settlements)
			{
				if (to->Body == fromBody || !CanTrade(ship, to)) continue;
				double factor = RouteFactorOf(ship, from, to);
				if (factor <= 0) continue;
				double tonnes = cs::Math::Min(cs::Math::Min(capacity, available), to->Absorb(good));
				// A small market's whole want of a good can be less than a usual load: it takes what it wants
				// (down to MinLoadTonnes), if a run that small pays.
				if (tonnes < cs::Math::Min(minimum, cs::Math::Max(MinLoadTonnes, to->Target(good) * 0.5))) continue;
				auto run = PriceRun(ship, fromBody, to->Body, jd, tonnes, tanks, fuelPrice);
				if (!run.Reachable) continue;
				double cost = from->PurchaseCost(good, tonnes);
				if (ship->Faction->IsHouse && cost * (1 + PortTax) > ship->Faction->Treasury)
				{
					// The house cannot overdraw its account: take only the lot it can pay for (U.SPC-T539).
					double afford = cs::Math::Max(0.0, ship->Faction->Treasury) / (cost * (1 + PortTax)) * 0.95;
					tonnes *= afford;
					if (tonnes < cs::Math::Min(minimum, MinLoadTonnes)) continue;
					cost = from->PurchaseCost(good, tonnes);
					if (cost * (1 + PortTax) > ship->Faction->Treasury) continue;
					run = PriceRun(ship, fromBody, to->Body, jd, tonnes, tanks, fuelPrice);
					if (!run.Reachable) continue;
				}
				double days = run.Days + extraDays;
				double bonus = ContractBonus(to, good, tonnes);
				double value = (SaleEstimate(to, good, tonnes) + bonus) * (1 - DangerAt(to->Body));
				double profit = value - cost - run.Cost - extraCost - extraDays * RunningCostPerDay(cls);
				double score = profit / days;
				Option o;
				o.From = from;
				o.To = to;
				o.Good = good;
				o.Tonnes = tonnes;
				o.Cost = cost;
				o.Bonus = bonus;
				o.Score = score > 0 ? score * factor : score;
				into.Add(o);
			}
		}
	}

	double Economy::ContractBonus(const cs::Ref<Settlement>& to, Good g, double tonnes)
	{
		if (GoodInfo::Of(g)->Category != GoodCategory::Essential || to->Owner->IsPirate || to->Owner->Treasury <= 0) return 0;
		double expected = to->Stock[static_cast<int>(g)] + to->Inbound[static_cast<int>(g)];
		double target = to->Target(g);
		if (target <= 0 || expected >= target * 0.7) return 0;
		double shortfall = 1 - expected / (target * 0.7);
		return shortfall * (600000 + tonnes * GoodInfo::Of(g)->BasePrice * 1.5);
	}

	double Economy::SaleEstimate(const cs::Ref<Settlement>& to, Good g, double tonnes)
	{
		constexpr int steps = 4;
		double value = 0, stock = to->Stock[static_cast<int>(g)] + to->Inbound[static_cast<int>(g)];
		for (int k = 0; k < steps; k++)
		{
			double chunk = tonnes / steps;
			value += to->PriceAt(g, stock + chunk * 0.5) * chunk;
			stock += chunk;
		}
		return value * 0.97 * (1 - PortTax);
	}

	void Economy::Load(const cs::Ref<Ship>& ship, const Option& o)
	{
		int g = static_cast<int>(o.Good);
		o.From->Stock[g] = cs::Math::Max(0.0, o.From->Stock[g] - o.Tonnes);
		o.From->Owner->Treasury += o.Cost * PortTax;
		o.From->TradeVolumeCredits += o.Cost;
		o.To->Inbound[g] += o.Tonnes;
		ship->HasCargo = true;
		ship->CargoGood = o.Good;
		ship->CargoTonnes = o.Tonnes;
		ship->CargoCost = o.Cost * (1 + PortTax);
		ship->Faction->Treasury -= ship->CargoCost;
		ship->CargoBonus = o.Bonus;
		ship->CargoBonusPayer = o.To->Owner;
		ship->CargoDestination = o.To;
		ship->Cargo = CargoText(ship);
		o.From->RefreshPrices();
		if (Loaded) Loaded(ship, o.From);
	}

	void Economy::LoadFor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, Good good, double tonnes, const cs::Ref<Settlement>& to)
	{
		Option o;
		o.From = from;
		o.To = to;
		o.Good = good;
		o.Tonnes = tonnes;
		o.Cost = from->PurchaseCost(good, tonnes);
		Load(ship, o);
	}

	double Economy::LoadUnbooked(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& from, Good good, double tonnes, const cs::String& bound)
	{
		int g = static_cast<int>(good);
		double cost = from->PurchaseCost(good, tonnes);
		from->Stock[g] = cs::Math::Max(0.0, from->Stock[g] - tonnes);
		from->Owner->Treasury += cost * PortTax;
		from->TradeVolumeCredits += cost;
		ship->HasCargo = true;
		ship->CargoGood = good;
		ship->CargoTonnes = tonnes;
		ship->CargoCost = cost * (1 + PortTax);
		ship->Faction->Treasury -= ship->CargoCost;
		ship->CargoBonus = 0;
		ship->CargoDestination = nullptr;
		ship->Cargo = cs::Cat(cs::Fmt(tonnes, "N0"), " t ", GoodInfo::Of(good)->Name, " for ", bound);
		from->RefreshPrices();
		if (Loaded) Loaded(ship, from);
		return ship->CargoCost;
	}

	cs::String Economy::CargoText(const cs::Ref<Ship>& ship)
	{
		if (!ship->HasCargo) return cs::String();
		return cs::Cat(cs::Fmt(ship->CargoTonnes, "N0"), " t ", GoodInfo::Of(ship->CargoGood)->Name,
			(ship->CargoDestination != nullptr ? cs::Cat(" for ", ship->CargoDestination->Name) : cs::String()));
	}

	int Economy::LowOnPropellant(const cs::Ref<Ship>& ship, int here, double jd)
	{
		if (ship->PropellantFraction() >= 0.5) return -1;
		auto port = NearestPropellantPort(ship, here, jd);
		if (port == nullptr) return -1;
		if (!ship->HasCargo) ship->Cargo = cs::Cat("Low on propellant, making for ", port->Name);
		return port->Body;
	}

	int Economy::NearestPropellant(const cs::Ref<Ship>& ship, int here, double jd) const
	{
		auto port = NearestPropellantPort(ship, here, jd);
		return port ? port->Body : -1;
	}

	cs::Ref<Settlement> Economy::NearestPropellantPort(const cs::Ref<Ship>& ship, int here, double jd) const
	{
		if (EstimateTrip == nullptr || HasPropellantAt(ship, here)) return nullptr;
		cs::Ref<Settlement> best = nullptr;
		double bestDays = cs::Double::MaxValue;
		for (const auto& s : Settlements)
		{
			if (s->Body == here || !HasPropellant(ship, s)) continue;
			auto run = PriceRun(ship, here, s->Body, jd, ship->HasCargo ? ship->CargoTonnes : 0, ship->Propellant(), 0);
			if (!run.Reachable || run.Days >= bestDays) continue;
			bestDays = run.Days;
			best = s;
		}
		return best;
	}

	double Economy::TravelDays(const cs::Ref<Ship>& ship, int from, int to, double jd) const
	{
		return cs::Math::Max(0.05, EstimateTravelDays != nullptr ? EstimateTravelDays(ship, from, to, jd) : 10) + 1.0;
	}

	double Economy::DangerAt(int body) const
	{
		return Danger != nullptr ? cs::Math::Max(0.0, cs::Math::Min(0.9, Danger(body))) : 0;
	}

	double Economy::Best(const cs::List<Option>& options)
	{
		double best = cs::Double::NegativeInfinity;
		for (const auto& o : options) best = cs::Math::Max(best, o.Score);
		return best;
	}

	Economy::Option Economy::Choose(cs::List<Option>& options)
	{
		options.Sort([](const Option& a, const Option& b) { return cs::CompareTo(b.Score, a.Score); });
		int n = cs::Math::Min(4, options.Count());
		double top = cs::Math::Max(options[0].Score, 1e-9);
		double total = 0;
		auto weights = cs::NewArray<double>(n);
		for (int i = 0; i < n; i++)
		{
			double w = cs::Math::Max(0.0, options[i].Score / top);
			weights[i] = w * w * w;
			total += weights[i];
		}
		double pick = rng.NextDouble() * total;
		for (int i = 0; i < n; i++)
		{
			pick -= weights[i];
			if (pick <= 0) return options[i];
		}
		return options[0];
	}

	// ---------------------------------------------------------------- propellant

	bool Economy::HasPropellant(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) const
	{
		if (s->Fallen || !CanTrade(ship, s)) return false;
		for (auto g : Propulsion::ReactionMass())
			if (s->Available(g) >= 1) return true;
		return false;
	}

	bool Economy::HasPropellantAt(const cs::Ref<Ship>& ship, int body) const
	{
		for (const auto& s : At(body))
			if (HasPropellant(ship, s)) return true;
		return false;
	}

	double Economy::PropellantPrice(const cs::Ref<Settlement>& s)
	{
		double mass = cs::Double::MaxValue;
		for (auto g : Propulsion::ReactionMass())
			if (s->Available(g) >= 1) mass = cs::Math::Min(mass, s->Price[static_cast<int>(g)]);
		if (mass == cs::Double::MaxValue)
			for (auto g : Propulsion::ReactionMass()) mass = cs::Math::Min(mass, s->Price[static_cast<int>(g)]);
		return (mass + Propulsion::HeliumThreePerTonne * s->Price[static_cast<int>(Good::HeliumThree)]) * (1 + PortTax);
	}

	double Economy::PropellantPriceAt(const cs::Ref<Ship>& ship, int body) const
	{
		double best = cs::Double::MaxValue;
		for (const auto& s : At(body))
			if (HasPropellant(ship, s)) best = cs::Math::Min(best, PropellantPrice(s));
		return best == cs::Double::MaxValue ? Propulsion::PropellantBasePrice() * (1 + PortTax) : best;
	}

	double Economy::Refuel(const cs::Ref<Ship>& ship, int body, double /*jd*/, bool force)
	{
		if (ship->PropellantCapacity() <= 0 || (!force && ship->PropellantFraction() >= Propulsion::TopUpBelow)) return 0;
		auto markets = cs::List<cs::Ref<Settlement>>();
		for (const auto& s : At(body))
			if (HasPropellant(ship, s)) markets.Add(s);
		markets.Sort([](const cs::Ref<Settlement>& a, const cs::Ref<Settlement>& b) { return cs::CompareTo(PropellantPrice(a), PropellantPrice(b)); });
		double taken = 0;
		for (const auto& s : markets)
		{
			double room = ship->PropellantCapacity() - ship->Propellant();
			if (room < 1) break;
			double discard;
			taken += BuyPropellant(ship, s, room, /*mustAfford:*/ ship->Faction->IsHouse, discard);
		}
		return taken;
	}

	Economy::TradeOutcome Economy::RefuelAt(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, double& tonnes, double& cost)
	{
		tonnes = cost = 0;
		if (!CanTrade(ship, s)) return LawRefuses(ship, s) ? TradeOutcome::Refused : TradeOutcome::CannotDock;
		double room = ship->PropellantCapacity() - ship->Propellant();
		if (room < 0.01) return TradeOutcome::NoRoom;
		if (!HasPropellant(ship, s)) return TradeOutcome::NoStock;
		tonnes = BuyPropellant(ship, s, room, /*mustAfford:*/ true, cost);
		return tonnes > 0 ? TradeOutcome::Ok : TradeOutcome::CantAfford;
	}

	double Economy::BuyPropellant(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, double tonnes, bool mustAfford, double& cost)
	{
		cost = 0;
		double got = 0;
		auto order = cs::List<Good>(Propulsion::ReactionMass());
		order.Sort([&s](Good a, Good b) { return cs::CompareTo(s->Price[static_cast<int>(a)], s->Price[static_cast<int>(b)]); });
		auto payer = ship->Faction;
		for (auto g : order)
		{
			double t = cs::Math::Min(tonnes - got, s->Available(g));
			if (t < 0.01) continue;
			// Each tonne also takes its helium-3, where there is any, so price the pair together.
			double heRatio = s->Available(Good::HeliumThree) > 0 ? Propulsion::HeliumThreePerTonne : 0;
			double total = (s->PurchaseCost(g, t) + s->PurchaseCost(Good::HeliumThree, t * heRatio)) * (1 + PortTax);
			if (mustAfford && total > payer->Treasury - cost)
			{
				double afford = cs::Math::Max(0.0, payer->Treasury - cost);
				if (afford <= 0 || total <= 0) break;
				t *= afford / total * 0.99;
				if (t < 0.01) break;
			}
			got += t;
			cost += Purchase(s, g, t);
			double he = cs::Math::Min(t * heRatio, s->Available(Good::HeliumThree));
			if (he > 0) cost += Purchase(s, Good::HeliumThree, he);
		}
		if (got <= 0) return 0;
		ship->SetPropellant(ship->Propellant() + got);
		payer->Treasury -= cost;
		ship->Credits -= cost;
		s->RefreshPrices();
		return got;
	}

	double Economy::Purchase(const cs::Ref<Settlement>& s, Good g, double tonnes)
	{
		double price = s->PurchaseCost(g, tonnes);
		s->Stock[static_cast<int>(g)] = cs::Math::Max(0.0, s->Stock[static_cast<int>(g)] - tonnes);
		s->Owner->Treasury += price * PortTax;
		s->TradeVolumeCredits += price;
		s->PropellantSold[static_cast<int>(g)] += tonnes;
		return price * (1 + PortTax);
	}

	// ---------------------------------------------------------------- player trade

	const char* ToString(Economy::TradeOutcome v)
	{
		switch (v)
		{
			case Economy::TradeOutcome::Ok: return "Ok";
			case Economy::TradeOutcome::CannotDock: return "CannotDock";
			case Economy::TradeOutcome::NoStock: return "NoStock";
			case Economy::TradeOutcome::NoRoom: return "NoRoom";
			case Economy::TradeOutcome::CantAfford: return "CantAfford";
			case Economy::TradeOutcome::NothingToSell: return "NothingToSell";
			case Economy::TradeOutcome::Refused: return "Refused";
			case Economy::TradeOutcome::Contraband: return "Contraband";
		}
		return "";
	}

	Economy::TradeOutcome Economy::BuyIntoHold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good good, int crates, int& bought)
	{
		TradeOutcome discard;
		return BuyIntoHold(ship, s, good, crates, bought, discard);
	}

	Economy::TradeOutcome Economy::BuyIntoHold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good good, int crates, int& bought, TradeOutcome& stoppedBy)
	{
		bought = 0;
		stoppedBy = TradeOutcome::Ok;
		if (!CanTrade(ship, s)) return stoppedBy = LawRefuses(ship, s) ? TradeOutcome::Refused : TradeOutcome::CannotDock;
		double unitTonnes = CargoFootprint::Of(good).Tonnes;
		double factor = PriceFactorOf(ship, s);
		auto stopped = TradeOutcome::Ok;
		for (int i = 0; i < crates; i++)
		{
			if (s->Available(good) < unitTonnes) { stopped = TradeOutcome::NoStock; break; }
			double cost = s->PurchaseCost(good, unitTonnes) * factor;
			double total = cost * (1 + PortTax);
			if (ship->Faction->Treasury < total) { stopped = TradeOutcome::CantAfford; break; }
			auto crate = ship->PersonalHold()->AddFirstFit(good);
			if (crate == nullptr) { stopped = TradeOutcome::NoRoom; break; }
			// What a haven sells is plunder: stolen goods, contraband to any lawful patrol.
			crate->Stolen = s->Owner != nullptr && s->Owner->IsPirate;

			s->Stock[static_cast<int>(good)] = cs::Math::Max(0.0, s->Stock[static_cast<int>(good)] - unitTonnes);
			s->Owner->Treasury += cost * PortTax;
			s->TradeVolumeCredits += cost;
			ship->Faction->Treasury -= total;
			bought++;
		}
		// Prices follow the stock that changed, however the run of crates ended.
		if (bought > 0) s->RefreshPrices();
		stoppedBy = stopped;
		return bought > 0 ? TradeOutcome::Ok : stopped;
	}

	Economy::TradeOutcome Economy::SellFromHold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s, Good good, int crates, int& sold)
	{
		sold = 0;
		if (!CanTrade(ship, s)) return LawRefuses(ship, s) ? TradeOutcome::Refused : TradeOutcome::CannotDock;
		auto hold = ship->PersonalHold();
		// A lawful market will not touch stolen goods; a haven takes anything, and a lawless station's black market
		// takes stolen goods at its own prices (U.SPC-T94).
		bool haven = s->Owner != nullptr && s->Owner->IsPirate;
		bool fence = haven || BlackMarket::Takes(s);
		double factor = PriceFactorOf(ship, s);
		auto stopped = TradeOutcome::Ok;
		for (int i = 0; i < crates; i++)
		{
			cs::Ref<CargoItem> item = nullptr;
			bool stolenOnly = false;
			for (const auto& it : hold->Items())
			{
				if (it->Good != good) continue;
				if (it->Stolen && !fence) { stolenOnly = true; continue; }
				item = it;
				break;
			}
			if (item == nullptr) { stopped = stolenOnly ? TradeOutcome::Contraband : TradeOutcome::NothingToSell; break; }

			double tonnes = item->Tonnes();
			// Stolen goods sold at a lawless station go to its black market, not its open board.
			bool black = item->Stolen && !haven;
			double value = (black ? BlackMarket::SaleValue(s, good, tonnes) : s->SaleValue(good, tonnes)) / factor;
			// A black sale is paid out of the station owner's treasury (as BlackMarket.Sell does) and is untaxed.
			double tax = black ? 0 : value * PortTax;
			hold->Remove(item);
			if (black) s->BlackStock[static_cast<int>(good)] += tonnes;
			else s->Stock[static_cast<int>(good)] += tonnes;
			if (black) s->Owner->Treasury -= value;
			else s->Owner->Treasury += tax;
			s->TradeVolumeCredits += value;
			ship->Faction->Treasury += value - tax;
			sold++;
		}
		// Prices follow the stock that changed, however the run of crates ended.
		if (sold > 0) s->RefreshPrices();
		return sold > 0 ? TradeOutcome::Ok : stopped;
	}

	// ---------------------------------------------------------------- queries

	double Economy::PriceIndex(Good g) const
	{
		double sum = 0;
		int n = 0;
		for (const auto& s : Settlements)
		{
			if (s->Demand[static_cast<int>(g)] <= 0) continue;
			sum += s->Price[static_cast<int>(g)] / GoodInfo::Of(g)->BasePrice;
			n++;
		}
		return n > 0 ? sum / n : 1;
	}

	void Economy::Capture(const cs::Ref<Settlement>& s, const cs::Ref<Faction>& newOwner)
	{
		s->Owner = newOwner;
		s->DaysSinceCapture = 0;
		s->BuildPoints *= 0.3;
	}
}
