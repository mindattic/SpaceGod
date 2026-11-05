// US-T97: self-destruct on whatever the player embodies (see SelfDestruct.h).

#include "Embodiment/SelfDestruct.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/Collisions.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/Dogfighting.h"
#include "Combat/PilotFlight.h"
#include "Combat/War.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"

namespace SpaceGod::Core
{
	namespace
	{
		SelfDestructReport last;

		/// The gap between two hull volumes, km (0 when they touch), the closest point on <paramref name="them"/>'s surface
		/// toward the blast, and the way out from the blast to it (unit).
		double Gap(const CollisionVolume& me, const CollisionVolume& them, DVec3& at, DVec3& outward)
		{
			DVec3 pa, pb;
			Collisions::ClosestPoints(me.Tail(), me.Head(), them.Tail(), them.Head(), pa, pb);
			auto d = pb - pa;
			double dist = d.Length();
			if (dist > 1e-9) outward = d / dist;
			else
			{
				auto c = them.Pos - me.Pos;
				outward = c.LengthSquared() > 1e-18 ? c.Normalized() : DVec3::UnitX;
			}
			// The face of its hull toward the blast: what touches it takes the most.
			at = pb - outward * cs::Math::Min(them.RadiusKm, dist);
			return cs::Math::Max(0.0, dist - me.RadiusKm - them.RadiusKm);
		}

		/// The blast's damage <paramref name="gapKm"/> off the hull: all of it touching, falling off with the square of the gap.
		double Falloff(double contactHp, double radiusKm, double gapKm)
		{
			if (radiusKm <= 0 || gapKm > radiusKm) return 0;
			double x = 1 - gapKm / radiusKm;
			return contactHp * x * x;
		}

		/// The blast's <paramref name="hp"/> on a unit, through the battle's own damage path: the shield over the patch
		/// struck first, then the hardpoints on that side by nearness (each a hit of its own, so armour blunts each), and a
		/// small craft's blast on an open weak point as its guns would land there.
		void Strike(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& o, DVec3 at, DVec3 incoming, double hp, bool smallCraft)
		{
			auto oc = o->Combat;
			auto weights = Collisions::StruckSideWeights(oc, o->ToLocal(at));
			double total = 0;
			for (double w : weights) total += w;
			if (total <= 0)
			{
				b->ApplyDamage(o, nullptr, hp, at, at, 1, incoming);
				return;
			}
			for (int i = 0; i < weights.Count() && i < oc->Subsystems.Count(); i++)
			{
				if (weights[i] <= 0) continue;
				if (!o->Present()) return;
				auto s = oc->Subsystems[i];
				double exposed = smallCraft ? WeakPoints::Factor(o, s, incoming) : 1;
				b->ApplyDamage(o, s, hp * weights[i], at, at, exposed, incoming);
			}
		}
	}

	cs::Ref<ShipCombat> SelfDestruct::CombatOf(const cs::Ref<IEmbodiable>& seat)
	{
		if (auto f = cs::As<FighterSeat>(seat)) return f->Unit != nullptr ? f->Unit->Combat : nullptr;
		if (auto h = cs::As<ShipHelm>(seat)) return h->Ship != nullptr ? h->Ship->Combat : nullptr;
		// A gun sets off the hull (or station) it is mounted on: its gunner is aboard.
		if (auto t = cs::As<MountedTurret>(seat)) return t->Unit != nullptr ? t->Unit->Combat : nullptr;
		if (auto st = cs::As<StationSeat>(seat)) return st->Combat;
		return nullptr;
	}

	bool SelfDestruct::Can(const cs::Ref<IEmbodiable>& seat)
	{
		auto c = CombatOf(seat);
		return c != nullptr && c->Spec != nullptr && c->Alive() && !c->SelfDestructed;
	}

	double SelfDestruct::FuseSeconds(const cs::Ref<IEmbodiable>& seat)
	{
		auto c = CombatOf(seat);
		bool small = cs::Is<FighterSeat>(seat), station = cs::Is<StationSeat>(seat);
		if (auto t = cs::As<MountedTurret>(seat))
		{
			small = t->Unit->Kind == UnitKind::Fighter;
			station = t->Unit->Kind == UnitKind::Platform;
		}
		return FuseSeconds(c, small, station);
	}

