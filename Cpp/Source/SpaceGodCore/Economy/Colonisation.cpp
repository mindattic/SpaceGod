// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Colonisation.cs (US-T19).

#include "Economy/Colonisation.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "OrbitalElements.h"
#include "Ships.h"
#include "Combat/Diplomacy.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/People.h"
#include "Economy/Settlement.h"
#include "Systems/StarSystemSpec.h"

namespace SpaceGod::Core
{
	const char* ToString(ColonyStage v)
	{
		switch (v)
		{
		case ColonyStage::Established: return "Established";
		case ColonyStage::Outpost: return "Outpost";
		case ColonyStage::Colony: return "Colony";
		case ColonyStage::Ruin: return "Ruin";
		}
		return "";
	}

	const char* ToString(ColonyLanding v)
	{
		switch (v)
		{
		case ColonyLanding::None: return "None";
		case ColonyLanding::Founded: return "Founded";
		case ColonyLanding::Reinforced: return "Reinforced";
		case ColonyLanding::TurnedBack: return "TurnedBack";
		case ColonyLanding::Returned: return "Returned";
		}
		return "";
	}

	// ---------------------------------------------------------------- ColonySite

	bool ColonySite::Has(Good g) const { return Resources.IndexOf(g) >= 0; }

	bool ColonySite::Viable() const { return Zone != HabitableZone::None || Has(Good::Water); }

	double ColonySite::Score() const
	{
		if (!Viable() || Capacity <= 0) return 0;
		double zone = Zone == HabitableZone::Conservative ? 3 : Zone == HabitableZone::Optimistic ? 2 : 1;
		double worth = 0;
		for (Good g : Resources) worth += cs::Math::Log10(10 + GoodInfo::Of(g)->BasePrice);
		return zone * (1 + 0.25 * worth) * cs::Math::Log10(Capacity);
	}

	cs::Ref<ColonySite> ColonySite::Survey(const cs::Ref<BodyCatalog>& catalog, int index)
	{
		auto b = (*catalog)[index];
		if (b->Kind == BodyKind::Star || b->Kind == BodyKind::Barycentre || b->GasGiant) return nullptr;
		if (b->Kind == BodyKind::Station && !b->HabitatSite) return nullptr;
		// Climb to the body whose parent is a star: its orbit sets the sunlight.
		auto orbiter = b;
		cs::Ref<BodyDefinition> star = nullptr;
		while (orbiter->ParentIndex >= 0)
		{
			auto parent = (*catalog)[orbiter->ParentIndex];
			if (parent->Kind == BodyKind::Star)
			{
				star = parent;
				break;
			}
			orbiter = parent;
		}
		if (star == nullptr || orbiter->Elements == nullptr) return nullptr;
		double aAu = orbiter->Elements->AKm / Astro::AuKm, e = cs::Math::Min(0.95, orbiter->Elements->E);
		if (aAu <= 0) return nullptr;
		auto site = cs::New<ColonySite>();
		site->Body = index;
		site->Name = b->Name;
		site->Zone = HabitableZones::Classify(star->LuminositySolar, star->TeffK, aAu, e);
		site->Flux = HabitableZones::MeanFlux(star->LuminositySolar, aAu, e);
		if (b->HabitatSite)
		{
			// An orbit marked out for a habitat (U.SPC-T293): nothing to dig, but water and nitrogen from captured comets,
			// as the frontier's ice stations have them (assumed); room for more people the more sunlight there is to farm by.
			site->Habitat = true;
			site->Resources = cs::List<Good>{ Good::Water, Good::Nitrogen };
			site->Capacity = site->Zone == HabitableZone::Conservative ? 3e5 : site->Zone == HabitableZone::Optimistic ? 2e5 : 1e5;
			return site;
		}
		bool small = b->Kind == BodyKind::Asteroid || b->RadiusKm < 200;
		double inner = HabitableZones::RunawayFlux(star->TeffK), outer = HabitableZones::MaxGreenhouseFlux(star->TeffK);
		auto list = cs::List<Good>();
		if (site->Zone != HabitableZone::None)
		{
			list.Add(Good::Water);
			if (!small)
			{
				list.Add(Good::Nitrogen);
				list.Add(Good::Titanium);
			}
		}
		else if (site->Flux > inner)
		{
			list.Add(Good::NickelIron);
			list.Add(Good::Titanium);
			list.Add(Good::Copper);
			if (small) list.Add(Good::Platinum);
		}
		else if (site->Flux > outer * 0.25 && small)
		{
			// The warm inner belt: dry metal rock.
			list.Add(Good::NickelIron);
			list.Add(Good::Platinum);
			list.Add(Good::RareEarths);
		}
		else
		{
			// Cold: ices. Only a world big enough to have held on to them has nitrogen and hydrocarbons too.
			list.Add(Good::Water);
			if (!small)
			{
				list.Add(Good::Nitrogen);
				list.Add(Good::Hydrocarbons);
			}
		}
		site->Resources = list.ToArray();
		site->Capacity = site->Zone == HabitableZone::Conservative ? 2e6
			: site->Zone == HabitableZone::Optimistic ? 1e6
			: b->RadiusKm >= 1000 ? 4e5
			: b->RadiusKm >= 200 ? 1.5e5
			: 5e4;
		return site;
	}

