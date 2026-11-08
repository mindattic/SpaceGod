// US-T83: the walking mech (see MechWalker.h).

#include "Embodiment/MechWalker.h"
#include "Astro.h"
#include "Combat/Battle.h"
#include "Combat/Collisions.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Gunnery.h" // US-T270: rounds crossing it, hulls in the way
#include "Embodiment/RepairWorker.h" // TeamsOn: a hull with EVA crews has a mech bay

#include <cstdint>

namespace SpaceGod::Core
{
	namespace
	{
		/// US-T270: a hash of (a, b) to two numbers in -1..1, for a hostile's spread: fixed by who fired and which round it
		/// was, never drawn from the battle's generator, so co-op, replays and the Bench step alike.
		void Spread2(uint64_t a, uint64_t b, double& x, double& y)
		{
			uint64_t z = a * 0x9E3779B97F4A7C15ull + b + 0x632BE59BD9B4E019ull;
			auto mix = [](uint64_t v)
			{
				v = (v ^ (v >> 30)) * 0xBF58476D1CE4E5B9ull;
				v = (v ^ (v >> 27)) * 0x94D049BB133111EBull;
				return v ^ (v >> 31);
			};
			uint64_t h1 = mix(z), h2 = mix(z ^ 0xD1B54A32D192ED03ull);
			x = static_cast<double>(h1 >> 11) / static_cast<double>(1ull << 53) * 2 - 1;
			y = static_cast<double>(h2 >> 11) / static_cast<double>(1ull << 53) * 2 - 1;
		}

		DVec3 PerpendicularTo(DVec3 v)
		{
			auto a = cs::Math::Abs(v.Y) < 0.9 ? DVec3(0, 1, 0) : DVec3(1, 0, 0);
			auto p = a - v * DVec3::Dot(a, v);
			return p.LengthSquared() > 1e-12 ? p.Normalized() : DVec3(0, 0, 1);
		}

		/// <paramref name="v"/> laid flat on the plane square to <paramref name="n"/>, unit; <paramref name="fallback"/> when it
		/// lies along <paramref name="n"/>.
		DVec3 Flatten(DVec3 v, DVec3 n, DVec3 fallback)
		{
			auto f = v - n * DVec3::Dot(v, n);
			return f.LengthSquared() > 1e-12 ? f.Normalized() : fallback;
		}

		double MoveToward(double value, double target, double step)
		{
			if (value < target) return cs::Math::Min(target, value + step);
			return cs::Math::Max(target, value - step);
		}

		double Rad(double deg) { return deg * Astro::Deg2Rad; }
		double Deg(double rad) { return rad / Astro::Deg2Rad; }
	}

	MechWalker::MechWalker(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& carrier)
	{
		Battle = battle;
		Carrier = carrier;
		Side = carrier != nullptr ? carrier->Side : 0;
		Callsign = carrier != nullptr ? cs::Cat(carrier->Name(), " mech") : cs::String("mech");
		Armor = MaxArmor;
		JetFuel = JetFuelS;
		CannonAmmo = MaxCannonAmmo;
		Missiles = MaxMissiles;
	}

	bool MechWalker::CarriesMech(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Present() && !u->Captured && RepairWorker::TeamsOn(u) > 0;
	}

	bool MechWalker::Standable(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Present() && u->Kind != UnitKind::Fighter && u->LengthKm >= MinHullKm && u->Combat != nullptr && u->Combat->Spec != nullptr;
	}

	cs::Ref<MechWalker> MechWalker::Deploy(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& carrier, cs::String& why)
	{
		why = cs::String();
		if (battle == nullptr || carrier == nullptr) why = "not in a fight";
		else if (battle->Over) why = "the fight is over";
		else if (!CarriesMech(carrier)) why = "no mech bay aboard (capital ships and stations carry one)";
		if (!why.empty()) return nullptr;
		// One mech to a bay (US-T270): the one out if it still stands; none more once it is wrecked.
		for (const auto& m : battle->Mechs)
		{
			if (m == nullptr || m->Carrier != carrier) continue;
			if (m->Lost.empty()) return m;
			why = cs::Cat("its mech was lost in this fight (", m->Lost, ")");
			return nullptr;
		}
		auto m = cs::New<MechWalker>(battle, carrier);
		m->PlaceOnCarrier();
		m->Note("Out of the bay: mag boots on the plating. Space lights the jump jets");
		battle->Mechs.Add(m);
		return m;
	}

