// Port of Unity.SpaceGod Core/Combat/Battle.cs (US-T10).

#include "Combat/Battle.h"

#include "Astro.h"
#include "Combat/AlphaStrike.h"
#include "Combat/ArmourAngling.h"
#include "Combat/Arsenal.h"
#include "Combat/Boarding.h"
#include "Combat/BoardingShuttles.h"
#include "Combat/BreacherBays.h"
#include "Combat/Broadsides.h"
#include "Combat/CarrierWings.h"
#include "Combat/Choreography.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/Dogfighting.h"
#include "Combat/DriftingRounds.h"
#include "Combat/ElectronicWarfare.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/FireCones.h"
#include "Combat/Fighters.h"
#include "Combat/FleetCoordination.h"
#include "Combat/FlightDecks.h"
#include "Combat/FlightModels.h"
#include "Combat/GateBreakers.h"
#include "Combat/Gunnery.h"
#include "Combat/ModularHull.h"
#include "Combat/Morale.h"
#include "Combat/PilotAim.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/Radiators.h"
#include "Combat/RaidDoctrine.h"
#include "Combat/RearLine.h"
#include "Combat/ScreenWeapons.h"
#include "Combat/ShieldEmitters.h"
#include "Combat/SpinalMounts.h"
#include "Combat/StrandedWings.h"
#include "Combat/TachyonLances.h"
#include "Combat/WeakPatches.h"
#include "Combat/WeaponSystems.h"
#include "Combat/Collisions.h"
#include "Combat/BoardingCommand.h"
#include "Combat/BuriedBridges.h"
#include "Combat/MarineHabitats.h"
#include "BodyDefinition.h"
#include "ScaleModel.h"
#include "Embodiment/MountedTurret.h"
#include "Economy/Settlement.h"
#include "Sensors/SensorPosture.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Systems/JumpGate.h"
#include "Systems/LaneUpkeep.h"

namespace SpaceGod::Core
{
	const char* ToString(UnitKind v)
	{
		switch (v)
		{
			case UnitKind::Ship: return "Ship";
			case UnitKind::Platform: return "Platform";
			case UnitKind::Fighter: return "Fighter";
		}
		return "";
	}

	const char* ToString(DogfightManeuver v)
	{
		switch (v)
		{
			case DogfightManeuver::Attack: return "Attack";
			case DogfightManeuver::Extend: return "Extend";
			case DogfightManeuver::Break: return "Break";
			case DogfightManeuver::StrafeBurn: return "StrafeBurn";
			case DogfightManeuver::StrafeCoast: return "StrafeCoast";
			case DogfightManeuver::StrafeFlip: return "StrafeFlip";
			case DogfightManeuver::Loop: return "Loop";
			case DogfightManeuver::Roll: return "Roll";
			case DogfightManeuver::Crossing: return "Crossing";
		}
		return "";
	}

	const char* ToString(MissileKind v)
	{
		switch (v)
		{
			case MissileKind::Missile: return "Missile";
			case MissileKind::Torpedo: return "Torpedo";
			case MissileKind::Pod: return "Pod";
			case MissileKind::Emp: return "Emp";
			case MissileKind::Antimatter: return "Antimatter";
			case MissileKind::Interceptor: return "Interceptor";
			case MissileKind::Breacher: return "Breacher";
			case MissileKind::Rocket: return "Rocket";
			case MissileKind::Hive: return "Hive";
			case MissileKind::HiveMicro: return "HiveMicro";
		}
		return "";
	}

	const char* ToString(FxKind v)
	{
		switch (v)
		{
			case FxKind::Beam: return "Beam";
			case FxKind::BeamMiss: return "BeamMiss";
			case FxKind::Railgun: return "Railgun";
			case FxKind::RailgunMiss: return "RailgunMiss";
			case FxKind::PointDefense: return "PointDefense";
			case FxKind::Guns: return "Guns";
			case FxKind::ShieldHit: return "ShieldHit";
			case FxKind::Impact: return "Impact";
			case FxKind::SubsystemDestroyed: return "SubsystemDestroyed";
			case FxKind::MissileKill: return "MissileKill";
			case FxKind::UnitDestroyed: return "UnitDestroyed";
			case FxKind::MissileLaunch: return "MissileLaunch";
			case FxKind::Breach: return "Breach";
			case FxKind::PodBounce: return "PodBounce";
			case FxKind::Captured: return "Captured";
			case FxKind::Latch: return "Latch";
			case FxKind::Pulse: return "Pulse";
			case FxKind::Hail: return "Hail";
			case FxKind::MineBlast: return "MineBlast";
			case FxKind::EmpBurst: return "EmpBurst";
			case FxKind::Deployed: return "Deployed";
			case FxKind::TachyonLance: return "TachyonLance";
			case FxKind::TachyonLanceMiss: return "TachyonLanceMiss";
			case FxKind::Phaser: return "Phaser";
			case FxKind::PhaserMiss: return "PhaserMiss";
			case FxKind::Plasma: return "Plasma";
			case FxKind::PlasmaMiss: return "PlasmaMiss";
			case FxKind::FlakBurst: return "FlakBurst";
			case FxKind::Vulcan: return "Vulcan";
			case FxKind::HiveSplit: return "HiveSplit";
		}
		return "";
	}

	// ==================================================================== BattleUnit

	DVec3 BattleUnit::DockPoint(const cs::Ref<BattleUnit>& t, double standOffKm)
	{
		DVec3 right, unusedUp;
		t->Basis(right, unusedUp);
		if (dockSideFor != t || DockSide == 0)
		{
			dockSideFor = t;
			DockSide = DVec3::Dot(Pos - t->Pos, right) >= 0 ? 1 : -1;
		}
		return t->Pos + right * (DockSide * (t->Radius() * 0.35 + Radius() + standOffKm));
	}

	cs::String BattleUnit::Name() const
	{
		return Ship != nullptr ? Ship->Name : Platform != nullptr ? cs::Cat(Platform->Name, " defence platform") : Fort != nullptr ? cs::Cat(Fort->Name, " fort") : CraftHull != nullptr ? CraftHull->Name : cs::String("Fighter");
	}

	void BattleUnit::Basis(DVec3& right, DVec3& up) const
	{
		auto roof = PlayerControlled() ? Up : Roof;
		if (roof.LengthSquared() > 1e-12)
		{
			auto r = DVec3::Cross(Forward, roof);
			if (r.LengthSquared() > 1e-12)
			{
				right = r.Normalized();
				up = DVec3::Cross(right, Forward);
				return;
			}
		}
		// Level: roof is north projected off the nose. Continuous right up to the pole (U.SPC-T562); only a nose
		// exactly along the pole has no north to project, and falls back to X.
		auto upRef = DVec3::Cross(Forward, DVec3::UnitZ).LengthSquared() > 1e-12 ? DVec3::UnitZ : DVec3::UnitX;
		right = DVec3::Cross(Forward, upRef).Normalized();
		up = DVec3::Cross(right, Forward);
	}

	DVec3 BattleUnit::PointOf(const cs::Ref<SubsystemDef>& d) const { return PointLocal(DVec3(d->X, d->Y, d->Z)); }

	DVec3 BattleUnit::PointLocal(DVec3 local) const
	{
		DVec3 right, up;
		Basis(right, up);
		return Pos + (right * local.X + up * local.Y + Forward * local.Z) * LengthKm;
	}

	DVec3 BattleUnit::ToLocal(DVec3 point) const
	{
		DVec3 right, up;
		Basis(right, up);
		auto d = (point - Pos) / cs::Math::Max(LengthKm, 1e-9);
		return DVec3(DVec3::Dot(d, right), DVec3::Dot(d, up), DVec3::Dot(d, Forward));
	}

	DVec3 BattleUnit::DirToLocal(DVec3 dir) const
	{
		DVec3 right, up;
		Basis(right, up);
		return DVec3(DVec3::Dot(dir, right), DVec3::Dot(dir, up), DVec3::Dot(dir, Forward));
	}

	DVec3 BattleUnit::ShieldStrike(DVec3 point, cs::Nullable<DVec3> incoming) const
	{
		auto local = ToLocal(point);
		auto spec = Combat ? Combat->Spec : nullptr;
		if (spec == nullptr) return local;
		return ShieldEmitters::StrikePoint(spec, local, incoming.has_value() ? cs::Nullable<DVec3>(DirToLocal(*incoming)) : cs::Nullable<DVec3>());
	}

	DVec3 BattleUnit::LocalDirToWorld(DVec3 local) const
	{
		DVec3 right, up;
		Basis(right, up);
		return right * local.X + up * local.Y + Forward * local.Z;
	}

	bool BattleUnit::Bears(const cs::Ref<SubsystemDef>& d, DVec3 point) const
	{
		if (d->BearsEverywhere()) return true;
		auto to = point - PointOf(d);
		if (to.LengthSquared() < 1e-18) to = point - Pos;
		return d->Covers(Combat ? Combat->Spec : nullptr, DirToLocal(to));
	}

	bool BattleUnit::BearsAlong(const cs::Ref<SubsystemDef>& d, DVec3 dir) const
	{
		return d->BearsEverywhere() || d->Covers(Combat ? Combat->Spec : nullptr, DirToLocal(dir));
	}

	double BattleUnit::YawDegOf(DVec3 point) const
	{
		auto local = ToLocal(point);
		return Astro::WrapDegrees360(cs::Math::Atan2(local.X, local.Z) * Astro::Rad2Deg);
	}

	// ==================================================================== Battle

	Battle::Battle(int id, int body, cs::String name, DVec3 anchor, double startJd, int seed)
		: Id(id), Body(body), Name(std::move(name)), Anchor(anchor), StartJd(startJd), rng(seed)
	{
	}

	// ---------------------------------------------------------------- membership

	cs::Ref<BattleUnit> Battle::AddShip(const cs::Ref<Ship>& ship, int side, DVec3 pos, DVec3 forward)
	{
		auto u = NewUnit(UnitKind::Ship, side, ship->Faction, ship->Combat, ship->LengthKm(), pos, forward);
		u->Ship = ship;
		ship->Battle = shared_from_this();
		if (ship->Combat->Spec->Squadrons > 0) { if (Launch) Launch(shared_from_this(), u); }
		return u;
	}

	cs::Ref<BattleUnit> Battle::AddPlatform(const cs::Ref<Settlement>& s, const cs::Ref<ShipCombat>& platform, int side, DVec3 pos)
	{
		auto u = NewUnit(UnitKind::Platform, side, s->Owner, platform, CombatSpec::PlatformLengthKm, pos, DVec3::UnitX);
		u->Platform = s;
		return u;
	}

	cs::Ref<BattleUnit> Battle::AddFort(const cs::Ref<JumpGate>& gate, const cs::Ref<ShipCombat>& fort, int side, DVec3 pos)
	{
		auto u = NewUnit(UnitKind::Platform, side, gate->Owner, fort, CombatSpec::PlatformLengthKm, pos, DVec3::UnitX);
		u->Fort = gate;
		return u;
	}

	cs::Ref<BattleUnit> Battle::NewUnit(UnitKind kind, int side, const cs::Ref<Faction>& faction, const cs::Ref<ShipCombat>& combat, double lengthKm, DVec3 pos, DVec3 forward)
	{
		combat->ResetShieldTimers();
		auto u = cs::New<BattleUnit>();
		u->Id = nextUnitId++;
		u->Kind = kind;
		u->Side = side;
		u->Faction = faction;
		u->Combat = combat;
		u->LengthKm = lengthKm;
		u->Pos = pos;
		u->Forward = forward.LengthSquared() > 0 ? forward.Normalized() : DVec3::UnitX;
		u->RetargetIn = rng.NextDouble() * 2;
		u->Wander = rng.NextDouble() * 100;
		u->Circle = rng.Next(2) == 0 ? -1 : 1;
		u->MoraleThreshold = 0.55 + rng.NextDouble() * 0.5;
		if (side >= 0 && side < 2 && SideLeaders[side] == nullptr) SideLeaders[side] = faction;
		Units.Add(u);
		return u;
	}

	cs::Ref<BattleUnit> Battle::UnitOf(const cs::Ref<Ship>& ship)
	{
		for (const auto& u : Units)
			if (u->Ship == ship) return u;
		return nullptr;
	}

	bool Battle::Hostile(const cs::Ref<BattleUnit>& a, const cs::Ref<BattleUnit>& b) const { return a->Side != b->Side && a->Present() && b->Present(); }

	// ---------------------------------------------------------------- stepping

	void Battle::Advance(double seconds, bool coarse)
	{
		if (Over || seconds <= 0) return;
		accumulator += seconds;
		double unit = coarse ? CoarseStep : MaxStep;
		int cap = coarse ? MaxCoarseStepsPerAdvance : MaxStepsPerAdvance;
		int steps = cs::ToInt(cs::Math::Ceiling(accumulator / unit - 1e-9));
		double dt = unit;
		if (steps > cap)
		{
			// Far behind (a year per second): take coarser steps, up to 20 s; drop the rest.
			dt = cs::Math::Min(cs::Math::Max(20.0, unit), accumulator / cap);
			steps = cap;
		}
		for (int i = 0; i < steps && !Over; i++)
		{
			double step = cs::Math::Min(dt, accumulator);
			if (step <= 1e-9) break;
			Step(step);
			accumulator -= step;
		}
		if (accumulator > dt * 2) accumulator = 0;
	}

	void Battle::Step(double dt)
	{
		auto self = shared_from_this();
		Seconds += dt;
		TallySideStrength();
		// Who fights under which command ship this step, and who has just lost theirs (U.SPC-T477).
		FleetCoordination::Step(self, dt);
		// Which recon fighters are up to jam and paint this step (U.SPC-T485).
		ElectronicWarfare::Sweep(self);
		for (int i = 0; i < Units.Count(); i++)
		{
			auto u = Units[i];
			if (!u->Present()) continue;
			if (u->Kind == UnitKind::Fighter)
			{
				// One the player has taken flies on their stick; the rest of its squadron flies on as before (U.SPC-T422).
				cs::Ref<FighterControl> stick;
				if (Flown.Count() > 0 && Flown.TryGetValue(u, stick)) FighterAI::FlyManned(self, u, stick, dt);
				else if (FighterStep) FighterStep(self, u, dt);
				continue;
			}
			Think(u, dt);
			Move(u, dt);
		}
		// Craft down on a flight deck ride with their hull, are turned round and launched again (U.SPC-T518).
		FlightDecks::Step(self, dt);
		for (int i = 0; i < Units.Count(); i++)
		{
			auto u = Units[i];
			if (!u->Present()) continue;
			if (u->PlayerControlled()) StepPlayerWeapons(u, dt);
			Fire(u, dt);
			// A mine asked for is laid now or not at all, not saved up for when the mount is ready; a volley
			// called is fired now, and the next one builds from nothing.
			if (u->PlayerControlled())
			{
				u->PlayerMineRequested = false;
				u->PlayerDecoyRequested = false;
				if (u->PlayerVolleyRelease) u->VolleyCharge = 0;
				u->PlayerVolleyRelease = false;
			}
		}
		StepMissiles(dt);
		StepRounds(dt);
		StepMines(dt);
		StepDecoys(dt);
		StepBoarding(dt);
		StepSurrenders(dt);
		double track = 1 - cs::Math::Exp(-dt / Gunnery::TrackingLagS);
		for (const auto& u : Units)
		{
			if (!u->Present()) continue;
			u->SeenVel = u->SeenVel + (u->Vel - u->SeenVel) * track;
			auto c = u->Combat;
			// A capital ship's captain orders its batteries, braces and works damage control (U.SPC-T425).
			if (Broadsides::Commands(u)) Broadsides::Step(self, u, dt);
			// A big ship's heat fills or drains, and past its sinks' limit its crew and systems cook (U.SPC-T491).
			StepHeat(u, dt);
			if (u->Stunned > 0) u->Stunned -= dt;
			// A small craft with power to spare for its shields recharges them that much faster (U.SPC-T424).
			// A charging tachyon lance draws on the reactor, and the shields recover that much slower (U.SPC-T487).
			// The player's split on a hull they hold counts the same way, and sets how much field it holds (US-T94).
			else c->RegenShields(Seconds, dt * PowerFactor(u, PowerSystem::Shields) * TachyonLances::ShieldRegenFactor(c), ShieldCapacityOf(u));
			// No escape with boarders fighting aboard (or latched to a hull it is boarding), nor for a hull that has struck
			// its colours or been taken: drifting off with its drives cold is not running.
			if (u->Kind == UnitKind::Ship && u->Latched == nullptr && !c->Disabled() && !c->Interior->UnderBoarding() && !u->Surrendering && !u->Captured && (u->Pos.Length() > EscapeKm || (u->Fleeing && NearestHostileDistance(u) > 30)))
				u->Fled = true;
		}
		if (Fx.Count() > 600) Fx.RemoveRange(0, Fx.Count() - 600);
		CheckOver();
	}

	void Battle::CheckOver()
	{
		bool any0 = false, any1 = false, fight0 = false, fight1 = false;
		for (const auto& u : Units)
		{
			if (!u->Present() || u->Kind == UnitKind::Fighter || u->Captured) continue;
			// Marines alone make a combatant only of a hull that will fly a boarding approach: the AI's,
			// never one the player is flying (see Think), which with no guns has nothing left to fight with.
			bool willing = !u->Fleeing && !u->Surrendering && !u->Combat->Disabled() && (u->Combat->Armed() || (CanBoard(u) && !u->PlayerControlled()));
			if (u->Side == 0) { any0 = true; fight0 |= willing; }
			else { any1 = true; fight1 |= willing; }
		}
		bool fighting = any0 && any1 && (fight0 || fight1);
		Mopping = !fighting;
		if (Seconds < MaxSeconds && (fighting || Boarding() || FightersAloft() || AwaitingSurrenderTerms())) return;
		Over = true;
		Winner = fight0 && !fight1 ? 0 : fight1 && !fight0 ? 1 : any0 && !any1 ? 0 : any1 && !any0 ? 1 : -1;
	}

	bool Battle::IsPrize(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		// Its own boarding, or an ally's on its side it may join (U.SPC-T392).
		auto inside = target->Combat->Interior;
		if (inside->Boarder != nullptr && inside->Admits(u->Faction, u->Side)) return true;
		for (const auto& o : Units)
			if (o->Side == u->Side && o->Present() && (o->BoardTarget == target || o->Latched == target)) return true;
		return false;
	}

	bool Battle::CanBoard(const cs::Ref<BattleUnit>& u)
	{
		return u->Kind == UnitKind::Ship && (u->Faction->IsPirate || u->Faction->IsMilitary || (Boards != nullptr && Boards(u))) && u->Combat->MarinesAvailable() >= 15;
	}

	bool Battle::MayBoard(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		return u->Faction->IsPirate || u->Faction->IsMilitary || (WantsAlive != nullptr && WantsAlive(u, target));
	}

	bool Battle::SideCanBoard(int side)
	{
		for (const auto& o : Units)
			if (o->Present() && o->Side == side && !o->Captured && !o->Combat->Disabled() && CanBoard(o)) return true;
		return false;
	}

	bool Battle::Boarding() const
	{
		for (const auto& u : Units)
		{
			if (!u->Present()) continue;
			if (u->Combat->Interior->UnderBoarding() || u->BoardTarget != nullptr || u->Latched != nullptr) return true;
		}
		return false;
	}

	bool Battle::FightersAloft() const
	{
		for (const auto& u : Units)
			if (u->Kind == UnitKind::Fighter && u->Present() && !Flown.ContainsKey(u) && StrandedWings::CanLandOn(u->Fighter->LandsOn())) return true;
		return false;
	}

