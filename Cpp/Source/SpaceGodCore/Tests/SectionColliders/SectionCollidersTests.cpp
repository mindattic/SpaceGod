// SectionCollidersTests: unit checks of the two-phase collision core (US-T177, Combat/SectionColliders.h): the
// box-box and box-sphere narrow phase, the sweep-and-prune broad phase, the deterministic multi-body step, groups,
// immovable walls, the stateless debris bounce, and a hull's sections summing to its hull points.
//
//   SectionCollidersTests.exe     exit code 0 = every check passed, 1 = failures (listed)
//
// Built only by tools/build-core.ps1 (SPACEGOD_STANDALONE); the UE module compiles it to nothing.

#ifdef SPACEGOD_STANDALONE

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Ships.h"
#include "Combat/Collisions.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/ModularHull.h"
#include "Combat/SectionColliders.h"

using namespace SpaceGod::Core;

namespace
{
	int failures = 0, checks = 0;

	void Check(bool ok, const char* what)
	{
		checks++;
		if (ok) return;
		failures++;
		std::printf("FAIL  %s\n", what);
	}

	bool Near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

	SectionCollider Box(DVec3 centre, DVec3 half)
	{
		SectionCollider s;
		s.Centre = centre;
		s.Axes[0] = DVec3::UnitX; s.Axes[1] = DVec3::UnitY; s.Axes[2] = DVec3::UnitZ;
		s.Half = half;
		s.MaxHp = s.Hp = 100;
		return s;
	}

	CollisionBody Ball(int id, DVec3 pos, double r, double massKt, DVec3 vel = DVec3::Zero)
	{
		CollisionBody b;
		b.Id = id;
		b.Volume = CollisionVolume::Sphere(pos, vel, r, massKt);
		return b;
	}

	/// The deepest overlap left between any two bodies that can collide.
	double WorstOverlap(const std::vector<CollisionBody>& bodies)
	{
		double worst = 0;
		for (size_t i = 0; i < bodies.size(); i++)
			for (size_t j = i + 1; j < bodies.size(); j++)
			{
				const auto& a = bodies[i];
				const auto& b = bodies[j];
				if (a.Group != 0 && a.Group == b.Group) continue;
				if (a.Volume.Immovable() && b.Volume.Immovable()) continue; // nothing to push apart
				auto c = SectionColliders::Contact(a, b);
				if (c.Hit) worst = std::max(worst, c.DepthKm);
			}
		return worst;
	}

	void BoxBoxTests()
	{
		DVec3 n, p;
		double depth = 0;
		auto a = Box(DVec3::Zero, DVec3(1, 1, 1));
		auto b = Box(DVec3(1.5, 0, 0), DVec3(1, 1, 1));
		Check(SectionColliders::BoxBox(a, b, n, depth, p), "box-box: overlapping boxes touch");
		Check(Near(depth, 0.5), "box-box: depth is the overlap along the separating axis");
		Check(Near(n.X, 1) && Near(n.Y, 0) && Near(n.Z, 0), "box-box: normal points from a toward b");
		auto c = Box(DVec3(2.5, 0, 0), DVec3(1, 1, 1));
		Check(!SectionColliders::BoxBox(a, c, n, depth, p), "box-box: separated boxes do not touch");
		// A rotated box (45 degrees about z) whose corner reaches into a: the SAT still finds it.
		auto r = Box(DVec3(2.3, 0, 0), DVec3(1, 1, 1));
		double s = std::sqrt(0.5);
		r.Axes[0] = DVec3(s, s, 0); r.Axes[1] = DVec3(-s, s, 0);
		Check(SectionColliders::BoxBox(a, r, n, depth, p), "box-box: a rotated box's corner is caught");
		Check(depth > 0 && depth < 1, "box-box: the rotated overlap is shallow");
	}

	void BoxSphereTests()
	{
		DVec3 n, p;
		double depth = 0;
		auto a = Box(DVec3::Zero, DVec3(1, 1, 1));
		Check(SectionColliders::BoxSphere(a, DVec3(1.5, 0, 0), 1, n, depth, p), "box-sphere: overlapping sphere touches");
		Check(Near(depth, 0.5) && Near(n.X, 1), "box-sphere: depth and normal off the face");
		Check(!SectionColliders::BoxSphere(a, DVec3(3, 0, 0), 1, n, depth, p), "box-sphere: a sphere clear of the box");
		Check(SectionColliders::BoxSphere(a, DVec3(0.8, 0, 0), 0.1, n, depth, p), "box-sphere: a centre inside the box");
		Check(Near(n.X, 1) && Near(depth, 0.3), "box-sphere: inside, out through the nearest face");
	}