	cs::List<cs::Ref<MechWalker>> MechWalker::DeployedIn(const cs::Ref<Core::Battle>& battle)
	{
		cs::List<cs::Ref<MechWalker>> list;
		if (battle == nullptr || battle->Over) return list;
		for (const auto& m : battle->Mechs)
			if (m != nullptr && m->Lost.empty()) list.Add(m);
		return list;
	}

	DVec3 MechWalker::Plating(const cs::Ref<CombatSpec>& spec, DVec3 local)
	{
		// The collision capsule (Collisions: a hull a length long, its beam the capsule's diameter): the nearest point of its
		// surface, so the boots walk its length, not slide to an end cap as its farthest-point-along a direction would.
		double r = spec != nullptr ? spec->BeamFraction * 0.5 : 0.1;
		double core = cs::Math::Max(0.0, 0.5 - r);
		auto onAxis = DVec3(0, 0, cs::Math::Clamp(local.Z, -core, core));
		auto out = local - onAxis;
		out = out.LengthSquared() > 1e-18 ? out.Normalized() : DVec3(0, 1, 0);
		return onAxis + out * r;
	}

	DVec3 MechWalker::PlatingNormal(const cs::Ref<CombatSpec>& spec, DVec3 local)
	{
		double r = spec != nullptr ? spec->BeamFraction * 0.5 : 0.1;
		double core = cs::Math::Max(0.0, 0.5 - r);
		auto out = local - DVec3(0, 0, cs::Math::Clamp(local.Z, -core, core));
		return out.LengthSquared() > 1e-18 ? out.Normalized() : DVec3(0, 1, 0);
	}

	DVec3 MechWalker::RoofLocal() const
	{
		return PlatingNormal(Hull != nullptr && Hull->Combat != nullptr ? Hull->Combat->Spec : nullptr, Local);
	}

	void MechWalker::PlaceOnCarrier()
	{
		auto spec = Carrier->Combat != nullptr ? Carrier->Combat->Spec : nullptr;
		// The dorsal plating a little aft of amidships (the workers' airlock is amidships), facing the bow.
		auto dir = DVec3(0, 1, -0.15);
		Hull = Carrier;
		Local = Plating(spec, dir.Normalized() * 0.5);
		auto n = PlatingNormal(spec, Local);
		Legs = Flatten(DVec3(0, 0, 1), n, PerpendicularTo(n));
		Speed = 0;
		Pos = Hull->PointLocal(Local);
		Vel = Hull->Vel;
		Facing = Hull->LocalDirToWorld(Legs).Normalized();
		Up = Hull->LocalDirToWorld(n).Normalized();
		AimDir = Facing;
		SinceLanding = 0;
	}

	cs::Ref<Core::Faction> MechWalker::Faction() const { return Carrier != nullptr ? Carrier->Faction : nullptr; }

	cs::String MechWalker::Blocker() const
	{
		if (!Lost.empty()) return Lost;
		if (Battle == nullptr || Battle->Over) return "back in its bay: the fight is over";
		return cs::String();
	}

	EmbodimentPose MechWalker::PoseAt(double /*jd*/)
	{
		return EmbodimentPose(Battle != nullptr ? Battle->Body : 0, Pos + (Battle != nullptr ? Battle->Anchor : DVec3::Zero), Facing, Up);
	}

	void MechWalker::TakeCharge()
	{
		held = true;
		secondaryWas = true; // a right button held through the switch is not a salvo
	}

	void MechWalker::HandBack()
	{
		held = false;
		JetsLit = false;
		salvoLeft = 0;
		// Nobody at its controls: the legs stop where they stand (in flight it coasts on).
		Speed = 0;
	}

	void MechWalker::Note(cs::String what)
	{
		Event = std::move(what);
		EventS = 3.5;
	}

	void MechWalker::Control(const EmbodimentInput& input, double dt)
	{
		if (Battle == nullptr || !Lost.empty()) return;
		dt = cs::Math::Max(0.0, dt);
		EventS = cs::Math::Max(0.0, EventS - dt);
		SinceLanding += dt;
		SinceHit += dt;
		liftGrace = cs::Math::Max(0.0, liftGrace - dt);
		for (double& f : MuzzleFlash) f = cs::Math::Max(0.0, f - dt);
		// The hull under its boots gone (destroyed, fled, docked): thrown clear, coasting on the hull's way.
		if (Hull != nullptr && !Hull->Present())
		{
			Hull = nullptr;
			liftGrace = LiftGraceS;
			Note("The hull went out from under you: thrown clear");
		}
		if (input.Aim.LengthSquared() > 1e-12) AimDir = input.Aim.Normalized();
		if (Hull != nullptr) Walk(input, dt);
		else Fly(input, dt);
		if (!Lost.empty()) return;
		Weapons(input, dt);
	}

