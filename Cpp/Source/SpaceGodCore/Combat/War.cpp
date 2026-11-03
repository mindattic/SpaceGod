// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/War.cs (see docs/MIGRATION.md).

#include "Combat/War.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Lagrange.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/BreacherBays.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Corvettes.h"
#include "Combat/Crews.h"
#include "Combat/Diplomacy.h"
#include "Combat/DriftingRounds.h"
#include "Combat/Facilities.h"
#include "Combat/Fighters.h"
#include "Combat/FleetCoordination.h"
#include "Combat/FleetPatterns.h"
#include "Combat/GateBreakers.h"
#include "Combat/GatePickets.h"
#include "Combat/LaneInterdictors.h"
#include "Combat/Morale.h"
#include "Combat/Outfitting.h"
#include "Combat/PatrolWings.h"
#include "Combat/PrizeSettlement.h"
#include "Combat/Pursuit.h"
#include "Combat/RaidDoctrine.h"
#include "Combat/Salvage.h"
#include "Combat/StrandedWings.h"
#include "Combat/Wreck.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Sensors/Sensors.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"
#include "Systems/LaneUpkeep.h"
#include "Systems/Polities.h"
#include "Systems/StrategicCommand.h"

namespace SpaceGod::Core
{
	namespace
	{
		// .NET double.CompareTo: NaN sorts below everything, equal to itself (Cs.h has no CompareTo).
		int CompareTo(double a, double b)
		{
			if (a < b) return -1;
			if (a > b) return 1;
			if (a == b) return 0;
			if (cs::Double::IsNaN(a)) return cs::Double::IsNaN(b) ? 0 : -1;
			return 1;
		}
	}

	const char* ToString(OrderKind v)
	{
		switch (v)
		{
		case OrderKind::Patrol: return "Patrol";
		case OrderKind::Ambush: return "Ambush";
		case OrderKind::Attack: return "Attack";
		case OrderKind::Defend: return "Defend";
		case OrderKind::Siege: return "Siege";
		case OrderKind::Repair: return "Repair";
		case OrderKind::Raid: return "Raid";
		case OrderKind::Hunt: return "Hunt";
		case OrderKind::Assault: return "Assault";
		case OrderKind::GateWatch: return "GateWatch";
		case OrderKind::HoldGate: return "HoldGate";
		case OrderKind::Blockade: return "Blockade";
		case OrderKind::Escort: return "Escort";
		case OrderKind::Explore: return "Explore";
		}
		return "";
	}

	cs::List<cs::String> War::TakePilotNotices()
	{
		auto list = cs::List<cs::String>(pilotNotices);
		pilotNotices.Clear();
		return list;
	}

	War::War(cs::Ref<World> world, double jd, int seed)
		: world(world), Wrecks(cs::New<SpaceGod::Core::Wrecks>()), rng(seed)
	{
		lastJd = jd;
		nextDailyJd = jd + 1;
		warStartJd = jd;
		nextPodSweepJd = jd;
		Crews = cs::New<SpaceGod::Core::Crews>(cs::WrapAdd(cs::WrapMul(seed, 31), 7));
		Crews->HavenOf = [this](cs::Ref<Ship> s) { return HomeHaven(s); };
		Crews->RaidersTaken = [this](cs::Ref<EscapePod> pod, cs::Ref<Faction> by, double when)
		{
			if (by->IsMilitary && pod->Haven != nullptr) Discover(pod->Haven, by, when, cs::Cat("raiders picked up from the ", pod->Name, "'s escape pods give it away"));
		};
		Crews->Launched += [this](cs::Ref<EscapePod> pod)
		{
			this->world->Log(pod->LaunchedJd,
				cs::Cat("The ", pod->Name, " launches ", pod->Pods, " escape pod", (pod->Pods == 1 ? "" : "s"), ": ", cs::Fmt(pod->People, "0"), " ", pod->Owners->Name, " crew adrift and broadcasting"), WorldEventKind::Loss, PodBody(pod));
		};
		Crews->Resolved += [this](cs::Ref<EscapePod> pod, cs::Ref<Ship> ship)
		{
			cs::String by = ship != nullptr ? cs::Cat("the ", ship->Name) : pod->TakenBy != nullptr ? cs::Cat("the ", pod->TakenBy->Name) : cs::String("nobody");
			cs::String people = cs::Fmt(pod->People, "0");
			cs::String text;
			if (pod->Breacher)
			{
				switch (pod->State)
				{
				// A drifting Breacher round (U.SPC-T519).
				case PodState::Rescued: text = cs::Cat(Cap(by), " tows in the ", pod->Name, ": ", people, " ", pod->Owners->Name, " marines home safe"); break;
				case PodState::Hostage: text = cs::Cat(Cap(by), " salvages the ", pod->Name, " and holds its ", people, " marines for ransom or sale"); break;
				case PodState::Prisoner: text = cs::Cat(Cap(by), " tows in the ", pod->Name, " and takes its ", people, " marines prisoner"); break;
				default: text = cs::Cat("The ", pod->Name, " falls silent: its air ran out with ", people, " marines inside"); break;
				}
			}
			else
			{
				switch (pod->State)
				{
				case PodState::Rescued: text = cs::Cat(Cap(by), " picks up the ", pod->Name, "'s escape pods: ", people, " crew safe"); break;
				case PodState::Hostage: text = cs::Cat(Cap(by), " hauls in the ", pod->Name, "'s escape pods and holds ", people, " crew for ransom"); break;
				case PodState::Prisoner: text = cs::Cat(Cap(by), " picks up the ", pod->Name, "'s escape pods and takes ", people, " prisoners"); break;
				default: text = cs::Cat("The ", pod->Name, "'s escape pods fall silent: their air ran out with ", people, " aboard"); break;
				}
			}
			this->world->Log(pod->ResolvedJd, text, pod->State == PodState::Lost ? WorldEventKind::Loss : WorldEventKind::Info, PodBody(pod));
		};
		Crews->CaptivesReleased += [this](cs::Ref<Captives> c, cs::String how, double when)
		{
			this->world->Log(when, how, WorldEventKind::Info, c->Haven != nullptr ? c->Haven->Body : -1);
		};
	}

	cs::String War::Cap(const cs::String& s)
	{
		return s.size() == 0 ? s : cs::Cat(cs::Strings::ToUpperInvariant(cs::Strings::Substring(s, 0, 1)), cs::Strings::Substring(s, 1));
	}

	int War::PodBody(const cs::Ref<EscapePod>& pod) { return pod->Site == nullptr ? pod->Body : -1; }

	cs::Ref<SpaceGod::Core::Fleet> War::Fleet() const { return world->Fleet; }
	cs::Ref<SpaceGod::Core::Economy> War::Economy() const { return world->Economy; }
	cs::Ref<SpaceGod::Core::Diplomacy> War::Diplomacy() const { return world->Diplomacy(); }
	cs::Ref<SpaceGod::Core::BodyCatalog> War::Catalog() const { return world->Catalog(); }

	double War::Danger(int body) const
	{
		double d = 0;
		return danger.TryGetValue(body, d) ? d : 0;
	}

	int War::Losses(const cs::Ref<Faction>& f) const
	{
		int n = 0;
		return losses.TryGetValue(f, n) ? n : 0;
	}

	cs::Ref<Battle> War::BattleAt(int body) const
	{
		for (const auto& b : Battles)
			if (b->Body == body && b->Site == nullptr) return b;
		return nullptr;
	}

	cs::Ref<GroupOrder> War::OrderOf(const cs::Ref<Ship>& leader) const
	{
		cs::Ref<GroupOrder> o;
		return Orders.TryGetValue(leader, o) ? o : nullptr;
	}

	// ---------------------------------------------------------------- per frame

	void War::Update(double jd)
	{
		double seconds = (jd - lastJd) * Astro::SecondsPerDay;
		lastJd = jd;
		for (int i = Battles.Count() - 1; i >= 0; i--)
		{
			auto b = Battles[i];
			if (b->Fresh)
			{
				b->Fresh = false;
				continue;
			}
			b->Advance(seconds, world->Coarse());
			// The player who has left the fight (flown out of it, or let go for the cargo) is free of it
			// now, not when the rest have finished: free to cruise, take the gate or engage elsewhere.
			for (const auto& u : b->Units)
				if (u->Fled && u->PlayerControlled() && u->Ship != nullptr && u->Ship->Battle == b) u->Ship->Battle = nullptr;
			if (b->Over) Resolve(b, jd);
		}
		Interdictions(jd);
		GateAmbushes(jd);
		if (jd >= nextPodSweepJd)
		{
			double days = cs::Math::Min(1.0, jd - nextPodSweepJd + PodSweepDays);
			Crews->Sweep(Fleet()->Ships(), Diplomacy(), world->PilotedShip, jd, days);
			Crews->Expire(jd);
			nextPodSweepJd = jd + PodSweepDays;
		}
		while (jd >= nextDailyJd)
		{
			Daily(nextDailyJd);
			nextDailyJd += 1;
		}
	}

	// ---------------------------------------------------------------- lane ambushes

	void War::Interdictions(double jd)
	{
		auto lanes = Fleet()->Lanes;
		if (lanes == nullptr) return;
		auto seen = cs::Dictionary<cs::Ref<Ship>, LanePass>();
		for (const auto& v : Fleet()->Ships())
		{
			// The player's ship is never a lurker, but riding a lane at the helm (U.SPC-T41) it is a rider like any
			// other, and a pulse throws it out (U.SPC-T42): a piloted hull is in the lane phase only while it rides.
			if (v->Destroyed || v->IsEscort() || v->Phase != ShipPhase::Lane || v->Battle != nullptr) continue;
			double p = Core::Fleet::LaneProgress(v, jd);
			seen[v] = LanePass{ v->LaneLeg.Lane, v->LaneLeg.From == v->LaneLeg.Lane->A ? p : 1 - p };
		}
		for (const auto& lurker : Fleet()->Ships())
		{
			if (!Lurking(lurker)) continue;
			double ready = 0;
			if (pulseReady.TryGetValue(lurker, ready) && jd < ready) continue;
			auto lane = lurker->LaneLeg.Lane;
			double station = lurker->LaneLeg.From == lane->A ? lurker->LaneHold : 1 - lurker->LaneHold;
			double window = PulseRadiusKm / cs::Math::Max(lurker->LaneLengthKm, 1.0);
			auto victims = cs::List<cs::Ref<Ship>>();
			for (auto [v, pass] : seen)
			{
				// Already caught by another lurker's pulse this pass: it is fighting there.
				if (v->LaneLeg.Lane != lane || !Targets(lurker, v) || v->Derelict || v->Battle != nullptr) continue;
				double now = pass.at;
				// Last seen on another lane (it rode one out and another in since the last look): no pass of this one yet.
				LanePass b;
				double before = laneSeen.TryGetValue(v, b) && b.lane == lane ? b.at : now;
				if ((before - station) * (now - station) <= 0 || cs::Math::Abs(now - station) < window) victims.Add(v);
			}
			if (victims.Count() > 0 && WorthPulsing(lurker, victims)) Interdict(lurker, station, victims, jd);
		}
		laneSeen.Clear();
		for (auto [k, v] : seen) laneSeen[k] = v;
	}

	bool War::WorthPulsing(const cs::Ref<Ship>& lurker, const cs::List<cs::Ref<Ship>>& victims)
	{
		// A navy's lane picket (U.SPC-T479) pulls out every raider or enemy it can take on: the guns riding with them no
		// more than its own group's, with a margin for the stun the pulse leaves them under.
		if (!lurker->Faction->IsPirate)
		{
			double hostile = 0;
			for (const auto& v : victims) hostile += GroupStrength(v);
			return hostile <= GroupStrength(lurker) * PicketNerve;
		}
		auto marks = cs::List<cs::Ref<Ship>>();
		double guns = 0;
		for (const auto& v : victims)
		{
			if (v->Spec->IsMilitary || v->Faction->IsPirate) guns += GroupStrength(v);
			else
			{
				marks.Add(v);
				// Its escorts ride the lane with it.
				guns += GroupStrength(v) - v->Combat->Strength();
			}
		}
		if (marks.Count() == 0) return true;
		double band = GroupStrength(lurker);
		return BestMark(marks, guns, band) != nullptr || (guns > 0 && guns <= band * RaidDoctrine::BaseNerve);
	}

