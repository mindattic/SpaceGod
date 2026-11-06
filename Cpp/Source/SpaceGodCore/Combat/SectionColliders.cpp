#include "Combat/SectionColliders.h"

#include <algorithm>
#include <cmath>

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/ModularHull.h"
#include "Combat/Battle.h"
#include "Combat/War.h"
#include "Ephemeris.h"
#include "Infrastructure/Infrastructure.h"
#include "World.h"

namespace SpaceGod::Core
{
	double SectionCollider::SupportAlong(DVec3 n) const
	{
		return std::fabs(DVec3::Dot(Axes[0], n)) * Half.X + std::fabs(DVec3::Dot(Axes[1], n)) * Half.Y + std::fabs(DVec3::Dot(Axes[2], n)) * Half.Z;
	}

	double CollisionBody::SectionHpTotal() const
	{
		double t = 0;
		for (const auto& s : Sections) t += s.MaxHp;
		return t;
	}

	double CollisionBody::BroadRadiusKm() const
	{
		double r = Volume.BoundingRadiusKm();
		for (const auto& s : Sections) r = std::max(r, (s.Centre - Volume.Pos).Length() + s.Half.Length());
		return r;
	}

	std::vector<SectionCollider> SectionColliders::Build(const cs::Ref<ShipCombat>& c, DVec3 pos, DVec3 forward, DVec3 up, double lengthKm)
	{
		std::vector<SectionCollider> out;
		auto h = c->Structure();
		double length = lengthKm >= 0 ? lengthKm : c->Spec->LengthKm, total = 0;
		for (int i = 0; i < h->Modules.Count(); i++) total += h->Modules[i]->MaxStructure;
		if (total <= 0) return out;
		DVec3 right = Collisions::ToWorld(DVec3(1, 0, 0), forward, up);
		DVec3 u = Collisions::ToWorld(DVec3(0, 1, 0), forward, up);
		DVec3 f = Collisions::ToWorld(DVec3(0, 0, 1), forward, up);
		for (int i = 0; i < h->Modules.Count(); i++)
		{
			auto m = h->Modules[i];
			if (m->Detached) continue;
			SectionCollider s;
			s.ModuleId = m->Id;
			s.Centre = pos + Collisions::ToWorld(m->Centre, forward, up) * length;
			s.Axes[0] = right; s.Axes[1] = u; s.Axes[2] = f;
			s.Half = m->HalfExtent * length;
			s.MaxHp = c->Spec->Hull * m->MaxStructure / total;
			s.Hp = s.MaxHp * m->Condition();
			out.push_back(s);
		}
		return out;
	}

	bool SectionColliders::InProximity(const CollisionBody& a, const CollisionBody& b)
	{
		return (a.Volume.Pos - b.Volume.Pos).Length() <= a.BroadRadiusKm() + b.BroadRadiusKm() + ProximityKm;
	}

	bool SectionColliders::BoxBox(const SectionCollider& a, const SectionCollider& b, DVec3& normal, double& depthKm, DVec3& point)
	{
		DVec3 d = b.Centre - a.Centre;
		double best = 1e300;
		DVec3 bestAxis;
		auto test = [&](DVec3 axis) -> bool
		{
			double len = axis.Length();
			if (len < 1e-9) return true; // parallel edges: no axis
			axis = axis / len;
			double dist = DVec3::Dot(d, axis);
			double overlap = a.SupportAlong(axis) + b.SupportAlong(axis) - std::fabs(dist);
			if (overlap <= 0) return false;
			if (overlap < best - 1e-12)
			{
				best = overlap;
				bestAxis = dist < 0 ? -axis : axis;
			}
			return true;
		};
		for (int i = 0; i < 3; i++) if (!test(a.Axes[i])) return false;
		for (int i = 0; i < 3; i++) if (!test(b.Axes[i])) return false;
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				if (!test(DVec3::Cross(a.Axes[i], b.Axes[j]))) return false;
		normal = bestAxis;
		depthKm = best;
		// b's deepest vertex into a (along -normal), halfway back out: on the touching surfaces, wherever the hit is.
		DVec3 v = b.Centre;
		for (int i = 0; i < 3; i++)
		{
			double hb[3] = { b.Half.X, b.Half.Y, b.Half.Z };
			v = v - b.Axes[i] * (hb[i] * (DVec3::Dot(b.Axes[i], bestAxis) >= 0 ? 1.0 : -1.0));
		}
		point = v + bestAxis * (best * 0.5);
		return true;
	}

