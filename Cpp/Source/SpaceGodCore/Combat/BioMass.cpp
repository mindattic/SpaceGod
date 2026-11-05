// Not a C# file (US-T125): the Bio Mass (see BioMass.h).
#include "Combat/BioMass.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"
#include "World.h"

#include "Persistence/ObjectGraph.h"

namespace SpaceGod::Core
{
	double BioGlobule::RadiusKm() const { return 0.012 * cs::Math::cbrt(cs::Math::Max(Mass, 1.0)); }
	double BioGlobule::MaxHp() const { return Mass * BioMass::HpPerTonne; }

	BioMass::BioMass(const cs::Ref<World>& world_, int seed, double jd) : world(world_), rng(seed), lastJd(jd)
	{
		// A few already adrift when the game starts, then one seeping in every SeepDays or so.
		for (int i = 0; i < SeededAtStart; i++) Seep(jd);
		nextSeepJd = jd + SeepDays * (0.5 + rng.NextDouble());
	}

	DVec3 BioMass::RandomDirection()
	{
		double z = rng.NextDouble() * 2 - 1, a = rng.NextDouble() * cs::Math::Tau, r = cs::Math::Sqrt(cs::Math::Max(0.0, 1 - z * z));
		// Kept near the ecliptic, where the traffic is: space life drifts in the plane the system's dust lies in.
		return DVec3(r * cs::Math::Cos(a), r * cs::Math::Sin(a), z * 0.25).Normalized();
	}

	int BioMass::BodyGunCount(int body) const
	{
		auto cat = world->Catalog();
		if (cat == nullptr || body < 0 || body >= cat->Count()) return 0;
		switch ((*cat)[body]->Kind)
		{
			case BodyKind::Star: return StarGuns;
			case BodyKind::Planet: case BodyKind::DwarfPlanet: return PlanetGuns;
			case BodyKind::Moon: return MoonGuns;
			default: return 0;
		}
	}

	double BioMass::BodyGunRangeKm(int body) const
	{
		auto cat = world->Catalog();
		if (cat == nullptr || body < 0 || body >= cat->Count()) return 0;
		return (*cat)[body]->RadiusKm * (1 + BodyGunReachRadii) + BodyGunReachKm;
	}

	double BioMass::WeaponDps(const cs::Ref<ShipCombat>& combat)
	{
		if (combat == nullptr || !combat->Alive()) return 0;
		double dps = 0;
		for (const auto& s : combat->Subsystems)
		{
			if (s == nullptr || s->Hp <= 0 || s->Def == nullptr || s->Def->Weapon == nullptr) continue;
			auto w = s->Def->Weapon;
			if (w->Damage <= 0 || w->Cooldown <= 0) continue;
			dps += w->Damage * cs::Math::Max(1, w->Salvo) * w->Accuracy / w->Cooldown;
		}
		return dps;
	}

	DVec3 BioMass::Helio(const cs::Ref<BioGlobule>& g, double jd) const
	{
		return world->Ephemeris->HelioAt(g->Body, jd) + g->Anchor + g->Local;
	}

	cs::Ref<BioGlobule> BioMass::Nearest(DVec3 helioKm, double jd, double& distanceKm) const
	{
		cs::Ref<BioGlobule> best;
		distanceKm = cs::Double::PositiveInfinity;
		for (const auto& g : Globules)
		{
			if (!g->Alive()) continue;
			double d = (Helio(g, jd) - helioKm).Length();
			if (d < distanceKm) { distanceKm = d; best = g; }
		}
		return best;
	}

	cs::Ref<BioGlobule> BioMass::Spawn(int body, DVec3 anchorKm, DVec3 localKm, double mass, double jd)
	{
		auto g = cs::New<BioGlobule>();
		g->Id = nextId++;
		g->Body = body;
		g->Anchor = anchorKm;
		g->Local = localKm;
		g->Mass = cs::Math::Clamp(mass, 1e3, MaxMass);
		g->Hp = g->MaxHp();
		g->BornJd = jd;
		g->Seed = rng.Next();
		g->Drift = RandomDirection() * 0.01;
		Globules.Add(g);
		PickGoal(g);
		return g;
	}

	void BioMass::PickGoal(const cs::Ref<BioGlobule>& g)
	{
		g->GoalGate = -1;
		g->GoalBody = -1;
		int gates = world->Gates.Count();
		// Most make for a gate (it spreads); the rest wander toward a world to feed, and meet its guns.
		if (gates > 0 && rng.NextDouble() < 0.7) g->GoalGate = rng.Next(gates);
		else
		{
			auto cat = world->Catalog();
			int n = cat ? cat->Count() : 0;
			if (n > 1) g->GoalBody = 1 + rng.Next(n - 1);
		}
	}

