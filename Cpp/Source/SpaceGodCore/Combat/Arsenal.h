#pragma once
// The full arsenal (US-T101): no Unity.SpaceGod original. The rules the vulcan rotary cannon, the hive missile and the
// dumb-fire rocket add to Battle's own (their numbers are WeaponSpec's, in CombatSpecs.cpp).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class Subsystem;
	class WeaponSpec;
	enum class ModuleKind : int;
	enum class SubsystemKind : int;

	/// <summary>
	/// The weapons the arsenal was missing (US-T101). A <b>vulcan</b> is a rotary gun: cold it fires at a quarter of its
	/// rate, every round it fires spins it faster, full rate after <see cref="WeaponSpec.SpinUpS"/> of firing, and left idle
	/// it winds down again. A <b>hive</b> round homes for a moment and splits into a swarm of frail micro-missiles that point
	/// defence can thin. A <b>rocket</b> has no seeker at all: a salvo fired straight down the nose that hits only what it
	/// flies into. The homing missile is the missile launcher every hull already carries.
	/// </summary>
	class SPACEGODCORE_API Arsenal final
	{
	public:
		/// <summary>How much slower than its full rate a cold rotary gun fires.</summary>
		static constexpr double SpinStartFactor = 4;

		/// <summary>How long a rotary gun's trigger can rest, seconds, before its barrels begin to wind down, and how much
		/// slower they wind down than up.</summary>
		static constexpr double SpinIdleS = 0.3; static constexpr double SpinDownFactor = 2;

		/// <summary>How much wider a rotary gun's rounds scatter than an autocannon's.</summary>
		static constexpr double RotaryScatter = 1.8;

		/// <summary>How far off its line a rocket of a salvo flies, deg.</summary>
		static constexpr double RocketSpreadDeg = 1.2;

		/// <summary>The damage a screen must do to stop a rocket, a hive round before it splits, and one micro-missile.</summary>
		static constexpr double RocketHp = 6; static constexpr double HiveHp = 12; static constexpr double MicroHp = 4;

		/// <summary>A micro-missile's speed against its hive round's, its life in seconds, and how wide the swarm fans out, deg.</summary>
		static constexpr double MicroSpeedFactor = 1.35; static constexpr double MicroLifeS = 14; static constexpr double SplitSpreadDeg = 25;

		static bool IsRotary(const cs::Ref<WeaponSpec>& w);
		static bool IsRocket(const cs::Ref<WeaponSpec>& w);
		static bool IsHive(const cs::Ref<WeaponSpec>& w);

		/// <summary>The reload of the round a rotary gun has just fired at its spin now, seconds; the round spins it up by the
		/// time it covers. Any other gun's own <see cref="WeaponSpec.Cooldown"/> times <paramref name="scale"/>.</summary>
		static double FireRotary(const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w, double scale = 1);

		/// <summary>An idle rotary gun winds down (called each step with the reloads).</summary>
		static void SpinDown(const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w, double dt);

		/// <summary>Rounds of a rotary gun's full rate one shot stands for in a step of <paramref name="dt"/> seconds (at least
		/// one): a fight stepped a second at a time fires the same weight as one stepped by frames.</summary>
		static double RoundsPerShot(const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w, double dt);

		/// <summary>Fits <paramref name="module"/> (no yard bill: a demo craft's, or a build's) onto every weapon hardpoint of
		/// <paramref name="ship"/> of its mount type that takes it, in place of what is there. Returns how many it went onto.</summary>
		static int FitAll(const cs::Ref<Ship>& ship, ModuleKind module);
	};
}