	void ContactTests()
	{
		auto a = Ball(1, DVec3::Zero, 1, 1);
		auto b = Ball(2, DVec3(5, 0, 0), 1, 1);
		Check(!SectionColliders::Contact(a, b).Hit, "contact: broad phase rejects distant spheres");
		b.Volume.Pos = DVec3(1.5, 0, 0);
		auto c = SectionColliders::Contact(a, b);
		Check(c.Hit && Near(c.DepthKm, 0.5), "contact: section-less spheres overlap by their radii");
		Check(c.SectionA == -1 && c.SectionB == -1, "contact: no sections, none reported");

		// A body with sections against a sphere: the section struck is named.
		CollisionBody ship;
		ship.Id = 3;
		ship.Volume = CollisionVolume::Sphere(DVec3::Zero, DVec3::Zero, 0.1, 10);
		ship.Sections.push_back(Box(DVec3(-1, 0, 0), DVec3(0.5, 0.5, 0.5)));
		ship.Sections.push_back(Box(DVec3(1, 0, 0), DVec3(0.5, 0.5, 0.5)));
		auto rock = Ball(4, DVec3(1.9, 0, 0), 0.5, 0);
		auto hit = SectionColliders::Contact(ship, rock);
		Check(hit.Hit && hit.SectionA == 1, "contact: the bow section takes the rock");
		Check(Near(hit.DepthKm, 0.1), "contact: depth into the bow section's box");
		Check(SectionColliders::InProximity(ship, Ball(5, DVec3(2.4, 0, 0), 0.5, 0)), "proximity: near but apart is in proximity");
	}

	void StepTests()
	{
		// Two free bodies overlapping: pushed apart, momentum conserved, one impact.
		std::vector<CollisionBody> bodies{ Ball(1, DVec3::Zero, 1, 2, DVec3(0.1, 0, 0)), Ball(2, DVec3(1.5, 0, 0), 1, 2, DVec3(-0.1, 0, 0)) };
		auto impacts = SectionColliders::Step(bodies);
		Check(impacts.size() == 1, "step: one impact for one pair");
		Check(WorstOverlap(bodies) < 1e-9, "step: nothing left overlapping");
		DVec3 p = bodies[0].Volume.Vel * 2 + bodies[1].Volume.Vel * 2;
		Check(p.Length() < 1e-12, "step: momentum between free bodies conserved");
		Check(bodies[0].Volume.Vel.X <= 0 && bodies[1].Volume.Vel.X >= 0, "step: closing bodies part");

		// An immovable wall: the free body is pushed all the way out, the wall stays.
		std::vector<CollisionBody> wall{ Ball(1, DVec3::Zero, 1, 0), Ball(2, DVec3(1.2, 0, 0), 0.5, 1) };
		SectionColliders::Step(wall);
		Check(Near(wall[0].Volume.Pos.Length(), 0), "step: an immovable body does not move");
		Check(Near((wall[1].Volume.Pos - wall[0].Volume.Pos).Length(), 1.5, 1e-9), "step: a free body is pushed out of a wall");

		// Two immovable bodies are left alone (nothing to push).
		std::vector<CollisionBody> rocks{ Ball(1, DVec3::Zero, 1, 0), Ball(2, DVec3(0.5, 0, 0), 1, 0) };
		Check(SectionColliders::Step(rocks).empty(), "step: no pair of immovables");

		// A group never collides with itself.
		std::vector<CollisionBody> wing{ Ball(1, DVec3::Zero, 1, 1), Ball(2, DVec3(0.5, 0, 0), 1, 1) };
		wing[0].Group = wing[1].Group = 7;
		Check(SectionColliders::Step(wing).empty(), "step: bodies of one group pass through each other");
		Check(Near(wing[1].Volume.Pos.X, 0.5), "step: a grouped body is not pushed");
	}

	std::vector<CollisionBody> Crowd(uint32_t seed, int n)
	{
		std::vector<CollisionBody> out;
		uint32_t h = seed;
		auto next = [&]() { h = ModularHull::Hash(h + 0x9E3779B9u); return (h & 0xFFFF) / 65535.0; };
		for (int i = 0; i < n; i++)
		{
			DVec3 pos(next() * 10, next() * 10, next() * 10);
			DVec3 vel((next() - 0.5) * 0.2, (next() - 0.5) * 0.2, (next() - 0.5) * 0.2);
			out.push_back(Ball(i + 1, pos, 0.3 + next() * 0.6, i % 7 == 0 ? 0 : 1 + next() * 5, vel));
		}
		return out;
	}

	void DeterminismTests()
	{
		auto a = Crowd(12345, 60);
		auto b = Crowd(12345, 60);
		// The same bodies handed over in another order end up exactly the same: the step sorts by id.
		std::vector<CollisionBody> c(b.rbegin(), b.rend());
		auto ia = SectionColliders::Step(a);
		auto ic = SectionColliders::Step(c);
		bool same = a.size() == c.size() && ia.size() == ic.size();
		for (size_t i = 0; same && i < a.size(); i++)
			same = a[i].Id == c[i].Id && std::memcmp(&a[i].Volume.Pos, &c[i].Volume.Pos, sizeof(DVec3)) == 0
				&& std::memcmp(&a[i].Volume.Vel, &c[i].Volume.Vel, sizeof(DVec3)) == 0;
		for (size_t i = 0; same && i < ia.size(); i++) same = ia[i].IdA == ic[i].IdA && ia[i].IdB == ic[i].IdB;
		Check(same, "determinism: input order does not change the result, bit for bit");
		Check(!ia.empty(), "determinism: the crowd had contacts to resolve");
		// Each pair is struck (impulse, damage) once a step.
		bool once = true;
		for (size_t i = 0; i < ia.size(); i++)
			for (size_t j = i + 1; j < ia.size(); j++)
				if (ia[i].IdA == ia[j].IdA && ia[i].IdB == ia[j].IdB) once = false;
		Check(once, "step: one impact per pair per step");
		// A few steps relax a crowd apart.
		double start = WorstOverlap(Crowd(12345, 60));
		for (int k = 0; k < 6; k++) SectionColliders::Step(a);
		double left = WorstOverlap(a);
		Check(left < 1e-6 || left < start * 0.5, "step: a crowd relaxes apart over a few steps");
		auto inReach = SectionColliders::NearOthers(a);
		Check(inReach.size() == a.size(), "proximity: one flag per body");
	}