	double Battle::NearestHostileDistance(const cs::Ref<BattleUnit>& u)
	{
		double best = cs::Double::MaxValue;
		for (const auto& o : Units)
			if (Hostile(u, o) && o->Kind != UnitKind::Fighter && !o->Captured && (o->Combat->Armed() || CanBoard(o)))
				best = cs::Math::Min(best, (o->Pos - u->Pos).Length());
		return best;
	}

	// ---------------------------------------------------------------- decisions

	void Battle::Think(const cs::Ref<BattleUnit>& u, double dt)
	{
		auto c = u->Combat;
		if (u->Captured || u->Latched != nullptr || u->Surrendering) return;
		if (u->Kind == UnitKind::Ship && !u->PlayerControlled() && !c->Disabled() && StrikesColours(u))
		{
			Surrender(u);
			return;
		}
		if (u->Kind == UnitKind::Ship && u->Faction->IsPirate && !u->PlayerControlled())
		{
			MaybeDemandCargo(u);
			// The cargo in hand and nothing left worth taking: raiders withdraw rather than trade fire with escorts for
			// nothing (U.SPC-T374).
			if (!u->Fleeing && !c->Disabled() && RaidDone(u)) u->Fleeing = true;
		}
		if (u->Kind == UnitKind::Ship)
		{
			bool merchant = u->Ship != nullptr && !u->Ship->Spec->IsMilitary && !u->Faction->IsPirate;
			// Whether to run is the player's call when the player is flying it.
			if (c->Disabled() || u->PlayerControlled()) u->Fleeing = false;
			else if (!u->Fleeing && (merchant || c->HullFraction() < 0.25 || (!c->Armed() && !CanBoard(u)) || Routed(u->Side)))
				u->Fleeing = true;
			// Only the AI flies a boarding approach (Move leaves a player-flown hull alone), so a
			// player-flown hull never holds a boarding target it would never close on.
			// Nor does a ship its flagship has ordered into line break off to board (U.SPC-T425).
			if (u->PlayerControlled() || Broadsides::HoldsFormation(u)) u->BoardTarget = nullptr;
			else ChooseBoarding(u);
			// A carrier sends its boarding shuttles to the prizes instead (U.SPC-T486).
			SendShuttles(u, dt);
			// The wing orders the player gave from its helm lapse once nobody of ours holds it or one of its craft (US-T106).
			HelmOrdersLapse(u);
		}
		u->RetargetIn -= dt;
		if (u->Target == nullptr || !u->Target->Present() || u->RetargetIn <= 0)
		{
			u->Target = PickTarget(u);
			// Under a command ship its staff has the next mark picked out already; with it lost, nobody has (U.SPC-T477).
			u->RetargetIn = ((c->BridgeIntact() ? 3 : 7) + rng.NextDouble() * 3) * FleetCoordination::RetargetFactor(u);
		}
	}

	bool Battle::Routed(int side)
	{
		if (Seconds < 120) return false;
		double mine = 0, theirs = 0;
		for (const auto& u : Units)
		{
			if (!u->Present()) continue;
			double s = u->Combat->Strength();
			if (u->Side == side) mine += s;
			else theirs += s;
		}
		return theirs > 0 && mine < theirs * 0.22;
	}

	cs::Ref<BattleUnit> Battle::PickTarget(const cs::Ref<BattleUnit>& u)
	{
		cs::Ref<BattleUnit> best = nullptr;
		double bestScore = cs::Double::MaxValue;
		cs::Ref<BattleUnit> flagship = SideFlagship(u->Side);
		// The hull its command ship is fighting, to concentrate on (U.SPC-T477).
		auto focus = FleetCoordination::FocusTarget(u);
		for (const auto& o : Units)
		{
			if (!Hostile(u, o) || o->Captured || o->Surrendering) continue;
			// A hunter wants its quarry's captain alive: a disabled flagship is boarded, not finished off.
			if (o->Combat->Disabled() && WantsAlive != nullptr && WantsAlive(u, o) && SideCanBoard(u->Side)) continue;
			// Raiders rob a merchant or civilian hull, they don't destroy it (U.SPC-T374): once it lies dead in space
			// they hold their fire, and demand its cargo instead (see MaybeDemandCargo).
			if (o->Combat->Disabled() && Spares(u, o)) continue;
			double score = (o->Pos - u->Pos).Length();
			// A disabled hull is a prize, not a threat: shoot it only if nothing else is left.
			if (o->Combat->Disabled()) score += 40;
			if (o->Kind == UnitKind::Fighter) score += u->Kind == UnitKind::Fighter ? 0 : 25;
			else if (u->Kind == UnitKind::Fighter) score -= 4;
			if (o->Ship != nullptr && !o->Ship->Spec->IsMilitary) score += u->Faction->IsPirate ? -15 : 10;
			// A lane tender is the prize of the fight (U.SPC-T481): sink it and its lane goes dark.
			if (LaneUpkeep::IsTender(o->Ship)) score -= LaneUpkeep::TenderFocus()->Value;
			// Raiders kill a gate picket first, before it can warn its navy (U.SPC-T480).
			if (o->Ship != nullptr && !o->Combat->Disabled()) score -= RaidDoctrine::TargetBias(u->Faction, o->Ship);
			// A command ship is the priority target (U.SPC-T477): sink it and its whole fleet fights uncoordinated.
			if (FleetCoordination::Coordinates(o)) score -= FleetCoordination::Priority()->Value;
			// A frigate's torpedoes are for the capitals (U.SPC-T475), and so are a dreadnought's spinal guns (U.SPC-T490).
			score -= AlphaStrike::TargetBias(u, o);
			score -= RearLine::TargetBias(u, o);
			// A gate breaker is there to break the gate's forts (U.SPC-T478).
			score -= GateBreakers::TargetBias(u, o);
			if (o->Kind == UnitKind::Platform) score += 6;
			// US-T180: a crewed fighter goes for the capitals, an interceptor for the craft round them.
			score -= Choreography::TargetBias(u, o);
			if (flagship != nullptr && flagship != u && flagship->Target == o) score -= 6;
			if (focus != nullptr && focus == o) score -= FleetCoordination::Focus()->Value;
			score *= 0.85 + rng.NextDouble() * 0.3;
			// Stickiness (U.SPC-T560): the current mark keeps the gun unless a rival is clearly better, so heading settles.
			if (o == u->Target) score *= TargetStickiness;
			if (score < bestScore)
			{
				bestScore = score;
				best = o;
			}
		}
		return best;
	}

	cs::Ref<BattleUnit> Battle::SideFlagship(int side)
	{
		cs::Ref<BattleUnit> best = nullptr;
		bool command = false;
		for (const auto& u : Units)
		{
			if (!u->Present() || u->Side != side || u->Kind == UnitKind::Fighter) continue;
			bool c = FleetCoordination::Coordinates(u);
			if (best == nullptr || (c && !command) || (c == command && u->Combat->Spec->Hull > best->Combat->Spec->Hull))
			{
				best = u;
				command = c;
			}
		}
		return best;
	}

	void Battle::Move(const cs::Ref<BattleUnit>& u, double dt)
	{
		// The Runtime pilot loop is the single source of truth for a player-flown hull's pose;
		// fighting it here would mean the battle sees a ghost position the player never flew to.
		if (u->PlayerControlled()) return;
		auto c = u->Combat;
		if (u->Latched != nullptr)
		{
			// Fixed to the hull it is boarding.
			u->Pos = u->Latched->PointLocal(u->LatchLocal);
			u->Vel = u->Latched->Vel;
			return;
		}
		if (c->Disabled() || u->Captured || u->Surrendering || u->Stunned > 0)
		{
			// Adrift: no drive, slowly losing what way it had.
			u->Vel = u->Vel * cs::Math::Max(0.0, 1 - 0.002 * dt);
			u->Pos = u->Pos + u->Vel * dt;
			return;
		}
		if (u->BoardTarget != nullptr && !u->Fleeing)
		{
			Approach(u, dt);
			return;
		}
		if (!u->Fleeing && Dogfights(u))
		{
			Dogfight(u, dt);
			return;
		}
		// ...and its drive (U.SPC-T491).
		double maxSpeed = c->Spec->MaxSpeed * (0.25 + 0.75 * c->EngineFactor()) * Radiators::Throttle(u);
		if (maxSpeed <= 0)
		{
			u->Vel = DVec3::Zero;
			if (u->Target != nullptr) TurnToward(u, u->Target->Pos - u->Pos, 3, dt);
			return;
		}

		DVec3 desired;
		cs::Nullable<DVec3> strikeFace;
		double burn = 1;
		if (u->Fleeing)
		{
			auto away = u->Pos - HostileCentroid(u);
			if (away.LengthSquared() < 1e-6) away = u->Pos.LengthSquared() > 0 ? u->Pos : DVec3::UnitX;
			desired = away.Normalized() * maxSpeed;
		}
		else if (u->Target != nullptr && AlphaStrike::Flies(u) && !Broadsides::HoldsFormation(u))
		{
			// A frigate's alpha strike (U.SPC-T475): burn in nose-on, loose the volley, flip and burn back out.
			DVec3 strikeHeading;
			burn = AlphaStrike::Steer(u, maxSpeed, desired, strikeHeading);
			maxSpeed *= burn;
			strikeFace = strikeHeading;
		}
		else if (u->Target != nullptr && RearLine::Flies(u) && !Broadsides::HoldsFormation(u))
		{
			// A dreadnought holds the rear and snipes from its spinal guns' extreme reach, its bow on the mark (U.SPC-T490).
			DVec3 laid;
			RearLine::Steer(shared_from_this(), u, maxSpeed, desired, laid);
			strikeFace = laid;
		}
		else if (u->Target != nullptr)
		{
			auto t = u->Target;
			auto d = u->Pos - t->Pos;
			double dist = cs::Math::Max(d.Length(), 1e-6);
			double range = PreferredRange(u);
			auto dir = d / dist;
			auto spot = t->Pos + dir * range;
			auto around = DVec3::Cross(DVec3::UnitZ, dir);
			if (around.LengthSquared() < 1e-6) around = DVec3::UnitY;
			// US-T164: a capital steams across the enemy's line broadside-on and comes about to fight its fresh side; a
			// corvette, frigate or destroyer makes passes at speed. Anything else circles at range as before.
			if (Choreography::IsCapital(u)) desired = Choreography::Crossing(shared_from_this(), u, t, range, maxSpeed, dt);
			else if (Choreography::IsLightWarship(u)) desired = Choreography::LightPass(shared_from_this(), u, t, range, maxSpeed);
			else desired = (spot - u->Pos) * 0.02 + around.Normalized() * (maxSpeed * 0.35 * u->Circle);
			if (u->Leader != nullptr && u->Leader->Present() && !u->Leader->Fleeing)
			{
				// Escorts hold loose station on their flagship while it fights.
				auto station = StationOf(u);
				desired = desired * 0.45 + (station - u->Pos) * 0.03 * 0.55 + u->Leader->Vel * 0.55;
			}
			// A hull carrying the screen keeps between the capital it guards and the enemy, so what comes at the capital
			// passes through its umbrella first (U.SPC-T489).
			double screen = ScreenWeapons::ScreenStation()->Value;
			if (screen > 0 && ScreenWeapons::Screens(c))
				if (auto ward = ScreenWeapons::WardOf(shared_from_this(), u))
				{
					auto post = ScreenWeapons::StationOn(ward, ThreatTo(ward, u));
					desired = desired * (1 - screen) + ((post - u->Pos) * 0.05 + ward->Vel) * screen;
				}
		}
		else
		{
			desired = -u->Pos * 0.01;
		}
		// In a formation its flagship ordered (U.SPC-T425) it keeps its station tightly, fighting from there.
		bool held = !u->Fleeing && Broadsides::HoldsFormation(u);
		if (held) desired = (StationOf(u) - u->Pos) * 0.05 + u->Leader->Vel;
		if (desired.Length() > maxSpeed) desired = desired.Normalized() * maxSpeed;

		double accel = c->Spec->Accel * (0.3 + 0.7 * c->EngineFactor()) * burn;
		auto dv = desired - u->Vel;
		double step = accel * dt;
		u->Vel = u->Vel + (dv.Length() > step ? dv.Normalized() * step : dv);
		u->Pos = u->Pos + u->Vel * dt;

		bool broadside = !u->Fleeing && !strikeFace.has_value() && u->Target != nullptr;
		auto face = u->Fleeing ? u->Vel
			: strikeFace.has_value() ? *strikeFace
			: u->Target == nullptr ? (held ? u->Leader->Forward : u->Vel)
			: BroadsideFacing(u, (u->Target->Pos - u->Pos).Normalized());
		// US-T164: a light warship's nose weaves off its line, jinking, rather than sitting dead steady on it.
		if (broadside && Choreography::IsLightWarship(u)) face = Choreography::Weave(shared_from_this(), u, face);
		if (WeakPatches::Steers(u))
		{
			// A capital's captain rolls and turns its hull to keep its weak patches out of the heaviest fire (U.SPC-T502).
			auto bearings = broadside ? ConeFirepower(u, (u->Target->Pos - u->Pos).Normalized(), ArmourAngling::AiSlants(u) && FireCones::AiBringsGunsToBear()->Value) : nullptr;
			DVec3 faceIn = face, roof;
			WeakPatches::Orient(shared_from_this(), u, faceIn, bearings, bearings != nullptr ? cs::Nullable<DVec3>((u->Target->Pos - u->Pos).Normalized()) : cs::Nullable<DVec3>(), cs::Nullable<DVec3>(), dt,
				face, roof, cs::Nullable<DVec3>(u->Forward));
			TurnToward(u, face, c->Spec->TurnRateDegS, dt);
			WeakPatches::RollToward(u, roof, c->Spec->TurnRateDegS, dt);
			return;
		}
		TurnToward(u, face, c->Spec->TurnRateDegS, dt);
		if (u->Roof.LengthSquared() > 1e-12) WeakPatches::RollToward(u, WeakPatches::LevelRoof(u->Forward), c->Spec->TurnRateDegS, dt);
	}

	// ---------------------------------------------------------------- dogfighting (U.SPC-T274)

	bool Battle::Dogfights(const cs::Ref<BattleUnit>& u)
	{
		return u->Kind == UnitKind::Ship && u->Ship != nullptr
			&& (u->Ship->Spec->Class == ShipClass::Fighter || u->Ship->Spec->Class == ShipClass::Interceptor || u->Ship->Spec->Class == ShipClass::Courier);
	}

	void Battle::Dogfight(const cs::Ref<BattleUnit>& u, double dt)
	{
		auto c = u->Combat;
		auto cls = u->Ship->Spec->Class;
		auto tune = PilotTuning::Of(cls);
		auto hull = HullDynamics::Of(c->Spec);
		double engines = 0.4 + 0.6 * c->EngineFactor();
		double top = tune.MaxSpeed * engines;
		double speed = top;
		bool boost = false;

		// The player first, if near enough to be worth the chase.
		for (const auto& o : Units)
			if (o->PlayerControlled() && Hostile(u, o) && (o->Pos - u->Pos).Length() < HuntPlayerKm)
			{
				u->Target = o;
				break;
			}
		auto t = u->Target != nullptr && u->Target->Present() && Hostile(u, u->Target) ? u->Target : nullptr;

		// US-T204: on anything but a capital (whose strafing runs below stay), the Newtonian dogfight a carrier's craft
		// flies (US-T188): facing apart from its track, its catalog turn, roll, thrust and braking, and named manoeuvres
		// picked by the situation, each pilot on its own deterministic draws.
		if (t != nullptr && !Choreography::Strafeable(t))
		{
			if (Choreography::Strafing(u)) Choreography::EndRun(u);
			u->Maneuver = DogfightManeuver::Attack;
			u->ManeuverLeft = 0;
			if (DogfightManeuvers::Fly(shared_from_this(), u, t, dt))
			{
				// Its roll kept for the crewed flight model, should it next fly a strafing run.
				u->Up = u->Roof;
				return;
			}
		}
		u->Tactics.FoeId = 0;
		u->Tactics.Left = 0;

		auto threat = OnItsTail(u);
		u->ManeuverLeft -= dt;
		double crewTurn = tune.TurnRateDegS * DogfightTurnShare * engines;
		if (threat != nullptr && u->Maneuver != DogfightManeuver::Break)
		{
			u->Maneuver = DogfightManeuver::Break;
			u->ManeuverLeft = 1.6 + rng.NextDouble();
			u->BreakSide = rng.Next(2) == 0 ? -1 : 1;
		}
		else if (u->ManeuverLeft <= 0 && (u->Maneuver == DogfightManeuver::Break || u->Maneuver == DogfightManeuver::Extend))
		{
			// US-T164: out of an extension, half the time it pulls a loop back onto the target rather than turning flat.
			bool loop = u->Maneuver == DogfightManeuver::Extend && t != nullptr && !Choreography::Strafeable(t) && rng.NextDouble() < 0.5;
			u->Maneuver = loop ? DogfightManeuver::Loop : DogfightManeuver::Attack;
			u->ManeuverLeft = loop ? 180.0 / cs::Math::Max(crewTurn, 1.0) + 1.5 : 0;
		}

		// US-T164: on a capital, the strafing run (burn, coast down its length with the guns on it, flip and burn);
		// on a fighter, the loops and rolling breaks of a furball. A break off its tail comes first all the same.
		if (u->Maneuver != DogfightManeuver::Break && u->Maneuver != DogfightManeuver::Extend)
		{
			Choreography::Flight trick;
			bool flown = false;
			if (t != nullptr && Choreography::Strafeable(t))
			{
				trick = Choreography::StrafeRun(shared_from_this(), u, t, crewTurn, dt);
				flown = true;
			}
			else
			{
				if (Choreography::Strafing(u)) Choreography::EndRun(u);
				flown = Choreography::FighterTurn(shared_from_this(), u, t, crewTurn, dt, trick);
			}
			if (flown)
			{
				auto steer = trick.Goal;
				if (u->Pos.Length() > EscapeKm * 0.8) steer = steer.Normalized() + (-u->Pos).Normalized() * 1.5;
				FlyCrewed(u, cls, tune, hull, engines, steer, trick.Throttle, trick.Boost, dt, trick.Coast, trick.Roll);
				return;
			}
		}
		else if (Choreography::Strafing(u)) Choreography::EndRun(u);

		DVec3 right, up;
		u->Basis(right, up);
		DVec3 goal;
		double roll = 0;
		switch (u->Maneuver)
		{
			case DogfightManeuver::Break:
				// Hard over to one side and a little up, under boost, rolling into it (US-T164).
				goal = right * u->BreakSide + up * 0.35 + u->Forward * 0.2;
				boost = true;
				roll = u->BreakSide;
				break;
			case DogfightManeuver::Extend:
				goal = u->Forward + (t != nullptr ? (u->Pos - t->Pos).Normalized() * 0.4 : DVec3::Zero);
				boost = true;
				if (t != nullptr && (t->Pos - u->Pos).Length() > ExtendKm) u->ManeuverLeft = 0;
				break;
			default:
			{
				if (t == nullptr)
				{
					goal = -u->Pos;
					speed *= 0.5;
					break;
				}
				auto rel = t->Pos - u->Pos;
				double d = rel.Length();
				double rounds = RoundSpeedOf(c);
				auto aimAt = rounds > 0 ? Gunnery::LeadPoint(u->Pos, u->Vel, t->Pos, t->Vel, rounds) : t->Pos;
				goal = aimAt - u->Pos;
				// A weave on the way in, so the target's gunners have a hard time of it too.
				if (d > 0.8)
				{
					double w = cs::Math::Sin(Seconds * 2.3 + u->Wander) * 0.12;
					goal = goal.Normalized() + (right * w + up * w * 0.5);
				}
				// On its tail, hold station rather than overrun it.
				if (d < 1.2) speed = cs::Math::Max(speed * 0.6, cs::Math::Min(speed, t->Vel.Length() + 0.05));
				if (d < MergeKm)
				{
					u->Maneuver = DogfightManeuver::Extend;
					u->ManeuverLeft = 2 + rng.NextDouble();
				}
				break;
			}
		}
		// Never out of the fight altogether: past most of the escape radius, it turns back in.
		if (u->Pos.Length() > EscapeKm * 0.8) goal = goal.Normalized() + (-u->Pos).Normalized() * 1.5;
		FlyCrewed(u, cls, tune, hull, engines, goal, top > 0 ? speed / top : 0, boost, dt, false, roll);
	}

