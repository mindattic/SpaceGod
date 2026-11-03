#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/AlphaStrike.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	enum class ShipClass : int;
	class BattleUnit;
	class RangeSetting;
	class ShipCombat;
	class Subsystem;
	class WeaponSpec;

	/// <summary>Where an alpha-striking hull is in its run (U.SPC-T475).</summary>
	enum class StrikePhase : int
	{
		/// <summary>Burning in at its target, every launcher held for the volley.</summary>
		RunIn,

		/// <summary>The volley loosed: flipped and burning back out of the enemy's reach while its launchers reload.</summary>
		Egress,
	};

	SPACEGODCORE_API const char* ToString(StrikePhase v);

	/// <summary>
	/// The frigate's doctrine, the alpha strike (U.SPC-T475, docs/SHIP_ROSTER.md): a fast, lightly armoured missile and
	/// torpedo boat does not trade fire. It burns in at its mark with every launcher held, and only once the mark is inside
	/// the reach of all of them (<see cref="VolleyShare"/>) and every one is loaded does it loose them together, one
	/// devastating volley of anti-ship missiles and antimatter torpedoes; then it flips and burns back out
	/// (<see cref="EgressKm"/>) while they reload, and comes round for another run. Speed and distance are its armour: it
	/// spends most of a fight beyond the guns' reach, and only runs the gauntlet to strike. With its magazines spent it has
	/// nothing left to fight with and withdraws. Which classes fly the doctrine is data (<see cref="Register"/>); the
	/// battle asks <see cref="Flies"/>, <see cref="Holds"/> and <see cref="Steer"/>. Every figure is a setting in the menu's
	/// Broadsides group.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API AlphaStrike
	{
		static cs::String Km(double v);

		static cs::String Percent(double v);

		static cs::String Times(double v);

	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		static const cs::Ref<RangeSetting>& VolleyShare();

		static const cs::Ref<RangeSetting>& EgressKm();

		static const cs::Ref<RangeSetting>& Burn();

		static const cs::Ref<RangeSetting>& CapitalBias();

		// ---------------------------------------------------------------- the magazines

		/// <summary>A frigate's long-range anti-ship missile: further-reaching, quicker and harder-hitting than a warship's
		/// stock missile, from a magazine half again as deep.</summary>
		static const cs::Ref<WeaponSpec>& AntiShipMissile();

		/// <summary>A frigate's antimatter (photon) torpedo: the capitals' warhead (<see cref="WeaponSpec.AntimatterTorpedo"/>),
		/// with twice the rounds in the magazine behind its launcher.</summary>
		static const cs::Ref<WeaponSpec>& MagazineTorpedo();

		// ---------------------------------------------------------------- who flies it

	private:
		static cs::HashSet<ShipClass>& classes();

	public:
		/// <summary>Has hulls of <paramref name="cls"/> fly the alpha strike (or not): how a new strike boat plugs in.</summary>
		static void Register(ShipClass cls, bool flies = true);

		/// <summary>Whether hulls of <paramref name="cls"/> fly the alpha strike.</summary>
		static bool Doctrine(ShipClass cls);

		/// <summary>Whether <paramref name="u"/> fights by the alpha strike now: a hull of a class that flies it, under its
		/// crew's command (the player flies and fires his own), with a working launcher left.</summary>
		static bool Flies(const cs::Ref<BattleUnit>& u);

		/// <summary>A launcher that goes in the volley: a missile or torpedo launcher aimed at hulls (not a screen's
		/// interceptors, an EMP or boarding pods).</summary>
		static bool IsStrikeMount(const cs::Ref<Subsystem>& s);

		/// <summary>The launchers that still have something to fire: standing, with rounds in the magazine.</summary>
		static int StrikeMounts(const cs::Ref<ShipCombat>& c);

		/// <summary>How near its mark <paramref name="u"/> must be to loose its volley, km: <see cref="VolleyShare"/> of its
		/// shortest-ranged working launcher's reach, so every one of them reaches.</summary>
		static double VolleyRangeKm(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether every working launcher on <paramref name="u"/> is loaded.</summary>
		static bool Loaded(const cs::Ref<BattleUnit>& u);

	private:
		/// <summary>How far <paramref name="u"/> is from the surface of its mark, km.</summary>
		static double Gap(const cs::Ref<BattleUnit>& u);

	public:
		/// <summary>Whether <paramref name="u"/>, burning in, is close enough with every launcher loaded to loose the volley.</summary>
		static bool Releases(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="s"/>, one of an alpha-striking hull's launchers, holds its fire this step: always on
		/// the way out, and on the way in until the whole volley can go (<paramref name="release"/>, from <see cref="Releases"/>).</summary>
		static bool Holds(const cs::Ref<Subsystem>& s, bool release);

		/// <summary>The volley is away: <paramref name="u"/> flips and burns back out.</summary>
		static void Loosed(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// Where <paramref name="u"/> wants to go and which way it points this step, flying the alpha strike at its
		/// <see cref="BattleUnit.Target"/> with <paramref name="speed"/> as its cruising speed. Burning in, it drives straight
		/// at the mark nose-on, its launchers bearing; burning out, it turns its tail to the mark and drives away from it,
		/// until it is <see cref="EgressKm"/> off (or near the edge of the fight) with every launcher loaded, and then comes
		/// round for the next run. Returns how hard it burns on this leg: the multiple of its cruising speed and its drive's
		/// push it flies at, its boost as far as <see cref="Burn"/> has it, so the flip sheds its run-in speed quickly.
		/// </summary>
		static double Steer(const cs::Ref<BattleUnit>& u, double speed, DVec3& desired, DVec3& face);

		/// <summary>How much nearer than it is a frigate counts <paramref name="o"/> when it picks its mark, km: a capital
		/// ship is what its torpedoes are for.</summary>
		static double TargetBias(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o);
	};
}