	bool War::Lurking(const cs::Ref<Ship>& lurker)
	{
		return !lurker->Destroyed && !lurker->IsEscort() && lurker->Ambushing() && lurker->Battle == nullptr &&
			lurker != world->PilotedShip && (world->Pursuit == nullptr || world->Pursuit->Of(lurker) == nullptr) &&
			LaneInterdictors::Fielded(lurker, Fleet()->Ships());
	}

	bool War::Targets(const cs::Ref<Ship>& lurker, const cs::Ref<Ship>& rider)
	{
		if (lurker == nullptr || rider == nullptr || rider->Faction == nullptr || lurker->Faction == nullptr) return false;
		if (lurker->Faction->IsPirate) return !rider->Faction->IsPirate;
		if (rider->Faction == lurker->Faction) return false;
		if (Diplomacy() != nullptr && Diplomacy()->Hostile(lurker->Faction, rider->Faction)) return true;
		return rider == world->PilotedShip && world->Law != nullptr && world->Law->Player->WantedBy(lurker->Faction);
	}

	void War::Interdict(const cs::Ref<Ship>& lurker, double station, cs::List<cs::Ref<Ship>>& victims, double jd)
	{
		auto lane = lurker->LaneLeg.Lane;
		bool picket = !lurker->Faction->IsPirate;
		auto emitter = LaneInterdictors::EmitterOf(lurker, Fleet()->Ships());
		if (!emitter) emitter = lurker;
		pulseReady[lurker] = jd + LaneInterdictors::RechargeDays()->Value;
		// A pilot surging through may be past before the shell closes: the pulse goes off, and misses.
		auto pilot = world->PilotedShip;
		if (pilot != nullptr && victims.Contains(pilot) && pilot->LaneSurged && rng.NextDouble() < SurgeSlipChance)
		{
			victims.Remove(pilot);
			pilotNotices.Add(cs::Cat("Through! The ", emitter->Name, "'s pulse goes off astern as you surge clear of the ", lane->Name, " lane's interdiction field"));
			world->Log(jd, cs::Cat("The ", pilot->Name, " surges through an interdiction pulse on the ", lane->Name, " lane"), WorldEventKind::Battle, lane->A);
			if (victims.Count() == 0) return;
		}
		for (const auto& v : victims)
		{
			double at = v->LaneLeg.From == lane->A ? station : 1 - station;
			auto side = lurker->LaneHoldSide * (0.8 + rng.NextDouble() * 0.4);
			Fleet()->PullOut(v, jd, at, side);
		}
		auto helio = Fleet()->LanePoint(lurker, jd, lurker->LaneHold) + lurker->LaneHoldSide;
		auto b = cs::New<Battle>(nextBattleId++, 0, cs::Cat((picket ? "Interdiction on the " : "Ambush on the "), lane->Name, " lane"), helio, jd, rng.Next());
		// A navy's picket stops raiders and enemies on the lane in the law's name, as a patrol interdicting a
		// wanted ship does (U.SPC-T479).
		b->EnforcingSide = picket ? 0 : -1;
		{
			auto site = cs::New<LaneSite>();
			site->Hop = lurker->LaneLeg; site->FromOffset = lurker->LaneFromOffset; site->ToOffset = lurker->LaneToOffset; site->Progress = lurker->LaneHold; site->Side = lurker->LaneHoldSide;
			b->Site = site;
		}
		Hook(b);
		auto approach = DVec3::Cross(DVec3::UnitZ, lurker->LaneHoldSide);
		b->ApproachAxis = approach.LengthSquared() > 0 ? approach.Normalized() : DVec3::UnitX;
		Join(b, lurker, 0, false);
		for (const auto& v : victims) Join(b, v, 1, false);
		// The pilot's hull, caught riding at the helm, is flown in the fight that follows: placed with the others the
		// pulse caught, but its pose is the helm's from here (U.SPC-T42).
		if (pilot != nullptr && victims.Contains(pilot) && pilot->Battle == nullptr)
		{
			// (C# evaluates the DVec3 constructor's arguments left to right; C++ leaves the order unspecified, so draw them first.)
			double rx = rng.NextDouble() - 0.5;
			double ry = rng.NextDouble() - 0.5;
			double rz = (rng.NextDouble() - 0.5) * 0.4;
			auto spot = Spot(b, 1) + DVec3(rx, ry, rz) * 6;
			auto unit = b->AddShip(pilot, 1, spot, -spot.Normalized());
			unit->SetPlayerControlled(true);
		}
		// The pulse comes from the group's interdictor (U.SPC-T479), wherever it flies in the formation.
		cs::Ref<BattleUnit> source = b->UnitOf(emitter);
		if (!source) source = b->UnitOf(lurker);
		// A victim whose scanners made the raiders out lying dark beside the lane as it closed (by class at least,
		// at the lurk's stand-off from the lane line) was braced for the pulse: drives and shields ride it out.
		auto warned = cs::HashSet<cs::Ref<Ship>>();
		for (const auto& v : victims)
			if (Forewarned(v, lurker, jd)) warned.Add(v->Controller());
		for (const auto& u : b->Units)
		{
			if (u->Side != 1) continue;
			// A fighter rides with the carrier that launched it: a braced carrier's wings are braced too.
			auto owner = u->Ship;
			if (!owner) owner = (u->Fighter != nullptr && u->Fighter->Carrier != nullptr) ? u->Fighter->Carrier->Ship : nullptr;
			if (owner != nullptr && warned.Contains(owner->Controller())) continue;
			u->Stunned = 60 + rng.NextDouble() * 60;
			u->Combat->ZeroAllShields();
		}
		for (const auto& v : warned)
			world->Log(jd, cs::Cat("The ", v->Name, " spotted the raiders lying dark off the ", lane->Name, " lane and was braced for the pulse"), WorldEventKind::Battle, lane->A);
		if (pilot != nullptr && pilot->Battle == b)
		{
			auto pu = b->UnitOf(pilot);
			cs::String closing = picket ? cs::Cat(lurker->Faction->Name, " warships closing") : cs::String("Raiders closing");
			pilotNotices.Add(pu != nullptr && pu->Stunned > 0
				? cs::Cat("INTERDICTED: the ", emitter->Name, "'s pulse throws you out of the ", lane->Name, " lane, drives and shields dead for ", cs::Fmt(pu->Stunned, "0"), " s. ", closing)
				: cs::Cat("INTERDICTED: thrown out of the ", lane->Name, " lane, but braced for the pulse: drives and shields held. ", closing));
		}
		if (source != nullptr) b->Emit(FxKind::Pulse, source->Pos, source->Pos, 30, 0, source, -1);
		Battles.Add(b);
		cs::String pulled = cs::Cat(victims.Count(), " ship", (victims.Count() == 1 ? "" : "s"), " pulled out");
		if (picket)
			// The law's own picket: a fight on the lane, but no raiders' ambush to steer the traffic round.
			world->Log(jd, cs::Cat("The ", lurker->Faction->Name, " lane picket ", lurker->Name, " interdicts the ", lane->Name, " lane: ", pulled), WorldEventKind::Battle, lane->A);
		else
		{
			double danger = 0;
			lanes0()->Danger.TryGetValue(lane, danger);
			lanes0()->SetDanger(lane, danger + 3);
			world->Log(jd, cs::Cat(lurker->Faction->Name, " raiders (", lurker->Name, ") spring an ambush on the ", lane->Name, " lane: ", pulled), WorldEventKind::Battle, lane->A);
			BumpDanger(lane->A, 1);
			BumpDanger(lane->B, 1);
		}
		BattleStarted.Invoke(b);
	}

	bool War::Forewarned(const cs::Ref<Ship>& victim, const cs::Ref<Ship>& lurker, double jd)
	{
		auto sensors = world->Sensors;
		if (sensors == nullptr || victim == nullptr || lurker == nullptr) return false;
		double pass = cs::Math::Max(lurker->LaneHoldSide.Length(), 1.0);
		return Sensors::Resolve(sensors->GroupSuite(victim), Signatures::Of(lurker, false, cs::Nullable<bool>(false)), pass, Signatures::Transponder(lurker)) >= ContactLevel::Class;
	}

	cs::Nullable<LaneThreat> War::ThreatAhead(const cs::Ref<Ship>& rider, double jd)
	{
		if (rider == nullptr || rider->Destroyed || rider->Phase != ShipPhase::Lane || rider->Battle != nullptr || Fleet()->Lanes == nullptr) return std::nullopt;
		auto lane = rider->LaneLeg.Lane;
		bool forward = rider->LaneLeg.From == lane->A;
		double now = Core::Fleet::LaneProgress(rider, jd);
		auto suite = world->Sensors != nullptr ? world->Sensors->GroupSuite(rider) : SensorSuite::Of(rider);
		double reach = suite->ReachKm(EmitterSignature);
		cs::Nullable<LaneThreat> best = std::nullopt;
		for (const auto& lurker : Fleet()->Ships())
		{
			if (!lurker->Ambushing() || lurker->LaneLeg.Lane != lane || !Targets(lurker, rider) || !Lurking(lurker)) continue;
			double ready = 0;
			if (pulseReady.TryGetValue(lurker, ready) && ready > jd) continue;
			// The lurker's station and the pulse's reach, as fractions of the rider's own leg.
			double stationA = lurker->LaneLeg.From == lane->A ? lurker->LaneHold : 1 - lurker->LaneHold;
			double station = forward ? stationA : 1 - stationA;
			double window = PulseRadiusKm / cs::Math::Max(lurker->LaneLengthKm, 1.0);
			if (now >= station + window) continue;
			double edge = station - window;
			if (rider->LaneEndProgress < edge) continue;
			double km = cs::Math::Max(0.0, station - now) * rider->LaneLengthKm;
			if (km > reach) continue;
			double pulseJd = now >= edge ? jd : RideReaches(rider, edge, jd);
			double seconds = (pulseJd - jd) * Astro::SecondsPerDay;
			if (!best.has_value() || seconds < best->SecondsToPulse)
				best = LaneThreat(lurker, seconds, km, Forewarned(rider, lurker, jd));
		}
		return best;
	}

	double War::RideReaches(const cs::Ref<Ship>& rider, double progress, double jd)
	{
		double lo = jd, hi = cs::Math::Max(jd, rider->PhaseEndJd);
		if (Core::Fleet::LaneProgress(rider, hi) < progress) return hi;
		for (int i = 0; i < 50; i++)
		{
			double mid = (lo + hi) * 0.5;
			if (Core::Fleet::LaneProgress(rider, mid) < progress) lo = mid;
			else hi = mid;
		}
		return hi;
	}

	cs::Ref<Hyperlanes> War::lanes0() { return Fleet()->Lanes; }

	void War::Hook(const cs::Ref<Battle>& b)
	{
		b->UnitDestroyed = [this](cs::Ref<Battle> battle, cs::Ref<BattleUnit> u) { OnUnitDestroyed(battle, u); };
		// Bounty hunters board, and hold their fire on a disabled quarry to take its captain alive (U.SPC-T63).
		b->Boards = [this](cs::Ref<BattleUnit> u) { return u->Ship != nullptr && Hunter(u->Ship); };
		b->WantsAlive = [this](cs::Ref<BattleUnit> u, cs::Ref<BattleUnit> o) { return u->Ship != nullptr && o->Ship != nullptr && world->Outlaws != nullptr && world->Outlaws->WantsAlive(u->Ship, o->Ship); };
		b->Launch = [](cs::Ref<Battle> battle, cs::Ref<BattleUnit> carrier) { FighterAI::Launch(battle, carrier); };
		b->FighterStep = [](cs::Ref<Battle> battle, cs::Ref<BattleUnit> u, double dt) { FighterAI::Step(battle, u, dt); };
		b->UnitCaptured = [this](cs::Ref<Battle> battle, cs::Ref<BattleUnit> u, cs::Ref<Faction> captor)
		{
			captures[u] = captor;
			// The player's ship taken: it is the captor's prize now, no longer the player's to fly
			// (the Runtime sees PilotedShip go and ends the session).
			if (u->Ship != nullptr && u->Ship == world->PilotedShip)
			{
				world->PilotedShip = nullptr;
				u->SetPlayerControlled(false);
			}
			// Allies who boarded together take it together (U.SPC-T392): the log names them all.
			cs::String by = u->Combat->Interior->Joint() ? u->Combat->Interior->BoarderNames() : captor->Name;
			if (u->Ship != nullptr) world->Log(battle->StartJd + battle->Seconds / Astro::SecondsPerDay,
				cs::Cat(battle->Name, ": ", by, " marines take the ", u->Ship->Name), WorldEventKind::Capture, Shown(battle));
		};
		// (C#'s `+=` on these delegate fields of a battle just made: each is still null, so it is an assignment.)
		b->UnitSurrendered = [this](cs::Ref<Battle> battle, cs::Ref<BattleUnit> u)
		{
			if (u->Ship != nullptr) world->Log(battle->StartJd + battle->Seconds / Astro::SecondsPerDay,
				cs::Cat(battle->Name, ": the ", u->Ship->Name, " strikes its colours and hails for terms"), WorldEventKind::Battle, Shown(battle));
		};
		b->SurrenderDecided = [this](cs::Ref<Battle> battle, cs::Ref<BattleUnit> u, cs::Ref<Faction> captor, SurrenderTerms terms) { OnSurrenderDecided(battle, u, captor, terms); };
		b->CargoDemanded = [this](cs::Ref<Battle> battle, cs::Ref<BattleUnit> u, cs::Ref<Faction> pirates)
		{
			if (u->Ship != nullptr) world->Log(battle->StartJd + battle->Seconds / Astro::SecondsPerDay,
				cs::Cat(battle->Name, ": ", pirates->Name, " demand the ", u->Ship->Name, "'s cargo"), WorldEventKind::Battle, Shown(battle));
		};
	}

