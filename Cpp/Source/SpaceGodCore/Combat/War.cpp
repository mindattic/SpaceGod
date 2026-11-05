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
#include "Combat/CargoPods.h" // US-T95
#include "Economy/Economy.h"
#include "Embodiment/SelfDestruct.h"
#include "Sensors/SensorPosture.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Infrastructure/Infrastructure.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Sensors/Sensors.h"
#include "Settings/GameSetting.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"
#include "Systems/LaneUpkeep.h"
#include "Systems/Polities.h"
#include "Systems/StrategicCommand.h"

namespace SpaceGod::Core
{
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
		// The self-destructs counting down (US-T114): once a tick each, on the sim clock, whoever holds the hull.
		std::vector<const ShipCombat*> counted;
		for (int i = Battles.Count() - 1; i >= 0; i--)
		{
			auto b = Battles[i];
			if (b->Fresh)
			{
				b->Fresh = false;
				continue;
			}
			b->Advance(seconds, world->Coarse());
			SelfDestruct::Advance(b, seconds, counted);
			// The player who has left the fight (flown out of it, or let go for the cargo) is free of it
			// now, not when the rest have finished: free to cruise, take the gate or engage elsewhere.
			for (const auto& u : b->Units)
				if (u->Fled && u->PlayerControlled() && u->Ship != nullptr && u->Ship->Battle == b) u->Ship->Battle = nullptr;
			if (b->Over) Resolve(b, jd);
		}
		SelfDestruct::AdvanceOutside(world, seconds, counted);
		// The helm's ping windows, a dark hull's cooling and a restart (US-T93), on the same clock.
		SensorPosture::Advance(world, seconds);
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
			double laneDanger = 0; // C#'s local `danger`, renamed: it hides the `danger` field (C4458)
			lanes0()->Danger.TryGetValue(lane, laneDanger);
			lanes0()->SetDanger(lane, laneDanger + 3);
			world->Log(jd, cs::Cat(lurker->Faction->Name, " raiders (", lurker->Name, ") spring an ambush on the ", lane->Name, " lane: ", pulled), WorldEventKind::Battle, lane->A);
			BumpDanger(lane->A, 1);
			BumpDanger(lane->B, 1);
		}
		BattleStarted.Invoke(b);
	}

	bool War::Forewarned(const cs::Ref<Ship>& victim, const cs::Ref<Ship>& lurker, double /*jd*/)
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
		double reach = suite.ReachKm(EmitterSignature);
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
		b->Launch = [this](cs::Ref<Battle> battle, cs::Ref<BattleUnit> carrier)
		{
			// A carrier the player is flying into the fight holds its wing aboard for the helm to launch squadron by squadron
			// (US-T106); every other carrier launches as it always has.
			if (carrier->Ship != nullptr && carrier->Ship == world->PilotedShip)
			{
				carrier->WingHeld = true;
				return;
			}
			FighterAI::Launch(battle, carrier);
		};
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
		if (ship == nullptr) return false;
		return LoseOutsideBattle(ship, body, offsetKm, jd, cs::Cat("The ", ship->Name, " breaks up in a collision with ", what));
	}

	bool War::LoseOutsideBattle(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd, const cs::String& logLine)
	{
		if (ship == nullptr || ship->Destroyed || ship->Battle != nullptr) return false;
		auto wreck = Wrecks->Add(ship, body, offsetKm, jd);
		(void)wreck;
		Scuttle(ship);
		world->Log(jd, logLine, WorldEventKind::Loss, body);
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

	cs::Ref<JumpGate> War::BusiestGate(const cs::Ref<Faction>& /*clan*/, double /*jd*/)
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
						double share = ship->CargoTonnes * 0.6;
						// US-T95: the winners tractor their share in from the pods the hull spilled, then strip its hold; what
						// they hauled off is no longer there to salvage. A pilot who swept the pods first left them less.
						if (auto wreck = Wrecks->Of(ship))
						{
							auto spilledPods = Wrecks->SpilledPods();
							double there = spilledPods->SpilledFrom(wreck, ship->CargoGood);
							for (const auto& keptStack : wreck->Salvage)
								if (keptStack.Good == ship->CargoGood) there += keptStack.Tonnes;
							if (there < share) share = cs::Math::Max(0.0, there);
							spilledPods->Haul(wreck, ship->CargoGood, share);
						}
						lootTonnes += share;
						lootValue += share * GoodInfo::Of(ship->CargoGood)->BasePrice;
						DeliverLoot(lootClan, ship->CargoGood, share, b);
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

	// ---------------------------------------------------------------- orders

	namespace
	{
		// C#'s `new GroupOrder { Kind = k, Body = body }`.
		cs::Ref<GroupOrder> Order(OrderKind kind, int body)
		{
			auto o = cs::New<GroupOrder>();
			o->Kind = kind;
			o->Body = body;
			return o;
		}
	}

	int War::ChooseNext(const cs::Ref<Ship>& leader, int here, double jd)
	{
		// Bounty hunters go where their contracts take them (U.SPC-T62).
		if (Hunter(leader)) return world->Outlaws->ChooseNext(leader, here, jd);
		// A navy's supply convoy is led by a freighter but takes its orders from the navy (U.SPC-T375).
		if (!leader->Spec->IsMilitary && !leader->Faction->IsPirate && !leader->Convoy) return -1;
		auto order = Decide(leader, here, jd);
		cs::Ref<GroupOrder> current;
		if (!Orders.TryGetValue(leader, current) || current->Kind != order->Kind || current->Body != order->Body)
		{
			if (current != nullptr) PriorOrders[leader] = current;
			order->SinceJd = jd;
			Orders[leader] = order;
		}
		return order->Body;
	}

	double War::Dwell(const cs::Ref<Ship>& ship, double normal)
	{
		double until = 0;
		if (retreating.TryGetValue(ship, until) && lastJd < until) return 0.05;
		cs::Ref<GroupOrder> o;
		if (!Orders.TryGetValue(ship, o)) return normal;
		switch (o->Kind)
		{
		case OrderKind::Repair:
		case OrderKind::Siege:
		case OrderKind::Assault:
		case OrderKind::Defend: return 1.0;
		case OrderKind::Raid: return 1.5 + rng.NextDouble();
		case OrderKind::GateWatch:
		case OrderKind::HoldGate: return 0.5;
		case OrderKind::Blockade: return 1.0;
		case OrderKind::Escort: return 0.1;
		case OrderKind::Explore: return 0.3;
		default: return normal;
		}
	}

	cs::Ref<GroupOrder> War::Decide(const cs::Ref<Ship>& leader, int here, double jd)
	{
		auto f = leader->Faction;
		double until = 0;
		bool withdrawing = retreating.TryGetValue(leader, until) && jd < until;
		if (f->IsPirate) return DecidePirate(leader, here, jd, withdrawing);
		// A fresh plan: a lane picket planned earlier and not yet reached no longer stands (U.SPC-T479).
		leader->LurkLane = nullptr;
		if (leader->PicketGate != nullptr) return DecidePicket(leader, here, jd, withdrawing);
		if (Corvettes::Stationed(leader)) return DecideCorvette(leader, here, jd, withdrawing);
		if (leader->PatrolWing) return DecideWing(leader, here, jd, withdrawing);
		// A fresh plan: a gate it was holding is held again only if it is still the order (U.SPC-T374).
		auto held = leader->StakeoutKm;
		leader->StakeoutKm = std::nullopt;

		if (withdrawing || GroupDamage(leader) > 0.22 || MissilesLow(leader) || !leader->Combat->Armed() || HangarsLow(leader) || MarinesLow(leader))
		{
			int yard = NearestYard(f, here, jd);
			if (yard >= 0) return Order(OrderKind::Repair, yard);
		}

		// Its polity's command has given it an objective (U.SPC-T423): that comes before its own plans.
		if (auto directed = Directed(leader, here, jd)) return directed;

		// Defend: an own port with enemies at it.
		int threatened = -1;
		double bestDays = cs::Double::MaxValue;
		for (const auto& s : Economy()->Settlements)
		{
			if (s->Owner != f) continue;
			if (!EnemiesAt(s->Body, f)) continue;
			double days = s->Body == here ? 0 : Fleet()->EstimateTravelDays(leader, here, s->Body, jd);
			if (days < bestDays && days < 40)
			{
				bestDays = days;
				threatened = s->Body;
			}
		}
		if (threatened >= 0) return Order(OrderKind::Defend, threatened);

		// A gate picket has warned of hostiles come in at one of its own gates (U.SPC-T480): command sends a group
		// strong enough to meet them there while they are still at the gate, before they are loose in the system.
		if (!IsHomeGuard(leader) && GateWarned(leader, here, jd, held) != nullptr)
			return Order(OrderKind::HoldGate, Catalog()->Root()->Index);

		// At a haven it found, with the garrison broken: stay and finish the job.
		for (const auto& s : Economy()->At(here))
			if (Assailable(s, f) && s->Platforms.Count() == 0)
				return Order(OrderKind::Assault, here);

		// Besieging: stay until the port falls or defenders show up.
		for (const auto& s : Economy()->At(here))
			if (Diplomacy()->Hostile(f, s->Owner) && !s->Owner->IsPirate && s->Platforms.Count() == 0 && s->DaysSinceCapture > CaptureGraceDays && AtWar(f))
				return Order(OrderKind::Siege, here);

		bool atWar = AtWar(f);
		// Cut the enemy's lanes (U.SPC-T481): fall on a lane tender of a power it is at war with, if one is in reach.
		if (atWar && !IsHomeGuard(leader) && world->LaneUpkeep != nullptr)
		{
			int tender = world->LaneUpkeep->HuntTarget(leader, here, jd);
			if (tender >= 0) return Order(OrderKind::Hunt, tender);
		}
		if (atWar && !IsHomeGuard(leader))
		{
			double strength = GroupStrength(leader);
			int target = -1;
			double best = 0;
			for (const auto& s : Economy()->Settlements)
			{
				if (!Diplomacy()->Hostile(f, s->Owner) || s->Owner->IsPirate) continue;
				double defence = DefenceAt(s->Body, f);
				if (strength < defence * 1.25) continue;
				double days = Fleet()->EstimateTravelDays(leader, here, s->Body, jd);
				double value = (s->Population / 1e5 + 2) * (s->HasShipyard() ? 2 : 1) / (defence + 200) / (days + 5);
				if (value > best)
				{
					best = value;
					target = s->Body;
				}
			}
			if (target >= 0) return Order(OrderKind::Attack, target);
		}

		// Hunt pirates working an own port -- that its ships or stations have seen there (U.SPC-T66).
		auto at = world->Sensors != nullptr ? world->Sensors->HelioOf(leader, jd) : DVec3();
		for (const auto& s : Economy()->Settlements)
			if (s->Owner == f && s->Body != here && PiratesSeenAt(s->Body, f, at, jd))
				return Order(OrderKind::Hunt, s->Body);

		// Hold one of its own jump gates that raiders are known to be lying off (U.SPC-T374): command sends a group
		// strong enough to clear them, which takes station on the gate's approach and fights them there.
		if (!IsHomeGuard(leader) && GateToHold(leader, here, at, jd, held) != nullptr)
			return Order(OrderKind::HoldGate, Catalog()->Root()->Index);

		// Strike a hidden haven this navy has found, if the group can take its garrison.
		if (!IsHomeGuard(leader))
			if (auto mark = AssaultTarget(leader, here, jd))
				return Order(OrderKind::Assault, mark->Body);

		// A group with a field projector fitted to one of its hulls may hold a picket on a troubled lane out of the system (U.SPC-T479, U.SPC-T507).
		if (!IsHomeGuard(leader))
			if (auto picket = LanePicket(leader, here, PatrolWings::Ports(world, f), false)) return picket;

		return Order(OrderKind::Patrol, NextPatrol(leader, here));
	}

	cs::Ref<GroupOrder> War::Directed(const cs::Ref<Ship>& leader, int here, double jd)
	{
		auto command = world->Polities != nullptr ? world->Polities->Command : nullptr;
		auto o = command != nullptr ? command->ObjectiveOf(leader) : nullptr;
		if (o == nullptr) return nullptr;
		auto f = leader->Faction;
		if (o->Where != world) return Order(OrderKind::Patrol, here);
		switch (o->Kind)
		{
		case ObjectiveKind::Take:
		{
			cs::Ref<Settlement> target = nullptr;
			double bestDays = cs::Double::MaxValue;
			for (const auto& s : Economy()->Settlements)
			{
				if (!command->Enemy(f, s) || (o->Body >= 0 ? s->Body != o->Body : s->Kind == SettlementKind::Haven)) continue;
				double days = s->Body == here ? 0 : Fleet()->EstimateTravelDays(leader, here, s->Body, jd);
				if (days < bestDays)
				{
					bestDays = days;
					target = s;
				}
			}
			if (target == nullptr)
			{
				// Nothing left there held by an enemy: taken (or the war is over and it was given back); it holds the place.
				int held = o->Body;
				if (held < 0)
					for (const auto& s : Economy()->Settlements)
						if (s->Owner == f && !s->Fallen && (held < 0 || s->Population > PopulationAt(held, f))) held = s->Body;
				if (held < 0 || !OwnsAt(held, f))
				{
					// Not taken by it: the war that made it an enemy's is over.
					command->Complete(leader, world, jd, cs::Cat("The ", leader->Name, "'s group stands down: nothing there is an enemy's to take now"));
					return nullptr;
				}
				command->Taken(leader, world, held, jd);
				return Order(OrderKind::Defend, command->ObjectiveOf(leader)->Body);
			}
			if (target->Kind == SettlementKind::Haven) return Order(OrderKind::Assault, target->Body);
			if (target->Body == here && target->Platforms.Count() == 0 && target->DaysSinceCapture > CaptureGraceDays)
				return Order(OrderKind::Siege, here);
			return Order(OrderKind::Attack, target->Body);
		}
		case ObjectiveKind::Hold:
			return Order(OrderKind::Defend, o->Body >= 0 ? o->Body : here);
		case ObjectiveKind::Blockade:
		{
			for (const auto& s : Economy()->At(o->Body))
				if (command->Blockadable(f, s)) return Order(OrderKind::Blockade, o->Body);
			command->Complete(leader, world, jd, cs::Cat("The ", leader->Name, "'s group lifts its blockade of ", (*Catalog())[o->Body]->Name, ": the port is no enemy's now"));
			return nullptr;
		}
		case ObjectiveKind::Escort:
		{
			auto charge = o->Charge != nullptr ? o->Charge->Controller() : nullptr;
			if (charge == nullptr || charge->Destroyed || !Fleet()->IsPresent(charge)) return Order(OrderKind::Patrol, here);
			int bound = BoundFor(charge);
			return Order(OrderKind::Escort, bound >= 0 ? bound : here);
		}
		case ObjectiveKind::Explore:
		{
			auto worlds = cs::List<int>();
			if (o->Body >= 0) worlds.Add(o->Body);
			else
			{
				for (int b : world->FreeShipPorts())
					if (!worlds.Contains(b)) worlds.Add(b);
				for (const auto& s : Economy()->Settlements)
					if (!worlds.Contains(s->Body)) worlds.Add(s->Body);
			}
			if (worlds.Contains(here) && !o->Visited.Contains(here)) o->Visited.Add(here);
			int next = -1;
			double nearest = cs::Double::MaxValue;
			auto from = world->Ephemeris->HelioAt(here, jd);
			for (int b : worlds)
			{
				if (o->Visited.Contains(b)) continue;
				double d = (world->Ephemeris->HelioAt(b, jd) - from).Length();
				if (d < nearest)
				{
					nearest = d;
					next = b;
				}
			}
			if (next < 0)
			{
				command->Complete(leader, world, jd, cs::Cat("The ", leader->Name, "'s group has explored every world it was sent to (", o->Visited.Count(), ") and stands down"));
				return nullptr;
			}
			return Order(OrderKind::Explore, next);
		}
		}
		return nullptr;
	}

	bool War::OwnsAt(int body, const cs::Ref<Faction>& f)
	{
		for (const auto& s : Economy()->At(body))
			if (s->Owner == f && !s->Fallen) return true;
		return false;
	}

	double War::PopulationAt(int body, const cs::Ref<Faction>& f)
	{
		double p = 0;
		for (const auto& s : Economy()->At(body))
			if (s->Owner == f && !s->Fallen) p += s->Population;
		return p;
	}

	int War::BoundFor(const cs::Ref<Ship>& c)
	{
		if (c->Battle != nullptr) return c->Battle->Body;
		if (c->Phase == ShipPhase::Transit || c->Phase == ShipPhase::Lane || c->Phase == ShipPhase::Lurk || c->Phase == ShipPhase::Adrift)
			// (C#'s `c.Journey != null`: a C++ journey is never null, so an empty one counts as none.)
			return c->FinalBody >= 0 && c->Journey.Count() > 0 ? c->FinalBody : c->DestinationBody;
		if ((c->Phase == ShipPhase::Queued || c->Planetside()) && c->Ring != nullptr) return c->Ring->Body;
		return c->Orbit != nullptr ? c->Orbit->Body : -1;
	}

	cs::Ref<JumpGate> War::GateToHold(const cs::Ref<Ship>& leader, int here, DVec3 at, double jd, cs::Nullable<DVec3> held)
	{
		auto f = leader->Faction;
		int root = Catalog()->Root()->Index;
		if (held.has_value() && jd < leader->StakeoutUntilJd)
		{
			auto spot = *held;
			auto holding = GateNear(spot);
			if (holding != nullptr && holding->Owner == f)
			{
				leader->StakeoutKm = spot;
				return holding;
			}
		}
		if (world->Gates.Count() == 0) return nullptr;
		double strength = GroupStrength(leader);
		cs::Ref<JumpGate> best = nullptr;
		double bestBand = 0;
		for (const auto& g : world->Gates)
		{
			if (g->Owner != f) continue;
			double band = RaidersOffGate(g, f, at, jd);
			if (band <= 0 || strength < band * 1.25) continue;
			if (band > bestBand)
			{
				bestBand = band;
				best = g;
			}
		}
		if (best == nullptr) return nullptr;
		double days = here == root ? 0 : Fleet()->EstimateTravelDays(leader, here, root, jd);
		if (days > 30) return nullptr;
		StakeOut(leader, best, jd + days + HoldGateDays);
		return best;
	}

	double War::WarnedOffGate(const cs::Ref<JumpGate>& gate, const cs::Ref<Faction>& f, double jd)
	{
		int root = Catalog()->Root()->Index;
		double total = 0;
		auto counted = cs::HashSet<cs::Ref<Ship>>();
		for (const auto& w : PicketWarnings)
		{
			if (w->Gate != gate || w->Owner != f || jd - w->Jd > GatePickets::WarningDays || jd < w->Jd) continue;
			auto s = w->Intruder != nullptr ? w->Intruder->Controller() : nullptr;
			if (s == nullptr || s->Destroyed || !Fleet()->IsPresent(s) || !counted.Add(s) || !Diplomacy()->Hostile(f, s->Faction)) continue;
			if (!Core::Fleet::IsAt(s, root) || s->Orbit == nullptr || (s->Orbit->OffsetAt(jd) - gate->PositionKm).Length() > GateFightRangeKm) continue;
			if (s->Faction->IsPirate && s->StakeoutKm.has_value() && GateNear(*s->StakeoutKm) == gate) continue;
			total += GroupStrength(s);
		}
		return total;
	}

	cs::Ref<JumpGate> War::GateWarned(const cs::Ref<Ship>& leader, int here, double jd, cs::Nullable<DVec3> held)
	{
		auto f = leader->Faction;
		if (world->Gates.Count() == 0 || PicketWarnings.Count() == 0) return nullptr;
		double strength = GroupStrength(leader);
		cs::Ref<JumpGate> best = nullptr;
		double bestThreat = 0;
		for (const auto& g : world->Gates)
		{
			if (g->Owner != f) continue;
			double threat = WarnedOffGate(g, f, jd);
			if (threat <= 0) continue;
			double forts = 0;
			for (const auto& fort : g->Forts) forts += fort->Strength();
			if (strength + forts < threat * 1.25) continue;
			if (threat > bestThreat)
			{
				bestThreat = threat;
				best = g;
			}
		}
		if (best == nullptr) return nullptr;
		int root = Catalog()->Root()->Index;
		if (held.has_value() && GateNear(*held) == best)
		{
			leader->StakeoutKm = *held;
			leader->StakeoutUntilJd = cs::Math::Max(leader->StakeoutUntilJd, jd + HoldGateDays);
			return best;
		}
		double days = here == root ? 0 : Fleet()->EstimateTravelDays(leader, here, root, jd);
		if (days > 30) return nullptr;
		StakeOut(leader, best, jd + days + HoldGateDays);
		return best;
	}

	cs::Ref<GroupOrder> War::DecidePicket(const cs::Ref<Ship>& leader, int here, double jd, bool withdrawing)
	{
		if (withdrawing || GroupDamage(leader) > GatePickets::RepairDamage)
		{
			int yard = NearestYard(leader->Faction, here, jd);
			if (yard >= 0) return Order(OrderKind::Repair, yard);
		}
		return Order(OrderKind::Patrol, Catalog()->Root()->Index);
	}

	cs::Ref<GroupOrder> War::DecideCorvette(const cs::Ref<Ship>& leader, int here, double jd, bool withdrawing)
	{
		auto f = leader->Faction;
		if (withdrawing || GroupDamage(leader) > Corvettes::RepairDamage || MissilesLow(leader) || !leader->Combat->Armed())
		{
			int yard = NearestYard(f, here, jd);
			if (yard >= 0) return Order(OrderKind::Repair, yard);
		}
		// (C#'s `leader.Route ?? new[] { ... }`: a C++ route is never null, so an empty one counts as none.)
		auto beat = leader->Route.Count() > 0 ? leader->Route : cs::List<int>{ leader->CorvetteStation };
		int threatened = -1;
		double bestDays = cs::Double::MaxValue;
		for (int body : beat)
		{
			if (!EnemiesAt(body, f)) continue;
			double days = body == here ? 0 : Fleet()->EstimateTravelDays(leader, here, body, jd);
			if (days < bestDays)
			{
				bestDays = days;
				threatened = body;
			}
		}
		if (threatened >= 0) return Order(OrderKind::Defend, threatened);
		// The pirates and smugglers working its ground, as its navy's picture has them: the nearest first.
		auto at = world->Sensors != nullptr ? world->Sensors->HelioOf(leader, jd) : DVec3();
		auto ground = cs::List<int>(beat);
		for (const auto& s : Economy()->Settlements)
			if (!ground.Contains(s->Body) && Corvettes::Polices(world, leader, s->Body)) ground.Add(s->Body);
		int mark = -1;
		bestDays = cs::Double::MaxValue;
		for (int body : ground)
		{
			if (body == here || !PiratesSeenAt(body, f, at, jd)) continue;
			double days = Fleet()->EstimateTravelDays(leader, here, body, jd);
			if (days < bestDays)
			{
				bestDays = days;
				mark = body;
			}
		}
		if (mark >= 0) return Order(OrderKind::Hunt, mark);
		return Order(OrderKind::Patrol, Corvettes::NextOnBeat(leader, here));
	}

	double War::RaidersOffGate(const cs::Ref<JumpGate>& gate, const cs::Ref<Faction>& f, DVec3 at, double jd)
	{
		int root = Catalog()->Root()->Index;
		auto sensors = world->Sensors;
		double total = 0;
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || s->IsEscort() || s->Faction == nullptr || !s->Faction->IsPirate || s->Runner || !s->StakeoutKm.has_value()) continue;
			if (!Diplomacy()->Hostile(f, s->Faction) || GateNear(*s->StakeoutKm) != gate) continue;
			if (sensors != nullptr)
			{
				auto seen = sensors->KnownAt(f, s, at, jd);
				if (!(seen.has_value() && seen->Body == root && seen->Level >= ContactLevel::Class)) continue;
			}
			total += GroupStrength(s);
		}
		return total;
	}

	cs::Ref<GroupOrder> War::DecideWing(const cs::Ref<Ship>& leader, int here, double jd, bool withdrawing)
	{
		auto f = leader->Faction;
		// A supply convoy (U.SPC-T375) goes on round its run, to a yard when hurt: its stores, not the fighting, are its business.
		if (leader->Convoy)
		{
			if (withdrawing || GroupDamage(leader) > 0.22)
			{
				int dock = NearestYard(f, here, jd);
				if (dock >= 0) return Order(OrderKind::Repair, dock);
			}
			return Order(OrderKind::Patrol, PatrolWings::NextOnBeat(leader, here));
		}
		if (withdrawing || GroupDamage(leader) > 0.22 || MissilesLow(leader) || !leader->Combat->Armed())
		{
			int yard = NearestYard(f, here, jd);
			if (yard >= 0) return Order(OrderKind::Repair, yard);
		}
		// A wing too can be given an objective by its polity's command, off its beat (U.SPC-T423).
		if (auto directed = Directed(leader, here, jd)) return directed;
		auto beat = leader->Route; // (C#'s `?? new int[0]`: a C++ route is never null.)
		int threatened = -1;
		double bestDays = cs::Double::MaxValue;
		for (int body : beat)
		{
			if (!EnemiesAt(body, f)) continue;
			double days = body == here ? 0 : Fleet()->EstimateTravelDays(leader, here, body, jd);
			if (days < bestDays)
			{
				bestDays = days;
				threatened = body;
			}
		}
		if (threatened >= 0) return Order(OrderKind::Defend, threatened);
		auto at = world->Sensors != nullptr ? world->Sensors->HelioOf(leader, jd) : DVec3();
		for (int body : beat)
			if (body != here && PiratesSeenAt(body, f, at, jd))
				return Order(OrderKind::Hunt, body);
		// A lane picket wing (U.SPC-T479) lies in wait now and then on a lane between the ports of its beat.
		if (auto picket = LanePicket(leader, here, beat, true)) return picket;
		return Order(OrderKind::Patrol, PatrolWings::NextOnBeat(leader, here));
	}

	cs::Ref<GroupOrder> War::LanePicket(const cs::Ref<Ship>& leader, int here, const cs::List<int>& ports, bool keepToPorts)
	{
		auto lanes = Fleet()->Lanes;
		if (lanes == nullptr || !LaneInterdictors::Fielded(leader, Fleet()->Ships())) return nullptr;
		if (rng.NextDouble() >= LaneInterdictors::PicketChance()->Value) return nullptr;
		int system = lanes->SystemOf(here);
		cs::Ref<Lane> best = nullptr;
		int bound = -1;
		double bestScore = 0;
		for (const auto& lane : lanes->Lanes)
		{
			if (lane->A != system && lane->B != system) continue;
			int far = lane->Other(system), to = -1;
			// (C#'s `ports != null`: the list is passed by reference.)
			for (int p : ports)
				if (lanes->SystemOf(p) == far) { to = p; break; }
			if (to < 0)
			{
				if (keepToPorts) continue;
				to = far;
			}
			double laneDanger = 0; // C#'s local `danger`, renamed: it hides the `danger` field (C4458)
			lanes->Danger.TryGetValue(lane, laneDanger);
			double score = (laneDanger + 0.25) * (0.7 + rng.NextDouble() * 0.6);
			if (score > bestScore)
			{
				bestScore = score;
				best = lane;
				bound = to;
			}
		}
		if (best == nullptr) return nullptr;
		leader->LurkLane = best;
		leader->LurkAt = 0.3 + rng.NextDouble() * 0.4;
		leader->LurkDays = 2 + rng.NextDouble() * 2;
		return Order(OrderKind::Ambush, bound);
	}

	bool War::Commanded(const cs::Ref<Ship>& ship, int body)
	{
		if (ship == nullptr) return false;
		auto leader = ship->Controller();
		if (Hunter(leader)) return true;
		// A band staked out off a gate is there to fall on what comes through (see GateAmbushes).
		if (leader->StakeoutKm.has_value() && body == Catalog()->Root()->Index) return true;
		for (const auto& st : Economy()->At(body))
			if (st->Owner == leader->Faction && !st->Fallen) return true;
		cs::Ref<GroupOrder> o;
		bool ordered = Orders.TryGetValue(leader, o);
		cs::Ref<GroupOrder> prior;
		PriorOrders.TryGetValue(leader, prior);
		// Never ordered anywhere yet: the old rule, for all but a wing.
		if (!ordered) return !leader->PatrolWing || PatrolWings::OnBeat(leader, body);
		if (Objective(o, body) || Objective(prior, body)) return true;
		// On its beat a group patrols, and fights for it, unless it was sent away on other business (a wing always).
		return PatrolWings::OnBeat(leader, body) && (leader->PatrolWing || o->Kind == OrderKind::Patrol || prior == nullptr || prior->Kind == OrderKind::Patrol);
	}

	bool War::Objective(const cs::Ref<GroupOrder>& o, int body)
	{
		return o != nullptr && o->Body == body && o->Kind != OrderKind::Patrol && o->Kind != OrderKind::Repair;
	}

	cs::Ref<GroupOrder> War::DecidePirate(const cs::Ref<Ship>& leader, int here, double jd, bool withdrawing)
	{
		// A fresh plan: an ambush planned earlier and not yet reached no longer stands.
		leader->LurkLane = nullptr;
		auto watch = leader->StakeoutKm;
		leader->StakeoutKm = std::nullopt;
		auto home = HomeHaven(leader);
		// A group whose haven has been taken or destroyed is out of the game: it lies low where it is,
		// no raids and no ambushes, until the haven is rebuilt.
		if (home != nullptr && home->Fallen) return Order(OrderKind::Patrol, here);
		if (!home)
		{
			home = LiveHavenOf(leader->Faction);
			if (!home) home = FirstLiveHaven();
		}
		int haven = home != nullptr ? home->Body : -1;
		if (withdrawing || GroupDamage(leader) > 0.3 || MissilesLow(leader) || !leader->Combat->Armed())
			return Order(OrderKind::Repair, haven >= 0 ? haven : here);

		// Home is under attack: back to defend it.
		if (haven >= 0 && EnemiesAt(haven, leader->Faction) && (haven == here || Fleet()->EstimateTravelDays(leader, here, haven, jd) < 30))
			return Order(OrderKind::Defend, haven);

		// A hold full of plunder goes home to the haven's stores first (U.SPC-T94).
		if (leader->PlunderTonnes() > 0 && home != nullptr && home->Owner == leader->Faction)
		{
			if (haven == here) UnloadPlunder(leader, here);
			else return Order(OrderKind::Repair, haven);
		}

		// Lying in wait off a jump gate (U.SPC-T94): hold there till the watch is up; or, now and then, go and lie off
		// the busiest gate, where traffic bunches up going in and coming out.
		int root = Catalog()->Root()->Index;
		if (watch.has_value() && jd < leader->StakeoutUntilJd)
		{
			leader->StakeoutKm = *watch;
			return Order(OrderKind::GateWatch, root);
		}
		if (here != root && world->Gates.Count() > 0 && rng.NextDouble() < GateWatchChance)
			if (auto gate = BusiestGate(leader->Faction, jd))
			{
				double days = Fleet()->EstimateTravelDays(leader, here, root, jd);
				if (days < 30)
				{
					StakeOut(leader, gate, jd + days + 2 + rng.NextDouble() * 2);
					return Order(OrderKind::GateWatch, root);
				}
			}

		// Go after a lane tender in reach (U.SPC-T481): worth a fat purse to whoever wants the lanes cut.
		if (world->LaneUpkeep != nullptr && world->LaneUpkeep->RaidersHunt())
		{
			int tender = world->LaneUpkeep->HuntTarget(leader, here, jd);
			if (tender >= 0) return Order(OrderKind::Raid, tender);
		}

		// Lie in wait beside a busy hyperlane: only with an interdictor in the band to pull its marks out (U.SPC-T479).
		auto lanes = Fleet()->Lanes;
		if (lanes != nullptr && LaneInterdictors::Fielded(leader, Fleet()->Ships()) && rng.NextDouble() < 0.55)
		{
			int system = lanes->SystemOf(here);
			auto lane = BusiestLane(system);
			if (lane != nullptr)
			{
				if (lane->A == system || lane->B == system)
				{
					leader->LurkLane = lane;
					leader->LurkAt = 0.3 + rng.NextDouble() * 0.4;
					leader->LurkDays = 2.5 + rng.NextDouble() * 2;
					return Order(OrderKind::Ambush, lane->Other(system));
				}
				double toA = (world->Ephemeris->HelioAt(lane->A, jd) - world->Ephemeris->HelioAt(here, jd)).Length();
				double toB = (world->Ephemeris->HelioAt(lane->B, jd) - world->Ephemeris->HelioAt(here, jd)).Length();
				return Order(OrderKind::Ambush, toA < toB ? lane->A : lane->B);
			}
		}

		// Stay on a hunting ground a while if it has traffic and no warships.
		cs::Ref<GroupOrder> o;
		if (Orders.TryGetValue(leader, o) && o->Kind == OrderKind::Raid && o->Body == here && jd - o->SinceJd < 4 && DefenceAt(here, leader->Faction) < GroupStrength(leader))
			return Order(OrderKind::Raid, here);

		int target = -1;
		double best = 0;
		for (const auto& s : Economy()->Settlements)
		{
			if (s->Kind == SettlementKind::Haven || s->Body == here) continue;
			double traffic = 1;
			for (const auto& t : Fleet()->Ships())
				if (!t->Destroyed && world->IsTrader(t) && (t->DestinationBody == s->Body || (t->Orbit != nullptr && t->Orbit->Body == s->Body))) traffic++;
			double defence = DefenceAt(s->Body, leader->Faction);
			// Raiders take chances against a port's guns, but prefer the soft ones.
			if (defence > GroupStrength(leader) * 1.6) continue;
			double days = Fleet()->EstimateTravelDays(leader, here, s->Body, jd);
			double value = traffic / (defence + GroupStrength(leader) * 0.3 + 100) / (days + 3) * (0.7 + rng.NextDouble() * 0.6);
			if (value > best)
			{
				best = value;
				target = s->Body;
			}
		}
		if (target < 0) target = haven >= 0 && haven != here ? haven : NextPatrol(leader, here);
		return Order(OrderKind::Raid, target);
	}

	bool War::Hunter(const cs::Ref<Ship>& ship) { return world->Outlaws != nullptr && world->Outlaws->IsHunter(ship); }

	bool War::IsHomeGuard(const cs::Ref<Ship>& leader)
	{
		// Each navy keeps its oldest surviving group at home: one it still commands (not the hull the player
		// is flying, nor a derelict), or every other group would be free to leave. A patrol wing is never it: it keeps
		// its beat whatever (U.SPC-T371).
		for (const auto& s : Fleet()->Ships())
			if (!s->Destroyed && !s->IsEscort() && !s->Derelict && !s->PatrolWing && s != world->PilotedShip && s->Faction == leader->Faction && s->Spec->IsMilitary)
				return s == leader;
		return false;
	}

	int War::NextPatrol(const cs::Ref<Ship>& leader, int here)
	{
		auto own = cs::List<int>();
		for (const auto& s : Economy()->Settlements)
			if (s->Owner == leader->Faction && !own.Contains(s->Body)) own.Add(s->Body);
		if (own.Count() == 0) own.AddRange(leader->Route); // (C#'s `leader.Route != null`: an empty route adds nothing.)
		own.Remove(here);
		if (own.Count() == 0) return here;
		return own[rng.Next(own.Count())];
	}

	int War::NearestYard(const cs::Ref<Faction>& f, int here, double jd)
	{
		int best = -1;
		double bestDist = cs::Double::MaxValue;
		for (const auto& s : Economy()->Settlements)
		{
			if (s->Owner != f || (!s->HasShipyard() && s->Kind != SettlementKind::Haven)) continue;
			double d = s->Body == here ? 0 : (world->Ephemeris->HelioAt(s->Body, jd) - world->Ephemeris->HelioAt(here, jd)).Length();
			if (d < bestDist)
			{
				bestDist = d;
				best = s->Body;
			}
		}
		return best;
	}

	int War::HavenBody(const cs::Ref<Faction>& clan)
	{
		auto h = LiveHavenOf(clan);
		return h != nullptr ? h->Body : -1;
	}

	cs::String War::HavenName(const cs::Ref<Faction>& clan)
	{
		auto h = clan != nullptr ? LiveHavenOf(clan) : nullptr;
		if (h != nullptr) return h->Name;
		auto first = FirstLiveHaven();
		return first != nullptr ? first->Name : cs::String("a pirate haven");
	}

	cs::Ref<Settlement> War::FirstLiveHaven()
	{
		for (const auto& s : Economy()->Settlements)
			if (s->Kind == SettlementKind::Haven && !s->Fallen) return s;
		return nullptr;
	}

	cs::Ref<Settlement> War::HomeHaven(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr) return nullptr;
		// (C#'s `route == null`: a C++ route is never null, so an empty one counts as none.)
		const auto route = ship->Controller()->Route;
		if (route.Count() == 0) return nullptr;
		for (const auto& s : Economy()->At(route[0]))
			if (s->Kind == SettlementKind::Haven) return s;
		return nullptr;
	}

	// ---------------------------------------------------------------- hidden havens

	bool War::Discover(const cs::Ref<Settlement>& st, const cs::Ref<Faction>& by, double jd, const cs::String& how)
	{
		if (st == nullptr || !st->Hidden || by == nullptr || by->IsPirate || st->KnownTo.Contains(by)) return false;
		bool first = st->KnownTo.Count() == 0;
		st->KnownTo.Add(by);
		cs::String text = first
			? cs::Cat("The ", by->Name, " find a hidden ", st->Owner->Name, " haven, ", st->Name, ", on ", (*Catalog())[st->Body]->Name, ": ", how)
			: cs::Cat("The ", by->Name, " learn where ", st->Name, " (", (*Catalog())[st->Body]->Name, ") lies: ", how);
		world->Log(jd, text, WorldEventKind::Info, st->Body);
		return true;
	}

	cs::List<cs::Ref<Settlement>> War::ScanFrom(const cs::Ref<Faction>& by, DVec3 helioKm, double jd, const cs::String& who, double rangeKm)
	{
		auto found = cs::List<cs::Ref<Settlement>>();
		if (by == nullptr || by->IsPirate) return found;
		for (const auto& st : Economy()->Settlements)
		{
			if (!st->Hidden || st->KnownTo.Contains(by)) continue;
			if ((world->Ephemeris->HelioAt(st->Body, jd) - helioKm).Length() > rangeKm) continue;
			// (C#'s `who != null`: no name is an empty one here.)
			if (Discover(st, by, jd, cs::Cat((!who.empty() ? cs::Cat("the ", who, "'s") : cs::String("a ship's")), " scanners pick it out"))) found.Add(st);
		}
		return found;
	}

	bool War::TrailHome(const cs::Ref<Ship>& raider, const cs::Ref<Faction>& tracker, double jd)
	{
		auto home = HomeHaven(raider);
		if (home == nullptr || tracker == nullptr || tracker->IsPirate) return false;
		return Discover(home, tracker, jd, cs::Cat(tracker->Name, " ships follow the beaten ", raider->Name, " home"));
	}

	void War::Scans(double jd)
	{
		bool anyHidden = false;
		for (const auto& st : Economy()->Settlements) anyHidden |= st->Hidden;
		if (!anyHidden) return;
		// A group sweeps as far as the best scanners in it reach.
		auto reach = cs::Dictionary<cs::Ref<Ship>, double>();
		for (const auto& e : Fleet()->Ships())
			if (e->IsEscort() && !e->Destroyed && e->Loadout != nullptr)
			{
				double r = 0;
				reach.TryGetValue(e->Leader, r);
				reach[e->Leader] = cs::Math::Max(r, Outfitting::ScanRangeKm(e));
			}
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || s->IsEscort() || s->Derelict || s == world->PilotedShip || !s->Spec->IsMilitary || s->Faction->IsPirate) continue;
			double escorts = 0;
			reach.TryGetValue(s, escorts);
			ScanFrom(s->Faction, Fleet()->StateOf(s, jd).HelioKm, jd, s->Name, cs::Math::Max(escorts, Outfitting::ScanRangeKm(s)));
		}
	}

	bool War::Assailable(const cs::Ref<Settlement>& s, const cs::Ref<Faction>& f)
	{
		return s->Kind == SettlementKind::Haven && !s->Fallen && s->Hidden && s->KnownTo.Contains(f) && Diplomacy()->Hostile(f, s->Owner);
	}

	cs::Ref<Settlement> War::AssaultTarget(const cs::Ref<Ship>& leader, int here, double jd)
	{
		auto f = leader->Faction;
		double strength = GroupStrength(leader);
		cs::Ref<Settlement> best = nullptr;
		double bestDays = 40;
		for (const auto& s : Economy()->Settlements)
		{
			if (!Assailable(s, f) || strength < DefenceAt(s->Body, f) * 1.5) continue;
			double days = s->Body == here ? 0 : Fleet()->EstimateTravelDays(leader, here, s->Body, jd);
			if (days < bestDays)
			{
				bestDays = days;
				best = s;
			}
		}
		return best;
	}

	bool War::HasMarines(const cs::Ref<Ship>& ship)
	{
		auto leader = ship->Controller();
		if (!leader->Destroyed && leader->Combat->MarinesAvailable() > 0) return true;
		for (const auto& e : Fleet()->Ships())
			if (e->Leader == leader && !e->Destroyed && e->Combat->MarinesAvailable() > 0) return true;
		return false;
	}

	bool War::CanAssault(const cs::Ref<Settlement>& st, const cs::Ref<Ship>& ship)
	{
		return st != nullptr && ship != nullptr && !ship->Destroyed && st->Kind == SettlementKind::Haven && !st->Fallen && st->Platforms.Count() == 0 &&
			BattleAt(st->Body) == nullptr && !PiratesAt(st->Body) && Diplomacy()->Hostile(ship->Faction, st->Owner);
	}

	bool War::AssaultHaven(const cs::Ref<Settlement>& st, const cs::Ref<Ship>& ship, bool capture, double jd)
	{
		if (!CanAssault(st, ship)) return false;
		if (capture ? !HasMarines(ship) : !ship->Controller()->Combat->Armed() && !HasMarines(ship)) return false;
		FallHaven(st, ship->Faction, capture, jd);
		return true;
	}

	void War::FallHaven(const cs::Ref<Settlement>& st, const cs::Ref<Faction>& by, bool capture, double jd)
	{
		double value = 0;
		for (int i = 0; i < GoodInfo::Count; i++)
		{
			value += cs::Math::Max(0.0, st->Stock[i]) * GoodInfo::Of(static_cast<Good>(i))->BasePrice;
			st->Stock[i] = 0;
		}
		if (capture) by->Treasury += value * 0.6;
		st->Platforms.Clear();
		st->Population = 0;
		st->Fallen = true;
		st->RebuildJd = jd + HavenRebuildDays;
		if (st->Hidden) st->KnownTo.Add(by);
		assaultDays.Remove(st);
		cs::String where = cs::Cat(st->Name, " (", (*Catalog())[st->Body]->Name, ")");
		world->Log(jd, capture
			? cs::Cat("The ", by->Name, " board and take ", where, ", seizing ", cs::Fmt(value / 1e6, "N0"), " M of stolen goods; its raiders scatter")
			: cs::Cat("The ", by->Name, " destroy ", where, "; its raiders scatter"), WorldEventKind::Capture, st->Body);
		BumpDanger(st->Body, 1);
	}

	void War::HavenAssaults(double jd)
	{
		for (const auto& st : Economy()->Settlements)
		{
			if (st->Kind != SettlementKind::Haven) continue;
			if (st->Fallen)
			{
				if (jd >= st->RebuildJd) RebuildHaven(st, jd);
				continue;
			}
			cs::Ref<Ship> assailant = nullptr;
			if (st->Platforms.Count() == 0 && BattleAt(st->Body) == nullptr && !PiratesAt(st->Body))
				for (const auto& s : Fleet()->Ships())
				{
					if (s->Destroyed || s->IsEscort() || !s->Spec->IsMilitary || s->Faction->IsPirate || s == world->PilotedShip) continue;
					auto c = s->Controller();
					if (c->Phase != ShipPhase::Parked || c->Orbit->Body != st->Body || !Diplomacy()->Hostile(s->Faction, st->Owner)) continue;
					cs::Ref<GroupOrder> o;
					if (!Orders.TryGetValue(s, o) || o->Kind != OrderKind::Assault) continue;
					if (assailant == nullptr || GroupStrength(s) > GroupStrength(assailant)) assailant = s;
				}
			if (assailant == nullptr)
			{
				assaultDays.Remove(st);
				continue;
			}
			double days = 0;
			assaultDays.TryGetValue(st, days);
			days += 1;
			assaultDays[st] = days;
			if (days >= HavenAssaultDays) FallHaven(st, assailant->Faction, HasMarines(assailant), jd);
		}
	}

	void War::RebuildHaven(const cs::Ref<Settlement>& st, double jd)
	{
		st->Fallen = false;
		st->Population = st->InitialPopulation;
		while (st->Platforms.Count() < st->MaxPlatforms) st->Platforms.Add(cs::New<ShipCombat>(CombatSpec::Platform()));
		world->Log(jd, cs::Cat("The ", st->Owner->Name, " have rebuilt ", st->Name, " (", (*Catalog())[st->Body]->Name, ")"), WorldEventKind::Info, st->Body);
	}

	double War::AssaultDays(const cs::Ref<Settlement>& s) const
	{
		double d = 0;
		return assaultDays.TryGetValue(s, d) ? d : 0;
	}

	bool War::AtWar(const cs::Ref<Faction>& f)
	{
		for (const auto& g : Fleet()->Factions())
			if (g != f && !g->IsPirate && Diplomacy()->Get(f, g) == Relation::War) return true;
		return false;
	}

	bool War::EnemiesAt(int body, const cs::Ref<Faction>& f)
	{
		if (auto b = BattleAt(body))
			if (b->SideLeaders[0] == f || b->SideLeaders[1] == f) return true;
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || s->IsEscort() || !(s->Spec->IsMilitary || s->Faction->IsPirate)) continue;
			if (!Diplomacy()->Hostile(f, s->Faction)) continue;
			auto c = s->Controller();
			if (c->Phase == ShipPhase::Parked && c->Orbit->Body == body) return true;
		}
		return false;
	}

	bool War::EnemyNavyAt(int body, const cs::Ref<Faction>& f)
	{
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || s->IsEscort() || !s->Spec->IsMilitary || s->Faction->IsPirate || !Diplomacy()->Hostile(f, s->Faction)) continue;
			auto c = s->Controller();
			if (c->Phase == ShipPhase::Parked && c->Orbit != nullptr && c->Orbit->Body == body) return true;
		}
		return false;
	}

	bool War::PiratesAt(int body)
	{
		for (const auto& s : Fleet()->Ships())
			if (!s->Destroyed && !s->IsEscort() && s->Faction->IsPirate && !s->Runner && s->Phase == ShipPhase::Parked && s->Orbit->Body == body) return true;
		return false;
	}

	bool War::PiratesSeenAt(int body, const cs::Ref<Faction>& f, DVec3 where, double jd)
	{
		auto sensors = world->Sensors;
		if (sensors == nullptr)
		{
			for (const auto& s : Fleet()->Ships())
				if (!s->Destroyed && !s->IsEscort() && s->Faction->IsPirate && s->Phase == ShipPhase::Parked && s->Orbit->Body == body) return true;
			return false;
		}
		for (const auto& c : sensors->PictureOf(f)->Contacts())
		{
			auto s = c->Ship;
			if (s->Destroyed || s->IsEscort() || s->Faction == nullptr || !s->Faction->IsPirate) continue;
			auto seen = sensors->KnownAt(f, s, where, jd);
			if (seen.has_value() && seen->Body == body && seen->Level >= ContactLevel::Class) return true;
		}
		return false;
	}

	bool War::KnowsOfRaidersAt(int body, const cs::Ref<Faction>& f, DVec3 where, double jd) { return PiratesSeenAt(body, f, where, jd); }

	double War::DefenceAt(int body, const cs::Ref<Faction>& attacker)
	{
		double total = 0;
		for (const auto& st : Economy()->At(body))
			if (Diplomacy()->Hostile(attacker, st->Owner))
				for (const auto& p : st->Platforms) total += p->Strength();
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || !(s->Spec->IsMilitary || s->Faction->IsPirate) || !Diplomacy()->Hostile(attacker, s->Faction)) continue;
			auto c = s->Controller();
			if (c->Phase == ShipPhase::Parked && c->Orbit->Body == body) total += s->Combat->Strength();
		}
		return total;
	}

	double War::GroupStrength(const cs::Ref<Ship>& leader)
	{
		double total = leader->Combat->Strength();
		for (const auto& s : Fleet()->Ships())
			if (s->Leader == leader && !s->Destroyed) total += s->Combat->Strength();
		return total;
	}

	double War::GroupDamage(const cs::Ref<Ship>& leader)
	{
		double sum = leader->Combat->HullDamage();
		int n = 1;
		for (const auto& s : Fleet()->Ships())
			if (s->Leader == leader && !s->Destroyed)
			{
				sum += s->Combat->HullDamage();
				n++;
			}
		return sum / n;
	}

	bool War::HangarsLow(const cs::Ref<Ship>& leader)
	{
		int have = 0, full = 0;
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || (s != leader && s->Leader != leader)) continue;
			have += s->Combat->Fighters();
			full += s->Combat->Spec->HangarCraft();
		}
		return full > 0 && have < full * 0.5;
	}

	bool War::MarinesLow(const cs::Ref<Ship>& leader)
	{
		double have = 0, full = 0;
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || (s != leader && s->Leader != leader)) continue;
			have += s->Combat->Interior->Marines();
			full += s->Combat->Spec->Marines;
		}
		return full > 0 && have < full * 0.35;
	}

	bool War::MissilesLow(const cs::Ref<Ship>& leader)
	{
		int have = 0, full = 0;
		for (const auto& sub : leader->Combat->Subsystems)
			if (sub->Def->Weapon != nullptr && sub->Def->Weapon->Missile && !sub->Destroyed())
			{
				have += sub->Ammo;
				full += sub->Def->Weapon->Ammo;
			}
		return full > 0 && have < full * 0.3;
	}

	// ---------------------------------------------------------------- daily

	void War::Daily(double jd)
	{
		for (const auto& key : danger.Keys()) danger[key] *= cs::Math::Exp(-1.0 / 20);
		for (const auto& s : Economy()->Settlements)
			for (int i = 0; i < s->ExtraDemand.Count(); i++) s->ExtraDemand[i] = 0; // Array.Clear
		PruneTracking(jd);

		for (const auto& s : Fleet()->Ships())
			if (s->Derelict && !s->Destroyed && jd - s->DerelictSinceJd > 25) s->Destroyed = true;
		Wrecks->AdvanceTo(jd);
		Crews->Daily(Diplomacy(), jd);
		// A polity recruits once a day from every settlement it holds in any system, by the world that keeps its books.
		auto polities = world->Polities;
		Core::Crews::Recruit(polities->KeptBy(world, Fleet()->Factions()), polities->Worlds().Count() > 1 ? polities->Settlements() : Economy()->Settlements, 1);
		if (Fleet()->Lanes != nullptr)
			for (const auto& lane : Fleet()->Lanes->Danger.Keys()) Fleet()->Lanes->SetDanger(lane, Fleet()->Lanes->Danger.At(lane) * 0.85);
		MergeGroups(jd);
		PayUpkeep();
		DrillMarines();
		Refit(jd);
		Sieges(jd);
		Scans(jd);
		HavenAssaults(jd);
		RebuildPlatforms(jd);
		// The navies' corvettes: reposted when their ports change hands, and replaced when lost (U.SPC-T476).
		Corvettes::Upkeep(world, CorvettesShortSince, jd);
		PirateRecruiting(jd);
		DiplomacyCycle(jd);

		// Groups that met at a body without a fight (a war resumed, say) fight now, if they are fit to.
		for (const auto& s : Fleet()->Ships())
		{
			// Not the player's ship: its fights start only where the player engages (TryStartPlayerBattle).
			if (s->Destroyed || s->IsEscort() || s->Battle != nullptr || s->Phase != ShipPhase::Parked || s == world->PilotedShip) continue;
			if (!(s->Spec->IsMilitary || s->Faction->IsPirate) || s->Runner || !Willing(s, jd)) continue;
			int body = s->Orbit->Body;
			double ended = 0;
			if (lastBattleEndJd.TryGetValue(body, ended) && jd - ended < 0.5) continue;
			if (BattleAt(body) == nullptr) TryStartBattle(body, s->Faction, jd, body == Catalog()->Root()->Index ? cs::Nullable<DVec3>(s->Orbit->OffsetAt(jd)) : cs::Nullable<DVec3>());
		}
	}

	void War::MergeGroups(double jd)
	{
		int root = Catalog()->Root()->Index;
		auto groups = cs::List<cs::Ref<Ship>>();
		for (const auto& s : Fleet()->Ships())
			// The player's ship is flown by the player: never handed to an AI flagship, nor one itself.
			// Nor a group on an interstellar voyage: it keeps to its own business.
			// Nor bounty hunters: each works its own contract.
			// Nor a pirate clan's smugglers: they carry loot, not guns, and never raid (see Ship.Runner).
			// Nor a patrol wing: it keeps to its own beat (U.SPC-T371).
			if (!s->Destroyed && !s->IsEscort() && s->Battle == nullptr && s != world->PilotedShip && (s->Spec->IsMilitary || s->Faction->IsPirate) && !s->Runner && !s->PatrolWing && s->Phase == ShipPhase::Parked && s->Voyage == nullptr && !Hunter(s))
				groups.Add(s);
		groups.Sort([this](const cs::Ref<Ship>& a, const cs::Ref<Ship>& b) { return cs::Compare(GroupStrength(b), GroupStrength(a)); });
		for (int i = 0; i < groups.Count(); i++)
		{
			auto big = groups[i];
			if (big->Destroyed || big->IsEscort() || IsHomeGuard(big)) continue;
			for (int j = i + 1; j < groups.Count(); j++)
			{
				auto small = groups[j];
				if (small->IsEscort() || small->Faction != big->Faction || small->Orbit->Body != big->Orbit->Body || IsHomeGuard(small)) continue;
				if (big->Orbit->Body == root && (big->Orbit->OffsetAt(jd) - small->Orbit->OffsetAt(jd)).Length() > GateFightRangeKm) continue;
				int size = 1, joining = 1;
				for (const auto& e : Fleet()->Ships())
				{
					if (e->Destroyed) continue;
					if (e->Leader == big) size++;
					if (e->Leader == small) joining++;
				}
				if (size + joining > 8) continue;
				auto members = cs::List<cs::Ref<Ship>>{ small };
				for (const auto& e : Fleet()->Ships())
					if (e->Leader == small && !e->Destroyed) members.Add(e);
				// The joining band's own plunder comes along into the flagship it now follows (U.SPC-T308).
				MovePlunder(small, big);
				for (const auto& m : members)
				{
					Fleet()->AddEscort(big, m, Core::Fleet::SlotFor(FreeSlot(big)));
					size++;
				}
				Orders.Remove(small);
				PriorOrders.Remove(small);
				retreating.Remove(small);
			}
		}
	}

	int War::FreeSlot(const cs::Ref<Ship>& leader)
	{
		for (int k = 0; ; k++)
		{
			auto at = Core::Fleet::SlotFor(k);
			bool taken = false;
			for (const auto& e : Fleet()->Ships())
				if (!e->Destroyed && e->Leader == leader && (e->FormationSlot - at).LengthSquared() < 1e-9)
				{
					taken = true;
					break;
				}
			if (!taken) return k;
		}
	}

	void War::PayUpkeep()
	{
		constexpr double perDay = Core::Economy::UpkeepPerYear / 365.25;
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || s->Derelict || !s->Spec->IsMilitary || s->Faction->IsPirate) continue;
			s->Faction->Treasury -= Core::Economy::HullPrice(s->Spec->Class) * perDay;
		}
		for (const auto& st : Economy()->Settlements)
			if (!st->Owner->IsPirate && st->Platforms.Count() > 0) st->Owner->Treasury -= st->Platforms.Count() * PlatformPrice * perDay;
	}

	void War::DrillMarines()
	{
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || s->Derelict || s->Battle != nullptr || s->Combat == nullptr) continue;
			s->Combat->Interior->Drill(1);
		}
	}

	void War::Refit(double /*jd*/)
	{
		const int alloysI = static_cast<int>(Good::Alloys), electronicsI = static_cast<int>(Good::Electronics);
		const int munitionsI = static_cast<int>(Good::Munitions), medicalI = static_cast<int>(Good::MedicalSupplies);
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || s->Battle != nullptr || !(s->Spec->IsMilitary || s->Faction->IsPirate)) continue;
			auto c = s->Controller();
			if (c->Phase != ShipPhase::Parked) continue;
			cs::Ref<Settlement> yard = nullptr, port = nullptr;
			for (const auto& st : Economy()->At(c->Orbit->Body))
			{
				if (st->Owner != s->Faction || st->Fallen) continue;
				port = st;
				if (st->HasShipyard() || st->Kind == SettlementKind::Haven) yard = st;
			}
			// Strays that set down here with no hangar to take them rejoin their carrier now it has called (U.SPC-T591).
			for (const auto& st : Economy()->At(c->Orbit->Body))
				if (st->Strays.Count() > 0 && !st->Fallen && s->Combat->Hangar != nullptr) CarrierWings::Rejoin(st, s);
			// Prisoners in the brig go ashore at any port of the owner's, for their bounty or ransom (U.SPC-T65).
			if (port != nullptr && s->Prisoners > 0 && s != world->PilotedShip) Outfitting::HandOverPrisoners(Economy(), s, port);
			// In a port of its own a hull is in supply again: tanks, magazines and stores (U.SPC-T93).
			if (port != nullptr && (s->Voyage == nullptr || s->Voyage->Campaign == nullptr)) s->Combat->Readiness = 1;
			if (s->Faction->Treasury <= 0) continue;
			if (yard == nullptr) continue;

			double scale = s->LengthKm();
			double damage = s->Combat->Damage();
			if (damage > 0.001)
			{
				double share = cs::Math::Min(0.15, damage);
				double alloys = 900 * scale * share / 0.15, electronics = 60 * scale * share / 0.15;
				double have = cs::Math::Min(1.0, cs::Math::Min(yard->Stock[alloysI] / cs::Math::Max(alloys, 1e-9), yard->Stock[electronicsI] / cs::Math::Max(electronics, 1e-9)));
				yard->ExtraDemand[alloysI] += alloys;
				yard->ExtraDemand[electronicsI] += electronics;
				if (yard->Kind == SettlementKind::Haven) have = 0.5;
				yard->Stock[alloysI] -= cs::Math::Min(yard->Stock[alloysI], alloys * have);
				yard->Stock[electronicsI] -= cs::Math::Min(yard->Stock[electronicsI], electronics * have);
				s->Faction->Treasury -= (alloys * yard->Price[alloysI] + electronics * yard->Price[electronicsI]) * have;
				if (have > 0) s->Combat->Repair(share * have);
			}
			int missing = 0;
			for (const auto& sub : s->Combat->Subsystems)
				if (sub->Def->Weapon != nullptr && sub->Def->Weapon->Missile) missing += sub->Def->Weapon->Ammo - sub->Ammo;
			if (missing > 0)
			{
				double tonnes = missing * 1.5;
				yard->ExtraDemand[munitionsI] += tonnes;
				if (yard->Stock[munitionsI] >= tonnes || yard->Kind == SettlementKind::Haven)
				{
					yard->Stock[munitionsI] = cs::Math::Max(0.0, yard->Stock[munitionsI] - tonnes);
					s->Faction->Treasury -= tonnes * yard->Price[munitionsI];
					for (const auto& sub : s->Combat->Subsystems)
						if (sub->Def->Weapon != nullptr && sub->Def->Weapon->Missile) sub->Ammo = sub->Def->Weapon->Ammo;
				}
			}
			// Marines lost boarding (and crew lost to breaches) are replaced from the faction's trained
			// reserve, as far as it goes; the wounded go to the yard's hospitals.
			auto inside = s->Combat->Interior;
			double marinesShort = s->Combat->Spec->Marines - inside->Marines();
			double crewShort = cs::Math::Max(0.0, s->Combat->Spec->Crew - inside->Crew());
			if ((marinesShort > s->Combat->Spec->Marines * 0.1 || crewShort > s->Combat->Spec->Crew * 0.1) && !inside->UnderBoarding())
			{
				double marines = Core::Crews::Draft(s->Faction, cs::Math::Max(0.0, marinesShort));
				double crew = Core::Crews::Draft(s->Faction, crewShort);
				double medical = marines * 0.05;
				yard->ExtraDemand[medicalI] += medical;
				yard->Stock[medicalI] = cs::Math::Max(0.0, yard->Stock[medicalI] - medical);
				s->Faction->Treasury -= marines * 40000;
				if (marines + crew > 0) inside->Muster(inside->Crew() + crew, inside->Marines() + marines);
			}
			int fighters = s->Combat->Spec->HangarCraft() - s->Combat->Fighters();
			if (fighters > 0 && yard->Stock[electronicsI] > 30)
			{
				int add = cs::Math::Min(fighters, 4);
				yard->Stock[electronicsI] -= 6 * add;
				yard->Stock[alloysI] = cs::Math::Max(0.0, yard->Stock[alloysI] - 40 * add);
				s->Faction->Treasury -= add * 2.5e6;
				s->Combat->RestoreFighters(add);
			}
			// The craft that came back hurt are mended whole in the yard's shops (U.SPC-T526): each is its own hull.
			int hurt = CarrierWings::Damaged(s->Combat);
			if (hurt > 0 && yard->Stock[alloysI] > 10 * hurt)
			{
				yard->Stock[alloysI] -= 10 * hurt;
				yard->Stock[electronicsI] = cs::Math::Max(0.0, yard->Stock[electronicsI] - 2 * hurt);
				s->Faction->Treasury -= hurt * 4e5;
				CarrierWings::MendAboard(s->Combat);
			}
			// Boarding shuttles lost are built anew (U.SPC-T486).
			int shuttles = s->Combat->Spec->Shuttles - s->Combat->Shuttles();
			if (shuttles > 0 && yard->Stock[electronicsI] > 30)
			{
				yard->Stock[electronicsI] -= 4 * shuttles;
				yard->Stock[alloysI] = cs::Math::Max(0.0, yard->Stock[alloysI] - 60 * shuttles);
				s->Faction->Treasury -= shuttles * 3e6;
				s->Combat->RestoreShuttles(shuttles);
			}
			// A sound, rearmed hull of a flush owner takes an upgrade now and then (U.SPC-T65); the player fits their own.
			if (s != world->PilotedShip && s->Combat->Damage() <= 0.001 && missing == 0 && rng.NextDouble() < AiOutfitChance)
			{
				auto command = world->Polities != nullptr ? world->Polities->Command : nullptr;
				Outfitting::AiRefit(Economy(), s, yard, command != nullptr ? command->BuildPlanOf(s->Faction) : nullptr);
			}
		}
	}

	double War::SiegeNeeded(const cs::Ref<Settlement>& s) { return cs::Math::Min(20.0, 4 + s->Population / 2e5); }

	void War::Sieges(double jd)
	{
		for (const auto& st : Economy()->Settlements)
		{
			if (st->Kind == SettlementKind::Haven) continue;
			cs::Ref<Faction> besieger = nullptr, blockader = nullptr;
			bool defended = st->Platforms.Count() > 0 || BattleAt(st->Body) != nullptr || st->DaysSinceCapture < CaptureGraceDays, guarded = false;
			for (const auto& s : Fleet()->Ships())
			{
				if (s->Destroyed || s->IsEscort() || !s->Spec->IsMilitary || s->Faction->IsPirate) continue;
				auto c = s->Controller();
				if (c->Phase != ShipPhase::Parked || c->Orbit->Body != st->Body) continue;
				cs::Ref<GroupOrder> o, b;
				if (Diplomacy()->Hostile(s->Faction, st->Owner) && Orders.TryGetValue(s, o) && o->Kind == OrderKind::Siege) besieger = s->Faction;
				// A group sent to blockade it holds the space over it as a siege does, but lands nobody (U.SPC-T423).
				else if (Diplomacy()->Hostile(s->Faction, st->Owner) && Orders.TryGetValue(s, b) && b->Kind == OrderKind::Blockade) blockader = s->Faction;
				else if (s->Faction == st->Owner) defended = guarded = true;
			}
			auto holder = besieger ? besieger : blockader;
			if (holder != nullptr && !guarded) besiegers[st] = holder;
			else besiegers.Remove(st);
			if (besieger == nullptr || defended)
			{
				siegeDays.Remove(st);
				continue;
			}
			double days = 0;
			siegeDays.TryGetValue(st, days);
			days += 1;
			siegeDays[st] = days;
			if (days >= SiegeNeeded(st))
			{
				auto old = st->Owner;
				Economy()->Capture(st, besieger);
				// Taken back by the power it was occupied from: liberated, no longer occupied (U.SPC-T297).
				// Taken from anyone else: occupied from them, the same bookkeeping a campaign's marines leave
				// (see InterstellarWars.Occupy) -- without it the war's peace never counts this port's system
				// as ceded, and its people never rise to retake it (U.SPC-T362).
				if (st->OccupiedFrom == besieger) st->OccupiedFrom = nullptr;
				else if (!st->OccupiedFrom) st->OccupiedFrom = old;
				siegeDays.Remove(st);
				besiegers.Remove(st);
				RevokeBookings();
				world->Log(jd, cs::Cat(st->Name, " (", (*Catalog())[st->Body]->Name, ") falls to the ", besieger->Name, ", taken from the ", old->Name), WorldEventKind::Capture, st->Body);
				BumpDanger(st->Body, 1);
			}
		}
	}

	void War::RevokeBookings()
	{
		for (const auto& t : Fleet()->Ships())
		{
			if (t->Destroyed || t->CargoDestination == nullptr || Economy()->CanTrade(t, t->CargoDestination)) continue;
			Economy()->Release(t);
			t->CargoDestination = nullptr;
			t->Cargo = Core::Economy::CargoText(t);
		}
	}

	double War::SiegeDays(const cs::Ref<Settlement>& s) const
	{
		double d = 0;
		return siegeDays.TryGetValue(s, d) ? d : 0;
	}

	cs::Ref<Faction> War::Besieger(const cs::Ref<Settlement>& s) const
	{
		cs::Ref<Faction> f;
		return besiegers.TryGetValue(s, f) ? f : nullptr;
	}

	void War::RebuildPlatforms(double /*jd*/)
	{
		const int alloysI = static_cast<int>(Good::Alloys), electronicsI = static_cast<int>(Good::Electronics);
		for (const auto& st : Economy()->Settlements)
		{
			if (st->Fallen) continue;
			// Not mid-fight: a daily refit would refill shields and ammunition (and end a boarding) in battle.
			if (BattleAt(st->Body) != nullptr) continue;
			// Nor with an enemy navy holding the space overhead (U.SPC-T92): nothing goes up past its guns.
			if (EnemyNavyAt(st->Body, st->Owner)) continue;
			for (const auto& p : st->Platforms)
				if (p->Damage() > 0) { p->Repair(0.05); p->FreeResupply(); }
			if (st->Platforms.Count() >= st->MaxPlatforms) continue;
			// A new platform every couple of weeks while the port can afford the steel.
			if (rng.NextDouble() > 0.07) continue;
			constexpr double alloys = 3000, electronics = 200;
			st->ExtraDemand[alloysI] += alloys / 14;
			st->ExtraDemand[electronicsI] += electronics / 14;
			if (st->Stock[alloysI] < alloys || st->Stock[electronicsI] < electronics) continue;
			// ... and the owner can pay for it: a broke owner leaves its ports bare.
			if (st->Owner->Treasury < PlatformPrice) continue;
			st->Stock[alloysI] -= alloys;
			st->Stock[electronicsI] -= electronics;
			st->Owner->Treasury -= PlatformPrice;
			st->Platforms.Add(cs::New<ShipCombat>(CombatSpec::Platform()));
		}
	}

	const cs::List<cs::String>& War::RaiderNames()
	{
		static const cs::List<cs::String> names =
		{
			"Gallows Bird", "Red Meridian", "Widow's Due", "Cutthroat Margin", "No Quarter", "Bad Debt", "Scavenger", "Hollow Crown",
			"Ill Wind", "Knife Edge", "Grudge", "Salvage Rights", "Last Laugh", "Black Ledger",
		};
		return names;
	}

	void War::RaisePirateGroup(double jd, int body)
	{
		auto pirates = world->Pirates;
		// (C#'s `world.RaiderNames ?? RaiderNames`: no names is an empty list here.)
		const auto& names = world->RaiderNames.Count() > 0 ? world->RaiderNames : RaiderNames();
		auto Name = [&]() -> cs::String
		{
			int n = raidersRaised++;
			return names[n % names.Count()] + (n >= names.Count() ? cs::Cat(" ", (n / names.Count() + 1)) : cs::String(""));
		};
		// C# evaluates the arguments left to right: Name() bumps raidersRaised before the class is chosen from it.
		cs::String leaderName = Name();
		auto leader = Fleet()->AddShip(leaderName, raidersRaised % 3 == 0 ? ShipClass::Cruiser : ShipClass::Destroyer, pirates, ShipRole::Patrol);
		// Raiders fit phaser arrays as their taste and their plunder allow, from their haven's slips (U.SPC-T505).
		auto slips = LiveHavenOf(pirates);
		FleetPatterns::Commission(leader, slips, nullptr);
		leader->Route = cs::List<int>{ body };
		// Every raider band takes its prizes with Breacher rounds fired from its leader's Breacher bay (U.SPC-T466, U.SPC-T521),
		// a destroyer fitting a second habitat to supply the teams.
		BreacherBays::Equip(leader);
		int escorts = 1 + rng.Next(2);
		for (int i = 0; i < escorts; i++)
		{
			auto e = Fleet()->AddShip(Name(), ShipClass::Destroyer, pirates, ShipRole::Escort);
			FleetPatterns::Commission(e, slips, nullptr);
			Fleet()->AddEscort(leader, e, Core::Fleet::SlotFor(i));
		}
		// Every raider band keeps a lane interdictor to pull its marks out of the hyperlanes (U.SPC-T479): a destroyer with
		// a field projector fitted into its free space (U.SPC-T507). No pulse without one.
		auto interdictor = Fleet()->AddShip(Name(), ShipClass::Destroyer, pirates, ShipRole::Escort);
		LaneInterdictors::Fit(interdictor);
		Fleet()->AddEscort(leader, interdictor, Core::Fleet::SlotFor(escorts));
		Fleet()->Station(leader, body, jd);
		double hands = Core::Crews::Complement(leader->Spec->Class) + (escorts + 1) * Core::Crews::Complement(ShipClass::Destroyer);
		Core::Crews::Draft(pirates, hands);
		world->NotifyAdded(leader);
	}

	double War::BandComplement() { return Core::Crews::Complement(ShipClass::Cruiser) + 3 * Core::Crews::Complement(ShipClass::Destroyer); }

	void War::PirateRecruiting(double jd)
	{
		if (LiveHavenOf(world->Pirates) == nullptr) return;
		int groups = 0;
		auto based = cs::Dictionary<cs::Ref<Settlement>, int>();
		for (const auto& s : Fleet()->Ships())
			if (!s->Destroyed && !s->IsEscort() && s->Faction->IsPirate && !s->Runner)
			{
				groups++;
				if (auto h = HomeHaven(s))
				{
					int n = 0;
					based[h] = (based.TryGetValue(h, n) ? n : 0) + 1;
				}
			}
		// Smuggling and protection rackets keep the Banner afloat between prizes.
		world->Pirates->Treasury += world->PirateIncomePerDay;
		if (groups >= world->PirateGroupCap || world->Pirates->Treasury < 5e7 || rng.NextDouble() > 0.08) return;
		// A new band needs hands: hostages pressed into service, and whoever else signs on.
		if (world->Pirates->Manpower < BandComplement()) return;
		// The new band sails from the standing haven with the fewest of its own.
		cs::Ref<Settlement> from = nullptr;
		int fewest = cs::Int::MaxValue;
		for (const auto& st : Economy()->Settlements)
		{
			if (st->Kind != SettlementKind::Haven || st->Fallen || st->Owner != world->Pirates) continue;
			int k = 0;
			int n = based.TryGetValue(st, k) ? k : 0;
			if (n < fewest)
			{
				fewest = n;
				from = st;
			}
		}
		world->Pirates->Treasury -= 4e7;
		RaisePirateGroup(jd, from->Body);
		world->Log(jd, cs::Cat("A new ", world->Pirates->Name, " raider group sails from ", (from->Discovered() ? from->Name : cs::String("somewhere in the dark"))), WorldEventKind::Info, from->Discovered() ? from->Body : -1);
	}

	int War::Warships(const cs::Ref<Faction>& f)
	{
		int n = 0;
		for (const auto& s : Fleet()->Ships())
			if (!s->Destroyed && s->Faction == f && s->Spec->IsMilitary) n++;
		return n;
	}

	bool War::Beaten(const cs::Ref<Faction>& f, const cs::Ref<Faction>& enemy)
	{
		int own = Warships(f), them = Warships(enemy);
		if (own >= them) return false;
		// Its yards all under the enemy's guns: beaten however few hulls it has lost since the war last flared up
		// (the count starts again then, and a siege needs only days to take a yard).
		int free = 0;
		bool besieged = false;
		for (const auto& st : Economy()->Settlements)
		{
			if (st->Owner != f || !st->HasShipyard()) continue;
			cs::Ref<Faction> by;
			if (besiegers.TryGetValue(st, by) && by == enemy) besieged = true;
			else free++;
		}
		if (besieged && free == 0) return true;
		return Losses(f) > 2 && own <= BeatenNavyShips && own * 3 < them;
	}

	void War::DiplomacyCycle(double jd)
	{
		auto a = world->RivalA;
		auto b = world->RivalB;
		if (a == nullptr || b == nullptr) return;
		auto r = Diplomacy()->Get(a, b);
		if (r == Relation::War)
		{
			// A navy that has been beaten sues for peace rather than fight on to the last hull and the last yard.
			auto loser = Beaten(a, b) ? a : Beaten(b, a) ? b : nullptr;
			if (loser != nullptr)
			{
				auto winner = loser == a ? b : a;
				Diplomacy()->Set(a, b, Relation::Ceasefire);
				ceasefireUntilJd = jd + 120 + rng.NextDouble() * 120;
				world->Log(jd, cs::Cat("The ", loser->Name, ", its navy broken, sues for peace: ceasefire with the ", winner->Name), WorldEventKind::Diplomacy);
			}
			else if (jd - warStartJd > 240 && Losses(a) > 2 && Losses(b) > 2 && rng.NextDouble() < 0.012)
			{
				Diplomacy()->Set(a, b, Relation::Ceasefire);
				ceasefireUntilJd = jd + 90 + rng.NextDouble() * 120;
				world->Log(jd, cs::Cat("Ceasefire between the ", a->Name, " and the ", b->Name), WorldEventKind::Diplomacy);
			}
		}
		else if (r == Relation::Ceasefire && jd > ceasefireUntilJd)
		{
			// Neither side goes back to war while one of them is still too weak to fight: it rebuilds first.
			if (Beaten(a, b) || Beaten(b, a))
			{
				ceasefireUntilJd = jd + 30;
				return;
			}
			Diplomacy()->Set(a, b, Relation::War);
			RevokeBookings();
			warStartJd = jd;
			losses[a] = 0;
			losses[b] = 0;
			world->Log(jd, cs::Cat("The ceasefire collapses: the ", a->Name, " and the ", b->Name, " are at war again"), WorldEventKind::Diplomacy);
		}
	}

	// ---------------------------------------------------------------- shipbuilding

	cs::Nullable<ShipClass> War::ChooseWarship(const cs::Ref<Settlement>& yard)
	{
		auto f = yard->Owner;
		if (!f->IsMilitary) return std::nullopt;
		// The patrol wings are a standing force of their own, not counted against the battle fleet's cap (U.SPC-T371).
		int count = 0;
		for (const auto& s : Fleet()->Ships())
			if (!s->Destroyed && s->Faction == f && s->Spec->IsMilitary && !s->PatrolWing) count++;
		// What it lays down, and how big a navy it keeps, are its polity's build plan (U.SPC-T423; the balanced fleet unless told otherwise).
		auto command = world->Polities != nullptr ? world->Polities->Command : nullptr;
		auto plan = command != nullptr ? command->BuildPlanOf(f) : nullptr;
		if (!plan) plan = BuildPlan::Balanced();
		if (plan->Cycle.Count() == 0) return std::nullopt;
		int cap = AtWar(f) ? world->NavyCapAtWar : world->NavyCap;
		double share = command != nullptr ? command->NavyShare(f) : 1;
		if (share != 1) cap = cs::ToInt(cs::Math::Round(cap * share));
		if (count >= cap) return std::nullopt;
		// At war, a gate breaker to take the enemy's gates (U.SPC-T478), when the yard has its keel's worth to hand.
		auto breaker = ShipClass::GateBreaker;
		if (yard->BuildPoints >= Core::Economy::HullCost(breaker) && f->Treasury >= Core::Economy::HullPrice(breaker) && Core::Crews::CanCrew(f, breaker))
		{
			int breakers = 0;
			for (const auto& s : Fleet()->Ships())
				if (!s->Destroyed && s->Faction == f && GateBreakers::IsClass(s->Spec->Class)) breakers++;
			if (GateBreakers::Wanted(f, AtWar(f), breakers)) return breaker;
		}
		return plan->Cycle[(builtWarships + yard->Id) % plan->Cycle.Count()];
	}

	const cs::List<cs::String>& War::HullNames()
	{
		static const cs::List<cs::String> names =
		{
			"Valiant", "Bastion", "Sentinel", "Corvus", "Tenacity", "Ardent", "Harrier", "Rampart", "Vigil", "Onslaught",
			"Meridian", "Paladin", "Warden", "Sabre", "Gauntlet", "Thunderer", "Steadfast", "Lodestar", "Zenith", "Implacable",
		};
		return names;
	}

	cs::Ref<Ship> War::LaunchWarship(const cs::Ref<Settlement>& yard, ShipClass cls, double jd)
	{
		builtWarships++;
		cs::String name = HullNames()[builtWarships % HullNames().Count()] + (builtWarships >= HullNames().Count() ? cs::Cat(" ", (builtWarships / HullNames().Count() + 1)) : cs::String(""));
		// Reinforce an under-strength group refitting here, or form a new one.
		cs::Ref<Ship> group = nullptr;
		for (const auto& s : Fleet()->Ships())
		{
			// Never the player's ship: escorts would follow a schedule the player isn't flying.
			if (s->Destroyed || s->IsEscort() || s->Faction != yard->Owner || !s->Spec->IsMilitary || s->Battle != nullptr || s == world->PilotedShip || s->PatrolWing) continue;
			if (s->Phase != ShipPhase::Parked || s->Orbit->Body != yard->Body) continue;
			int escorts = 0;
			for (const auto& e : Fleet()->Ships())
				if (e->Leader == s && !e->Destroyed) escorts++;
			if (escorts < 5 && s->Combat->Spec->Hull >= CombatSpec::Of(cls)->Hull) group = s;
		}
		// A new command ship (U.SPC-T477) goes to lead a group here that has none, whatever its flagship's size.
		bool command = FleetCoordination::IsCommandClass(cls);
		if (command)
		{
			cs::Ref<Ship> led = nullptr;
			for (const auto& s : Fleet()->Ships())
			{
				if (s->Destroyed || s->IsEscort() || s->Faction != yard->Owner || !s->Spec->IsMilitary || s->Battle != nullptr || s == world->PilotedShip || s->PatrolWing) continue;
				if (s->Phase != ShipPhase::Parked || s->Orbit->Body != yard->Body || GroupHasCommandShip(s)) continue;
				if (led == nullptr || s->Combat->Spec->Hull > led->Combat->Spec->Hull) led = s;
			}
			if (led != nullptr) group = led;
		}
		cs::Ref<Ship> ship;
		// The new hull is built to its navy's patterns, by its doctrine, its wealth and what this yard can build (U.SPC-T505).
		auto strategic = world->Polities != nullptr ? world->Polities->Command : nullptr;
		auto plan = strategic != nullptr ? strategic->BuildPlanOf(yard->Owner) : nullptr;
		if (group != nullptr)
		{
			ship = Fleet()->AddShip(name, cls, yard->Owner, ShipRole::Escort);
			FleetPatterns::Commission(ship, yard, plan);
			Fleet()->AddEscort(group, ship, Core::Fleet::SlotFor(FreeSlot(group)));
			if (command && !GroupHasCommandShip(group, ship)) TakeCommand(group, ship);
		}
		else
		{
			ship = Fleet()->AddShip(name, cls, yard->Owner, ShipRole::Patrol);
			FleetPatterns::Commission(ship, yard, plan);
			auto own = cs::List<int>();
			for (const auto& s : Economy()->Settlements)
				if (s->Owner == yard->Owner && !own.Contains(s->Body)) own.Add(s->Body);
			ship->Route = own.ToArray();
			world->Launch(ship, yard, jd);
		}
		// The heavy equipment its polity's build plan wants in every group, fitted to this hull if its group carries none
		// yet and it can take it (U.SPC-T507: lane security's field projector makes it the group's lane interdictor).
		auto groupPlan = strategic != nullptr ? strategic->BuildPlanOf(yard->Owner) : nullptr;
		FitGroupEquipment(ship, group ? group : ship, groupPlan ? groupPlan : BuildPlan::Balanced());
		cs::String joins = group == nullptr ? cs::String("") : ship->IsEscort() ? cs::Cat(", joining ", group->Name) : cs::Cat(", taking command of ", group->Name, "'s group");
		world->Log(jd, cs::Cat(yard->Name, " commissions the ", cs::Strings::ToLowerInvariant(ShipClassSpec::Of(cls)->DisplayName), " ", ship->Name) + joins, WorldEventKind::Launch, yard->Body);
		return ship;
	}

	void War::FitGroupEquipment(const cs::Ref<Ship>& ship, const cs::Ref<Ship>& leader, const cs::Ref<BuildPlan>& plan)
	{
		// (C#'s `plan?.GroupFittings == null`: no fittings is an empty list here.)
		if (plan == nullptr || plan->GroupFittings.Count() == 0) return;
		for (const auto& id : plan->GroupFittings)
		{
			auto kind = FacilityKinds::Of(id);
			if (kind == nullptr || GroupCarries(leader, id)) continue;
			Outfitting::Install(ship, kind);
		}
	}

	bool War::GroupCarries(const cs::Ref<Ship>& leader, const cs::String& id)
	{
		if (FleetCoordination::Count(leader, id) > 0) return true;
		for (const auto& s : Fleet()->Ships())
			if (s->Leader == leader && !s->Destroyed && FleetCoordination::Count(s, id) > 0) return true;
		return false;
	}

	bool War::GroupHasCommandShip(const cs::Ref<Ship>& leader, const cs::Ref<Ship>& except)
	{
		if (leader != except && FleetCoordination::IsCommandShip(leader)) return true;
		for (const auto& e : Fleet()->Ships())
			if (e->Leader == leader && e != except && !e->Destroyed && FleetCoordination::IsCommandShip(e)) return true;
		return false;
	}

	void War::TakeCommand(const cs::Ref<Ship>& leader, const cs::Ref<Ship>& flag)
	{
		if (leader == nullptr || flag == nullptr || flag->Leader != leader || leader->IsEscort() || leader->Destroyed || flag->Destroyed) return;
		auto station = flag->FormationSlot;
		Succession(leader, true, flag);
		if (flag->Leader != nullptr) return;
		Fleet()->AddEscort(flag, leader, station);
		leader->Route.Clear(); // (C#'s `leader.Route = null`.)
	}

	// ---------------------------------------------------------------- helpers

	void War::BumpDanger(int body, double amount)
	{
		double d = 0;
		danger.TryGetValue(body, d);
		danger[body] = d + amount;
	}

	cs::String War::Describe(const cs::Ref<Battle>& b, int side)
	{
		auto f = b->SideLeaders[side];
		if (f == nullptr) return "nobody";
		cs::Ref<Ship> flag = nullptr;
		int count = 0;
		for (const auto& u : b->Units)
		{
			if (u->Side != side || u->Kind == UnitKind::Fighter) continue;
			count++;
			if (u->Ship != nullptr && (flag == nullptr || u->Ship->Combat->Spec->Hull > flag->Combat->Spec->Hull)) flag = u->Ship;
		}
		auto owner = flag != nullptr && flag->Faction != nullptr ? flag->Faction : f;
		cs::String who = owner->IsPirate ? cs::Cat(owner->Name, " raiders") : owner->Name;
		return flag != nullptr ? cs::Cat(who, " (", flag->Name, ", ", count, " ships)") : cs::Cat(who, " defences");
	}

	cs::String War::Short(const cs::Ref<Battle>& b, int side)
	{
		auto f = b->SideLeaders[side];
		if (f != nullptr && f->IsPirate) return "pirates";
		// (C#'s `f?.Prefix is string p`: a C++ prefix is never null.)
		if (f != nullptr && f->Prefix.size() > 0) return f->Prefix;
		return side == 0 ? "attackers" : "defenders";
	}

	cs::String War::Ordinal(int n)
	{
		if (n == 2) return "Second";
		if (n == 3) return "Third";
		int tens = n % 100;
		cs::String suffix;
		if (tens >= 11 && tens <= 13) suffix = "th";
		else
			switch (n % 10)
			{
			case 1: suffix = "st"; break;
			case 2: suffix = "nd"; break;
			case 3: suffix = "rd"; break;
			default: suffix = "th"; break;
			}
		return cs::Cat(n, suffix);
	}
}