	// ---------------------------------------------------------------- ColonyShip

	ColonyShip::ColonyShip() : Cargo(cs::NewArray<double>(GoodInfo::Count)) {}

	int ColonyShip::Body() const
	{
		return Returning && From != nullptr ? From->Body : (Reinforcing ? Reinforcing->Body : Site->Body);
	}

	// ---------------------------------------------------------------- Colonisation

	const cs::List<cs::String>& Colonisation::ShipNames()
	{
		static const cs::List<cs::String> names{ "Pathfinder", "Homestead", "Pioneer", "Wayfarer", "Providence", "Arcadia", "Endeavour", "Threshold", "Hearth", "New Dawn" };
		return names;
	}

	void Colonisation::SurveyAll(const cs::Ref<BodyCatalog>& catalog)
	{
		Sites.Clear();
		for (int i = 0; i < catalog->Count(); i++)
		{
			auto site = ColonySite::Survey(catalog, i);
			if (site != nullptr) Sites.Add(site);
		}
	}

	void Colonisation::Review(const cs::Ref<Economy>& economy, double jd)
	{
		Land(economy, jd);
		Tend(economy, jd);
		Launch(economy, jd);
	}

	// ---------------------------------------------------------------- pressure

	double Colonisation::Pressure(const cs::Ref<Economy>& economy, const cs::Ref<Faction>& f)
	{
		double sum = 0;
		for (const auto& s : economy->Settlements)
			if (s->Owner == f && Lives(s) && s->Kind != SettlementKind::Haven) sum += s->Overflow;
		return sum;
	}

	bool Colonisation::Lives(const cs::Ref<Settlement>& s) { return !s->Fallen && s->Stage != ColonyStage::Ruin; }

	bool Colonisation::IsFounded(const cs::Ref<Settlement>& s) { return s->Stage == ColonyStage::Outpost || s->Stage == ColonyStage::Colony; }

	bool Colonisation::Occupied(const cs::Ref<Economy>& economy, int body, double jd)
	{
		for (const auto& s : economy->Settlements)
		{
			if (s->Body != body) continue;
			if (Lives(s)) return true;
			if (s->Stage == ColonyStage::Ruin && jd - s->RuinedJd < RuinYears * 365.25) return true;
		}
		return false;
	}

	bool Colonisation::Bound(int body) const
	{
		for (const auto& v : Voyages)
			if (!v->Abroad() && v->Body() == body) return true;
		for (const auto& v : Incoming)
			if (v->Body() == body) return true;
		return false;
	}