	void BounceTests()
	{
		std::vector<SphereObstacle> station{ SphereObstacle{ DVec3(5, 0, 0), 1 } };
		DVec3 miss = SectionColliders::BounceOff(DVec3::Zero, DVec3(0, 3, 0), station, 0.5);
		Check(Near(miss.Y, 3) && Near(miss.X, 0), "bounce: a path that misses goes on unchanged");
		DVec3 hit = SectionColliders::BounceOff(DVec3::Zero, DVec3(8, 0, 0), station, 0.5);
		// Strikes the surface at x = 4 with 4 km to go, comes back at half that.
		Check(Near(hit.X, 2) && Near(hit.Y, 0), "bounce: a head-on path comes straight back, at the restitution");
		Check((hit - station[0].Centre).Length() >= 1 - 1e-12, "bounce: the piece ends outside the station");
		DVec3 inside = SectionColliders::BounceOff(DVec3(5.5, 0, 0), DVec3::Zero, station, 0.5);
		Check(Near(inside.X, 6), "bounce: a start inside a station is put on its surface");
		DVec3 again = SectionColliders::BounceOff(DVec3::Zero, DVec3(8, 0, 0), station, 0.5);
		Check(std::memcmp(&hit, &again, sizeof(DVec3)) == 0, "bounce: stateless, the same path bounces the same way");
	}

	void HullTests()
	{
		auto spec = CombatSpec::Of(ShipClass::Cruiser);
		auto c = cs::New<ShipCombat>(spec);
		auto sections = SectionColliders::Build(c, DVec3::Zero, DVec3::UnitX, DVec3::UnitZ, spec->LengthKm);
		Check(!sections.empty(), "hull: a cruiser has sections");
		double total = 0;
		for (const auto& s : sections) total += s.MaxHp;
		Check(Near(total, spec->Hull, 1e-6 * spec->Hull), "hull: the sections' hit points sum to the hull's");
		// Both scales: the same hull built twice as long has its boxes twice as far out and twice the size.
		auto twice = SectionColliders::Build(c, DVec3::Zero, DVec3::UnitX, DVec3::UnitZ, spec->LengthKm * 2);
		bool scaled = twice.size() == sections.size();
		for (size_t i = 0; scaled && i < sections.size(); i++)
			scaled = Near(twice[i].Half.Length(), sections[i].Half.Length() * 2, 1e-9) && Near(twice[i].Centre.Length(), sections[i].Centre.Length() * 2, 1e-9);
		Check(scaled, "hull: sections scale with the length the caller gives");
		auto body = SectionColliders::HullBody(1, c, DVec3::Zero, DVec3::Zero, DVec3::UnitX, DVec3::UnitZ, spec->LengthKm, true);
		Check(body.Sections.size() == sections.size() && body.HullHp == spec->Hull, "hull: a hull body carries its sections and hull points");
		Check(body.BroadRadiusKm() >= spec->LengthKm * 0.5 - 1e-9, "hull: the broad sphere holds the whole hull");

		// Collision damage written back into the structure: the module struck loses structure.
		auto h = c->Structure();
		int id = sections[0].ModuleId;
		double before = h->Module(id)->Structure;
		SectionColliders::StrikeStructure(c, id, spec->Hull * 0.01);
		Check(h->Module(id)->Structure < before, "structure: a section's damage lands on its module");
		// Enough to shatter it: a non-root module comes away.
		int loose = -1;
		for (const auto& s : sections) if (s.ModuleId != h->Root) { loose = s.ModuleId; break; }
		if (loose >= 0)
		{
			SectionColliders::StrikeStructure(c, loose, spec->Hull * 10);
			Check(h->Module(loose)->Detached, "structure: a shattered section breaks away (US-T91)");
			auto after = SectionColliders::Build(c, DVec3::Zero, DVec3::UnitX, DVec3::UnitZ, spec->LengthKm);
			Check(after.size() < sections.size(), "structure: a section that came away is no longer a collider");
		}
	}
}

int main()
{
	BoxBoxTests();
	BoxSphereTests();
	ContactTests();
	StepTests();
	DeterminismTests();
	BounceTests();
	HullTests();
	std::printf("SectionCollidersTests: %d check(s), %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

#endif
