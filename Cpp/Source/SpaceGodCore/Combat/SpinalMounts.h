#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/SpinalMounts.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	enum class ModuleKind : int;
	enum class ShipClass : int;
	enum class SubsystemKind : int;
	class IPartSpec;
	class RangeSetting;
	class ShipCombat;
	class Subsystem;
	class SubsystemDef;
	class WeaponSpec;

	/// <summary>
	/// Spinal mounts (U.SPC-T490, docs/SHIP_ROSTER.md "Dreadnought / Battleship"): a battleship is built around one or two
	/// guns as long as the hull itself, laid along the keel with their muzzles at the bow, fed by a large power plant inside
	/// the hull. A spinal mount cannot traverse (<see cref="ConeHalfDeg"/>): the hull lays it on the target. Four weapons go
	/// in one: a spinal railgun (the longest kinetic reach and the hardest slug in the game, on a slow recharge), a coilgun
	/// (a shorter reach, a lighter, slower slug, but cycling twice as fast), a tachyon lance (U.SPC-T487) and a plasma
	/// spinal cannon (an enormous bolt that bleeds off over distance, U.SPC-T488). Each spinal mount draws on a spinal power
	/// plant (<see cref="FacilityKinds.SpinalPlant"/>, a facility inside the hull): with fewer plants than standing mounts
	/// they recharge only at <see cref="Unpowered"/> of the rate. Which weapons go in a spinal mount, and what each becomes
	/// there, is data (<see cref="Register"/>); the yard, the battle and the doctrine (<see cref="RearLine"/>) read it from
	/// here. Every figure is a setting in Settings &gt; Broadsides.
	/// </summary>
	// [SettingsSource]
	class SpinalMounts
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order, then runs the static constructor. Every static field accessor runs it first,
		/// as C# runs a type's initialiser on first touching any of its statics.</summary>
		static void RunClassConstructor();

		// ------------------------------------------------------------ the weapons (before the settings that tune them)

		/// <summary>A spinal railgun: a slug driven down rails as long as the hull, the longest reach of any kinetic gun and a
		/// hit second only to a torpedo's, at twenty-two km/s, on a long recharge.</summary>
		static const cs::Ref<WeaponSpec>& Railgun();

		/// <summary>A spinal coilgun: magnetic coils down the keel throw a lighter, slower slug a little less far, but recharge in
		/// half the time.</summary>
		static const cs::Ref<WeaponSpec>& Coilgun();

		/// <summary>A plasma spinal cannon: a bolt four times a turret plasma cannon's, holding together more than twice as far,
		/// but still a brawler's weapon whose damage bleeds off with distance.</summary>
		static const cs::Ref<WeaponSpec>& Plasma();

		// ------------------------------------------------------------ settings

	private:
		static cs::String Km(double v);
		static cs::String Hp(double v);
		static cs::String Secs(double v);
		static cs::String Percent(double v);

	public:
		static const cs::Ref<RangeSetting>& RailgunRange();

		static const cs::Ref<RangeSetting>& RailgunDamage();

		static const cs::Ref<RangeSetting>& RailgunRecharge();

		static const cs::Ref<RangeSetting>& CoilgunRange();

		static const cs::Ref<RangeSetting>& CoilgunDamage();

		static const cs::Ref<RangeSetting>& CoilgunRecharge();

		static const cs::Ref<RangeSetting>& PlasmaRange();

		static const cs::Ref<RangeSetting>& PlasmaDamage();

		static const cs::Ref<RangeSetting>& PlasmaRecharge();

		static const cs::Ref<RangeSetting>& Unpowered();

		// ------------------------------------------------------------ the mount

		/// <summary>How far off the keel a spinal mount's narrow cone reaches, deg (U.SPC-T533): it cannot traverse, so the hull
		/// lays it.</summary>
		static constexpr double ConeHalfDeg = 10;

		/// <summary>A spinal mount is a gun the length of the hull, built heavier than any turret: its hardpoint takes this many
		/// times a turret's damage before it is shot away.</summary>
		static constexpr double HpFactor = 1.5;

		/// <summary>One weapon that goes in a spinal mount: the hardpoint it becomes, what it fires there and what the yard builds.</summary>
		class Kind final
		{
		public:
			ModuleKind Module;
			SubsystemKind Subsystem;

			/// <summary>What it fires as a spinal mount (read when the mount is built, so it follows the settings).</summary>
			cs::Func<cs::Ref<WeaponSpec>> Weapon;

			/// <summary>What a yard builds: the ship-length version of the module, its bill and mass.</summary>
			cs::Ref<IPartSpec> Part;

			Kind(ModuleKind module, SubsystemKind subsystem, cs::Func<cs::Ref<WeaponSpec>> weapon, cs::Ref<IPartSpec> part);
		};

	private:
		/// <summary>A module built to a spinal mount's length: its name, and its bill and mass that many times over.</summary>
		class SpinalPart;

		static cs::Dictionary<ModuleKind, cs::Ref<Kind>>& kinds();

	public:
		/// <summary>The hulls built around spinal mounts, the only ones that may fit one or a spinal power plant. A new
		/// dreadnought class joins by being added here.</summary>
		static cs::HashSet<ShipClass>& Classes();

		/// <summary>Adds (or replaces) a weapon that a spinal mount takes.</summary>
		static void Register(const cs::Ref<Kind>& kind);

		/// <summary>The spinal version of <paramref name="m"/>, or null if a spinal mount does not take it.</summary>
		static cs::Ref<Kind> Of(ModuleKind m);

		/// <summary>Whether a spinal mount takes <paramref name="m"/>.</summary>
		static bool Takes(ModuleKind m);

		/// <summary>Whether a hull of <paramref name="cls"/> is built around spinal mounts.</summary>
		static bool CanCarry(cs::Nullable<ShipClass> cls);

		/// <summary>What the yard builds for <paramref name="m"/> in a spinal mount.</summary>
		static cs::Ref<IPartSpec> Part(ModuleKind m);

		/// <summary>A spinal mount of <paramref name="m"/> with its muzzle at (x, y, z) ship lengths: down the bow, as long as
		/// the hull, laid by the hull.</summary>
		static cs::Ref<SubsystemDef> Mount(ModuleKind m, double x, double y, double z);

		/// <summary>The spinal mount of <paramref name="m"/> in the place of <paramref name="was"/> (a refit).</summary>
		static cs::Ref<SubsystemDef> Mount(ModuleKind m, const cs::Ref<SubsystemDef>& was);

		// ------------------------------------------------------------ in battle

		/// <summary>A gun that fires a slug down rails or coils (the turret and fighter railguns and the spinal guns): its round
		/// draws as a railgun's, and the hull lays it within the railgun's gimbal.</summary>
		static bool IsSlug(SubsystemKind k);

		/// <summary>A gun the hull itself lays (it swings only within the railgun's narrow gimbal): a railgun, a coilgun, a
		/// tachyon lance or any spinal mount.</summary>
		static bool HullLaid(const cs::Ref<SubsystemDef>& d);

		/// <summary>The spinal mounts on the hull still standing.</summary>
		static int Standing(const cs::Ref<ShipCombat>& c);

		/// <summary>
		/// How fast <paramref name="s"/> recharges against its full rate: 1 for anything but a spinal mount, and for a spinal
		/// mount while the hull has a spinal power plant for every standing mount; with fewer, the plants' share of the mounts,
		/// never below <see cref="Unpowered"/>.
		/// </summary>
		static double ChargeRate(const cs::Ref<ShipCombat>& c, const cs::Ref<Subsystem>& s);

		/// <summary>The reach of the shortest-ranged spinal mount on the hull still standing, km (0 if none): how far off the
		/// hull can stand and still have every spinal mount bear.</summary>
		static double ReachKm(const cs::Ref<ShipCombat>& c);
	};
}
