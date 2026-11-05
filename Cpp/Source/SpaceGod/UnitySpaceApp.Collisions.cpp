// Port of Unity.SpaceGod Runtime/UnitySpaceApp.Collisions.cs (US-T29): collisions for the piloted hull. Members are
// declared in UnitySpaceApp.Collisions.inl.

#include "UnitySpaceApp.h"

#include "AsteroidField.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Collisions.h"
#include "Combat/CombatState.h"
#include "Combat/PilotFlight.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Infrastructure/Infrastructure.h"
#include "Sensors/Sensors.h"

#include "Views/AsteroidBelt.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	bool UnitySpaceApp::UpdatePilotCollisions(const cs::Ref<Core::Ship>& ship, DVec3 posBefore, float dt)
	{
		// Riding a hulk on the clamps is not flying into it.
		if (Salvage != nullptr) return true;
		PruneContactToasts(Time::unscaledTime);
		double jd = Clock->Jd;
		DVec3 frameHelio = Ephemeris()->HelioAt(pilotFrameBody, jd);
		DVec3 frameVel = FrameVelocityKmS(pilotFrameBody, jd);
		auto spec = ship->Combat->Spec;
		double pad = (pilot().Pos - posBefore).Length() + 1;
		CollisionVolume me = CollisionVolume::Hull(spec, pilot().Pos, pilot().Vel, pilot().Forward);
		cs::Ref<Core::Battle> battle = ship->Battle;
		cs::Ref<BattleUnit> mine = battle ? battle->UnitOf(ship) : nullptr;

		auto Near = [&](DVec3 pos, double radius) { return (pos - me.Pos).Length() <= me.BoundingRadiusKm() + radius + pad; };

		if (battle != nullptr)
		{
			for (cs::Ref<BattleUnit> u : battle->Units)
			{
				if (u == mine || !u->Present()) continue;
				DVec3 pos = battle->Anchor + u->Pos;
				if (!Near(pos, u->LengthKm * 0.5)) continue;
				CollisionVolume other = CollisionVolume::Hull(u->Combat->Spec, pos, u->Vel, u->Forward);
				if (u->Kind == UnitKind::Platform) other.MassKt = 0;
				Impact hit = Collide(me, other, posBefore, dt);
				if (!hit.Hit) continue;
				if (u->Kind != UnitKind::Platform)
				{
					u->Pos = other.Pos - battle->Anchor;
					u->Vel = other.Vel;
				}
				if (hit.EnergyTJ <= 0) continue;
				battle->ApplyImpact(u, hit.Point - battle->Anchor, Collisions::DamageHp(u->Combat->Spec, hit.EnergyShareTJ()));
				if (!Struck(ship, me, hit, ContactKey::Of(u), u->Kind == UnitKind::Fighter ? cs::String("a fighter") : cs::Cat("the ", u->Name()))) return false;
			}
		}
		else
		{
			for (cs::Ref<Core::Ship> s : Fleet()->Ships())
			{
				// Not an escort: the fleet places it at its leader (its slot is only drawn), so it would be struck
				// as a second hull stacked on its leader's centre rather than where it is seen.
				if (s == ship || s->Destroyed || s->Battle != nullptr || s->Planetside() || s->Combat == nullptr || s->IsEscort()) continue;
				bool derelict = s->Derelict && s->Orbit != nullptr;
				cs::Func<DVec3, double> helioAt = derelict
					? cs::Func<DVec3, double>([this, s](double j) { return DerelictHelio(Ephemeris(), s, j); })
					: cs::Func<DVec3, double>([this, s](double j) { return Fleet()->StateOf(s, j).HelioKm; });
				DVec3 pos = helioAt(jd) - frameHelio;
				if (!Near(pos, s->LengthKm() * 0.5)) continue;
				DVec3 vel = VelocityKmS(helioAt, jd) - frameVel;
				DVec3 facing = Fleet()->StateOf(s, jd).Facing;
				CollisionVolume other = CollisionVolume::Hull(s->Combat->Spec, pos, vel, facing);
				other.Pinned = true; // on its schedule (or adrift where it was left): it holds its track
				Impact hit = Collide(me, other, posBefore, dt);
				if (!hit.Hit || hit.EnergyTJ <= 0) continue;
				DVec3 local = Collisions::ToLocal(hit.Point, other.Pos, other.Axis, PilotState::LevelUp(other.Axis), s->LengthKm());
				Collisions::ApplyDamage(s->Combat, local, Collisions::DamageHp(s->Combat->Spec, hit.EnergyShareTJ()));
				if (!s->Combat->Alive() && World()->War->LoseInCollision(s, pilotFrameBody, other.Pos, jd, cs::Cat("the ", ship->Name)))
					ShowToast(cs::Cat("The ", s->Name, " breaks up under the impact"));
				if (!Struck(ship, me, hit, ContactKey::Of(s), cs::Cat(derelict ? "the derelict " : "the ", s->Name))) return false;
			}
		}

		// Wreckage: every piece of a wreck's drifting debris field.
		// (Iterated by value: a hull lost in Struck adds its own wreck to the list, and the loop ends right there.)
		for (cs::Ref<Wreck> w : World()->War->Wrecks->Items)
		{
			DVec3 centre = World()->WreckHelio(w, jd) - frameHelio;
			if (!Near(centre, w->DebrisRadiusKm(jd) + w->LengthKm)) continue;
			if (w->DebrisPieceRadiusKm(jd) <= 0) continue;
			DVec3 vel = VelocityKmS([this, w](double j) { return World()->WreckHelio(w, j); }, jd) - frameVel;
			for (int k = 0; k < w->DebrisPieces(); k++)
			{
				CollisionVolume piece = w->DebrisCollider(k, jd, centre, vel);
				Impact hit = Collide(me, piece, posBefore, dt);
				if (!hit.Hit || hit.EnergyTJ <= 0) continue;
				if (!Struck(ship, me, hit, ContactKey::Of(w), cs::Cat("debris from the wreck of ", w->Name))) return false;
			}
		}

		// Stations, yards and spacecraft in orbit: immovable, as far as a hull is concerned.
		for (cs::Ref<OrbitalStructure> st : World()->Infrastructure->Structures)
		{
			DVec3 pos = st->HelioAt(Ephemeris(), jd) - frameHelio;
			double r = cs::Math::Max(0.01, st->LengthKm * 0.5);
			if (!Near(pos, r)) continue;
			CollisionVolume other = CollisionVolume::Sphere(pos, VelocityKmS([this, st](double j) { return st->HelioAt(Ephemeris(), j); }, jd) - frameVel, r, 0);
			Impact hit = Collide(me, other, posBefore, dt);
			if (!hit.Hit || hit.EnergyTJ <= 0) continue;
			if (!Struck(ship, me, hit, ContactKey::Of(st), st->Name)) return false;
		}

		// The belt's real rocks (Sol).
		cs::Ref<Core::AsteroidField> field = IsSol() ? (AsteroidBelt != nullptr ? AsteroidBelt->Field : nullptr) : nullptr;
		ScanBelt(field, frameHelio + me.Pos, jd);
		if (field != nullptr)
		{
			DVec3 sun = Ephemeris()->HelioAt(0, jd);
			for (int i : beltCandidates)
			{
				DVec3 pos = sun + field->PositionAu(i, jd) * Astro::AuKm - frameHelio;
				double r = field->DiameterKm(i) * 0.5;
				if (!Near(pos, r)) continue;
				DVec3 vel = VelocityKmS([this, field, i](double j) { return Ephemeris()->HelioAt(0, j) + field->PositionAu(i, j) * Astro::AuKm; }, jd) - frameVel;
				CollisionVolume rock = CollisionVolume::Sphere(pos, vel, r, 0);
				Impact hit = Collide(me, rock, posBefore, dt);
				if (!hit.Hit || hit.EnergyTJ <= 0) continue;
				if (!Struck(ship, me, hit, ContactKey::Of(field->Number[i]), cs::Cat("the asteroid (", field->Number[i], ")"))) return false;
			}
		}

		pilot().Pos = me.Pos;
		pilot().Vel = me.Vel;
		return true;
	}

	Impact UnitySpaceApp::Collide(CollisionVolume& me, CollisionVolume& other, DVec3 meBefore, double dt)
	{
		Impact hit = Collisions::Resolve(me, other);
		if (hit.Hit) return hit;
		DVec3 otherBefore = other.Pos - other.Vel * dt;
		DVec3 relStart = otherBefore - meBefore;
		DVec3 move = (other.Pos - me.Pos) - relStart;
		if (move.Length() < cs::Math::Min(me.RadiusKm, other.RadiusKm)) return hit;
		double t = Collisions::TimeOfImpact(relStart, move, me.BoundingRadiusKm() + other.BoundingRadiusKm());
		if (t < 0) return hit;
		DVec3 meEnd = me.Pos, otherEnd = other.Pos;
		constexpr int steps = 8;
		for (int k = 0; k <= steps; k++)
		{
			double s = t + (1 - t) * k / steps;
			me.Pos = DVec3::Lerp(meBefore, meEnd, s);
			other.Pos = DVec3::Lerp(otherBefore, otherEnd, s);
			DVec3 unusedNormal, unusedPoint;
			double unusedDepth = 0;
			if (Collisions::Contact(me, other, unusedNormal, unusedDepth, unusedPoint)) return Collisions::Resolve(me, other);
		}
		me.Pos = meEnd;
		other.Pos = otherEnd;
		return hit;
	}

	bool UnitySpaceApp::Struck(const cs::Ref<Core::Ship>& ship, CollisionVolume& me, const Impact& hit, const ContactKey& what, const cs::String& name)
	{
		pilot().Pos = me.Pos;
		pilot().Vel = me.Vel;
		auto spec = ship->Combat->Spec;
		double hp = Collisions::DamageHp(spec, hit.EnergyShareTJ());
		double before = ship->Combat->HullFraction();
		cs::Ref<Core::Battle> battle = ship->Battle;
		cs::Ref<BattleUnit> unit = battle ? battle->UnitOf(ship) : nullptr;
		if (unit != nullptr)
		{
			unit->Pos = pilot().Pos - battle->Anchor;
			unit->Vel = pilot().Vel;
			unit->Forward = pilot().Forward;
			battle->ApplyImpact(unit, hit.Point - battle->Anchor, hp);
		}
		else
		{
			DVec3 local = Collisions::ToLocal(hit.Point, pilot().Pos, pilot().Forward, pilot().Up, ship->LengthKm());
			Collisions::ApplyDamage(ship->Combat, local, hp);
			if (!ship->Combat->Alive()) World()->War->LoseInCollision(ship, pilotFrameBody, pilot().Pos, Clock->Jd, name);
		}
		if (ship->Destroyed || !ship->Combat->Alive())
		{
			ExitPiloting();
			ShowToast(cs::Cat("The ", ship->Name, " breaks up in a collision with ", name, " at ", Speed(hit.ClosingKmS)));
			return false;
		}
		float now = Time::unscaledTime;
		float at = 0;
		if (!contactToastAt.TryGetValue(what, at) || now - at > ContactToastIntervalS)
		{
			contactToastAt[what] = now;
			double lost = before - ship->Combat->HullFraction();
			ShowToast(cs::Cat("COLLISION with ", name, " at ", Speed(hit.ClosingKmS),
				(lost > 0.0005 ? cs::Cat(": hull ", Mathf::RoundToInt(static_cast<float>(ship->Combat->HullFraction() * 100)), "%") : hp > 0 ? cs::String(": shields took it") : cs::String())));
		}
		return true;
	}

	bool UnitySpaceApp::SurfaceImpact(const cs::Ref<Core::Ship>& ship, int body, DVec3 outward, double sinkKmS)
	{
		auto spec = ship->Combat->Spec;
		double hp = Collisions::DamageHp(spec, Collisions::SurfaceImpactShareTJ(Collisions::MassOf(spec), sinkKmS));
		if (hp <= 0) return true;
		DVec3 down = Collisions::ToLocal(pilot().Pos - outward, pilot().Pos, pilot().Forward, pilot().Up, 1);
		Impact impact;
		impact.Hit = true;
		impact.ClosingKmS = sinkKmS;
		impact.Normal = -outward;
		DVec3 local = Collisions::SurfacePoint(spec, down);
		impact.Point = pilot().Pos + (Collisions::ToWorld(local, pilot().Forward, pilot().Up) * ship->LengthKm());
		// Its whole energy share is the hull's (the world takes the rest): reuse the struck-hull path.
		impact.EnergyTJ = Collisions::SurfaceImpactShareTJ(Collisions::MassOf(spec), sinkKmS) * 2;
		CollisionVolume me = CollisionVolume::Hull(spec, pilot().Pos, pilot().Vel, pilot().Forward);
		return Struck(ship, me, impact, ContactKey::Of((*Catalog())[body]), (*Catalog())[body]->Name);
	}

	void UnitySpaceApp::ScanBelt(const cs::Ref<Core::AsteroidField>& field, DVec3 hullHelio, double jd)
	{
		double au = hullHelio.Length() / Astro::AuKm;
		if (field == nullptr || au < 1.5 || au > 6)
		{
			beltCandidates.Clear();
			beltScanning.Clear();
			beltCursor = 0;
			return;
		}
		// (C#: beltXyz == null || shorter than the field. An empty list stands for null.)
		if (beltXyz.Count() < field->Count * 3) beltXyz = cs::NewArray<float>(field->Count * 3);
		int per = (field->Count + BeltScanFrames - 1) / BeltScanFrames;
		int end = cs::Math::Min(field->Count, beltCursor + per);
		field->PositionsAu(jd, beltCursor, per, beltXyz);
		DVec3 sun = Ephemeris()->HelioAt(0, jd);
		DVec3 p = (hullHelio - sun) / Astro::AuKm;
		double limit = BeltCandidateKm / Astro::AuKm, limit2 = limit * limit;
		for (int i = beltCursor; i < end; i++)
		{
			double dx = beltXyz[i * 3] - p.X, dy = beltXyz[i * 3 + 1] - p.Y, dz = beltXyz[i * 3 + 2] - p.Z;
			if (dx * dx + dy * dy + dz * dz < limit2) beltScanning.Add(i);
		}
		beltCursor = end;
		if (beltCursor < field->Count) return;
		std::swap(beltCandidates, beltScanning);
		beltScanning.Clear();
		beltCursor = 0;
	}

	void UnitySpaceApp::ReportBeltRocks(double jd)
	{
		auto sensors = World()->Sensors;
		if (sensors == nullptr) return;
		cs::List<Core::Sensors::Occluder> rocks;
		cs::Ref<Core::AsteroidField> field = IsSol() ? (AsteroidBelt != nullptr ? AsteroidBelt->Field : nullptr) : nullptr;
		if (field != nullptr && beltCandidates.Count() > 0)
		{
			DVec3 sun = Ephemeris()->HelioAt(0, jd);
			for (int i : beltCandidates)
			{
				double r = field->DiameterKm(i) * 0.5;
				if (r >= Core::Sensors::MinOccluderRadiusKm) rocks.Add(Core::Sensors::Occluder(sun + field->PositionAu(i, jd) * Astro::AuKm, r));
			}
		}
		sensors->ReportPilotRocks(rocks, jd);
	}

	void UnitySpaceApp::PruneContactToasts(float now)
	{
		if (contactToastAt.Count() <= 64) return;
		staleContactToasts.Clear();
		for (auto kv : contactToastAt)
			if (now - kv.second > ContactToastStaleS) staleContactToasts.Add(kv.first);
		for (const auto& k : staleContactToasts) contactToastAt.Remove(k);
	}

	DVec3 UnitySpaceApp::FrameVelocityKmS(int body, double jd)
	{
		return VelocityKmS([this, body](double j) { return Ephemeris()->HelioAt(body, j); }, jd);
	}

	DVec3 UnitySpaceApp::VelocityKmS(const cs::Func<DVec3, double>& helioAt, double jd)
	{
		// (Hoisted: C# evaluates the two samples left to right.)
		DVec3 ahead = helioAt(jd + VelocityStepDays);
		DVec3 behind = helioAt(jd - VelocityStepDays);
		return (ahead - behind) / (2 * VelocityStepDays * Astro::SecondsPerDay);
	}

	cs::String UnitySpaceApp::Speed(double kmS)
	{
		return kmS >= 1 ? cs::Cat(cs::Fmt(kmS, "0.0"), " km/s") : cs::Cat(cs::Fmt(kmS * 1000, "0"), " m/s");
	}
}
