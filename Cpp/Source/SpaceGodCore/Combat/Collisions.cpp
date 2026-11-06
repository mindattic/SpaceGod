// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Collisions.cs (see docs/MIGRATION.md).

#include "Combat/Collisions.h"

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/PilotFlight.h"
#include "Combat/ShieldEmitters.h"

namespace SpaceGod::Core
{
	CollisionVolume CollisionVolume::Sphere(DVec3 pos, DVec3 vel, double radiusKm, double massKt)
	{
		CollisionVolume v;
		v.Pos = pos;
		v.Vel = vel;
		v.RadiusKm = cs::Math::Max(0.0, radiusKm);
		v.MassKt = massKt;
		return v;
	}

	CollisionVolume CollisionVolume::Hull(const cs::Ref<CombatSpec>& spec, DVec3 pos, DVec3 vel, DVec3 forward, double massKt)
	{
		double length = cs::Math::Max(1e-6, spec->LengthKm);
		double r = length * cs::Math::Max(0.01, cs::Math::Min(1.0, spec->BeamFraction)) * 0.5;
		auto axis = forward.LengthSquared() > 1e-18 ? forward.Normalized() : DVec3::UnitX;
		CollisionVolume v;
		v.Pos = pos; v.Vel = vel; v.Axis = axis;
		v.HalfLengthKm = cs::Math::Max(0.0, length * 0.5 - r); v.RadiusKm = r;
		v.MassKt = cs::Double::IsNaN(massKt) ? Collisions::MassOf(spec) : massKt;
		return v;
	}

	double Collisions::MassOf(const cs::Ref<CombatSpec>& spec)
	{
		return spec->MassKt > 0 ? spec->MassKt : 3500 * cs::Math::Pow(cs::Math::Max(0.005, spec->LengthKm), 3);
	}

	// ---------------------------------------------------------------- detection

	bool Collisions::Contact(const CollisionVolume& a, const CollisionVolume& b, DVec3& normal, double& depthKm, DVec3& point)
	{
		DVec3 pa, pb;
		ClosestPoints(a.Tail(), a.Head(), b.Tail(), b.Head(), pa, pb);
		auto d = pb - pa;
		double dist = d.Length();
		double reach = a.RadiusKm + b.RadiusKm;
		normal = DVec3();
		point = DVec3();
		depthKm = reach - dist;
		if (depthKm <= 0) return false;
		// Centres on top of each other: any direction will do, the line between the bodies if they have one.
		if (dist > 1e-12) normal = d / dist;
		else
		{
			auto c = b.Pos - a.Pos;
			normal = c.LengthSquared() > 1e-24 ? c.Normalized() : DVec3::UnitX;
		}
		point = pa + normal * (a.RadiusKm - depthKm * 0.5);
		return true;
	}

	double Collisions::TimeOfImpact(DVec3 relStart, DVec3 moveB, double reachKm)
	{
		double c = relStart.LengthSquared() - reachKm * reachKm;
		if (c <= 0) return 0;
		double aa = moveB.LengthSquared();
		if (aa < 1e-24) return -1;
		double bb = DVec3::Dot(relStart, moveB);
		if (bb >= 0) return -1; // not closing
		double disc = bb * bb - aa * c;
		if (disc < 0) return -1;
		double t = (-bb - cs::Math::Sqrt(disc)) / aa;
		return t >= 0 && t <= 1 ? t : -1;
	}

	Impact Collisions::Resolve(CollisionVolume& a, CollisionVolume& b)
	{
		DVec3 n, point;
		double depth;
		if (!Contact(a, b, n, depth, point)) return Impact();
		return ResolveContact(a, b, n, depth, point);
	}

