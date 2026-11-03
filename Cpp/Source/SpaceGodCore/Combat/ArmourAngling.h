#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/ArmourAngling.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class BattleUnit;
	class CombatSpec;
	class RangeSetting;
	struct IncomingFire;

	/// <summary>
	/// Armour angling on the real 3D hull (U.SPC-T534, docs/SHIP_ROSTER.md "Armour angling"). There are no bow, beam and
	/// stern armour faces (they went with the facing arcs, U.SPC-T533): each hit on a capital ship is measured against the
	/// hull's own surface where it actually strikes, the hull's outward normal at the impact point on its real shape (the
	/// capsule its beam wide, <see cref="Collisions.SurfacePoint"/>), found by following the shot's line in to where it meets
	/// the hull. The more slanted the strike, the thicker the plate it has to cross (one over the cosine of the strike, at most
	/// <see cref="MaxObliquity"/>) and the more of it is turned aside, both scaled by <see cref="Strength"/>. So a shot
	/// raking a hull lengthways, or striking the curve of its side or its rounded ends, does less than one square on, and a
	/// captain who keeps its hull slanted to the fire (AI captains by doctrine, <see cref="AiWeight"/>; the weak-patch rolling
	/// weighs it too, <see cref="WeakPatches"/>) takes less while its guns still bear.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API ArmourAngling
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		// ------------------------------------------------------------ settings

		static const cs::Ref<RangeSetting>& Strength();

		static const cs::Ref<RangeSetting>& MaxObliquity();

		static const cs::Ref<RangeSetting>& AiWeight();

		/// <summary>How far either side of a battery's heading an AI captain looks for a slanted one that still bears, deg.</summary>
		static constexpr double SlantDeg = 30;

		// ------------------------------------------------------------ one hit

		/// <summary>
		/// The thicker armour a strike meets and the share of it not turned aside, for a strike whose cosine against the
		/// hull's surface normal is <paramref name="cos"/> (1 square on, 0 grazing): the armour reads one over the cosine,
		/// at most <see cref="MaxObliquity"/>, scaled by <see cref="Strength"/>, and a fully glancing strike loses
		/// <see cref="Strength"/> of the hit.
		/// </summary>
		/// <returns>C#'s (double armour, double through): first is armour, second is through.</returns>
		static std::pair<double, double> Angling(double cos);

		/// <summary>
		/// The angling of a hit on <paramref name="target"/> aimed at battle-local <paramref name="at"/> and travelling along
		/// <paramref name="incoming"/>: measured against the hull's surface where the shot's line meets it
		/// (<see cref="StrikeCos"/>).
		/// </summary>
		/// <returns>C#'s (double armour, double through): first is armour, second is through.</returns>
		static std::pair<double, double> Angling(const cs::Ref<BattleUnit>& target, DVec3 at, DVec3 incoming);

		// ------------------------------------------------------------ the hull's surface

	private:
		static void Shape(const cs::Ref<CombatSpec>& spec, double& r, double& core);

	public:
		/// <summary>The hull's outward surface normal at a point on it (ship lengths, hull frame): the capsule's, straight
		/// out from the keel along its length and rounding over to the bow or stern at its ends.</summary>
		static DVec3 SurfaceNormal(const cs::Ref<CombatSpec>& spec, DVec3 local);

		/// <summary>
		/// Where a shot whose line passes through <paramref name="throughLocal"/> travelling along <paramref name="dirLocal"/>
		/// (both hull frame, ship lengths) first meets the hull's surface, if it does: the nearest entry into the capsule along
		/// that line, its straight length or either rounded end.
		/// </summary>
		static bool Entry(const cs::Ref<CombatSpec>& spec, DVec3 throughLocal, DVec3 dirLocal, DVec3& hit);

		/// <summary>
		/// The cosine of a strike against the hull's surface, 1 square on to 0 grazing, for a shot whose line passes through
		/// <paramref name="throughLocal"/> travelling along <paramref name="dirLocal"/> (hull frame): measured at the point
		/// where the line meets the hull (<see cref="Entry"/>); a line that misses the hull's body (a mount standing proud of
		/// it struck at the edge) is measured against the surface where it was aimed.
		/// </summary>
		static double StrikeCos(const cs::Ref<CombatSpec>& spec, DVec3 throughLocal, DVec3 dirLocal);

		// ------------------------------------------------------------ the face a hull shows the fire

		/// <summary>The share of a hit's harm that gets home through the armour at cosine <paramref name="cos"/>: what is not
		/// turned aside, over how much thicker the plate reads (1 square on, or with angling off).</summary>
		static double HarmAt(double cos);

	private:
		/// <summary>How finely <see cref="Harm(CombatSpec, DVec3)"/> is tabled by the fire's angle to the keel.</summary>
		static constexpr int TableSteps = 64;

		/// <summary>
		/// The mean share of the harm getting home over the face a capsule hull shows fire coming in at an angle to its keel
		/// whose cosine is <paramref name="keelCos"/> (0 square to its beam, 1 straight along it), each part of its surface
		/// weighted by how much of the line of fire it takes up: its straight length and its two rounded ends integrated
		/// finely. The capsule is the same all round its keel, so this is all the slant depends on.
		/// </summary>
		static double FaceHarm(double r, double core, double keelCos);

		static cs::Ref<cs::List<double>> TableOf(const cs::Ref<CombatSpec>& spec);

	public:
		/// <summary>The mean share of a hit's harm that gets home (<see cref="HarmAt"/>) over the face a hull of
		/// <paramref name="spec"/> shows fire from the way <paramref name="towardShooterLocal"/> (hull frame): 1 with angling
		/// off, lower the more slanted the hull lies to the fire.</summary>
		static double Harm(const cs::Ref<CombatSpec>& spec, DVec3 towardShooterLocal);

		/// <summary>The share of the harm getting home of fire from every hostile in <paramref name="fire"/>, each by its
		/// weight, on a hull of <paramref name="spec"/> lying in frame (<paramref name="right"/>, <paramref name="up"/>,
		/// <paramref name="forward"/>).</summary>
		static double Harm(const cs::Ref<CombatSpec>& spec, const cs::List<IncomingFire>& fire, DVec3 right, DVec3 up, DVec3 forward);

		/// <summary>Whether <paramref name="u"/>'s captain weighs slanting its hull to the fire: a capital ship's AI captain
		/// with angling and the doctrine's weight on.</summary>
		static bool AiSlants(const cs::Ref<BattleUnit>& u);
	};
}
