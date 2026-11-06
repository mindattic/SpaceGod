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
	double BioGlobule::MaxHp() const { return Mass * BioMass::HpPerTonne * (Form == BioForm::Tumor ? BioMass::TumorToughness : 1.0); }

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
		if (CountOf(BioForm::Globule) >= MaxGlobules) return;
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
		// US-T192: a bloom is one of a swarm (not news on its own); a tumor names what it was on.
		if (g->Form == BioForm::Bloom) return;
		cs::String on;
		if (g->Form == BioForm::Tumor && g->Host == BioHost::Body)
		{
			auto cat = world->Catalog();
			if (cat != nullptr && g->HostId >= 0 && g->HostId < cat->Count()) on = cs::Cat(" off ", (*cat)[g->HostId]->Name);
		}
		world->Log(jd, cs::Cat("A Bio Mass ", g->FormName(), " (", static_cast<int>(g->Mass / 1000), " kt) was burned away", on, " by ", by), WorldEventKind::Battle, g->Body);
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

	// ---------------------------------------------------------------- blooms and tumors (US-T192)

	int BioMass::CountOf(BioForm form) const
	{
		int n = 0;
		for (const auto& g : Globules)
			if (g->Alive() && g->Form == form) n++;
		return n;
	}

	int BioMass::OnShip(int shipId, BioForm form) const
	{
		int n = 0;
		for (const auto& g : Globules)
			if (g->Alive() && g->Form == form && g->Host == BioHost::Ship && g->HostId == shipId) n++;
		return n;
	}

	cs::List<int> BioMass::RootBodies() const
	{
		cs::List<int> out = cs::List<int>();
		auto cat = world ? world->Catalog() : nullptr;
		int n = cat ? cat->Count() : 0;
		for (int b = 0; b < n; b++)
		{
			auto kind = (*cat)[b]->Kind;
			if (kind == BodyKind::Station || kind == BodyKind::Asteroid) out.Add(b);
		}
		return out;
	}

	bool BioMass::TumorOn(int body) const
	{
		for (const auto& g : Globules)
		{
			if (!g->Alive() || g->Form != BioForm::Tumor) continue;
			if (g->Host == BioHost::Body && g->HostId == body) return true;
			if (!g->Attached() && g->GoalBody == body) return true; // a seed already on its way there
		}
		return false;
	}

	int BioMass::NearestRootBody(DVec3 helioKm, const cs::List<DVec3>& bodies, int except) const
	{
		int best = -1;
		double bestD = cs::Double::PositiveInfinity;
		for (int b : roots)
		{
			if (b == except || b >= bodies.Count() || TumorOn(b)) continue;
			double d = (bodies[b] - helioKm).Length();
			if (d < bestD) { bestD = d; best = b; }
		}
		return best;
	}

	cs::String BioMass::HostName(const cs::Ref<BioGlobule>& g) const
	{
		if (g->Host == BioHost::Body)
		{
			auto cat = world->Catalog();
			if (cat != nullptr && g->HostId >= 0 && g->HostId < cat->Count()) return (*cat)[g->HostId]->Name;
		}
		else if (g->Host == BioHost::Ship)
			for (const auto& s : world->Fleet->Ships())
				if (s != nullptr && s->Id == g->HostId) return s->Name;
		return cs::String("open space");
	}

	void BioMass::Latch(const cs::Ref<BioGlobule>& g, BioHost host, int hostId, DVec3 hostHelio, double hostRadiusKm, double jd)
	{
		DVec3 here = Helio(g, jd);
		DVec3 out = here - hostHelio;
		out = out.LengthSquared() > 1e-12 ? out.Normalized() : RandomDirection();
		g->Host = host;
		g->HostId = hostId;
		g->AttachedJd = jd;
		g->GoalGate = g->GoalBody = -1;
		g->Drift = DVec3::Zero;
		// Half sunk into the host's skin, on the side it came in from.
		double skin = hostRadiusKm + g->RadiusKm() * 0.45;
		if (host == BioHost::Body)
		{
			g->Body = hostId;
			g->Anchor = out * skin;
			g->Local = DVec3::Zero;
		}
		else
		{
			g->Body = 0;
			g->Anchor = hostHelio - world->Ephemeris->HelioAt(0, jd);
			g->Local = out * skin;
		}
		if (g->Form == BioForm::Tumor)
			world->Log(jd, cs::Cat("A Bio Mass tumor (", static_cast<int>(g->Mass / 1000), " kt) took root on ", HostName(g)), WorldEventKind::Battle, g->Body);
	}

	void BioMass::Detach(const cs::Ref<BioGlobule>& g, const cs::List<DVec3>& bodies, double jd)
	{
		// A bloom off its host withers; a tumor torn loose (its hull gone, landed or in a lane) drifts, and creeps for the next rock.
		if (g->Form == BioForm::Bloom) { g->Hp = 0; return; }
		DVec3 here = bodies[g->Body] + g->Anchor + g->Local;
		g->Host = BioHost::None;
		g->HostId = -1;
		g->AttachedJd = cs::Double::NaN;
		g->Body = 0;
		g->Anchor = here - bodies[0];
		g->Local = DVec3::Zero;
		g->GoalBody = NearestRootBody(here, bodies, -1);
		world->Log(jd, "A Bio Mass tumor was torn loose from its host and is adrift", WorldEventKind::Battle, 0);
	}

	cs::Ref<BioGlobule> BioMass::SpawnTumor(BioHost host, int hostId, int body, DVec3 anchorKm, double mass, double jd)
	{
		auto g = Spawn(body, anchorKm, DVec3::Zero, mass, jd);
		g->Form = BioForm::Tumor;
		g->Mass = cs::Math::Clamp(mass, 1e4, TumorMaxMass);
		g->Hp = g->MaxHp();
		g->GoalGate = g->GoalBody = -1;
		g->Drift = g->Drift * 0.2;
		g->NextBroodJd = jd + TumorBroodDays * (0.5 + rng.NextDouble());
		if (host == BioHost::Body)
		{
			auto cat = world->Catalog();
			if (cat != nullptr && hostId >= 0 && hostId < cat->Count())
				Latch(g, host, hostId, world->Ephemeris->HelioAt(hostId, jd), (*cat)[hostId]->RadiusKm, jd);
		}
		else if (host == BioHost::Ship)
			for (const auto& s : world->Fleet->Ships())
			{
				if (s == nullptr || s->Id != hostId || s->Destroyed) continue;
				try
				{
					double len = s->Combat && s->Combat->Spec ? s->Combat->Spec->LengthKm * 0.5 : 0.1;
					Latch(g, host, hostId, world->Fleet->StateOf(s, jd).HelioKm, len, jd);
				}
				catch (const std::exception&) {}
				break;
			}
		return g;
	}

	int BioMass::EruptBlooms(DVec3 helioKm, int count, double jd)
	{
		int made = 0;
		DVec3 root = world->Ephemeris->HelioAt(0, jd);
		for (int i = 0; i < count && CountOf(BioForm::Bloom) < MaxBlooms; i++)
		{
			auto b = Spawn(0, helioKm - root + RandomDirection() * 0.6, DVec3::Zero, BloomMass * (0.7 + 0.6 * rng.NextDouble()), jd);
			b->Form = BioForm::Bloom;
			b->Hp = b->MaxHp();
			b->GoalGate = b->GoalBody = -1;
			b->Drift = RandomDirection() * 0.05;
			b->WithersJd = jd + BloomLifeDays * (0.7 + 0.6 * rng.NextDouble());
			made++;
		}
		return made;
	}

	void BioMass::SeepTumor(double jd)
	{
		if (CountOf(BioForm::Tumor) >= MaxTumors || roots.Count() == 0) return;
		cs::List<int> free = cs::List<int>();
		for (int b : roots)
			if (!TumorOn(b)) free.Add(b);
		if (free.Count() == 0) return;
		int body = free[rng.Next(free.Count())];
		auto cat = world->Catalog();
		double R = (*cat)[body]->RadiusKm;
		SpawnTumor(BioHost::Body, body, body, RandomDirection() * (R + 5), TumorSeedMass * (0.8 + 0.6 * rng.NextDouble()), jd);
	}

	void BioMass::Drain(const cs::Ref<BioGlobule>& g, double /*jd*/, double dt, const cs::Ref<Ship>& ship)
	{
		auto c = ship->Combat;
		if (c == nullptr || c->Spec == nullptr) return;
		bool bloom = g->Form == BioForm::Bloom;
		double floor = c->Spec->Hull * (bloom ? BloomHullFloor : TumorHullFloor);
		double dps = bloom ? BloomBiteDps : TumorDrainDps * cs::Math::Max(0.5, g->RadiusKm() / 0.75);
		if (c->Hull <= floor) return;
		double take = cs::Math::Min(c->Hull - floor, dps * dt);
		c->Hull -= take;
		if (!bloom) g->Mass = cs::Math::Min(TumorMaxMass, g->Mass + take * TumorMassPerHull);
	}

	void BioMass::DrainBody(const cs::Ref<BioGlobule>& g, double /*jd*/, double dt)
	{
		bool bloom = g->Form == BioForm::Bloom;
		double days = dt / 86400.0;
		double share = (bloom ? BloomEatPerDay : TumorEatPerDay) * days;
		double dps = bloom ? BloomBiteDps : TumorDrainDps * cs::Math::Max(0.5, g->RadiusKm() / 0.75);
		for (const auto& s : world->Economy->Settlements)
		{
			if (s->Body != g->HostId || s->Fallen) continue;
			// Its people (never below StationFloor of what it began with), and its platforms' hulls.
			double floor = cs::Math::Max(1.0, StationFloor * s->InitialPopulation);
			if (s->Population > floor) s->Population = cs::Math::Max(floor, s->Population - s->Population * share);
			for (const auto& p : s->Platforms)
			{
				if (p == nullptr || p->Spec == nullptr || !p->Alive()) continue;
				double pf = p->Spec->Hull * (bloom ? BloomHullFloor : TumorHullFloor);
				if (p->Hull > pf) p->Hull = cs::Math::Max(pf, p->Hull - dps * dt);
			}
		}
	}

	void BioMass::Grow(const cs::Ref<BioGlobule>& g, double jd, double dt, const cs::List<DVec3>& bodies, const cs::List<Hull>& hulls)
	{
		bool bloom = g->Form == BioForm::Bloom;
		if (bloom && !cs::Double::IsNaN(g->WithersJd) && jd >= g->WithersJd) { g->Hp = 0; return; } // withered
		auto cat = world->Catalog();

		// Latched on: it rides its host and drains it.
		if (g->Host == BioHost::Ship)
		{
			const Hull* host = nullptr;
			for (const auto& h : hulls)
				if (h.ship->Id == g->HostId) { host = &h; break; }
			if (host == nullptr) { Detach(g, bodies, jd); return; }
			g->Body = 0;
			g->Anchor = host->helio - bodies[0];
			Drain(g, jd, dt, host->ship);
			return;
		}
		if (g->Host == BioHost::Body)
		{
			if (g->HostId < 0 || g->HostId >= bodies.Count()) { Detach(g, bodies, jd); return; }
			DrainBody(g, jd, dt);
			// A bloom that has held on a station or asteroid with no tumor takes root there and grows into one.
			auto kind = (*cat)[g->HostId]->Kind;
			if (bloom && jd - g->AttachedJd >= BloomRootDays && (kind == BodyKind::Station || kind == BodyKind::Asteroid)
				&& !TumorOn(g->HostId) && CountOf(BioForm::Tumor) < MaxTumors)
			{
				g->Form = BioForm::Tumor;
				g->Mass = TumorSeedMass * 0.5;
				g->Hp = g->MaxHp();
				g->WithersJd = cs::Double::NaN;
				g->NextBroodJd = jd + TumorBroodDays * (0.5 + rng.NextDouble());
				g->Anchor = g->Anchor.Normalized() * ((*cat)[g->HostId]->RadiusKm + g->RadiusKm() * 0.45);
				world->Log(jd, cs::Cat("Bio Mass blooms took root on ", HostName(g), ": a tumor is growing there"), WorldEventKind::Battle, g->HostId);
			}
			return;
		}

		// Free: a bloom darts for the nearest hull or station in sight; a tumor's seed creeps for its rock, and takes any
		// hull that brushes it.
		DVec3 here = bodies[g->Body] + g->Anchor + g->Local;
		double r = g->RadiusKm();
		double speed = bloom ? BloomKmS : TumorKmS;
		double step = speed * dt;
		const Hull* prey = nullptr;
		double preyD = cs::Double::PositiveInfinity;
		double reach = bloom ? BloomSeekKm : 0;
		for (const auto& h : hulls)
		{
			double len = h.ship->Combat->Spec ? h.ship->Combat->Spec->LengthKm * 0.5 : 0.1;
			double d = (h.helio - here).Length();
			double contact = len + r + LatchKm;
			if (d <= contact + (bloom ? 0.0 : step) && d < preyD) { prey = &h; preyD = d; continue; } // touching
			if (bloom && d < reach && d < preyD) { prey = &h; preyD = d; }
		}
		int goalBody = -1;
		double goalD = cs::Double::PositiveInfinity;
		if (bloom)
		{
			for (int b : roots)
			{
				if (b >= bodies.Count()) continue;
				double d = (bodies[b] - here).Length() - (*cat)[b]->RadiusKm;
				if (d < reach && d < goalD) { goalD = d; goalBody = b; }
			}
		}
		else
		{
			if (g->GoalBody < 0 || g->GoalBody >= bodies.Count()) g->GoalBody = NearestRootBody(here, bodies, -1);
			goalBody = g->GoalBody;
			if (goalBody >= 0) goalD = (bodies[goalBody] - here).Length() - (*cat)[goalBody]->RadiusKm;
		}

		if (prey != nullptr && (bloom ? preyD < goalD : true))
		{
			double len = prey->ship->Combat->Spec ? prey->ship->Combat->Spec->LengthKm * 0.5 : 0.1;
			DVec3 to = prey->helio - here;
			double d = to.Length();
			if (d <= len + r + LatchKm + step) { Latch(g, BioHost::Ship, prey->ship->Id, prey->helio, len, jd); return; }
			g->Anchor = g->Anchor + to * (step / d);
			return;
		}
		if (goalBody >= 0)
		{
			// Worked in the rock's own frame, so it closes on it however fast it orbits.
			if (g->Body != goalBody)
			{
				g->Anchor = here - bodies[goalBody];
				g->Local = DVec3::Zero;
				g->Body = goalBody;
			}
			double R = (*cat)[goalBody]->RadiusKm;
			double d = g->Anchor.Length();
			if (d - R <= r + LatchKm + step) { Latch(g, BioHost::Body, goalBody, bodies[goalBody], R, jd); return; }
			g->Anchor = g->Anchor - g->Anchor * (step / d);
			return;
		}
		// Nothing in sight: it drifts.
		g->Local = g->Local + g->Drift * dt;
	}

	void BioMass::Brood(const cs::Ref<BioGlobule>& g, double jd, DVec3 here, const cs::List<Hull>& hulls)
	{
		// A globule or a tumor erupts a cluster of blooms when a hull is near (not the one it is on), now and then.
		if (g->Form == BioForm::Bloom || jd < g->NextBroodJd || CountOf(BioForm::Bloom) >= MaxBlooms) return;
		bool near = false;
		for (const auto& h : hulls)
		{
			if (g->Host == BioHost::Ship && h.ship->Id == g->HostId) continue;
			if ((h.helio - here).Length() < BloomSeekKm) { near = true; break; }
		}
		if (!near) return;
		double days = g->Form == BioForm::Tumor ? TumorBroodDays : GlobuleBroodDays;
		g->NextBroodJd = jd + days * (0.5 + rng.NextDouble());
		int count = 3 + rng.Next(BloomCluster - 2);
		DVec3 skin = here + RandomDirection() * g->RadiusKm();
		int made = EruptBlooms(skin, count, jd);
		if (made > 0 && g->Form == BioForm::Tumor)
			world->Log(jd, cs::Cat("Bio Mass blooms erupted from the tumor on ", HostName(g)), WorldEventKind::Battle, g->Body);
	}

	void BioMass::FeedTumor(const cs::Ref<BioGlobule>& g, double jd, double days, DVec3 here, const cs::List<DVec3>& bodies)
	{
		auto cat = world->Catalog();
		if (g->Attached())
		{
			// Rooted, it grows on what it drains (twice as fast on an asteroid's rock).
			bool rock = g->Host == BioHost::Body && g->HostId >= 0 && g->HostId < cat->Count() && (*cat)[g->HostId]->Kind == BodyKind::Asteroid;
			g->Mass = cs::Math::Min(TumorMaxMass, g->Mass * (1 + TumorGrowthPerDay * days * (rock ? 2.0 : 1.0)));
			// Still half sunk in its host's skin as it swells.
			if (g->Host == BioHost::Body && g->Anchor.LengthSquared() > 0)
				g->Anchor = g->Anchor.Normalized() * ((*cat)[g->HostId]->RadiusKm + g->RadiusKm() * 0.45);
		}
		// Wrecks in reach are drawn in and eaten.
		auto wrecks = world->War ? world->War->Wrecks : nullptr;
		if (wrecks != nullptr)
			for (int w = wrecks->Items.Count() - 1; w >= 0; w--)
			{
				auto wreck = wrecks->Items[w];
				if ((world->WreckHelio(wreck, jd) - here).Length() > g->RadiusKm() + WreckReachKm) continue;
				wrecks->Items.RemoveAt(w);
				g->Mass = cs::Math::Min(TumorMaxMass, g->Mass + WreckMeal);
				g->Consumed++;
				world->Log(jd, cs::Cat("A Bio Mass tumor consumed the wreck of the ", wreck->Name), WorldEventKind::Loss, wreck->Body);
			}
		// Big enough, it sheds a seed that creeps off for the next station or asteroid: how it spreads.
		if (g->Attached() && g->Mass >= TumorShedMass && CountOf(BioForm::Tumor) < MaxTumors)
		{
			int next = NearestRootBody(here, bodies, g->Host == BioHost::Body ? g->HostId : -1);
			if (next < 0) return;
			g->Mass -= TumorSeedMass;
			g->Hp = cs::Math::Min(g->Hp, g->MaxHp());
			auto seed = SpawnTumor(BioHost::None, -1, 0, here - bodies[0] + RandomDirection() * (g->RadiusKm() * 3), TumorSeedMass, jd);
			seed->GoalBody = next;
			world->Log(jd, cs::Cat("A Bio Mass tumor on ", HostName(g), " shed a seed: it is creeping for ", (*cat)[next]->Name), WorldEventKind::Battle, g->Body);
		}
	}

	void BioMass::Step(double jd, double dt, const cs::List<DVec3>& bodies, const cs::List<DVec3>& shipHelio, const cs::List<cs::Ref<Ship>>& armed, const cs::List<Hull>& hulls)
	{
		for (int i = 0; i < Globules.Count(); i++)
		{
			auto g = Globules[i];
			if (!g->Alive()) continue;
			if (g->Form == BioForm::Globule) Creep(g, jd, dt, bodies);
			else Grow(g, jd, dt, bodies, hulls); // US-T192
			if (!g->Alive()) continue;
			DVec3 here = bodies[g->Body] + g->Anchor + g->Local;
			double r = g->RadiusKm();
			// US-T192: the guns that would hit a growth's host hold most of their fire.
			double cover = HostCoverFor(g);
			Brood(g, jd, here, hulls);

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
				Hit(g, dps * dt * cover, BioShooter::Orbital, b, bodies[b], jd);
			}
			// US-T192: a growth on a station or asteroid (no orbital guns of its own) is fired on by that station's platforms.
			if (g->Alive() && g->Host == BioHost::Body && g->HostId >= 0 && g->HostId < bodies.Count() && BodyGunCount(g->HostId) == 0)
			{
				double dps = 0;
				for (const auto& s : world->Economy->Settlements)
					if (s->Body == g->HostId && !s->Fallen)
						for (const auto& p : s->Platforms) dps += WeaponDps(p);
				Hit(g, dps * dt * cover, BioShooter::Orbital, g->HostId, bodies[g->HostId], jd);
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
				Hit(g, dps * dt * cover, BioShooter::Gate, gi, gate->PositionKm, jd);
			}
			// Every armed hull in reach, of every faction: kill on sight.
			for (int k = 0; k < armed.Count() && g->Alive(); k++)
			{
				double d = (here - shipHelio[k]).Length();
				if (d > ShipEngageKm + r) continue;
				auto ship = armed[k];
				Hit(g, WeaponDps(ship->Combat) * dt, BioShooter::Ship, ship->Id, shipHelio[k], jd);
				// A touch is a bite (a globule's: blooms and tumors latch on and drain instead, in Grow).
				if (g->Form == BioForm::Globule && ship->Combat->Spec != nullptr && d < r + ship->Combat->Spec->LengthKm + 0.5)
				{
					double floor = ship->Combat->Spec->Hull * BiteHullFloor;
					double bite = BiteDps * (r / 0.5) * dt;
					if (ship->Combat->Hull > floor) ship->Combat->Hull = cs::Math::Max(floor, ship->Combat->Hull - bite);
				}
			}
			if (!g->Alive()) continue;
			double regen = g->Form == BioForm::Tumor ? TumorRegenPerS : g->Form == BioForm::Bloom ? 0.0 : RegenPerS;
			g->Hp = cs::Math::Min(g->MaxHp(), g->Hp + regen * dt);
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
			if (!g->Alive() || g->Form == BioForm::Bloom) continue;
			DVec3 here = bodies[g->Body] + g->Anchor + g->Local;
			if (g->Form == BioForm::Tumor)
			{
				FeedTumor(g, jd, days, here, bodies);
				continue;
			}
			g->Mass = cs::Math::Min(MaxMass, g->Mass * (1 + GrowthPerDay * days));
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
			if (g->Mass >= SplitMass && CountOf(BioForm::Globule) < MaxGlobules)
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
		auto cat = world->Catalog();
		int n = cat ? cat->Count() : 0;
		// US-T192: the stations and asteroids a tumor can root on, and its own seeding.
		roots.Clear();
		for (int b = 0; b < n; b++)
		{
			auto kind = (*cat)[b]->Kind;
			if (kind == BodyKind::Station || kind == BodyKind::Asteroid) roots.Add(b);
		}
		if (cs::Double::IsNaN(nextTumorJd)) nextTumorJd = jd + TumorSeepDays * (0.5 + rng.NextDouble());
		else if (jd >= nextTumorJd)
		{
			SeepTumor(jd);
			nextTumorJd = jd + TumorSeepDays * (0.5 + rng.NextDouble());
		}
		if (Globules.Count() == 0) return;

		cs::List<DVec3> bodies = cs::List<DVec3>();
		for (int b = 0; b < n; b++) bodies.Add(world->Ephemeris->HelioAt(b, jd));

		// The hulls in open space (not landed, not riding a lane), and where they are, once an update: the armed ones
		// fire on it, any of them is a bloom's prey or a tumor's host (US-T192).
		cs::List<cs::Ref<Ship>> armed = cs::List<cs::Ref<Ship>>();
		cs::List<DVec3> shipHelio = cs::List<DVec3>();
		cs::List<Hull> hulls = cs::List<Hull>();
		for (const auto& s : world->Fleet->Ships())
		{
			if (s == nullptr || s->Destroyed || s->Combat == nullptr || s->Spec == nullptr || s->Planetside() || s->Phase == ShipPhase::Lane) continue;
			try
			{
				auto st = world->Fleet->StateOf(s, jd);
				hulls.Add(Hull{ s, st.HelioKm });
				if (WeaponDps(s->Combat) <= 0) continue;
				armed.Add(s);
				shipHelio.Add(st.HelioKm);
			}
			catch (const std::exception&) {}
		}

		int steps = cs::Math::Min(MaxSteps, cs::Math::Max(1, static_cast<int>(cs::Math::Ceiling(dt / StepS))));
		double h = dt / steps;
		for (int k = 0; k < steps; k++) Step(jd, h, bodies, shipHelio, armed, hulls);
		Feed(jd, dt, bodies);
		for (int i = Globules.Count() - 1; i >= 0; i--)
			if (!Globules[i]->Alive()) Globules.RemoveAt(i);
	}
}

SG_SERIALIZE(SpaceGod::Core::BioGlobule, "Unity.SpaceGod.Core.BioGlobule",
	SG_F(Anchor), SG_F(Body), SG_F(BornJd), SG_F(Consumed), SG_F(Drift), SG_F(ExitedJd), SG_F(GoalBody), SG_F(GoalGate),
	SG_F(Hp), SG_F(Id), SG_F(Local), SG_F(Mass), SG_F(Seed),
	SG_OPT(AttachedJd), SG_OPT(Form), SG_OPT(Host), SG_OPT(HostId), SG_OPT(NextBroodJd), SG_OPT(WithersJd)) // US-T192, absent from older saves

SG_SERIALIZE(SpaceGod::Core::BioMass, "Unity.SpaceGod.Core.BioMass",
	SG_F(Crossed), SG_F(Globules), SG_F(Killed), SG_F(lastJd), SG_F(nextId), SG_F(nextSeepJd), SG_F(rng), SG_F(world),
	SG_OPT(nextTumorJd)) // US-T192, absent from older saves