	bool Colonisation::Sailing(const cs::Ref<Faction>& f) const
	{
		for (const auto& v : Voyages)
			if (v->Owner == f) return true;
		return false;
	}

	double Colonisation::Days(const cs::Ref<Settlement>& from, int body, double jd) const
	{
		if (from->Body == body) return 0;
		double d = TravelDays ? TravelDays(from, body, jd) : DefaultTravelDays;
		return cs::Double::IsNaN(d) || d < 0 ? DefaultTravelDays : d;
	}

	// ---------------------------------------------------------------- launching

	void Colonisation::Launch(const cs::Ref<Economy>& economy, double jd)
	{
		auto factions = cs::List<cs::Ref<Faction>>();
		for (const auto& s : economy->Settlements)
			if (s->Owner != nullptr && !s->Owner->IsPirate && s->Kind != SettlementKind::Haven && Lives(s) && !factions.Contains(s->Owner)) factions.Add(s->Owner);
		for (const auto& f : factions) TryLaunch(economy, f, jd);
	}

	cs::Ref<ColonyShip> Colonisation::TryLaunch(const cs::Ref<Economy>& economy, const cs::Ref<Faction>& f, double jd)
	{
		if (f == nullptr || f->IsPirate || Sailing(f)) return nullptr;
		double pool = Pressure(economy, f);
		if (pool < PartySize) return nullptr;

		// The colonists gather at the most crowded port.
		cs::Ref<Settlement> from = nullptr;
		for (const auto& s : economy->Settlements)
			if (s->Owner == f && Lives(s) && s->Kind != SettlementKind::Haven && (from == nullptr || s->Overflow > from->Overflow)) from = s;
		if (from == nullptr) return nullptr;

		// A young settlement of its own with room and doing well takes them first ...
		cs::Ref<Settlement> reinforce = nullptr;
		double bestDays = cs::Double::MaxValue;
		for (const auto& s : economy->Settlements)
		{
			if (s->Owner != f || s == from || !Lives(s) || !IsFounded(s) || s->Wellbeing < ReinforceWellbeing || Bound(s->Body)) continue;
			double room = s->InitialPopulation * People::MaxGrowth - s->Population;
			if (room < PartySize * 0.5) continue;
			double d = Days(from, s->Body, jd);
			if (d < bestDays)
			{
				bestDays = d;
				reinforce = s;
			}
		}

		cs::Ref<ColonySite> site = nullptr;
		cs::Ref<ForeignSite> abroad = nullptr;
		double cost;
		if (reinforce != nullptr) cost = ReinforceCost;
		else
		{
			// ... or else the best free site in reach: in this system, or through the gates in one newly opened to settlement.
			double best = 0;
			for (const auto& c : Sites)
			{
				double score = c->Score();
				if (score <= 0 || Occupied(economy, c->Body, jd) || Bound(c->Body)) continue;
				double d = Days(from, c->Body, jd);
				double value = score / (1 + d / 200);
				if (value > best)
				{
					best = value;
					site = c;
					bestDays = d;
				}
			}
			if (Abroad)
				for (const auto& x : Abroad(f, from, jd))
				{
					double score = x->Site->Score();
					if (score <= 0 || cs::Double::IsNaN(x->Days) || x->Days < 0) continue;
					double value = score * NewSystemWorth / (1 + x->Days / 200);
					if (value > best)
					{
						best = value;
						site = x->Site;
						abroad = x;
						bestDays = x->Days;
					}
				}
			if (site == nullptr) return nullptr;
			cost = FoundingCost;
		}
		if (f->Treasury < cost) return nullptr;

		const auto& shipNames = ShipNames();
		auto ship = cs::New<ColonyShip>();
		ship->Name = cs::Cat(cs::Strings::IsNullOrEmpty(f->Prefix) ? cs::String("") : cs::Cat(f->Prefix, " "), shipNames[launched % shipNames.Count()], launched >= shipNames.Count() ? cs::Cat(" ", launched / shipNames.Count() + 1) : cs::String(""));
		ship->Owner = f;
		ship->From = from;
		ship->Site = site ? site : SiteAt(reinforce->Body);
		ship->Reinforcing = reinforce;
		ship->DepartJd = jd;
		ship->ArriveJd = jd + bestDays;
		ship->Into = abroad ? abroad->Into : nullptr;
		ship->IntoEconomy = abroad ? abroad->Economy : nullptr;
		ship->Where = abroad ? abroad->Where : cs::String();

		// The colonists come from every crowded place in turn, most crowded first, none below its floor.
		double party = cs::Math::Min(PartySize, pool), taken = 0;
		auto crowded = cs::List<cs::Ref<Settlement>>();
		for (const auto& s : economy->Settlements)
			if (s->Owner == f && Lives(s) && s->Kind != SettlementKind::Haven && s->Overflow > 0) crowded.Add(s);
		crowded.Sort([](const cs::Ref<Settlement>& a, const cs::Ref<Settlement>& b) { return cs::Compare(b->Overflow, a->Overflow); });
		for (const auto& s : crowded)
		{
			double take = cs::Math::Min(cs::Math::Min(s->Overflow, party - taken), cs::Math::Max(0.0, s->Population - s->InitialPopulation * People::MinShare));
			if (take <= 0) continue;
			s->Population -= take;
			s->Overflow -= take;
			taken += take;
			if (taken >= party) break;
		}
		if (taken <= 0) return nullptr;
		ship->Colonists = taken;
		launched++;

		// Starter goods: what they eat, drink and breathe for the first months, medicine and spares for a year,
		// bought from the port's market (what it can spare) by the faction.
		auto kind = SettlementKind::MiningOutpost;
		double spent = cost;
		for (const auto& [g, perDay] : People::NeedsOf(kind))
		{
			double days = GoodInfo::Of(g)->Category == GoodCategory::Essential && g != Good::MedicalSupplies && g != Good::Nitrogen ? StarterDays : MedicineDays;
			if (g == Good::Nitrogen) days = StarterDays;
			double want = perDay * taken * days;
			double tonnes = cs::Math::Min(want, from->Available(g));
			if (tonnes <= 0) continue;
			from->Stock[static_cast<int>(g)] -= tonnes;
			ship->Cargo[static_cast<int>(g)] += tonnes;
			spent += tonnes * from->Price[static_cast<int>(g)];
		}
		f->Treasury -= spent;
		ship->Of = shared_from_this();
		Voyages.Add(ship);
		if (ship->Into) ship->Into->Incoming.Add(ship);
		// Its own hull (U.SPC-T524): a freighter fitted out at the port, which flies the voyage itself (through the gates
		// for a site abroad). Colonists for a settlement on the very body they sail from have no voyage to fly.
		if (Embark && (ship->Abroad() || from->Body != ship->Body()))
		{
			ship->Hull = Embark(ship, jd);
			if (ship->Hull != nullptr && ship->Abroad() && (!SailAbroad || !SailAbroad(ship, jd)))
			{
				// No way through the gates for it after all: the hull goes back to work, and the voyage is reckoned as before.
				auto hull = ship->Hull;
				ship->Hull = nullptr;
				hull->Colony = nullptr;
				if (Refit) Refit(hull);
			}
		}
		if (Launched) Launched(ship);
		return ship;
	}

