#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/TachyonLances.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	enum class ShipClass : int;
	class RangeSetting;
	class Settlement;
	class ShipCombat;
	class WeaponSpec;

	/// <summary>
	/// The tachyon lance (U.SPC-T487, docs/SHIP_ROSTER.md "Weapons by tier"): a capital ship's spinal weapon whose
	/// shot crosses its whole reach at once, so there is no travel time and no lead to lay. It ignores light-speed
	/// delay, and it outranges every other gun. That would change the whole doctrine, so it is held in check: only a
	/// capital hull can carry one, in a bow mount the hull itself must lay on the target (a narrow spinal cone,
	/// <see cref="ConeHalfDeg"/>); it is dear to build and only the greatest yards can build it
	/// (<see cref="YardBuilds"/>); it recharges slowly; and while it charges it draws on the reactor so hard that the
	/// hull's shields recover more slowly (<see cref="ShieldRegenFactor"/>). Kinetics and missiles stay the weapons
	/// of the line. Its tunables are declared settings.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API TachyonLances
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order, so the settings register in the order C# registers them. Every static field
		/// accessor runs it first, as C# runs a type's initialiser on first touching any of its statics.</summary>
		static void RunClassConstructor();

		// ------------------------------------------------------------ settings

		static const cs::Ref<RangeSetting>& Range();

		static const cs::Ref<RangeSetting>& Damage();

		static const cs::Ref<RangeSetting>& Recharge();

		static const cs::Ref<RangeSetting>& PowerDraw();

		static const cs::Ref<RangeSetting>& YardLevel();

		// ------------------------------------------------------------ rules

		/// <summary>The lance's weapon, at the settings' figures.</summary>
		static cs::Ref<WeaponSpec> Spec();

		/// <summary>How far off the keel a lance's narrow cone reaches, deg (U.SPC-T533): spinal, so the hull lays it.</summary>
		static constexpr double ConeHalfDeg = 10;

		/// <summary>The hulls that can carry a lance: the capital ships, the only ones with the power plant to feed it. A new
		/// capital class joins by being added here.</summary>
		static cs::HashSet<ShipClass>& CapitalClasses();

		/// <summary>Whether a hull of <paramref name="cls"/> can carry a lance (a station or satellite, with no class, cannot).</summary>
		static bool CanCarry(cs::Nullable<ShipClass> cls);

		/// <summary>Whether <paramref name="yard"/> can build a lance: a lawful shipyard (not a raider haven's black-market
		/// slips) of at least <see cref="YardLevel"/>.</summary>
		static bool YardBuilds(const cs::Ref<Settlement>& yard);

		/// <summary>The share of its shield recovery a hull keeps: less <see cref="PowerDraw"/> while any lance aboard is
		/// charging (a lance shot away draws nothing).</summary>
		static double ShieldRegenFactor(const cs::Ref<ShipCombat>& c);
	};
}