	void War::OnSurrenderDecided(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor, SurrenderTerms terms)
	{
		auto ship = u->Ship;
		if (ship == nullptr) return;
		double jd = b->StartJd + b->Seconds / Astro::SecondsPerDay;
		cs::String who = captor != nullptr ? captor->Name : cs::String("its foes");
		switch (terms)
		{
		case SurrenderTerms::RansomCargo:
			if (ship->HasCargo)
			{
				if (captor != nullptr && captor->IsPirate) Loot(ship, captor, b);
				else
				{
					if (captor != nullptr) captor->Treasury += ship->CargoTonnes * GoodInfo::Of(ship->CargoGood)->BasePrice * 0.8;
					Economy()->Release(ship);
					ship->HasCargo = false;
					ship->CargoDestination = nullptr;
					ship->Cargo = cs::String();
				}
			}
			world->Log(jd, captor != nullptr && captor->IsPirate
				? cs::Cat(b->Name, ": the ", ship->Name, " jettisons its cargo, ", who, " drones collect it, and it is let go")
				: cs::Cat(b->Name, ": the ", ship->Name, " hands its cargo to ", who, " and is let go"), WorldEventKind::Capture, Shown(b));
			break;
		case SurrenderTerms::LetGo:
			world->Log(jd, cs::Cat(b->Name, ": ", who, " let the ", ship->Name, " go"), WorldEventKind::Battle, Shown(b));
			break;
		case SurrenderTerms::TakeShip:
			world->Log(jd, cs::Cat(b->Name, ": ", who, " send boarders to take the ", ship->Name), WorldEventKind::Capture, Shown(b));
			break;
		}
	}

	cs::Ref<Lane> War::BusiestLane(int system)
	{
		auto lanes = Fleet()->Lanes;
		cs::Ref<Lane> best = nullptr;
		double bestScore = 0;
		for (const auto& lane : lanes->Lanes)
		{
			double traffic = 0;
			for (const auto& s : Fleet()->Ships())
			{
				if (s->Destroyed || !world->IsTrader(s)) continue;
				if (s->Phase == ShipPhase::Lane && s->LaneLeg.Lane == lane) traffic += 1;
				// (C#'s `s.Journey != null`: a C++ list is never null, and an empty one adds nothing.)
				for (const auto& hop : s->Journey) if (hop.Lane == lane) traffic += 0.5;
			}
			double score = traffic * (lane->A == system || lane->B == system ? 2 : 1) * (0.7 + rng.NextDouble() * 0.6);
			if (score > bestScore)
			{
				bestScore = score;
				best = lane;
			}
		}
		return best;
	}

	// ---------------------------------------------------------------- prizes

	void War::DisposeOfPrize(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor, double jd)
	{
		auto ship = u->Ship;
		if (ship == nullptr || ship->Destroyed)
		{
			if (u->Kind == UnitKind::Platform) StrikeOff(u);
			return;
		}
		// Captors with a brig lock up some of its people first, for bounty or ransom in port (U.SPC-T65).
		auto captors = cs::List<cs::Ref<Ship>>();
		for (const auto& cu : b->Units)
			if (cu->Ship != nullptr && !cu->Dead && cu->Ship != ship && cu->Ship->Faction == captor) captors.Add(cu->Ship);
		double prisoners = Outfitting::TakePrisoners(ship, captors);
		cs::String how;
		double hull = ship->Combat->HullFraction();
		bool warship = ship->Spec->IsMilitary;
		if (captor->IsPirate)
		{
			// Looting draws nothing from the dice, so the roll can be made first: a hull about to be cast
			// adrift is stripped in a hurry, and some of its cargo is left aboard for a salvager.
			double roll = rng.NextDouble();
			bool adrift = !warship && roll < 0.55;
			auto good = ship->CargoGood;
			double left = 0;
			if (ship->HasCargo)
			{
				if (adrift)
				{
					left = ship->CargoTonnes * SalvageSession::LeftAboardShare;
					// The whole load it was bringing is off the port's books, not just the part looted.
					Economy()->Release(ship);
					ship->CargoDestination = nullptr;
					ship->CargoTonnes -= left;
				}
				Loot(ship, captor, b);
			}
			if (warship && hull >= 0.3) how = Steal(ship, captor, b, jd) ? cs::Cat("pressed into ", captor->Name, " service") : cs::String("scuttled");
			else if (warship) { Scuttle(ship, b, u, jd); how = "scuttled, its crew put off in the pods"; }
			else if (adrift) { LeaveAdrift(ship, b, u, captor, good, left, jd); how = "stripped and left adrift"; }
			else if (roll < 0.85)
			{
				captor->Treasury += Core::Economy::HullPrice(ship->Spec->Class) * 0.25;
				Plundered.Invoke(captor, Core::Economy::HullPrice(ship->Spec->Class), jd);
				// Its crew go with it, as hostages to be ransomed.
				TakeCrewCaptive(ship, captor, jd);
				Detach(ship);
				ship->Destroyed = true;
				// A hull lost to its owners like any other.
				int lost = 0;
				losses.TryGetValue(ship->Faction, lost);
				losses[ship->Faction] = lost + 1;
				how = cs::Cat("taken to ", HavenName(captor), " and sold to the breakers");
			}
			else { Scuttle(ship, b, u, jd); how = "scuttled, its crew put off in the pods"; }
		}
		else if (ship->Faction->IsPirate || hull < 0.3)
		{
			// A raider's surviving crew, taken off before it is scuttled, give away where they sail from.
			if (ship->Faction->IsPirate)
				if (auto home = HomeHaven(ship->Controller()))
					Discover(home, captor, jd, cs::Cat("prisoners taken off the ", ship->Name, " give it away"));
			if (ship->Faction->IsPirate)
			{
				TakeCrewCaptive(ship, captor, jd);
				Scuttle(ship);
				how = "scuttled";
			}
			else
			{
				Scuttle(ship, b, u, jd);
				how = "scuttled, its crew put off in the pods";
			}
		}
		else
		{
			if (ship->HasCargo)
			{
				captor->Treasury += ship->CargoTonnes * GoodInfo::Of(ship->CargoGood)->BasePrice * 0.8;
				Economy()->Release(ship);
				ship->HasCargo = false;
				ship->CargoDestination = nullptr;
				ship->Cargo = cs::String();
			}
			how = Steal(ship, captor, b, jd) ? cs::String("taken as a prize") : cs::String("scuttled");
		}
		world->Log(jd, cs::Cat("The ", ship->Name, " is ", how, " by the ", captor->Name) + (prisoners >= 1 ? cs::Cat(", ", cs::Fmt(prisoners, "0"), " of its crew taken prisoner") : cs::String("")), WorldEventKind::Capture, Shown(b));
	}

	cs::Ref<Faction> War::SettleJointPrize(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor, double jd)
	{
		auto ship = u->Ship;
		auto inside = u->Combat != nullptr ? u->Combat->Interior : nullptr;
		if (ship == nullptr || ship->Destroyed || inside == nullptr || !inside->Joint()) return captor;
		double price = Core::Economy::HullPrice(ship->Spec->Class);
		auto good = ship->CargoGood;
		auto deal = PrizeSettlement::Negotiate(inside->Parties, price * ship->Combat->HullFraction() * PrizeSettlement::PrizeValueShare,
			price * PrizeSettlement::BreakersShare, ship->HasCargo ? ship->CargoTonnes : 0, [](cs::Ref<Faction> f) { return f->Treasury; });
		cs::String allies = inside->BoarderNames();
		auto parts = cs::List<cs::String>();
		for (const auto& claim : deal->Claims)
		{
			// The keeper's part of the hold stays aboard for it; everyone else's is paid out now.
			if (claim->Faction != deal->Keeper && claim->CargoTonnes > 0 && ship->HasCargo)
			{
				double tonnes = cs::Math::Min(claim->CargoTonnes, ship->CargoTonnes);
				if (claim->Faction->IsPirate) DeliverLoot(claim->Faction, good, tonnes, b);
				else claim->Faction->Treasury += tonnes * GoodInfo::Of(good)->BasePrice * 0.8;
				ship->CargoTonnes -= tonnes;
			}
			if (claim->Faction != deal->Keeper && claim->HullPaid > 0)
			{
				claim->Faction->Treasury += claim->HullPaid;
				if (deal->Keeper != nullptr) deal->Keeper->Treasury -= claim->HullPaid;
			}
			parts.Add(cs::Cat(claim->Faction->Name, " ", cs::Fmt(claim->Share * 100, "0"), "%"));
		}
		if (ship->HasCargo && ship->CargoTonnes <= 1e-6)
		{
			Economy()->Release(ship);
			ship->HasCargo = false;
			ship->CargoDestination = nullptr;
			ship->Cargo = cs::String();
		}
		cs::String claims = cs::Strings::Join(", ", parts);
		if (deal->Keeper != nullptr)
		{
			world->Log(jd, cs::Cat("The ", allies, " settle the ", ship->Name, " between them (", claims, "): the ", deal->Keeper->Name, " keep the hull, buying out their partners for ", cs::Fmt(deal->BuyOut(), "N0"), " cr, and the hold is shared"),
				WorldEventKind::Capture, Shown(b));
			return deal->Keeper;
		}
		// Nobody could buy the others out: the hull goes to the breakers, its people as the largest claim's prisoners.
		cs::Ref<PrizeClaim> lead = nullptr;
		for (const auto& claim : deal->Claims)
			if (lead == nullptr || claim->Share > lead->Share) lead = claim;
		if (ship->HasCargo)
		{
			Economy()->Release(ship);
			ship->HasCargo = false;
			ship->CargoDestination = nullptr;
			ship->Cargo = cs::String();
		}
		auto leadFaction = lead != nullptr ? lead->Faction : nullptr;
		TakeCrewCaptive(ship, leadFaction ? leadFaction : captor, jd);
		Detach(ship);
		ship->Destroyed = true;
		int lost = 0;
		losses.TryGetValue(ship->Faction, lost);
		losses[ship->Faction] = lost + 1;
		world->Log(jd, cs::Cat("The ", allies, " cannot agree who keeps the ", ship->Name, " (", claims, "): it is sold to the breakers for ", cs::Fmt(deal->BreakersValue, "N0"), " cr, shared between them with the hold"),
			WorldEventKind::Capture, Shown(b));
		return nullptr;
	}

	void War::Loot(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& clan, const cs::Ref<Battle>& b)
	{
		Economy()->Release(ship);
		double value = ship->CargoTonnes * GoodInfo::Of(ship->CargoGood)->BasePrice;
		DeliverLoot(clan, ship->CargoGood, ship->CargoTonnes, b);
		clan->Treasury += value * 0.3;
		Plundered.Invoke(clan, value, lastJd);
		ship->HasCargo = false;
		ship->CargoDestination = nullptr;
		ship->Cargo = cs::String();
	}

