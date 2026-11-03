#pragma once
// Port of Unity.SpaceGod Core/Embodiment/MountedTurret.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Subsystem;
	class SubsystemDef;
	enum class SubsystemKind : int;

	/// <summary>The player's hand on one gun mount while it is embodied (see <see cref="MountedTurret"/>).</summary>
	class MountControl final
	{
	public:
		/// <summary>Where the player wants the barrel (unit, ecliptic axes).</summary>
		DVec3 Aim;

		/// <summary>The trigger, held this frame.</summary>
		bool Trigger = false;

		/// <summary>Laid as one of a whole station's guns (<see cref="StationSeat"/>, U.SPC-T447), not as a turret on its own.</summary>
		bool ByStation = false;
	};

	/// <summary>
	/// One gun mount on a hull or station in a battle, as an <see cref="IEmbodiable"/> (U.SPC-T18): a ship's
	/// battery, a defence platform's heavy laser, a fort's point defence. Taken, it traverses toward the
	/// player's aim at its own rate within its own fire cone and fires down its barrel on the trigger, every shot a
	/// real aimed one (see <see cref="Battle.FireManned"/>); every other mount on the hull goes on under its
	/// crew, and the hull itself flies as before. Handed back, its crew works it again.
	/// </summary>
	class SPACEGODCORE_API MountedTurret final : public IEmbodiable
	{
		cs::Ref<IEmbodiable> host;

	public:
		cs::Ref<Core::Battle> Battle;
		cs::Ref<BattleUnit> Unit;
		cs::Ref<Subsystem> Mount;

		MountedTurret(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& unit, const cs::Ref<Subsystem>& mount, const cs::Ref<IEmbodiable>& host = nullptr);

		cs::String Name() const override;
		EmbodimentKind Kind() const override { return EmbodimentKind::Turret; }
		cs::Ref<Core::Faction> Faction() const override;
		cs::Ref<IEmbodiable> Host() const override { return host; }
		bool CanEmbody() const override { return Blocker().empty(); }
		/// <summary>The player holds this mount on its own: one laid as part of a whole station (<see cref="StationSeat"/>) is the station's.</summary>
		bool Embodied() const override;

		cs::String Blocker() const override;

		/// <summary>A direct-fire gun the player can lay: not a launcher, a mine layer, an EMP or a boarding bay.</summary>
		static bool IsMannable(const cs::Ref<SubsystemDef>& d);

		/// <summary>How fast a mount traverses, deg/s: point defence and light guns whip round, heavy lasers swing
		/// slowly, and a spinal railgun barely moves within its gimbal.</summary>
		static double TraverseDegS(SubsystemKind k);

		/// <summary>
		/// Where a mount points at rest (U.SPC-T533): along its cone's axis, in the battle's frame; a turret whose cone is
		/// centred straight up or down off the hull (a dorsal or ventral turret) rests trained fore (aft for one facing
		/// aft) inside that cone instead of pointing at the sky.
		/// </summary>
		static DVec3 RestAim(const cs::Ref<BattleUnit>& u, const cs::Ref<SubsystemDef>& d);

		/// <summary>
		/// <paramref name="aim"/> (unit) brought within where the mount can point: a spinal railgun only within its gimbal of
		/// the keel, any other mount only within its fire cone (U.SPC-T533), swung back onto the cone's edge on the way to it.
		/// </summary>
		static DVec3 Bound(const cs::Ref<BattleUnit>& u, const cs::Ref<SubsystemDef>& d, DVec3 aim);

		EmbodimentPose PoseAt(double jd) override;

		/// <summary>The crew stands aside: the mount is the player's, still pointing where they left it.</summary>
		void TakeCharge() override;

		void Control(const EmbodimentInput& input, double dt) override;

		/// <summary>The crew takes the mount back and picks its own targets again.</summary>
		void HandBack() override;

		bool Equals(const cs::Ref<IEmbodiable>& obj) const override;
		size_t GetHashCode() const override;
	};
}
