#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/ScreenWeapons.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	enum class MissileKind : int;
	enum class SubsystemKind : int;
	class Battle;
	class BattleUnit;
	class Missile;
	class RangeSetting;
	class ShipCombat;
	class WeaponSpec;

	/// <summary>
	/// Heavy warheads and the anti-fighter screen (U.SPC-T489, see docs/SHIP_ROSTER.md). Antimatter (photon) torpedoes are
	/// heavy guided warheads at medium range for frigates and capitals; a destroyer's screen of rotary flak cannons and
	/// interceptor missiles, beside its point-defence lasers, is the umbrella that shoots down missile swarms and fighters
	/// before they reach the capitals it escorts. The battle (<see cref="Battle"/>) fires them; this holds what they are
	/// worth, how a screen picks what to shoot and where a screening hull keeps station. Tunables are declared settings.
	/// </summary>
	// [SettingsSource]
	class ScreenWeapons
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		// ------------------------------------------------------------ settings

		static const cs::Ref<RangeSetting>& AntimatterYield();

		static const cs::Ref<RangeSetting>& AntimatterToughness();

		static const cs::Ref<RangeSetting>& FlakBurst();

		static const cs::Ref<RangeSetting>& FlakDamage();

		static const cs::Ref<RangeSetting>& InterceptorReliability();

		static const cs::Ref<RangeSetting>& ScreenStation();

		// ------------------------------------------------------------ fixed figures

		/// <summary>An interceptor's own toughness and how long it flies before it gives up, seconds.</summary>
		static constexpr double InterceptorHp = 8, InterceptorLifeSeconds = 30;

		/// <summary>How near an interceptor's proximity fuze goes off, km, and how far off it looks for another round when
		/// the one it was hunting is gone, km.</summary>
		static constexpr double FuzeKm = 0.12, ReacquireKm = 4;

		/// <summary>How far a screening hull keeps out from its ward's hull toward the threat, km, and how far off a ward
		/// can be for a hull to screen it at all.</summary>
		static constexpr double StandoffKm = 1.5, WardSearchKm = 30;

		// ------------------------------------------------------------ rules

		/// <summary>A screen mount: a flak cannon or an interceptor launcher (point defence lasers are every hull's own).</summary>
		static bool IsScreenMount(SubsystemKind k);

		/// <summary>Whether a hull carries a working screen: a live flak cannon, or an interceptor launcher with rounds left.</summary>
		static bool Screens(const cs::Ref<ShipCombat>& c);

		/// <summary>What a launcher's round flies as.</summary>
		static MissileKind KindOf(SubsystemKind launcher, const cs::Ref<WeaponSpec>& w);

		/// <summary>How much damage a screen must do to a launcher's round to stop it: a capital tube's torpedo is tougher
		/// than a bomber's (U.SPC-T377), an antimatter torpedo tougher still.</summary>
		static double RoundHp(SubsystemKind launcher);

		/// <summary>The warhead a launcher's round carries, with the settings applied.</summary>
		static double Warhead(SubsystemKind launcher, const cs::Ref<WeaponSpec>& w);

		/// <summary>A round a screen shoots at: anything guided at a hull or a small craft, but not another screen's
		/// interceptor (no threat to anything) nor a boarding pod's squad (point defence lasers still take those).</summary>
		static bool IsThreat(const cs::Ref<Missile>& m);

		/// <summary>
		/// How badly a screen wants a hostile round dead: its warhead, more for one closing on a capital ship, and the nearer
		/// it is to what it is homing on the more urgent. A screen takes the worst threat in its reach, not the nearest
		/// round, so the torpedo bound for the battleship goes before the missile bound for a frigate.
		/// </summary>
		static double Threat(const cs::Ref<Missile>& m);

		/// <summary>
		/// The hull a screening ship guards: its flagship if it is the bigger, else the biggest friendly hull at least
		/// half again its size within <see cref="WardSearchKm"/>; null when there is nothing to screen (it is the
		/// biggest there, or alone).
		/// </summary>
		static cs::Ref<BattleUnit> WardOf(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>Where a screening hull keeps station: out from its ward's hull toward the threat by <see cref="StandoffKm"/>,
		/// so what comes at the ward passes through its umbrella first.</summary>
		static DVec3 StationOn(const cs::Ref<BattleUnit>& ward, DVec3 threat);
	};
}