	bool SectionColliders::BoxSphere(const SectionCollider& box, DVec3 centre, double radiusKm, DVec3& normal, double& depthKm, DVec3& point)
	{
		DVec3 d = centre - box.Centre;
		double l[3] = { DVec3::Dot(d, box.Axes[0]), DVec3::Dot(d, box.Axes[1]), DVec3::Dot(d, box.Axes[2]) };
		double h[3] = { box.Half.X, box.Half.Y, box.Half.Z };
		double q[3];
		bool inside = true;
		for (int i = 0; i < 3; i++)
		{
			q[i] = std::max(-h[i], std::min(h[i], l[i]));
			if (q[i] != l[i]) inside = false;
		}
		DVec3 closest = box.Centre + box.Axes[0] * q[0] + box.Axes[1] * q[1] + box.Axes[2] * q[2];
		if (!inside)
		{
			DVec3 v = centre - closest;
			double dist = v.Length();
			if (dist >= radiusKm) return false;
			normal = v / dist;
			depthKm = radiusKm - dist;
			point = closest;
			return true;
		}
		// Centre inside the box: out through the nearest face (lowest axis on a tie).
		int axis = 0;
		double least = 1e300;
		for (int i = 0; i < 3; i++)
		{
			double gap = h[i] - std::fabs(l[i]);
			if (gap < least - 1e-12) { least = gap; axis = i; }
		}
		double sign = l[axis] < 0 ? -1.0 : 1.0;
		normal = box.Axes[axis] * sign;
		depthKm = least + radiusKm;
		point = centre + normal * (least * 0.5 - radiusKm * 0.5);
		return true;
	}

	/// The point of a section-less body's core segment nearest a box (alternating clamps between segment and box):
	/// the sphere the body stands for against that box.
	static DVec3 NearestOnCore(const CollisionVolume& v, const SectionCollider& box)
	{
		if (v.HalfLengthKm <= 0) return v.Pos;
		DVec3 p = box.Centre;
		DVec3 onSeg = v.Pos;
		for (int it = 0; it < 4; it++)
		{
			double t = std::max(-v.HalfLengthKm, std::min(v.HalfLengthKm, DVec3::Dot(p - v.Pos, v.Axis)));
			onSeg = v.Pos + v.Axis * t;
			DVec3 d = onSeg - box.Centre;
			double l[3] = { DVec3::Dot(d, box.Axes[0]), DVec3::Dot(d, box.Axes[1]), DVec3::Dot(d, box.Axes[2]) };
			double h[3] = { box.Half.X, box.Half.Y, box.Half.Z };
			p = box.Centre;
			for (int i = 0; i < 3; i++) p = p + box.Axes[i] * std::max(-h[i], std::min(h[i], l[i]));
		}
		return onSeg;
	}