	void Battle::FlyCrewed(const cs::Ref<BattleUnit>& u, ShipClass cls, const PilotTuning& tune, const HullDynamics& hull, double engines, DVec3 goal, double throttle, bool boost, double dt,
		bool coast, double roll)
	{
		auto crew = PilotTuning(tune.MaxSpeed * engines, tune.TurnRateDegS * DogfightTurnShare * engines, tune.BoostSpeedMultiplier,
			tune.BoostDurationS, 1, 0, tune.RollFactor);
		auto drives = HullDynamics(hull.MassKt, hull.MainAccel * engines, hull.RetroAccel * engines, hull.LateralAccel * engines,
			hull.VerticalAccel * engines, hull.PitchYawAngularAccel, hull.RollAngularAccel);
		auto f = u->Forward;
		auto up = u->Up - f * DVec3::Dot(u->Up, f);
		PilotState state{};
		state.Pos = u->Pos;
		state.Vel = u->Vel;
		state.Forward = f;
		state.Up = up.LengthSquared() > 1e-12 ? up.Normalized() : PilotState::LevelUp(f);
		state.AngularVelocity = u->TurnSpin;
		PilotInput input{};
		input.DesiredForward = goal.LengthSquared() > 1e-12 ? goal.Normalized() : DVec3::Zero;
		input.Throttle = coast ? 0 : cs::Math::Max(0.0, cs::Math::Min(1.0, throttle));
		input.Boost = boost && !coast;
		input.FlightAssistOff = coast;
		input.Roll = cs::Math::Max(-1.0, cs::Math::Min(1.0, roll));
		input.InCombat = true;
		FlightModelRegistry::Default()->For(cls)->Step(state, u->Combat, drives, crew, input, DVec3::Zero, dt);
		u->Pos = state.Pos;
		u->Vel = state.Vel;
		u->Forward = state.Forward;
		u->Up = state.Up;
		// US-T164: drawn with the roll its crew flies it at (Basis reads Roof for a hull the battle flies), not held level.
		u->Roof = state.Up;
		u->TurnSpin = state.AngularVelocity;
	}

	cs::Ref<BattleUnit> Battle::OnItsTail(const cs::Ref<BattleUnit>& u)
	{
		for (const auto& o : Units)
		{
			if (!Hostile(u, o) || o->Combat->Disabled() || o->Stunned > 0 || !o->Combat->Armed() || o->Surrendering || o->Captured) continue;
			auto to = o->Pos - u->Pos;
			double d = to.Length();
			if (d < 1e-6 || d > TailKm) continue;
			auto dir = to / d;
			if (DVec3::Dot(u->Forward, dir) < -0.3 && DVec3::Dot(o->Forward, -dir) > 0.94) return o;
		}
		return nullptr;
	}

	double Battle::RoundSpeedOf(const cs::Ref<ShipCombat>& c)
	{
		double speed = 0;
		for (const auto& s : c->Subsystems)
		{
			auto w = s->Def->Weapon;
			if (s->Destroyed() || w == nullptr || w->Missile || w->AntiSmall || w->Supporting()) continue;
			speed = cs::Math::Max(speed, w->MuzzleSpeedKmS);
		}
		return speed;
	}

	DVec3 Battle::BroadsideFacing(const cs::Ref<BattleUnit>& u, DVec3 toTarget)
	{
		// Only fire coming back from the target is worth slanting the hull to.
		auto foe = u->Target;
		bool slant = ArmourAngling::AiSlants(u) && FireCones::AiBringsGunsToBear()->Value
			&& foe != nullptr && foe->Combat->Armed() && !foe->Combat->Disabled() && foe->Stunned <= 0;
		auto byBearing = ConeFirepower(u, toTarget, slant);
		if (byBearing == nullptr) return toTarget;
		int bestBearing = 0;
		double best = -1;
		// US-T164: a crossing capital lays its bow along its course between broadsides that bear alike, so coming about
		// (Choreography::Crossing) turns its other side, and that side's shield arrays, to the enemy.
		auto course = Choreography::Course(u);
		bool steaming = course.LengthSquared() > 1e-12;
		if (!slant)
		{
			double top = 0;
			for (auto [key, value] : *byBearing) top = cs::Math::Max(top, value);
			for (auto [key, value] : *byBearing)
			{
				double score = steaming ? value + top * 0.02 * DVec3::Dot(BowFor(toTarget, key).Normalized(), course) : value;
				if (score > best + 1e-9) { best = score; bestBearing = key; }
			}
			return BowFor(toTarget, bestBearing);
		}
		// Slanting the hull to its target's fire turns part of every hit aside on its armour (U.SPC-T534): the captain
		// gives up a share of its guns' bearing for it, by doctrine, and keeps to the heading nearest its own.
		double most = 0;
		for (auto [key, value] : *byBearing) most = cs::Math::Max(most, value);
		if (most <= 0) return toTarget;
		double weight = ArmourAngling::AiWeight()->Value;
		auto t = toTarget.Normalized();
		best = cs::Double::NegativeInfinity;
		for (auto [key, value] : *byBearing)
		{
			auto f = BowFor(t, key).Normalized();
			auto up = WeakPatches::LevelRoof(f);
			auto right = DVec3::Cross(f, up).Normalized();
			auto local = DVec3(DVec3::Dot(t, right), DVec3::Dot(t, up), DVec3::Dot(t, f));
			double score = value / most - weight * ArmourAngling::Harm(u->Combat->Spec, local) + 1e-3 * DVec3::Dot(f, u->Forward)
				+ (steaming ? 0.02 * DVec3::Dot(f, course) : 0);
			if (score > best + 1e-9) { best = score; bestBearing = key; }
		}
		return BowFor(toTarget, bestBearing);
	}

	cs::Ref<cs::Dictionary<int, double>> Battle::ConeFirepower(const cs::Ref<BattleUnit>& u, DVec3 toTarget, bool slant)
	{
		if (toTarget.LengthSquared() < 1e-18) return nullptr;
		auto spec = u->Combat->Spec;
		// C# holds null until the first gun; an empty list stands in for it (hasGuns).
		cs::List<std::pair<cs::Ref<SubsystemDef>, double>> guns;
		bool hasGuns = false;
		for (const auto& s : u->Combat->Subsystems)
		{
			if (!s->Def->IsWeapon() || s->Destroyed() || s->Def->Weapon->AntiSmall || s->Kind() == SubsystemKind::BoardingPods || s->Def->Weapon->Supporting()) continue;
			auto w = s->Def->Weapon;
			if (w->Missile && s->Ammo <= 0) continue;
			hasGuns = true;
			guns.Add({ s->Def, w->Damage * w->Salvo / w->Cooldown });
		}
		if (!hasGuns) return nullptr;
		auto t = toTarget.Normalized();
		auto Score = [&](int bearing)
		{
			auto f = BowFor(t, bearing).Normalized();
			auto up = WeakPatches::LevelRoof(f);
			auto right = DVec3::Cross(f, up).Normalized();
			auto local = DVec3(DVec3::Dot(t, right), DVec3::Dot(t, up), DVec3::Dot(t, f));
			double sum = 0;
			for (const auto& [def, fire] : guns)
				if (def->Covers(spec, local)) sum += fire;
			return sum;
		};
		auto byBearing = cs::New<cs::Dictionary<int, double>>();
		(*byBearing)[0] = Score(0);
		if (!FireCones::AiBringsGunsToBear()->Value) return byBearing;
		for (const auto& [def, unusedFire] : guns)
		{
			auto axis = def->AxisOn(spec);
			if (axis.X * axis.X + axis.Z * axis.Z < 0.01) continue;
			int bearing = cs::ToInt(cs::Math::Round(Astro::WrapDegrees360(cs::Math::Atan2(axis.X, axis.Z) * Astro::Rad2Deg))) % 360;
			if (!byBearing->ContainsKey(bearing)) (*byBearing)[bearing] = Score(bearing);
		}
		// With slant, the strongest headings slanted either way too, so a captain can weigh laying its strongest battery
		// at an angle to the fire while its guns still bear (U.SPC-T534, see ArmourAngling).
		if (slant)
		{
			int off = cs::ToInt(cs::Math::Round(ArmourAngling::SlantDeg));
			double most = 0;
			for (auto [key, value] : *byBearing) most = cs::Math::Max(most, value);
			cs::List<int> strongest;
			for (auto [key, value] : *byBearing)
				if (most > 0 && value >= most - 1e-9) strongest.Add(key);
			for (int bearing : strongest)
				for (int sign = -1; sign <= 1; sign += 2)
				{
					int b = ((bearing + sign * off) % 360 + 360) % 360;
					if (!byBearing->ContainsKey(b)) (*byBearing)[b] = Score(b);
				}
		}
		return byBearing;
	}

	DVec3 Battle::BowFor(DVec3 toTarget, int bearingDeg) { return RotateYaw(toTarget, bearingDeg); }

	DVec3 Battle::RotateYaw(DVec3 v, double angleDeg)
	{
		double rad = angleDeg * Astro::Deg2Rad;
		double cos = cs::Math::Cos(rad), sin = cs::Math::Sin(rad);
		// Turn about the vertical axis made square to v (in v's vertical plane), so the result lies exactly
		// angleDeg off v even when v is above or below the horizon; level v gives the plain vertical axis.
		auto axis = DVec3::UnitZ;
		auto vn = v.LengthSquared() > 1e-18 ? v.Normalized() : v;
		auto square = axis - vn * DVec3::Dot(axis, vn);
		if (square.LengthSquared() > 1e-12) axis = square.Normalized();
		return v * cos + DVec3::Cross(axis, v) * sin + axis * DVec3::Dot(axis, v) * (1 - cos);
	}

	DVec3 Battle::StationOf(const cs::Ref<BattleUnit>& u)
	{
		auto l = u->Leader;
		// Level frame (U.SPC-T565): a weak-patch roll of the leader must not swing line-abreast slots round it.
		auto up = WeakPatches::LevelRoof(l->Forward);
		auto right = DVec3::Cross(l->Forward, up).Normalized();
		double t = Seconds * 0.05 + u->Wander;
		auto wander = DVec3(cs::Math::Sin(t * 1.3), cs::Math::Sin(t * 0.9 + 1.7), cs::Math::Sin(t * 0.7 + 4.1)) * 0.35;
		auto s = u->Slot + wander;
		return l->Pos + right * s.X + up * s.Y + l->Forward * s.Z;
	}