	void MechWalker::Walk(const EmbodimentInput& input, double dt)
	{
		auto spec = Hull->Combat != nullptr ? Hull->Combat->Spec : nullptr;
		auto n = PlatingNormal(spec, Local);
		Legs = Flatten(Legs, n, PerpendicularTo(n));
		// The hull frame's right (+X) against its up and forward is roof x forward: BattleUnit::Basis's world right is
		// forward x roof, and the frame's axes map onto the world's with a mirror.
		auto right = DVec3::Cross(n, Legs);

		// The legs: A/D turn them, slower the faster they go; W/S throttle with the weight of the thing.
		double maxFwd = MaxForwardKmS * (input.Boost ? RunFactor : 1.0);
		double frac = cs::Math::Clamp(cs::Math::Abs(Speed) / MaxForwardKmS, 0.0, 1.0);
		double turnDeg = input.StrafeRight * (TurnRateStillDeg + (TurnRateFullDeg - TurnRateStillDeg) * frac) * dt;
		if (turnDeg != 0)
		{
			double a = Rad(turnDeg);
			Legs = (Legs * cs::Math::Cos(a) + right * cs::Math::Sin(a)).Normalized();
			right = DVec3::Cross(n, Legs);
		}
		double target = input.Throttle >= 0 ? input.Throttle * maxFwd : input.Throttle * MaxReverseKmS;
		bool building = cs::Math::Abs(target) > cs::Math::Abs(Speed) && (Speed == 0 || (target > 0) == (Speed > 0));
		Speed = MoveToward(Speed, target, (building ? AccelKmS2 : DecelKmS2) * dt);
		if (Speed != 0 && dt > 0)
		{
			auto next = Local + Legs * (Speed * dt / cs::Math::Max(Hull->LengthKm, 1e-6));
			Local = Plating(spec, next);
			n = PlatingNormal(spec, Local);
			Legs = Flatten(Legs, n, PerpendicularTo(n));
			right = DVec3::Cross(n, Legs);
		}
		// The gait: a stride a StrideKm walked, and a slower shuffle turning on the spot.
		double before = GaitPhase;
		GaitPhase += (cs::Math::Abs(Speed) * dt) / StrideKm + cs::Math::Abs(turnDeg) / 90.0;
		if (cs::Math::Floor(GaitPhase * 2) > cs::Math::Floor(before * 2)) Footfalls++;

		// The torso twists after the aim, within its limit off the legs; the arms pitch after it off the plating.
		auto aim = Hull->DirToLocal(AimDir);
		aim = aim.LengthSquared() > 1e-12 ? aim.Normalized() : Legs;
		auto flat = Flatten(aim, n, Legs);
		double wantYaw = cs::Math::Clamp(Deg(cs::Math::Atan2(DVec3::Dot(flat, right), DVec3::Dot(flat, Legs))), -TorsoTwistLimitDeg, TorsoTwistLimitDeg);
		double wantPitch = cs::Math::Clamp(Deg(cs::Math::Asin(cs::Math::Clamp(DVec3::Dot(aim, n), -1.0, 1.0))), AimPitchMinDeg, AimPitchMaxDeg);
		TorsoYawDeg = MoveToward(TorsoYawDeg, wantYaw, TorsoTwistRateDeg * dt);
		AimPitchDeg = MoveToward(AimPitchDeg, wantPitch, AimPitchRateDeg * dt);
		double y = Rad(TorsoYawDeg), p = Rad(AimPitchDeg);
		auto gun = (Legs * cs::Math::Cos(y) + right * cs::Math::Sin(y)) * cs::Math::Cos(p) + n * cs::Math::Sin(p);
		AimDir = Hull->LocalDirToWorld(gun).Normalized();

		Pos = Hull->PointLocal(Local);
		Vel = Hull->Vel;
		Facing = Hull->LocalDirToWorld(Legs).Normalized();
		Up = Hull->LocalDirToWorld(n).Normalized();
		JetsLit = false;
		JetFuel = cs::Math::Min(JetFuelS, JetFuel + JetRefillPerS * dt);
		if (input.JumpJets)
		{
			if (JetFuel > 0) LiftOff();
			else if (EventS <= 0) Note("JETS DRY: the tank refills while you stand on a hull");
		}
	}