	bool War::LoseInCollision(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd, const cs::String& what)
	{
		if (ship == nullptr || ship->Destroyed || ship->Battle != nullptr) return false;
		auto wreck = Wrecks->Add(ship, body, offsetKm, jd);
		(void)wreck;
		Scuttle(ship);
		world->Log(jd, cs::Cat("The ", ship->Name, " breaks up in a collision with ", what), WorldEventKind::Loss, body);
		return true;
	}

	void War::Scuttle(const cs::Ref<Ship>& ship, const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double jd)
	{
		if (b != nullptr && u != nullptr && ship->Combat != nullptr)
		{
			auto inside = ship->Combat->Interior;
			double aboard = inside->Crew() + inside->Marines();
			Crews->Launch(ship->Name, ship->Faction, aboard, Core::Crews::Complement(ship->Spec->Class), AbandonCause::Scuttled,
				b->Body, b->Anchor, u->Pos, b->Site, jd, cs::String(), /*haven:*/ ship->Faction->IsPirate ? HomeHaven(ship->Controller()) : nullptr);
			inside->TakeCrew(inside->Crew());
			inside->TakeMarines(inside->Marines());
		}
		// Its group (if it led one) gets a new flagship, so the escorts are not left frozen on a wreck.
		Detach(ship);
		if (ship->HasCargo) Economy()->Release(ship);
		ship->HasCargo = false;
		ship->Destroyed = true;
		int n = 0;
		losses.TryGetValue(ship->Faction, n);
		losses[ship->Faction] = n + 1;
	}

	void War::LeaveAdrift(const cs::Ref<Ship>& ship, const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& raiders, Good good, double leftTonnes, double jd)
	{
		Detach(ship);
		ship->Derelict = true;
		ship->DerelictSinceJd = jd;
		ship->DerelictBy = raiders;
		ship->Cargo = "Derelict: boarded, stripped and abandoned";
		Core::Fleet::PinAt(ship, b->Body, b->Anchor + u->Pos, u->Forward, jd);
		// A hull lost to its owners like any other.
		int lost = 0;
		losses.TryGetValue(ship->Faction, lost);
		losses[ship->Faction] = lost + 1;
		double aboard = ship->Combat != nullptr ? ship->Combat->Interior->Crew() + ship->Combat->Interior->Marines() : 0;
		SalvageSession::Strand(ship, raiders, good, leftTonnes, jd, aboard);
		// Those who did not hide aboard are put off in the pods, to be hauled in for ransom or found by someone kinder.
		double hid = ship->Combat != nullptr ? ship->Combat->Interior->Survivors() : 0;
		if (aboard - hid >= 0.5)
			Crews->Launch(ship->Name, ship->Faction, aboard - hid, Core::Crews::Complement(ship->Spec->Class), AbandonCause::Scuttled, b->Body, b->Anchor, u->Pos, b->Site, jd);
	}

	void War::TakeCrewCaptive(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& captor, double jd)
	{
		if (ship->Combat == nullptr) return;
		auto inside = ship->Combat->Interior;
		double people = inside->Crew() + inside->Marines();
		if (people < 0.5) return;
		inside->TakeCrew(inside->Crew());
		inside->TakeMarines(inside->Marines());
		if (ship->Faction->IsPirate) Core::Crews::Lose(ship->Faction, people);
		else if (captor->IsPirate) Crews->Hold(ship->Faction, captor, people, false, LiveHavenOf(captor), jd, ship->Name);
		else Crews->Hold(ship->Faction, captor, people, true, nullptr, jd, ship->Name);
	}

	bool War::Steal(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& captor, const cs::Ref<Battle>& b, double jd)
	{
		TakeCrewCaptive(ship, captor, jd);
		Detach(ship);
		auto old = ship->Faction;
		int n = 0;
		losses.TryGetValue(old, n);
		losses[old] = n + 1;
		ship->Faction = captor;
		// Whoever sat in its brig goes free with the taking: nobody's to hand over for a bounty any more.
		ship->Prisoners = 0;
		ship->PrisonerBounty = 0;
		if (captor->Prefix.size() > 0 && cs::Strings::StartsWith(ship->Name, cs::Cat(old->Prefix, " "))) ship->Name = cs::Cat(captor->Prefix, cs::Strings::Substring(ship->Name, static_cast<int>(old->Prefix.size())));
		ship->Combat->Interior->HandOver(ship->Combat->Spec->Crew * 0.3);
		cs::Ref<Ship> group = nullptr;
		for (const auto& u : b->Units)
		{
			if (u->Ship == nullptr || u->Dead || u->Ship == ship || u->Ship->Faction != captor) continue;
			auto leader = u->Ship->Controller();
			if (!leader->Destroyed && leader->Faction == captor && !leader->IsEscort() && leader != world->PilotedShip) group = leader;
		}
		// C#'s `ship.Orbit?.Body == group.Orbit?.Body`: two nulls are equal, a null and a body are not.
		auto shipBody = ship->Orbit != nullptr ? cs::Nullable<int>(ship->Orbit->Body) : cs::Nullable<int>();
		auto groupBody = (group != nullptr && group->Orbit != nullptr) ? cs::Nullable<int>(group->Orbit->Body) : cs::Nullable<int>();
		if (ship->Spec->IsMilitary && group != nullptr && ship->Phase == group->Phase && shipBody == groupBody)
		{
			int index = 0;
			for (const auto& e : Fleet()->Ships()) if (e->Leader == group && !e->Destroyed) index++;
			if (index < 7)
			{
				// The first slot no living escort holds: the lost and the cut loose leave gaps.
				Fleet()->AddEscort(group, ship, Core::Fleet::SlotFor(FreeSlot(group)));
				return true;
			}
		}
		if (ship->Spec->IsMilitary)
		{
			ship->Role = ShipRole::Patrol;
			// (C#'s `group?.Route ?? ...`: a C++ route is never null, so an empty one counts as none.)
			if (group != nullptr && group->Route.Count() > 0) ship->Route = group->Route;
			else ship->Route = captor->IsPirate && HavenBody(captor) >= 0 ? cs::List<int>{ HavenBody(captor) } : OwnPorts(captor, ship->Orbit->Body);
		}
		return true;
	}

	cs::List<int> War::OwnPorts(const cs::Ref<Faction>& f, int fallback)
	{
		auto own = cs::List<int>();
		for (const auto& s : Economy()->Settlements)
			if (s->Owner == f && !own.Contains(s->Body)) own.Add(s->Body);
		return own.Count() > 0 ? own.ToArray() : cs::List<int>{ fallback };
	}

	void War::Detach(const cs::Ref<Ship>& ship)
	{
		if (!ship->IsEscort())
		{
			Succession(ship);
			return;
		}
		auto lead = ship->Leader;
		ship->Leader = nullptr;
		ship->Phase = lead->Phase;
		ship->Orbit = OwnSpot(lead->Orbit, ship, lead);
		ship->DepartureOrbit = lead->DepartureOrbit;
		ship->DestinationBody = lead->DestinationBody;
		ship->OriginBody = lead->OriginBody;
		ship->PhaseStartJd = lead->PhaseStartJd;
		ship->PhaseEndJd = lead->PhaseEndJd;
		ship->FrameBody = lead->FrameBody;
		ship->Path = lead->Path;
		ship->PathCumulative = lead->PathCumulative;
		ship->PathLength = lead->PathLength;
		ship->AccelKmPerDay2 = lead->AccelKmPerDay2;
		ship->Conic = lead->Conic;
		ship->GravityLossKmS = lead->GravityLossKmS;
		ship->DepartureVelocity = lead->DepartureVelocity;
		ship->ArrivalVelocity = lead->ArrivalVelocity;
		ship->Journey = lead->Journey;
		ship->JourneyIndex = lead->JourneyIndex;
		ship->FinalBody = lead->FinalBody;
		ship->LaneLeg = lead->LaneLeg;
		ship->LaneFromOffset = lead->LaneFromOffset;
		ship->LaneToOffset = lead->LaneToOffset;
		ship->LaneLengthKm = lead->LaneLengthKm;
		ship->LaneStartProgress = lead->LaneStartProgress;
		ship->LaneEndProgress = lead->LaneEndProgress;
		ship->LaneHold = lead->LaneHold;
		ship->LaneHoldSide = lead->LaneHoldSide;
		CopyRest(lead, ship);
	}

	cs::Ref<ParkingOrbit> War::OwnSpot(const cs::Ref<ParkingOrbit>& orbit, const cs::Ref<Ship>& ship, const cs::Ref<Ship>& lead)
	{
		if (orbit == nullptr || orbit->Fixed || orbit->RadiusKm <= 0) return orbit;
		double gap = Core::Fleet::SafeDistanceKm(ship, lead) * 1.5;
		auto o = cs::New<ParkingOrbit>();
		o->Body = orbit->Body; o->RadiusKm = orbit->RadiusKm; o->U = orbit->U; o->V = orbit->V; o->Center = orbit->Center;
		o->EpochJd = orbit->EpochJd; o->PhaseAtEpoch = orbit->PhaseAtEpoch - gap / orbit->RadiusKm; o->RateRadPerDay = orbit->RateRadPerDay;
		return o;
	}

	void War::CopyRest(const cs::Ref<Ship>& from, const cs::Ref<Ship>& to)
	{
		to->LaneStar = from->LaneStar;
		to->LaneShape = from->LaneShape;
		to->Ring = from->Ring;
		to->HoldPoint = from->HoldPoint;
	}

	void War::MovePlunder(const cs::Ref<Ship>& from, const cs::Ref<Ship>& to)
	{
		// (A null plunder hold in C# is an empty list here.)
		if (from->Plunder.Count() == 0) return;
		if (to->Plunder.Count() == 0) to->Plunder = cs::NewArray<double>(GoodInfo::Count);
		for (int i = 0; i < GoodInfo::Count; i++) to->Plunder[i] += from->Plunder[i];
		from->Plunder.Clear();
	}

	// ---------------------------------------------------------------- meeting the enemy

	void War::OnArrived(const cs::Ref<Ship>& ship, double jd)
	{
		// On the ground (or on the way down or up) a ship is out of the fight.
		if (ship->Destroyed || ship->Battle != nullptr || ship->Planetside()) return;
		// Local traffic neither joins a fight nor draws one (U.SPC-T375).
		if (ship->Controller()->Role == ShipRole::Commuter) return;
		int body = ship->Orbit->Body;
		// Raiders home with plunder put it in the haven's stores (U.SPC-T94).
		if (ship->Faction->IsPirate) UnloadPlunder(ship->Controller(), body);
		// A clan's smuggler docks at a lawless station unchallenged: its guns look the other way (U.SPC-T94).
		if (ship->Runner)
			for (const auto& st : Economy()->At(body))
				if (st->Lawless && !st->Fallen) return;
		// Anyone who turns up at a hidden haven's rock sees it.
		if (!ship->Faction->IsPirate)
			for (const auto& st : Economy()->At(body))
				if (st->Hidden) Discover(st, ship->Faction, jd, cs::Cat("the ", ship->Name, " comes upon it"));
		// Off the system's root (at a jump gate's approach) only what is near it counts (U.SPC-T94).
		cs::Nullable<DVec3> at = body == Catalog()->Root()->Index ? cs::Nullable<DVec3>(ship->Controller()->Orbit->OffsetAt(jd)) : cs::Nullable<DVec3>();
		auto existing = BattleAt(body);
		if (existing != nullptr && at.has_value() && (existing->Anchor - *at).Length() > GateFightRangeKm) existing = nullptr;
		if (existing != nullptr)
		{
			// Join the side it can fight beside: against one side's leader and not the other's.
			bool h0 = Diplomacy()->Hostile(ship->Faction, existing->SideLeaders[0]);
			bool h1 = Diplomacy()->Hostile(ship->Faction, existing->SideLeaders[1]);
			int side = h0 && !h1 ? 1 : h1 && !h0 ? 0 : -1;
			if (side >= 0) Join(existing, ship, side, true);
			return;
		}
		if (TryStartBattle(body, ship->Faction, jd, at) != nullptr) return;
		// Not fit to pick a fight itself: one waiting here that is falls on it instead, unless it is pulling back
		// from a fight it has just lost (let go while it gets clear, see TryStartBattle).
		if (Withdrawing(ship->Controller(), jd)) return;
		for (const auto& s : PresentGroups(body, at, jd))
			if (s->Battle == nullptr && (s->Spec->IsMilitary || s->Faction->IsPirate) && Diplomacy()->Hostile(s->Faction, ship->Faction) && Willing(s->Controller(), jd) && s->Combat->Armed() && Commanded(s, body))
			{
				TryStartBattle(body, s->Faction, jd, at);
				return;
			}
	}