	void Battle::TurnToward(const cs::Ref<BattleUnit>& u, DVec3 dir, double degPerSec, double dt)
	{
		if (dir.LengthSquared() < 1e-12) return;
		auto want = dir.Normalized();
		auto f = u->Forward;
		double rate = degPerSec * Astro::Deg2Rad;
		if (rate <= 0 || dt <= 0) return;
		double alpha = u->Combat != nullptr ? HullDynamics::Of(u->Combat->Spec).PitchYawAngularAccel : 0;
		alpha = cs::Math::Max(alpha, rate / 1.5);
		double maxDw = alpha * dt;

		auto axis = DVec3::Cross(f, want);
		double sin = axis.Length(), cos = DVec3::Dot(f, want);
		double angle = cs::Math::Atan2(sin, cos);
		if (sin < 1e-9) axis = cos > 0 ? DVec3::Zero : DVec3::Cross(f, cs::Math::Abs(f.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX);
		auto target = axis.LengthSquared() > 0 ? axis.Normalized() * cs::Math::Min(rate, cs::Math::Sqrt(2 * alpha * angle)) : DVec3::Zero;

		auto w = u->TurnSpin;
		w = w - f * DVec3::Dot(w, f);
		auto dw = target - w;
		if (dw.Length() > maxDw) dw = dw.Normalized() * maxDw;
		w = w + dw;

		double spin = w.Length();
		// Arriving this frame with little swing left to kill: settle on the heading rather than hunt about it.
		if (angle <= spin * dt + 1e-12 && spin <= 3 * maxDw + 1e-12)
		{
			u->Forward = want;
			u->TurnSpin = DVec3::Zero;
			return;
		}
		if (spin > 0)
		{
			auto nf = PilotFlight::RotateAbout(f, w / spin, spin * dt).Normalized();
			u->Forward = nf;
			w = w - nf * DVec3::Dot(w, nf);
		}
		u->TurnSpin = w;
	}

	double Battle::PreferredRange(const cs::Ref<BattleUnit>& u)
	{
		double best = 0;
		for (const auto& s : u->Combat->Subsystems)
		{
			auto w = s->Def->Weapon;
			// A tachyon lance reaches far past the line's guns (U.SPC-T487): a hull fights at its battery's range and
			// the lance reaches out from there, so it never hangs back where only the lance bears.
			if (w == nullptr || s->Destroyed() || w->AntiSmall || w->Lance) continue;
			if (w->Missile && s->Ammo <= 0) continue;
			best = cs::Math::Max(best, w->Missile ? w->RangeKm * 0.5 : w->RangeKm * 0.7);
		}
		return best > 0 ? best : 12;
	}

	DVec3 Battle::HostileCentroid(const cs::Ref<BattleUnit>& u)
	{
		auto sum = DVec3::Zero;
		int n = 0;
		for (const auto& o : Units)
		{
			if (!Hostile(u, o)) continue;
			sum = sum + o->Pos;
			n++;
		}
		return n > 0 ? sum / n : DVec3::Zero;
	}

	// ---------------------------------------------------------------- weapons

	void Battle::Fire(const cs::Ref<BattleUnit>& u, double dt)
	{
		auto c = u->Combat;
		// Knocked out by a pulse or an EMP: no gun on the hull fires until its systems come back.
		if (u->Captured || u->Surrendering || u->Stunned > 0) return;
		// Powered down by the player (US-T93): the reactor at minimum, no gun fires until the restart is done.
		if (SensorPosture::Offline(c)) return;
		// Surer under a command ship's targeting computers, worse in the confusion after losing it (U.SPC-T477).
		// Worse again with an enemy recon fighter jamming it (U.SPC-T485).
		double accuracyScale = (c->BridgeIntact() ? 1 : 0.6) * FleetCoordination::AccuracyFactor(u) * ElectronicWarfare::JamFactor(shared_from_this(), u);
		// The shared sensor picture: gunners lay on the command ship's track, not their own (U.SPC-T477).
		double picture = FleetCoordination::SharedPicture(u);
		// A small craft's weapons reload as fast as the power it gives them (U.SPC-T424).
		// A big ship that cannot shed its heat has its crew throttle the guns' reloading (U.SPC-T491).
		double weaponPower = PowerFactor(u, PowerSystem::Weapons) * Radiators::Throttle(u); // and a held hull's, US-T94
		for (const auto& s : c->Subsystems)
		{
			// A fleet out of supply reloads slower: short of rounds, of spares for its mounts, of power to spare (U.SPC-T93).
			// A spinal mount recharges only as fast as the hull's spinal power plants feed it (U.SPC-T490).
			if (s->Def->Weapon != nullptr && !s->Destroyed()) s->Cooldown -= dt * c->Readiness * weaponPower * SpinalMounts::ChargeRate(c, s);
			// A rotary gun left idle winds down (US-T101).
			if (s->Spin > 0) Arsenal::SpinDown(s, s->Def->Weapon, dt);
		}
		// A braced crew is holding on, not working its guns: they load, but only point defence fires (U.SPC-T425).
		bool braced = !Broadsides::Fires(u);
		// The volleys are worked out afresh this step, now every gun is as loaded as it will be (U.SPC-T425).
		Broadsides::BeginVolleys(u);
		// The captain tunes the phaser arrays for the fight as it stands: a lance or a sweep (U.SPC-T488).
		EnergyWeapons::Step(shared_from_this(), u);
		// The Breacher bay fires a Breacher round once the habitats have the next team ready (U.SPC-T466).
		FireBreacherBay(u, dt);
		// A frigate holds every launcher for one volley, loosed together once its mark is in reach of them all (U.SPC-T475).
		bool alpha = AlphaStrike::Flies(u);
		bool release = alpha && AlphaStrike::Releases(u);
		bool loosed = false;
		for (const auto& s : c->Subsystems)
		{
			auto w = EnergyWeapons::Effective(s, c);
			if (w == nullptr || s->Destroyed()) continue;
			// A mount the player has taken is theirs to lay and fire, reloading or not (U.SPC-T18).
			cs::Ref<MountControl> manned;
			if (Manned.Count() > 0 && Manned.TryGetValue(s, manned))
			{
				FireManned(u, s, manned, w, dt, accuracyScale);
				continue;
			}
			if (braced && !w->AntiSmall) continue;
			if (s->Cooldown > 0 || s->Kind() == SubsystemKind::Torpedo) continue;
			if (s->Kind() == SubsystemKind::BoardingPods)
			{
				FirePods(u, s);
				continue;
			}
			if (w->Deploy != DeployKind::None)
			{
				TryDeploy(u, s, w);
				continue;
			}
			auto origin = u->PointOf(s->Def);

			if (w->AntiSmall && ScreenWeapons::IsScreenMount(s->Kind()))
			{
				// The screen (U.SPC-T489): flak bursts on, or an interceptor goes after, the worst round in reach.
				auto threat = ScreenTarget(u, s, origin, w);
				if (threat != nullptr)
				{
					FireScreen(u, s, origin, w, threat, nullptr, accuracyScale);
					continue;
				}
			}
			else if (w->AntiSmall)
			{
				auto missile = NearestMissile(u->Side, origin, w->RangeKm);
				if (missile != nullptr)
				{
					s->Aim = (missile->Pos - origin).Normalized();
					s->Cooldown = w->Cooldown * (0.85 + rng.NextDouble() * 0.3);
					if (rng.NextDouble() < w->Accuracy * accuracyScale)
					{
						missile->Hp -= w->Damage;
						if (missile->Hp <= 0)
						{
							missile->Gone = true;
							RoundLost(missile);
							Emit(FxKind::MissileKill, missile->Pos, missile->Pos, 0.08, missile->Side, nullptr, -1);
						}
					}
					Emit(EnergyWeapons::FxOf(s->Kind(), true).value_or(FxKind::PointDefense), origin, missile->Pos, 0, u->Side, u, s->Index);
					continue;
				}
			}

			cs::Ref<BattleUnit> target;
			bool helm = u->PlayerControlled() && u->PlayerHelm;
			if (helm && HelmVolley(u, s, origin, w, accuracyScale)) continue;
			if (u->PlayerControlled() && !helm && IsPlayerAimable(u, s->Def))
			{
				// The player's own nose guns: no AI target selection, no autofire. Side/aft
				// batteries (a broadside hull's real firepower) fall through to the branch below
				// completely unchanged, so piloting never silences a ship's crewed turrets.
				if (w->EmpSeconds > 0) target = u->PlayerEmpHeld ? PlayerEmpTarget(u, origin, w) : nullptr;
				else if (!w->Missile)
				{
					// Down the crosshair, hitting only what the shot really meets (U.SPC-T270).
					if (u->PlayerFireHeld && !u->Overheated) FirePlayerGun(u, s, origin, w);
					else s->Aim = DVec3::Zero;
					continue;
				}
				else if (Arsenal::IsRocket(w))
				{
					// Dumb-fire rockets (US-T101): no lock, the salvo goes straight down the nose on the second trigger.
					if (u->PlayerSecondaryHeld && s->Ammo > 0 && SecondaryFires(u, s))
					{
						s->Cooldown = w->Cooldown;
						FireRockets(u, s, origin, u->Forward, w);
					}
					else s->Aim = DVec3::Zero;
					continue;
				}
				// Homing and hive missiles go once the lock is made, those of the group picked at a carrier craft's stick (US-T101).
				else target = u->PlayerSecondaryHeld && u->LockProgress >= 1 && SecondaryFires(u, s) ? PlayerLockedTarget(u, w, origin) : nullptr;
			}
			else
			{
				target = ChooseWeaponTarget(u, origin, w);
				// At a capital ship's helm the crews go for the hull the player has locked, wherever it is in their fire cones and reach.
				if (helm)
					if (auto locked = LockedFor(u, s, origin, w)) target = locked;
				// Hold fire on a prize our own people are boarding or closing on.
				if (target != nullptr && IsPrize(u, target)) target = nullptr;
			}
			// Every mount aims within its fire cone (U.SPC-T533): a turret's wide one round its mount's normal, a spinal
			// mount's or fixed tube's narrow one along the keel. It engages only a target that lies inside it, in 3D.
			// A crewed small craft's guns bear on a capital anywhere along its keel, so a strafer raking the hull's far end
			// fires at the hull rather than holding for its centre (US-T164, Choreography::BearingPoint).
			if (target != nullptr && !u->Bears(s->Def, u->PlayerControlled() ? target->Pos : Choreography::BearingPoint(u, target))) target = nullptr;
			// A capital ship's mounts fire under their own orders (U.SPC-T425, given to the guns that bear where the captain
			// looks): held, not at all; in a volley, a gun holds until every gun of the volley bearing on the same hull is
			// loaded, and they go together, surer and harder.
			double volley = 1;
			if (target != nullptr && u->Broadside != nullptr && Broadsides::IsOrdered(s))
			{
				auto order = u->Broadside->OrderOf(s);
				if (order == BatteryOrder::HoldFire) target = nullptr;
				else if (order == BatteryOrder::Volley && Broadsides::IsBatteryGun(s))
				{
					if (!Broadsides::Releases(u, target))
					{
						Broadsides::Holding(u, target);
						s->Aim = (target->Pos - origin).Normalized();
						continue;
					}
					volley = 1 + Broadsides::CrewVolleyDamageBonus;
				}
			}
			if (target == nullptr)
			{
				s->Aim = DVec3::Zero;
				continue;
			}
			if (alpha && AlphaStrike::Holds(s, release))
			{
				s->Aim = (target->Pos - origin).Normalized();
				continue;
			}
			// No round in reach: the screen turns on fighters (U.SPC-T489); a flak cannon with only a hull near fires on it as
			// point defence does.
			if (ScreenWeapons::IsScreenMount(s->Kind()) && WeakPoints::IsSmallCraft(target))
			{
				FireScreen(u, s, origin, w, nullptr, target, accuracyScale);
				continue;
			}
			// A player's guided rounds go for the weak point picked on the locked hull (U.SPC-T424).
			auto sub = u->PlayerControlled() && w->Missile && target == u->PlayerLockTarget && u->PlayerSubTarget != nullptr && !u->PlayerSubTarget->Destroyed()
						&& target->Combat->Subsystems.Contains(u->PlayerSubTarget)
				? u->PlayerSubTarget
				: PickSubsystem(target, u);
			auto aimPoint = sub != nullptr ? target->PointOf(sub->Def) : target->Pos;
			s->Aim = (aimPoint - origin).Normalized();
			s->Cooldown = w->Cooldown * (0.85 + rng.NextDouble() * 0.3);
			bool small = WeakPoints::IsSmallCraft(u);
			// A rotary gun's crew fires as fast as its barrels have spun up (US-T101), and in a coarse step the burst the
			// step stands for.
			double burst = 1;
			if (Arsenal::IsRotary(w))
			{
				burst = Arsenal::RoundsPerShot(s, w, dt);
				s->Cooldown = Arsenal::FireRotary(s, w, s->Cooldown / w->Cooldown);
			}

			// Dumb-fire rockets (US-T101): the salvo goes down the lead line onto the target, unguided.
			if (Arsenal::IsRocket(w))
			{
				if (s->Ammo <= 0) continue;
				auto lead = Gunnery::LeadPoint(origin, u->Vel, aimPoint, target->Vel, w->MissileSpeedKmS);
				auto line = lead - origin;
				FireRockets(u, s, origin, line.LengthSquared() > 1e-12 ? line.Normalized() : u->Forward, w);
				continue;
			}

			if (w->Missile)
			{
				if (s->Ammo <= 0) continue;
				// A capital torpedo tube's round flies as a torpedo: tougher for point defence to stop (U.SPC-T377); an
				// antimatter torpedo is tougher still and hits hardest of all (U.SPC-T489).
				auto kindOf = ScreenWeapons::KindOf(s->Kind(), w);
				bool torpedo = kindOf == MissileKind::Torpedo || kindOf == MissileKind::Antimatter;
				if (alpha && AlphaStrike::IsStrikeMount(s)) loosed = true;
				for (int k = 0; k < w->Salvo && s->Ammo > 0; k++)
				{
					s->Ammo--;
					// C# evaluates the three draws left to right; C++ argument order is unspecified, so sequence them.
					double rx = rng.NextDouble() - 0.5;
					double ry = rng.NextDouble() - 0.5;
					double rz = rng.NextDouble() - 0.5;
					auto spread = DVec3(rx, ry, rz) * 0.4;
					auto m = cs::New<Missile>();
					m->Pos = origin;
					m->Vel = (u->Forward + spread + s->Aim).Normalized() * (w->MissileSpeedKmS * 0.3) + u->Vel;
					m->Target = target;
					m->TargetSub = sub;
					m->Damage = ScreenWeapons::Warhead(s->Kind(), w);
					m->Speed = w->MissileSpeedKmS;
					m->Hp = ScreenWeapons::RoundHp(s->Kind());
					m->Life = 90;
					m->Side = u->Side;
					m->Torpedo = torpedo;
					m->Kind = kindOf;
					m->EmpSeconds = w->EmpSeconds;
					m->EmpRadiusKm = w->EmpRadiusKm;
					m->SmallCraft = small;
					// A hive round flies whole a moment, then splits into its swarm (US-T101, StepMissiles).
					if (Arsenal::IsHive(w))
					{
						m->SplitIn = w->SplitAfterS;
						m->SplitInto = w->SplitInto;
					}
					Missiles.Add(m);
				}
				Emit(FxKind::MissileLaunch, origin, origin + s->Aim, 0, u->Side, u, s->Index);
				continue;
			}

			if (target->PlayerControlled())
			{
				// At the player's own hull every shot is aimed and real (U.SPC-T270): led, late on a target
				// crossing the gunner's sights and scattered by his skill, it hits only if it meets the hull.
				auto dir = AimAt(u, origin, target, aimPoint, w, accuracyScale * (volley > 1 ? 1 + Broadsides::CrewVolleyAccuracyBonus : 1));
				s->Aim = dir;
				ResolveShot(u, s, origin, dir, w, volley * burst);
				continue;
			}

			double dist = (aimPoint - origin).Length();
			double size = cs::Math::Max(0.15, cs::Math::Min(1.0, target->LengthKm / 0.4));
			if (target->Kind == UnitKind::Fighter) size *= w->AntiSmall ? 1.4 : 0.35;
			// A tachyon lance's shot lands the instant it is fired, so it is as sure at the end of its reach as close in (U.SPC-T487).
			double falloff = w->Lance ? 1 : 1 - 0.4 * dist / w->RangeKm * (1 - picture);
			double chance = w->Accuracy * accuracyScale * size * falloff * (volley > 1 ? 1 + Broadsides::CrewVolleyAccuracyBonus : 1);
			// US-T180: a small craft crossing the gunner's line fast is hard to track; one sat on its tail is not.
			chance *= Choreography::Evasion(u, target, origin);
			bool hit = rng.NextDouble() < chance;
			auto energy = EnergyWeapons::FxOf(s->Kind(), hit);
			auto kind = energy.has_value() ? *energy
				: Arsenal::IsRotary(w) ? FxKind::Vulcan
				: s->Kind() == SubsystemKind::FighterGuns || s->Kind() == SubsystemKind::Autocannon || s->Kind() == SubsystemKind::AutoTurret ? FxKind::Guns
				: w->AntiSmall ? FxKind::PointDefense
				: w->Lance ? (hit ? FxKind::TachyonLance : FxKind::TachyonLanceMiss)
				: SpinalMounts::IsSlug(s->Kind()) ? (hit ? FxKind::Railgun : FxKind::RailgunMiss)
				: hit ? FxKind::Beam : FxKind::BeamMiss;

			if (w->MuzzleSpeedKmS > 0)
			{
				// A dumb kinetic round, not a hitscan: it takes real time to cross the range, so
				// fire it off and let FireKineticRound/StepRounds decide what it actually hits.
				FireKineticRound(u, s, origin, aimPoint, target, sub, w, hit, dist, kind, volley * burst);
				continue;
			}

			if (hit)
			{
				// A plasma bolt bleeds off over the distance it crossed (U.SPC-T488); every other beam hits as hard at any range.
				double landed = w->DamageAt(dist) * volley;
				Emit(kind, origin, aimPoint, landed, u->Side, target, sub ? sub->Index : -1);
				ApplyDamage(target, sub, landed, aimPoint, std::nullopt, small ? WeakPoints::Factor(target, sub, aimPoint - origin) : 1, aimPoint - origin,
					false, WeaponSystems::FamilyOf(s->Kind()));
			}
			else
			{
				double rx = rng.NextDouble() - 0.5;
				double ry = rng.NextDouble() - 0.5;
				double rz = rng.NextDouble() - 0.5;
				auto miss = aimPoint + DVec3(rx, ry, rz) * (target->LengthKm * 2 + 0.3);
				Emit(kind, origin, origin + (miss - origin).Normalized() * cs::Math::Min(w->RangeKm, dist * 1.3), 0, u->Side, nullptr, -1);
			}
		}
		// The volley is away: flip and burn back out (U.SPC-T475).
		if (loosed) AlphaStrike::Loosed(u);
	}

	void Battle::FireKineticRound(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& mount, DVec3 origin, DVec3 aimPoint, const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& targetSub, const cs::Ref<WeaponSpec>& w, bool hit, double dist, FxKind kind, double damageScale)
	{
		double speed = w->MuzzleSpeedKmS;
		double leadTime = cs::Math::Min(dist / speed, 20.0);
		auto lead = aimPoint + target->Vel * leadTime;
		DVec3 impact;
		if (hit) impact = lead;
		else
		{
			double rx = rng.NextDouble() - 0.5;
			double ry = rng.NextDouble() - 0.5;
			double rz = rng.NextDouble() - 0.5;
			impact = lead + DVec3(rx, ry, rz) * (target->LengthKm * 2 + 0.3);
		}
		auto dir = impact - origin;
		double travelKm = dir.Length();
		auto vel = travelKm > 1e-9 ? dir / travelKm * speed : u->Forward * speed;
		double arriveIn = travelKm / speed;

		Emit(kind, origin, hit ? impact : origin + (impact - origin).Normalized() * cs::Math::Min(w->RangeKm, dist * 1.3), 0, u->Side, nullptr, -1);
		auto r = cs::New<KineticRound>();
		r->Pos = origin;
		r->Vel = vel;
		r->Target = target;
		r->TargetSub = targetSub;
		r->Damage = w->Damage * damageScale;
		r->ArriveIn = arriveIn;
		r->Life = arriveIn + (hit ? 0 : 0.5);
		r->Side = u->Side;
		r->Hit = hit;
		r->Railgun = SpinalMounts::IsSlug(mount->Kind());
		r->Tracer = Arsenal::IsRotary(w);
		r->SmallCraft = WeakPoints::IsSmallCraft(u);
		Rounds.Add(r);
	}

	void Battle::StepRounds(double dt)
	{
		for (const auto& r : Rounds)
		{
			if (r->Gone) continue;
			if (r->Aimed)
			{
				StepAimedRound(r, dt);
				continue;
			}
			r->Pos = r->Pos + r->Vel * dt;
			r->ArriveIn -= dt;
			r->Life -= dt;
			if (r->Hit && r->ArriveIn <= 0)
			{
				r->Gone = true;
				// Not into a hull that has since come over to the firer's own side (taken as a prize),
				// nor one that struck its colours while the round was in flight (the same race StepMissiles
				// spares a target for with its own "spared" check).
				if (r->Target->Present() && r->Target->Side != r->Side && !r->Target->Surrendering)
				{
					auto sub = r->TargetSub != nullptr && !r->TargetSub->Destroyed() ? r->TargetSub : nullptr;
					ApplyDamage(r->Target, sub, r->Damage, r->Pos, std::nullopt, r->SmallCraft ? WeakPoints::Factor(r->Target, sub, r->Vel - r->Target->Vel) : 1, r->Vel - r->Target->Vel,
						false, WeaponSystems::FamilyOfRound(*r));
				}
			}
			else if (r->Life <= 0) r->Gone = true;
		}
		Rounds.RemoveAll([](const cs::Ref<KineticRound>& r) { return r->Gone; });
	}

	// Point defence is never the player's to aim: it keeps engaging missiles and small craft on its own.
	// Nor is an auto turret (U.SPC-T278): its own fire control works it, whoever flies the hull.
	// A forward mount is one whose fire cone takes in dead ahead (U.SPC-T533).
	bool Battle::IsPlayerAimable(const cs::Ref<BattleUnit>& u, const cs::Ref<SubsystemDef>& d)
	{
		return !(d->Weapon != nullptr && d->Weapon->AntiSmall) && d->Kind != SubsystemKind::AutoTurret && d->Covers(u->Combat ? u->Combat->Spec : nullptr, FireCones::Bow);
	}

	void Battle::FirePlayerGun(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, const cs::Ref<WeaponSpec>& w)
	{
		auto aim = u->PlayerAim.LengthSquared() > 1e-12 ? u->PlayerAim.Normalized() : u->Forward;
		auto sight = Gunnery::Gimbal(aim, u->Forward, SpinalMounts::IsSlug(s->Kind()) || w->Lance || s->Def->Spinal ? Gunnery::RailgunGimbalDeg : Gunnery::GimbalDeg);
		cs::Ref<BattleUnit> unusedHit;
		double converge = FirstHostileAlong(u->Side, u->Pos, sight, w->RangeKm, unusedHit);
		if (converge < 0) converge = w->RangeKm;
		auto toConverge = u->Pos + sight * converge - origin;
		auto dir = toConverge.LengthSquared() > 1e-12 ? toConverge.Normalized() : sight;
		double scatter = w->MuzzleSpeedKmS > 0 && !SpinalMounts::IsSlug(s->Kind()) ? Gunnery::AutocannonScatterDeg : Gunnery::BeamScatterDeg;
		// A rotary gun hoses its rounds wider (US-T101).
		if (Arsenal::IsRotary(w)) scatter *= Arsenal::RotaryScatter;
		dir = Gunnery::Scatter(dir, scatter * Astro::Deg2Rad, rng);
		s->Aim = dir;
		// The player's trigger keeps the gun's own rhythm, not a crew's jittered one; a rotary gun's as its barrels have spun up.
		s->Cooldown = Arsenal::IsRotary(w) ? Arsenal::FireRotary(s, w) : w->Cooldown;
		// A tachyon lance is fed by the reactor and held back by its recharge, not by the guns' heat (U.SPC-T487).
		// Nor is a spinal mount, which its own power plant feeds (U.SPC-T490).
		if (!w->Lance && !s->Def->Spinal) u->Heat += w->Damage * Gunnery::HeatPerDamage;
		if (u->Heat >= 1)
		{
			u->Heat = 1;
			u->Overheated = true;
		}
		ResolveShot(u, s, origin, dir, w);
	}

	void Battle::FireManned(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, const cs::Ref<MountControl>& m, const cs::Ref<WeaponSpec>& w, double dt, double accuracyScale)
	{
		auto current = s->Aim.LengthSquared() > 1e-12 ? s->Aim.Normalized() : MountedTurret::RestAim(u, s->Def);
		auto want = m->Aim.LengthSquared() > 1e-12 ? MountedTurret::Bound(u, s->Def, m->Aim.Normalized()) : current;
		s->Aim = PilotFlight::ClampAim(want, current, MountedTurret::TraverseDegS(s->Kind()) * Astro::Deg2Rad * dt);
		if (!m->Trigger || s->Cooldown > 0) return;
		// A manned rotary gun fires as fast as its barrels have spun up (US-T101).
		s->Cooldown = Arsenal::IsRotary(w) ? Arsenal::FireRotary(s, w) : w->Cooldown;
		bool slug = SpinalMounts::IsSlug(s->Kind());
		double scatter = (w->MuzzleSpeedKmS > 0 && !slug ? Gunnery::AutocannonScatterDeg : Gunnery::BeamScatterDeg) / cs::Math::Max(accuracyScale, 0.1);
		if (Arsenal::IsRotary(w)) scatter *= Arsenal::RotaryScatter;
		auto origin = u->PointOf(s->Def);
		auto dir = Gunnery::Scatter(s->Aim, scatter * Astro::Deg2Rad, rng);
		ResolveShot(u, s, origin, dir, w);
	}

	DVec3 Battle::AimAt(const cs::Ref<BattleUnit>& u, DVec3 origin, const cs::Ref<BattleUnit>& target, DVec3 aimPoint, const cs::Ref<WeaponSpec>& w, double accuracyScale)
	{
		double skill = cs::Math::Max(0.0, cs::Math::Min(1.0, w->Accuracy * accuracyScale));
		// Where he sees it: carried on from a moment ago at the velocity he has read it as having, so a hull that
		// has just changed course is not where he thinks.
		// A coordinated gunner reads the course off the shared picture, and lags it less (U.SPC-T477).
		auto seen = aimPoint - (target->Vel - target->SeenVel) * (Gunnery::TrackingLagS * (1 - 0.5 * skill) * (1 - FleetCoordination::SharedPicture(u)));
		auto point = w->MuzzleSpeedKmS > 0 ? Gunnery::LeadPoint(origin, u->Vel, seen, target->SeenVel, w->MuzzleSpeedKmS) : seen;
		auto to = point - origin;
		auto dir = to.LengthSquared() > 1e-12 ? to.Normalized() : u->Forward;
		return Gunnery::Scatter(dir, Gunnery::MaxScatterDeg * (1 - skill) * Astro::Deg2Rad, rng);
	}

	void Battle::ResolveShot(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, DVec3 dir, const cs::Ref<WeaponSpec>& w, double damageScale)
	{
		bool beam = w->MuzzleSpeedKmS <= 0;
		double damage = w->Damage * damageScale;
		if (!beam)
		{
			bool slug = SpinalMounts::IsSlug(s->Kind());
			bool rotary = Arsenal::IsRotary(w);
			Emit(slug ? FxKind::Railgun : rotary ? FxKind::Vulcan : FxKind::Guns, origin, origin + dir * w->RangeKm, 0, u->Side, nullptr, -1);
			auto r = cs::New<KineticRound>();
			r->Pos = origin;
			r->Vel = u->Vel + dir * w->MuzzleSpeedKmS;
			r->Damage = damage;
			r->Life = w->RangeKm / w->MuzzleSpeedKmS;
			r->Side = u->Side;
			r->Railgun = slug;
			r->Tracer = rotary;
			r->Aimed = true;
			r->SmallCraft = WeakPoints::IsSmallCraft(u);
			Rounds.Add(r);
			return;
		}
		cs::Ref<BattleUnit> hit;
		double t = FirstHostileAlong(u->Side, origin, dir, w->RangeKm, hit);
		if (hit == nullptr)
		{
			Emit(EnergyWeapons::FxOf(s->Kind(), false).value_or(w->AntiSmall ? FxKind::PointDefense : w->Lance ? FxKind::TachyonLanceMiss : FxKind::BeamMiss), origin, origin + dir * w->RangeKm, 0, u->Side, nullptr, -1);
			return;
		}
		auto at = origin + dir * t;
		auto sub = SubsystemNear(hit, at);
		// A plasma bolt bleeds off over the distance it crossed (U.SPC-T488).
		if (w->FalloffEdgeShare < 1 && w->Damage > 0) damage *= w->DamageAt(t) / w->Damage;
		Emit(EnergyWeapons::FxOf(s->Kind(), true).value_or(w->AntiSmall ? FxKind::PointDefense : w->Lance ? FxKind::TachyonLance : FxKind::Beam), origin, at, damage, u->Side, hit, sub ? sub->Index : -1);
		ApplyDamage(hit, sub, damage, at, std::nullopt, WeakPoints::IsSmallCraft(u) ? WeakPoints::Factor(hit, sub, dir) : 1, dir, false, WeaponSystems::FamilyOf(s->Kind()));
	}

	void Battle::StepAimedRound(const cs::Ref<KineticRound>& r, double dt)
	{
		auto step = r->Vel * dt;
		double len = step.Length();
		r->Life -= dt;
		if (len > 1e-12)
		{
			cs::Ref<BattleUnit> hit;
			double t = FirstHostileAlong(r->Side, r->Pos, step / len, len, hit);
			if (hit != nullptr)
			{
				auto at = r->Pos + step / len * t;
				r->Gone = true;
				auto sub = SubsystemNear(hit, at);
				ApplyDamage(hit, sub, r->Damage, at, std::nullopt, r->SmallCraft ? WeakPoints::Factor(hit, sub, r->Vel - hit->Vel) : 1, r->Vel - hit->Vel,
					false, WeaponSystems::FamilyOfRound(*r));
				return;
			}
		}
		r->Pos = r->Pos + step;
		if (r->Life <= 0) r->Gone = true;
	}

	double Battle::FirstHostileAlong(int side, DVec3 origin, DVec3 dir, double maxKm, cs::Ref<BattleUnit>& hit)
	{
		hit = nullptr;
		double best = -1;
		for (const auto& o : Units)
		{
			if (o->Side == side || !o->Present() || o->Surrendering) continue;
			double t = Gunnery::RaySphere(origin, dir, maxKm, o->Pos, Gunnery::HitRadius(o));
			if (t >= 0 && (best < 0 || t < best))
			{
				best = t;
				hit = o;
			}
		}
		return best;
	}

	cs::Ref<Subsystem> Battle::SubsystemNear(const cs::Ref<BattleUnit>& target, DVec3 at)
	{
		if (target->Kind == UnitKind::Fighter) return nullptr;
		cs::Ref<Subsystem> best = nullptr;
		double bestD = target->LengthKm / 6;
		for (const auto& s : target->Combat->Subsystems)
		{
			if (s->Destroyed() || s->Def->Buried) continue;
			double d = (target->PointOf(s->Def) - at).Length();
			if (d < bestD)
			{
				bestD = d;
				best = s;
			}
		}
		return best;
	}

	// ---------------------------------------------------------------- broadsides at the helm (U.SPC-T272)

	bool Battle::HelmVolley(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, const cs::Ref<WeaponSpec>& w, double accuracyScale)
	{
		if (w->Missile || w->AntiSmall || w->Supporting() || u->PlayerVolleyAim.LengthSquared() < 1e-12) return false;
		auto look = u->PlayerVolleyAim.Normalized();
		if (!u->BearsAlong(s->Def, look)) return false;
		if (u->PlayerVolleyRelease)
		{
			auto target = VolleyTarget(u, look, w->RangeKm);
			if (target == nullptr) return false;
			double built = cs::Math::Min(1.0, u->VolleyCharge / VolleyBuildSeconds);
			auto sub = PickSubsystem(target);
			auto aimPoint = sub != nullptr ? target->PointOf(sub->Def) : target->Pos;
			auto dir = AimAt(u, origin, target, aimPoint, w, accuracyScale * (1 + VolleyAccuracyBonus * built));
			s->Aim = dir;
			s->Cooldown = w->Cooldown;
			ResolveShot(u, s, origin, dir, w, 1 + VolleyDamageBonus * built);
			return true;
		}
		if (!u->PlayerFireHeld) return false;
		s->Aim = look;
		return true;
	}

	cs::Ref<BattleUnit> Battle::VolleyTarget(const cs::Ref<BattleUnit>& u, DVec3 look, double rangeKm)
	{
		double cone = cs::Math::Cos(VolleyConeDeg * Astro::Deg2Rad);
		cs::Ref<BattleUnit> best = nullptr;
		double bestCos = cone;
		for (const auto& o : Units)
		{
			if (!Hostile(u, o) || o->Surrendering || o->Captured) continue;
			auto to = o->Pos - u->Pos;
			double d = to.Length();
			if (d < 1e-6 || d - o->Radius() > rangeKm) continue;
			double cos = DVec3::Dot(look, to / d);
			if (o == u->PlayerLockTarget && cos > cone) return o;
			if (cos > bestCos)
			{
				bestCos = cos;
				best = o;
			}
		}
		return best;
	}

	cs::Ref<BattleUnit> Battle::LockedFor(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, const cs::Ref<WeaponSpec>& w)
	{
		auto t = u->PlayerLockTarget;
		if (t == nullptr || !Hostile(u, t) || t->Surrendering || t->Captured) return nullptr;
		if ((t->Pos - origin).Length() - t->Radius() > w->RangeKm) return nullptr;
		if (w->AntiSmall && t->Kind != UnitKind::Fighter) return nullptr;
		return u->Bears(s->Def, t->Pos) ? t : nullptr;
	}

	// ---------------------------------------------------------------- the dogfight arsenal (U.SPC-T271)

	void Battle::TryDeploy(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w)
	{
		if (w->Deploy == DeployKind::Decoy)
		{
			ThrowDecoy(u, s, w);
			return;
		}
		bool want = u->PlayerControlled() ? u->PlayerMineRequested : SomethingOnItsTail(u);
		if (!want || s->Ammo <= 0 || u->Stunned > 0) return;
		s->Ammo--;
		s->Cooldown = w->Cooldown;
		auto at = u->Pos - u->Forward * (u->Radius() + 0.04);
		auto mine = cs::New<Mine>();
		mine->Pos = at;
		mine->Side = u->Side;
		mine->Damage = w->Damage;
		mine->BlastKm = w->RangeKm;
		mine->ArmIn = MineArmSeconds;
		mine->Life = MineLifeSeconds;
		Mines.Add(mine);
		Emit(FxKind::Deployed, at, at, 0.02, u->Side, u, s->Index);
	}

	bool Battle::SomethingOnItsTail(const cs::Ref<BattleUnit>& u)
	{
		for (const auto& o : Units)
		{
			if (!Hostile(u, o) || o->Captured || o->Surrendering || !o->Combat->Armed()) continue;
			auto to = o->Pos - u->Pos;
			double d = to.Length();
			if (d < 2.5 && d > 1e-6 && DVec3::Dot(u->Forward, to / d) < -0.3) return true;
		}
		return false;
	}

	// ---------------------------------------------------------------- countermeasures (U.SPC-T424)

	void Battle::ThrowDecoy(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w)
	{
		bool want = u->PlayerControlled() ? u->PlayerDecoyRequested : MissileInbound(u, Countermeasures::AiReactKm) != nullptr;
		if (!want || s->Ammo <= 0 || u->Stunned > 0) return;
		s->Ammo--;
		s->Cooldown = w->Cooldown;
		auto at = u->Pos - u->Forward * (u->Radius() + 0.02);
		auto decoy = cs::New<Decoy>();
		decoy->Pos = at;
		decoy->Vel = u->Vel * 0.6 - u->Forward * 0.05;
		decoy->Side = u->Side;
		decoy->Life = Countermeasures::LifeSeconds;
		Decoys.Add(decoy);
		Emit(FxKind::Deployed, at, at, 0.03, u->Side, u, s->Index);
		for (const auto& m : Missiles)
		{
			if (m->Gone || m->Target != u || m->Decoy != nullptr || (m->Pos - u->Pos).Length() > Countermeasures::ReachKm) continue;
			// A bomber's torpedo riding a recon fighter's paint is seldom fooled (U.SPC-T485).
			if (rng.NextDouble() < ElectronicWarfare::SeduceChance(shared_from_this(), m)) m->Decoy = decoy;
		}
	}

	cs::Ref<Missile> Battle::MissileInbound(const cs::Ref<BattleUnit>& u, double km)
	{
		cs::Ref<Missile> best = nullptr;
		double bestD = km;
		for (const auto& m : Missiles)
		{
			if (m->Gone || m->Target != u || m->Decoy != nullptr || m->Kind == MissileKind::Pod || m->Kind == MissileKind::Breacher) continue;
			double d = (m->Pos - u->Pos).Length();
			if (d <= bestD)
			{
				bestD = d;
				best = m;
			}
		}
		return best;
	}

	void Battle::StepDecoys(double dt)
	{
		for (const auto& d : Decoys)
		{
			d->Pos = d->Pos + d->Vel * dt;
			d->Life -= dt;
			if (d->Life <= 0) d->Gone = true;
		}
		Decoys.RemoveAll([](const cs::Ref<Decoy>& d) { return d->Gone; });
	}

	cs::Ref<BattleUnit> Battle::PlayerEmpTarget(const cs::Ref<BattleUnit>& u, DVec3 origin, const cs::Ref<WeaponSpec>& w)
	{
		auto locked = PlayerLockedTarget(u, w, origin);
		if (locked != nullptr) return locked;
		auto aim = u->PlayerAim.LengthSquared() > 1e-12 ? u->PlayerAim.Normalized() : u->Forward;
		cs::Ref<BattleUnit> best = nullptr;
		double bestCos = cs::Math::Cos(25 * Astro::Deg2Rad);
		for (const auto& o : Units)
		{
			if (!Hostile(u, o) || o->Stunned > 0 || o->Surrendering || o->Captured) continue;
			auto to = o->Pos - origin;
			double d = to.Length();
			if (d < 1e-6 || d - o->Radius() > w->RangeKm) continue;
			double cos = DVec3::Dot(aim, to / d);
			if (cos > bestCos)
			{
				bestCos = cos;
				best = o;
			}
		}
		return best;
	}

	double Battle::EmpStunSeconds(double seconds, double lengthKm) { return seconds * cs::Math::Min(1.0, 0.2 / cs::Math::Max(lengthKm, 1e-6)); }

	void Battle::EmpBurst(DVec3 at, int side, double seconds, double radiusKm)
	{
		Emit(FxKind::EmpBurst, at, at, radiusKm, side, nullptr, -1);
		for (const auto& o : Units)
		{
			if (o->Side == side || !o->Present() || o->Surrendering) continue;
			if ((o->Pos - at).Length() > radiusKm + Gunnery::HitRadius(o)) continue;
			o->Stunned = cs::Math::Max(o->Stunned, EmpStunSeconds(seconds, o->LengthKm));
			for (const auto& g : o->Combat->Subsystems)
				if (g->Kind() == SubsystemKind::ShieldGenerator) g->Shield *= 0.5;
		}
	}

	void Battle::StepMines(double dt)
	{
		for (const auto& m : Mines)
		{
			if (m->Gone) continue;
			m->Pos = m->Pos + m->Vel * dt;
			m->ArmIn -= dt;
			m->Life -= dt;
			if (m->Life <= 0)
			{
				m->Gone = true;
				continue;
			}
			if (!m->Armed()) continue;
			bool tripped = false;
			for (const auto& o : Units)
				if (o->Side != m->Side && o->Present() && (o->Pos - m->Pos).Length() <= MineTriggerKm + Gunnery::HitRadius(o))
				{
					tripped = true;
					break;
				}
			if (!tripped) continue;
			m->Gone = true;
			Emit(FxKind::MineBlast, m->Pos, m->Pos, m->BlastKm, m->Side, nullptr, -1);
			for (int i = 0; i < Units.Count(); i++)
			{
				auto o = Units[i];
				if (o->Side == m->Side || !o->Present() || o->Surrendering) continue;
				double d = cs::Math::Max(0.0, (o->Pos - m->Pos).Length() - Gunnery::HitRadius(o));
				if (d > m->BlastKm) continue;
				ApplyDamage(o, nullptr, m->Damage * (1 - d / m->BlastKm), m->Pos);
			}
		}
		Mines.RemoveAll([](const cs::Ref<Mine>& m) { return m->Gone; });
	}

	void Battle::StepHeat(const cs::Ref<BattleUnit>& u, double dt)
	{
		auto c = u->Combat;
		bool working = !u->Captured && !u->Surrendering && u->Stunned <= 0;
		double reload = working ? c->Readiness * PowerFactor(u, PowerSystem::Weapons) : 0;
		burnedOut.Clear();
		Radiators::Step(u, dt, reload, &burnedOut);
		for (const auto& s : burnedOut)
		{
			auto at = u->PointOf(s->Def);
			Emit(FxKind::SubsystemDestroyed, at, at, u->LengthKm * 0.12, u->Side, u, s->Index);
		}
	}

	double Battle::PowerFactor(const cs::Ref<BattleUnit>& u, PowerSystem s) const
	{
		// A small craft shares its own reactor, whoever flies it (U.SPC-T424).
		if (u->Power != nullptr) return u->Power->Factor(s);
		// A hull the player has powered down (US-T93) has nothing to give its guns or drive until its restart is done; its
		// shields' factor stays the rate the unheld field bleeds away at (ShieldCapacityOf holds it to nothing).
		if (SensorPosture::Offline(u->Combat)) return s == PowerSystem::Shields ? 1 : 0;
		// A hull's or station's, only as the player set it and while the player holds it (US-T94): a crew runs even.
		auto r = u->Combat->Reactor;
		return r != nullptr && PlayerAboard(u) ? r->Factor(s) : 1;
	}

	double Battle::ShieldCapacityOf(const cs::Ref<BattleUnit>& u) const
	{
		// Only the player's hands (US-T94): a pilot's own share moves the recharge alone, as it always has.
		if (u->Power != nullptr) return u->PlayerControlled() ? u->Power->ShieldCapacity() : 1;
		// Powered down (US-T93): the field has nothing holding it up and bleeds away.
		if (SensorPosture::Offline(u->Combat)) return 0;
		auto r = u->Combat->Reactor;
		return r != nullptr && PlayerAboard(u) ? r->ShieldCapacity() : 1;
	}

	bool Battle::PlayerAboard(const cs::Ref<BattleUnit>& u) const
	{
		if (u->PlayerControlled() || u->Combat->StationHeld) return true;
		if (Manned.Count() == 0) return false;
		for (const auto& s : u->Combat->Subsystems)
			if (Manned.ContainsKey(s)) return true;
		return false;
	}

	void Battle::StepPlayerWeapons(const cs::Ref<BattleUnit>& u, double dt)
	{
		// Power to the weapons cools the guns faster (U.SPC-T424).
		u->Heat = cs::Math::Max(0.0, u->Heat - Gunnery::HeatCoolPerS * PowerFactor(u, PowerSystem::Weapons) * dt);
		if (u->PlayerHelm && u->PlayerFireHeld) u->VolleyCharge = cs::Math::Min(VolleyBuildSeconds, u->VolleyCharge + dt);
		if (u->Overheated && u->Heat < Gunnery::HeatRecoverBelow) u->Overheated = false;

		auto t = u->PlayerLockTarget;
		if (t == nullptr || !t->Present() || !Hostile(u, t))
		{
			u->LockProgress = 0;
			return;
		}
		auto to = t->Pos - u->Pos;
		if (to.LengthSquared() < 1e-12) return;
		auto dir = to.Normalized();
		auto aim = u->PlayerAim.LengthSquared() > 1e-12 ? u->PlayerAim.Normalized() : u->Forward;
		if (u->LockProgress >= 1)
		{
			if (DVec3::Dot(u->Forward, dir) < cs::Math::Cos(Gunnery::LockHoldDeg * Astro::Deg2Rad)) u->LockProgress = 0;
			return;
		}
		bool onIt = DVec3::Dot(aim, dir) >= cs::Math::Cos(Gunnery::LockConeDeg * Astro::Deg2Rad);
		u->LockProgress = onIt ? cs::Math::Min(1.0, u->LockProgress + dt / Gunnery::LockSeconds) : cs::Math::Max(0.0, u->LockProgress - 2 * dt / Gunnery::LockSeconds);
	}

	cs::Ref<BattleUnit> Battle::PlayerLockedTarget(const cs::Ref<BattleUnit>& u, const cs::Ref<WeaponSpec>& w, DVec3 origin)
	{
		auto t = u->PlayerLockTarget;
		if (t == nullptr || !t->Present() || !Hostile(u, t) || t->Surrendering || t->Captured) return nullptr;
		return (t->Pos - origin).Length() - t->Radius() <= w->RangeKm ? t : nullptr;
	}

	cs::Ref<BattleUnit> Battle::ChooseWeaponTarget(const cs::Ref<BattleUnit>& u, DVec3 origin, const cs::Ref<WeaponSpec>& w)
	{
		if (w->AntiSmall)
		{
			// No missile in reach: point defence turns on fighters, then anything close.
			cs::Ref<BattleUnit> near = nullptr;
			double best = w->RangeKm;
			for (const auto& o : Units)
			{
				if (!Hostile(u, o) || o->Captured || o->Surrendering || HoldsFireOn(u, o)) continue;
				// An interceptor is wasted on a hull: it goes only after small craft (U.SPC-T489).
				if (w->Missile && !WeakPoints::IsSmallCraft(o)) continue;
				double d = (o->Pos - origin).Length() - o->Radius();
				if ((d < best && (near == nullptr || near->Kind != UnitKind::Fighter || o->Kind == UnitKind::Fighter)) || (o->Kind == UnitKind::Fighter && near != nullptr && near->Kind != UnitKind::Fighter && d < w->RangeKm))
				{
					best = d;
					near = o;
				}
			}
			return near;
		}
		auto t = u->Target;
		// Its own target only while that is still a fair one: not struck its colours, not since taken
		// by this side (a capture doesn't clear everyone's target the way a surrender does).
		bool emp = w->EmpSeconds > 0;
		// A dogfight missile's seeker takes a fighter as readily as a hull (U.SPC-T424).
		bool seeksSmall = emp || w->SeeksSmallCraft;
		if (t != nullptr && Hostile(u, t) && !t->Surrendering && !t->Captured && !HoldsFireOn(u, t) && (t->Pos - origin).Length() - t->Radius() <= w->RangeKm && !(t->Kind == UnitKind::Fighter && w->Missile && !seeksSmall) && !(emp && t->Stunned > 0))
			return t;
		cs::Ref<BattleUnit> any = nullptr;
		double nearest = w->RangeKm;
		for (const auto& o : Units)
		{
			// An EMP round is for a small craft too, but wasted on one already drifting dead (U.SPC-T271).
			// An auto turret's fire control takes fighters as readily as hulls (U.SPC-T278).
			bool takesFighters = seeksSmall || u->Kind == UnitKind::Fighter || w == WeaponSpec::AutoTurret();
			if (!Hostile(u, o) || o->Captured || o->Surrendering || HoldsFireOn(u, o) || (emp && o->Stunned > 0) || (o->Kind == UnitKind::Fighter && ((w->Missile && !seeksSmall) || !takesFighters))) continue;
			double d = (o->Pos - origin).Length() - o->Radius();
			if (d < nearest)
			{
				nearest = d;
				any = o;
			}
		}
		return any;
	}

	// ---------------------------------------------------------------- the screen (U.SPC-T489)

	cs::Ref<Missile> Battle::ScreenTarget(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& mount, DVec3 origin, const cs::Ref<WeaponSpec>& w)
	{
		bool interceptor = mount->Kind() == SubsystemKind::InterceptorMissile;
		if (interceptor && mount->Ammo <= 0) return nullptr;
		cs::Ref<Missile> best = nullptr;
		double bestThreat = 0;
		for (const auto& m : Missiles)
		{
			if (m->Side == u->Side || !ScreenWeapons::IsThreat(m)) continue;
			if ((m->Pos - origin).Length() > w->RangeKm) continue;
			if (interceptor && Hunted(m, u->Side)) continue;
			double threat = ScreenWeapons::Threat(m);
			if (threat > bestThreat)
			{
				bestThreat = threat;
				best = m;
			}
		}
		return best;
	}

	DVec3 Battle::ThreatTo(const cs::Ref<BattleUnit>& ward, const cs::Ref<BattleUnit>& u)
	{
		auto sum = DVec3::Zero;
		double weight = 0;
		for (const auto& m : Missiles)
		{
			if (m->Gone || m->Side == u->Side || m->Target != ward || !ScreenWeapons::IsThreat(m)) continue;
			double w = cs::Math::Max(m->Damage, 1.0);
			sum = sum + m->Pos * w;
			weight += w;
		}
		return weight > 0 ? sum / weight : HostileCentroid(u);
	}

	bool Battle::Hunted(const cs::Ref<Missile>& m, int side)
	{
		for (const auto& i : Missiles)
			if (!i->Gone && i->Kind == MissileKind::Interceptor && i->Side == side && i->Prey == m) return true;
		return false;
	}

	void Battle::FireScreen(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, const cs::Ref<WeaponSpec>& w, const cs::Ref<Missile>& round, const cs::Ref<BattleUnit>& craft, double accuracyScale)
	{
		auto at = round != nullptr ? round->Pos : craft->Pos;
		s->Aim = (at - origin).Normalized();
		s->Cooldown = w->Cooldown * (0.85 + rng.NextDouble() * 0.3);
		if (s->Kind() == SubsystemKind::InterceptorMissile)
		{
			if (s->Ammo <= 0) return;
			s->Ammo--;
			auto m = cs::New<Missile>();
			m->Pos = origin;
			m->Vel = s->Aim * (w->MissileSpeedKmS * 0.4) + u->Vel;
			m->Target = craft;
			m->Prey = round;
			m->Damage = w->Damage;
			m->Speed = w->MissileSpeedKmS;
			m->Hp = ScreenWeapons::InterceptorHp;
			m->Life = ScreenWeapons::InterceptorLifeSeconds;
			m->Side = u->Side;
			m->Kind = MissileKind::Interceptor;
			Missiles.Add(m);
			Emit(FxKind::MissileLaunch, origin, origin + s->Aim, 0, u->Side, u, s->Index);
			return;
		}
		double burst = cs::Math::Max(w->BurstKm, 0.05) * ScreenWeapons::FlakBurst()->Value;
		// US-T180: a shell fused for where a fast-crossing craft was bursts behind it more often than not.
		double evade = round == nullptr ? Choreography::Evasion(u, craft, origin) : 1.0;
		if (rng.NextDouble() >= w->Accuracy * accuracyScale * evade)
		{
			// Wide: the shell bursts a burst and a half or two off its mark.
			double ox = rng.NextDouble() - 0.5;
			double oy = rng.NextDouble() - 0.5;
			double oz = rng.NextDouble() - 0.5;
			auto off = DVec3(ox, oy, oz);
			auto offDir = off.LengthSquared() > 1e-9 ? off.Normalized() : DVec3::UnitX;
			at = at + offDir * burst * (1.5 + rng.NextDouble() * 0.5);
		}
		FlakBurstAt(at, u->Side, w->Damage * ScreenWeapons::FlakDamage()->Value, burst);
		Emit(FxKind::FlakBurst, origin, at, burst, u->Side, u, s->Index);
	}

	void Battle::FlakBurstAt(DVec3 at, int side, double damage, double radiusKm)
	{
		if (damage <= 0 || radiusKm <= 0) return;
		for (const auto& m : Missiles)
		{
			if (m->Gone || m->Side == side) continue;
			double d = (m->Pos - at).Length();
			if (d > radiusKm) continue;
			m->Hp -= damage * (1 - 0.5 * d / radiusKm);
			if (m->Hp <= 0)
			{
				m->Gone = true;
				RoundLost(m);
				Emit(FxKind::MissileKill, m->Pos, m->Pos, 0.08, m->Side, nullptr, -1);
			}
		}
		for (int i = 0; i < Units.Count(); i++)
		{
			auto o = Units[i];
			if (o->Side == side || !o->Present() || !WeakPoints::IsSmallCraft(o) || o->Surrendering) continue;
			double d = cs::Math::Max(0.0, (o->Pos - at).Length() - o->Radius());
			if (d > radiusKm) continue;
			ApplyDamage(o, nullptr, damage * (1 - 0.5 * d / radiusKm), at, std::nullopt, 1, std::nullopt, false, WeaponFamily::Flak);
		}
	}

	void Battle::StepInterceptor(const cs::Ref<Missile>& m, double dt)
	{
		if (m->Life <= 0)
		{
			m->Gone = true;
			Emit(FxKind::MissileKill, m->Pos, m->Pos, 0.03, m->Side, nullptr, -1);
			return;
		}
		if (m->Prey == nullptr || m->Prey->Gone)
		{
			m->Prey = nullptr;
			double bestD = ScreenWeapons::ReacquireKm;
			for (const auto& o : Missiles)
			{
				if (o->Side == m->Side || !ScreenWeapons::IsThreat(o) || Hunted(o, m->Side)) continue;
				double d = (o->Pos - m->Pos).Length();
				if (d < bestD)
				{
					bestD = d;
					m->Prey = o;
				}
			}
			if (m->Prey == nullptr)
			{
				m->Gone = true;
				Emit(FxKind::MissileKill, m->Pos, m->Pos, 0.03, m->Side, nullptr, -1);
				return;
			}
		}
		auto prey = m->Prey;
		auto to = prey->Pos - m->Pos;
		double dist = to.Length();
		auto lead = prey->Pos + prey->Vel * cs::Math::Min(dist / cs::Math::Max(m->Speed, 1e-6), 10.0);
		auto want = (lead - m->Pos).Normalized() * m->Speed;
		auto dv = want - m->Vel;
		double turn = m->Speed * 1.2 * dt;
		m->Vel = m->Vel + (dv.Length() > turn ? dv.Normalized() * turn : dv);
		auto start = m->Pos;
		m->Pos = m->Pos + m->Vel * dt;
		// Swept, and allowing for the prey's own motion this step, so neither a coarse step nor a crossing shot slips past.
		if (SegmentPointDistance(start, m->Pos, prey->Pos) > ScreenWeapons::FuzeKm + prey->Vel.Length() * dt) return;
		m->Gone = true;
		Emit(FxKind::MissileKill, m->Pos, m->Pos, 0.04, m->Side, nullptr, -1);
		if (rng.NextDouble() >= ScreenWeapons::InterceptorReliability()->Value) return;
		prey->Hp -= m->Damage;
		if (prey->Hp > 0) return;
		prey->Gone = true;
		RoundLost(prey);
		Emit(FxKind::MissileKill, prey->Pos, prey->Pos, 0.08, prey->Side, nullptr, -1);
	}

	cs::Ref<Missile> Battle::NearestMissile(int side, DVec3 origin, double range)
	{
		cs::Ref<Missile> best = nullptr;
		double bestD = range * 2;
		for (const auto& m : Missiles)
		{
			// Another side's interceptors threaten nothing a laser need waste a shot on (U.SPC-T489).
			if (m->Gone || m->Side == side || m->Kind == MissileKind::Interceptor) continue;
			double d = (m->Pos - origin).Length();
			if (d > range) continue;
			// A Breacher round tumbling adrift threatens nobody (U.SPC-T519): shot at only with nothing live in reach.
			if (m->Adrift) d += range;
			if (d < bestD)
			{
				bestD = d;
				best = m;
			}
		}
		return best;
	}

	cs::Ref<Subsystem> Battle::PickSubsystem(const cs::Ref<BattleUnit>& target, const cs::Ref<BattleUnit>& shooter)
	{
		bool robbing = shooter != nullptr && !shooter->PlayerControlled() && RaidDoctrine::Robs(shooter->Faction, target->Ship);
		if (target->Kind == UnitKind::Fighter) return nullptr;
		bool focus = Broadsides::FocusesFire(shooter);
		double total = 0;
		const auto& subs = target->Combat->Subsystems;
		auto weights = cs::NewArray<double>(subs.Count());
		for (int i = 0; i < subs.Count(); i++)
		{
			auto s = subs[i];
			if (s->Destroyed()) continue;
			double w;
			if (robbing) w = RaidDoctrine::SubsystemWeight(s->Kind(), s->Shield > 0);
			else switch (s->Kind())
			{
				case SubsystemKind::HeavyLaser:
				case SubsystemKind::LaserTurret:
				case SubsystemKind::Railgun:
				case SubsystemKind::Autocannon:
				case SubsystemKind::LightRailgun:
				case SubsystemKind::AutoTurret:
				case SubsystemKind::TorpedoTube:
				case SubsystemKind::PhaserArray:
				case SubsystemKind::PlasmaCannon:
				case SubsystemKind::AntimatterTorpedo:
				case SubsystemKind::VulcanCannon:
				case SubsystemKind::HiveLauncher:
				case SubsystemKind::RocketPod:
				case SubsystemKind::MissileLauncher: w = 3; break;
				// The gun that outranges them all is the one gunners most want silenced (U.SPC-T487).
				case SubsystemKind::TachyonLance:
				// As are a dreadnought's spinal guns (U.SPC-T490).
				case SubsystemKind::Coilgun: w = 4; break;
				case SubsystemKind::ShieldGenerator: w = s->Shield > 0 ? 2.5 : 0.5; break;
				case SubsystemKind::Engine: w = 1.4; break;
				case SubsystemKind::Hangar: w = target->Combat->Fighters() > 0 ? 1.8 : 0.4; break;
				case SubsystemKind::Bridge: w = 1.1; break;
				case SubsystemKind::PointDefense: w = 0.8; break;
				// The screen is worth knocking out before the strike goes in (U.SPC-T489).
				case SubsystemKind::FlakCannon:
				case SubsystemKind::InterceptorMissile: w = 1.2; break;
				case SubsystemKind::Reactor: w = 0.25; break;
				// A radiator panel is a weak point, the more worth it on a hull already running hot (U.SPC-T491).
				case SubsystemKind::Radiator: w = Radiators::TargetWeight(target); break;
				default: w = 0.5; break;
			}
			// A bridge buried at the centre of the hull (U.SPC-T492) is worth aiming at only once the hull is holed deep
			// enough for fire to get through to it.
			if (s->Def->Buried) w *= BuriedBridges::Reach(target->Combat, s);
			// Fire goes where the shield is already weak (U.SPC-T501): a mount in a patch whose field is down or whose
			// emitter is shot away, unless a captain at the helm has told the crews to spread it.
			if (focus) w *= ShieldEmitters::FocusWeight(target->Combat, s);
			weights[i] = w;
			total += w;
		}
		if (total <= 0) return nullptr;
		double pick = rng.NextDouble() * total;
		for (int i = 0; i < subs.Count(); i++)
		{
			pick -= weights[i];
			if (weights[i] > 0 && pick <= 0) return subs[i];
		}
		return nullptr;
	}

	void Battle::ApplyDamage(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& sub, double damage, DVec3 at, cs::Nullable<DVec3> shieldAt, double exposed, cs::Nullable<DVec3> incoming,
		bool strikeTorpedo, WeaponFamily family)
	{
		if (!target->Present()) return;
		auto c = target->Combat;
		// What the shot is (US-T189): how hard it hits the field, the hull and the plate.
		bool profiled = family != WeaponFamily::None && !WeaponSystems::ProfileOf(family).Neutral();
		// The shield that takes it is the field over the patch of hull it strikes, in 3D (U.SPC-T501); a guided munition
		// strikes where it crossed into the hull on the side it came in from, not at the hardpoint it homed on (U.SPC-T380).
		auto strike = target->ShieldStrike(shieldAt.value_or(at), incoming);
		auto shieldPoint = target->PointLocal(strike);
		auto generator = c->EmitterFor(strike);
		if (strikeTorpedo) damage *= StrikeTorpedoes::Factor(target, generator == nullptr || generator->Shield <= 0);
		if (generator != nullptr)
		{
			generator->LastHitSeconds = Seconds;
			if (generator->Shield > 0)
			{
				double soaked;
				if (profiled) damage = WeaponSystems::ThroughShield(family, damage, generator->Shield, soaked);
				else
				{
					soaked = cs::Math::Min(generator->Shield, damage);
					damage -= soaked;
				}
				generator->Shield -= soaked;
				// Sub names the emitter whose field took it, so the bubble it raises is that patch's (U.SPC-T40).
				Emit(FxKind::ShieldHit, shieldPoint, target->Pos, target->LengthKm, target->Side, target, generator->Index);
				WeaponSystems::Cue(family, WeaponCueEvent::ShieldImpact, shieldPoint, soaked, target->Side, Id);
				if (damage <= 0) return;
			}
		}
		// Past the field, the shot meets the hull (US-T189): a plasma bolt or a torpedo guts it, a laser only scorches.
		if (profiled) damage = WeaponSystems::VsHull(family, damage);
		if (sub != nullptr && exposed > 1) damage *= exposed;
		// A capital ship's crew braced for the broadside shrugs off part of it (U.SPC-T425), and a shot meeting its hull
		// obliquely, measured against the hull's own surface where the shot's line strikes it in 3D, crosses thicker plate
		// and is partly turned aside (U.SPC-T534).
		double armour = c->Spec->Armor;
		// A railgun's slug or a torpedo's warhead goes through much of the plate (US-T189).
		if (profiled) armour = WeaponSystems::ArmourFaced(family, armour);
		if (Broadsides::Commands(target))
		{
			damage *= Broadsides::BraceFactor(target);
			// A weak point struck from its open side has no armour to angle (U.SPC-T424).
			if (incoming.has_value() && !(sub != nullptr && exposed > 1))
			{
				auto [thicker, through] = ArmourAngling::Angling(target, shieldAt.value_or(at), *incoming);
				armour *= thicker;
				damage *= through;
			}
		}
		damage = cs::Math::Max(damage * 0.15, damage - armour);
		Emit(FxKind::Impact, at, at, damage, target->Side, target, sub ? sub->Index : -1);
		WeaponSystems::Cue(family, WeaponCueEvent::Impact, at, damage, target->Side, Id);
		if (sub != nullptr && !sub->Destroyed())
		{
			// A buried bridge (U.SPC-T492) takes only what gets through the hull around it; the rest stays in the hull.
			double toSub = 0.65 * BuriedBridges::Reach(c, sub);
			sub->Hp -= damage * toSub;
			c->Hull -= damage * (1 - toSub);
			if (sub->Hp <= 0)
			{
				c->Hull -= -sub->Hp;
				sub->Hp = 0;
				Emit(FxKind::SubsystemDestroyed, at, at, target->LengthKm * 0.12, target->Side, target, sub->Index);
				if (sub->Kind() == SubsystemKind::Reactor) c->Hull = 0;
				if (sub->Kind() == SubsystemKind::ShieldGenerator) sub->Shield = 0;
				if (sub->Kind() == SubsystemKind::Hangar) c->SetFighters(cs::Math::Max(0, c->Fighters() - 6));
			}
		}
		else c->Hull -= damage;

		if (c->Hull <= 0)
		{
			c->Hull = 0;
			target->Dead = true;
			Emit(FxKind::UnitDestroyed, target->Pos, target->Pos, target->LengthKm, target->Side, target, -1);
			if (UnitDestroyed) UnitDestroyed(shared_from_this(), target);
		}
	}

	void Battle::ApplyImpact(const cs::Ref<BattleUnit>& target, DVec3 at, double hp)
	{
		if (!target->Present() || hp <= 0) return;
		auto c = target->Combat;
		double shieldBefore = c->Shield();
		auto generator = c->EmitterFor(target->ShieldStrike(at));
		cs::List<cs::Ref<Subsystem>> lost;
		double through = Collisions::ApplyDamage(c, target->ToLocal(at), hp, Seconds, &lost);
		if (c->Shield() < shieldBefore) Emit(FxKind::ShieldHit, at, target->Pos, target->LengthKm, target->Side, target, generator ? generator->Index : -1);
		if (through > 0) Emit(FxKind::Impact, at, at, through, target->Side, target, -1);
		for (const auto& s : lost) Emit(FxKind::SubsystemDestroyed, at, at, target->LengthKm * 0.12, target->Side, target, s->Index);
		if (c->Hull <= 0)
		{
			c->Hull = 0;
			target->Dead = true;
			Emit(FxKind::UnitDestroyed, target->Pos, target->Pos, target->LengthKm, target->Side, target, -1);
			if (UnitDestroyed) UnitDestroyed(shared_from_this(), target);
		}
	}

	void Battle::StepMissiles(double dt)
	{
		// The micro-missiles of hive rounds that split this step, added once the step is done (US-T101).
		cs::List<cs::Ref<Missile>> swarm;
		for (const auto& m : Missiles)
		{
			if (m->Gone) continue;
			m->Life -= dt;
			// A dumb-fire rocket flies straight and hits what it meets (US-T101).
			if (m->Kind == MissileKind::Rocket)
			{
				StepRocket(m, dt);
				continue;
			}
			// A hive round homes a moment, then splits into its swarm on the way to a target still there to hit (US-T101).
			if (m->SplitInto > 0 && m->Decoy == nullptr)
			{
				m->SplitIn -= dt;
				if (m->SplitIn <= 0 && m->Target != nullptr && m->Target->Present() && m->Target->Side != m->Side && !m->Target->Surrendering)
				{
					SplitHive(m, swarm);
					continue;
				}
			}
			// An interceptor after a round flies its own hunt (U.SPC-T489).
			if (m->Kind == MissileKind::Interceptor && m->Target == nullptr)
			{
				StepInterceptor(m, dt);
				continue;
			}
			// A Breacher round flies its own ballistic course (U.SPC-T466).
			if (m->Kind == MissileKind::Breacher)
			{
				StepBreacher(m, dt);
				continue;
			}
			// A hull that has struck its colours is no longer shot at, nor boarded until its terms open it.
			bool spared = m->Target != nullptr && m->Target->Surrendering && !(m->Kind == MissileKind::Pod && m->Target->SurrenderOpenToBoarding);
			if (m->Life <= 0 || m->Target == nullptr || !m->Target->Present() || m->Target->Side == m->Side || spared)
			{
				m->Gone = true;
				Emit(FxKind::MissileKill, m->Pos, m->Pos, 0.05, m->Side, nullptr, -1);
				continue;
			}
			// Drawn off by a flare-and-chaff cloud (U.SPC-T424): it chases the cloud and goes off in it, harmlessly.
			if (m->Decoy != nullptr)
			{
				auto toDecoy = m->Decoy->Pos - m->Pos;
				if (m->Decoy->Gone || toDecoy.Length() <= cs::Math::Max(0.03, m->Speed * dt))
				{
					m->Gone = true;
					Emit(FxKind::MissileKill, m->Pos, m->Pos, 0.05, m->Side, nullptr, -1);
					continue;
				}
				auto chase = toDecoy.Normalized() * m->Speed - m->Vel;
				double swing = m->Speed * 0.6 * dt;
				m->Vel = m->Vel + (chase.Length() > swing ? chase.Normalized() * swing : chase);
				m->Pos = m->Pos + m->Vel * dt;
				continue;
			}
			auto t = m->Target;
			auto aim = m->TargetSub != nullptr && !m->TargetSub->Destroyed() ? t->PointOf(m->TargetSub->Def) : t->Pos;
			auto to = aim - m->Pos;
			double dist = to.Length();
			// Lead the target a little and close at full speed.
			auto lead = aim + t->Vel * cs::Math::Min(dist / cs::Math::Max(m->Speed, 1e-6), 20.0);
			auto want = (lead - m->Pos).Normalized() * m->Speed;
			auto dv = want - m->Vel;
			// A bomber's torpedo on a target a recon fighter is painting steers onto it that much harder (U.SPC-T485).
			double turn = m->Speed * 0.6 * dt * ElectronicWarfare::Guidance(shared_from_this(), m);
			m->Vel = m->Vel + (dv.Length() > turn ? dv.Normalized() * turn : dv);
			auto start = m->Pos;
			m->Pos = m->Pos + m->Vel * dt;
			// Swept test, so a coarse step cannot pass through the target.
			if (SegmentPointDistance(start, m->Pos, aim) < t->Radius() * 0.6 + 0.05)
			{
				m->Gone = true;
				// Where the warhead crossed the hull toward where it came from: the arc it has to get through.
				auto from = start - t->Pos;
				if (from.LengthSquared() < 1e-12) from = m->Vel * -1;
				auto entry = t->Pos + from.Normalized() * t->Radius();
				if (m->Kind == MissileKind::Pod) PodHit(m, t, aim, entry);
				else if (m->Kind == MissileKind::Emp) EmpBurst(m->Pos, m->Side, m->EmpSeconds, m->EmpRadiusKm);
				else
				{
					auto sub = m->TargetSub != nullptr && !m->TargetSub->Destroyed() ? m->TargetSub : PickSubsystem(t);
					ApplyDamage(t, sub, m->Damage, aim, entry, m->SmallCraft ? WeakPoints::Factor(t, sub, m->Vel - t->Vel) : 1, m->Vel - t->Vel,
						StrikeTorpedoes::IsStrike(m), WeaponSystems::FamilyOfRound(m->Kind));
				}
			}
		}
		if (swarm.Count() > 0) Missiles.AddRange(swarm);
		Missiles.RemoveAll([](const cs::Ref<Missile>& m) { return m->Gone; });
	}

	// ---------------------------------------------------------------- the full arsenal (US-T101)

	void Battle::FireRockets(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, DVec3 dir, const cs::Ref<WeaponSpec>& w)
	{
		s->Aim = dir;
		double speed = cs::Math::Max(w->MissileSpeedKmS, 1e-3);
		bool small = WeakPoints::IsSmallCraft(u);
		for (int k = 0; k < w->Salvo && s->Ammo > 0; k++)
		{
			s->Ammo--;
			auto line = Gunnery::Scatter(dir, Arsenal::RocketSpreadDeg * Astro::Deg2Rad, rng);
			auto m = cs::New<Missile>();
			// Rippled off the rail one behind another, not all from one point.
			m->Pos = origin - line * (k * 0.004);
			m->Vel = u->Vel + line * speed;
			m->Damage = w->Damage;
			m->Speed = speed;
			m->Hp = Arsenal::RocketHp;
			m->Life = w->RangeKm / speed;
			m->Side = u->Side;
			m->Kind = MissileKind::Rocket;
			m->SmallCraft = small;
			Missiles.Add(m);
		}
		Emit(FxKind::MissileLaunch, origin, origin + dir, 0, u->Side, u, s->Index);
	}

	void Battle::StepRocket(const cs::Ref<Missile>& m, double dt)
	{
		auto step = m->Vel * dt;
		double len = step.Length();
		if (len > 1e-12)
		{
			cs::Ref<BattleUnit> hit;
			double t = FirstHostileAlong(m->Side, m->Pos, step / len, len, hit);
			if (hit != nullptr)
			{
				auto at = m->Pos + step / len * t;
				m->Gone = true;
				auto sub = SubsystemNear(hit, at);
				ApplyDamage(hit, sub, m->Damage, at, at, m->SmallCraft ? WeakPoints::Factor(hit, sub, m->Vel - hit->Vel) : 1, m->Vel - hit->Vel,
					false, WeaponFamily::Missile);
				return;
			}
		}
		m->Pos = m->Pos + step;
		if (m->Life <= 0)
		{
			// Out of motor and reach: it bursts harmlessly where it is.
			m->Gone = true;
			Emit(FxKind::MissileKill, m->Pos, m->Pos, 0.03, m->Side, nullptr, -1);
		}
	}

	void Battle::SplitHive(const cs::Ref<Missile>& m, cs::List<cs::Ref<Missile>>& swarm)
	{
		m->Gone = true;
		auto t = m->Target;
		auto heading = m->Vel.LengthSquared() > 1e-12 ? m->Vel.Normalized() : (t->Pos - m->Pos).Normalized();
		Emit(FxKind::HiveSplit, m->Pos, m->Pos + heading * 0.15, m->SplitInto, m->Side, nullptr, -1);
		double speed = m->Speed * Arsenal::MicroSpeedFactor;
		for (int i = 0; i < m->SplitInto; i++)
		{
			auto line = Gunnery::Scatter(heading, Arsenal::SplitSpreadDeg * Astro::Deg2Rad, rng);
			auto micro = cs::New<Missile>();
			micro->Pos = m->Pos;
			// Kicked out of the casing across its line, the seekers then turn in onto the target on their own.
			micro->Vel = m->Vel * 0.6 + line * (speed * 0.5);
			micro->Target = t;
			// A weak point picked for the round stays every seeker's; else each finds its own mark on a hull.
			micro->TargetSub = m->TargetSub != nullptr && !m->TargetSub->Destroyed() ? m->TargetSub
				: t->Kind == UnitKind::Fighter ? nullptr : PickSubsystem(t);
			micro->Damage = m->Damage;
			micro->Speed = speed;
			micro->Hp = Arsenal::MicroHp;
			micro->Life = Arsenal::MicroLifeS;
			micro->Side = m->Side;
			micro->Kind = MissileKind::HiveMicro;
			micro->SmallCraft = m->SmallCraft;
			swarm.Add(micro);
		}
	}

	bool Battle::SecondaryFires(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s)
	{
		cs::Ref<FighterControl> stick;
		if (Flown.Count() == 0 || !Flown.TryGetValue(u, stick)) return true;
		auto group = WeaponGroups::Of(s->Kind());
		return group.has_value() && *group == stick->SecondaryGroup;
	}

	// ---------------------------------------------------------------- boarding

	void Battle::FirePods(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s)
	{
		if (s->Ammo <= 0 || u->Combat->MarinesAvailable() < WeaponSpec::PodMarines * 2) return;
		cs::Ref<BattleUnit> target = nullptr;
		double best = s->Def->Weapon->RangeKm;
		for (const auto& o : Units)
		{
			if (!Hostile(u, o) || o->Captured || o->Kind == UnitKind::Fighter) continue;
			// A hull waiting on terms isn't fired on, pods included -- unless the terms are to take it.
			if (o->Surrendering && !o->SurrenderOpenToBoarding) continue;
			if (o->Combat->Shield() > o->Combat->Spec->Shield * 0.05 && o->Combat->HasShieldGenerator()) continue;
			// A rival's boarding is theirs; an ally's on this side is joined (U.SPC-T392).
			if (!o->Combat->Interior->Admits(u->Faction, u->Side)) continue;
			double d = (o->Pos - u->Pos).Length();
			if (o->Combat->Disabled()) d *= 0.5;
			if (d < best)
			{
				best = d;
				target = o;
			}
		}
		if (target == nullptr) return;
		FirePodsAt(u, s, target);
	}

	void Battle::FirePodsAt(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, const cs::Ref<BattleUnit>& target)
	{
		auto origin = u->PointOf(s->Def);
		s->Cooldown = s->Def->Weapon->Cooldown * (0.85 + rng.NextDouble() * 0.3);
		for (int k = 0; k < s->Def->Weapon->Salvo && s->Ammo > 0; k++)
		{
			if (u->Combat->MarinesAvailable() < WeaponSpec::PodMarines * 2) break;
			s->Ammo--;
			RemoveMarines(u->Combat->Interior, WeaponSpec::PodMarines);
			double rx = rng.NextDouble() - 0.5;
			double ry = rng.NextDouble() - 0.5;
			double rz = rng.NextDouble() - 0.5;
			auto spread = DVec3(rx, ry, rz) * 0.3;
			auto m = cs::New<Missile>();
			m->Pos = origin;
			m->Vel = ((target->Pos - origin).Normalized() + spread).Normalized() * 0.3 + u->Vel;
			m->Target = target;
			m->TargetSub = PickSubsystem(target, u);
			m->Damage = s->Def->Weapon->Damage;
			m->Speed = s->Def->Weapon->MissileSpeedKmS;
			m->Hp = 40;
			m->Life = 60;
			m->Side = u->Side;
			m->Kind = MissileKind::Pod;
			m->Owner = u->Faction;
			m->Marines = WeaponSpec::PodMarines;
			m->Source = u;
			Missiles.Add(m);
		}
		Emit(FxKind::MissileLaunch, origin, origin + u->Forward, 0, u->Side, u, s->Index);
	}

	void Battle::PodHit(const cs::Ref<Missile>& m, const cs::Ref<BattleUnit>& t, DVec3 at, cs::Nullable<DVec3> shieldAt)
	{
		auto c = t->Combat;
		auto generator = c->EmitterFor(t->ShieldStrike(shieldAt.value_or(at), m->Vel - t->Vel));
		if (generator != nullptr && generator->Shield > 0)
		{
			Emit(FxKind::PodBounce, at, at, t->LengthKm, t->Side, t, -1);
			return;
		}
		// The drill tip punches in; the squad goes through the hole it made.
		ApplyDamage(t, m->TargetSub != nullptr && !m->TargetSub->Destroyed() ? m->TargetSub : nullptr, m->Damage, at, shieldAt);
		if (!t->Present()) return;
		Embed(t, at, m->Vel, m->Side, false);
		auto local = t->ToLocal(at);
		auto space = c->Interior->Nearest(local.Z, local.Y);
		// An ally on the same side boarding it already is joined, the two fighting it as one party (U.SPC-T392); a hull
		// taken while the pod was in flight (or one a faction of another side boards) cannot be landed on: the squad
		// returns to the ship that fired it, if that is still there.
		// The squad fights as well as it drilled in the habitat it came from (U.SPC-T465).
		double drill = m->Source != nullptr && m->Source->Combat != nullptr ? m->Source->Combat->Interior->DrillFactor() : 1;
		if (!c->Interior->Board(m->Owner, m->Owner->MarineQuality * drill, m->Marines, space, true, Seconds, rng, m->Side))
		{
			if (m->Source != nullptr && m->Source->Present() && !m->Source->Captured) m->Source->Combat->Interior->Embark(0, m->Marines);
		}
		Emit(FxKind::Breach, at, at - m->Vel.Normalized() * 0.05, t->LengthKm * 0.1, m->Side, t, space->Index);
	}

	void Battle::Embed(const cs::Ref<BattleUnit>& t, DVec3 at, DVec3 travel, int side, bool breacher)
	{
		auto inward = travel.LengthSquared() > 0 ? travel.Normalized() : (t->Pos - at).Normalized();
		DVec3 right, up;
		t->Basis(right, up);
		auto e = cs::New<Core::Embedded>();
		e->Unit = t;
		e->Local = t->ToLocal(at);
		e->Side = side;
		e->Breacher = breacher;
		e->Inward = DVec3(DVec3::Dot(inward, right), DVec3::Dot(inward, up), DVec3::Dot(inward, t->Forward));
		Embedded.Add(e);
	}

	void Battle::RemoveMarines(const cs::Ref<Interior>& interior, double count)
	{
		for (const auto& c : interior->Compartments)
		{
			double take = cs::Math::Min(c->Marines, count);
			c->Marines -= take;
			count -= take;
			if (count <= 0) return;
		}
	}

	void Battle::FireBreacherBay(const cs::Ref<BattleUnit>& u, double dt)
	{
		if (u->Kind != UnitKind::Ship || u->Ship == nullptr) return;
		if (u->BreacherBayReload > 0)
		{
			u->BreacherBayReload -= dt * u->Combat->Readiness;
			return;
		}
		// Any side's captain fires the bay its hull carries (U.SPC-T521); whom it may fire at is BreacherTarget's to say.
		if (u->Fleeing || u->Combat->Disabled() || BreacherBays::Count(u->Ship) <= 0) return;
		auto interior = u->Combat->Interior;
		if (!BreacherBays::TeamReady(interior)) return;
		auto target = BreacherTarget(u);
		if (target == nullptr) return;
		FireBreacherAt(u, target);
	}

	void Battle::FireBreacherAt(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		auto interior = u->Combat->Interior;
		double speed = BreacherBays::RoundSpeedKmS()->Value;
		auto origin = u->Pos + u->Forward * (u->Radius() * 0.3);
		auto aim = BreacherBays::Lead(origin, u->Vel, target->Pos, target->Vel, speed);
		auto dir = aim - origin;
		if (dir.LengthSquared() < 1e-18) dir = u->Forward;
		double team = MarineHabitats::Draw(interior, BreacherBays::TeamSize()->Value);
		auto m = cs::New<Missile>();
		m->Pos = origin;
		m->Vel = u->Vel + dir.Normalized() * speed;
		m->Target = target;
		m->Damage = BreacherBays::ImpactDamage()->Value;
		m->Speed = speed;
		m->Hp = BreacherBays::RoundHp;
		// It flies on past a miss for as long again as it takes to cross the bay's range, then goes adrift (U.SPC-T519).
		m->Life = BreacherBays::RangeKm()->Value / speed * 2 + 5;
		m->Side = u->Side;
		m->Kind = MissileKind::Breacher;
		m->Owner = u->Faction;
		m->Marines = team;
		m->Source = u;
		Missiles.Add(m);
		u->BreacherBayReload = BreacherBays::IntervalSeconds(u->Ship);
		Emit(FxKind::MissileLaunch, origin, origin + dir.Normalized(), 0, u->Side, u, -1);
	}

	cs::Ref<BattleUnit> Battle::BreacherTarget(const cs::Ref<BattleUnit>& u)
	{
		cs::Ref<BattleUnit> best = nullptr;
		double bestD = BreacherBays::RangeKm()->Value;
		for (const auto& o : Units)
		{
			if (!Hostile(u, o) || o->Captured || o->Kind == UnitKind::Fighter) continue;
			auto inside = o->Combat->Interior;
			if (!inside->Admits(u->Faction, u->Side)) continue;
			// A hull waiting on terms is not boarded until they open it to boarding.
			if (!MayBreach(u, o) || (o->Surrendering && !o->SurrenderOpenToBoarding)) continue;
			// A merchant's hold is asked for first, not fought through its corridors for (U.SPC-T374).
			if (o->Ship != nullptr && Spares(u, o) && o->Ship->HasCargo && !o->SurrenderOpenToBoarding) continue;
			if (!BreacherBays::Opens(o->Combat)) continue;
			double d = (o->Pos - u->Pos).Length();
			if (o->Combat->Disabled()) d *= 0.5;
			// A hull battered nearer to wreck is the readier prize (U.SPC-T521).
			d *= 0.5 + 0.5 * cs::Math::Min(1.0, o->Combat->HullFraction());
			if (d < bestD)
			{
				bestD = d;
				best = o;
			}
		}
		return best;
	}

	bool Battle::MayBreach(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		return MayBoard(u, target) || (target->Faction != nullptr && target->Faction->IsPirate);
	}

	void Battle::StepBreacher(const cs::Ref<Missile>& m, double dt)
	{
		if (m->Adrift)
		{
			m->Pos = m->Pos + m->Vel * dt;
			return;
		}
		if (m->Life <= 0)
		{
			GoAdrift(m);
			m->Pos = m->Pos + m->Vel * dt;
			return;
		}
		auto t = m->Target;
		if (t != nullptr && t->Present())
		{
			auto aim = BreacherBays::Lead(m->Pos, DVec3::Zero, t->Pos, t->Vel, m->Vel.Length());
			m->Vel = BreacherBays::Correct(m->Vel, aim - m->Pos, BreacherBays::SteerKmS2()->Value, dt);
		}
		auto start = m->Pos;
		m->Pos = m->Pos + m->Vel * dt;
		cs::Ref<BattleUnit> hit = nullptr;
		double nearest = cs::Double::MaxValue;
		for (const auto& o : Units)
		{
			if (!o->Present() || o->Side == m->Side || o->Kind == UnitKind::Fighter) continue;
			if (SegmentPointDistance(start, m->Pos, o->Pos) >= o->Radius() * 0.6 + 0.05) continue;
			double d = (o->Pos - start).LengthSquared();
			if (d < nearest)
			{
				nearest = d;
				hit = o;
			}
		}
		if (hit == nullptr) return;
		auto from = start - hit->Pos;
		if (from.LengthSquared() < 1e-12) from = m->Vel * -1;
		m->Gone = BreacherHit(m, hit, from.Normalized());
	}

	void Battle::GoAdrift(const cs::Ref<Missile>& m)
	{
		m->Adrift = true;
		m->Target = nullptr;
		m->TargetSub = nullptr;
	}

	void Battle::RoundLost(const cs::Ref<Missile>& m)
	{
		if (m->Kind != MissileKind::Breacher || m->Marines < 0.5) return;
		Crews::RecordDeaths(m->Owner, m->Marines);
		m->Marines = 0;
	}

	bool Battle::BreacherHit(const cs::Ref<Missile>& m, const cs::Ref<BattleUnit>& t, DVec3 from)
	{
		auto c = t->Combat;
		auto entry = t->Pos + from * t->Radius();
		auto contact = t->Pos + from * (t->Radius() * 0.3);
		if (!BreacherBays::Opens(c))
		{
			Emit(FxKind::PodBounce, entry, entry, t->LengthKm, t->Side, t, -1);
			// It glances off the shield and careens away, tumbling (U.SPC-T519).
			m->Vel = t->Vel + DriftingRounds::Glance(m->Vel - t->Vel, from);
			m->Pos = entry + from * 0.05;
			GoAdrift(m);
			return false;
		}
		ApplyDamage(t, PickSubsystem(t), m->Damage, contact, entry);
		if (!t->Present()) return true;
		Embed(t, contact, m->Vel - t->Vel, m->Side, true);
		auto local = t->ToLocal(contact);
		auto space = c->Interior->Nearest(local.Z, local.Y);
		bool spared = t->Surrendering && !t->SurrenderOpenToBoarding;
		// The team fights as well as it drilled in the habitat it came from (U.SPC-T465), with the shock of the breach.
		double drill = m->Source != nullptr && m->Source->Combat != nullptr ? m->Source->Combat->Interior->DrillFactor() : 1;
		if (spared || !c->Interior->Board(m->Owner, m->Owner->MarineQuality * BreacherBays::ShockEdge * drill, m->Marines, space, true, Seconds, rng, m->Side))
		{
			if (m->Source != nullptr && m->Source->Present() && !m->Source->Captured) m->Source->Combat->Interior->Embark(0, m->Marines);
		}
		Emit(FxKind::Breach, contact, contact + from * 0.1, t->LengthKm * 0.15, m->Side, t, space->Index);
		return true;
	}

	void Battle::ChooseBoarding(const cs::Ref<BattleUnit>& u)
	{
		if (u->BoardTarget != nullptr)
		{
			auto bt = u->BoardTarget;
			// A hull waiting on terms is not boarded until they open it to boarding (as it is not fired on, pods included).
			bool stillOpen = bt->Present() && !bt->Captured && (!bt->Surrendering || bt->SurrenderOpenToBoarding) &&
				(bt->Combat->Disabled() || bt->SurrenderOpenToBoarding || bt->Stunned > 10);
			if (!stillOpen) u->BoardTarget = nullptr;
		}
		// A boarder that can no longer fly the approach (crippled, or breaking off) gives up the
		// prize, or the battle would wait on a boarding that never comes.
		if (u->Fleeing || u->Combat->Disabled()) { u->BoardTarget = nullptr; return; }
		// A carrier with boarding shuttles sends them, not its own hull (U.SPC-T486).
		if (BoardsByShuttle(u)) { u->BoardTarget = nullptr; return; }
		if (u->BoardTarget != nullptr) return;
		bool boarder = u->Faction->IsPirate || u->Faction->IsMilitary || (Boards != nullptr && Boards(u));
		if (!boarder || u->Combat->MarinesAvailable() < 15) return;
		cs::Ref<BattleUnit> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& o : Units)
		{
			if (!Hostile(u, o) || o->Captured || o->Kind != UnitKind::Ship) continue;
			if (!o->Combat->Interior->Admits(u->Faction, u->Side)) continue;
			if (!MayBoard(u, o) || (o->Surrendering && !o->SurrenderOpenToBoarding)) continue;
			// A merchant's hold is asked for, not fought through its corridors for (U.SPC-T374): raiders board a hull
			// with cargo aboard only once it has turned their demand down.
			if (Spares(u, o) && o->Ship->HasCargo && !o->SurrenderOpenToBoarding) continue;
			bool open = o->Combat->Disabled() || o->SurrenderOpenToBoarding || o->Stunned > 10;
			if (!open) continue;
			if (ThreatNear(u, o->Pos, 6)) continue;
			double d = (o->Pos - u->Pos).Length();
			if (d < bestD)
			{
				bestD = d;
				best = o;
			}
		}
		u->BoardTarget = best;
	}