	cs::Ref<Colonisation> Colonisation::Bearing(const cs::Ref<ColonyShip>& v) { return v->Returning || !v->Abroad() ? v->Of : v->Into; }

	cs::Nullable<int> Colonisation::HullDestination(const cs::Ref<Ship>& hull, const cs::Ref<Colonisation>& here)
	{
		auto v = hull ? hull->Colony : nullptr;
		if ((v ? v->Of : nullptr) == nullptr || !v->Of->Voyages.Contains(v)) return std::nullopt;
		return Bearing(v) == here ? v->Body() : -1;
	}

	ColonyLanding Colonisation::HullArrived(const cs::Ref<Ship>& hull, const cs::Ref<Colonisation>& here, const cs::Ref<Economy>& economy, double jd)
	{
		auto v = hull ? hull->Colony : nullptr;
		if (v == nullptr || v->Of.get() != this || !Voyages.Contains(v) || Bearing(v) != here || hull->Orbit == nullptr || hull->Orbit->Body != v->Body())
			return ColonyLanding::None;
		Voyages.Remove(v);
		if (v->Returning)
		{
			if (v->From != nullptr && Lives(v->From))
			{
				v->From->Population += v->Colonists;
				v->From->Overflow += v->Colonists;
				Unload(v, v->From);
			}
			hull->Colony = nullptr;
			if (Returned) Returned(v);
			return ColonyLanding::Returned;
		}
		Arrive(economy, v, jd);
		if (v->Returning) return ColonyLanding::TurnedBack;
		hull->Colony = nullptr;
		return v->Reinforcing != nullptr ? ColonyLanding::Reinforced : ColonyLanding::Founded;
	}