	void MechWalker::LiftOff()
	{
		auto from = Hull;
		Hull = nullptr;
		Vel = from->Vel + Up * LiftKickKmS;
		Pos = Pos + Up * (HeightKm * 0.2);
		Speed = 0;
		liftGrace = LiftGraceS;
		JetsLit = true;
		Note(cs::Cat("JUMP JETS: off the ", from->Name(), "'s plating"));
	}

	void MechWalker::Fly(const EmbodimentInput& input, double dt)
	{
		// The roof stays square to the look; the legs hang under the torso.
		Up = Flatten(Up, AimDir, PerpendicularTo(AimDir));
		Facing = AimDir;
		TorsoYawDeg = MoveToward(TorsoYawDeg, 0, TorsoTwistRateDeg * dt);
		AimPitchDeg = MoveToward(AimPitchDeg, 0, AimPitchRateDeg * dt);
		Speed = 0;
		JetsLit = input.JumpJets && JetFuel > 0 && dt > 0;
		if (JetsLit)
		{
			auto right = DVec3::Cross(AimDir, Up);
			auto push = AimDir * input.Throttle + right * input.StrafeRight + Up * input.StrafeUp;
			// No stick: straight along the look.
			push = push.LengthSquared() > 1e-9 ? push.Normalized() : AimDir;
			// Shift: the jets at full, twice the push for twice the burn.
			double burn = cs::Math::Min(JetFuel, dt * (input.Boost ? 2.0 : 1.0));
			Vel = Vel + push * (JetAccelKmS2 * burn);
			JetFuel -= burn;
			if (JetFuel <= 0)
			{
				JetFuel = 0;
				Note("JETS DRY: coasting; set down on a hull to refill");
			}
		}
		else if (input.JumpJets && JetFuel <= 0 && EventS <= 0) Note("JETS DRY: coasting; set down on a hull to refill");
		Pos = Pos + Vel * dt;
		if (liftGrace > 0) return;
		// Mag boots: within a stride of any hull's plating, coasting onto it (the jets let go), they catch.
		for (const auto& u : Battle->Units)
		{
			if (!Standable(u)) continue;
			auto local = u->ToLocal(Pos);
			double r = local.Length();
			if (r > 1.0) continue; // nowhere near: a hull is within half a length of its centre
			auto surface = Plating(u->Combat->Spec, local);
			double clearance = HeightKm * 0.6 / u->LengthKm;
			// Inside the capsule (its axis nearer than its radius) or within a stride of its plating.
			double out = DVec3::Dot(local - surface, PlatingNormal(u->Combat->Spec, local));
			bool inside = out <= 0;
			if (!inside && (JetsLit || out > clearance)) continue;
			SetDown(u, surface);
			return;
		}
	}

	void MechWalker::SetDown(const cs::Ref<BattleUnit>& u, DVec3 local)
	{
		double rel = (Vel - u->Vel).Length();
		if (rel > CrashKmS)
		{
			Lost = cs::Cat("wrecked: slammed into the ", u->Name(), " at ", cs::Fmt(rel * 1000, "N0"), " m/s");
			Armor = 0;
			Battle->Emit(FxKind::Impact, Pos, Pos, 0.01, Side, nullptr, -1);
			return;
		}
		Hull = u;
		Local = local;
		auto n = PlatingNormal(u->Combat != nullptr ? u->Combat->Spec : nullptr, Local);
		Legs = Flatten(u->DirToLocal(Facing), n, PerpendicularTo(n));
		Speed = 0;
		TorsoYawDeg = AimPitchDeg = 0;
		Pos = u->PointLocal(Local);
		Vel = u->Vel;
		Up = u->LocalDirToWorld(n).Normalized();
		Facing = u->LocalDirToWorld(Legs).Normalized();
		JetsLit = false;
		SinceLanding = 0;
		Landings++;
		if (rel > LandSafeKmS)
		{
			double dent = (rel - LandSafeKmS) * HardLandingArmorPerKmS;
			Armor = cs::Math::Max(1.0, Armor - dent);
			Note(cs::Cat("HARD LANDING on the ", u->Name(), " at ", cs::Fmt(rel * 1000, "N0"), " m/s: ", cs::Fmt(dent, "N0"), " armour lost"));
		}
		else Note(cs::Cat("Boots down on the ", u->Name(), u->Side == Side ? cs::String("") : cs::String(": an enemy hull under your feet")));
	}

