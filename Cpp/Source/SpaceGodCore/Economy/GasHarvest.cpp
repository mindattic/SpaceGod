// Not a C# file (US-T146): gas giant harvesting (see GasHarvest.h).
#include "Economy/GasHarvest.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "ScaleModel.h"
#include "Combat/BioMass.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Comms/Pilots.h"
#include "Economy/CargoHold.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Infrastructure/Infrastructure.h"
#include "Law/Law.h"
#include "Persistence/ObjectGraph.h"

namespace SpaceGod::Core
{
	namespace
	{
		constexpr int He3 = static_cast<int>(Good::HeliumThree);
		constexpr int Hydro = static_cast<int>(Good::Hydrocarbons);
		constexpr int Nitro = static_cast<int>(Good::Nitrogen);
		/// <summary>What a platform's tanks hold, scooped and carried: the fusion fuel first, then the volatiles.</summary>
		constexpr int Gases[] = { He3, Hydro, Nitro };

		/// <summary>Earth's GM, km^3/s^2: a giant's mass in Earths.</summary>
		constexpr double EarthGm = 398600.4418;
		/// <summary>Above this many Earth masses a giant is Jupiter-like (hydrogen and helium); below, an ice giant.</summary>
		constexpr double JovianMassEarth = 50;

		/// <summary>The life-story beat a guard's finished contract writes (US-T138's kinds).</summary>
		constexpr LifeEventKind GuardedBeat = LifeEventKind::OwnedAct;

		double Sum(const cs::List<double>& v)
		{
			double t = 0;
			for (int g : Gases) t += v[g];
			return t;
		}

		cs::String Cr(double v) { return cs::Fmt(v, "N0"); }
		cs::String T(double v) { return cs::Fmt(v, "N0"); }

		/// <summary>A tanker's flip and burn at a working merchant's 0.3 g, days, and a few hours alongside.</summary>
		double BurnDays(double km)
		{
			constexpr double accelKmS2 = 0.3 * 9.80665e-3;
			return 2 * cs::Math::Sqrt(cs::Math::Max(km, 0.0) / accelKmS2) / Astro::SecondsPerDay + 0.25;
		}

		cs::String ThreatName(HarvestThreat t, const cs::Ref<Faction>& by)
		{
			switch (t)
			{
			case HarvestThreat::Pirates: return by != nullptr ? cs::Cat(by->Name, " raiders") : cs::String("Raiders");
			case HarvestThreat::BioMass: return "The Bio Mass";
			case HarvestThreat::VoidRaiders: return "Ghost ships out of the void";
			case HarvestThreat::Rival: return by != nullptr ? cs::Cat(by->Name, " warships") : cs::String("Enemy warships");
			}
			return "Raiders";
		}

		/// <summary>The pilot's own record, by the hull they command (as comms books a crime, US-T137): never the SpaceGod's.</summary>
		cs::Ref<LawRecord> CaptainRecord(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::String& pilotName)
		{
			if (world == nullptr || world->Law == nullptr || ship == nullptr) return nullptr;
			auto captain = ship->Controller();
			cs::Ref<LawRecord> rec;
			if (!world->Law->Captains.TryGetValue(captain, rec) || rec == nullptr)
			{
				rec = cs::New<LawRecord>(!pilotName.empty() ? pilotName : cs::Cat("the master of the ", captain->Name), captain);
				world->Law->Captains[captain] = rec;
			}
			return rec;
		}

		void Standing(const cs::Ref<LawRecord>& rec, const cs::Ref<Faction>& f, double delta)
		{
			if (rec == nullptr || f == nullptr || f->IsPirate || delta == 0) return;
			if (rec->Standing == nullptr) rec->Standing = cs::New<cs::Dictionary<cs::Ref<Faction>, double>>();
			double v = 0;
			rec->Standing->TryGetValue(f, v);
			(*rec->Standing)[f] = cs::Math::Clamp(v + delta, -100.0, 100.0);
		}
	}

	const char* ToString(HarvestThreat v)
	{
		switch (v)
		{
		case HarvestThreat::Pirates: return "Pirates";
		case HarvestThreat::BioMass: return "BioMass";
		case HarvestThreat::VoidRaiders: return "VoidRaiders";
		case HarvestThreat::Rival: return "Rival";
		}
		return "?";
	}

	const char* ToString(TankerLeg v)
	{
		switch (v)
		{
		case TankerLeg::Waiting: return "Waiting";
		case TankerLeg::Outbound: return "Outbound";
		case TankerLeg::Returning: return "Returning";
		}
		return "?";
	}

	// ---------------------------------------------------------------- the pieces

	double HarvestPlatform::TankTonnes() const { return Sum(Tank); }

	double HarvestTanker::CargoTonnes() const { return Sum(Cargo); }

	double HarvestTanker::Progress(double jd) const
	{
		if (Leg == TankerLeg::Waiting || ArriveJd <= DepartJd) return 0;
		return cs::Math::Clamp((jd - DepartJd) / (ArriveJd - DepartJd), 0.0, 1.0);
	}

	double HarvestOperation::OrbitDays(int platform) const
	{
		if (platform < 0 || platform >= Platforms.Count()) return 0.1;
		auto st = Platforms[platform]->Structure;
		if (st == nullptr || st->Orbit == nullptr || st->Orbit->RateRadPerDay <= 0) return 0.1;
		return cs::Math::Tau / st->Orbit->RateRadPerDay;
	}

	int HarvestOperation::Working(double jd) const
	{
		int n = 0;
		for (const auto& p : Platforms)
			if (!p->Down(jd)) n++;
		return n;
	}

	int HarvestContract::OrbitsDone(double jd) const
	{
		if (cs::Double::IsNaN(StartJd) || OrbitDays <= 0) return 0;
		return cs::Math::Clamp(static_cast<int>(cs::Math::Floor((jd - StartJd) / OrbitDays)), 0, Orbits);
	}

	cs::Ref<HarvestWave> HarvestContract::LiveWave() const
	{
		for (const auto& w : Waves)
			if (w->Spawned && !w->Resolved) return w;
		return nullptr;
	}

