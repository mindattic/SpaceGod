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

	/// <summary>What shape of the Bio Mass a <see cref="BioGlobule"/> is (US-T192). Saved.</summary>
	enum class BioForm : int
	{
		/// <summary>A drifting globule (US-T125): creeps for the gates and the worlds, feeds, buds.</summary>
		Globule,
		/// <summary>A small, fast bloom: erupts in a cluster beside a hull or station, darts in and latches on, bites,
		/// and withers in a day or two unless it takes root on a station or asteroid (then it grows into a tumor).</summary>
		Bloom,
		/// <summary>A large, slow tumor: roots on a station, asteroid or hull, grows on what it drains, broods blooms, and
		/// sheds a seed that creeps to the next station or asteroid once it is big enough.</summary>
		Tumor,
	};

	/// <summary>What a bloom or tumor has latched onto (US-T192). Saved.</summary>
	enum class BioHost : int
	{
		None,
		/// <summary>A hull: <see cref="BioGlobule.HostId"/> is its Ship.Id; the growth rides it.</summary>
		Ship,
		/// <summary>A station or asteroid body (or a world): <see cref="BioGlobule.HostId"/> is its catalog index.</summary>
		Body,
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

		// US-T192: blooms and tumors (absent from older saves: a globule, free).
		BioForm Form = BioForm::Globule;
		BioHost Host = BioHost::None;
		/// <summary>The host's Ship.Id or body index (<see cref="Host"/>).</summary>
		int HostId = -1;
		/// <summary>When it latched on (NaN free).</summary>
		double AttachedJd = cs::Double::NaN;
		/// <summary>The earliest date it erupts its next cluster of blooms (a globule near a hull, a tumor).</summary>
		double NextBroodJd = 0;
		/// <summary>A bloom withers at this date unless it has taken root (NaN never).</summary>
		double WithersJd = cs::Double::NaN;

		bool Attached() const { return Host != BioHost::None; }
		const char* FormName() const { return Form == BioForm::Bloom ? "bloom" : Form == BioForm::Tumor ? "tumor" : "globule"; }

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
	///
	/// US-T192: it attacks in two more shapes (<see cref="BioForm"/>). Blooms are small and fast: a cluster erupts out of a
	/// globule or a tumor when a hull or station is near, darts in, latches on and bites (hull, a station's platforms and
	/// people), and withers in a day or two, unless one latched on a station or asteroid with no tumor takes root there and
	/// grows into one. Tumors are large and slow: one seeds onto a station or asteroid now and then (or roots from a bloom,
	/// or latches onto a hull that brushes past a free seed), drains what it is on (hull, people) and grows on it, broods
	/// blooms, and, once big, sheds a seed that creeps off to root on the next station or asteroid. Both are shot as a
	/// globule is (the guns that would hit their host hold most of their fire, <see cref="HostCover"/>), both are bounded
	/// (<see cref="MaxBlooms"/>, <see cref="MaxTumors"/>), and both draw on the same random stream.
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

		// ---------------------------------------------------------------- blooms and tumors (US-T192)

		/// <summary>Blooms (small, fast): at most this many per system, a cluster of 3 to <see cref="BloomCluster"/> at a time.</summary>
		static constexpr int MaxBlooms = 18;
		static constexpr int BloomCluster = 5;
		static constexpr double BloomMass = 9e3;
		/// <summary>How fast a bloom darts (km/s), how far it sees a hull or station to go for (km), and when it is on.</summary>
		static constexpr double BloomKmS = 140;
		static constexpr double BloomSeekKm = 6000;
		static constexpr double LatchKm = 1.0;
		/// <summary>A free or latched bloom withers after this (days) unless it roots.</summary>
		static constexpr double BloomLifeDays = 1.5;
		/// <summary>A bloom latched on a station or asteroid with no tumor there roots into one after this (days).</summary>
		static constexpr double BloomRootDays = 0.5;
		/// <summary>A latched bloom's bite (hull points a second), never below <see cref="BloomHullFloor"/> of the hull.</summary>
		static constexpr double BloomBiteDps = 14;
		static constexpr double BloomHullFloor = 0.1;
		/// <summary>Population a latched bloom eats a day (share), and the platforms' hull it gnaws (points a second).</summary>
		static constexpr double BloomEatPerDay = 0.004;

		/// <summary>Tumors (large, slow): at most this many per system, seeding in about this often (days) while there is room.</summary>
		static constexpr int MaxTumors = 4;
		static constexpr double TumorSeepDays = 110;
		static constexpr double TumorSeedMass = 2.5e5;
		static constexpr double TumorMaxMass = 4e6;
		/// <summary>A tumor this big sheds a seed (<see cref="TumorSeedMass"/>) that creeps to the next station or asteroid.</summary>
		static constexpr double TumorShedMass = 1.2e6;
		/// <summary>Growth a day while rooted (share of its mass; twice on an asteroid's rock), and how fast a free one creeps.</summary>
		static constexpr double TumorGrowthPerDay = 0.03;
		static constexpr double TumorKmS = 6;
		/// <summary>Tumors are tougher than globules (hit points per tonne, a multiple) and heal faster.</summary>
		static constexpr double TumorToughness = 1.5;
		static constexpr double TumorRegenPerS = 30;
		/// <summary>A rooted tumor's drain: hull points a second (never below <see cref="TumorHullFloor"/>), tonnes gained a point.</summary>
		static constexpr double TumorDrainDps = 40;
		static constexpr double TumorHullFloor = 0.05;
		static constexpr double TumorMassPerHull = 40;
		/// <summary>A station's population a rooted tumor eats a day (share), never below <see cref="StationFloor"/>.</summary>
		static constexpr double TumorEatPerDay = 0.03;
		/// <summary>How often a tumor (or a globule beside a hull) erupts a cluster of blooms (days, on average).</summary>
		static constexpr double TumorBroodDays = 0.4;
		static constexpr double GlobuleBroodDays = 0.25;
		/// <summary>A globule or tumor with no hull near looks again after this (seconds), so the hulls' positions are not
		/// worked out every update for a brood that is not coming.</summary>
		static constexpr double BroodLookS = 600;
		/// <summary>Guns that would hit the host to hit a growth on it hold most of their fire: their share (orbital, gate, platform).</summary>
		static constexpr double HostCover = 0.3;

		BioMass() = default;
		BioMass(const cs::Ref<World>& world, int seed, double jd);

		void Update(double jd);

		/// <summary>Live ones of a form here.</summary>
		int CountOf(BioForm form) const;

		/// <summary>A tumor rooted on <paramref name="host"/> (or a free seed when host is None): the sg.BioMass command and the seeding.</summary>
		cs::Ref<BioGlobule> SpawnTumor(BioHost host, int hostId, int body, DVec3 anchorKm, double mass, double jd);

		/// <summary>A cluster of blooms erupting at <paramref name="helioKm"/> (as many as there is room for): how many came.</summary>
		int EruptBlooms(DVec3 helioKm, int count, double jd);

		/// <summary>The growths latched onto a hull (Ship.Id): blooms and tumors.</summary>
		int OnShip(int shipId, BioForm form) const;

		/// <summary>The station or asteroid bodies a tumor can root on (catalog indices).</summary>
		cs::List<int> RootBodies() const;

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
		/// <summary>US-T192: the next tumor's seeding (NaN not yet drawn: absent from older saves).</summary>
		double nextTumorJd = cs::Double::NaN;

		/// <summary>The hulls in open space this update and where they are (a growth's hosts and prey), not saved.</summary>
		struct Hull { cs::Ref<Ship> ship; DVec3 helio; };
		/// <summary>The station and asteroid bodies (catalog indices), worked out once an update; not saved.</summary>
		cs::List<int> roots = cs::List<int>();
		/// <summary>Every open-space hull (armed or not) and where it is at <see cref="hullsJd"/>: worked out only when a free
		/// bloom or seed hunts, or a brood looks round, and then once an update (<see cref="Hulls"/>); cleared after it.</summary>
		cs::List<Hull> hullCache = cs::List<Hull>();
		bool hullsReady = false;
		double hullsJd = cs::Double::NaN;

		/// <summary>Whether a hull is one a growth can ride, be fed on or hunt (in open space: not landed, not in a lane).</summary>
		static bool InOpenSpace(const cs::Ref<Ship>& s);
		const cs::List<Hull>& Hulls(double jd);
		/// <summary>The one hull a growth rides (by Ship.Id), without working out the rest: false if it is gone or not in open space.</summary>
		bool HostHull(int shipId, double jd, Hull& out);

		void Seep(double jd);
		void SeepTumor(double jd);
		void Grow(const cs::Ref<BioGlobule>& g, double jd, double dt, const cs::List<DVec3>& bodies);
		void Brood(const cs::Ref<BioGlobule>& g, double jd, DVec3 here);
		void FeedTumor(const cs::Ref<BioGlobule>& g, double jd, double days, DVec3 here, const cs::List<DVec3>& bodies);
		cs::String HostName(const cs::Ref<BioGlobule>& g) const;
		void Latch(const cs::Ref<BioGlobule>& g, BioHost host, int hostId, DVec3 hostHelio, double hostRadiusKm, double jd);
		void Detach(const cs::Ref<BioGlobule>& g, const cs::List<DVec3>& bodies, double jd);
		void Drain(const cs::Ref<BioGlobule>& g, double jd, double dt, const cs::Ref<Ship>& ship);
		void DrainBody(const cs::Ref<BioGlobule>& g, double jd, double dt);
		int NearestRootBody(DVec3 helioKm, const cs::List<DVec3>& bodies, int except) const;
		bool TumorOn(int body) const;
		double HostCoverFor(const cs::Ref<BioGlobule>& g) const { return g->Attached() ? HostCover : 1.0; }
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