	cs::Ref<Battle> War::TryStartPlayerBattle(const cs::Ref<Ship>& ship, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd)
	{
		if (ship->Destroyed || ship->Battle != nullptr || ship->Planetside()) return nullptr;
		// Only in the body's own neighbourhood (the frame body never changes mid-flight, however far the
		// pilot goes), and only into a fight it has actually reached: one placed beyond the battle's
		// edge would be counted as escaped on the very next step. A fight it starts is fought where it is.
		if (offsetFromBodyKm.Length() > ScaleModel::NeighbourhoodKm((*Catalog())[body])) return nullptr;
		auto b = BattleAt(body);
		if (b != nullptr && (offsetFromBodyKm - b->Anchor).Length() > Battle::EscapeKm * 0.8) return nullptr;
		// Already left this one: not back in until it is over.
		if (b != nullptr && b->UnitOf(ship) != nullptr) return nullptr;
		if (!b) b = TryStartBattle(body, ship->Faction, jd, cs::Nullable<DVec3>(offsetFromBodyKm), true);
		if (b == nullptr) return nullptr;
		bool h0 = Diplomacy()->Hostile(ship->Faction, b->SideLeaders[0]);
		bool h1 = Diplomacy()->Hostile(ship->Faction, b->SideLeaders[1]);
		int side = h0 && !h1 ? 1 : h1 && !h0 ? 0 : -1;
		if (side < 0) return nullptr;
		auto unit = b->AddShip(ship, side, offsetFromBodyKm - b->Anchor, forward);
		unit->SetPlayerControlled(true);
		return b;
	}

	cs::Ref<Battle> War::TryStartPlayerAttack(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& target, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd)
	{
		if (ship == nullptr || target == nullptr || ship == target || ship->Destroyed || ship->Battle != nullptr || ship->Planetside()) return nullptr;
		if (target->Destroyed || target->GateDeparted || target->Derelict || target->Planetside()) return nullptr;
		// An enemy is fought as usual where anyone there is armed and willing; an enemy merchant alone, with no
		// escort or platforms to start that fight, is attacked as any other ship the player opens fire on.
		if (Diplomacy()->Hostile(ship->Faction, target->Faction))
		{
			auto fight = TryStartPlayerBattle(ship, body, offsetFromBodyKm, forward, jd);
			if (fight != nullptr || ship->Battle != nullptr) return fight;
		}
		auto neighbourhood = ScaleModel::NeighbourhoodKm((*Catalog())[body]);
		if (offsetFromBodyKm.Length() > neighbourhood) return nullptr;
		auto existing = BattleAt(body);
		if (existing != nullptr)
		{
			auto tu = existing->UnitOf(target);
			if (tu == nullptr || target->Battle != existing || existing->UnitOf(ship) != nullptr) return nullptr;
			if ((offsetFromBodyKm - existing->Anchor).Length() > Battle::EscapeKm * 0.8) return nullptr;
			auto joined = existing->AddShip(ship, 1 - tu->Side, offsetFromBodyKm - existing->Anchor, forward);
			joined->SetPlayerControlled(true);
			return existing;
		}
		if (target->Battle != nullptr) return nullptr;
		auto leader = target->Controller();
		// Only a ship holding orbit here (parked, or waiting on a docking ring): one in flight keeps its own schedule.
		if (leader->Destroyed || leader->Derelict || leader->Battle != nullptr || leader == world->PilotedShip || !Core::Fleet::IsAt(leader, body)) return nullptr;

		// Out at the root (a jump gate) only those near the fight join it, not the target's ships at every gate
		// (as TryStartBattle, U.SPC-T94).
		auto present = PresentGroups(body, body == Catalog()->Root()->Index ? cs::Nullable<DVec3>(offsetFromBodyKm) : cs::Nullable<DVec3>(), jd);
		auto b = NewBattle(body, jd, present, cs::Nullable<DVec3>(offsetFromBodyKm));
		auto unit = b->AddShip(ship, 0, DVec3::Zero, forward);
		unit->SetPlayerControlled(true);
		Join(b, leader, 1, false);
		for (const auto& s : present)
			if (s->Faction == target->Faction && s->Battle == nullptr) Join(b, s, 1, false);
		for (const auto& st : Economy()->At(body))
		{
			if (st->Owner != target->Faction) continue;
			for (int i = 0; i < st->Platforms.Count(); i++)
			{
				double a = Astro::TwoPi * i / cs::Math::Max(1, st->Platforms.Count());
				b->AddPlatform(st, st->Platforms[i], 1, Spot(b, 1) * 0.4 + DVec3(cs::Math::Cos(a), cs::Math::Sin(a), 0.1) * 6);
			}
		}
		Battles.Add(b);
		world->Log(jd, cs::Cat(b->Name, ": the ", ship->Name, " opens fire on the ", target->Faction->Name, " ", cs::Strings::ToLowerInvariant(target->Spec->DisplayName), " ", target->Name), WorldEventKind::Battle, Shown(b));
		BumpDanger(body, 1);
		BattleStarted.Invoke(b);
		return b;
	}

	cs::Ref<Battle> War::TryStartInterdiction(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& patrol, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd)
	{
		return Enforce(ship, patrol, body, offsetFromBodyKm, forward, jd, "Interdiction off ",
			[ship](cs::Ref<Ship> leader, cs::Ref<Battle> b) { return cs::Cat(b->Name, ": the ", leader->Faction->Name, " patrol ", leader->Name, " moves on the wanted ", ship->Name); });
	}

	cs::Ref<Battle> War::TryStartHunt(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& hunter, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd)
	{
		return Enforce(ship, hunter, body, offsetFromBodyKm, forward, jd, "Bounty hunt off ",
			[ship](cs::Ref<Ship> leader, cs::Ref<Battle> b) { return cs::Cat(b->Name, ": the bounty hunter ", leader->Name, " closes on the wanted ", ship->Name); });
	}

	cs::Ref<Battle> War::TryStartPursuit(const cs::Ref<Ship>& ship, const cs::Ref<Pursuer>& g, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd)
	{
		if (ship == nullptr || ship->Destroyed || ship->Planetside() || g == nullptr || g->Leader->Destroyed) return nullptr;
		auto b = ship->Battle;
		int side;
		if (b != nullptr)
		{
			auto mine = b->UnitOf(ship);
			if (mine == nullptr) return nullptr;
			side = 1 - mine->Side;
		}
		else
		{
			b = cs::New<Battle>(nextBattleId++, body, cs::Cat("Run down off ", (*Catalog())[body]->Name), offsetFromBodyKm, jd, rng.Next());
			b->AggressorSide = 1;
			Hook(b);
			auto approach = g->Pos - offsetFromBodyKm;
			b->ApproachAxis = approach.LengthSquared() > 1e-9 ? approach.Normalized() : DVec3::UnitX;
			auto unit = b->AddShip(ship, 0, DVec3::Zero, forward);
			unit->SetPlayerControlled(true);
			side = 1;
			Battles.Add(b);
			world->Log(jd, cs::Cat(b->Name, ": the ", g->Faction()->Name, " ", g->Leader->Name, " runs down the ", ship->Name), WorldEventKind::Battle, Shown(b));
			BattleStarted.Invoke(b);
		}
		for (const auto& s : g->Ships)
		{
			if (s->Destroyed || s->Battle != nullptr) continue;
			auto at = Pursuit::OffsetOf(g, s) - b->Anchor;
			if (at.Length() > PursuitStartKm) at = at.Normalized() * PursuitStartKm;
			auto mine = b->UnitOf(ship);
			auto face = (mine != nullptr ? mine->Pos : DVec3::Zero) - at;
			b->AddShip(s, side, at, face.LengthSquared() > 1e-12 ? face : DVec3::UnitX);
		}
		return b;
	}

	cs::Ref<Battle> War::Enforce(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& patrol, int body, DVec3 offsetFromBodyKm, DVec3 forward, double jd, const cs::String& name, cs::Func<cs::String, cs::Ref<Ship>, cs::Ref<Battle>> news)
	{
		if (ship == nullptr || patrol == nullptr || ship->Destroyed || ship->Battle != nullptr || ship->Planetside()) return nullptr;
		auto leader = patrol->Controller();
		if (leader->Destroyed || leader->Battle != nullptr || leader->Derelict || leader == world->PilotedShip) return nullptr;
		if (offsetFromBodyKm.Length() > ScaleModel::NeighbourhoodKm((*Catalog())[body]) || BattleAt(body) != nullptr) return nullptr;
		auto b = cs::New<Battle>(nextBattleId++, body, cs::Cat(name, (*Catalog())[body]->Name), offsetFromBodyKm, jd, rng.Next());
		b->EnforcingSide = 1;
		b->AggressorSide = 1;
		Hook(b);
		auto approach = DVec3::Cross(offsetFromBodyKm.LengthSquared() > 1e-9 ? offsetFromBodyKm.Normalized() : DVec3::UnitX, DVec3::UnitZ);
		b->ApproachAxis = approach.LengthSquared() > 1e-6 ? approach.Normalized() : DVec3::UnitX;
		auto unit = b->AddShip(ship, 0, DVec3::Zero, forward);
		unit->SetPlayerControlled(true);
		Join(b, leader, 1, false);
		Battles.Add(b);
		world->Log(jd, news(leader, b), WorldEventKind::Law, Shown(b));
		BattleStarted.Invoke(b);
		return b;
	}

	cs::Ref<Battle> War::TryStartBattle(int body, const cs::Ref<Faction>& aggressor, double jd, cs::Nullable<DVec3> anchor)
	{
		return TryStartBattle(body, aggressor, jd, anchor, false);
	}

	cs::Ref<Battle> War::TryStartBattle(int body, const cs::Ref<Faction>& aggressor, double jd, cs::Nullable<DVec3> anchor, bool byPlayer)
	{
		// Off the system's root (out at a jump gate) a fight takes in only those near where it starts, not every ship
		// at every gate (U.SPC-T94).
		cs::Nullable<DVec3> near = std::nullopt;
		if (body == Catalog()->Root()->Index) near = anchor.has_value() ? anchor : SpotOf(aggressor, body, jd);
		auto present = PresentGroups(body, near, jd);
		auto hostile = cs::HashSet<cs::Ref<Faction>>();
		bool armed = aggressor->IsMilitary || aggressor->IsPirate;
		for (const auto& s : present)
			// A group withdrawing from a fight it lost is let go while it gets clear: no quarrel to pick with it
			// (it is still swept in if a fight starts here over someone else).
			if (Diplomacy()->Hostile(aggressor, s->Faction) && !Withdrawing(s->Controller(), jd))
			{
				hostile.Add(s->Faction);
				armed |= s->Spec->IsMilitary || s->Faction->IsPirate;
			}
		for (const auto& st : Economy()->At(body))
			if (st->Platforms.Count() > 0 && Diplomacy()->Hostile(aggressor, st->Owner))
			{
				hostile.Add(st->Owner);
				armed = true;
			}
		if (hostile.Count() == 0 || !armed) return nullptr;
		// Raiders falling on a port's merchants size them up first (U.SPC-T374): with no guns there to fight, they
		// spring only on a mark worth the heat (a civilian hull is left alone unless it is worth a great deal).
		if (aggressor->IsPirate && !byPlayer && !WorthAFight(aggressor, body, present, jd)) return nullptr;

		// A fight needs someone fit and willing to start it: a healthy armed warship or a platform.
		bool aggressorArmed = false, defenderArmed = false, aggressorFresh = false;
		for (const auto& s : present)
		{
			bool military = (s->Spec->IsMilitary || s->Faction->IsPirate) && Willing(s->Controller(), jd) && s->Combat->Armed();
			// Only a group commanded to fight here opens the fight (U.SPC-T371); one merely passing through, or off its
			// beat, leaves the hostiles it meets alone. The player's own fight is the player's to start.
			if (s->Faction == aggressor && military && (byPlayer || Commanded(s, body))) aggressorArmed = true;
			if (s->Faction == aggressor && !Withdrawing(s->Controller(), jd)) aggressorFresh = true;
			if (hostile.Contains(s->Faction) && military) defenderArmed = true;
		}
		bool platforms = false;
		for (const auto& st : Economy()->At(body))
			if (st->Platforms.Count() > 0 && hostile.Contains(st->Owner)) platforms = true;
		// Platforms open on a hostile hull come into range, but not again on one that has only just run from them.
		if (!aggressorArmed && !(platforms && (aggressorFresh || byPlayer)) && !(byPlayer && defenderArmed)) return nullptr;

		auto b = NewBattle(body, jd, present, anchor.has_value() ? anchor : near);
		for (const auto& s : present)
		{
			int side = SideFor(s->Faction, aggressor, hostile);
			if (side >= 0) Join(b, s, side, false);
		}
		for (const auto& st : Economy()->At(body))
		{
			int side = SideFor(st->Owner, aggressor, hostile);
			if (side < 0) continue;
			for (int i = 0; i < st->Platforms.Count(); i++)
			{
				double a = Astro::TwoPi * i / cs::Math::Max(1, st->Platforms.Count());
				auto pos = Spot(b, side) * 0.4 + DVec3(cs::Math::Cos(a), cs::Math::Sin(a), 0.1) * 6;
				b->AddPlatform(st, st->Platforms[i], side, pos);
			}
		}
		Battles.Add(b);
		world->Log(jd, cs::Cat(b->Name, ": ", Describe(b, 0), " engage ", Describe(b, 1)), WorldEventKind::Battle, Shown(b));
		BumpDanger(body, 1.5);
		BattleStarted.Invoke(b);
		return b;
	}