	cs::Ref<HarvestWave> HarvestContract::NextWave() const
	{
		for (const auto& w : Waves)
			if (!w->Spawned && !w->Resolved) return w;
		return nullptr;
	}

	cs::String HarvestContract::Title() const
	{
		auto name = Operation != nullptr && Platform >= 0 && Platform < Operation->Platforms.Count() ? Operation->Platforms[Platform]->Name : cs::String("a skimmer");
		return cs::Cat("Guard the ", name, " for ", Orbits, " orbits");
	}

	// ---------------------------------------------------------------- the system's works

	GasHarvest::GasHarvest(const cs::Ref<World>& world_, int seed, double jd) : world(world_), rng(seed), lastJd(jd)
	{
		Plan(jd);
		nextDayJd = jd;
	}

	double GasHarvest::GuardKm(int body) const
	{
		auto def = (*world->Catalog())[body];
		return cs::Math::Max(ScaleModel::NeighbourhoodKm(def), def->RadiusKm * 6);
	}

	void GasHarvest::Plan(double jd)
	{
		auto cat = world->Catalog();
		if (cat == nullptr || cat->Count() == 0) return;
		bool beyondSol = cat->Root()->Name != "Sun";
		cs::List<int> giants;
		for (int b = 1; b < cat->Count(); b++)
		{
			auto def = (*cat)[b];
			if (def->GasGiant && def->Kind != BodyKind::Star && def->RadiusKm > 0 && def->Gm > 0) giants.Add(b);
		}
		// The outermost giant of a system beyond Sol lies where the Void Raiders reach (US-T124).
		int outermost = -1;
		double far = -1;
		for (int b : giants)
		{
			double d = (world->Ephemeris->HelioAt(b, jd) - world->Ephemeris->HelioAt(0, jd)).Length();
			if (d > far) { far = d; outermost = b; }
		}
		for (int b : giants)
		{
			auto def = (*cat)[b];
			auto op = cs::New<HarvestOperation>();
			op->Name = cs::Cat(def->Name, " Gas Works");
			op->Body = b;
			op->Outermost = beyondSol && b == outermost;
			double massEarth = def->Gm / EarthGm;
			bool jovian = massEarth >= JovianMassEarth;
			int n = cs::Math::Min(6, (jovian ? (massEarth > 200 ? 5 : 4) : 3) + rng.Next(0, 2));
			// A Jupiter-like giant's air is hydrogen and helium: more helium-3; an ice giant's richer in methane and ammonia.
			double richness = 0.8 + 0.4 * rng.NextDouble();
			op->Rate[He3] = (jovian ? 14.0 : 7.0) * richness;
			op->Rate[Hydro] = (jovian ? 220.0 : 380.0) * richness;
			op->Rate[Nitro] = (jovian ? 60.0 : 150.0) * richness;
			for (int i = 0; i < n; i++)
			{
				auto p = cs::New<HarvestPlatform>();
				p->Name = cs::Cat(def->Name, " Skimmer ", i + 1);
				p->Seed = rng.Next();
				auto st = cs::New<OrbitalStructure>();
				st->Name = p->Name;
				st->Kind = StructureKind::Depot;
				st->Body = b;
				// Low, just over the cloud tops its skimmers dive into; tilted clear of any rings.
				double radius = def->RadiusKm * (1.06 + 0.24 * rng.NextDouble());
				double inc = def->HasRings() ? 25 + 35 * rng.NextDouble() : 5 + 40 * rng.NextDouble();
				double node = 360 * rng.NextDouble();
				double phase = 360.0 * i / n + 30 * rng.NextDouble();
				st->Orbit = Infrastructure::EquatorialOrbit(def, radius, inc, node, phase);
				st->LengthKm = 2.2 + 1.4 * rng.NextDouble();
				st->Note = "A gas skimmer platform: tank farms in low orbit, its skimmers diving into the cloud tops (US-T146).";
				p->Structure = st;
				op->Platforms.Add(p);
			}
			for (int i = 0; i < n; i++)
			{
				auto t = cs::New<HarvestTanker>();
				t->Name = cs::Cat(def->Name, " Tanker ", ++tankerSerial);
				t->Seed = rng.Next();
				t->Platform = i;
				op->Tankers.Add(t);
			}
			Rehome(op, jd);
			Operations.Add(op);
		}
	}

	void GasHarvest::Update(double jd)
	{
		if (world == nullptr) return;
		double dt = cs::Double::IsNaN(lastJd) ? 0 : cs::Math::Max(0.0, jd - lastJd);
		lastJd = jd;
		if (cs::Double::IsNaN(nextDayJd)) nextDayJd = jd;
		int days = 0;
		while (jd >= nextDayJd && days < 400)
		{
			Day(nextDayJd);
			nextDayJd += 1;
			days++;
		}
		if (jd >= nextDayJd) nextDayJd = jd + 1; // a jump of years: the works catch up from here
		RunContracts(jd, dt);
	}

	void GasHarvest::Day(double jd)
	{
		double pressure = PiratePressure();
		for (const auto& op : Operations)
		{
			Rehome(op, jd);
			Harvest(op, jd);
			SailTankers(op, jd);
			MoveTankers(op, jd, pressure);
			RaidPlatforms(op, jd, pressure);
		}
		PostContracts(jd);
		Trim();
	}

	void GasHarvest::Rehome(const cs::Ref<HarvestOperation>& op, double jd)
	{
		auto cat = world->Catalog();
		auto usable = [](const cs::Ref<Settlement>& s) { return s != nullptr && !s->Fallen && s->Kind != SettlementKind::Haven; };
		if (!usable(op->Port))
		{
			// The biggest port among the giant's own moons, else the nearest port in the system.
			cs::Ref<Settlement> best;
			for (const auto& s : world->Economy->Settlements)
				if (usable(s) && (s->Body == op->Body || (*cat)[s->Body]->ParentIndex == op->Body) && (best == nullptr || s->Population > best->Population)) best = s;
			if (best == nullptr)
			{
				double bestKm = cs::Double::PositiveInfinity;
				auto here = world->Ephemeris->HelioAt(op->Body, jd);
				for (const auto& s : world->Economy->Settlements)
				{
					if (!usable(s)) continue;
					double km = (world->Ephemeris->HelioAt(s->Body, jd) - here).Length();
					if (km < bestKm) { bestKm = km; best = s; }
				}
			}
			op->Port = best;
		}
		auto owner = op->Port != nullptr && op->Port->Owner != nullptr && !op->Port->Owner->IsPirate ? op->Port->Owner : world->Independents;
		op->Owner = owner;
		for (const auto& p : op->Platforms)
			if (p->Structure != nullptr) p->Structure->SetOwner(owner);
	}