	void Colonisation::TurnBack(const cs::Ref<ColonyShip>& v)
	{
		if (v == nullptr || v->Returning) return;
		v->Returning = true;
		if (v->Into) v->Into->Incoming.Remove(v);
	}

	bool Colonisation::HullLost(const cs::Ref<ColonyShip>& v, double jd)
	{
		auto h = v->Hull;
		cs::Ref<Faction> by = nullptr;
		if (h->Destroyed && !h->Scrapped) { }
		else if (h->Faction != v->Owner) by = h->Faction;
		else if (jd <= v->ArriveJd + OverdueDays) return false;
		Voyages.Remove(v);
		if (v->Into) v->Into->Incoming.Remove(v);
		if (h->Colony == v) h->Colony = nullptr;
		for (int i = 0; i < v->Cargo.Count(); i++) v->Cargo[i] = 0; // Array.Clear
		if (!h->Destroyed)
		{
			// Taken: its captors do with it as they will. Given up: whatever is left of it goes back to work where it is.
			if (by != nullptr) { if (h->Role == ShipRole::Colonist) h->Role = ShipRole::Free; }
			else if (Refit) Refit(h);
		}
		if (Lost) Lost(v, by);
		return true;
	}

	cs::Ref<ColonySite> Colonisation::SiteAt(int body) const
	{
		for (const auto& s : Sites)
			if (s->Body == body) return s;
		auto site = cs::New<ColonySite>();
		site->Body = body;
		site->Name = cs::Cat("body ", body);
		site->Capacity = 0;
		return site;
	}

	// ---------------------------------------------------------------- landing

	void Colonisation::Land(const cs::Ref<Economy>& economy, double jd)
	{
		for (int i = 0; i < Voyages.Count(); i++)
		{
			auto v = Voyages[i];
			// A real hull lands its colonists when it gets there (HullArrived), not by the timetable (U.SPC-T524).
			if (v->Hull != nullptr)
			{
				if (HullLost(v, jd)) i--;
				continue;
			}
			if (jd < v->ArriveJd) continue;
			Voyages.RemoveAt(i--);
			Arrive(economy, v, jd);
		}
	}

