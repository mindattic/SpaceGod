#pragma once
// US-T264: hands-on mining. A player at a miner's helm (or any hull a yard has fitted with a mining laser) cuts asteroids
// with a mining beam into ore crates for the grid hold, instead of only tugs and hired captains cutting ore.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Economy/Goods.h"

#include <tuple>

namespace SpaceGod::Core
{
	class AsteroidClass;
	class AsteroidField;
	class Faction;
	class Ship;
	class World;

	/// <summary>Where a rock the beam can cut comes from.</summary>
	enum class RockSource : int
	{
		None,

		/// <summary>One of the main belt's (or the Trojans') real asteroids (<see cref="AsteroidField"/>), in Sol.</summary>
		Belt,

		/// <summary>A body of the catalogue: a named asteroid (Psyche, Lutetia), a void station drift's rock, a shattered
		/// planetoid or one of its chunks (<see cref="VoidSites"/>).</summary>
		Body,

		/// <summary>One drawn chunk of an ice field (<see cref="SpaceFeatures"/>).</summary>
		Ice,
	};
	SPACEGODCORE_API const char* ToString(RockSource v);

	/// <summary>A rock the beam can cut: what it is, where it is now and what it is made of.</summary>
	struct SPACEGODCORE_API MineableRock
	{
		RockSource Source = RockSource::None;

		/// <summary>Stable across frames, peers and saves: "belt:&lt;number&gt;", "body:&lt;name&gt;", "ice:&lt;field&gt;#&lt;i&gt;".</summary>
		cs::String Id;

		/// <summary>What the HUD calls it.</summary>
		cs::String Name;

		/// <summary>The catalogue body (for <see cref="RockSource.Body"/>), else -1.</summary>
		int Body = -1;

		/// <summary>Its centre now, heliocentric km, and its radius, km.</summary>
		DVec3 HelioKm;
		double RadiusKm = 0;

		/// <summary>Its composition (BeltMining's kinds): what grinding it yields.</summary>
		cs::Ref<AsteroidClass> Class;

		/// <summary>A void drift's rarer ore, worked besides the rock's own yield (<see cref="VoidSitePlan.FieldOre"/>).</summary>
		bool HasFieldOre = false;
		Good FieldOre{};

		/// <summary>Rock the beam can take off it before it is worked out, tonnes.</summary>
		double ReserveTonnes = 0;

		bool Valid() const { return Source != RockSource::None; }
	};

	/// <summary>What a mining laser of a grade does (1 a fitted laser, 2 a miner's own or two fitted, 3 the best rig).</summary>
	struct SPACEGODCORE_API MiningLaserSpec
	{
		int Grade = 0;

		/// <summary>How far past the rock's surface the beam reaches, km.</summary>
		double RangeKm = 0;

		/// <summary>Seconds of beam on a stony face before it fractures (harder, denser rock takes longer).</summary>
		double FractureSeconds = 0;

		/// <summary>Rock broken loose each fracture, tonnes.</summary>
		double FractureTonnes = 0;

		/// <summary>How well it follows a vein: the factor the rare elements' share is raised by.</summary>
		double VeinFactor = 0;

		static MiningLaserSpec Of(int grade);

		/// <summary>"Fitted mining laser", "Industrial mining laser", "Deep-core mining laser".</summary>
		static cs::String Name(int grade);
	};

	/// <summary>How the beam stands this frame.</summary>
	enum class BeamState : int
	{
		/// <summary>The trigger is not held.</summary>
		Off,

		/// <summary>Held, but the hull carries no mining laser.</summary>
		NoLaser,

		/// <summary>Held, but the hull cannot cut now (in a fight, cruising, powered down, between the stars).</summary>
		Blocked,

		/// <summary>Held, with no rock in reach down the aim.</summary>
		NoRock,

		/// <summary>On a rock, heating its face.</summary>
		Cutting,

		/// <summary>On a rock that is worked out.</summary>
		Depleted,

		/// <summary>On a rock, but the hold has no room for what it yields.</summary>
		HoldFull,
	};
	SPACEGODCORE_API const char* ToString(BeamState v);

