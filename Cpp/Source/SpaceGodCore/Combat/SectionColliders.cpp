#include "Combat/SectionColliders.h"

#include <algorithm>
#include <cmath>

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/ModularHull.h"

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

	std::vector<SectionCollider> SectionColliders::Build(const cs::Ref<ShipCombat>& c, DVec3 pos, DVec3 forward, DVec3 up)
	{
		std::vector<SectionCollider> out;
		auto h = c->Structure();
		double length = c->Spec->LengthKm, total = 0;
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
		return (a.Volume.Pos - b.Volume.Pos).Length() <= a.Volume.BoundingRadiusKm() + b.Volume.BoundingRadiusKm() + ProximityKm;
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
		point = a.Centre + bestAxis * a.SupportAlong(bestAxis) - bestAxis * (best * 0.5);
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

	/// The point of a section-less body's core segment nearest a point: the sphere it stands for against a box.
	static DVec3 NearestOnCore(const CollisionVolume& v, DVec3 to)
	{
		if (v.HalfLengthKm <= 0) return v.Pos;
		double t = std::max(-v.HalfLengthKm, std::min(v.HalfLengthKm, DVec3::Dot(to - v.Pos, v.Axis)));
		return v.Pos + v.Axis * t;
	}

	SectionContact SectionColliders::Contact(const CollisionBody& a, const CollisionBody& b)
	{
		SectionContact best;
		// Phase 1: bounding spheres.
		if ((a.Volume.Pos - b.Volume.Pos).Length() > a.Volume.BoundingRadiusKm() + b.Volume.BoundingRadiusKm()) return best;
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
				DVec3 c = NearestOnCore(b.Volume, a.Sections[i].Centre);
				bool hit = BoxSphere(a.Sections[i], c, b.Volume.RadiusKm, n, depth, p);
				keep(hit, n, depth, p, (int)i, -1);
			}
		}
		else
		{
			for (size_t j = 0; j < b.Sections.size(); j++)
			{
				DVec3 c = NearestOnCore(a.Volume, b.Sections[j].Centre);
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

	std::vector<SectionImpact> SectionColliders::Step(std::vector<CollisionBody>& bodies)
	{
		std::stable_sort(bodies.begin(), bodies.end(), [](const CollisionBody& x, const CollisionBody& y) { return x.Id < y.Id; });
		std::vector<SectionImpact> impacts;
		for (int pass = 0; pass < Passes; pass++)
		{
			bool any = false;
			for (size_t i = 0; i < bodies.size(); i++)
				for (size_t j = i + 1; j < bodies.size(); j++)
				{
					SectionImpact r = Resolve(bodies[i], bodies[j]);
					if (!r.Hit.Hit) continue;
					any = true;
					impacts.push_back(r);
				}
			if (!any) break;
		}
		return impacts;
	}
}