	void BioMass::Seep(double jd)
	{
		if (Globules.Count() >= MaxGlobules) return;
		auto cat = world->Catalog();
		if (cat == nullptr || cat->Count() == 0) return;
		// Out of the guns' reach: off a gate's approach (beyond its guns) or off a random world (beyond its guns),
		// so it lives a while before something sees it.
		DVec3 at;
		int gates = world->Gates.Count();
		if (gates > 0 && rng.NextDouble() < 0.5)
		{
			auto gate = world->Gates[rng.Next(gates)];
			at = gate->PositionKm + RandomDirection() * (GateGunRangeKm * (4 + 20 * rng.NextDouble()));
		}
		else
		{
			int body = rng.Next(cat->Count());
			double reach = cs::Math::Max(BodyGunRangeKm(body), 1e5);
			at = world->Ephemeris->HelioAt(body, jd) + RandomDirection() * (reach * (2 + 6 * rng.NextDouble()));
		}
		double mass = SpawnMass * (0.6 + 2.4 * rng.NextDouble());
		Spawn(0, at - world->Ephemeris->HelioAt(0, jd), DVec3::Zero, mass, jd);
	}

	void BioMass::Hit(const cs::Ref<BioGlobule>& g, double damage, BioShooter by, int index, DVec3 fromHelio, double jd)
	{
		if (damage <= 0 || !g->Alive()) return;
		g->Hp -= damage;
		g->DamageTaken += damage;
		g->LastShooter = by;
		g->LastShooterIndex = index;
		g->LastShotFromHelio = fromHelio;
		g->LastHitJd = jd;
		if (g->Hp <= 0) Die(g, jd);
	}

	void BioMass::Die(const cs::Ref<BioGlobule>& g, double jd)
	{
		g->Hp = 0;
		Killed++;
		const char* by = g->LastShooter == BioShooter::Gate ? "gate guns" : g->LastShooter == BioShooter::Ship ? "warships" : "orbital guns";
		world->Log(jd, cs::Cat("A Bio Mass globule (", static_cast<int>(g->Mass / 1000), " kt) was burned away by ", by), WorldEventKind::Battle, g->Body);
	}

	void BioMass::Creep(const cs::Ref<BioGlobule>& g, double /*jd*/, double dt, const cs::List<DVec3>& bodies)
	{
		// The slow roll of the meat inside its own frame, damped back toward its anchor.
		g->Local = g->Local + g->Drift * dt;
		if (g->Local.Length() > 2 * g->RadiusKm() + 1) g->Drift = -g->Drift;

		DVec3 goal;
		bool any = false;
		if (g->GoalGate >= 0 && g->GoalGate < world->Gates.Count()) { goal = world->Gates[g->GoalGate]->PositionKm; any = true; }
		else if (g->GoalBody >= 0 && g->GoalBody < bodies.Count()) { goal = bodies[g->GoalBody]; any = true; }
		if (!any) return;
		DVec3 here = bodies[g->Body] + g->Anchor;
		DVec3 to = goal - here;
		double d = to.Length();
		if (d < 1) return;
		double step = cs::Math::Min(d, CreepKmS * dt);
		g->Anchor = g->Anchor + to * (step / d);
	}

	void BioMass::TryCross(const cs::Ref<BioGlobule>& g, double jd)
	{
		if (g->GoalGate < 0 || g->GoalGate >= world->Gates.Count()) return;
		auto gate = world->Gates[g->GoalGate];
		DVec3 here = world->Ephemeris->HelioAt(g->Body, jd) + g->Anchor;
		if ((here - gate->PositionKm).Length() > JumpGate::ActivationRangeKm + g->RadiusKm()) return;
		auto far = gate->Partner;
		auto farWorld = far && far->Home ? far->Home->World : nullptr;
		Crossed++;
		if (farWorld == nullptr || farWorld.get() == world.get())
		{
			// Nowhere to go: it smothers the dead gate and is burned off it.
			world->Log(jd, cs::Cat("Bio Mass smothered the ", gate->Name, " gate and was burned off it"), WorldEventKind::Battle, 0);
			g->Hp = 0;
			return;
		}
		world->Log(jd, cs::Cat("Bio Mass forced its way through the ", gate->Name, " gate"), WorldEventKind::Battle, 0);
		auto keep = g;
		Globules.Remove(g);
		farWorld->EnsureBioMass(jd)->Arrive(keep, far, jd);
	}