	/// <summary>
	/// One helm's mining beam: the rock it is on, how hot the face is, the ore broken loose and not yet a full crate, and
	/// what the HUD shows. Kept by the helm (<see cref="ShipHelm.Beam"/>) and advanced only by the helm's own input (the
	/// primary trigger in the frames), so every co-op peer cuts the same crates at the same frame. Not saved: a face
	/// half heated when the game is saved cools; the rock's depletion is saved (<see cref="World.HandCut"/>).
	/// </summary>
	class SPACEGODCORE_API MiningBeam final
	{
	public:
		BeamState State = BeamState::Off;

		/// <summary>The rock the beam is on (invalid when none).</summary>
		MineableRock Rock;

		/// <summary>The face's heat toward the next fracture, 0..1.</summary>
		double Heat = 0;

		/// <summary>Where the beam meets the rock, heliocentric km, and how far that is from the hull, km.</summary>
		DVec3 HitHelioKm;
		double DistanceKm = 0;

		/// <summary>The beam's direction from the hull (unit, ecliptic axes).</summary>
		DVec3 Direction;

		/// <summary>Ore broken loose and waiting to fill a crate, tonnes by good.</summary>
		cs::List<double> Bin = cs::NewArray<double>(GoodInfo::Count);

		/// <summary>Over this helm's time: fractures, crates loaded and tonnes of rock broken.</summary>
		int Fractures = 0, Crates = 0;
		double TonnesBroken = 0;

		/// <summary>The grade of the laser it last fired with.</summary>
		int Grade = 0;

		/// <summary>Whose claim the rock is on (null for free rock), and whether the pilot is jumping it.</summary>
		cs::Ref<Faction> ClaimHolder;
		bool Jumping = false;

		/// <summary>The HUD's line for the beam (empty when there is nothing to say).</summary>
		cs::String Readout;

		/// <summary>The rock each claim-jump was last recorded on, and when: one crime a rock a day.</summary>
		cs::Dictionary<cs::String, double> JumpedAt;

		/// <summary>What happened (a crate cut, a rock worked out, a claim jumped), for toasts; taken by the HUD.</summary>
		cs::List<cs::String> Notices;

		bool Active() const { return State == BeamState::Cutting; }

		cs::List<cs::String> TakeNotices();
	};

	/// <summary>
	/// Hands-on mining (US-T264). A player at the helm of a miner (an industrial mining laser, grade 2), an ore or capture
	/// tug (the same), or any hull a yard has fitted with mining lasers (each one a grade, up to 3, on top of a miner's own)
	/// holds the primary trigger out of a fight to fire a mining beam down the aim. Any rock in reach is cut: one of the
	/// main belt's real asteroids, a named one of the catalogue, a void station drift's rock, a shattered planetoid or its
	/// chunks, or an ice field's chunk. The face heats under the beam and fractures, the rock broken loose is ground by its
	/// composition (<see cref="AsteroidClass"/>: carbonaceous, stony, metallic, or comet ice; a void drift's rocks also
	/// carry its rarer ore) and the elements go into ore crates in the grid hold as each fills. A better laser reaches
	/// further, cuts faster, breaks more at a time and follows the veins of the rare elements better. A rock is worked out
	/// after its reserve (by its size) is cut, and stays so (<see cref="World.HandCut"/>).
	///
	/// Claims matter: a rock on a body the player's house holds a mining claim on is the house's to cut; a rock on a body a
	/// settlement stands on (an assay camp's drift rock, a mining colony) belongs to the settlement's owner, and cutting it
	/// flying any other flag is claim jumping, a crime on the pilot's record (<see cref="CrimeKind.ClaimJumping"/>), once a
	/// rock a day. The ore sells at assay camps and markets like any other crate.
	///
	/// Everything here is decided by the world's state and the helm's input alone (rocks by their ephemeris, composition by a
	/// hash of the rock's id, no random draws), so it is the same on every peer, and the Bench never touches it.
	/// </summary>
	class SPACEGODCORE_API HandMining final
	{
	public:
		/// <summary>The main belt's asteroids, handed over by the game when it loads them (the core has no data files); null
		/// leaves the belt's own rocks out (named asteroids, void drifts and ice fields still count).</summary>
		static cs::Ref<AsteroidField>& Belt();

		/// <summary>How far off the aim a rock may lie and still be hit, deg.</summary>
		static constexpr double ConeHalfDeg = 12;

