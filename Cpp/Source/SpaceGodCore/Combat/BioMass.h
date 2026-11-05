#pragma once
// Not a C# file (US-T125): the Bio Mass, a space-dwelling lifeform and an infection, hostile to everything.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class JumpGate;
	class Ship;
	class ShipCombat;

	/// <summary>Who last fired on a globule (the views draw the shot, the HUD names it). Not saved.</summary>
	enum class BioShooter : int
	{
		None,
		/// <summary>The guns embedded in and orbiting a star, planet or moon (<see cref="BioMass.BodyGunCount"/>).</summary>
		Orbital,
		/// <summary>A jump gate's own guns and its forts (<see cref="BioMass.GateGunRangeKm"/>).</summary>
		Gate,
		/// <summary>An armed hull's weapons (any faction's, the player's too).</summary>
		Ship,
	};

	/// <summary>
	/// One globule of the Bio Mass (US-T125): a pulsing, viscous lump of flesh, bone, tissue, mouths and hair, adrift in
	/// space. Placed as a wreck is: an <see cref="Anchor"/> in its body's frame (true km, shown through the scale model's
	/// local offset) plus a small <see cref="Local"/> offset at ship scale, so it sits among the hulls it touches in real
	/// and game scale alike.
	/// </summary>
	class SPACEGODCORE_API BioGlobule final
	{
	public:
		int Id = 0;

		/// <summary>The body whose frame <see cref="Anchor"/> is in (0, the system's root, for open space).</summary>
		int Body = 0;
		DVec3 Anchor, Local;

		/// <summary>Its slow internal drift (km/s, the <see cref="Local"/> frame): the meat rolling over itself.</summary>
		DVec3 Drift;

		/// <summary>Tonnes of living tissue: its size (<see cref="RadiusKm"/>) and its toughness (<see cref="MaxHp"/>).</summary>
		double Mass = 0;
		double Hp = 0;

		double BornJd = 0;

		/// <summary>A per-globule number the views shape it from (its lumps, mouths, bones and hair).</summary>
		int Seed = 0;

		/// <summary>Where it is creeping: a jump gate's index in <see cref="World.Gates"/>, or -1 for none (it wanders).</summary>
		int GoalGate = -1;

		/// <summary>The body it is creeping toward when it has no gate (-1 none).</summary>
		int GoalBody = -1;

		/// <summary>The date it came out of a gate (NaN never): the far side's gate guns open up on it as it exits.</summary>
		double ExitedJd = cs::Double::NaN;

		/// <summary>Wrecks and stations it has eaten.</summary>
		int Consumed = 0;

		// Not saved: what hit it in its last update, for the views' tracers and the HUD.
		BioShooter LastShooter = BioShooter::None;
		/// <summary>Which one: the body's index (orbital guns), the gate's in World.Gates, or the hull's Id.</summary>
		int LastShooterIndex = -1;
		DVec3 LastShotFromHelio;
		double LastHitJd = cs::Double::NaN;
		double DamageTaken = 0;

		double RadiusKm() const;
		double MaxHp() const;
		bool Alive() const { return Hp > 0; }

		BioGlobule() = default;
		SG_SERIALIZABLE(BioGlobule);
	};

	/// <summary>
	/// The Bio Mass of one star system (US-T125). Globules seep into every system from the dark between the stars, drift,
	/// feed and swell, and creep toward its jump gates to spread to the next system. Everything shoots it on sight:
	/// <list type="bullet">
	/// <item>every star, planet and moon carries orbital guns (embedded batteries and a ring of gun satellites) that open
	/// fire the moment a globule is within <see cref="BodyGunRangeKm"/>, joined by the settlements' defence platforms;</item>
	/// <item>a jump gate's guns and its forts fire on a globule closing on it (<see cref="GateGunRangeKm"/>), so most die on
	/// the approach; one that gets through is fired on again by the far gate the moment it comes out;</item>
	/// <item>every armed hull of every faction (the player's too) within <see cref="ShipEngageKm"/> engages it.</item>
	/// </list>
	/// It infects: a globule that touches a wreck eats it and grows, it gnaws at a hull it touches (never past
	/// <see cref="BiteHullFloor"/>), and one that reaches a station eats into its population. It is bounded: at most
	/// <see cref="MaxGlobules"/> per system, at most <see cref="MaxMass"/> each (a big one splits instead), and it seeds
	/// from its own random stream so a run stays deterministic.
	/// </summary>
	class SPACEGODCORE_API BioMass final
	{
	public:
		cs::List<cs::Ref<BioGlobule>> Globules = cs::List<cs::Ref<BioGlobule>>();

		/// <summary>Globules destroyed here, all time.</summary>
		int Killed = 0;
		/// <summary>Globules that forced their way through one of this system's gates, all time.</summary>
		int Crossed = 0;

		static constexpr int MaxGlobules = 10;
		static constexpr int SeededAtStart = 3;
		/// <summary>On average one new globule seeps in this often (days), while there is room.</summary>
		static constexpr double SeepDays = 45;

		static constexpr double SpawnMass = 6e4;
		static constexpr double MaxMass = 2.4e6;
		static constexpr double SplitMass = 1.6e6;
		/// <summary>Growth, a share of its mass a day, from what it filters out of space.</summary>
		static constexpr double GrowthPerDay = 0.04;
		/// <summary>Hit points per tonne, and what it heals a second.</summary>
		static constexpr double HpPerTonne = 0.2;
		static constexpr double RegenPerS = 25;
		/// <summary>How fast it creeps toward its goal (km/s): slow for a ship, quick for meat.</summary>
		static constexpr double CreepKmS = 25;

		/// <summary>Orbital guns per body (a star has the most), their reach past the body's surface and their fire.</summary>
		static constexpr int StarGuns = 6;
		static constexpr int PlanetGuns = 4;
		static constexpr int MoonGuns = 2;
		static constexpr double BodyGunReachKm = 40000;
		static constexpr double BodyGunReachRadii = 2;
		static constexpr double GunDps = 180;
		/// <summary>A jump gate's own guns, their reach, and the forts' share of their weapons' fire.</summary>
		static constexpr int GateGuns = 3;
		static constexpr double GateGunRangeKm = 30000;
		/// <summary>Armed hulls engage within this (km): every faction's warships close and fire on it.</summary>
		static constexpr double ShipEngageKm = 5000;

		/// <summary>What a touch does: a bite (hull points a second, scaled by size) never takes a hull below this share.</summary>
		static constexpr double BiteDps = 60;
		static constexpr double BiteHullFloor = 0.15;
		/// <summary>A wreck within this of it (km past its radius) is drawn in and eaten: tonnes gained.</summary>
		static constexpr double WreckReachKm = 25;
		static constexpr double WreckMeal = 1.5e5;
		/// <summary>A station's population eaten a day while a globule sits on its body (share), bounded below.</summary>
		static constexpr double StationEatPerDay = 0.02;
		static constexpr double StationFloor = 0.5;

		/// <summary>The longest step the creep and the guns are worked in (seconds), and the most steps one update takes.</summary>
		static constexpr double StepS = 600;
		static constexpr int MaxSteps = 48;

		BioMass() = default;
		BioMass(const cs::Ref<World>& world, int seed, double jd);

		void Update(double jd);

		/// <summary>Where a globule is (heliocentric km).</summary>
		DVec3 Helio(const cs::Ref<BioGlobule>& g, double jd) const;

		/// <summary>A new globule (the sg.BioMass command and the seeding): its anchor in <paramref name="body"/>'s frame.</summary>
		cs::Ref<BioGlobule> Spawn(int body, DVec3 anchorKm, DVec3 localKm, double mass, double jd);

		/// <summary>A globule coming out of <paramref name="gate"/> (this system's end), from the far side.</summary>
		void Arrive(const cs::Ref<BioGlobule>& g, const cs::Ref<JumpGate>& gate, double jd);

		/// <summary>The orbital guns a body carries (0 for a barycentre, station or asteroid).</summary>
		int BodyGunCount(int body) const;
		double BodyGunRangeKm(int body) const;

		/// <summary>The nearest live globule to a point, and its distance (km), or null.</summary>
		cs::Ref<BioGlobule> Nearest(DVec3 helioKm, double jd, double& distanceKm) const;

		/// <summary>Damage dealt to it by the player's hull and the DPS of an armed hull's working weapons.</summary>
		static double WeaponDps(const cs::Ref<ShipCombat>& combat);

	private:
		cs::Ref<World> world;
		cs::Random rng;
		int nextId = 1;
		double lastJd = cs::Double::NaN, nextSeepJd = 0;

		void Seep(double jd);
		void Step(double jd, double dt, const cs::List<DVec3>& bodies, const cs::List<DVec3>& shipHelio, const cs::List<cs::Ref<Ship>>& armed);
		void Creep(const cs::Ref<BioGlobule>& g, double jd, double dt, const cs::List<DVec3>& bodies);
		void Feed(double jd, double dt, const cs::List<DVec3>& bodies);
		void TryCross(const cs::Ref<BioGlobule>& g, double jd);
		void Die(const cs::Ref<BioGlobule>& g, double jd);
		void PickGoal(const cs::Ref<BioGlobule>& g);
		void Hit(const cs::Ref<BioGlobule>& g, double damage, BioShooter by, int index, DVec3 fromHelio, double jd);
		DVec3 RandomDirection();

		SG_SERIALIZABLE(BioMass);
	};
}