	double SelfDestruct::FuseSeconds(const cs::Ref<ShipCombat>& c, bool smallCraft, bool station)
	{
		if (station) return 20;
		double m = c != nullptr && c->Spec != nullptr ? MassKt(c) : 0;
		if (smallCraft || m < 20) return 5;
		return m < 1000 ? 8 : 15;
	}

	double SelfDestruct::Remaining(const cs::Ref<IEmbodiable>& seat)
	{
		auto c = CombatOf(seat);
		return c != nullptr && !c->SelfDestructed && c->SelfDestructS >= 0 ? c->SelfDestructS : -1;
	}

	void SelfDestruct::Command(const cs::Ref<IEmbodiable>& seat, int key)
	{
		if (key == static_cast<int>(SelfDestructKey::None)) return;
		Command(CombatOf(seat), key, FuseSeconds(seat));
	}

	void SelfDestruct::Command(const cs::Ref<ShipCombat>& c, int key, double fuse)
	{
		if (c == nullptr || c->SelfDestructed) return;
		if (key == static_cast<int>(SelfDestructKey::Arm) && c->SelfDestructS < 0 && c->Alive()) c->SelfDestructS = fuse;
		else if (key == static_cast<int>(SelfDestructKey::Abort)) c->SelfDestructS = -1;
	}

	bool SelfDestruct::Tick(const cs::Ref<ShipCombat>& c, int key, double fuse, double dt)
	{
		if (c == nullptr) return false;
		Command(c, key, fuse);
		if (c->SelfDestructed || c->SelfDestructS < 0 || !c->Alive()) return false;
		c->SelfDestructS = cs::Math::Max(0.0, c->SelfDestructS - cs::Math::Max(0.0, dt));
		return c->SelfDestructS <= 0;
	}

	void SelfDestruct::Disarm(const cs::Ref<ShipCombat>& c)
	{
		if (c != nullptr && !c->SelfDestructed) c->SelfDestructS = -1;
	}

	double SelfDestruct::MassKt(const cs::Ref<ShipCombat>& c) { return Collisions::MassOf(c->Spec); }

	double SelfDestruct::ReactorFactor(const cs::Ref<ShipCombat>& c)
	{
		// The reactor's own health: half the blast is the reactor going up, the rest the hull's magazines and fuel.
		double sum = 0;
		int n = 0;
		for (const auto& s : c->Subsystems)
			if (s->Kind() == SubsystemKind::Reactor)
			{
				sum += cs::Math::Max(0.0, cs::Math::Min(1.0, s->Health()));
				n++;
			}
		return n == 0 ? 1 : 0.5 + 0.5 * sum / n;
	}

	double SelfDestruct::RadiusKm(const cs::Ref<ShipCombat>& c)
	{
		return c->Spec->LengthKm * 0.5 + ReachKmPerCbrtKt * cs::Math::Pow(MassKt(c) * ReactorFactor(c), 1.0 / 3);
	}

	double SelfDestruct::ContactHp(const cs::Ref<ShipCombat>& c)
	{
		return ContactHpPerCbrtKt * cs::Math::Pow(MassKt(c), 1.0 / 3) * ReactorFactor(c);
	}

	double SelfDestruct::KillCrew(const cs::Ref<ShipCombat>& c, const cs::Ref<Core::Faction>& owners)
	{
		auto inside = c->Interior;
		if (inside == nullptr) return 0;
		double aboard = inside->Crew() + inside->Marines();
		if (aboard <= 0) return 0;
		// Nobody reaches the pods: lost to their faction for good, and a blow to the survivors' morale.
		Crews::RecordDeaths(owners, aboard);
		inside->TakeCrew(inside->Crew());
		inside->TakeMarines(inside->Marines());
		return aboard;
	}

	void SelfDestruct::Record(const SelfDestructReport& r)
	{
		int seq = last.Seq + 1;
		last = r;
		last.Seq = seq;
	}

	const SelfDestructReport& SelfDestruct::Last() { return last; }