	SectionContact SectionColliders::Contact(const CollisionBody& a, const CollisionBody& b)
	{
		SectionContact best;
		// Phase 1: bounding spheres.
		if ((a.Volume.Pos - b.Volume.Pos).Length() > a.BroadRadiusKm() + b.BroadRadiusKm()) return best;
		if (a.Sections.empty() && b.Sections.empty())
		{
			DVec3 n, p;
			double depth;
			if (Collisions::Contact(a.Volume, b.Volume, n, depth, p))
			{
				best.Hit = true; best.Normal = n; best.Point = p; best.DepthKm = depth;
			}
			return best;
		}
		// Phase 2: convex sections.
		auto keep = [&](bool hit, DVec3 n, double depth, DVec3 p, int sa, int sb)
		{
			if (hit && (!best.Hit || depth > best.DepthKm + 1e-12))
			{
				best.Hit = true; best.Normal = n; best.DepthKm = depth; best.Point = p; best.SectionA = sa; best.SectionB = sb;
			}
		};
		DVec3 n, p;
		double depth = 0;
		if (!a.Sections.empty() && !b.Sections.empty())
		{
			for (size_t i = 0; i < a.Sections.size(); i++)
				for (size_t j = 0; j < b.Sections.size(); j++)
				{
					bool hit = BoxBox(a.Sections[i], b.Sections[j], n, depth, p);
					keep(hit, n, depth, p, (int)i, (int)j);
				}
		}
		else if (!a.Sections.empty())
		{
			for (size_t i = 0; i < a.Sections.size(); i++)
			{
				DVec3 c = NearestOnCore(b.Volume, a.Sections[i]);
				bool hit = BoxSphere(a.Sections[i], c, b.Volume.RadiusKm, n, depth, p);
				keep(hit, n, depth, p, (int)i, -1);
			}
		}
		else
		{
			for (size_t j = 0; j < b.Sections.size(); j++)
			{
				DVec3 c = NearestOnCore(a.Volume, b.Sections[j]);
				bool hit = BoxSphere(b.Sections[j], c, a.Volume.RadiusKm, n, depth, p);
				keep(hit, -n, depth, p, -1, (int)j); // box-to-sphere is b-to-a: flip it
			}
		}
		return best;
	}

	double SectionColliders::DamageHp(const CollisionBody& body, double energyShareTJ)
	{
		if (body.HullHp <= 0 || body.Volume.MassKt <= 0) return 0;
		double m = body.Volume.MassKt;
		double shrugged = 0.5 * m * Collisions::SafeImpactKmS * Collisions::SafeImpactKmS * (1 + body.Armor * 0.1);
		double e = energyShareTJ - shrugged;
		return e <= 0 ? 0 : body.HullHp * e / (m * Collisions::ToughnessTJPerKt);
	}

	bool SectionColliders::Strike(CollisionBody& body, int index, double hp)
	{
		if (index < 0 || index >= (int)body.Sections.size() || hp <= 0) return false;
		auto& s = body.Sections[index];
		bool was = s.Destroyed();
		s.Hp = std::max(0.0, s.Hp - hp);
		return !was && s.Destroyed();
	}

	SectionImpact SectionColliders::Resolve(CollisionBody& a, CollisionBody& b)
	{
		SectionImpact out;
		out.IdA = a.Id; out.IdB = b.Id;
		SectionContact c = Contact(a, b);
		if (!c.Hit) return out;
		DVec3 posA = a.Volume.Pos, posB = b.Volume.Pos;
		out.Hit = Collisions::ResolveContact(a.Volume, b.Volume, c.Normal, c.DepthKm, c.Point);
		out.SectionA = c.SectionA; out.SectionB = c.SectionB;
		DVec3 da = a.Volume.Pos - posA, db = b.Volume.Pos - posB;
		for (auto& s : a.Sections) s.Centre = s.Centre + da;
		for (auto& s : b.Sections) s.Centre = s.Centre + db;
		if (out.Hit.EnergyTJ > 0)
		{
			out.HpA = DamageHp(a, out.Hit.EnergyShareTJ());
			out.HpB = DamageHp(b, out.Hit.EnergyShareTJ());
			Strike(a, out.SectionA, out.HpA);
			Strike(b, out.SectionB, out.HpB);
		}
		return out;
	}