	void GasHarvest::Harvest(const cs::Ref<HarvestOperation>& op, double jd)
	{
		for (const auto& p : op->Platforms)
		{
			p->Condition = cs::Math::Min(1.0, p->Condition + 0.04);
			if (p->Down(jd)) continue;
			double scoop = Sum(op->Rate) * p->Condition;
			double room = cs::Math::Max(0.0, TankCapacity - p->TankTonnes());
			double share = scoop > 0 ? cs::Math::Min(1.0, room / scoop) * p->Condition : 0;
			for (int g : Gases)
			{
				double t = op->Rate[g] * share;
				p->Tank[g] += t;
				op->Harvested[g] += t;
			}
		}
	}

	double GasHarvest::TransitDays(int from, int to, double jd) const
	{
		return BurnDays((world->Ephemeris->HelioAt(from, jd) - world->Ephemeris->HelioAt(to, jd)).Length());
	}

	cs::Ref<Settlement> GasHarvest::Destination(const cs::Ref<HarvestOperation>& op, const cs::List<double>& load, double jd) const
	{
		cs::Ref<Settlement> best;
		double bestScore = 0;
		auto diplomacy = world->Diplomacy();
		for (const auto& s : world->Economy->Settlements)
		{
			if (s->Fallen || s->Kind == SettlementKind::Haven) continue;
			if (op->Owner != nullptr && s->Owner != nullptr && s->Owner != op->Owner && diplomacy != nullptr && diplomacy->Hostile(op->Owner, s->Owner)) continue;
			double value = 0;
			for (int g : Gases)
			{
				double room = cs::Math::Max(0.0, s->Capacity(static_cast<Good>(g)) - s->Stock[g]);
				value += cs::Math::Min(load[g], room) * s->Price[g];
			}
			if (value <= 0) continue;
			// Worth over the days to get there; the owner's own ports first.
			double score = value / (1 + TransitDays(op->Body, s->Body, jd)) * (s->Owner == op->Owner ? 1.5 : 1.0);
			if (score > bestScore) { bestScore = score; best = s; }
		}
		return best;
	}

	void GasHarvest::SailTankers(const cs::Ref<HarvestOperation>& op, double jd)
	{
		for (const auto& t : op->Tankers)
		{
			if (t->Lost)
			{
				if (jd < t->ReplacedJd) continue;
				// A new hull off the slips, paid for by the owner.
				t->Lost = false;
				t->Leg = TankerLeg::Waiting;
				for (int g : Gases) t->Cargo[g] = 0;
				if (op->Owner != nullptr) op->Owner->Treasury -= 2.5e6;
			}
			if (t->Leg != TankerLeg::Waiting) continue;
			int fullest = -1;
			double most = TankerLoad * 0.75;
			for (int i = 0; i < op->Platforms.Count(); i++)
				if (op->Platforms[i]->TankTonnes() >= most) { most = op->Platforms[i]->TankTonnes(); fullest = i; }
			if (fullest < 0) continue;
			auto p = op->Platforms[fullest];
			double share = cs::Math::Min(1.0, TankerLoad / p->TankTonnes());
			cs::List<double> load = cs::NewArray<double>(GoodInfo::Count);
			for (int g : Gases) load[g] = p->Tank[g] * share;
			auto to = Destination(op, load, jd);
			if (to == nullptr) continue; // nobody wants it: it stays in the tanks
			for (int g : Gases)
			{
				p->Tank[g] -= load[g];
				t->Cargo[g] = load[g];
			}
			t->Platform = fullest;
			t->To = to;
			t->Leg = TankerLeg::Outbound;
			t->DepartJd = jd;
			t->ArriveJd = jd + TransitDays(op->Body, to->Body, jd);
		}
	}

	void GasHarvest::MoveTankers(const cs::Ref<HarvestOperation>& op, double jd, double basePressure)
	{
		double pressure = Pressure(op, basePressure);
		for (const auto& t : op->Tankers)
		{
			if (t->Lost) continue;
			if (t->Leg == TankerLeg::Outbound)
			{
				// One draw a day under way, whatever happens, so the stream stays in step.
				double roll = rng.NextDouble();
				if (roll < TankerRaidPerDay * pressure)
				{
					cs::Ref<Faction> by;
					auto threat = DrawThreat(op, by);
					if (threat == HarvestThreat::BioMass) threat = HarvestThreat::Pirates, by = world->Pirates; // the meat takes no tankers
					for (int g : Gases) op->Plundered[g] += t->Cargo[g];
					double fuel = t->Cargo[He3];
					op->FuelLost += fuel;
					double tonnes = t->CargoTonnes();
					for (int g : Gases) t->Cargo[g] = 0;
					t->Lost = true;
					t->ReplacedJd = jd + TankerRebuildDays;
					op->TankersLost++;
					op->Raids++;
					op->LastRaidJd = jd;
					op->LastThreat = threat;
					Log(jd, cs::Cat(ThreatName(threat, by), " took the ", t->Name, " bound for ", t->To != nullptr ? t->To->Name : cs::String("port"), " with ",
						T(tonnes), " t of gas (", T(fuel), " t of helium-3): ", op->Owner != nullptr ? op->Owner->Name : cs::String("its owner"), "'s fuel supply is cut"), op->Body, true);
					continue;
				}
				if (jd < t->ArriveJd) continue;
				auto to = t->To;
				double pay = 0;
				if (to != nullptr && !to->Fallen)
				{
					for (int g : Gases)
					{
						auto good = static_cast<Good>(g);
						double room = cs::Math::Max(0.0, to->Capacity(good) * 1.25 - to->Stock[g]);
						double sold = cs::Math::Min(t->Cargo[g], room);
						if (sold <= 0) continue;
						pay += to->SaleValue(good, sold) * OwnerShare;
						to->Stock[g] += sold;
						op->Delivered[g] += sold;
					}
				}
				for (int g : Gases) t->Cargo[g] = 0; // what the market had no room for is vented
				if (op->Owner != nullptr) op->Owner->Treasury += pay;
				op->Deliveries++;
				t->Trips++;
				double leg = cs::Math::Max(0.25, t->ArriveJd - t->DepartJd);
				t->Leg = TankerLeg::Returning;
				t->DepartJd = jd;
				t->ArriveJd = jd + leg;
			}
			else if (t->Leg == TankerLeg::Returning && jd >= t->ArriveJd)
			{
				t->Leg = TankerLeg::Waiting;
			}
		}
	}