	bool Battle::ThreatNear(const cs::Ref<BattleUnit>& u, DVec3 at, double km)
	{
		for (const auto& o : Units)
			if (Hostile(u, o) && !o->Captured && !o->Surrendering && o->Kind != UnitKind::Fighter && o->Combat->Armed() && !o->Combat->Disabled() && (o->Pos - at).Length() < km)
				return true;
		return false;
	}

	void Battle::Approach(const cs::Ref<BattleUnit>& u, double dt)
	{
		auto t = u->BoardTarget;
		auto c = u->Combat;
		double maxSpeed = c->Spec->MaxSpeed * (0.25 + 0.75 * c->EngineFactor());
		auto dock = u->DockPoint(t, 0.06);
		auto to = dock - u->Pos;
		double dist = to.Length();
		// Arrive matched: slow down as it closes, holding the target's own drift.
		double want = cs::Math::Min(maxSpeed, dist * 0.05 + 0.002);
		auto desired = t->Vel + (dist > 1e-6 ? to / dist : DVec3::Zero) * want;
		auto dv = desired - u->Vel;
		double step = c->Spec->Accel * (0.3 + 0.7 * c->EngineFactor()) * dt * 1.5;
		u->Vel = u->Vel + (dv.Length() > step ? dv.Normalized() * step : dv);
		u->Pos = u->Pos + u->Vel * dt;
		TurnToward(u, t->Forward, c->Spec->TurnRateDegS, dt);

		if (dist < 0.08 && (u->Vel - t->Vel).Length() < 0.01)
		{
			u->LatchTimer += dt;
			if (u->LatchTimer > 20)
			{
				u->Latched = t;
				u->LatchLocal = t->ToLocal(u->Pos);
				u->BoardTarget = nullptr;
				u->LatchTimer = 0;
				if (t->Combat->Interior->Admits(u->Faction, u->Side))
				{
					double marines = c->MarinesAvailable() * 0.85;
					RemoveMarines(c->Interior, marines);
					auto airlock = t->Combat->Interior->Of(CompartmentKind::Midships);
					if (!airlock) airlock = t->Combat->Interior->Nearest(0, 0);
					t->Combat->Interior->Board(u->Faction, u->Faction->MarineQuality * c->Interior->DrillFactor(), marines, airlock, false, Seconds, rng, u->Side);
					Emit(FxKind::Latch, u->Pos, t->Pos, u->LengthKm, u->Side, t, airlock->Index);
				}
			}
		}
		else u->LatchTimer = 0;
	}