	bool War::WorthAFight(const cs::Ref<Faction>& clan, int body, const cs::List<cs::Ref<Ship>>& present, double jd)
	{
		for (const auto& st : Economy()->At(body))
			if (st->Platforms.Count() > 0 && Diplomacy()->Hostile(clan, st->Owner)) return true;
		auto marks = cs::List<cs::Ref<Ship>>();
		double band = 0;
		for (const auto& s : present)
		{
			if (s->Faction == clan)
			{
				if (Willing(s->Controller(), jd)) band += s->Combat->Strength();
				continue;
			}
			if (!Diplomacy()->Hostile(clan, s->Faction) || Withdrawing(s->Controller(), jd)) continue;
			if (s->Spec->IsMilitary || s->Faction->IsPirate) return true;
			if (!s->IsEscort()) marks.Add(s);
		}
		return marks.Count() == 0 || BestMark(marks, 0, cs::Math::Max(band, 1e-6)) != nullptr;
	}

	void War::StrikeOff(const cs::Ref<BattleUnit>& u)
	{
		if (u->Platform != nullptr) u->Platform->Platforms.Remove(u->Combat);
		if (u->Fort != nullptr) u->Fort->Forts.Remove(u->Combat);
	}

	cs::Ref<Battle> War::StartGateBattle(const cs::Ref<JumpGate>& gate, const cs::Ref<Faction>& attacker, double jd)
	{
		int root = Catalog()->Root()->Index;
		if (gate == nullptr || (gate->Home != nullptr ? gate->Home->World : nullptr) != world || BattleAt(root) != nullptr) return nullptr;
		auto present = cs::List<cs::Ref<Ship>>();
		for (const auto& s : PresentGroups(root))
		{
			auto o = s->Controller()->Orbit;
			if (o != nullptr && (o->OffsetAt(jd) - gate->PositionKm).Length() <= GateReachKm) present.Add(s);
		}
		auto hostile = cs::HashSet<cs::Ref<Faction>>();
		if (gate->Forts.Count() > 0 && Diplomacy()->Hostile(attacker, gate->Owner)) hostile.Add(gate->Owner);
		bool armed = false;
		for (const auto& s : present)
		{
			bool military = (s->Spec->IsMilitary || s->Faction->IsPirate) && s->Combat->Armed();
			if (s->Faction == attacker && military && Willing(s->Controller(), jd)) armed = true;
			else if (military && !s->Faction->IsPirate && Diplomacy()->Hostile(attacker, s->Faction)) hostile.Add(s->Faction);
		}
		if (!armed || hostile.Count() == 0) return nullptr;

		int n = 0;
		gateBattles.TryGetValue(gate, n);
		gateBattles[gate] = n + 1;
		cs::String name = cs::Cat((n == 0 ? cs::String("") : cs::Cat(Ordinal(n + 1), " ")), "Battle of the ", gate->Name);
		auto b = cs::New<Battle>(nextBattleId++, root, name, gate->PositionKm, jd, rng.Next());
		Hook(b);
		auto approach = DVec3::Cross(gate->PositionKm.Normalized(), DVec3::UnitZ);
		b->ApproachAxis = approach.LengthSquared() > 1e-6 ? approach.Normalized() : DVec3::UnitX;
		b->AggressorSide = 0;
		for (const auto& s : present)
		{
			if (s->Faction->IsPirate) continue;
			int side = SideFor(s->Faction, attacker, hostile);
			if (side >= 0) Join(b, s, side, false);
		}
		if (hostile.Contains(gate->Owner))
			for (int i = 0; i < gate->Forts.Count(); i++)
			{
				double a = Astro::TwoPi * i / cs::Math::Max(1, gate->Forts.Count());
				auto pos = Spot(b, 1) * 0.4 + DVec3(cs::Math::Cos(a), cs::Math::Sin(a), 0.1) * 6;
				b->AddFort(gate, gate->Forts[i], 1, pos);
			}
		Battles.Add(b);
		world->Log(jd, cs::Cat(b->Name, ": ", Describe(b, 0), " storm the gate against ", Describe(b, 1)), WorldEventKind::Battle, root);
		BumpDanger(root, 1.5);
		BattleStarted.Invoke(b);
		return b;
	}

	bool War::Willing(const cs::Ref<Ship>& leader, double jd)
	{
		return !Withdrawing(leader, jd) && leader->Combat->HullDamage() < 0.45 && leader->Combat->Armed();
	}

	bool War::Withdrawing(const cs::Ref<Ship>& leader, double jd)
	{
		double until = 0;
		return retreating.TryGetValue(leader, until) && jd < until;
	}

	int War::SideFor(const cs::Ref<Faction>& f, const cs::Ref<Faction>& aggressor, const cs::HashSet<cs::Ref<Faction>>& hostile)
	{
		if (f == aggressor) return 0;
		if (hostile.Contains(f)) return 1;
		if (Diplomacy()->Hostile(f, aggressor)) return 1;
		for (const auto& h : hostile)
			if (Diplomacy()->Hostile(f, h)) return 0;
		return -1;
	}

	cs::List<cs::Ref<Ship>> War::PresentGroups(int body, cs::Nullable<DVec3> near, double jd)
	{
		auto list = cs::List<cs::Ref<Ship>>();
		for (const auto& s : Fleet()->Ships())
		{
			// A ship under direct player control isn't swept into a fight the AI decides to start.
			if (s->Destroyed || s->Battle != nullptr || s == world->PilotedShip) continue;
			auto c = s->Controller();
			// A stripped, abandoned hulk is not a fighting group.
			if (c->Destroyed || s->Derelict || c->Derelict || !Core::Fleet::IsAt(c, body)) continue;
			// Buses, taxis and people's own craft are no combatants (U.SPC-T375): they stand clear of a fight.
			if (c->Role == ShipRole::Commuter) continue;
			if (near.has_value() && (c->Orbit->OffsetAt(jd) - *near).Length() > GateFightRangeKm) continue;
			list.Add(s);
		}
		return list;
	}

	cs::Nullable<DVec3> War::SpotOf(const cs::Ref<Faction>& f, int body, double jd)
	{
		for (const auto& s : Fleet()->Ships())
			if (!s->Destroyed && !s->IsEscort() && s->Faction == f && s != world->PilotedShip && Core::Fleet::IsAt(s, body)) return s->Orbit->OffsetAt(jd);
		return std::nullopt;
	}

	cs::Ref<Battle> War::NewBattle(int body, double jd, const cs::List<cs::Ref<Ship>>& present, cs::Nullable<DVec3> at)
	{
		auto b0 = (*Catalog())[body];
		DVec3 dir = DVec3::Zero;
		for (const auto& s : present)
			if (s->Controller()->Orbit != nullptr) dir += s->Controller()->Orbit->OffsetAt(jd).Normalized();
		if (dir.LengthSquared() < 1e-6)
		{
			// (C# evaluates the DVec3 constructor's arguments left to right; C++ leaves the order unspecified, so draw them first.)
			double rx = rng.NextDouble() - 0.5;
			double ry = rng.NextDouble() - 0.5;
			dir = DVec3(rx, ry, 0.2);
		}
		auto anchor = at.has_value() ? *at : dir.Normalized() * Battle::AnchorDistanceKm(b0);
		// A fight over a haven nobody has found yet must not name its rock: it is a skirmish
		// somewhere in the belt (counted apart, so the first named battle there is still the first).
		bool concealed = HidesHaven(body);
		cs::String place = !concealed ? b0->Name : b0->TrojanPoint != LagrangePoint::None ? cs::String("the Trojans") : b0->Kind == BodyKind::Asteroid ? cs::String("the Belt") : cs::String("the Dark");
		int key = concealed ? (b0->Kind == BodyKind::Asteroid ? ConcealedBelt : ConcealedDark) : body;
		// A fight out at a jump gate is named for the gate (U.SPC-T94).
		if (body == Catalog()->Root()->Index && at.has_value())
			if (auto gate = GateNear(*at))
			{
				place = cs::Cat("the ", gate->Name);
				key = GateKey - world->Gates.IndexOf(gate);
			}
		int n = 0;
		battleCount.TryGetValue(key, n);
		battleCount[key] = n + 1;
		cs::String kind = concealed ? "Skirmish in " : "Battle of ";
		cs::String name = (n == 0 ? kind : cs::Cat(Ordinal(n + 1), " ", kind)) + place;
		auto b = cs::New<Battle>(nextBattleId++, body, name, anchor, jd, rng.Next());
		Hook(b);
		auto approach = DVec3::Cross(anchor.Normalized(), DVec3::UnitZ);
		if (approach.LengthSquared() < 1e-6) approach = DVec3::UnitX;
		b->ApproachAxis = approach.Normalized();
		return b;
	}

	cs::Ref<JumpGate> War::GateNear(DVec3 offsetKm)
	{
		cs::Ref<JumpGate> best = nullptr;
		double bestKm = GateFightRangeKm;
		for (const auto& g : world->Gates)
		{
			double km = (g->PositionKm - offsetKm).Length();
			if (km < bestKm)
			{
				bestKm = km;
				best = g;
			}
		}
		return best;
	}

	// ---------------------------------------------------------------- gate ambushes

	void War::GateAmbushes(double jd)
	{
		if (world->Gates.Count() == 0) return;
		int root = Catalog()->Root()->Index;
		double ended = 0;
		if (BattleAt(root) != nullptr || (lastBattleEndJd.TryGetValue(root, ended) && jd - ended < 0.1)) return;
		for (const auto& lurker : Fleet()->Ships())
		{
			if (lurker->Destroyed || lurker->IsEscort() || !lurker->Faction->IsPirate || lurker->Runner || !lurker->StakeoutKm.has_value()) continue;
			if (lurker->Battle != nullptr || lurker == world->PilotedShip || !Core::Fleet::IsAt(lurker, root) || !Willing(lurker, jd)) continue;
			auto at = lurker->Orbit->OffsetAt(jd);
			auto marks = cs::List<cs::Ref<Ship>>();
			double guns = 0;
			cs::Ref<Ship> picket = nullptr;
			for (const auto& s : Fleet()->Ships())
			{
				if (s->Destroyed || s->Battle != nullptr || s == world->PilotedShip || s->Derelict || !Core::Fleet::IsAt(s->Controller(), root)) continue;
				if (!Diplomacy()->Hostile(lurker->Faction, s->Faction) || (s->Controller()->Orbit->OffsetAt(jd) - at).Length() > GateAmbushRangeKm) continue;
				if (s->Spec->IsMilitary || s->Faction->IsPirate) guns += s->Combat->Strength();
				else if (!s->IsEscort()) marks.Add(s);
				if (picket == nullptr && RaidDoctrine::IsPicket(s)) picket = s;
			}
			// The band sizes up what is there first (U.SPC-T374): the best mark worth the guns about it; with none, a gate
			// picket that would give it away (U.SPC-T480), or the warships alone, if they are few enough to take on.
			double band = GroupStrength(lurker);
			auto prey = BestMark(marks, guns, band);
			bool silence = picket != nullptr && guns <= band * RaidDoctrine::PicketNerve;
			if (prey == nullptr && !silence && (guns <= 0 || guns > band * RaidDoctrine::BaseNerve)) continue;
			auto b = TryStartBattle(root, lurker->Faction, jd, cs::Nullable<DVec3>(at));
			if (b == nullptr) continue;
			auto gate = GateNear(at);
			if (gate != nullptr) gate->Ambushes++;
			world->Log(jd, cs::Cat(lurker->Faction->Name, " raiders (", lurker->Name, ") lying off the ", (gate != nullptr ? gate->Name : cs::String("gates")), " fall on ") +
				(silence ? cs::Cat("the picket ", picket->Name) : prey != nullptr ? cs::Cat("the ", prey->Name) : cs::String("the warships coming through")), WorldEventKind::Battle, root);
			return;
		}
	}