	void GasHarvest::RaidPlatforms(const cs::Ref<HarvestOperation>& op, double jd, double basePressure)
	{
		double pressure = Pressure(op, basePressure);
		// A guard on station under a contract here keeps raiders off the rest of the works too; the port's own gun
		// platforms do, a little, when it is one of the giant's moons.
		bool guarded = false;
		for (const auto& c : Contracts)
			if (c->State == ContractState::Taken && c->Operation == op && !cs::Double::IsNaN(c->GuardSeenJd) && jd - c->GuardSeenJd < 0.5
				&& c->GuardKmFromGiant <= GuardKm(op->Body)) guarded = true;
		bool defended = op->Port != nullptr && op->Port->Platforms.Count() > 0 && (op->Port->Body == op->Body || (*world->Catalog())[op->Port->Body]->ParentIndex == op->Body);
		double cut = (guarded ? 1 - GuardCut : 1.0) * (defended ? 1 - DefendedCut : 1.0);
		for (int i = 0; i < op->Platforms.Count(); i++)
		{
			double roll = rng.NextDouble();
			auto p = op->Platforms[i];
			if (p->Down(jd)) continue;
			// A platform under a contract is the guard's: its raids are the contract's waves.
			bool contracted = false;
			for (const auto& c : Contracts)
				if (c->State == ContractState::Taken && c->Operation == op && c->Platform == i) contracted = true;
			if (contracted || roll >= PlatformRaidPerDay * pressure * cut) continue;
			cs::Ref<Faction> by;
			auto threat = DrawThreat(op, by);
			Knock(op, i, threat, by, jd, "");
		}
	}

	void GasHarvest::Knock(const cs::Ref<HarvestOperation>& op, int platform, HarvestThreat by, const cs::Ref<Faction>& raiders, double jd, const cs::String& how)
	{
		auto p = op->Platforms[platform];
		double tonnes = p->TankTonnes(), fuel = p->Tank[He3];
		for (int g : Gases)
		{
			if (by != HarvestThreat::BioMass) op->Plundered[g] += p->Tank[g];
			p->Tank[g] = 0;
		}
		op->FuelLost += fuel;
		p->Condition = cs::Math::Max(0.05, p->Condition - RaidWear);
		double down = DownDaysMin + (DownDaysMax - DownDaysMin) * rng.NextDouble();
		p->DownUntilJd = jd + down;
		p->Raids++;
		op->Raids++;
		op->LastRaidJd = jd;
		op->LastThreat = by;
		cs::String what = by == HarvestThreat::BioMass ? cs::String("fed on") : by == HarvestThreat::Rival ? cs::String("shot up") : cs::String("raided");
		Log(jd, cs::Cat(ThreatName(by, raiders), " ", what, " the ", p->Name, how, ": ", T(tonnes), " t of gas lost (", T(fuel), " t of helium-3), the platform down ",
			cs::Fmt(down, "N0"), " days; ", op->Owner != nullptr ? op->Owner->Name : cs::String("its owner"), "'s fuel supply is cut"), op->Body, true);
	}

	// ---------------------------------------------------------------- threats

	double GasHarvest::PiratePressure() const
	{
		double raiders = 0;
		for (const auto& s : world->Fleet->Ships())
			if (!s->Destroyed && !s->IsEscort() && s->Faction != nullptr && s->Faction->IsPirate) raiders++;
		for (const auto& h : world->Economy->Settlements)
			if (h->Kind == SettlementKind::Haven && !h->Fallen) raiders += 0.5;
		return cs::Math::Min(1.0, raiders / 6.0);
	}

	double GasHarvest::Pressure(const cs::Ref<HarvestOperation>& op, double basePressure) const
	{
		double p = basePressure;
		if (world->Contracts != nullptr) p += 0.6 * world->Contracts->DangerAt(op->Body);
		if (BioMassNear(op->Body, world->Jd)) p += 0.3;
		if (op->Outermost) p += 0.15;
		if (RivalOf(op->Owner, nullptr) != nullptr) p += 0.2;
		return cs::Math::Clamp(p, 0.05, 1.5);
	}

	bool GasHarvest::BioMassNear(int body, double jd) const
	{
		auto bio = world->BioMass;
		if (bio == nullptr || cs::Double::IsNaN(jd)) return false;
		auto here = world->Ephemeris->HelioAt(body, jd);
		double reach = cs::Math::Max(5e6, (*world->Catalog())[body]->RadiusKm * 8);
		for (const auto& g : bio->Globules)
			if (g->Alive() && (bio->Helio(g, jd) - here).Length() < reach) return true;
		return false;
	}

	cs::Ref<Faction> GasHarvest::RivalOf(const cs::Ref<Faction>& owner, const cs::Ref<Faction>& guard) const
	{
		auto diplomacy = world->Diplomacy();
		if (owner == nullptr || diplomacy == nullptr) return nullptr;
		auto fits = [&](const cs::Ref<Faction>& f)
		{
			return f != nullptr && f != owner && !f->IsPirate && diplomacy->Hostile(f, owner) && (guard == nullptr || (f != guard && diplomacy->Hostile(f, guard)));
		};
		for (const auto& f : { world->RivalA, world->RivalB, world->Sol, world->Reach, world->Belt })
			if (fits(f)) return f;
		for (const auto& s : world->Economy->Settlements)
			if (fits(s->Owner)) return s->Owner;
		return nullptr;
	}

