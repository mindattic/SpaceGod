#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/FireCones.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	enum class ModuleKind : int;
	enum class SubsystemKind : int;
	class CombatSpec;
	class RangeSetting;
	class SubsystemDef;
	class ToggleSetting;

	/// <summary>
	/// Every weapon hardpoint aims within a cone, not a facing arc (U.SPC-T533, docs/SHIP_ROSTER.md "Fire cones"). Everything
	/// fights in full 3D, so a gun bears on whatever lies inside its cone: a direction on the hull (its axis, by default the
	/// mount's own normal, the way the hull's surface faces where it sits) and a half-angle it can traverse within, above or
	/// below the hull as well as to the side. A turret has a wide cone round its normal; a spinal mount or a fixed tube a
	/// narrow one along the keel. The half-angle is a datum on the mount's <see cref="ModuleSpec"/>
	/// (<see cref="ModuleSpec.ConeHalfDeg"/>) unless the hull frame gives the hardpoint its own
	/// (<see cref="SubsystemDef.ConeHalfDeg"/>, <see cref="SubsystemDef.ConeAxis"/>), and the settings here scale every cone.
	/// Battery orders, the AI captain's choice of which guns to bring to bear, the player's fire control and the turrets a
	/// player mans all work from the cones.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API FireCones
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		// ------------------------------------------------------------ settings

		static const cs::Ref<RangeSetting>& TurretTraverse();

		static const cs::Ref<RangeSetting>& FixedCone();

		static const cs::Ref<ToggleSetting>& AiBringsGunsToBear();

		// ------------------------------------------------------------ hull-frame directions (right, up, forward)

		static inline const DVec3 Bow = DVec3(0, 0, 1);
		static inline const DVec3 Stern = DVec3(0, 0, -1);
		static inline const DVec3 Starboard = DVec3(1, 0, 0);
		static inline const DVec3 Port = DVec3(-1, 0, 0);
		static inline const DVec3 Dorsal = DVec3(0, 1, 0);
		static inline const DVec3 Ventral = DVec3(0, -1, 0);

		/// <summary>The beam a hull is reckoned with when its design is not to hand, as a share of its length.</summary>
		static constexpr double DefaultBeamFraction = 0.2;

		// ------------------------------------------------------------ what a mount is

		/// <summary>
		/// A mount laid along the keel rather than on its own turret: a spinal mount, a fixed tube or launcher (the parts a
		/// <see cref="ModuleSpec.Spinal"/> module is), a fighter's fixed guns, a bomber's slung torpedo or a drill head.
		/// </summary>
		static bool IsFixed(const cs::Ref<SubsystemDef>& d);

		/// <summary>The module a hardpoint kind is, where a yard fits one, or null for the parts of a hull no yard refits.</summary>
		static cs::Nullable<ModuleKind> ModuleOf(SubsystemKind k);

		/// <summary>The half-angle a mount of <paramref name="k"/> is built with, deg: its module's
		/// (<see cref="ModuleSpec.ConeHalfDeg"/>), or for the parts no yard fits, their own (a fighter's fixed guns and a
		/// bomber's torpedo along the keel, pods and decoys thrown anywhere).</summary>
		static double DesignHalfDeg(SubsystemKind k);

		/// <summary>
		/// How far off its axis <paramref name="d"/> can bear, deg (0-180): the hull frame's own figure for the hardpoint, else
		/// its module's, scaled by <see cref="FixedCone"/> for a fixed mount and <see cref="TurretTraverse"/> for a turret.
		/// </summary>
		static double HalfDeg(const cs::Ref<SubsystemDef>& d);

		/// <summary>
		/// The direction <paramref name="d"/> aims about, unit, in the hull frame (right, up, forward): the hull frame's own
		/// axis for the hardpoint if it gives one; along the keel for a fixed mount (astern if it faces aft); else the
		/// mount's normal, the way the hull's surface faces where it sits (<see cref="MountNormal"/>).
		/// </summary>
		static DVec3 Axis(const cs::Ref<SubsystemDef>& d, double beamFraction = DefaultBeamFraction);

		static DVec3 Axis(const cs::Ref<SubsystemDef>& d, const cs::Ref<CombatSpec>& spec);

		/// <summary>
		/// The outward normal of a hull at a point on it (ship lengths, hull frame): the hull is a capsule its beam wide
		/// (<see cref="Collisions.SurfacePoint"/>), so the normal points straight out from the keel along its length and
		/// rounds over to the bow or stern at its ends. A point on the keel line itself, where no side is nearer than any
		/// other, faces up (or along the keel past the capsule's straight length).
		/// </summary>
		static DVec3 MountNormal(double x, double y, double z, double beamFraction = DefaultBeamFraction, bool facesAft = false);

		// ------------------------------------------------------------ bearing

		/// <summary>Whether a direction in the hull frame (right, up, forward; any length) lies inside <paramref name="d"/>'s cone.</summary>
		static bool Covers(const cs::Ref<SubsystemDef>& d, const cs::Ref<CombatSpec>& spec, DVec3 dirLocal);

		/// <summary>How far a direction in the hull frame lies off <paramref name="d"/>'s axis, deg (0-180).</summary>
		static double OffAxisDeg(const cs::Ref<SubsystemDef>& d, const cs::Ref<CombatSpec>& spec, DVec3 dirLocal);

		/// <summary>How far past the edge of <paramref name="d"/>'s cone a direction in the hull frame lies, deg: 0 inside it.</summary>
		static double OutsideDeg(const cs::Ref<SubsystemDef>& d, const cs::Ref<CombatSpec>& spec, DVec3 dirLocal);

		/// <summary>Whether two mounts' cones overlap: some direction lies inside both (their axes closer than their half-angles together).</summary>
		static bool Overlap(const cs::Ref<SubsystemDef>& a, const cs::Ref<SubsystemDef>& b, const cs::Ref<CombatSpec>& spec);

		/// <summary>
		/// A direction (unit) brought within <paramref name="d"/>'s cone, both in the same frame as <paramref name="axis"/>:
		/// unchanged inside it, else swung back onto the cone's edge on the way to it.
		/// </summary>
		static DVec3 Clamp(DVec3 dir, DVec3 axis, double halfDeg);
	};
}