	SelfDestructReport SelfDestruct::InBattle(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		SelfDestructReport r;
		if (b == nullptr || u == nullptr || u->Combat == nullptr || !u->Present()) return r;
		auto c = u->Combat;
		r.Name = u->Name();
		r.RadiusKm = RadiusKm(c);
		r.ContactHp = ContactHp(c);
		// The deed is the craft's own, its faction's and its captain's: no longer counted as the player's (the law, US-T96).
		u->PlayerFlown = false;
		auto owners = u->Ship != nullptr && u->Ship->Faction != nullptr ? u->Ship->Faction : u->Faction;
		r.CrewLost = KillCrew(c, owners);
		c->SelfDestructS = -1;
		c->SelfDestructed = true;
		bool small = u->Kind == UnitKind::Fighter;
		auto me = CollisionVolume::Hull(c->Spec, u->Pos, u->Vel, u->Forward);
		b->Emit(FxKind::MineBlast, u->Pos, u->Pos, r.RadiusKm, u->Side, u, -1);
		// Friend and foe alike: a blast takes no sides. (A copy: a kill can change the list.)
		auto units = cs::List<cs::Ref<BattleUnit>>(b->Units.begin(), b->Units.end());
		for (const auto& o : units)
		{
			if (o == u || !o->Present() || o->Combat == nullptr || o->Combat->Spec == nullptr) continue;
			if ((o->Pos - u->Pos).Length() > r.RadiusKm + me.BoundingRadiusKm() + o->LengthKm) continue;
			auto them = CollisionVolume::Hull(o->Combat->Spec, o->Pos, o->Vel, o->Forward);
			DVec3 at, outward;
			double hp = Falloff(r.ContactHp, r.RadiusKm, Gap(me, them, at, outward));
			if (hp <= 0) continue;
			r.Hit++;
			Strike(b, o, at, outward, hp, small);
			if (o->Dead) r.Destroyed++;
		}
		// Then the craft itself, as any kill: its wreck and debris, and its pods (empty: nobody got to them).
		c->ZeroAllShields();
		b->ApplyImpact(u, u->Pos, c->Hull + 1e9);
		Record(r);
		return r;
	}

	SelfDestructReport SelfDestruct::Outside(const cs::Ref<World>& world, const cs::Ref<ShipCombat>& c, const cs::String& name,
		const cs::Ref<Core::Faction>& owners, const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, DVec3 forward)
	{
		SelfDestructReport r;
		if (world == nullptr || c == nullptr || c->Spec == nullptr) return r;
		r.Name = name;
		r.RadiusKm = RadiusKm(c);
		r.ContactHp = ContactHp(c);
		r.CrewLost = KillCrew(c, owners);
		c->SelfDestructS = -1;
		c->SelfDestructed = true;
		double jd = world->Ephemeris->Jd;
		auto frame = world->Ephemeris->Helio(body);
		auto me = CollisionVolume::Hull(c->Spec, offsetKm, DVec3::Zero, forward);
		// The ships near it, as a collision finds them (escorts are drawn at their leader; a derelict is left to its hulk).
		auto ships = cs::List<cs::Ref<Ship>>(world->Fleet->Ships().begin(), world->Fleet->Ships().end());
		for (const auto& s : ships)
		{
			if (s == ship || s->Destroyed || s->Battle != nullptr || s->Planetside() || s->Combat == nullptr || s->IsEscort() || s->Derelict) continue;
			auto st = world->Fleet->StateOf(s, jd);
			auto pos = st.HelioKm - frame;
			if ((pos - offsetKm).Length() > r.RadiusKm + me.BoundingRadiusKm() + s->LengthKm()) continue;
			auto them = CollisionVolume::Hull(s->Combat->Spec, pos, DVec3::Zero, st.Facing);
			DVec3 at, outward;
			double hp = Falloff(r.ContactHp, r.RadiusKm, Gap(me, them, at, outward));
			if (hp <= 0) continue;
			r.Hit++;
			auto local = Collisions::ToLocal(at, them.Pos, them.Axis, PilotState::LevelUp(them.Axis), s->LengthKm());
			Collisions::ApplyDamage(s->Combat, local, hp);
			if (!s->Combat->Alive() && world->War->LoseOutsideBattle(s, body, them.Pos, jd, cs::Cat("The ", s->Name, " is torn apart by the blast of the ", name)))
				r.Destroyed++;
		}
		cs::String line = cs::Cat("The ", name, " self-destructs", r.Hit > 0 ? cs::Cat(", catching ", r.Hit, r.Hit == 1 ? " ship" : " ships", " in the blast") : cs::String());
		c->Hull = 0;
		if (ship != nullptr) world->War->LoseOutsideBattle(ship, body, offsetKm, jd, line);
		else world->Log(jd, line, WorldEventKind::Loss, body);
		Record(r);
		return r;
	}
}