	void MechWalker::Weapons(const EmbodimentInput& input, double dt)
	{
		bool press = input.Secondary && !secondaryWas;
		secondaryWas = input.Secondary;
		Heat = cs::Math::Max(0.0, Heat - HeatDissipation * dt);
		if (Overheated && Heat <= CoolResumeHeat)
		{
			Overheated = false;
			Note("Heat down: weapons back on line");
		}
		cannonCooldown = cs::Math::Max(-1.0 / CannonRate, cannonCooldown - dt);
		missileCooldown = cs::Math::Max(0.0, missileCooldown - dt);
		if (input.Primary && !Overheated && CannonAmmo > 0)
		{
			while (cannonCooldown <= 0 && CannonAmmo > 0 && !Overheated)
			{
				FireCannon();
				cannonCooldown += 1.0 / CannonRate;
			}
		}
		else if (input.Primary && CannonAmmo <= 0 && EventS <= 0) Note("Autocannons dry: rearmed when you go back aboard");
		if (cannonCooldown < 0 && !input.Primary) cannonCooldown = 0;
		if (press)
		{
			if (Overheated) Note("OVERHEATED: the pods are locked out until the heat comes down");
			else if (Missiles <= 0) Note("Missile pods empty");
			else if (salvoLeft > 0 || missileCooldown > 0) Note("Pods reloading");
			else
			{
				salvoLeft = cs::Math::Min(SalvoSize, Missiles);
				salvoTimer = 0;
				salvoTarget = TargetNearAim();
				Heat += MissileSalvoHeat;
				missileCooldown = MissileCooldownS;
				if (Heat >= 100)
				{
					Heat = 100;
					Overheated = true;
				}
			}
		}
		if (salvoLeft > 0)
		{
			salvoTimer -= dt;
			while (salvoLeft > 0 && salvoTimer <= 0)
			{
				LaunchMissile();
				salvoLeft--;
				salvoTimer += SalvoIntervalS;
			}
		}
	}

	DVec3 MechWalker::MuzzleAt(int arm) const
	{
		auto right = DVec3::Cross(AimDir, Up);
		right = right.LengthSquared() > 1e-12 ? right.Normalized() : PerpendicularTo(AimDir);
		// The arms' guns hang at the shoulders' height, a little over a metre out to each side, ahead of the torso.
		return Pos + Up * (HeightKm * 0.62) + right * (HeightKm * (arm == 0 ? -0.2 : 0.2)) + AimDir * (HeightKm * 0.35);
	}

	void MechWalker::FireCannon()
	{
		int arm = nextArm;
		nextArm ^= 1;
		auto from = MuzzleAt(arm);
		auto r = cs::New<KineticRound>();
		r->Pos = from;
		r->Vel = Vel + AimDir * CannonMuzzleKmS;
		r->Damage = CannonDamage;
		r->Life = CannonRangeKm / CannonMuzzleKmS;
		r->Side = Side;
		r->Tracer = true;
		r->Aimed = true;
		r->SmallCraft = true; // it hits a capital's weak points as a fighter's guns do
		Battle->Rounds.Add(r);
		Battle->Emit(FxKind::Guns, from, from + AimDir * CannonRangeKm, 0, Side, nullptr, -1);
		CannonAmmo--;
		ShellsFired++;
		MuzzleFlash[arm] = 0.06;
		Heat += CannonHeat;
		if (Heat >= 100)
		{
			Heat = 100;
			Overheated = true;
			Note("OVERHEATED: weapons locked out until the heat comes down");
		}
	}

	cs::Ref<BattleUnit> MechWalker::TargetNearAim() const
	{
		cs::Ref<BattleUnit> best = nullptr;
		double bestScore = cs::Double::MaxValue;
		double cone = cs::Math::Cos(Rad(MissileConeDeg));
		for (const auto& u : Battle->Units)
		{
			if (u->Side == Side || !u->Present() || u->Surrendering) continue;
			auto to = u->Pos - Pos;
			double d = to.Length();
			if (d > MissileRangeKm || d < 1e-9) continue;
			double c = DVec3::Dot(to / d, AimDir);
			if (c < cone) continue;
			// Nearest the crosshair first, then nearest.
			double score = (1 - c) * 1000 + d;
			if (score < bestScore)
			{
				bestScore = score;
				best = u;
			}
		}
		return best;
	}