	void Battle::StepBoarding(double dt)
	{
		for (const auto& u : Units)
		{
			if (!u->Present() || u->Captured) continue;
			auto interior = u->Combat->Interior;
			if (interior->Boarder == nullptr) continue;
			auto boarder = interior->Boarder;
			if (interior->Commanded()) KeepOrdersCommanded(u, interior);
			if (u->SurrenderOpenToBoarding) Capture(u, boarder);
			else if (interior->Step(dt, Seconds, rng)) Capture(u, boarder);
		}
		for (const auto& u : Units)
		{
			if (u->Latched == nullptr) continue;
			auto t = u->Latched;
			if (!t->Present() || t->Captured || t->Combat->Interior->Boarder == nullptr)
			{
				// Done (taken, repelled or gone): cast off.
				u->Latched = nullptr;
				u->Vel = u->Vel + (u->Pos - t->Pos).Normalized() * 0.01;
			}
		}
	}

	void Battle::KeepOrdersCommanded(const cs::Ref<BattleUnit>& u, const cs::Ref<Interior>& interior)
	{
		if (interior->DefenderOrder != BoardingOrder::Auto && !(u->PlayerControlled() && u->Present()))
			interior->AutoResolve(BoardingRole::Defenders);
		if (interior->BoarderOrder == BoardingOrder::Auto) return;
		for (const auto& o : Units)
			if (o->PlayerControlled() && o->Present() && !o->Captured && interior->IsParty(o->Faction)) return;
		interior->AutoResolve(BoardingRole::Boarders);
	}