	cs::Ref<Ship> War::BestMark(const cs::List<cs::Ref<Ship>>& marks, double guns, double band)
	{
		cs::Ref<Ship> best = nullptr;
		double bestScore = cs::Double::NegativeInfinity;
		for (const auto& s : marks)
		{
			double value = RaidDoctrine::PreyValue(s, world->Contracts);
			double heat = RaidDoctrine::Heat(s, world->Contracts);
			if (!RaidDoctrine::WorthTaking(value, heat, guns, band)) continue;
			double score = RaidDoctrine::Score(value, heat, guns, band);
			if (score > bestScore)
			{
				bestScore = score;
				best = s;
			}
		}
		return best;
	}

	cs::Ref<JumpGate> War::BusiestGate(const cs::Ref<Faction>& clan, double jd)
	{
		cs::Ref<JumpGate> best = nullptr;
		double bestScore = 0;
		for (const auto& g : world->Gates)
		{
			if (g->Traffic < MinGateTraffic || !g->Working()) continue;
			double score = g->Traffic * (0.7 + rng.NextDouble() * 0.6);
			if (score > bestScore)
			{
				bestScore = score;
				best = g;
			}
		}
		return best;
	}

	void War::StakeOut(const cs::Ref<Ship>& leader, const cs::Ref<JumpGate>& gate, double untilJd)
	{
		// Each band holds its own spot on the approach, a little off the line in.
		auto axis = gate->PositionKm.Normalized();
		auto side = DVec3::Cross(axis, cs::Math::Abs(axis.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX).Normalized();
		leader->StakeoutKm = gate->ApproachKm() + side * ((rng.NextDouble() - 0.5) * 400);
		leader->StakeoutUntilJd = untilJd;
	}

	bool War::HidesHaven(int body)
	{
		for (const auto& st : Economy()->At(body))
			if (!st->Discovered()) return true;
		return false;
	}

	int War::Shown(const cs::Ref<Battle>& b) { return HidesHaven(b->Body) ? -1 : b->Body; }

	DVec3 War::Spot(const cs::Ref<Battle>& b, int side) { return b->ApproachAxis * (side == 0 ? 13 : -13); }

	void War::Join(const cs::Ref<Battle>& b, const cs::Ref<Ship>& ship, int side, bool late)
	{
		// A ship under direct player control enters a fight only through TryStartPlayerBattle.
		if (ship->Battle != nullptr || ship->Destroyed || ship == world->PilotedShip) return;
		auto groupCentre = late ? b->ApproachAxis * (side == 0 ? 30 : -30) : Spot(b, side);
		// (C# evaluates the DVec3 constructor's arguments left to right; C++ leaves the order unspecified, so draw them first.)
		double jx = rng.NextDouble() - 0.5;
		double jy = rng.NextDouble() - 0.5;
		double jz = (rng.NextDouble() - 0.5) * 0.4;
		auto jitter = DVec3(jx, jy, jz) * 6;
		auto face = -groupCentre;
		if (ship->IsEscort())
		{
			auto leaderUnit = b->UnitOf(ship->Leader);
			if (leaderUnit == nullptr && ship->Leader->Battle == nullptr) Join(b, ship->Leader, side, late);
			if (ship->Battle != nullptr) return;
			leaderUnit = b->UnitOf(ship->Leader);
			if (leaderUnit != nullptr)
			{
				auto slot = ship->FormationSlot * (ship->Leader->LengthKm() * 1.3 + 0.8);
				DVec3 r, up;
				leaderUnit->Basis(r, up);
				auto u = b->AddShip(ship, side, leaderUnit->Pos + r * slot.X + up * slot.Y + leaderUnit->Forward * slot.Z, leaderUnit->Forward);
				u->Leader = leaderUnit;
				u->Slot = slot;
				return;
			}
		}
		b->AddShip(ship, side, groupCentre + jitter, face);
		for (const auto& e : Fleet()->Ships())
			if (e->Leader == ship && !e->Destroyed && e->Battle == nullptr) Join(b, e, side, late);
	}

	void War::OnUnitDestroyed(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		double jd = b->StartJd + b->Seconds / Astro::SecondsPerDay;
		if (u->Ship != nullptr)
		{
			// A merchant or civilian hull raiders destroy rather than rob draws heat on their clan (U.SPC-T374), worked
			// out before the hull is struck off (its jobs and cargo still on it).
			auto raiders = u->Side == 0 || u->Side == 1 ? b->SideLeaders[1 - u->Side] : nullptr;
			double heat = raiders != nullptr && RaidDoctrine::Robs(raiders, u->Ship) ? RaidDoctrine::DestroyHeat(u->Ship, world->Contracts) : 0;
			u->Ship->Destroyed = true;
			int n = 0;
			losses.TryGetValue(u->Faction, n);
			losses[u->Faction] = n + 1;
			Wrecks->Add(u, b->Body, b->Anchor, jd, -1, /*site:*/ b->Site);
			if (heat > 0)
			{
				double drawn = 0;
				HeatDrawn.TryGetValue(raiders, drawn);
				HeatDrawn[raiders] = drawn + heat;
				Heat.Invoke(raiders, heat, jd);
			}
		}
		AbandonShip(b, u, jd);
		BumpDanger(b->Body, u->Ship != nullptr && !u->Ship->Spec->IsMilitary ? 2.0 : 0.6);
	}

	void War::AbandonShip(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double jd)
	{
		auto c = u->Combat;
		if (c == nullptr) return;
		double aboard = c->Interior->Crew() + c->Interior->Marines();
		if (aboard < 0.5) return;
		bool reactor = false;
		for (const auto& sub : c->Subsystems)
			if (sub->Kind() == SubsystemKind::Reactor && sub->Destroyed()) reactor = true;
		auto owners = u->Ship != nullptr && u->Ship->Faction != nullptr ? u->Ship->Faction : u->Faction;
		cs::String name = u->Kind == UnitKind::Fighter ? cs::Cat(owners->Name, " fighter pilots") : u->Name();
		// (C#'s null merge key is an empty one here.)
		cs::String merge = u->Kind == UnitKind::Fighter ? cs::Cat("battle:", b->Id, ":fighters:", owners->Name) : cs::String();
		auto haven = owners->IsPirate && u->Ship != nullptr ? HomeHaven(u->Ship) : nullptr;
		auto pod = Crews->Launch(name, owners, aboard, c->Spec->Crew + c->Spec->Marines, reactor ? AbandonCause::ReactorLost : AbandonCause::Destroyed,
			b->Body, b->Anchor, u->Pos, b->Site, jd, merge, haven);
		// An outlaw captain may reach the pods; a captain in its brig breaks out into them (U.SPC-T63).
		if (u->Kind != UnitKind::Fighter && world->Outlaws != nullptr) world->Outlaws->OnHullLost(b, u, pod, reactor, jd);
		c->Interior->TakeCrew(c->Interior->Crew());
		c->Interior->TakeMarines(c->Interior->Marines());
	}

	// ---------------------------------------------------------------- after the battle

	void War::Resolve(const cs::Ref<Battle>& b, double jd)
	{
		Battles.Remove(b);
		Ended.Add(b);
		if (Ended.Count() > 30) Ended.RemoveAt(0);
		BattleEnded.Invoke(b, jd);
		// Breacher rounds still in space with their teams alive are left drifting, for a tow to bring in or pirates to
		// salvage (U.SPC-T519).
		for (const auto& pod : DriftingRounds::CastAdrift(b, Crews, jd))
			world->Log(jd, cs::Cat("A ", pod->Name, " is left drifting after the fight at ", b->Name, ": ", cs::Fmt(pod->People, "0"), " ", pod->Owners->Name, " marines ",
				"alive inside, waiting on a tow"), WorldEventKind::Loss, PodBody(pod));

		auto lost = cs::NewArray<int>(2);
		auto fled = cs::NewArray<int>(2);
		auto lootTonnes = 0.0;
		auto lootValue = 0.0;
		cs::Ref<Faction> lootClan = nullptr;
		// Ships already let go of this battle (a pilot who left it) have moved on: whatever they are
		// doing now -- flying, another fight -- is not this battle's to reset.
		auto left = cs::HashSet<cs::Ref<BattleUnit>>();
		for (const auto& u : b->Units)
			if (u->Ship != nullptr && u->Ship->Battle != b) left.Add(u);
		for (const auto& u : b->Units)
		{
			if (u->Side < 0 || u->Side > 1) continue;
			if (u->Dead && u->Kind != UnitKind::Fighter) lost[u->Side]++;
			if (u->Fled) fled[u->Side]++;
			if (u->Kind == UnitKind::Platform && u->Dead) StrikeOff(u);
			// Boarders still fighting aboard anything that wasn't taken (a platform too) are gone with the battle.
			if (!u->Captured && !left.Contains(u)) u->Combat->Interior->BoardersGone();
			if (u->Ship == nullptr || left.Contains(u)) continue;
			auto ship = u->Ship;
			ship->Battle = nullptr;
			if (u->Dead)
			{
				if (ship->HasCargo)
				{
					Economy()->Release(ship);
					if (b->SideLeaders[1 - u->Side] != nullptr && b->SideLeaders[1 - u->Side]->IsPirate)
					{
						lootClan = b->SideLeaders[1 - u->Side];
						lootTonnes += ship->CargoTonnes * 0.6;
						lootValue += ship->CargoTonnes * 0.6 * GoodInfo::Of(ship->CargoGood)->BasePrice;
						DeliverLoot(lootClan, ship->CargoGood, ship->CargoTonnes * 0.6, b);
						// What they hauled off is no longer in the wreck to salvage.
						if (auto wreck = Wrecks->Of(ship))
							for (int i = 0; i < wreck->Salvage.Count(); i++)
								if (wreck->Salvage[i].Good == ship->CargoGood)
									wreck->Salvage[i] = SalvageStack(ship->CargoGood, wreck->Salvage[i].Tonnes * 0.4);
					}
					ship->HasCargo = false;
				}
				ship->CargoDestination = nullptr;
				ship->Cargo = cs::String();
			}
		}
		for (const auto& u : b->Units)
			if (u->Ship != nullptr && u->Dead) Succession(u->Ship);
		for (const auto& u : b->Units)
		{
			cs::Ref<Faction> captor;
			if (!captures.TryGetValue(u, captor)) continue;
			captures.Remove(u);
			if (!u->Captured || u->Dead) continue;
			// A hull allies took together is theirs to settle first (U.SPC-T392): whoever keeps it disposes of it.
			auto keeper = SettleJointPrize(b, u, captor, jd);
			if (keeper != nullptr) DisposeOfPrize(b, u, keeper, jd);
		}

		// Survivors go back to their own schedules; those that ran, or lost, withdraw to refit.
		for (const auto& u : b->Units)
		{
			if (u->Ship == nullptr || u->Dead || u->Ship->IsEscort() || u->Ship->Destroyed || u->Ship->Derelict) continue;
			// The player's ship stays the player's: its schedule is set again only on release.
			if (u->Ship == world->PilotedShip || left.Contains(u)) continue;
			bool beaten = u->Fled || (b->Winner >= 0 && u->Side != b->Winner);
			// Pulled out of a lane: back in and on their way. Raiders who won take the loot home;
			// beaten ones run.
			if (u->Ship->Phase == ShipPhase::Adrift)
			{
				Fleet()->ResumeLane(u->Ship, jd);
				continue;
			}
			if (u->Ship->Phase == ShipPhase::Lurk)
			{
				if (beaten || b->Winner == u->Side)
				{
					retreating[u->Ship] = jd + (beaten ? 6 : 4);
					Fleet()->ResumeLane(u->Ship, jd);
				}
				continue;
			}
			if (beaten) retreating[u->Ship] = jd + 6;
			// The beaten leave at once; everyone else keeps their plans unless the fight overran them.
			if (beaten || u->Ship->PhaseEndJd <= jd || !(u->Ship->Spec->IsMilitary || u->Ship->Faction->IsPirate)) Fleet()->ScheduleNext(u->Ship, jd);
		}
		// A navy that beats raiders off may follow the survivors back to a hidden haven.
		auto victor = b->Winner >= 0 ? b->SideLeaders[b->Winner] : nullptr;
		if (victor != nullptr && !victor->IsPirate)
			for (const auto& u : b->Units)
			{
				if (u->Ship == nullptr || u->Dead || u->Ship->IsEscort() || !u->Ship->Faction->IsPirate || u->Side == b->Winner) continue;
				if (rng.NextDouble() < TrailChance) TrailHome(u->Ship, victor, jd);
			}
		lastBattleEndJd[b->Body] = jd;
		RecoverWings(b, jd);

		cs::String winner = b->Winner >= 0 ? cs::Cat(Describe(b, b->Winner), " hold the field") : cs::String("both sides break off");
		cs::String text = cs::Cat(b->Name, " ends: ", winner, ". Losses ", Short(b, 0), " ", lost[0], ", ", Short(b, 1), " ", lost[1]);
		if (lootTonnes > 0) text += cs::Cat("; pirates take ", cs::Fmt(lootTonnes, "N0"), " t of cargo");
		world->Log(jd, text, WorldEventKind::Battle, Shown(b));
		if (lootValue > 0 && lootClan != nullptr)
		{
			lootClan->Treasury += lootValue * 0.3;
			Plundered.Invoke(lootClan, lootValue, jd);
		}
	}

	void War::RecoverWings(const cs::Ref<Battle>& b, double jd)
	{
		int lost = 0;
		auto putIn = cs::Dictionary<cs::String, int>();
		auto PutIn = [&putIn](const cs::String& at)
		{
			int n = 0;
			putIn[at] = (putIn.TryGetValue(at, n) ? n : 0) + 1;
		};
		for (const auto& u : b->Units)
		{
			if (u->Kind != UnitKind::Fighter || u->Fighter == nullptr) continue;
			// Already landed on the carrier or station it found when its own was lost.
			if (u->Docked && u->Fighter->Home != nullptr)
			{
				StrandHull(u, u->Fighter->Home);
				PutIn(u->Fighter->Home->Name());
				continue;
			}
			if (!u->Present()) continue;
			auto home = u->Fighter->LandsOn();
			if (!StrandedWings::CanLandOn(home))
			{
				home = StrandedWings::Haven(b, u);
				if (home != nullptr) u->Fighter->Home = home;
			}
			if (home != nullptr)
			{
				FighterAI::TakeAboard(home, u);
				u->Docked = true;
				StrandHull(u, home);
				if (u->Fighter->Home != nullptr) PutIn(home->Name());
				continue;
			}
			auto port = StrandedWings::Port(b, u->Faction, Economy(), Diplomacy());
			if (port != nullptr)
			{
				u->Fled = true;
				if (u->CraftHull != nullptr) CarrierWings::Strand(port, u->CraftHull);
				PutIn(port->Name);
				continue;
			}
			// Nowhere to land: the craft is lost and its pilot ejects.
			u->Dead = true;
			lost++;
			AbandonShip(b, u, jd);
		}
		if (putIn.Count() == 0 && lost == 0) return;
		auto parts = cs::List<cs::String>();
		for (auto [key, value] : putIn) parts.Add(cs::Cat(value, " put in at ", key));
		if (lost > 0) parts.Add(cs::Cat(lost, " lost with nowhere to land, their pilots ejecting"));
		world->Log(jd, cs::Cat(b->Name, ": craft stranded by the loss of their carriers: ") + cs::Strings::Join(", ", parts), WorldEventKind::Battle, Shown(b));
	}

	void War::StrandHull(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& home)
	{
		auto hull = u->CraftHull;
		if (hull == nullptr || home->Platform == nullptr || CarrierWings::DockedIn(home->Combat, hull)) return;
		CarrierWings::Strand(home->Platform, hull);
	}

	void War::DeliverLoot(const cs::Ref<Faction>& clan, Good g, double tonnes, const cs::Ref<Battle>& b)
	{
		if (tonnes <= 0) return;
		auto haven = LiveHavenOf(clan);
		if (haven != nullptr)
		{
			haven->Stock[static_cast<int>(g)] += tonnes;
			return;
		}
		if (b == nullptr) return;
		for (const auto& u : b->Units)
		{
			auto c = u->Ship != nullptr ? u->Ship->Controller() : nullptr;
			if (c == nullptr || u->Dead || c->Destroyed || c->Faction != clan || c->Runner) continue;
			if (c->Plunder.Count() == 0) c->Plunder = cs::NewArray<double>(GoodInfo::Count);
			c->Plunder[static_cast<int>(g)] += tonnes;
			return;
		}
	}

	cs::Ref<Settlement> War::LiveHavenOf(const cs::Ref<Faction>& clan)
	{
		for (const auto& s : Economy()->Settlements)
			if (s->Kind == SettlementKind::Haven && !s->Fallen && s->Owner == clan) return s;
		return nullptr;
	}

	void War::UnloadPlunder(const cs::Ref<Ship>& ship, int body)
	{
		if ((ship == nullptr || ship->Plunder.Count() == 0) || ship->Runner) return;
		for (const auto& st : Economy()->At(body))
		{
			if (st->Kind != SettlementKind::Haven || st->Fallen || st->Owner != ship->Faction) continue;
			for (int i = 0; i < GoodInfo::Count; i++) st->Stock[i] += ship->Plunder[i];
			ship->Plunder.Clear();
			return;
		}
	}

	void War::HandOverCommand(const cs::Ref<Ship>& leader, bool keepHunter)
	{
		if (leader == nullptr || leader->IsEscort()) return;
		Succession(leader, keepHunter);
	}

	void War::Forget(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr) return;
		Orders.Remove(ship);
		PriorOrders.Remove(ship);
		retreating.Remove(ship);
		laneSeen.Remove(ship);
		pulseReady.Remove(ship);
	}

