#include "Combat/Corvettes.h"
#include "Combat/PatrolWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Systems/Polities.h"
#include "Settings/GameSetting.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Hyperlanes.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	const cs::Ref<RangeSetting>& Corvettes::PerNavy()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("corvettes.perNavy", SettingGroups::Ai,
			"Corvettes per navy", 3, 0, 8, 1,
			cs::Cat("How many corvettes (system-defence patrol hulls with no FTL drive) a navy stations for good in each system it holds ",
				"ports in, one at each of its ports, the most populous first. They patrol the hyper lanes from there and run down ",
				"smugglers and pirates. 0 keeps none."),
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0"); }), "corvette patrol police law smuggler pirate system defence navy");
		return setting;
	}

	const cs::List<cs::String>& Corvettes::Names()
	{
		static const cs::List<cs::String> names =
		{
			"Constable", "Marshal", "Warden", "Bailiff", "Reeve", "Provost", "Exciseman", "Tidewaiter", "Searcher", "Gauger",
			"Sheriff", "Justiciar", "Assessor", "Inspector", "Watchman", "Beadle",
		};
		return names;
	}

	bool Corvettes::Keeps(const cs::Ref<Faction>& f) { return f != nullptr && f->IsMilitary && !f->IsPirate; }

	int Corvettes::Wanted(int ports) { return cs::Math::Max(0, cs::Math::Min(ports, cs::ToInt(cs::Math::Round(PerNavy()->Value)))); }

	bool Corvettes::Stationed(const cs::Ref<Ship>& s) { return s != nullptr && s->CorvetteStation >= 0; }

	cs::List<cs::Ref<Ship>> Corvettes::Of(const cs::Ref<World>& w, const cs::Ref<Faction>& f)
	{
		cs::List<cs::Ref<Ship>> list;
		for (const auto& s : w->Fleet->Ships())
			if (Stationed(s) && s->Faction == f && !s->Destroyed && !s->Derelict && w->Fleet->IsPresent(s)) list.Add(s);
		return list;
	}

	cs::List<int> Corvettes::Beat(const cs::Ref<World>& w, const cs::Ref<Faction>& f, int station)
	{
		cs::List<int> beat = { station };
		auto lanes = w->Fleet->Lanes;
		if (lanes == nullptr) return beat.ToArray();
		auto ports = PatrolWings::Ports(w, f);
		int home = lanes->SystemOf(station);
		int nearPorts = 0; // C#: near (a Windows macro name, so renamed)
		for (int p : ports)
		{
			if (nearPorts >= HomePorts) break;
			if (!beat.Contains(p) && lanes->SystemOf(p) == home)
			{
				beat.Add(p);
				nearPorts++;
			}
		}
		int runs = 0;
		for (const auto& lane : lanes->Lanes)
		{
			if (runs >= LanePorts) break;
			if (lane->A != home && lane->B != home) continue;
			int farSystem = lane->Other(home); // C#: far (a Windows macro name, so renamed)
			for (int p : ports)
				if (!beat.Contains(p) && lanes->SystemOf(p) == farSystem)
				{
					beat.Add(p);
					runs++;
					break;
				}
		}
		return beat.ToArray();
	}

	bool Corvettes::Polices(const cs::Ref<World>& w, const cs::Ref<Ship>& corvette, int body)
	{
		if (!Stationed(corvette) || body < 0) return false;
		if (PatrolWings::OnBeat(corvette, body)) return true;
		auto lanes = w->Fleet->Lanes;
		return lanes != nullptr && lanes->SystemOf(body) == lanes->SystemOf(corvette->CorvetteStation);
	}

	int Corvettes::NextOnBeat(const cs::Ref<Ship>& corvette, int here)
	{
		int station = corvette->CorvetteStation;
		const auto& beat = corvette->Route;
		if (here != station || beat.Count() <= 1) return station;
		corvette->RouteIndex = corvette->RouteIndex % (beat.Count() - 1) + 1;
		return beat[corvette->RouteIndex];
	}

	cs::List<cs::Ref<Ship>> Corvettes::Raise(const cs::Ref<World>& w, double jd)
	{
		cs::List<cs::Ref<Ship>> raised;
		cs::List<cs::Ref<Faction>> factions;
		factions.AddRange(w->Fleet->Factions());
		for (const auto& f : factions)
		{
			if (!Keeps(f)) continue;
			auto ports = PatrolWings::Ports(w, f);
			int wanted = Wanted(ports.Count());
			for (int i = 0; i < wanted; i++) raised.Add(Station(w, f, ports[i], jd));
		}
		return raised;
	}

	cs::Ref<Ship> Corvettes::Station(const cs::Ref<World>& w, const cs::Ref<Faction>& f, int station, double jd)
	{
		int named = 0;
		if (w->Polities != nullptr)
			for (const auto& s : w->Polities->Navy(f))
				if (Stationed(s)) named++;
		cs::String name = cs::Cat(Names()[named % Names().Count()], (named >= Names().Count() ? cs::Cat(" ", named / Names().Count() + 1) : cs::String()));
		auto ship = cs::New<Ship>();
		ship->Name = f->Prefix.size() > 0 ? cs::Cat(f->Prefix, " ", name) : name;
		ship->Spec = ShipClassSpec::Of(ShipClass::Corvette);
		ship->Faction = f;
		ship->Role = ShipRole::Patrol;
		ship->Combat = cs::New<ShipCombat>(CombatSpec::Of(ShipClass::Corvette), cs::Nullable<ShipClass>(ShipClass::Corvette));
		ship->PatrolWing = true;
		ship->CorvetteStation = station;
		ship->Route = Beat(w, f, station);
		ship->RouteIndex = 0;
		w->Fleet->Receive(ship);
		ship->BandId = ship->Id;
		auto body = (*w->Catalog())[station];
		double r = cs::Math::Max(body->RadiusKm * 5, body->RadiusKm + 200);
		// Spread round the port by the golden angle, so two corvettes at one port never hold the same spot.
		double a = 2.399963 * (ship->Id % 97);
		auto offset = DVec3(cs::Math::Cos(a), cs::Math::Sin(a), 0.1).Normalized() * r;
		Fleet::PinAt(ship, station, offset, DVec3(-cs::Math::Sin(a), cs::Math::Cos(a), 0), jd);
		w->NotifyAdded(ship);
		return ship;
	}

	void Corvettes::Upkeep(const cs::Ref<World>& w, cs::Dictionary<cs::Ref<Faction>, double>& shortSince, double jd)
	{
		for (const auto& f : w->Fleet->Factions())
		{
			if (!Keeps(f)) continue;
			auto ports = PatrolWings::Ports(w, f);
			auto mine = Of(w, f);
			for (const auto& c : mine)
			{
				if (ports.Count() == 0 || ports.Contains(c->CorvetteStation)) continue;
				auto unmanned = Unmanned(ports, mine);
				Repost(w, c, unmanned.has_value() ? *unmanned : ports[0]);
			}
			int wanted = Wanted(ports.Count());
			if (mine.Count() >= wanted)
			{
				shortSince.Remove(f);
				continue;
			}
			double since = 0;
			if (!shortSince.TryGetValue(f, since))
			{
				shortSince[f] = jd;
				continue;
			}
			if (jd - since < RebuildDays) continue;
			auto port = Unmanned(ports, mine);
			if (!port.has_value()) continue;
			double price = Economy::HullPrice(ShipClass::Corvette);
			if (f->Treasury < price || !Crews::CanCrew(f, ShipClass::Corvette) || w->War->BattleAt(*port) != nullptr) continue;
			f->Treasury -= price;
			Crews::Draft(f, Crews::Complement(ShipClass::Corvette));
			Station(w, f, *port, jd);
			if (mine.Count() + 1 >= wanted) shortSince.Remove(f);
			else shortSince[f] = jd;
		}
	}

	cs::Nullable<int> Corvettes::Unmanned(const cs::List<int>& ports, const cs::List<cs::Ref<Ship>>& corvettes)
	{
		for (int p : ports)
		{
			bool manned = false;
			for (const auto& c : corvettes)
				if (c->CorvetteStation == p) manned = true;
			if (!manned) return p;
		}
		return std::nullopt;
	}

	void Corvettes::Repost(const cs::Ref<World>& w, const cs::Ref<Ship>& corvette, int station)
	{
		corvette->CorvetteStation = station;
		corvette->Route = Beat(w, corvette->Faction, station);
		corvette->RouteIndex = 0;
	}
}