	void Battle::Capture(const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor)
	{
		int side = -1;
		for (const auto& o : Units)
			if (o->Faction == captor && o->Side != u->Side) side = o->Side;
		if (side < 0) side = 1 - u->Side;
		if (!u->Captured) u->TakenFromSide = u->Side;
		u->Captured = true;
		u->Side = side;
		u->Fleeing = false;
		u->Target = nullptr;
		u->BoardTarget = nullptr;
		// Taken while still hailing: there are no terms left to settle.
		u->Surrendering = false;
		// Its escorts no longer keep station on it (it fights for the other side now), nor it on its old flagship.
		u->Leader = nullptr;
		for (const auto& o : Units)
		{
			if (o->Leader == u) o->Leader = nullptr;
			// Nobody keeps steering on it as it was: its captors' side re-picks now rather than circling its own prize.
			if (o->Target == u) o->Target = nullptr;
		}
		// A surrendered hull is taken without a fight aboard: mark it held so the fight stops there too.
		u->Combat->Interior->Captured = true;
		u->Combat->Interior->Settle();
		Emit(FxKind::Captured, u->Pos, u->Pos, u->LengthKm, side, u, -1);
		if (UnitCaptured) UnitCaptured(shared_from_this(), u, captor);
	}

	// ---------------------------------------------------------------- surrender