	void MechWalker::LaunchMissile()
	{
		if (Missiles <= 0) return;
		int tube = MissilesFired % SalvoSize;
		auto right = DVec3::Cross(AimDir, Up);
		right = right.LengthSquared() > 1e-12 ? right.Normalized() : PerpendicularTo(AimDir);
		// Off the shoulder pods: two tubes each side, high on the torso.
		auto from = Pos + Up * (HeightKm * (tube < 2 ? 0.8 : 0.86)) + right * (HeightKm * ((tube % 2) == 0 ? -0.3 : 0.3));
		auto m = cs::New<Missile>();
		m->Pos = from;
		m->Damage = MissileDamage;
		m->Speed = MissileSpeedKmS;
		m->Side = Side;
		m->SmallCraft = true;
		bool homing = salvoTarget != nullptr && salvoTarget->Present() && salvoTarget->Side != Side;
		if (homing)
		{
			m->Vel = Vel + (AimDir + Up * 0.25).Normalized() * (MissileSpeedKmS * 0.4);
			m->Target = salvoTarget;
			m->Hp = 12;
			m->Life = MissileRangeKm / MissileSpeedKmS * 1.6;
			m->Kind = MissileKind::Missile;
		}
		else
		{
			m->Vel = Vel + AimDir * MissileSpeedKmS;
			m->Hp = 8;
			m->Life = MissileRangeKm / MissileSpeedKmS;
			m->Kind = MissileKind::Rocket;
		}
		Battle->Missiles.Add(m);
		Battle->Emit(FxKind::MissileLaunch, from, from + AimDir, 0, Side, nullptr, -1);
		Missiles--;
		MissilesFired++;
	}