	void War::PruneTracking(double jd)
	{
		// (C#'s `List<Ship> gone = null` grown on demand: an empty list here, removed from only when filled.)
		cs::List<cs::Ref<Ship>> gone;
		for (auto [key, value] : retreating)
			if (key->Destroyed || key->GateDeparted || jd >= value) gone.Add(key);
		for (const auto& s : gone) retreating.Remove(s);
		gone.Clear();
		for (auto [key, value] : pulseReady)
			if (key->Destroyed || key->GateDeparted || jd >= value) gone.Add(key);
		for (const auto& s : gone) pulseReady.Remove(s);
		// Orders has its own cleanup on a leader's death in battle (Succession), but a ship that retires through a
		// gate while it still holds one (GateTransit's instant crossing of the player's own flown ship never calls
		// Forget, unlike InterstellarTraffic's AI crossing) would otherwise sit in this book forever too.
		gone.Clear();
		for (auto [key, value] : Orders)
			if (key->Destroyed || key->GateDeparted) gone.Add(key);
		for (const auto& s : gone) Orders.Remove(s);
		gone.Clear();
		for (auto [key, value] : PriorOrders)
			if (key->Destroyed || key->GateDeparted) gone.Add(key);
		for (const auto& s : gone) PriorOrders.Remove(s);
	}

	void War::Succession(const cs::Ref<Ship>& dead, bool keepHunter, const cs::Ref<Ship>& chosen)
	{
		if (dead->IsEscort()) return;
		cs::Ref<Ship> heir = chosen != nullptr && chosen->Leader == dead && !chosen->Destroyed ? chosen : nullptr;
		bool command = false;
		if (heir == nullptr)
			for (const auto& s : Fleet()->Ships())
			{
				if (s->Leader != dead || s->Destroyed) continue;
				bool c = FleetCoordination::IsCommandShip(s);
				if (heir == nullptr || (c && !command) || (c == command && s->Combat->Spec->Hull > heir->Combat->Spec->Hull))
				{
					heir = s;
					command = c;
				}
			}
		if (heir == nullptr)
		{
			// A hunt group wiped out abroad: nobody comes home to inherit the row (U.SPC-T588).
			if (dead->Destroyed && dead->Voyage != nullptr) dead->Voyage->LostWithAllHands();
			Orders.Remove(dead);
			PriorOrders.Remove(dead);
			return;
		}
		heir->Leader = nullptr;
		// A merchant's hired wing (U.SPC-T275) has no patrol route to take over: it goes its own way.
		heir->Role = dead->Route.Count() > 0 ? ShipRole::Patrol : ShipRole::Free;
		heir->Route = dead->Route;
		heir->RouteIndex = dead->RouteIndex;
		heir->Phase = dead->Phase;
		// The old flagship is not always gone (a capture leaves it flying on under new colours): take
		// over a little way along its orbit rather than sharing its exact spot with it indefinitely.
		heir->Orbit = OwnSpot(dead->Orbit, heir, dead);
		heir->DepartureOrbit = dead->DepartureOrbit;
		heir->DestinationBody = dead->DestinationBody;
		heir->OriginBody = dead->OriginBody;
		heir->PhaseStartJd = dead->PhaseStartJd;
		heir->PhaseEndJd = dead->PhaseEndJd;
		heir->FrameBody = dead->FrameBody;
		heir->Path = dead->Path;
		heir->PathCumulative = dead->PathCumulative;
		heir->PathLength = dead->PathLength;
		heir->AccelKmPerDay2 = dead->AccelKmPerDay2;
		heir->Conic = dead->Conic;
		heir->GravityLossKmS = dead->GravityLossKmS;
		heir->DepartureVelocity = dead->DepartureVelocity;
		heir->ArrivalVelocity = dead->ArrivalVelocity;
		heir->Journey = dead->Journey;
		heir->JourneyIndex = dead->JourneyIndex;
		heir->FinalBody = dead->FinalBody;
		heir->LaneLeg = dead->LaneLeg;
		heir->LaneFromOffset = dead->LaneFromOffset;
		heir->LaneToOffset = dead->LaneToOffset;
		heir->LaneLengthKm = dead->LaneLengthKm;
		heir->LaneStartProgress = dead->LaneStartProgress;
		heir->LaneEndProgress = dead->LaneEndProgress;
		heir->LaneHold = dead->LaneHold;
		heir->LaneHoldSide = dead->LaneHoldSide;
		// A band staked out off a gate keeps its watch under the new flagship (GateAmbushes looks only at a
		// leader's stake-out); the old one, if it flies on, keeps none.
		heir->StakeoutKm = dead->StakeoutKm;
		heir->StakeoutUntilJd = dead->StakeoutUntilJd;
		dead->StakeoutKm = std::nullopt;
		CopyRest(dead, heir);
		// The old flagship's hold of plunder goes on under the new one, not stranded on a hull that no
		// longer runs the group's schedule (U.SPC-T308). A flagship shot out from under the band takes its
		// hold down with it.
		if (!dead->Destroyed) MovePlunder(dead, heir);
		// An interstellar voyage goes on under the new flagship; one held at its gate goes again.
		heir->Voyage = dead->Voyage;
		heir->GateKm = dead->GateKm;
		if (heir->Voyage != nullptr) heir->Voyage->AtGate = false;
		dead->Voyage = nullptr;
		dead->GateKm = std::nullopt;
		for (const auto& s : Fleet()->Ships())
			if (s->Leader == dead && s != heir) s->Leader = heir;
		cs::Ref<GroupOrder> o;
		if (Orders.TryGetValue(dead, o)) Orders[heir] = o;
		// The objective its polity's command gave the group goes on under the heir (U.SPC-T423).
		if (world->Polities != nullptr) world->Polities->Command->OnSuccession(dead, heir);
		cs::Ref<GroupOrder> prior;
		if (PriorOrders.TryGetValue(dead, prior)) PriorOrders[heir] = prior;
		Orders.Remove(dead);
		PriorOrders.Remove(dead);
		if (!keepHunter && world->Outlaws != nullptr) world->Outlaws->OnSuccession(dead, heir);
	}
}