	CollisionBody SectionColliders::HullBody(int id, const cs::Ref<ShipCombat>& c, DVec3 pos, DVec3 vel, DVec3 forward, DVec3 up, double lengthKm, bool withSections)
	{
		CollisionBody body;
		body.Id = id;
		auto spec = c->Spec;
		double length = std::max(1e-6, lengthKm);
		double r = length * std::max(0.01, std::min(1.0, spec->BeamFraction)) * 0.5;
		DVec3 axis = forward.LengthSquared() > 1e-18 ? forward.Normalized() : DVec3::UnitX;
		body.Volume.Pos = pos;
		body.Volume.Vel = vel;
		body.Volume.Axis = axis;
		body.Volume.HalfLengthKm = std::max(0.0, length * 0.5 - r);
		body.Volume.RadiusKm = r;
		body.Volume.MassKt = Collisions::MassOf(spec);
		body.HullHp = spec->Hull;
		body.Armor = spec->Armor;
		if (withSections) body.Sections = Build(c, pos, axis, up, length);
		return body;
	}

	static bool CanMove(const CollisionBody& x) { return !x.Volume.Immovable() && !x.Volume.Pinned; }

	static bool CanCollide(const CollisionBody& a, const CollisionBody& b)
	{
		if (!CanMove(a) && !CanMove(b)) return false; // nothing to push apart
		return a.Group == 0 || a.Group != b.Group;
	}

	std::vector<std::pair<int, int>> SectionColliders::Candidates(const std::vector<CollisionBody>& bodies, double padKm)
	{
		int n = (int)bodies.size();
		std::vector<double> r(n);
		std::vector<int> order(n);
		for (int i = 0; i < n; i++) { r[i] = bodies[i].BroadRadiusKm() + padKm * 0.5; order[i] = i; }
		auto lo = [&](int i) { return bodies[i].Volume.Pos.X - r[i]; };
		std::sort(order.begin(), order.end(), [&](int x, int y) { double a = lo(x), b = lo(y); return a < b || (a == b && x < y); });
		std::vector<std::pair<int, int>> pairs;
		for (int k = 0; k < n; k++)
		{
			int i = order[k];
			double hi = bodies[i].Volume.Pos.X + r[i];
			for (int m = k + 1; m < n; m++)
			{
				int j = order[m];
				if (lo(j) > hi) break;
				const auto& a = bodies[i];
				const auto& b = bodies[j];
				if (!CanCollide(a, b)) continue;
				if ((a.Volume.Pos - b.Volume.Pos).Length() > r[i] + r[j]) continue;
				pairs.emplace_back(std::min(i, j), std::max(i, j));
			}
		}
		std::sort(pairs.begin(), pairs.end());
		return pairs;
	}

	std::vector<bool> SectionColliders::NearOthers(const std::vector<CollisionBody>& bodies)
	{
		std::vector<bool> inReach(bodies.size(), false);
		for (const auto& [i, j] : Candidates(bodies, ProximityKm))
		{
			inReach[i] = true;
			inReach[j] = true;
		}
		return inReach;
	}

	int SectionColliders::StrikeStructure(const cs::Ref<ShipCombat>& c, int moduleId, double hp)
	{
		if (c == nullptr || hp <= 0 || c->Spec == nullptr || c->Spec->Hull <= 0) return 0;
		auto h = c->Structure();
		if (moduleId < 0 || moduleId >= h->Modules.Count()) return 0;
		double total = 0;
		for (int i = 0; i < h->Modules.Count(); i++) total += h->Modules[i]->MaxStructure;
		if (total <= 0) return 0;
		auto m = h->Module(moduleId);
		if (m->Detached) return 0;
		// The section's hit points are hull points (Hull * MaxStructure / total); the module's structure is its own.
		auto pieces = h->Strike(m->Centre, hp * total / c->Spec->Hull);
		for (const auto& p : pieces) ModularHulls::ApplyBreak(c, p);
		return pieces.Count();
	}

