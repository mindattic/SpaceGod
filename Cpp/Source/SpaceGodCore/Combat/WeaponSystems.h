#pragma once
// Weapon systems (US-T189): no Unity.SpaceGod original. The families every gun, launcher and spinal mount belongs to,
// what each does to a shield and to a hull, how its shot looks, and the sound cue it raises.

#include <functional>

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	enum class SubsystemKind : int;
	enum class ModuleKind : int;
	enum class MissileKind : int;
	enum class ShipClass : int;
	enum class FxKind : int;
	struct CombatFx;
	struct EquipmentSlot;
	class KineticRound;
	class ShipCombat;
	class ShipCatalogEntry;

	/// <summary>
	/// The family a weapon belongs to (US-T189). <see cref="Laser"/>: hitscan beams (turrets, heavy lasers, point defence,
	/// phaser arrays), that strip shields well and burn bare hull poorly. <see cref="Plasma"/>: slow glowing bolts that
	/// splash off a shield but melt hull. <see cref="Missile"/> and <see cref="Torpedo"/>: guided warheads (and dumb-fire
	/// rockets) that flak, interceptors and point defence can shoot down, a torpedo the heavier and the harder on hull.
	/// <see cref="Railgun"/>: fast kinetic slugs, the longest reach of any gun, that punch through armour. <see cref="Flak"/>:
	/// area bursts that hurt every fighter and missile in them. <see cref="Kinetic"/>: autocannons, vulcans, fighter guns
	/// and auto turrets, the light tracer fire. <see cref="Lance"/>: the tachyon lance. <see cref="None"/> is anything else
	/// that does damage (a mine, a collision, a self-destruct, a boarding drill) and takes no profile.
	/// </summary>
	enum class WeaponFamily : int
	{
		None,
		Laser,
		Plasma,
		Missile,
		Torpedo,
		Railgun,
		Flak,
		Kinetic,
		Lance,
	};
	SPACEGODCORE_API const char* ToString(WeaponFamily v);

	/// <summary>How a family's shot is drawn (US-T189): a beam drawn at once along its line, a glowing bolt that crosses the
	/// gap, a guided round with its exhaust trail, a tracer streak, a hypervelocity slug's thin white line, or a burst.</summary>
	enum class ProjectileLook : int { None, Beam, Bolt, Trail, Tracer, Slug, Burst, Lance };
	SPACEGODCORE_API const char* ToString(ProjectileLook v);

	/// <summary>What a weapon cue is for (US-T189): a shot leaving the mount, one landing on bare hull, one taken by a
	/// shield, or a round shot down by the screen.</summary>
	enum class WeaponCueEvent : int { Fire, Impact, ShieldImpact, Intercepted };
	SPACEGODCORE_API const char* ToString(WeaponCueEvent v);

	/// <summary>
	/// What a weapon does to what it strikes (US-T189). Damage the shield over the struck patch takes is the shot's times
	/// <see cref="VsShield"/>; of the shot that gets past it (the share the field did not stop), the hull takes
	/// <see cref="VsHull"/> times. <see cref="ArmourPierce"/> is the share of the hull's armour the shot ignores. The
	/// neutral profile (1, 1, 0) is the battle's damage before weapon systems.
	/// </summary>
	struct DamageProfile
	{
		double VsShield = 1, VsHull = 1, ArmourPierce = 0;

		bool Neutral() const { return VsShield == 1 && VsHull == 1 && ArmourPierce == 0; }
	};

	/// <summary>A sound a weapon raises (US-T189), for whatever plays audio to pick up through
	/// <see cref="WeaponSystems.SoundHook"/>. <see cref="Name"/> is "&lt;family&gt;.&lt;event&gt;" ("laser.fire",
	/// "plasma.impact", "railgun.shield", "missile.intercepted"), a stable key for a sound bank.</summary>
	struct WeaponSoundCue
	{
		WeaponFamily Family{};
		WeaponCueEvent Event{};
		const char* Name = "";

		/// <summary>Where it happened, battle-local km, and how big (a hit's damage; 0 for a shot fired).</summary>
		DVec3 At;
		double Size = 0;

		/// <summary>The side that fired (a shot or an interception) or was hit (an impact), and the battle's id.</summary>
		int Side = 0;
		int BattleId = 0;
	};

	/// <summary>Everything a family is (US-T189): its name, its damage profile, how its shot looks and whether it travels,
	/// is guided and can be shot down.</summary>
	struct WeaponSystemInfo
	{
		WeaponFamily Family{};
		const char* Name = "";
		DamageProfile Profile;
		ProjectileLook Look{};

		/// <summary>Lands the instant it is fired (a beam, a lance) rather than crossing the range.</summary>
		bool Hitscan = false;

		/// <summary>Steers onto its target.</summary>
		bool Guided = false;

		/// <summary>Flak, interceptors and point defence can shoot it down.</summary>
		bool Interceptable = false;

		/// <summary>One line on what it is good for, for a tooltip or the help pages.</summary>
		const char* Role = "";
	};

	/// <summary>
	/// The weapon systems (US-T189): one place that says which family each gun, launcher and spinal mount belongs to and
	/// what that family does to shields and hulls. The battle's figures for each weapon (range, rate of fire, damage,
	/// accuracy, muzzle or missile speed) stay the <see cref="WeaponSpec"/>'s; a weapon is fitted into a hull's hardpoints
	/// through the slot model (<see cref="EquipmentSlot.Accepts"/>, <see cref="Outfitting"/>), never per ship. Damage
	/// profiles are read off the firing mount's kind (or a round's own kind), never stored, so saves are untouched.
	/// Deterministic: nothing here draws on a random stream, and the sound hook only listens.
	/// </summary>
	class SPACEGODCORE_API WeaponSystems final
	{
	public:
		/// <summary>The family of the weapon on a hardpoint of kind <paramref name="k"/>; None for a hardpoint with no gun.</summary>
		static WeaponFamily FamilyOf(SubsystemKind k);

		/// <summary>The family of the weapon module <paramref name="m"/>; None for a module that is not a weapon.</summary>
		static WeaponFamily FamilyOfModule(ModuleKind m);

		/// <summary>The family of a guided round of kind <paramref name="k"/>: a torpedo's, or a missile's (a rocket's and a
		/// hive's swarm too); None for a boarding pod, a Breacher round or an EMP round, which do their own thing.</summary>
		static WeaponFamily FamilyOfRound(MissileKind k);

		/// <summary>The family of a kinetic round in flight: a railgun's slug, else light kinetic fire.</summary>
		static WeaponFamily FamilyOfRound(const KineticRound& r);

		/// <summary>What family <paramref name="f"/> is.</summary>
		static const WeaponSystemInfo& Info(WeaponFamily f);

		static const DamageProfile& ProfileOf(WeaponFamily f) { return Info(f).Profile; }

		/// <summary>Every weapon family, None left out, in the order the help pages list them.</summary>
		static const cs::List<WeaponFamily>& Families();

		// ------------------------------------------------------------ damage

		/// <summary>
		/// A shot of <paramref name="damage"/> of family <paramref name="f"/> meets a shield holding <paramref name="shield"/>:
		/// what the field loses (<paramref name="soaked"/>) and what of the shot goes on past it, in the shot's own terms (the
		/// hull's profile not yet applied). The neutral profile soaks point for point.
		/// </summary>
		static double ThroughShield(WeaponFamily f, double damage, double shield, double& soaked);

		/// <summary>What a shot that got past the shield does to the hull of family <paramref name="f"/>, before armour.</summary>
		static double VsHull(WeaponFamily f, double damage);

		/// <summary>The armour a shot of family <paramref name="f"/> meets, of <paramref name="armour"/>.</summary>
		static double ArmourFaced(WeaponFamily f, double armour);

		// ------------------------------------------------------------ sound

		/// <summary>The listener weapon cues go to: set it (the runtime's audio, once there is one) to hear every shot fired,
		/// landing and shot down in every battle. Empty by default, when no cue is raised at all.</summary>
		static std::function<void(const WeaponSoundCue&)>& SoundHook();

		/// <summary>The stable cue key of <paramref name="f"/>'s <paramref name="e"/> ("laser.fire").</summary>
		static const char* CueName(WeaponFamily f, WeaponCueEvent e);

		/// <summary>Raises a cue to <see cref="SoundHook"/>, if anything is listening.</summary>
		static void Cue(WeaponFamily f, WeaponCueEvent e, DVec3 at, double size, int side, int battleId);

		/// <summary>The cue a battle effect stands for, if it is a shot fired or a round shot down (called from
		/// <see cref="Battle.Emit"/>); the family of a launch or a flak shell is read off the firing mount.</summary>
		static void CueFx(const CombatFx& fx, int battleId);

		/// <summary>The family whose shot fired effect <paramref name="k"/> draws; None for an effect that is no shot fired.</summary>
		static WeaponFamily FamilyOfFx(FxKind k);

		// ------------------------------------------------------------ fitting through the slot model

		/// <summary>Every weapon module slot <paramref name="slot"/> takes on a hull of <paramref name="cls"/> (null: a station),
		/// in catalogue order: what the slot model and the role-bound weapons allow (<see cref="EnergyWeapons.RoleAllows"/>).</summary>
		static cs::List<ModuleKind> Options(const EquipmentSlot& slot, cs::Nullable<ShipClass> cls);

		/// <summary>Whether any weapon slot of the catalog hull <paramref name="e"/> takes a weapon of family <paramref name="f"/>.</summary>
		static bool CanField(const cs::Ref<ShipCatalogEntry>& e, WeaponFamily f);

		/// <summary>The weapon mounts of <paramref name="c"/> still standing, by family (each family's count, None never).</summary>
		static cs::Dictionary<WeaponFamily, int> Armament(const cs::Ref<ShipCombat>& c);
	};
}
