#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/ShieldEmitters.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class CombatSpec;
	class RangeSetting;
	class ShipCombat;
	class Subsystem;

	/// <summary>
	/// Shields come from emitters on the hull, not facing arcs (U.SPC-T501, docs/SHIP_ROSTER.md "Shields"). Everything
	/// fights in full 3D, so a hull's shield is the fields of the emitters mounted on it, each a hardpoint like a turret
	/// (<see cref="SubsystemKind.ShieldGenerator"/>) projecting its own field over the patch of hull around it: the part of
	/// the hull nearer to it than to any other emitter. A shot drains the field of the patch it actually strikes in 3D;
	/// a field shot away, or its emitter destroyed, leaves that patch bare while the rest of the hull stays covered, so a
	/// ship can be holed in one place and protected everywhere else. Gunners (AI crews, and a player's crews at the helm
	/// unless told otherwise) favour hardpoints in a weakened patch (<see cref="FocusWeight"/>), and the player's own aim
	/// hits whichever patch it lands on. A capital ship's emergency repair crews, its damage control teams, bring a
	/// shot-away emitter or gun mount back given time (<see cref="Broadsides.StepDamageControl"/>). Every rate is a setting.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API ShieldEmitters
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		// ------------------------------------------------------------ settings

		static const cs::Ref<RangeSetting>& RegenDelay();

		static const cs::Ref<RangeSetting>& RegenRate();

		static const cs::Ref<RangeSetting>& FocusFire();

		static const cs::Ref<RangeSetting>& EmergencyRepairRate();

		static const cs::Ref<RangeSetting>& EmergencyRepairWork();

		static const cs::Ref<RangeSetting>& RestoredHealth();

		// ------------------------------------------------------------ patches

		/// <summary>
		/// Where on the hull a strike at <paramref name="local"/> (ship lengths, hull frame) meets the shield: the point
		/// itself if it is on or in the hull; a point out beyond the hull is brought in to the hull's surface in its
		/// direction (a blast, a missile's entry, a craft off the beam); and one at the hull's very centre (a shot at the
		/// hull as a whole) is put on the surface facing back along <paramref name="incomingLocal"/>, the way it came.
		/// </summary>
		static DVec3 StrikePoint(const cs::Ref<CombatSpec>& spec, DVec3 local, cs::Nullable<DVec3> incomingLocal = std::nullopt);

		/// <summary>The live field strength of the patch a hull point lies in, 0 (holed: its emitter shot away or its field
		/// down) to 1 (full). A hull with no emitters at all reads 1: it is bare everywhere, with no weaker spot to find.</summary>
		static double PatchStrength(const cs::Ref<ShipCombat>& c, DVec3 local);

		/// <summary>
		/// How much more a gunner wants <paramref name="s"/> for the field over it: 1 under a full field, up to
		/// 1 + <see cref="FocusFire"/> in a patch holed outright, so fire goes where the hull is already bare.
		/// </summary>
		static double FocusWeight(const cs::Ref<ShipCombat>& c, const cs::Ref<Subsystem>& s);

		/// <summary>Whether a shot-away mount is one emergency repair crews can bring back: a shield emitter, a nanite repair
		/// system on a shield mount (US-T102) or a gun mount.</summary>
		static bool Restorable(const cs::Ref<Subsystem>& s);
	};
}
