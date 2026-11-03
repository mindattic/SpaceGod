// Port of Assets/UnitySpace/Core/Systems/SocietySpec.cs (US-T17): SocietyRole, FactionSpec, SettlementSpec, SocietySpec,
// and World's SocietySpec.cs part (declared in World.h).

#include "Systems/SocietySpec.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/AlphaCentauri.h"
#include "Systems/Polities.h"
#include "BodyCatalog.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "World.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/PatrolWings.h"
#include "Combat/War.h"
#include "Economy/Companies.h"
#include "Economy/Economy.h"
#include "Economy/LocalTraffic.h"
#include "Infrastructure/Infrastructure.h"

namespace SpaceGod::Core
{
	const char* ToString(SocietyRole v)
	{
		switch (v)
		{
		case SocietyRole::Authority: return "Authority";
		case SocietyRole::Company: return "Company";
		case SocietyRole::Raiders: return "Raiders";
		}
		return "";
	}

	FactionSpec::FactionSpec(const cs::String& name, const cs::String& prefix, Rgb color, double treasury, double marines)
	{
		Name = name;
		Prefix = prefix;
		Color = color;
		Treasury = treasury;
		Marines = marines;
	}

	double SocietySpec::Frontier(double ly)
	{
		return cs::Math::Min(1.0, cs::Math::Pow(AlphaCentauri::DistanceLy / cs::Math::Max(ly, 1e-9), 2));
	}

	double SocietySpec::Population() const
	{
		double p = 0;
		for (const auto& s : Settlements) p += s->Population * Scale();
		return p;
	}

	void SocietySpec::AddStations(const cs::Ref<SystemSpec>& spec) const
	{
		for (const auto& st : Stations)
			if (!spec->Stations.Exists([&](const cs::Ref<StationSpec>& x) { return x->Name == st->Name; })) spec->Stations.Add(st);
	}

	cs::Ref<SettlementSpec> SocietySpec::Add(const cs::String& name, const cs::String& body, SettlementKind kind, SocietyRole owner, double population, const cs::String& note,
		const cs::List<Good>& mines, const cs::List<IndustryKind>& makes, double shipyard, double munitions, bool hidden, bool lawless)
	{
		auto s = cs::New<SettlementSpec>();
		s->Name = name; s->Body = body; s->Kind = kind; s->Owner = owner; s->Population = population; s->Note = note;
		s->Mines = mines; s->Makes = makes; s->Shipyard = shipyard; s->Munitions = munitions; s->Hidden = hidden;
		s->Lawless = lawless;
		Settlements.Add(s);
		return s;
	}

	void SocietySpec::Station(const cs::String& name, const cs::String& host, double aAu, double meanAnomalyDeg, const cs::String& summary)
	{
		auto st = cs::New<StationSpec>();
		st->Name = name; st->Host = host; st->AAu = aAu; st->MeanAnomalyDeg = meanAnomalyDeg; st->Summary = "Fiction. " + summary;
		Stations.Add(st);
	}

	void SocietySpec::HavenSite(const cs::String& name, const cs::String& host, double aAu, double meanAnomalyDeg, const cs::String& summary)
	{
		Station(name, host, aAu, meanAnomalyDeg, summary);
		HavenSites.Add(name);
	}

	// ================================================================ World (SocietySpec.cs part)