	cs::Ref<Settlement> Colonisation::Arrive(const cs::Ref<Economy>& economy, const cs::Ref<ColonyShip>& v, double jd)
	{
		auto target = v->Reinforcing;
		if (target != nullptr)
		{
			if (Lives(target) && target->Owner == v->Owner)
			{
				double room = cs::Math::Max(0.0, target->InitialPopulation * People::MaxGrowth - target->Population);
				double settle = cs::Math::Min(room, v->Colonists);
				target->Population += settle;
				target->Overflow += v->Colonists - settle;
				Unload(v, target);
				Provision(target);
				if (Landed) Landed(v, target);
				return target;
			}
		}
		else
		{
			// A ship from another system founds in that system's markets, and its news is that system's.
			auto there = v->IntoEconomy ? v->IntoEconomy : economy;
			auto into = v->Into ? v->Into : shared_from_this();
			if (v->Into) v->Into->Incoming.Remove(v);
			if (!Occupied(there, v->Site->Body, jd))
			{
				auto s = Found(there, v->Owner, v->Site, v->Colonists, jd);
				Unload(v, s);
				there->Open(s);
				if (into->Founded) into->Founded(s);
				return s;
			}
		}
		// The site was taken or the settlement lost while they were on their way: they go home. A real hull (U.SPC-T524)
		// carries them back itself, and they rejoin their port when it gets there (HullArrived).
		if (v->Hull != nullptr && !v->Hull->Destroyed && !v->Returning && v->From != nullptr && Lives(v->From))
		{
			v->Returning = true;
			if (v->Into) v->Into->Incoming.Remove(v);
			if (!Voyages.Contains(v)) Voyages.Add(v);
			if (Landed) Landed(v, nullptr);
			return nullptr;
		}
		if (v->From != nullptr && Lives(v->From))
		{
			v->From->Population += v->Colonists;
			v->From->Overflow += v->Colonists;
			Unload(v, v->From);
		}
		if (Landed) Landed(v, nullptr);
		return nullptr;
	}

	void Colonisation::Unload(const cs::Ref<ColonyShip>& v, const cs::Ref<Settlement>& s)
	{
		for (int i = 0; i < GoodInfo::Count; i++)
		{
			s->Stock[i] += v->Cargo[i];
			v->Cargo[i] = 0;
		}
	}

	cs::Ref<Settlement> Colonisation::Found(const cs::Ref<Economy>& economy, const cs::Ref<Faction>& owner, const cs::Ref<ColonySite>& site, double people, double jd)
	{
		int ruins = 0;
		for (const auto& x : economy->Settlements)
			if (x->Body == site->Body) ruins++;
		// A habitat is called after its site ("Ross 154 Habitat"); a world's first settlement is its landing;
		// each later re-founding is numbered by how many have stood here before it, so the second settlement
		// on a site is "New X 2", not an unnumbered "New X" that then jumps straight to "New X 3" (U.SPC-T320).
		cs::String name = ruins == 0 ? (site->Habitat ? site->Name : cs::Cat(site->Name, " Landing")) : cs::Cat("New ", site->Name, " ", ruins + 1);
		auto s = economy->Add(name, site->Body, SettlementKind::MiningOutpost, owner, people);
		s->Stage = ColonyStage::Outpost;
		s->FoundedJd = jd;
		s->Site = site;
		s->DaysSinceCapture = 9999;
		Provision(s);
		// A first mine on the site's most valuable ore, sized to a quarter of the colonists' industrial hands.
		cs::Nullable<Good> ore = std::nullopt;
		for (Good g : site->Resources)
			if (g != Good::Water && g != Good::Nitrogen && (!ore.has_value() || GoodInfo::Of(g)->BasePrice > GoodInfo::Of(*ore)->BasePrice)) ore = g;
		if (ore.has_value()) AddMine(s, *ore, People::Labour(s) * FirstMineShare / Recipe::MineWorkersPerTonneDay(*ore));
		return s;
	}

	void Colonisation::AddMine(const cs::Ref<Settlement>& s, Good ore, double perDay)
	{
		if (perDay <= 0) return;
		s->Facilities.Add(cs::New<Facility>(Recipe::Mine(ore, perDay), 1));
	}