	Impact Collisions::ResolveContact(CollisionVolume& a, CollisionVolume& b, DVec3 n, double depth, DVec3 point)
	{
		Impact impact;
		double ia = a.Immovable() ? 0 : 1 / a.MassKt;
		double ib = b.Immovable() ? 0 : 1 / b.MassKt;
		impact.Hit = true;
		impact.Normal = n;
		impact.Point = point;
		impact.DepthKm = depth;
		if (ia + ib <= 0) return impact;

		// Push apart: the one that cannot move (or is pinned to a track) stays put.
		bool aMoves = !a.Immovable() && !a.Pinned, bMoves = !b.Immovable() && !b.Pinned;
		double shareA = aMoves && bMoves ? ia / (ia + ib) : aMoves ? 1 : 0;
		double shareB = aMoves && bMoves ? ib / (ia + ib) : bMoves ? 1 : 0;
		if (aMoves) a.Pos = a.Pos - n * (depth * shareA);
		if (bMoves) b.Pos = b.Pos + n * (depth * shareB);

		auto rel = b.Vel - a.Vel;
		double vn = DVec3::Dot(rel, n);
		if (vn >= 0) return impact; // already parting
		double closing = -vn;
		double e = closing > SafeImpactKmS ? Restitution : 0;
		double invSum = ia + ib;
		double jn = (1 + e) * closing / invSum;
		auto tangential = rel - n * vn;
		double vt = tangential.Length();
		auto j = n * jn;
		if (vt > 1e-15)
		{
			// Friction: enough to stop the slide, but never more than the contact can grip.
			double jt = cs::Math::Min(Friction * jn, vt / invSum);
			j = j - tangential / vt * jt;
		}

		auto va = a.Vel - j * ia;
		auto vb = b.Vel + j * ib;
		auto relAfter = vb - va;
		double mu = 1 / invSum;
		impact.ClosingKmS = closing;
		impact.Impulse = j;
		impact.EnergyTJ = cs::Math::Max(0.0, 0.5 * mu * (rel.LengthSquared() - relAfter.LengthSquared()));
		if (aMoves) a.Vel = va;
		if (bMoves) b.Vel = vb;
		return impact;
	}

	double Collisions::SurfaceImpactShareTJ(double massKt, double sinkKmS)
	{
		return massKt <= 0 || sinkKmS <= 0 ? 0 : 0.5 * (0.5 * massKt * sinkKmS * sinkKmS);
	}

	// ---------------------------------------------------------------- damage

	double Collisions::DamageHp(const cs::Ref<CombatSpec>& spec, double energyShareTJ)
	{
		double m = MassOf(spec);
		double shrugged = 0.5 * m * SafeImpactKmS * SafeImpactKmS * (1 + spec->Armor * 0.1);
		double e = energyShareTJ - shrugged;
		return e <= 0 ? 0 : spec->Hull * e / (m * ToughnessTJPerKt);
	}

	DVec3 Collisions::ToLocal(DVec3 point, DVec3 centre, DVec3 forward, DVec3 up, double lengthKm)
	{
		auto f = forward.Normalized();
		auto u = (up - f * DVec3::Dot(up, f));
		u = u.LengthSquared() > 1e-18 ? u.Normalized() : PilotState::LevelUp(f);
		auto r = DVec3::Cross(f, u);
		auto d = (point - centre) / cs::Math::Max(lengthKm, 1e-9);
		return DVec3(DVec3::Dot(d, r), DVec3::Dot(d, u), DVec3::Dot(d, f));
	}

	DVec3 Collisions::ToWorld(DVec3 local, DVec3 forward, DVec3 up)
	{
		auto f = forward.Normalized();
		auto u = (up - f * DVec3::Dot(up, f));
		u = u.LengthSquared() > 1e-18 ? u.Normalized() : PilotState::LevelUp(f);
		auto r = DVec3::Cross(f, u);
		return r * local.X + u * local.Y + f * local.Z;
	}

	DVec3 Collisions::SurfacePoint(const cs::Ref<CombatSpec>& spec, DVec3 dirLocal)
	{
		auto d = dirLocal.LengthSquared() > 1e-18 ? dirLocal.Normalized() : DVec3(0, -1, 0);
		double r = spec->BeamFraction * 0.5;
		double core = cs::Math::Max(0.0, 0.5 - r);
		return DVec3(0, 0, core * cs::Math::Sign(cs::Math::Abs(d.Z) < 1e-9 ? 0 : d.Z)) + d * r;
	}