	cs::Ref<BattleUnit> MechWalker::Anchor() const
	{
		if (Hull != nullptr && Hull->Present()) return Hull;
		if (Carrier != nullptr && Carrier->Present()) return Carrier;
		if (Battle == nullptr) return Carrier;
		cs::Ref<BattleUnit> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& u : Battle->Units)
		{
			if (!u->Present() || u->Side != Side) continue;
			double d = (u->Pos - Pos).LengthSquared();
			if (d < bestD)
			{
				bestD = d;
				best = u;
			}
		}
		return best != nullptr ? best : Carrier;
	}

	// ---------------------------------------------------------------- US-T270: under fire

	DVec3 MechWalker::Centre() const { return Pos + Up * (HeightKm * 0.5); }

	bool MechWalker::ThreatOf(const cs::Ref<BattleUnit>& u, double& rate, double& damage, double& rangeKm, double& muzzleKmS, double& spread)
	{
		if (u == nullptr || !u->Present() || u->Surrendering || u->Captured || u->Stunned > 0 || u->Combat == nullptr || u->Combat->Disabled()) return false;
		if (u->Kind == UnitKind::Fighter)
		{
			// A small craft's guns: close in, quick, light.
			rate = 2.0; damage = 5; rangeKm = 2.0; muzzleKmS = 3.0; spread = 0.004;
		}
		else if (u->Kind == UnitKind::Platform)
		{
			// A station's point defence: far, steady.
			rate = 2.5; damage = 12; rangeKm = 5.0; muzzleKmS = 3.0; spread = 0.0035;
		}
		else if (u->LengthKm < 0.15)
		{
			// A light hull's turrets.
			rate = 1.5; damage = 8; rangeKm = 3.0; muzzleKmS = 3.0; spread = 0.004;
		}
		else
		{
			// A capital's point defence turned on it.
			rate = 2.5; damage = 9; rangeKm = 4.0; muzzleKmS = 3.0; spread = 0.0035;
		}
		return true;
	}

	void MechWalker::StepIn(const cs::Ref<Core::Battle>& battle, double dt)
	{
		if (battle == nullptr || battle->Mechs.Count() == 0 || dt <= 0) return;
		// A copy: a step must not see the list change under it.
		auto mechs = battle->Mechs;
		for (const auto& m : mechs)
		{
			if (m == nullptr || !m->Lost.empty() || m->Battle != battle) continue;
			// Nobody at its controls: it stands where it is (the legs coming to a stop) or coasts on, its boots catching as ever.
			if (!m->held) m->Control(EmbodimentInput{}, dt);
			if (!m->Lost.empty()) continue;
			for (const auto& u : battle->Units)
			{
				if (u == nullptr || u->Side == m->Side || u == m->Hull) continue; // the hull under its boots can't bear on its own plating
				m->EngagedBy(u, dt);
			}
		}
		// Every hostile aimed round that will cross a standing mech this step, before any other hostile hull, strikes it.
		for (const auto& r : battle->Rounds)
		{
			if (r == nullptr || r->Gone || !r->Aimed) continue;
			auto step = r->Vel * dt;
			double len = step.Length();
			if (len < 1e-12) continue;
			auto dir = step / len;
			cs::Ref<MechWalker> struck;
			double best = -1;
			for (const auto& m : mechs)
			{
				if (m == nullptr || !m->Lost.empty() || m->Side == r->Side) continue;
				double t = Gunnery::RaySphere(r->Pos, dir, len, m->Centre(), HitRadiusKm);
				if (t >= 0 && (best < 0 || t < best))
				{
					best = t;
					struck = m;
				}
			}
			if (struck == nullptr) continue;
			bool blocked = false;
			for (const auto& o : battle->Units)
			{
				// The plating it stands on is no shield from a round coming down at it: only a hull it is not on.
				if (o == nullptr || o == struck->Hull || o->Side == r->Side || !o->Present() || o->Surrendering) continue;
				double t = Gunnery::RaySphere(r->Pos, dir, best, o->Pos, Gunnery::HitRadius(o));
				if (t >= 0 && t < best)
				{
					blocked = true;
					break;
				}
			}
			if (blocked) continue;
			r->Gone = true;
			auto at = r->Pos + dir * best;
			battle->Emit(FxKind::Impact, at, at, 0.002, struck->Side, nullptr, -1);
			struck->Struck(r->Damage, r->Shooter != nullptr ? r->Shooter->Name() : cs::String());
		}
	}

	void MechWalker::EngagedBy(const cs::Ref<BattleUnit>& u, double dt)
	{
		double rate, damage, range, muzzle, spread;
		if (!ThreatOf(u, rate, damage, range, muzzle, spread)) return;
		auto centre = Centre();
		auto to = centre - u->Pos;
		double d = to.Length();
		if (d > range || d < 1e-9) return;
		// Each hostile fires on its own beat (its id sets the phase): the rounds due between this step's start and end.
		double phase = static_cast<double>((static_cast<uint64_t>(u->Id) * 0x9E3779B97F4A7C15ull) >> 11) / static_cast<double>(1ull << 53);
		double t1 = Battle->Seconds, t0 = t1 - dt;
		auto first = static_cast<int64_t>(cs::Math::Floor(t0 * rate + phase)) + 1;
		auto last = static_cast<int64_t>(cs::Math::Floor(t1 * rate + phase));
		if (last < first) return;
		if (last - first + 1 > MaxShotsPerStep) first = last - MaxShotsPerStep + 1;
		// Led on its way relative to the shooter, once over.
		double tof = d / muzzle;
		auto aim = centre + (Vel - u->Vel) * tof - u->Pos;
		aim = aim.LengthSquared() > 1e-18 ? aim.Normalized() : to / d;
		auto side = PerpendicularTo(aim);
		auto lift = DVec3::Cross(aim, side);
		for (int64_t shot = first; shot <= last; shot++)
		{
			double sx, sy;
			Spread2(static_cast<uint64_t>(u->Id) + 1, static_cast<uint64_t>(shot), sx, sy);
			auto dir = (aim + side * (sx * spread) + lift * (sy * spread)).Normalized();
			auto from = u->Pos + dir * cs::Math::Min(d * 0.5, Gunnery::HitRadius(u));
			auto r = cs::New<KineticRound>();
			r->Pos = from;
			r->Vel = u->Vel + dir * muzzle;
			r->Damage = damage;
			r->Life = range * 1.25 / muzzle;
			r->Side = u->Side;
			r->Tracer = true;
			r->Aimed = true;
			r->SmallCraft = u->Kind == UnitKind::Fighter;
			r->Shooter = u;
			Battle->Rounds.Add(r);
			Battle->Emit(FxKind::Guns, from, from + dir * cs::Math::Min(range, d * 1.2), 0, u->Side, u, -1);
		}
	}

	void MechWalker::Struck(double damage, const cs::String& by)
	{
		if (!Lost.empty() || damage <= 0) return;
		Armor -= damage;
		HitsTaken++;
		bool fresh = SinceHit > 4;
		SinceHit = 0;
		if (Armor <= 0)
		{
			Armor = 0;
			Lost = by.empty() ? cs::String("wrecked under fire") : cs::Cat("wrecked by the ", by, "'s guns");
			JetsLit = false;
			salvoLeft = 0;
			Battle->Emit(FxKind::Impact, Centre(), Centre(), 0.012, Side, nullptr, -1);
			return;
		}
		if (fresh || EventS <= 0) Note(cs::Cat("TAKING FIRE", by.empty() ? cs::String() : cs::Cat(" from the ", by), ": armour ", cs::Fmt(ArmorFraction() * 100, "0"), "%"));
	}

	EmbodimentCameraRig MechWalker::CameraRig() const { return EmbodimentCameraRig(EmbodimentCameraKind::Shoulder, 6, 0.05, 0.07); }

	EmbodimentRolePanel MechWalker::RolePanel()
	{
		EmbodimentRolePanel p;
		p.Title = "MECH";
		p.Verb = "Piloting";
		auto bar = [](double f)
		{
			int n = static_cast<int>(cs::Math::Round(cs::Math::Clamp(f, 0.0, 1.0) * 10));
			cs::String s;
			for (int i = 0; i < 10; i++) s += i < n ? "|" : ".";
			return s;
		};
		cs::String where = Hull != nullptr
			? cs::Cat("On the ", Hull->Name(), "'s plating", Hull->Side == Side ? cs::String() : cs::String(" (hostile)"), "  \xC2\xB7  ",
				cs::Fmt(Speed * 1000, "0.0"), " m/s")
			: cs::Cat(JetsLit ? "JETS LIT" : "Coasting", "  \xC2\xB7  ", cs::Fmt((Vel - (Anchor() != nullptr ? Anchor()->Vel : DVec3::Zero)).Length() * 1000, "N0"), " m/s");
		cs::String gauges = cs::Cat("ARMOUR ", bar(ArmorFraction()), " ", cs::Fmt(ArmorFraction() * 100, "0"), "%  \xC2\xB7  HEAT ", bar(Heat / 100),
			Overheated ? " LOCKED" : "", "  \xC2\xB7  JETS ", bar(JetFuel / JetFuelS), "  \xC2\xB7  ", CannonAmmo, " shells, ", Missiles, " missiles");
		cs::String state = cs::Cat(where, SinceHit < 2 ? "  \xC2\xB7  UNDER FIRE" : "", "  \xC2\xB7  ", gauges);
		p.Status = EventS > 0 && !Event.empty() ? cs::Cat(Event, "  \xC2\xB7  ", state) : state;
		if (Hull != nullptr)
		{
			p.Abilities.Add(EmbodimentAbility("W/S", "throttle the legs (Shift runs)"));
			p.Abilities.Add(EmbodimentAbility("A/D", "turn the legs"));
			p.Abilities.Add(EmbodimentAbility("Mouse", "twist the torso and aim the arms"));
			p.Abilities.Add(EmbodimentAbility("Space (hold)", "jump jets: lift off and fly", JetFuel > 0));
		}
		else
		{
			p.Abilities.Add(EmbodimentAbility("Space (hold)", "jump jets along the look", JetFuel > 0));
			p.Abilities.Add(EmbodimentAbility("W/S A/D Q/E", "steer the jets' push (Shift at full)", JetFuel > 0));
			p.Abilities.Add(EmbodimentAbility("Let go", "coast; boots catch on any hull you reach"));
		}
		p.Abilities.Add(EmbodimentAbility("Left mouse", "arm autocannons (hold)", !Overheated && CannonAmmo > 0));
		p.Abilities.Add(EmbodimentAbility("Right mouse", "missile salvo at the hostile nearest the aim", !Overheated && Missiles > 0 && salvoLeft == 0 && missileCooldown <= 0));
		p.Abilities.Add(EmbodimentAbility("' ;", "next seat in the fight"));
		p.Abilities.Add(EmbodimentAbility("P", "back to the free SpaceGod view"));
		return p;
	}

	EmbodimentSubject MechWalker::Subject() const
	{
		EmbodimentSubject s;
		s.Battle = Battle;
		s.Unit = Anchor();
		s.Ship = s.Unit != nullptr ? s.Unit->Ship : nullptr;
		s.HasPoint = true;
		s.PointKm = Pos;
		return s;
	}
}