	HarvestThreat GasHarvest::DrawThreat(const cs::Ref<HarvestOperation>& op, cs::Ref<Faction>& by)
	{
		double pirates = world->Pirates != nullptr ? 1.0 : 0.0;
		double bio = BioMassNear(op->Body, world->Jd) ? 0.8 : 0.0;
		double ghosts = op->Outermost ? 0.5 : 0.0;
		auto rival = RivalOf(op->Owner, nullptr);
		double war = rival != nullptr ? 0.7 : 0.0;
		double total = pirates + bio + ghosts + war;
		double r = rng.NextDouble() * cs::Math::Max(total, 1e-9);
		if (total <= 0 || (r -= pirates) < 0) { by = world->Pirates; return HarvestThreat::Pirates; }
		if ((r -= bio) < 0) { by = nullptr; return HarvestThreat::BioMass; }
		if ((r -= ghosts) < 0) { by = world->Pirates; return HarvestThreat::VoidRaiders; }
		by = rival;
		return HarvestThreat::Rival;
	}

	// ---------------------------------------------------------------- queries

	cs::Ref<HarvestOperation> GasHarvest::At(int body) const
	{
		for (const auto& op : Operations)
			if (op->Body == body) return op;
		return nullptr;
	}

	DVec3 GasHarvest::PlatformHelio(const cs::Ref<HarvestOperation>& op, int platform, double jd) const
	{
		return op->Platforms[platform]->Structure->HelioAt(world->Ephemeris, jd);
	}

	DVec3 GasHarvest::TankerHelio(const cs::Ref<HarvestOperation>& op, const cs::Ref<HarvestTanker>& t, double jd) const
	{
		int platform = cs::Math::Clamp(t->Platform, 0, op->Platforms.Count() - 1);
		auto home = PlatformHelio(op, platform, jd);
		if (t->Lost || t->Leg == TankerLeg::Waiting || t->To == nullptr)
		{
			// Alongside its platform, a little astern of it.
			auto st = op->Platforms[platform]->Structure;
			auto along = st->Orbit->TangentAt(jd);
			along = along.LengthSquared() > 1e-12 ? along.Normalized() : DVec3::UnitX;
			return home - along * (st->LengthKm * 3 + 1.5 * (t->Seed % 3));
		}
		auto port = world->Ephemeris->HelioAt(t->To->Body, jd);
		double f = t->Progress(jd);
		f = f * f * (3 - 2 * f); // eases away and in
		if (t->Leg == TankerLeg::Returning) f = 1 - f;
		return home + (port - home) * f;
	}

	bool GasHarvest::Nearest(DVec3 helioKm, double jd, cs::Ref<HarvestOperation>& op, int& platform, double& km) const
	{
		op = nullptr;
		platform = -1;
		km = cs::Double::PositiveInfinity;
		for (const auto& o : Operations)
			for (int i = 0; i < o->Platforms.Count(); i++)
			{
				double d = (PlatformHelio(o, i, jd) - helioKm).Length();
				if (d < km) { km = d; op = o; platform = i; }
			}
		return op != nullptr;
	}

	cs::Ref<HarvestContract> GasHarvest::OpenAt(const cs::Ref<HarvestOperation>& op, int platform) const
	{
		cs::Ref<HarvestContract> best;
		for (const auto& c : Contracts)
			if (c->State == ContractState::Open && c->Operation == op && (platform < 0 || c->Platform == platform) && (best == nullptr || c->Pay > best->Pay)) best = c;
		return best;
	}

	cs::Ref<HarvestContract> GasHarvest::ActiveFor(const cs::Ref<Ship>& ship) const
	{
		if (ship == nullptr) return nullptr;
		for (const auto& c : Contracts)
			if (c->State == ContractState::Taken && c->Taker == ship) return c;
		return nullptr;
	}

	double GasHarvest::GoodwillOf(const cs::Ref<Faction>& patron, const cs::Ref<Faction>& guard) const
	{
		if (patron == nullptr || guard == nullptr) return 0;
		double v = 0;
		return Goodwill.TryGetValue(cs::Cat(patron->Name, "|", guard->Name), v) ? v : 0;
	}

	// ---------------------------------------------------------------- protection contracts

	void GasHarvest::PostContracts(double jd)
	{
		for (const auto& c : Contracts)
			if (c->State == ContractState::Open && jd >= c->ExpiresJd)
			{
				c->State = ContractState::Expired;
				c->ClosedJd = jd;
			}
		double base = PiratePressure();
		for (const auto& op : Operations)
		{
			// Two draws a works a day whatever is open, so the stream stays in step.
			double roll = rng.NextDouble();
			int pick = rng.Next(0, cs::Math::Max(1, op->Platforms.Count()));
			if (op->Owner == nullptr || op->Owner->IsPirate || op->Port == nullptr || op->Platforms.Count() == 0) continue;
			int open = 0;
			for (const auto& c : Contracts)
				if (c->State == ContractState::Open && c->Operation == op) open++;
			if (open >= OpenPerWorks || roll >= 0.2 + 0.5 * cs::Math::Min(1.0, Pressure(op, base))) continue;
			Offer(op, pick, jd);
		}
	}

	cs::Ref<HarvestContract> GasHarvest::Offer(const cs::Ref<HarvestOperation>& op, int platform, double jd)
	{
		if (op == nullptr || op->Platforms.Count() == 0) return nullptr;
		platform = cs::Math::Clamp(platform, 0, op->Platforms.Count() - 1);
		auto c = cs::New<HarvestContract>();
		c->Id = nextContractId++;
		c->Operation = op;
		c->Platform = platform;
		c->Patron = op->Owner;
		c->Orbits = 3 + rng.Next(0, 6);
		c->OrbitDays = op->OrbitDays(platform);
		double danger = cs::Math::Min(1.0, Pressure(op, PiratePressure()));
		c->Pay = cs::Math::Round(PayPerOrbit * c->Orbits * (1 + danger) / 1000) * 1000;
		c->PostedJd = jd;
		c->ExpiresJd = jd + OfferDays;
		Contracts.Add(c);
		return c;
	}

