#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/EnergyWeapons.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	enum class FxKind : int;
	enum class ModuleKind : int;
	enum class ShipClass : int;
	enum class SubsystemKind : int;
	class Battle;
	class BattleUnit;
	class RangeSetting;
	class ShipCombat;
	class Subsystem;
	class ToggleSetting;
	class WeaponSpec;

	// Phaser-style beam arrays and plasma cannons (U.SPC-T488, docs/SHIP_ROSTER.md "Weapons by tier"). A phaser array is
	// the versatile medium-range energy weapon of destroyers and cruisers, and its captain tunes it in battle; a plasma
	// cannon is the brawler's and gate breaker's short-range gun, devastating up close, its bolt bleeding off over distance.

	/// <summary>
	/// How a hull's phaser arrays are tuned (U.SPC-T488): every array on the hull at once, by its captain, at any moment of a
	/// fight. The tuning model is beam shape. A <see cref="Lance"/> is one narrow, high-power beam at medium range for burning
	/// hulls; a <see cref="Sweep"/> spreads the same power into a wide, rapid, low-power fan at short range that works like
	/// point defence, picking off missiles and fighters first.
	/// </summary>
	enum class PhaserTuning : int
	{
		Lance,
		Sweep,
	};

	SPACEGODCORE_API const char* ToString(PhaserTuning v);

	/// <summary>
	/// The phaser arrays' and plasma cannons' figures, every tunable a declared setting (Settings &gt; Weapons), the beam
	/// tuning AI captains choose, and the hulls each weapon is fitted to by role.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API EnergyWeapons
	{
		// ------------------------------------------------------------ settings

		static cs::String Km(double v);
		static cs::String Hp(double v);

	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order, then runs the static constructor. Every static field accessor runs it first,
		/// as C# runs a type's initialiser on first touching any of its statics.</summary>
		static void RunClassConstructor();

		static const cs::Ref<RangeSetting>& LanceDamage();

		static const cs::Ref<RangeSetting>& LanceRange();

		static const cs::Ref<RangeSetting>& SweepDamage();

		static const cs::Ref<RangeSetting>& SweepRange();

		static const cs::Ref<ToggleSetting>& AiTunesPhasers();

		static const cs::Ref<RangeSetting>& PlasmaDamage();

		static const cs::Ref<RangeSetting>& PlasmaRange();

		static const cs::Ref<RangeSetting>& PlasmaEdgeShare();

		// ------------------------------------------------------------ the weapons

		/// <summary>A phaser array tuned to a narrow lance: a hull-burning beam at medium range, the default tuning and what a
		/// <see cref="SubsystemKind.PhaserArray"/>'s hardpoint carries as its own weapon.</summary>
		static const cs::Ref<WeaponSpec>& PhaserLance();

		/// <summary>A phaser array tuned to a wide sweep: a fast, low-power fan that engages missiles and small craft first.</summary>
		static const cs::Ref<WeaponSpec>& PhaserSweep();

		/// <summary>A plasma cannon: a short-ranged bolt that hits harder than any gun but a torpedo up close and bleeds off with distance.</summary>
		static const cs::Ref<WeaponSpec>& PlasmaCannon();

	private:
		/// <summary>Writes the settings into the shared weapon figures, so every fitted array and cannon follows the menu at once.</summary>
		static void Apply();

	public:
		// ------------------------------------------------------------ tuning

		/// <summary>The weapon a hardpoint fires as it stands: a phaser array's tuned beam, otherwise its own.</summary>
		static cs::Ref<WeaponSpec> Effective(const cs::Ref<Subsystem>& s, const cs::Ref<ShipCombat>& c);

		/// <summary>Whether the hull carries a working phaser array.</summary>
		static bool HasPhasers(const cs::Ref<ShipCombat>& c);

		/// <summary>The captain's other tuning: the player's order at the helm or in the seat.</summary>
		static PhaserTuning Retune(const cs::Ref<ShipCombat>& c);

		static cs::String Label(PhaserTuning t);

		/// <summary>Small craft or missiles hostile to the hull, within the sweep's reach of it, that make an AI captain sweep:
		/// two at once, or one with no hull in the lance's reach to burn instead.</summary>
		static constexpr int SweepThreats = 2;

		/// <summary>
		/// An AI captain's tuning for this moment (<see cref="AiTunesPhasers"/>): a wide sweep while fighters or missiles are
		/// closing within its reach (a little beyond, so it is ready when they arrive), a lance otherwise. The player's own
		/// hull is left as the player tuned it.
		/// </summary>
		static PhaserTuning Choose(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>Retunes an AI-flown hull's arrays for the fight as it stands, each step before its guns fire.</summary>
		static void Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>The effect a shot from this hardpoint draws, or null for the battle's usual beam, gun or slug.</summary>
		static cs::Nullable<FxKind> FxOf(SubsystemKind k, bool hit);

		// ------------------------------------------------------------ fitting by role

	private:
		/// <summary>
		/// The hull classes each role-bound weapon is fitted to (docs/SHIP_ROSTER.md): phaser arrays to destroyers and cruisers,
		/// plasma cannons to the brawlers (the battleship's line and the breacher's assault) and the gate breaker's siege
		/// batteries (U.SPC-T478). A module not listed fits any hull with a slot that takes it. A new class joins a role with
		/// <see cref="Allow"/>.
		/// </summary>
		static cs::Dictionary<ModuleKind, cs::HashSet<ShipClass>>& Roles();

	public:
		/// <summary>Lets hulls of <paramref name="cls"/> take <paramref name="module"/> (a new class taking up a weapon's role).</summary>
		static void Allow(ModuleKind module, ShipClass cls);

		/// <summary>Whether a hull of <paramref name="cls"/> (null: a station or satellite) may carry <paramref name="module"/>.</summary>
		static bool RoleAllows(ModuleKind module, cs::Nullable<ShipClass> cls);
	};
}