	/// <summary>
	/// A nearby system settled by <paramref name="society"/> (U.SPC-T90): the same machinery as Sol and Alpha
	/// Centauri, sized to how far out it is. Every settlement grows most of its own food and water; the mines
	/// and factories are then sized so that the system as a whole makes a little more of every good than it
	/// uses (<see cref="BalanceSupply"/>), leaving the traders a living carrying the rest about.
	/// </summary>
	cs::Ref<World> World::CreateSettled(cs::Ref<SpaceGod::Core::Ephemeris> ephemeris, double jd, int seed, cs::Ref<SocietySpec> society, cs::Ref<SpaceGod::Core::Polities> polities)
	{
		double startJd = jd - 45;
		double scale = society->Scale();
		auto w = World::Make(ephemeris, startJd, seed, polities);
		auto fleet = w->Fleet;

		// Each power is an interstellar polity (U.SPC-T91): a frontier colony's own state, or one that already holds
		// other systems (the Sol Federation at Barnard's Star), which this system's holdings then join.
		auto Make = [&](const cs::Ref<FactionSpec>& f, PolityKind kind) { return w->Polity(f->Name, f->Prefix, f->Color, kind, f->Treasury * scale, f->Marines); };

		auto authority = w->Sol = Make(society->Authority, PolityKind::State);
		authority->IsMilitary = true;
		auto company = w->Belt = Make(society->Company, PolityKind::Company);
		w->Independents = Make(society->Traders, PolityKind::FreeTraders);
		w->Pirates = society->Raiders != nullptr ? Make(society->Raiders, PolityKind::PirateClan) : w->UnknownRaiders();
		// Rivals, not enemies: the hook for a war later.
		w->RivalA = authority;
		w->RivalB = company;
		auto Of = [&](SocietyRole r) { return r == SocietyRole::Authority ? authority : r == SocietyRole::Company ? company : w->Pirates; };

		// ---- settlements
		auto e = w->Economy;
		cs::Ref<Settlement> haven = nullptr;
		for (const auto& spec : society->Settlements)
		{
			auto s = e->Add(spec->Name, (*w->Catalog())[spec->Body]->Index, spec->Kind, Of(spec->Owner), spec->Population * scale);
			s->Hidden = spec->Hidden;
			s->Lawless = spec->Lawless;
			if (spec->Kind == SettlementKind::Haven)
			{
				if (!haven) haven = s;
				continue;
			}
			// A token of each: BalanceSupply sizes them to the system's needs.
			for (const auto& g : spec->Mines) s->Facilities.Add(cs::New<Facility>(Recipe::Mine(g, 1), 1));
			for (const auto& k : spec->Makes) s->Facilities.Add(cs::New<Facility>(Recipe::Of(k), 0.01));
			if (spec->Shipyard > 0) s->Facilities.Add(cs::New<Facility>(Recipe::Of(IndustryKind::Shipyard), spec->Shipyard * scale));
			if (spec->Munitions > 0) s->Facilities.Add(cs::New<Facility>(Recipe::Of(IndustryKind::MunitionsPlant), spec->Munitions * scale));
		}
		auto Mine = [](cs::Ref<Settlement> s, Good g, double perDay) { s->Facilities.Add(cs::New<Facility>(Recipe::Mine(g, perDay), 1)); };
		auto Mk = [](cs::Ref<Settlement> s, IndustryKind k, double level) { s->Facilities.Add(cs::New<Facility>(Recipe::Of(k), level)); };
		for (const auto& s : e->Settlements) Subsist(s, Mine, Mk);
		BalanceSupply(e->Settlements, society->System);
		for (const auto& s : e->Settlements)
		{
			s->MaxPlatforms = s->Kind == SettlementKind::Haven ? 1 : s->Population >= 100e3 ? 2 : 1;
			for (int i = 0; i < s->MaxPlatforms; i++) s->Platforms.Add(cs::New<ShipCombat>(CombatSpec::Platform()));
		}

		// ---- the fleet's numbers, from how many people it serves
		double people = 0;
		for (const auto& s : e->Settlements) people += s->Population;
		auto Count = [&](double perShip, int min, const cs::List<cs::String>& names) { return cs::Math::Min(names.Count(), cs::Math::Max(min, cs::ToInt(cs::Math::Round(people / perShip)))); };
		int freighters = Count(25e3, 3, society->Freighters), haulers = Count(70e3, 1, society->Haulers), couriers = Count(120e3, 1, society->Couriers);
		int merchants = freighters + haulers + couriers;
		w->MerchantTarget = cs::ToInt(cs::Math::Ceiling(merchants * 1.5));
		w->MerchantCap = merchants * 2;
		int navy = cs::Math::Min(society->Navy.Count(), people >= 100e3 ? 3 : 2);
		w->NavyCap = navy + 1;
		w->NavyCapAtWar = navy + 3;
		w->PirateGroupCap = haven != nullptr ? 1 : 0;
		w->PirateIncomePerDay = haven != nullptr ? 4e5 * scale : 0;
		w->HunterCap = haven != nullptr ? 1 : 0;
		w->RaiderNames = society->RaiderNames;

		// ---- lanes, structures and rings
		auto lanes = cs::New<Hyperlanes>(ephemeris);
		for (const auto& [a, b, bend] : society->Lanes) lanes->Add(a, b, bend);
		w->Wire(startJd, seed, lanes);
		auto war = w->War;
		for (const auto& s : e->Settlements)
		{
			// A colony on a world goes up and down through a docking ring over its main town.
			if (s->Kind != SettlementKind::Colony || (*w->Catalog())[s->Body]->Kind == BodyKind::Station) continue;
			w->Infrastructure->AddRing(e, (*w->Catalog())[s->Body]->Name, s->Name + " Ring", s->Name, 0, 0);
		}
		w->Fleet->Rings.AddRange(w->Infrastructure->Rings);

		// ---- ships
		if (navy > 0)
		{
			cs::List<cs::String> patrol;
			for (const auto& s : e->Settlements)
				if (s->Owner == authority || s->Owner == company)
				{
					cs::String body = (*w->Catalog())[s->Body]->Name;
					if (!patrol.Contains(body)) patrol.Add(body);
				}
			cs::List<std::pair<ShipClass, cs::String>> escorts;
			for (int i = 1; i < navy; i++) escorts.Add({ i == 1 && people >= 100e3 ? ShipClass::Destroyer : ShipClass::Fighter, society->Navy[i] });
			fleet->Group(authority, startJd, people >= 100e3 ? ShipClass::Cruiser : ShipClass::Destroyer, society->Navy[0], patrol.ToArray(), escorts.ToArray());
		}
		auto Take = [](const cs::List<cs::String>& names, int n)
		{
			cs::List<cs::String> r;
			for (int i = 0; i < n; i++) r.Add(names[i]); // Array.Copy(names, r, n)
			return r;
		};
		w->BuildMerchants(startJd, Take(society->Freighters, freighters), Take(society->Haulers, haulers), Take(society->Couriers, couriers),
			cs::List<cs::Ref<Faction>>{ w->Independents, company, w->Independents, authority }, [company](int i) { (void)i; return company; });
		// BuildMerchants names later hulls from these lists: keep the whole of each.
		w->merchantNames = society->Freighters;
		w->minerNames = society->Haulers;

		w->Companies->MaxCompanies = cs::Math::Max(society->Companies.Count(), 2) + 1;
		cs::List<std::tuple<cs::String, cs::String, cs::String, cs::Ref<Faction>, bool>> table;
		for (const auto& [name, tag, home, flag, mining] : society->Companies) table.Add({ name, tag, home, Of(flag), mining });
		w->FoundCompanies(startJd, table.ToArray());
		w->AddFreeShips(startJd, society->Yachts, society->Prospectors, society->Explorers);
		if (haven != nullptr) war->RaisePirateGroup(startJd, haven->Body);
		for (const auto& site : society->HavenSites) w->HavenSites.Add((*w->Catalog())[site]->Index);
		// A system with a navy of its own flies its patrol wings too (U.SPC-T371).
		if (navy > 0) PatrolWings::Raise(w, startJd);
		// And its ports' ordinary traffic: buses, taxis and people's own craft (U.SPC-T375).
		LocalTraffic::Raise(w, startJd);
		w->SeatCapitals();
		w->MusterReserves();

		w->Update(jd);
		return w;
	}

