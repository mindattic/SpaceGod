// Port of Assets/UnitySpace/Core/Systems/LaneUpkeep.cs (see docs/MIGRATION.md).

#include "Systems/LaneUpkeep.h"

#include "DVec3.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/Settlement.h"
#include "Infrastructure/Infrastructure.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	// ================================================================ LaneCondition

	cs::String LaneCondition::Share(double v) { return cs::Cat(cs::Fmt(v * 100, "0.#"), "%"); }

	cs::String LaneCondition::Days(double v) { return cs::Cat(cs::Fmt(v, "0.##"), " d"); }

	const cs::Ref<RangeSetting>& LaneCondition::DecayPerDay()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.decayPerDay", SettingGroups::Simulation,
			"Lane wear", 0.02, 0, 0.2, 0.005,
			cs::Cat("How much of its condition a hyperlane loses each day nobody tends it (its accelerator rings drifting out of power and ",
				"alignment). At 2% an untended lane fails in a little over a month."),
			cs::Func<cs::String, double>(Share), "lane lanes hyperlane wear decay condition tender upkeep maintenance");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneCondition::TendPerDay()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.tendPerDay", SettingGroups::Simulation,
			"Lane tending", 0.1, 0, 1, 0.01,
			cs::Cat("How much of a hyperlane's condition each lane tender working it (riding it, or lying over the rings at either end) ",
				"restores a day."), cs::Func<cs::String, double>(Share), "lane lanes hyperlane tender tending repair upkeep maintenance");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneCondition::FailAt()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.failAt", SettingGroups::Simulation,
			"Lane failure point", 0.25, 0.05, 0.6, 0.05,
			cs::Cat("The condition at or below which a hyperlane fails: its ring gates go dark, nothing can join it, and traffic and trade ",
				"go round it (or fly under their own drives) until tenders bring it back."), cs::Func<cs::String, double>(Share), "lane lanes hyperlane fail failure condition");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneCondition::SlowestSpeed()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.slowestSpeed", SettingGroups::Simulation,
			"Worn lane speed", 0.4, 0.1, 1, 0.05,
			cs::Cat("The share of its full pace a hyperlane rides at when it is worn right down to its failure point; in between it slows ",
				"in proportion to its wear."), cs::Func<cs::String, double>(Share), "lane lanes hyperlane speed worn condition");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneCondition::WorstReliability()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.worstReliability", SettingGroups::Simulation,
			"Worn lane reliability", 0.6, 0, 1, 0.05,
			cs::Cat("The chance a ride goes through without a stall on a hyperlane worn right down to its failure point. A stalled ride ",
				"drops out part-way when the rings slip out of alignment and picks up again a while later."), cs::Func<cs::String, double>(Share),
			"lane lanes hyperlane reliability stall worn condition");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneCondition::StallDays()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.stallDays", SettingGroups::Simulation,
			"Lane stall", 0.3, 0, 3, 0.05,
			"About how long (days) a ride stalled by a worn hyperlane's misaligned rings waits beside the lane before it picks up again.",
			cs::Func<cs::String, double>(Days), "lane lanes hyperlane stall delay worn");
		return s;
	}

	bool LaneCondition::Carries(double condition) { return condition > FailAt()->Value; }

	double LaneCondition::Wear(double condition)
	{
		double span = cs::Math::Max(1e-6, 1 - FailAt()->Value);
		return cs::Math::Max(0.0, cs::Math::Min(1.0, (1 - condition) / span));
	}

	double LaneCondition::SpeedFactor(double condition) { return 1 - (1 - SlowestSpeed()->Value) * Wear(condition); }

	double LaneCondition::Reliability(double condition) { return 1 - (1 - WorstReliability()->Value) * Wear(condition); }

	// ================================================================ LaneUpkeep

	const cs::Ref<RangeSetting>& LaneUpkeep::TendersPerLane()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.tendersPerLane", SettingGroups::Simulation,
			"Tenders per lane", 1, 0, 4, 1,
			cs::Cat("How many lane tenders a polity keeps working each hyperlane whose ramps it keeps, building a new one at its yards when ",
				"one is lost. At 0 nobody tends the lanes and they all fail in time."), nullptr, "lane lanes hyperlane tender tenders upkeep");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneUpkeep::TenderValue()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.tenderValue", SettingGroups::Simulation,
			"Tender bounty", 2e7, 0, 1e8, 1e6,
			cs::Cat("What a lane tender is worth to raiders (credits, against a freighter's hold of about 5 million): they take on more ",
				"guns for one, and destroy it rather than rob it, to cut a system's lanes."),
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v / 1e6, "0.#"), "M cr"); }), "lane tender bounty raiders pirates target value");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneUpkeep::TenderFocus()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.tenderFocus", SettingGroups::Simulation,
			"Tender targeting", 20, 0, 60, 1,
			cs::Cat("How strongly warships and raiders in a fight favour a lane tender over other targets (as if it were that many km ",
				"nearer than it is)."), nullptr, "lane tender target targeting focus battle war");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneUpkeep::HuntDays()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.huntDays", SettingGroups::Simulation,
			"Tender hunting range", 5, 0, 30, 0.5,
			cs::Cat("How far (days of travel) a navy at war, or a raider band, goes out of its way to fall on a hostile lane tender. At 0 ",
				"nobody hunts them."), cs::Func<cs::String, double>(Days), "lane tender hunt hunting war raiders navy range");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneUpkeep::RaiderHuntChance()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.raiderHuntChance", SettingGroups::Simulation,
			"Raiders hunt tenders", 0.5, 0, 1, 0.05,
			cs::Cat("The chance a raider band planning its next move goes after a hostile lane tender in reach rather than lie in wait or ",
				"raid a port."), cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }), "lane tender raiders pirates hunt");
		return s;
	}

	const cs::Ref<RangeSetting>& LaneUpkeep::DwellDays()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("lanes.tenderDwellDays", SettingGroups::Simulation,
			"Tender work at the ramps", 1.5, 0.1, 10, 0.1,
			"About how long (days) a lane tender lies over the rings at each end of its lane before riding it back to the other.",
			cs::Func<cs::String, double>(Days), "lane tender dwell ramp work");
		return s;
	}

	cs::String LaneUpkeep::Days(double v) { return cs::Cat(cs::Fmt(v, "0.##"), " d"); }

	const cs::List<cs::String>& LaneUpkeep::Names()
	{
		static const cs::List<cs::String> names
		{
			"Steady Hand", "True Line", "Lamplighter", "Ringwright", "Plumb Line", "Long Watch", "Quiet Work", "Linesman",
			"Patient Light", "Fair Passage", "Keeps Faith", "Even Keel", "Bright Rings", "Wayfinder", "Good Order", "Sure Footing",
		};
		return names;
	}

	LaneUpkeep::LaneUpkeep(World* world, int seed)
		: world(world), rng(seed)
	{
	}

	cs::Ref<Hyperlanes> LaneUpkeep::Lanes() const { return world->Fleet->Lanes; }

	bool LaneUpkeep::IsTender(cs::Ref<Ship> ship) { return ship != nullptr && ship->Spec != nullptr && ship->Spec->Class == ShipClass::LaneTender; }

	cs::Ref<Lane> LaneUpkeep::LaneOf(cs::Ref<Ship> tender)
	{
		cs::Ref<Lane> lane;
		return tender != nullptr && Tending.TryGetValue(tender, lane) ? lane : nullptr;
	}

	cs::Ref<Faction> LaneUpkeep::OwnerOf(cs::Ref<Lane> lane)
	{
		if (lane == nullptr) return nullptr;
		cs::Ref<Faction> f;
		if (owners.TryGetValue(lane, f)) return f;
		f = world->LaneKeeper(lane->A);
		if (!f) f = world->LaneKeeper(lane->B);
		if (f != nullptr && f->IsPirate) f = nullptr;
		owners[lane] = f;
		return f;
	}

	bool LaneUpkeep::Alive(cs::Ref<Ship> ship) { return ship != nullptr && !ship->Destroyed && !ship->Derelict && !ship->GateDeparted; }

	int LaneUpkeep::TendersOn(cs::Ref<Lane> lane)
	{
		int n = 0;
		for (auto [key, value] : Tending)
			if (value == lane && Alive(key)) n++;
		return n;
	}

	bool LaneUpkeep::Working(cs::Ref<Ship> tender, cs::Ref<Lane> lane)
	{
		if (!Alive(tender) || tender->Battle != nullptr || lane == nullptr) return false;
		auto lanes = Lanes();
		if (tender->Phase == ShipPhase::Lane || tender->Phase == ShipPhase::Lurk || tender->Phase == ShipPhase::Adrift)
			return tender->LaneLeg.Lane == lane;
		if (tender->Phase != ShipPhase::Parked || tender->Orbit == nullptr || lanes == nullptr) return false;
		int system = lanes->SystemOf(tender->Orbit->Body);
		return system == lane->A || system == lane->B;
	}

	void LaneUpkeep::Update(double jd)
	{
		auto lanes = Lanes();
		if (lanes == nullptr) return;
		if (cs::Double::IsNaN(ownersJd) || jd - ownersJd >= 1 || jd < ownersJd)
		{
			owners.Clear();
			ownersJd = jd;
		}
		if (!raised)
		{
			raised = true;
			Raise(jd);
		}
		if (cs::Double::IsNaN(lastJd) || jd <= lastJd)
		{
			if (cs::Double::IsNaN(lastJd)) lastJd = jd;
			return;
		}
		double dt = cs::Math::Min(jd - lastJd, 30.0);
		lastJd = jd;
		cs::Dictionary<cs::Ref<Lane>, int> working;
		for (auto [key, value] : Tending)
			if (Working(key, value))
			{
				int n = 0;
				working[value] = (working.TryGetValue(value, n) ? n : 0) + 1;
			}
		for (const auto& lane : lanes->Lanes)
		{
			int tenders = 0;
			working.TryGetValue(lane, tenders);
			double before = lanes->ConditionOf(lane);
			double after = before + (tenders * LaneCondition::TendPerDay()->Value - LaneCondition::DecayPerDay()->Value) * dt;
			if (after >= 1 && before >= 1) continue;
			bool carried = lanes->Carries(lane);
			lanes->SetCondition(lane, after);
			bool carries = lanes->Carries(lane);
			if (carried == carries) continue;
			if (!carries)
				world->Log(jd, cs::Cat("The ", lane->Name, " lane has failed: its rings have gone dark for want of tending, and traffic goes round it"),
					WorldEventKind::Loss, lane->A);
			else
				world->Log(jd, cs::Cat("Tenders have the ", lane->Name, " lane's rings lit and aligned again: it is back in service"), WorldEventKind::Info, lane->A);
			LaneChanged.Invoke(lane, !carries, jd);
		}
	}

	cs::List<cs::Ref<Ship>> LaneUpkeep::Raise(double jd)
	{
		cs::List<cs::Ref<Ship>> made;
		auto lanes = Lanes();
		if (lanes == nullptr) return made;
		for (const auto& lane : lanes->Lanes)
		{
			auto owner = OwnerOf(lane);
			if (owner == nullptr) continue;
			for (int k = TendersOn(lane); k < TendersPerLane()->IntValue(); k++)
				made.Add(Commission(owner, lane, made.Count() % 2 == 0 ? lane->A : lane->B, jd, nullptr));
		}
		return made;
	}

	cs::Ref<Ship> LaneUpkeep::Commission(cs::Ref<Faction> owner, cs::Ref<Lane> lane, int body, double jd, cs::Ref<ParkingOrbit> orbit)
	{
		const auto& names = Names();
		cs::String name = cs::Cat(names[named % names.Count()], named >= names.Count() ? cs::Cat(" ", named / names.Count() + 1) : cs::String());
		named++;
		auto ship = world->Fleet->AddShip(name, ShipClass::LaneTender, owner, ShipRole::Tender);
		ship->Ports = cs::List<int>{ lane->A, lane->B };
		ship->Cargo = cs::Cat("Tending the ", lane->Name, " lane");
		Tending[ship] = lane;
		world->Fleet->Station(ship, body, jd, orbit);
		return ship;
	}

	cs::Ref<Lane> LaneUpkeep::ShortOfTenders(cs::Ref<Faction> owner)
	{
		auto lanes = Lanes();
		if (owner == nullptr || lanes == nullptr) return nullptr;
		cs::Ref<Lane> best = nullptr;
		int bestShort = 0;
		double bestCondition = cs::Double::MaxValue;
		for (const auto& lane : lanes->Lanes)
		{
			if (OwnerOf(lane) != owner) continue;
			int shortBy = TendersPerLane()->IntValue() - TendersOn(lane);
			if (shortBy <= 0) continue;
			double c = lanes->ConditionOf(lane);
			if (shortBy > bestShort || (shortBy == bestShort && c < bestCondition))
			{
				best = lane;
				bestShort = shortBy;
				bestCondition = c;
			}
		}
		return best;
	}

	cs::Nullable<ShipClass> LaneUpkeep::OrderAt(cs::Ref<Settlement> yard)
	{
		if ((yard ? yard->Owner : nullptr) == nullptr || yard->Owner->IsPirate || !raised) return std::nullopt;
		return ShortOfTenders(yard->Owner) != nullptr ? cs::Nullable<ShipClass>(ShipClass::LaneTender) : std::nullopt;
	}

	cs::Ref<Ship> LaneUpkeep::Launch(cs::Ref<Settlement> yard, double jd)
	{
		auto lane = ShortOfTenders(yard ? yard->Owner : nullptr);
		if (lane == nullptr) return nullptr;
		cs::Ref<ParkingOrbit> orbit = nullptr;
		if (world->Infrastructure != nullptr)
		{
			auto slip = world->Infrastructure->YardOf(yard);
			if (slip != nullptr) orbit = slip->Orbit;
		}
		auto ship = Commission(yard->Owner, lane, yard->Body, jd, orbit);
		world->Log(jd, cs::Cat(yard->Name, " launches the lane tender ", ship->Name, " for the upkeep of the ", lane->Name, " lane"), WorldEventKind::Launch, yard->Body);
		return ship;
	}

	int LaneUpkeep::ChooseNext(cs::Ref<Ship> tender, int here, double jd)
	{
		auto lanes = Lanes();
		auto lane = LaneOf(tender);
		if (lane == nullptr || OwnerOf(lane) != tender->Faction)
		{
			auto other = ShortOfTenders(tender->Faction);
			if (other != nullptr)
			{
				lane = other;
				Tending[tender] = lane;
				tender->Ports = cs::List<int>{ lane->A, lane->B };
				tender->Cargo = cs::Cat("Tending the ", lane->Name, " lane");
			}
		}
		if (lane == nullptr || lanes == nullptr) return here;
		int system = lanes->SystemOf(here);
		bool atEnd = system == lane->A || system == lane->B;
		if (!lanes->Carries(lane)) return atEnd ? here : Nearer(lane, here, jd);
		if (system == lane->A) return lane->B;
		if (system == lane->B) return lane->A;
		return Nearer(lane, here, jd);
	}

	int LaneUpkeep::Nearer(cs::Ref<Lane> lane, int here, double jd)
	{
		auto e = world->Ephemeris;
		auto at = e->HelioAt(here, jd);
		return (e->HelioAt(lane->A, jd) - at).Length() <= (e->HelioAt(lane->B, jd) - at).Length() ? lane->A : lane->B;
	}

	double LaneUpkeep::Dwell(cs::Ref<Ship> /*tender*/) { return DwellDays()->Value * (0.75 + rng.NextDouble() * 0.5); }

	void LaneUpkeep::OnLaneJoined(cs::Ref<Ship> ship, LaneHop hop, double /*jd*/)
	{
		auto lanes = Lanes();
		if (lanes == nullptr || ship == nullptr || ship == world->PilotedShip) return;
		if (world->Fleet->IsHeld != nullptr && world->Fleet->IsHeld(ship)) return;
		double reliability = lanes->Reliability(hop.Lane);
		if (reliability >= 1 || rng.NextDouble() < reliability) return;
		double days = LaneCondition::StallDays()->Value * (0.5 + rng.NextDouble());
		if (days <= 0) return;
		world->Fleet->StallRide(ship, 0.2 + rng.NextDouble() * 0.6, days);
	}

	int LaneUpkeep::HuntTarget(cs::Ref<Ship> hunter, int here, double jd)
	{
		if (hunter == nullptr || HuntDays()->Value <= 0) return -1;
		auto f = hunter->Faction;
		int best = -1;
		double bestDays = HuntDays()->Value;
		for (const auto& t : Tending.Keys())
		{
			if (!Alive(t) || t->Battle != nullptr || t->Faction == nullptr || t->Faction == f) continue;
			if (!world->Diplomacy()->Hostile(f, t->Faction)) continue;
			if (!f->IsPirate && (t->Faction->IsPirate || world->War == nullptr || !world->War->AtWar(f))) continue;
			int body = t->Phase == ShipPhase::Parked && t->Orbit != nullptr ? t->Orbit->Body : t->DestinationBody;
			if (body < 0 || body == here) continue;
			double days = world->Fleet->EstimateTravelDays(hunter, here, body, jd);
			if (days <= bestDays)
			{
				bestDays = days;
				best = body;
			}
		}
		return best;
	}

	bool LaneUpkeep::RaidersHunt() { return rng.NextDouble() < RaiderHuntChance()->Value; }
}