	bool GasHarvest::Take(const cs::Ref<HarvestContract>& c, const cs::Ref<Ship>& ship, DVec3 helioKm, double jd, cs::String& why, double firstWaveS)
	{
		why = cs::String();
		if (c == nullptr || c->State != ContractState::Open) { why = "the offer is gone"; return false; }
		if (ship == nullptr || ship->Destroyed) { why = "no hull to guard with"; return false; }
		if (ship->Combat == nullptr || !ship->Combat->Armed()) { why = "an unarmed hull cannot guard a platform"; return false; }
		if (ActiveFor(ship) != nullptr) { why = "this hull is already on a protection contract"; return false; }
		auto op = c->Operation;
		auto diplomacy = world->Diplomacy();
		if (c->Patron != nullptr && ship->Faction != nullptr && diplomacy != nullptr && diplomacy->Hostile(c->Patron, ship->Faction))
		{
			why = cs::Cat(c->Patron->Name, " is at war with ", ship->Faction->Name);
			return false;
		}
		if (ship->Faction != nullptr && ship->Faction->IsPirate) { why = "nobody hires pirates to guard their tanks"; return false; }
		double km = (helioKm - world->Ephemeris->HelioAt(op->Body, jd)).Length();
		if (km > GuardKm(op->Body)) { why = cs::Cat("come to ", (*world->Catalog())[op->Body]->Name, " first"); return false; }

		c->State = ContractState::Taken;
		c->Taker = ship;
		c->TakerFaction = ship->Faction;
		auto chars = Characters::Of(world);
		auto pilot = chars != nullptr ? chars->Find(ship) : nullptr;
		c->PilotName = pilot != nullptr ? pilot->Name : cs::String();
		c->StartJd = jd;
		c->EndJd = jd + c->Orbits * c->OrbitDays;
		c->GuardSeenJd = jd;
		c->GuardKmFromGiant = km;
		c->PlatformHp = 1;
		// The waves: one, and one more for every two orbits (at most four), spread over the contract.
		int count = cs::Math::Clamp(1 + c->Orbits / 2, 1, 4);
		double span = c->EndJd - c->StartJd;
		for (int k = 0; k < count; k++)
		{
			auto w = cs::New<HarvestWave>();
			w->Jd = c->StartJd + span * ((k + 0.25 + 0.4 * rng.NextDouble()) / count);
			if (k == 0 && firstWaveS > 0) w->Jd = jd + firstWaveS / Astro::SecondsPerDay;
			cs::Ref<Faction> by;
			w->Kind = DrawThreat(op, by);
			if (w->Kind == HarvestThreat::Rival)
			{
				// A power at war with the owner comes only if it is at war with the guard too; else the pirates do.
				by = RivalOf(op->Owner, ship->Faction);
				if (by == nullptr) { w->Kind = HarvestThreat::Pirates; by = world->Pirates; }
			}
			w->By = by;
			w->Size = cs::Math::Clamp(1 + (rng.NextDouble() < 0.3 + 0.12 * k ? 1 : 0) + (k == count - 1 && c->Orbits >= 6 ? 1 : 0), 1, 3);
			c->Waves.Add(w);
		}
		Log(jd, cs::Cat("The ", ship->Name, " takes the contract to guard the ", op->Platforms[c->Platform]->Name, " for ", c->Orbits, " orbits (",
			Cr(c->Pay), " cr from ", c->Patron != nullptr ? c->Patron->Name : cs::String("the works"), ")"), op->Body, false);
		return true;
	}

	void GasHarvest::Abandon(const cs::Ref<HarvestContract>& c, double jd)
	{
		if (c == nullptr || c->State != ContractState::Taken) return;
		Close(c, ContractState::Abandoned, jd, "given up");
	}

	void GasHarvest::Guard(const cs::Ref<Ship>& ship, DVec3 helioKm, double jd)
	{
		auto c = ActiveFor(ship);
		if (c == nullptr) return;
		c->GuardSeenJd = jd;
		c->GuardKmFromGiant = (helioKm - world->Ephemeris->HelioAt(c->Operation->Body, jd)).Length();
	}

	void GasHarvest::Spawned(const cs::Ref<HarvestWave>& w, const cs::List<cs::Ref<Ship>>& ships, int globule, double jd)
	{
		if (w == nullptr) return;
		w->Spawned = true;
		w->SpawnedJd = jd;
		w->Ships = ships;
		w->Globule = globule;
	}

	cs::Ref<HarvestWave> GasHarvest::DueWave(const cs::Ref<HarvestContract>& c, double jd) const
	{
		if (c == nullptr || c->State != ContractState::Taken || c->LiveWave() != nullptr) return nullptr;
		auto w = c->NextWave();
		return w != nullptr && jd >= w->Jd ? w : nullptr;
	}

