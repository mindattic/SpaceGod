#pragma once
// Two-phase collision with per-section convex colliders (US-T177, ties to US-T91 / ModularHull).

#include <vector>

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/Collisions.h"

namespace SpaceGod::Core
{
	class ShipCombat;

	/// <summary>One section of a hull as its own convex collider (an oriented box) with its own hit points; a hull's
	/// sections' <see cref="MaxHp"/> sum to its hull strength.</summary>
	struct SPACEGODCORE_API SectionCollider
	{
		int ModuleId = 0;
		DVec3 Centre;
		DVec3 Axes[3];
		DVec3 Half;
		double MaxHp = 0, Hp = 0;

		/// <summary>The section's hit points are gone (it breaks off, US-T91); it stays solid until its wreck piece takes over.</summary>
		bool Destroyed() const { return Hp <= 0; }

		/// <summary>Half the box's extent along unit <paramref name="n"/>.</summary>
		double SupportAlong(DVec3 n) const;
	};

	/// <summary>What a body is to the collision world: its broad-phase volume (sphere or capsule), and, for a ship built
	/// of sections, the convex colliders that take over once another body is within proximity.</summary>
	struct SPACEGODCORE_API CollisionBody
	{
		/// <summary>Stable id: fixes the order pairs are resolved in, so a run is deterministic.</summary>
		int Id = 0;
		CollisionVolume Volume;
		std::vector<SectionCollider> Sections;

		/// <summary>Hull points and armour, for damage; zero hull points means nothing to damage (a rock, a station).</summary>
		double HullHp = 0, Armor = 0;

		double SectionHpTotal() const;
	};

	/// <summary>One contact between two bodies: the pair of sections (-1 for a body with none) and the geometry.</summary>
	struct SPACEGODCORE_API SectionContact
	{
		bool Hit = false;
		DVec3 Normal, Point;
		double DepthKm = 0;
		int SectionA = -1, SectionB = -1;
	};

	/// <summary>What one resolved contact did, for the caller to apply (damage per section, toasts, wrecks).</summary>
	struct SPACEGODCORE_API SectionImpact
	{
		int IdA = 0, IdB = 0;
		int SectionA = -1, SectionB = -1;
		Impact Hit;
		double HpA = 0, HpB = 0;
	};

	class SPACEGODCORE_API SectionColliders
	{
	public:
		/// <summary>Narrow-phase colliders switch on when the broad-phase spheres are this close (km beyond touching),
		/// so a caller builds sections only for bodies inside it.</summary>
		static constexpr double ProximityKm = 0.5;

		/// <summary>Relaxation passes over all pairs per step: a body pushed out of one neighbour is checked against the rest.</summary>
		static constexpr int Passes = 4;

		/// <summary>The colliders of a hull in a pose, from its Structure(): one per module still attached, hit points
		/// shared out by structure so they sum to the design's hull points, scaled by what each has left.</summary>
		static std::vector<SectionCollider> Build(const cs::Ref<ShipCombat>& c, DVec3 pos, DVec3 forward, DVec3 up);

		/// <summary>Whether two bodies are inside each other's proximity radius (narrow phase is due).</summary>
		static bool InProximity(const CollisionBody& a, const CollisionBody& b);

		/// <summary>Box against box (separating axis test): the minimum-penetration axis, normal from a toward b.</summary>
		static bool BoxBox(const SectionCollider& a, const SectionCollider& b, DVec3& normal, double& depthKm, DVec3& point);

		/// <summary>Box against a sphere; normal from the box toward the sphere.</summary>
		static bool BoxSphere(const SectionCollider& box, DVec3 centre, double radiusKm, DVec3& normal, double& depthKm, DVec3& point);

		/// <summary>Phase 1: bounding spheres. Phase 2, once they overlap: every section pair (a body without sections
		/// is its sphere or capsule), the deepest contact kept, the earliest pair winning a tie.</summary>
		static SectionContact Contact(const CollisionBody& a, const CollisionBody& b);

		/// <summary>Resolves a contact: pushes the bodies out (sections ride along with their body) and exchanges the
		/// impulse. The damage each side takes is HpA/HpB, already struck into the section hit.</summary>
		static SectionImpact Resolve(CollisionBody& a, CollisionBody& b);

		/// <summary>Hit points an impact energy share does to a body (see Collisions::DamageHp).</summary>
		static double DamageHp(const CollisionBody& body, double energyShareTJ);

		/// <summary>Takes <paramref name="hp"/> off section <paramref name="index"/>; true if that destroyed it.</summary>
		static bool Strike(CollisionBody& body, int index, double hp);

		/// <summary>Every pair, in id order, relaxed over <see cref="Passes"/> passes: afterwards nothing overlaps (to the
		/// extent pinned and immovable bodies allow). Bodies are reordered by id. Returns the impacts, in resolution order.</summary>
		static std::vector<SectionImpact> Step(std::vector<CollisionBody>& bodies);
	};
}