	bool Battle::AwaitingSurrenderTerms() const
	{
		for (const auto& u : Units)
			if (u->Present() && u->Surrendering && !u->SurrenderOutcome.has_value()) return true;
		return false;
	}

	void Battle::TallySideStrength()
	{
		sideStrength[0] = sideStrength[1] = 0;
		for (const auto& u : Units)
			if (u->Present() && !u->Captured && !u->Surrendering && u->Side >= 0 && u->Side < 2)
				sideStrength[u->Side] += u->Combat->Strength();
	}

	double Battle::OutgunnedOf(const cs::Ref<BattleUnit>& u)
	{
		return u->Side < 0 || u->Side > 1 ? 0 : Morale::Outgunned(sideStrength[u->Side], sideStrength[1 - u->Side]);
	}

	double Battle::DamageTollOf(const cs::Ref<BattleUnit>& u)
	{
		auto c = u->Combat;
		double shield = c->Spec->Shield > 0 ? c->Shield() / c->Spec->Shield : 1;
		return Morale::DamageToll(c->HullFraction(), shield);
	}

	bool Battle::StrikesColours(const cs::Ref<BattleUnit>& u)
	{
		// C# evaluates the arguments left to right; sequenced here, since C++ leaves their order unspecified.
		double toll = DamageTollOf(u);
		double outgunned = OutgunnedOf(u);
		double resolve = Morale::Resolve(u->Faction, u->Ship != nullptr && u->Ship->Spec->IsMilitary) * (u->Side >= 0 && u->Side < 2 ? Dread[u->Side] : 1) * Morale::SupplyResolve(u->Combat->Readiness);
		return Morale::StrikesColours(toll, outgunned, resolve, u->MoraleThreshold);
	}

	void Battle::Surrender(const cs::Ref<BattleUnit>& u)
	{
		if (u->Surrendering || !u->Present() || u->Captured) return;
		u->Surrendering = true;
		u->SurrenderTimer = 0;
		u->Fleeing = false;
		u->Target = nullptr;
		u->BoardTarget = nullptr;
		for (const auto& o : Units)
		{
			if (o->Target == u) o->Target = nullptr;
			if (o->BoardTarget == u && !u->SurrenderOpenToBoarding) o->BoardTarget = nullptr;
		}
		Emit(FxKind::Hail, u->Pos, u->Pos, u->LengthKm, u->Side, u, -1);
		if (UnitSurrendered) UnitSurrendered(shared_from_this(), u);
	}

	cs::Ref<Faction> Battle::CaptorOf(int side) const { return side < 0 || side > 1 ? nullptr : SideLeaders[1 - side]; }

	bool Battle::CaptorCanBoard(const cs::Ref<BattleUnit>& u)
	{
		for (const auto& o : Units)
			// A player-flown hull never flies a boarding approach (see Think), so it can't be the one to take it.
			if (Hostile(u, o) && !o->Captured && !o->Surrendering && !o->Combat->Disabled() && !o->PlayerControlled() && CanBoard(o) && MayBoard(o, u)) return true;
		return false;
	}

	bool Battle::CaptorPlayerPresent(const cs::Ref<BattleUnit>& u)
	{
		for (const auto& o : Units)
			if (Hostile(u, o) && o->PlayerControlled()) return true;
		return false;
	}

	cs::Ref<BattleUnit> Battle::AwaitingTermsFrom(int captorSide)
	{
		for (const auto& u : Units)
			if (u->Present() && u->Surrendering && !u->SurrenderOutcome.has_value() && u->Side != captorSide) return u;
		return nullptr;
	}

	bool Battle::OfferSurrenderTerms(const cs::Ref<BattleUnit>& u, SurrenderTerms terms)
	{
		if (u == nullptr || !u->Present() || !u->Surrendering || u->SurrenderOutcome.has_value()) return false;
		if (terms == SurrenderTerms::TakeShip && !CaptorCanBoard(u)) return false;
		Settle(u, CaptorOf(u->Side), terms);
		return true;
	}

	void Battle::Settle(const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor, SurrenderTerms terms)
	{
		u->SurrenderOutcome = terms;
		if (terms == SurrenderTerms::TakeShip) u->SurrenderOpenToBoarding = true;
		if (terms == SurrenderTerms::RansomCargo && captor != nullptr && captor->IsPirate && (u->Side == 0 || u->Side == 1)) Robbed[1 - u->Side] = true;
		if (SurrenderDecided) SurrenderDecided(shared_from_this(), u, captor, terms);
		// Released: the hull powers up and leaves the field.
		if (terms != SurrenderTerms::TakeShip) u->Fled = true;
	}

	void Battle::StepSurrenders(double dt)
	{
		for (const auto& u : Units)
		{
			if (!u->Present()) continue;
			if (u->CargoDemandedBy != nullptr)
			{
				u->CargoDemandTimer += dt;
				// US-T203: an AI hull stalling for help complies after all once that help is no longer due in time.
				if (!u->PlayerControlled() && !HoldsOutForHelp(u)) AnswerCargoDemand(u, true);
				else if (u->CargoDemandTimer > SurrenderDecisionSeconds) AnswerCargoDemand(u, false);
			}
			if (!u->Surrendering || u->SurrenderOutcome.has_value()) continue;
			u->SurrenderTimer += dt;
			double wait = CaptorPlayerPresent(u) ? SurrenderDecisionSeconds : SurrenderAiDecisionSeconds;
			if (u->SurrenderTimer < wait) continue;
			auto captor = CaptorOf(u->Side);
			if (captor == nullptr) { Settle(u, nullptr, SurrenderTerms::LetGo); continue; }
			bool warship = u->Ship != nullptr && u->Ship->Spec->IsMilitary;
			bool cargo = u->Ship != nullptr && u->Ship->HasCargo;
			// CaptorCanBoard is evaluated before the draw, as C# evaluates arguments left to right.
			bool canBoard = CaptorCanBoard(u);
			double roll = rng.NextDouble();
			Settle(u, captor, Morale::ChooseTerms(captor->IsPirate, warship, cargo, canBoard, roll));
		}
	}

	void Battle::MaybeDemandCargo(const cs::Ref<BattleUnit>& pirate)
	{
		for (const auto& v : Units)
		{
			if (!Hostile(pirate, v) || v->CargoDemandMade || v->Surrendering || !v->Present() || v->Captured) continue;
			if (v->Ship == nullptr || !v->Ship->HasCargo) continue;
			if (v->PlayerControlled())
			{
				if (!v->Combat->Disabled() && !Morale::ShouldDemandCargo(OutgunnedOf(v), DamageTollOf(v))) continue;
			}
			else if (!RaidDoctrine::Robs(pirate->Faction, v->Ship) || !Stripped(v)) continue;
			v->CargoDemandMade = true;
			v->CargoDemandedBy = pirate->Faction;
			v->CargoDemandTimer = 0;
			Emit(FxKind::Hail, pirate->Pos, v->Pos, pirate->LengthKm, pirate->Side, pirate, -1);
			if (CargoDemanded) CargoDemanded(shared_from_this(), v, pirate->Faction);
			if (!v->PlayerControlled() && !HoldsOutForHelp(v)) AnswerCargoDemand(v, true);
		}
	}

	bool Battle::HoldsOutForHelp(const cs::Ref<BattleUnit>& v)
	{
		if (!HelpDueS || v->Combat == nullptr) return false;
		double due = HelpDueS(shared_from_this(), v);
		return due >= 0 && due <= MaxHoldOutS * v->Combat->HullFraction();
	}

	bool Battle::Stripped(const cs::Ref<BattleUnit>& v)
	{
		auto c = v->Combat;
		if (c->Disabled() || v->Stunned > 0) return true;
		bool shieldsDown = c->Spec->Shield <= 0 || c->Shield() <= c->Spec->Shield * 0.05;
		return shieldsDown && OutgunnedOf(v) >= 0.3;
	}

	bool Battle::AnswerCargoDemand(const cs::Ref<BattleUnit>& victim, bool comply)
	{
		if (victim == nullptr || victim->CargoDemandedBy == nullptr) return false;
		auto pirates = victim->CargoDemandedBy;
		victim->CargoDemandedBy = nullptr;
		if (!comply) victim->CargoRefused = true;
		if (!comply || !victim->Present()) return true;
		victim->SurrenderOutcome = SurrenderTerms::RansomCargo;
		if (victim->Side == 0 || victim->Side == 1) Robbed[1 - victim->Side] = true;
		if (SurrenderDecided) SurrenderDecided(shared_from_this(), victim, pirates, SurrenderTerms::RansomCargo);
		victim->Fled = true;
		return true;
	}

	bool Battle::Spares(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o)
	{
		return !u->PlayerControlled() && !o->CargoRefused && RaidDoctrine::Robs(u->Faction, o->Ship);
	}

	bool Battle::HoldsFireOn(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o) { return o->Combat->Disabled() && Spares(u, o); }

	bool Battle::RaidDone(const cs::Ref<BattleUnit>& u)
	{
		if (u->Side != 0 && u->Side != 1) return false;
		bool armedFoe = false, prey = false;
		for (const auto& o : Units)
		{
			if (!o->Present() || o->Captured || !Hostile(u, o)) continue;
			if (o->Ship == nullptr || !RaidDoctrine::Robs(u->Faction, o->Ship))
			{
				if (!o->Surrendering && !o->Combat->Disabled() && (o->Combat->Armed() || CanBoard(o))) armedFoe = true;
				continue;
			}
			prey = true;
			bool stillToTake = (o->Ship->HasCargo && !o->SurrenderOutcome.has_value()) || o->CargoRefused || IsPrize(u, o) ||
				(o->Surrendering && !o->SurrenderOutcome.has_value()) || o->SurrenderOpenToBoarding || SideCanBoard(u->Side);
			if (stillToTake) return false;
		}
		if (!prey && !Robbed[u->Side]) return false;
		return Robbed[u->Side] || !armedFoe;
	}

	double Battle::SegmentPointDistance(DVec3 a, DVec3 b, DVec3 p)
	{
		auto d = b - a;
		double len2 = d.LengthSquared();
		double t = len2 > 0 ? cs::Math::Max(0.0, cs::Math::Min(1.0, DVec3::Dot(p - a, d) / len2)) : 0;
		return (a + d * t - p).Length();
	}

	void Battle::Emit(FxKind kind, DVec3 a, DVec3 b, double size, int side, const cs::Ref<BattleUnit>& unit, int sub)
	{
		CombatFx fx;
		fx.Seq = ++FxSeq;
		fx.Kind = kind;
		fx.A = a;
		fx.B = b;
		fx.Size = size;
		fx.Side = side;
		fx.Unit = unit;
		fx.Sub = sub;
		Fx.Add(fx);
		// A shot fired or a round shot down raises its weapon's sound cue (US-T189), if anything is listening.
		if (WeaponSystems::SoundHook()) WeaponSystems::CueFx(fx, Id);
	}

	// ---------------------------------------------------------------- placement

	double Battle::AnchorDistanceKm(const cs::Ref<BodyDefinition>& body)
	{
		double displayRadius = ScaleModel::GameplayRadius(body->RadiusKm);
		double wanted = displayRadius + 900;
		double lo = body->RadiusKm, hi = body->RadiusKm * 400;
		for (int i = 0; i < 60; i++)
		{
			double mid = 0.5 * (lo + hi);
			if (ScaleModel::GameplayLocalDistance(body, mid) < wanted) lo = mid;
			else hi = mid;
		}
		return cs::Math::Max(hi, body->RadiusKm * 6);
	}
}