	void GasHarvest::RunContracts(double jd, double dt)
	{
		double dtS = dt * Astro::SecondsPerDay;
		for (const auto& c : Contracts)
		{
			if (c->State != ContractState::Taken) continue;
			auto op = c->Operation;
			auto taker = c->Taker;
			if (taker == nullptr || taker->Destroyed) { Close(c, ContractState::Failed, jd, "the guard was lost"); continue; }
			if (taker->Faction != c->TakerFaction) { Close(c, ContractState::Failed, jd, "the guard's hull changed hands"); continue; }
			bool onStation = !cs::Double::IsNaN(c->GuardSeenJd) && (jd - c->GuardSeenJd) * Astro::SecondsPerDay < 120 && c->GuardKmFromGiant <= GuardKm(op->Body);
			for (const auto& w : c->Waves)
			{
				if (w->Resolved) continue;
				if (!w->Spawned)
				{
					if (jd < w->Jd) break; // the waves come in order
					if (!onStation)
					{
						// Nobody there when they came: they have the platform to themselves.
						w->Resolved = true;
						c->PlatformHp = 0;
						Knock(op, c->Platform, w->Kind, w->By, jd, cs::Cat(" while its guard, the ", taker->Name, ", was away"));
						Close(c, ContractState::Failed, jd, cs::Cat(ThreatName(w->Kind, w->By), " found the platform unguarded"));
						break;
					}
					if ((jd - w->Jd) * Astro::SecondsPerDay > WaveGraceS)
					{
						// The game never put them on the scene (no helm, a fight elsewhere): they hit the platform and go.
						w->Resolved = true;
						c->PlatformHp -= 0.25 * w->Size;
					}
					break;
				}
				// Live: the attackers still in it wear the platform down.
				int alive = 0;
				for (const auto& s : w->Ships)
					if (s != nullptr && !s->Destroyed && s->Faction == w->By && (s->Battle != nullptr || (jd - w->SpawnedJd) * Astro::SecondsPerDay < 120)) alive++;
				if (w->Globule >= 0 && world->BioMass != nullptr)
					for (const auto& g : world->BioMass->Globules)
						if (g->Id == w->Globule && g->Alive()) alive++;
				int total = w->Ships.Count() + (w->Globule >= 0 ? 1 : 0);
				if (alive == 0)
				{
					w->Resolved = true;
					w->Beaten = true;
					Log(jd, cs::Cat("The ", taker->Name, " beat off ", cs::Strings::ToLowerInvariant(ThreatName(w->Kind, w->By)), " at the ", op->Platforms[c->Platform]->Name), op->Body, false);
					continue;
				}
				c->PlatformHp -= WaveWearPerS * dtS * (0.4 + 0.2 * w->Size) * alive / cs::Math::Max(1, total);
				break;
			}
			if (c->State != ContractState::Taken) continue;
			if (c->PlatformHp <= 0)
			{
				auto last = c->LiveWave();
				auto kind = last != nullptr ? last->Kind : op->LastThreat;
				auto by = last != nullptr ? last->By : nullptr;
				if (last != nullptr) last->Resolved = true;
				Knock(op, c->Platform, kind, by, jd, cs::Cat(" under the ", taker->Name, "'s guard"));
				Close(c, ContractState::Failed, jd, "the platform was knocked out");
				continue;
			}
			if (jd >= c->EndJd && c->LiveWave() == nullptr)
			{
				// Any wave still to come was due inside the orbits; past them it no longer is.
				for (const auto& w : c->Waves)
					if (!w->Spawned) w->Resolved = true;
				Close(c, ContractState::Completed, jd, cs::Cat(c->Orbits, " orbits guarded"));
			}
		}
	}

	void GasHarvest::Close(const cs::Ref<HarvestContract>& c, ContractState state, double jd, const cs::String& why)
	{
		c->State = state;
		c->ClosedJd = jd;
		c->Outcome = why;
		auto op = c->Operation;
		auto ship = c->Taker;
		auto platform = op->Platforms[c->Platform]->Name;
		auto giant = (*world->Catalog())[op->Body]->Name;
		auto chars = Characters::Of(world);
		auto pilot = chars != nullptr && ship != nullptr ? chars->Find(ship) : nullptr;
		auto rec = CaptainRecord(world, ship, c->PilotName);
		cs::String key = c->Patron != nullptr && c->TakerFaction != nullptr ? cs::Cat(c->Patron->Name, "|", c->TakerFaction->Name) : cs::String();
		double goodwill = 0;
		if (!key.empty()) Goodwill.TryGetValue(key, goodwill);
		if (state == ContractState::Completed)
		{
			// The pay to the hull that did it (its pilot's purse); the standing to its captain's own record and its colours.
			if (c->Patron != nullptr) c->Patron->Treasury -= c->Pay;
			if (ship != nullptr) ship->Credits += c->Pay;
			Standing(rec, c->Patron, CompleteStanding + 0.5 * c->Orbits);
			if (!key.empty())
			{
				Goodwill[key] = goodwill + 1 + 0.25 * c->Orbits;
				if (chars != nullptr)
				{
					double g = 0;
					if (chars->Grievance.TryGetValue(key, g)) chars->Grievance[key] = cs::Math::Max(0.0, g - c->Pay);
				}
			}
			if (chars != nullptr && pilot != nullptr)
				chars->Act(pilot, GuardedBeat, jd, 1 + 0.3 * c->Orbits,
					{ "victim", platform, "place", giant, "orbits", cs::Str(c->Orbits), "pay", Cr(c->Pay), "act", cs::Cat("guarded the ", platform) });
			Log(jd, cs::Cat("Contract done: the ", ship != nullptr ? ship->Name : cs::String("guard"), " guarded the ", platform, " for ", c->Orbits, " orbits; ",
				c->Patron != nullptr ? c->Patron->Name : cs::String("the works"), " pays ", Cr(c->Pay), " cr"), op->Body, false);
			if (world->Law != nullptr && ship != nullptr && ship == world->PilotedShip)
				world->Law->Notice(cs::Cat("Contract complete: ", c->Title(), ": ", Cr(c->Pay), " cr to the ", ship->Name));
			return;
		}
		Standing(rec, c->Patron, -FailStanding);
		if (!key.empty()) Goodwill[key] = goodwill - 2;
		cs::String what = state == ContractState::Abandoned ? "abandoned" : "failed";
		Log(jd, cs::Cat("Contract ", what, ": the ", ship != nullptr ? ship->Name : cs::String("guard"), " on the ", platform, " (", why, ")"), op->Body, false);
		if (world->Law != nullptr && ship != nullptr && ship == world->PilotedShip)
			world->Law->Notice(cs::Cat("Contract ", what, ": ", c->Title(), " (", why, "). ", c->Patron != nullptr ? c->Patron->Name : cs::String("The works"), " will remember it"));
	}

	void GasHarvest::Trim()
	{
		int closed = 0;
		for (const auto& c : Contracts)
			if (c->State != ContractState::Open && c->State != ContractState::Taken) closed++;
		for (int i = 0; i < Contracts.Count() && closed > 60;)
		{
			auto s = Contracts[i]->State;
			if (s != ContractState::Open && s != ContractState::Taken) { Contracts.RemoveAt(i); closed--; }
			else i++;
		}
	}

	// ---------------------------------------------------------------- raiding a platform