	/// <summary>
	/// Sizes a system's mines and factories so it makes <see cref="SupplyMargin"/> times what it uses of every good
	/// that is used: each good short is made up by raising every facility that makes it in proportion, again and
	/// again, since a bigger smelter wants more ore and a bigger mine more machinery; each counted at the share of
	/// its capacity it can expect to run at (<see cref="ExpectedRunning"/>). Havens fend for themselves
	/// and are left out. A good used but made nowhere is a mistake in the society's plan, and throws.
	/// </summary>
	void World::BalanceSupply(const cs::List<cs::Ref<Settlement>>& settlements, const cs::String& system)
	{
		auto supply = cs::NewArray<double>(GoodInfo::Count);
		auto demand = cs::NewArray<double>(GoodInfo::Count);
		for (int pass = 0; pass < 60; pass++)
		{
			for (int i = 0; i < supply.Count(); i++) supply[i] = 0;
			for (int i = 0; i < demand.Count(); i++) demand[i] = 0;
			for (const auto& s : settlements)
			{
				if (s->Kind == SettlementKind::Haven) continue;
				for (const auto& [g, perPerson] : s->PersonNeeds()) demand[static_cast<int>(g)] += perPerson * s->Population;
				for (const auto& f : s->Facilities)
				{
					double running = ExpectedRunning(s, f);
					for (const auto& [g, rate] : f->Recipe->Inputs) demand[static_cast<int>(g)] += rate * f->Level;
					for (const auto& [g, rate] : f->Recipe->Outputs) supply[static_cast<int>(g)] += rate * f->Level * running;
				}
			}
			bool changed = false;
			for (int g = 0; g < GoodInfo::Count; g++)
			{
				if (demand[g] <= 0 || supply[g] >= demand[g] * SupplyMargin * 0.999) continue;
				if (supply[g] <= 0) throw cs::InvalidOperationException(cs::Cat(system, " uses ", GoodInfo::Of(static_cast<Good>(g))->Name, " but makes none"));
				double factor = demand[g] * SupplyMargin / supply[g];
				for (const auto& s : settlements)
				{
					if (s->Kind == SettlementKind::Haven) continue;
					for (const auto& f : s->Facilities)
						for (const auto& [o, unused] : f->Recipe->Outputs)
							if (static_cast<int>(o) == g)
								f->Level = f->BaseLevel = f->Level * factor;
				}
				changed = true;
			}
			if (!changed) return;
		}
	}

	/// <summary>
	/// The share of capacity a facility can count on: all of it if every consumable it burns (a mine's machinery
	/// and helium-3, a lab's rare earths) is made where it stands, else <see cref="Recipe.MinorFloor"/>, since in a
	/// thin system those come in dribs and drabs. Life support runs on regardless.
	/// </summary>
	double World::ExpectedRunning(const cs::Ref<Settlement>& s, const cs::Ref<Facility>& f)
	{
		if (f->LifeSupport()) return 1;
		const auto& inputs = f->Recipe->Inputs;
		for (int k = 0; k < inputs.Count(); k++)
		{
			if (f->Recipe->Limiting[k]) continue;
			Good inputGood = std::get<0>(inputs[k]);
			bool local = false;
			for (const auto& other : s->Facilities)
				for (const auto& [o, unused] : other->Recipe->Outputs)
					local |= o == inputGood;
			if (!local) return Recipe::MinorFloor;
		}
		return 1;
	}
}