	void Colonisation::Provision(const cs::Ref<Settlement>& s)
	{
		auto site = s->Site;
		auto farmRecipe = Recipe::Of(IndustryKind::Farm);
		cs::Ref<Facility> farm = nullptr, water = nullptr, nitrogen = nullptr;
		double food = 0, waterNet = 0, nitrogenNet = 0;
		for (const auto& f : s->Facilities)
		{
			if (f->Recipe->Kind == IndustryKind::Farm) farm = f;
			if (f->Recipe->Kind == IndustryKind::Mine && std::get<0>(f->Recipe->Outputs[0]) == Good::Water) water = f;
			if (f->Recipe->Kind == IndustryKind::Mine && std::get<0>(f->Recipe->Outputs[0]) == Good::Nitrogen) nitrogen = f;
			for (const auto& [g, rate] : f->Recipe->Outputs)
			{
				if (g == Good::Food) food += rate * f->BaseLevel;
				if (g == Good::Water) waterNet += rate * f->BaseLevel;
				if (g == Good::Nitrogen) nitrogenNet += rate * f->BaseLevel;
			}
			for (const auto& [g, rate] : f->Recipe->Inputs)
			{
				if (g == Good::Water) waterNet -= rate * f->BaseLevel;
				if (g == Good::Nitrogen) nitrogenNet -= rate * f->BaseLevel;
			}
		}
		double foodNeed = s->Population * People::NeedOf(s->Kind, Good::Food) * SelfSufficiency;
		if (food < foodNeed)
		{
			double add = (foodNeed - food) / std::get<1>(farmRecipe->Outputs[0]);
			if (farm == nullptr) s->Facilities.Add(farm = cs::New<Facility>(farmRecipe, add));
			else Grow(farm, add);
			waterNet -= std::get<1>(farmRecipe->Inputs[0]) * add;
			nitrogenNet -= std::get<1>(farmRecipe->Inputs[1]) * add;
		}
		double waterShort = s->Population * People::NeedOf(s->Kind, Good::Water) * SelfSufficiency - waterNet;
		if (site != nullptr && site->Has(Good::Water) && waterShort > 0) MineMore(s, water, Good::Water, waterShort * 1.05);
		double nitrogenShort = s->Population * People::NeedOf(s->Kind, Good::Nitrogen) * SelfSufficiency - nitrogenNet;
		if (site != nullptr && site->Has(Good::Nitrogen) && nitrogenShort > 0) MineMore(s, nitrogen, Good::Nitrogen, nitrogenShort);
	}

	void Colonisation::Grow(const cs::Ref<Facility>& f, double addLevel)
	{
		double k = (f->BaseLevel + addLevel) / f->BaseLevel;
		f->BaseLevel *= k;
		f->Level *= k;
	}

	void Colonisation::MineMore(const cs::Ref<Settlement>& s, cs::Ref<Facility>& mine, Good ore, double perDay)
	{
		if (mine == nullptr)
		{
			s->Facilities.Add(mine = cs::New<Facility>(Recipe::Mine(ore, perDay), 1));
			return;
		}
		double unit = std::get<1>(mine->Recipe->Outputs[0]);
		Grow(mine, perDay / unit);
	}

	// ---------------------------------------------------------------- growth and failure

	void Colonisation::Tend(const cs::Ref<Economy>& economy, double jd)
	{
		// Settlements are added as ships land: walk a copy.
		for (const auto& s : economy->Settlements.ToArray())
		{
			if (!IsFounded(s) || s->Fallen) continue;
			if (Failing(s)) Abandon(economy, s, jd);
			else
			{
				// The settlers build farms and dig ice as their numbers grow.
				Provision(s);
				TryExpand(s);
				TryPromote(s);
			}
		}
	}

	bool Colonisation::Failing(const cs::Ref<Settlement>& s)
	{
		return IsFounded(s) && !s->Fallen && s->Population <= s->InitialPopulation * People::MinShare * 1.02 && s->Wellbeing < FailWellbeing;
	}