	double GasHarvest::Raid(const cs::Ref<HarvestOperation>& op, int platform, const cs::Ref<Ship>& ship, double jd, cs::String& said)
	{
		said = cs::String();
		if (op == nullptr || platform < 0 || platform >= op->Platforms.Count() || ship == nullptr) return 0;
		auto p = op->Platforms[platform];
		if (p->TankTonnes() < 1) { said = cs::Cat("The ", p->Name, "'s tanks are dry"); return 0; }
		auto hold = ship->PersonalHold();
		if (hold == nullptr) { said = "No hold to siphon into"; return 0; }
		double took = 0, fuel = 0;
		for (int g : Gases)
		{
			auto good = static_cast<Good>(g);
			double crate = CargoFootprint::Of(good).Tonnes;
			while (p->Tank[g] >= crate * 0.5)
			{
				auto item = hold->AddFirstFit(good);
				if (item == nullptr) break;
				item->Stolen = true;
				double t = cs::Math::Min(crate, p->Tank[g]);
				p->Tank[g] -= t;
				op->Plundered[g] += t;
				took += t;
				if (g == He3) fuel += t;
			}
		}
		if (took <= 0) { said = "No room in the hold for the gas"; return 0; }
		op->FuelLost += fuel;
		op->Raids++;
		p->Raids++;
		op->LastRaidJd = jd;
		op->LastThreat = HarvestThreat::Pirates;
		p->Condition = cs::Math::Max(0.05, p->Condition - 0.15);
		p->DownUntilJd = jd + 2;
		// Piracy on the pilot's own record, seen by the platform's crew; the story and the grudge are theirs and their colours'.
		auto chars = Characters::Of(world);
		auto pilot = chars != nullptr ? chars->Find(ship) : nullptr;
		auto rec = CaptainRecord(world, ship, pilot != nullptr ? pilot->Name : cs::String());
		if (world->Law != nullptr && rec != nullptr && op->Owner != nullptr && !op->Owner->IsPirate)
			world->Law->Commit(rec, CrimeKind::Piracy, world->Law->JurisdictionAt(op->Body), op->Owner, op->Body, jd, p->Name, "raided a gas skimmer's tanks");
		if (chars != nullptr)
		{
			if (pilot != nullptr) chars->Act(pilot, LifeEventKind::Robbed, jd, 1.5, { "victim", p->Name, "good", "helium-3", "tonnes", T(took) });
			chars->AddGrievance(op->Owner, ship->Faction, took * GoodInfo::Of(Good::HeliumThree)->BasePrice * 0.2);
		}
		Log(jd, cs::Cat("The ", ship->Name, " raided the ", p->Name, ": ", T(took), " t of gas siphoned off (", T(fuel), " t of helium-3); ",
			op->Owner != nullptr ? op->Owner->Name : cs::String("its owner"), "'s fuel supply is cut"), op->Body, true);
		said = cs::Cat("Siphoned ", T(took), " t of gas from the ", p->Name, " (", T(fuel), " t helium-3): stolen goods, and piracy on your pilot's record");
		return took;
	}

	void GasHarvest::Log(double jd, const cs::String& text, int body, bool loss)
	{
		world->Log(jd, text, loss ? WorldEventKind::Loss : WorldEventKind::Economy, body);
	}

	// ---------------------------------------------------------------- persistence (docs/PERSISTENCE.md)

	SG_ENUM(HarvestThreat, "SpaceGod.Core.HarvestThreat")
	SG_ENUM(TankerLeg, "SpaceGod.Core.TankerLeg")

	SG_SERIALIZE(HarvestPlatform, "SpaceGod.Core.HarvestPlatform",
		SG_F(Name), SG_F(Seed), SG_F(Structure), SG_F(Condition), SG_F(DownUntilJd), SG_ARRAY(Tank), SG_F(Raids))

	SG_SERIALIZE(HarvestTanker, "SpaceGod.Core.HarvestTanker",
		SG_F(Name), SG_F(Seed), SG_F(Leg), SG_F(Platform), SG_F(To), SG_F(DepartJd), SG_F(ArriveJd), SG_ARRAY(Cargo), SG_F(Trips),
		SG_F(Lost), SG_F(ReplacedJd))

	SG_SERIALIZE(HarvestOperation, "SpaceGod.Core.HarvestOperation",
		SG_F(Name), SG_F(Body), SG_F(Owner), SG_F(Port), SG_F(Platforms), SG_F(Tankers), SG_ARRAY(Rate), SG_F(Outermost),
		SG_ARRAY(Harvested), SG_ARRAY(Delivered), SG_ARRAY(Plundered), SG_F(Raids), SG_F(TankersLost), SG_F(Deliveries), SG_F(FuelLost),
		SG_F(LastRaidJd), SG_F(LastThreat))

	SG_SERIALIZE(HarvestWave, "SpaceGod.Core.HarvestWave",
		SG_F(Jd), SG_F(Kind), SG_F(By), SG_F(Size), SG_F(Spawned), SG_F(Resolved), SG_F(Beaten), SG_F(SpawnedJd), SG_F(Ships), SG_F(Globule))

	SG_SERIALIZE(HarvestContract, "SpaceGod.Core.HarvestContract",
		SG_F(Id), SG_F(State), SG_F(Operation), SG_F(Platform), SG_F(Patron), SG_F(Orbits), SG_F(OrbitDays), SG_F(Pay), SG_F(PostedJd),
		SG_F(ExpiresJd), SG_F(StartJd), SG_F(EndJd), SG_F(ClosedJd), SG_F(Taker), SG_F(TakerFaction), SG_F(PilotName), SG_F(Waves),
		SG_F(PlatformHp), SG_F(GuardSeenJd), SG_F(GuardKmFromGiant), SG_F(Outcome))

	SG_SERIALIZE(GasHarvest, "SpaceGod.Core.GasHarvest",
		SG_F(Operations), SG_F(Contracts), SG_F(Goodwill), SG_F(world), SG_F(rng), SG_F(nextDayJd), SG_F(lastJd), SG_F(nextContractId),
		SG_F(tankerSerial))
}
