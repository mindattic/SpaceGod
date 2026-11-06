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
#include "Combat/BioMass.h"
#include "Combat/CargoPods.h"
#include "Combat/Collisions.h"
#include "Combat/SectionColliders.h"
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
	namespace
	{
		/// Moves a body (and its sections with it) to <paramref name="to"/>.
		void MoveBody(CollisionBody& b, DVec3 to)
		{
			DVec3 d = to - b.Volume.Pos;
			b.Volume.Pos = to;
			for (auto& s : b.Sections) s.Centre = s.Centre + d;
		}

		/// US-T177: the two-phase contact (bounding spheres, then the hulls' section boxes) between the piloted hull and
		/// another body, caught even on a fast pass the way <see cref="UnitySpaceApp::Collide"/> catches it: tested where
		/// they end the frame and, if apart there but their broad spheres crossed on the way, at steps from first touch on.
		SectionImpact CollideBodies(CollisionBody& me, CollisionBody& other, DVec3 meBefore, double dt)
		{
			DVec3 otherBefore = other.Volume.Pos - other.Volume.Vel * dt;
			// US-T222: already inside one another where the frame began (a helm just taken where its schedule had it, beside
			// a ship that had only been parted from it), they are not meeting now: the hull is put back outside, with no
			// impulse and no damage. An impact is a meeting, caught the frame it happens; only that one does harm.
			if (SectionColliders::Contact(me, other).Hit)
			{
				DVec3 meEnd = me.Volume.Pos, otherEnd = other.Volume.Pos;
				MoveBody(me, meBefore);
				MoveBody(other, otherBefore);
				bool already = SectionColliders::Contact(me, other).Hit;
				MoveBody(me, meEnd);
				MoveBody(other, otherEnd);
				if (already)
				{
					// (Contact's normal points from me to the other; a little past the depth, so the next frame starts apart.)
					SectionContact c = SectionColliders::Contact(me, other);
					if (c.Hit) MoveBody(me, me.Volume.Pos - c.Normal * (c.DepthKm * 1.01 + 1e-6));
					return SectionImpact();
				}
			}
			SectionImpact hit = SectionColliders::Resolve(me, other);
			if (hit.Hit.Hit) return hit;
			DVec3 relStart = otherBefore - meBefore;
			DVec3 move = (other.Volume.Pos - me.Volume.Pos) - relStart;
			if (move.Length() < cs::Math::Min(me.Volume.RadiusKm, other.Volume.RadiusKm)) return hit;
			double t = Collisions::TimeOfImpact(relStart, move, me.BroadRadiusKm() + other.BroadRadiusKm());
			if (t < 0) return hit;
			DVec3 meEnd = me.Volume.Pos, otherEnd = other.Volume.Pos;
			constexpr int steps = 8;
			for (int k = 0; k <= steps; k++)
			{
				double s = t + (1 - t) * k / steps;
				MoveBody(me, DVec3::Lerp(meBefore, meEnd, s));
				MoveBody(other, DVec3::Lerp(otherBefore, otherEnd, s));
				if (SectionColliders::Contact(me, other).Hit) return SectionColliders::Resolve(me, other);
			}
			MoveBody(me, meEnd);
			MoveBody(other, otherEnd);
			return hit;
		}

		/// A body for something with no sections: a sphere.
		CollisionBody SphereBody(DVec3 pos, DVec3 vel, double radiusKm, double massKt, bool pinned)
		{
			CollisionBody b;
			b.Id = 1;
			b.Volume = CollisionVolume::Sphere(pos, vel, radiusKm, massKt);
			b.Volume.Pinned = pinned;
			return b;
		}

		/// A drifting pod's canister, km (CargoPodsView::PodLengthKm long): what the hull shoulders aside.
		constexpr double PodRadiusKm = 0.007;
	}

	bool UnitySpaceApp::UpdatePilotCollisions(const cs::Ref<Core::Ship>& ship, DVec3 posBefore, float dt)
	{
		// Riding a hulk on the clamps is not flying into it.
		if (Salvage != nullptr) return true;
		PruneContactToasts(Time::unscaledTime);
		double jd = Clock->Jd;
		DVec3 frameHelio = Ephemeris()->HelioAt(pilotFrameBody, jd);
		DVec3 frameVel = FrameVelocityKmS(pilotFrameBody, jd);
		double pad = (pilot().Pos - posBefore).Length() + 1;
		// US-T177: the hull is its sections' boxes (one per module still attached), each with its own hit points; the
		// battle leaves this hull to this loop (Battle::StepCollisions skips the flown unit), so nothing is resolved twice.
		CollisionBody me = SectionColliders::HullBody(0, ship->Combat, pilot().Pos, pilot().Vel, pilot().Forward, pilot().Up, ship->LengthKm(), true);
		cs::Ref<Core::Battle> battle = ship->Battle;
		cs::Ref<BattleUnit> mine = battle ? battle->UnitOf(ship) : nullptr;

		auto Near = [&](DVec3 pos, double radius) { return (pos - me.Volume.Pos).Length() <= me.BroadRadiusKm() + radius + pad; };
		// The flown hull's side of a contact: the section struck takes it into its structure (a piece may break away,
		// US-T91), then the hull and its hardpoints through Struck. False if the hull was lost.
		auto HitMe = [&](const SectionImpact& hit, const ContactKey& what, const cs::String& name)
		{
			if (hit.SectionA >= 0 && hit.SectionA < (int)me.Sections.size())
				SectionColliders::StrikeStructure(ship->Combat, me.Sections[hit.SectionA].ModuleId, Collisions::DamageHp(ship->Combat->Spec, hit.Hit.EnergyShareTJ()));
			return Struck(ship, me.Volume, hit.Hit, what, name);
		};

		if (battle != nullptr)
		{
			for (cs::Ref<BattleUnit> u : battle->Units)
			{
				if (u == mine || !u->Present()) continue;
				DVec3 pos = battle->Anchor + u->Pos;
				if (!Near(pos, u->LengthKm * 0.5)) continue;
				DVec3 right, up;
				u->Basis(right, up);
				CollisionBody other = SectionColliders::HullBody(1, u->Combat, pos, u->Vel, u->Forward, up, u->LengthKm, true);
				if (u->Kind == UnitKind::Platform) other.Volume.MassKt = 0;
				SectionImpact hit = CollideBodies(me, other, posBefore, dt);
				if (!hit.Hit.Hit) continue;
				if (u->Kind != UnitKind::Platform)
				{
					u->Pos = other.Volume.Pos - battle->Anchor;
					u->Vel = other.Volume.Vel;
				}
				if (hit.Hit.EnergyTJ <= 0) continue;
				double hp = Collisions::DamageHp(u->Combat->Spec, hit.Hit.EnergyShareTJ());
				battle->ApplyImpact(u, hit.Hit.Point - battle->Anchor, hp);
				if (hit.SectionB >= 0 && hit.SectionB < (int)other.Sections.size() && u->Present())
					SectionColliders::StrikeStructure(u->Combat, other.Sections[hit.SectionB].ModuleId, hp);
				if (!HitMe(hit, ContactKey::Of(u), u->Kind == UnitKind::Fighter ? cs::String("a fighter") : cs::Cat("the ", u->Name()))) return false;
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
				// US-T222: a scheduled ship's velocity is its schedule's (Fleet::HelioVelocityOf). Differenced from StateOf it lost
				// its world's own motion round the Sun (a parked hull is placed in that world's cached frame, which holds still
				// across the difference), so a warship parked at Earth beside the helm came at it at 29 km/s: the SFS Resolute,
				// taken still beside the SFS Dauntless, met it "at 1.1 km/s" and broke up.
				DVec3 vel = derelict ? VelocityKmS(helioAt, jd) - frameVel : Fleet()->HelioVelocityOf(s, jd) / Astro::SecondsPerDay - frameVel;
				DVec3 facing = Fleet()->StateOf(s, jd).Facing;
				CollisionBody other = SectionColliders::HullBody(1, s->Combat, pos, vel, facing, PilotState::LevelUp(facing), s->LengthKm(), true);
				other.Volume.Pinned = true; // on its schedule (or adrift where it was left): it holds its track
				SectionImpact hit = CollideBodies(me, other, posBefore, dt);
				if (!hit.Hit.Hit || hit.Hit.EnergyTJ <= 0) continue;
				double hp = Collisions::DamageHp(s->Combat->Spec, hit.Hit.EnergyShareTJ());
				DVec3 local = Collisions::ToLocal(hit.Hit.Point, other.Volume.Pos, other.Volume.Axis, PilotState::LevelUp(other.Volume.Axis), s->LengthKm());
				Collisions::ApplyDamage(s->Combat, local, hp);
				if (hit.SectionB >= 0 && hit.SectionB < (int)other.Sections.size() && s->Combat->Alive())
					SectionColliders::StrikeStructure(s->Combat, other.Sections[hit.SectionB].ModuleId, hp);
				if (!s->Combat->Alive() && World()->War->LoseInCollision(s, pilotFrameBody, other.Volume.Pos, jd, cs::Cat("the ", ship->Name)))
					ShowToast(cs::Cat("The ", s->Name, " breaks up under the impact"));
				if (!HitMe(hit, ContactKey::Of(s), cs::Cat(derelict ? "the derelict " : "the ", s->Name))) return false;
			}
		}

		// Wreckage: every piece of a wreck's drifting debris field, bounced off any station in its way (US-T177, as
		// DebrisFields draws it).
		// (Iterated by value: a hull lost in Struck adds its own wreck to the list, and the loop ends right there.)
		for (cs::Ref<Wreck> w : World()->War->Wrecks->Items)
		{
			DVec3 wreckHelio = World()->WreckHelio(w, jd);
			DVec3 centre = wreckHelio - frameHelio;
			if (!Near(centre, w->DebrisRadiusKm(jd) + w->LengthKm)) continue;
			if (w->DebrisPieceRadiusKm(jd) <= 0) continue;
			DVec3 vel = VelocityKmS([this, w](double j) { return World()->WreckHelio(w, j); }, jd) - frameVel;
			auto stations = SectionColliders::StationsNear(World(), wreckHelio, w->DebrisRadiusKm(jd) + w->LengthKm, jd);
			for (int k = 0; k < w->DebrisPieces(); k++)
			{
				CollisionVolume piece = w->DebrisCollider(k, jd, centre, vel);
				piece.Pos = centre + SectionColliders::BounceOff(DVec3::Zero, w->DebrisOffsetKm(k, jd), stations);
				CollisionBody body;
				body.Id = 1;
				body.Volume = piece;
				SectionImpact hit = CollideBodies(me, body, posBefore, dt);
				if (!hit.Hit.Hit || hit.Hit.EnergyTJ <= 0) continue;
				if (!HitMe(hit, ContactKey::Of(w), cs::Cat("debris from the wreck of ", w->Name))) return false;
			}
		}

		// Stations, yards and spacecraft in orbit: immovable, as far as a hull is concerned.
		for (cs::Ref<OrbitalStructure> st : World()->Infrastructure->Structures)
		{
			DVec3 pos = st->HelioAt(Ephemeris(), jd) - frameHelio;
			double r = cs::Math::Max(0.01, st->LengthKm * 0.5);
			if (!Near(pos, r)) continue;
			CollisionBody other = SphereBody(pos, VelocityKmS([this, st](double j) { return st->HelioAt(Ephemeris(), j); }, jd) - frameVel, r, 0, false);
			SectionImpact hit = CollideBodies(me, other, posBefore, dt);
			if (!hit.Hit.Hit || hit.Hit.EnergyTJ <= 0) continue;
			if (!HitMe(hit, ContactKey::Of(st), st->Name)) return false;
		}

		// US-T177: drifting cargo pods and the Bio Mass are shouldered aside rather than flown through. Neither is on a
		// path the hull can knock it off (a pod drifts on its spill, a globule creeps on the Bio Mass's own coarse steps),
		// so the contact only puts it back outside the hull's sections, by its tow or its local offset: no impulse, no
		// damage (a pod is a tin can; the Bio Mass does its own biting).
		auto Shoulder = [&](DVec3 pos, double radiusKm) -> DVec3
		{
			if (!Near(pos, radiusKm)) return DVec3::Zero;
			CollisionBody other = SphereBody(pos, DVec3::Zero, radiusKm, 1, false);
			SectionContact c = SectionColliders::Contact(me, other);
			return c.Hit ? c.Normal * c.DepthKm : DVec3::Zero;
		};
		if (World()->War->Wrecks->Pods != nullptr)
			for (cs::Ref<CargoPod> p : World()->War->Wrecks->Pods->Items)
			{
				if (p->Expired(jd)) continue;
				p->TowKm = p->TowKm + Shoulder(p->Helio(World(), jd) - frameHelio, PodRadiusKm);
			}
		if (World()->BioMass != nullptr)
			for (cs::Ref<BioGlobule> g : World()->BioMass->Globules)
			{
				if (!g->Alive() || g->Attached()) continue;
				g->Local = g->Local + Shoulder(World()->BioMass->Helio(g, jd) - frameHelio, g->RadiusKm());
			}

		// The belt's real rocks (Sol).
		cs::Ref<Core::AsteroidField> field = IsSol() ? (AsteroidBelt != nullptr ? AsteroidBelt->Field : nullptr) : nullptr;
		ScanBelt(field, frameHelio + me.Volume.Pos, jd);
		if (field != nullptr)
		{
			DVec3 sun = Ephemeris()->HelioAt(0, jd);
			for (int i : beltCandidates)
			{
				DVec3 pos = sun + field->PositionAu(i, jd) * Astro::AuKm - frameHelio;
				double r = field->DiameterKm(i) * 0.5;
				if (!Near(pos, r)) continue;
				DVec3 vel = VelocityKmS([this, field, i](double j) { return Ephemeris()->HelioAt(0, j) + field->PositionAu(i, j) * Astro::AuKm; }, jd) - frameVel;
				CollisionBody rock = SphereBody(pos, vel, r, 0, false);
				SectionImpact hit = CollideBodies(me, rock, posBefore, dt);
				if (!hit.Hit.Hit || hit.Hit.EnergyTJ <= 0) continue;
				if (!HitMe(hit, ContactKey::Of(field->Number[i]), cs::Cat("the asteroid (", field->Number[i], ")"))) return false;
			}
		}

		pilot().Pos = me.Volume.Pos;
		pilot().Vel = me.Volume.Vel;
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