	cs::List<double> Collisions::StruckSideWeights(const cs::Ref<ShipCombat>& c, DVec3 local)
	{
		const auto& subs = c->Subsystems;
		auto w = cs::NewArray<double>(subs.Count());
		double core = cs::Math::Max(0.0, 0.5 - c->Spec->BeamFraction * 0.5);
		auto onAxis = DVec3(0, 0, cs::Math::Max(-core, cs::Math::Min(core, local.Z)));
		auto outward = local - onAxis;
		outward = outward.LengthSquared() > 1e-18 ? outward.Normalized() : DVec3(0, 0, local.Z >= 0 ? 1 : -1);
		double total = 0;
		for (int i = 0; i < subs.Count(); i++)
		{
			const auto& s = subs[i];
			// A bridge buried at the centre of the hull (U.SPC-T492) is behind its full thickness: a collision never reaches it.
			if (s->Destroyed() || s->Def->Buried) continue;
			auto p = DVec3(s->Def->X, s->Def->Y, s->Def->Z);
			if (DVec3::Dot(p - onAxis, outward) < 0) continue; // the far side of the hull
			// (C#'s local is `near`; renamed because <windows.h> defines `near` as a macro in a UE unity build.)
			double nearness = 1 - (p - local).Length() / SubsystemReachLengths;
			if (nearness <= 0) continue;
			w[i] = nearness;
			total += nearness;
		}
		if (total > 0) for (int i = 0; i < w.Count(); i++) w[i] /= total;
		return w;
	}

	double Collisions::ApplyDamage(const cs::Ref<ShipCombat>& c, DVec3 local, double hp, double nowSeconds, cs::List<cs::Ref<Subsystem>>* destroyed)
	{
		if (hp <= 0 || !c->Alive()) return 0;
		// The field over the patch of hull struck takes it (U.SPC-T501).
		auto generator = c->EmitterFor(ShieldEmitters::StrikePoint(c->Spec, local));
		if (generator != nullptr)
		{
			generator->LastHitSeconds = nowSeconds;
			double soaked = cs::Math::Min(generator->Shield, hp);
			generator->Shield -= soaked;
			hp -= soaked;
			if (hp <= 0) return 0;
		}

		auto weights = StruckSideWeights(c, local);
		double toSubs = 0;
		for (double w : weights) toSubs += w;
		double subsDamage = toSubs > 0 ? hp * SubsystemShare : 0;
		c->Hull -= hp - subsDamage;
		for (int i = 0; i < weights.Count(); i++)
		{
			if (weights[i] <= 0) continue;
			auto s = c->Subsystems[i];
			s->Hp -= subsDamage * weights[i];
			if (s->Hp > 0) continue;
			c->Hull -= -s->Hp;
			s->Hp = 0;
			if (destroyed) destroyed->Add(s);
			if (s->Kind() == SubsystemKind::Reactor) c->Hull = 0;
			if (s->Kind() == SubsystemKind::ShieldGenerator) s->Shield = 0;
			if (s->Kind() == SubsystemKind::Hangar) c->SetFighters(cs::Math::Max(0, c->Fighters() - 6));
		}
		if (c->Hull < 0) c->Hull = 0;
		return hp;
	}

	// ---------------------------------------------------------------- geometry

	void Collisions::ClosestPoints(DVec3 p1, DVec3 q1, DVec3 p2, DVec3 q2, DVec3& c1, DVec3& c2)
	{
		auto d1 = q1 - p1;
		auto d2 = q2 - p2;
		auto r = p1 - p2;
		double a = d1.LengthSquared(), e = d2.LengthSquared(), f = DVec3::Dot(d2, r);
		double s, t;
		constexpr double eps = 1e-18;
		if (a <= eps && e <= eps)
		{
			c1 = p1;
			c2 = p2;
			return;
		}
		if (a <= eps)
		{
			s = 0;
			t = Clamp01(f / e);
		}
		else
		{
			double c = DVec3::Dot(d1, r);
			if (e <= eps)
			{
				t = 0;
				s = Clamp01(-c / a);
			}
			else
			{
				double b = DVec3::Dot(d1, d2);
				double denom = a * e - b * b;
				s = denom > eps ? Clamp01((b * f - c * e) / denom) : 0;
				t = (b * s + f) / e;
				if (t < 0)
				{
					t = 0;
					s = Clamp01(-c / a);
				}
				else if (t > 1)
				{
					t = 1;
					s = Clamp01((b - c) / a);
				}
			}
		}
		c1 = p1 + d1 * s;
		c2 = p2 + d2 * t;
	}

	double Collisions::Clamp01(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
}