	DVec3 SectionColliders::BounceOff(DVec3 origin, DVec3 travel, const std::vector<SphereObstacle>& obstacles, double restitution)
	{
		for (const auto& o : obstacles)
		{
			DVec3 d = origin - o.Centre;
			double dist = d.Length();
			if (o.RadiusKm > 0 && dist < o.RadiusKm)
				origin = o.Centre + (dist > 1e-12 ? d / dist : DVec3::UnitX) * o.RadiusKm;
		}
		double length = travel.Length();
		if (length < 1e-12) return origin;
		DVec3 dir = travel / length;
		double first = 1e300;
		int hit = -1;
		for (size_t k = 0; k < obstacles.size(); k++)
		{
			const auto& o = obstacles[k];
			if (o.RadiusKm <= 0) continue;
			DVec3 oc = origin - o.Centre;
			double b = DVec3::Dot(oc, dir);
			double cc = DVec3::Dot(oc, oc) - o.RadiusKm * o.RadiusKm;
			double disc = b * b - cc;
			if (disc < 0) continue;
			double t = -b - std::sqrt(disc);
			if (t < 0 || t > length || t >= first) continue;
			first = t;
			hit = (int)k;
		}
		if (hit < 0) return origin + travel;
		DVec3 at = origin + dir * first;
		DVec3 n = (at - obstacles[hit].Centre) / obstacles[hit].RadiusKm;
		DVec3 out = dir - n * (2 * DVec3::Dot(dir, n));
		return at + out * ((length - first) * restitution);
	}

	std::vector<SphereObstacle> SectionColliders::StationsNear(const cs::Ref<World>& world, DVec3 helioKm, double reachKm, double jd)
	{
		std::vector<SphereObstacle> out;
		if (world == nullptr || world->Ephemeris == nullptr) return out;
		if (world->Infrastructure != nullptr)
			for (const auto& st : world->Infrastructure->Structures)
			{
				double r = std::max(0.01, st->LengthKm * 0.5);
				DVec3 c = st->HelioAt(world->Ephemeris, jd) - helioKm;
				if (c.Length() > reachKm + r) continue;
				out.push_back(SphereObstacle{ c, r });
			}
		if (world->War != nullptr)
			for (const auto& b : world->War->Battles)
			{
				if (b->Over || b->Site != nullptr) continue;
				DVec3 anchor = world->Ephemeris->HelioAt(b->Body, jd) + b->Anchor - helioKm;
				if (anchor.Length() > reachKm + Battle::EscapeKm) continue;
				for (const auto& u : b->Units)
				{
					if (u->Kind != UnitKind::Platform || !u->Present()) continue;
					DVec3 c = anchor + u->Pos;
					if (c.Length() > reachKm + u->Radius()) continue;
					out.push_back(SphereObstacle{ c, u->Radius() });
				}
			}
		return out;
	}

	std::vector<SectionImpact> SectionColliders::Step(std::vector<CollisionBody>& bodies)
	{
		std::stable_sort(bodies.begin(), bodies.end(), [](const CollisionBody& x, const CollisionBody& y) { return x.Id < y.Id; });
		std::vector<SectionImpact> impacts;
		for (int pass = 0; pass < Passes; pass++)
		{
			bool any = false;
			for (const auto& [i, j] : Candidates(bodies, 0))
				{
					auto& a = bodies[i];
					auto& b = bodies[j];
					if (pass == 0)
					{
						SectionImpact r = Resolve(a, b);
						if (!r.Hit.Hit) continue;
						any = true;
						impacts.push_back(r);
						continue;
					}
					// Later passes only finish the separation: no second impulse, no second damage, no second report.
					SectionContact c = Contact(a, b);
					if (!c.Hit) continue;
					any = true;
					CollisionVolume va = a.Volume, vb = b.Volume;
					Collisions::ResolveContact(va, vb, c.Normal, c.DepthKm, c.Point);
					DVec3 da = va.Pos - a.Volume.Pos, db = vb.Pos - b.Volume.Pos;
					a.Volume.Pos = va.Pos; b.Volume.Pos = vb.Pos;
					for (auto& s : a.Sections) s.Centre = s.Centre + da;
					for (auto& s : b.Sections) s.Centre = s.Centre + db;
				}
			if (!any) break;
		}
		return impacts;
	}
}