		/// <summary>The most a rare element's share can be raised to by following its vein.</summary>
		static constexpr double MaxVeinShare = 0.05;

		/// <summary>A void drift's rarer ore, as a share of the rock broken.</summary>
		static constexpr double FieldOreShare = 0.03;

		/// <summary>One claim-jumping crime a rock this often, days.</summary>
		static constexpr double JumpCooldownDays = 1;

		/// <summary>Faster than this (km/s, in the hull's frame) the belt's rocks are not looked for: nothing can be cut at such
		/// speed, and a cruise drive racing through the belt would otherwise solve its every rock each frame.</summary>
		static constexpr double BeltSearchMaxKmS = 100;

		/// <summary>The longest step a frame may take, seconds (a stalled frame does not cut a crate at once).</summary>
		static constexpr double MaxStepSeconds = 0.5;

		/// <summary>The grade of mining laser <paramref name="ship"/> carries: 2 for a miner or a tug's own rig, plus one for
		/// each mining laser a yard fitted, up to 3; 0 for none.</summary>
		static int GradeOf(const cs::Ref<Ship>& ship);

		/// <summary>The composition of the rock with this id: comet ice if <paramref name="icy"/>, metallic if
		/// <paramref name="metalRich"/>, else drawn by the belt's shares from a hash of the id (the same every time).</summary>
		static cs::Ref<AsteroidClass> ClassOf(const cs::String& id, bool icy, bool metalRich);

		/// <summary>Rock a beam can take off a rock of this diameter before it is worked out, tonnes.</summary>
		static double ReserveOf(double diameterKm);

		/// <summary>Rock still to be cut off <paramref name="rock"/> in <paramref name="world"/>, tonnes.</summary>
		static double Remaining(const cs::Ref<World>& world, const MineableRock& rock);

		/// <summary>
		/// The rock the beam hits from <paramref name="helioKm"/> down <paramref name="direction"/>: the nearest whose surface
		/// lies within <paramref name="rangeKm"/> and whose disc is within <see cref="ConeHalfDeg"/> of the aim. Invalid if none.
		/// </summary>
		static MineableRock FindRock(const cs::Ref<World>& world, DVec3 helioKm, DVec3 direction, double rangeKm, bool withBelt = true);

		/// <summary>The nearest rock to <paramref name="helioKm"/> whose surface is within <paramref name="maxKm"/>, aimed at or
		/// not (for the HUD's "close in" line); <paramref name="surfaceKm"/> gets how far off its surface is.</summary>
		static MineableRock NearestRock(const cs::Ref<World>& world, DVec3 helioKm, double maxKm, double& surfaceKm, bool withBelt = true);

		/// <summary>The elements one tonne of <paramref name="rock"/> yields under a laser of <paramref name="grade"/>.</summary>
		static cs::List<std::tuple<Good, double>> YieldOf(const MineableRock& rock, int grade);

		/// <summary>Whose claim <paramref name="rock"/> lies on, or null for free rock: the player's house for a body it holds
		/// a claim on, else the owner of the biggest lawful settlement standing on the body.</summary>
		static cs::Ref<Faction> ClaimHolderOf(const cs::Ref<World>& world, const MineableRock& rock);

		/// <summary>Why <paramref name="ship"/> cannot cut right now (in a fight, in a lane), or empty.</summary>
		static cs::String Blocker(const cs::Ref<Ship>& ship);

		/// <summary>
		/// One frame of the beam: <paramref name="held"/> is the primary trigger, <paramref name="helioKm"/> where the hull is
		/// and <paramref name="aim"/> where it points the beam, <paramref name="speedKmS"/> how fast it moves in its frame. Heats the face of the rock hit, fractures it, grinds what
		/// broke loose into the bin and loads full crates into the hull's grid hold; books the rock's depletion in the world
		/// and a claim jump on the pilot's record.
		/// </summary>
		static void Step(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<MiningBeam>& beam, bool held,
			DVec3 helioKm, DVec3 aim, double speedKmS, double dt);

	private:
		static void Fracture(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<MiningBeam>& beam, const MiningLaserSpec& laser);

		static void RecordJump(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<MiningBeam>& beam);
	};
}