	bool Colonisation::TryExpand(const cs::Ref<Settlement>& s)
	{
		if (!IsFounded(s) || s->Fallen || s->Owner == nullptr) return false;
		double cap = s->InitialPopulation * People::MaxGrowth;
		if (s->Population < cap * ExpandAt || s->Wellbeing < ExpandWellbeing) return false;
		double siteCap = s->Site != nullptr && s->Site->Capacity > 0 ? s->Site->Capacity : cap;
		double newBase = cs::Math::Min(s->InitialPopulation * ExpandFactor, siteCap / People::MaxGrowth);
		if (newBase <= s->InitialPopulation * 1.01) return false;
		double cost = (newBase - s->InitialPopulation) * ExpansionCostPerPerson;
		if (s->Owner->Treasury < cost) return false;
		s->Owner->Treasury -= cost;
		double old = s->InitialPopulation;
		s->InitialPopulation = newBase;
		// The works grow with the habitat, and a mine is opened on the next ore the site has (life support
		// follows the people as they come, see Provision).
		double grow = newBase / old - 1;
		for (const auto& f : s->Facilities)
			if (!f->LifeSupport()) Grow(f, f->BaseLevel * grow);
		if (s->Site != nullptr)
			for (Good g : s->Site->Resources)
			{
				if (g == Good::Water || g == Good::Nitrogen || HasMine(s, g)) continue;
				AddMine(s, g, People::Labour(s) * NextMineShare / Recipe::MineWorkersPerTonneDay(g));
				break;
			}
		if (Expanded) Expanded(s);
		return true;
	}

	bool Colonisation::HasMine(const cs::Ref<Settlement>& s, Good g)
	{
		for (const auto& f : s->Facilities)
			if (f->Recipe->Kind == IndustryKind::Mine && std::get<0>(f->Recipe->Outputs[0]) == g) return true;
		return false;
	}

	bool Colonisation::TryPromote(const cs::Ref<Settlement>& s)
	{
		if (s->Stage != ColonyStage::Outpost || s->Fallen) return false;
		if (s->Population < ColonyPopulation || s->Jobs() < ColonyJobs) return false;
		s->Stage = ColonyStage::Colony;
		s->Kind = SettlementKind::Colony;
		// A colony's people use more water than an outpost's (showers and laundry): its ice mines grow to match.
		Provision(s);
		if (Promoted) Promoted(s);
		return true;
	}

	void Colonisation::Abandon(const cs::Ref<Economy>& economy, const cs::Ref<Settlement>& s, double jd)
	{
		cs::Ref<Settlement> refuge = nullptr;
		for (const auto& x : economy->Settlements)
		{
			if (x == s || !Lives(x) || x->Kind == SettlementKind::Haven) continue;
			bool own = x->Owner == s->Owner;
			bool ok = own || economy->Diplomacy == nullptr || !economy->Diplomacy->Hostile(x->Owner, s->Owner);
			if (!ok) continue;
			if (refuge == nullptr || (own && refuge->Owner != s->Owner) || (own == (refuge->Owner == s->Owner) && x->Population > refuge->Population)) refuge = x;
		}
		if (refuge == nullptr)
		{
			auto capital = s->Owner ? s->Owner->Capital : nullptr;
			if (capital != nullptr && capital != s && Lives(capital)) refuge = capital;
		}
		if (refuge != nullptr) refuge->Population += s->Population;
		s->Population = 0;
		s->Overflow = 0;
		s->Stage = ColonyStage::Ruin;
		s->Fallen = true;
		s->RebuildJd = cs::Double::PositiveInfinity;
		s->RuinedJd = jd;
		s->Facilities.Clear();
		s->Platforms.Clear();
		s->MaxPlatforms = 0;
		// Array.Clear
		for (int i = 0; i < s->Stock.Count(); i++) s->Stock[i] = 0;
		for (int i = 0; i < s->Demand.Count(); i++) s->Demand[i] = 0;
		for (int i = 0; i < s->Supply.Count(); i++) s->Supply[i] = 0;
		for (int i = 0; i < s->Inbound.Count(); i++) s->Inbound[i] = 0;
		if (Ruined) Ruined(s);
	}
}