	void BioMass::Arrive(const cs::Ref<BioGlobule>& g, const cs::Ref<JumpGate>& gate, double jd)
	{
		g->Id = nextId++;
		g->Body = 0;
		g->Anchor = gate->ArrivalOffsetKm() - world->Ephemeris->HelioAt(0, jd);
		g->Local = DVec3::Zero;
		g->ExitedJd = jd;
		Globules.Add(g);
		PickGoal(g);
		world->Log(jd, cs::Cat("Bio Mass came out of the ", gate->Name, " gate: its guns opened fire"), WorldEventKind::Battle, 0);
	}

	void BioMass::Step(double jd, double dt, const cs::List<DVec3>& bodies, const cs::List<DVec3>& shipHelio, const cs::List<cs::Ref<Ship>>& armed)
	{
		for (int i = 0; i < Globules.Count(); i++)
		{
			auto g = Globules[i];
			if (!g->Alive()) continue;
			Creep(g, jd, dt, bodies);
			DVec3 here = bodies[g->Body] + g->Anchor + g->Local;
			double r = g->RadiusKm();

			// Every stellar body's orbital guns, and its settlements' platforms beside them.
			for (int b = 0; b < bodies.Count() && g->Alive(); b++)
			{
				int guns = BodyGunCount(b);
				if (guns == 0) continue;
				double d = (here - bodies[b]).Length();
				if (d > BodyGunRangeKm(b) + r) continue;
				double dps = guns * GunDps;
				for (const auto& s : world->Economy->Settlements)
					if (s->Body == b && !s->Fallen)
						for (const auto& p : s->Platforms) dps += WeaponDps(p);
				Hit(g, dps * dt, BioShooter::Orbital, b, bodies[b], jd);
			}
			// The gates' guns and forts: on the approach, and on the far side as it comes out.
			for (int gi = 0; gi < world->Gates.Count(); gi++)
			{
				auto gate = world->Gates[gi];
				if (!g->Alive()) break;
				double d = (here - gate->PositionKm).Length();
				if (d > GateGunRangeKm + r || gate->Wrecked()) continue;
				double dps = GateGuns * GunDps * gate->Integrity;
				for (const auto& f : gate->Forts) dps += WeaponDps(f);
				Hit(g, dps * dt, BioShooter::Gate, gi, gate->PositionKm, jd);
			}
			// Every armed hull in reach, of every faction: kill on sight.
			for (int k = 0; k < armed.Count() && g->Alive(); k++)
			{
				double d = (here - shipHelio[k]).Length();
				if (d > ShipEngageKm + r) continue;
				auto ship = armed[k];
				Hit(g, WeaponDps(ship->Combat) * dt, BioShooter::Ship, ship->Id, shipHelio[k], jd);
				// A touch is a bite.
				if (ship->Combat->Spec != nullptr && d < r + ship->Combat->Spec->LengthKm + 0.5)
				{
					double floor = ship->Combat->Spec->Hull * BiteHullFloor;
					double bite = BiteDps * (r / 0.5) * dt;
					if (ship->Combat->Hull > floor) ship->Combat->Hull = cs::Math::Max(floor, ship->Combat->Hull - bite);
				}
			}
			if (!g->Alive()) continue;
			g->Hp = cs::Math::Min(g->MaxHp(), g->Hp + RegenPerS * dt);
			int before = Globules.Count();
			TryCross(g, jd);
			if (Globules.Count() < before) i--; // it went through (now the far system's)
		}
	}

	void BioMass::Feed(double jd, double dt, const cs::List<DVec3>& bodies)
	{
		double days = dt / 86400.0;
		int count = Globules.Count();
		for (int i = 0; i < count; i++)
		{
			auto g = Globules[i];
			if (!g->Alive()) continue;
			g->Mass = cs::Math::Min(MaxMass, g->Mass * (1 + GrowthPerDay * days));
			DVec3 here = bodies[g->Body] + g->Anchor + g->Local;
			// Wrecks in reach are drawn in and eaten.
			auto wrecks = world->War ? world->War->Wrecks : nullptr;
			if (wrecks != nullptr)
				for (int w = wrecks->Items.Count() - 1; w >= 0; w--)
				{
					auto wreck = wrecks->Items[w];
					if ((world->WreckHelio(wreck, jd) - here).Length() > g->RadiusKm() + WreckReachKm) continue;
					wrecks->Items.RemoveAt(w);
					g->Mass = cs::Math::Min(MaxMass, g->Mass + WreckMeal);
					g->Hp = cs::Math::Min(g->MaxHp(), g->Hp + WreckMeal * HpPerTonne);
					g->Consumed++;
					world->Log(jd, cs::Cat("Bio Mass consumed the wreck of the ", wreck->Name), WorldEventKind::Loss, wreck->Body);
				}
			// A station on a world it has reached (its guns failed): it eats into the population, never below half.
			if (g->GoalBody >= 0 && g->GoalBody < bodies.Count() && (here - bodies[g->GoalBody]).Length() < BodyGunRangeKm(g->GoalBody))
				for (const auto& s : world->Economy->Settlements)
				{
					if (s->Body != g->GoalBody || s->Fallen) continue;
					double eaten = s->Population * StationEatPerDay * days;
					if (s->Population - eaten < 1) continue;
					s->Population -= eaten;
					if (s->Population < 1) s->Population = 1;
				}
			// Too big: it buds, while there is room (bounded by MaxGlobules).
			if (g->Mass >= SplitMass && Globules.Count() < MaxGlobules)
			{
				g->Mass *= 0.5;
				g->Hp = cs::Math::Min(g->Hp, g->MaxHp());
				auto bud = Spawn(g->Body, g->Anchor + RandomDirection() * (g->RadiusKm() * 6), DVec3::Zero, g->Mass, jd);
				bud->Hp = cs::Math::Min(bud->MaxHp(), g->Hp);
			}
		}
	}

	void BioMass::Update(double jd)
	{
		if (world == nullptr || world->Ephemeris == nullptr) return;
		if (cs::Double::IsNaN(lastJd)) lastJd = jd;
		double dt = (jd - lastJd) * 86400.0;
		if (dt <= 0) return;
		lastJd = jd;

		if (jd >= nextSeepJd)
		{
			Seep(jd);
			nextSeepJd = jd + SeepDays * (0.5 + rng.NextDouble());
		}
		if (Globules.Count() == 0) return;

		auto cat = world->Catalog();
		int n = cat ? cat->Count() : 0;
		cs::List<DVec3> bodies = cs::List<DVec3>();
		for (int b = 0; b < n; b++) bodies.Add(world->Ephemeris->HelioAt(b, jd));

		// The armed hulls in open space (not landed, not riding a lane), and where they are, once an update.
		cs::List<cs::Ref<Ship>> armed = cs::List<cs::Ref<Ship>>();
		cs::List<DVec3> shipHelio = cs::List<DVec3>();
		for (const auto& s : world->Fleet->Ships())
		{
			if (s == nullptr || s->Destroyed || s->Combat == nullptr || s->Spec == nullptr || s->Planetside() || s->Phase == ShipPhase::Lane) continue;
			if (WeaponDps(s->Combat) <= 0) continue;
			try
			{
				auto st = world->Fleet->StateOf(s, jd);
				armed.Add(s);
				shipHelio.Add(st.HelioKm);
			}
			catch (const std::exception&) {}
		}

		int steps = cs::Math::Min(MaxSteps, cs::Math::Max(1, static_cast<int>(cs::Math::Ceiling(dt / StepS))));
		double h = dt / steps;
		for (int k = 0; k < steps; k++) Step(jd, h, bodies, shipHelio, armed);
		Feed(jd, dt, bodies);
		for (int i = Globules.Count() - 1; i >= 0; i--)
			if (!Globules[i]->Alive()) Globules.RemoveAt(i);
	}
}

SG_SERIALIZE(SpaceGod::Core::BioGlobule, "Unity.SpaceGod.Core.BioGlobule",
	SG_F(Anchor), SG_F(Body), SG_F(BornJd), SG_F(Consumed), SG_F(Drift), SG_F(ExitedJd), SG_F(GoalBody), SG_F(GoalGate),
	SG_F(Hp), SG_F(Id), SG_F(Local), SG_F(Mass), SG_F(Seed))

SG_SERIALIZE(SpaceGod::Core::BioMass, "Unity.SpaceGod.Core.BioMass",
	SG_F(Crossed), SG_F(Globules), SG_F(Killed), SG_F(lastJd), SG_F(nextId), SG_F(nextSeepJd), SG_F(rng), SG_F(world))
