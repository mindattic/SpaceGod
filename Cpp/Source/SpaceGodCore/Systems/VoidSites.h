#pragma once
// US-T123: what a void station's stretch of open space holds besides the relay -- an asteroid drift to mine, a cracked or
// shattered planetoid and its debris, and a sight or two worth the detour.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "BodyDefinition.h"

namespace SpaceGod::Core
{
	class BodyDefinition;
	class DebrisDisc;
	class World;
	enum class Good : int;

	/// <summary>What one of a void station's sites is (see <see cref="VoidSites"/>).</summary>
	enum class VoidSiteKind : int
	{
		None,

		/// <summary>One rock of the station's asteroid drift: a body a mining claim can be staked on.</summary>
		FieldRock,

		/// <summary>What is left of a planetoid that cracked or blew apart: its core, wrapped in a shell of its own debris.</summary>
		Planetoid,

		/// <summary>One of the big pieces broken off that planetoid, circling what is left of it.</summary>
		Chunk,

		/// <summary>A "tourist trap": an oddity crews make the detour for while the drive cools (see <see cref="VoidSight"/>).</summary>
		Sight,

		/// <summary>US-T130: the rock deep in a station's nebula a pirate clan has dug its haven into.</summary>
		DenRock,

		/// <summary>US-T130: a colossal inhabited megastructure: stacked tower blocks lit by thousands of windows, with
		/// docking spars reaching far out and ships berthed along them.</summary>
		Megastructure,
	};

	/// <summary>US-T130: what a station's asteroid field is like.</summary>
	enum class VoidFieldStyle : int
	{
		/// <summary>A clump of a few big stony rocks riding one orbit (US-T123's drift).</summary>
		Drift,
		/// <summary>Many small rocks in a looser spread, each wrapped in thick rubble.</summary>
		Rubble,
		/// <summary>Dense, dark, metal-rich rocks, rich in the rarer ore.</summary>
		MetalSwarm,
		/// <summary>Glittering chunks of ice, mined for water and volatiles.</summary>
		IceField,
	};

	/// <summary>
	/// US-T130: the mix of places a station gets (its field's style, whether it hides in a nebula, whether it has a
	/// megastructure). Chosen in one place (<see cref="VoidSites.Mix"/>) so a per-place biome can take the choice over.
	/// </summary>
	struct SPACEGODCORE_API VoidMix
	{
		VoidFieldStyle Field = VoidFieldStyle::Drift;
		bool Nebula = false;
		bool Megastructure = false;
	};

	/// <summary>The kinds of oddity a void station's <see cref="VoidSiteKind.Sight"/> can be.</summary>
	enum class VoidSight : int
	{
		None,
		/// <summary>A moonlet grown over with metre-long crystals that catch the tiny suns' light.</summary>
		GlassMoonlet,
		/// <summary>A comet nucleus whose last jet froze solid mid-spurt, a spire of ice standing off it.</summary>
		FrozenFountain,
		/// <summary>A generation ship nobody has ever claimed, its ring habitats dark, drifting with its lamps still on.</summary>
		DerelictArk,
		/// <summary>A perfect hoop of rock with nothing in the middle, which no one has explained.</summary>
		StoneArch,
		/// <summary>A rock veined with something that glows.</summary>
		Lantern,
	};

	/// <summary>One site about a void station: a body added to its catalogue, and how it is drawn.</summary>
	struct SPACEGODCORE_API VoidSite
	{
		cs::String Name;
		cs::String ParentName;
		VoidSiteKind Kind = VoidSiteKind::None;
		VoidSight Sight = VoidSight::None;

		/// <summary>Its orbit (circular) about its parent: radius, period, inclination, node and where it is at J2000.</summary>
		double AKm = 0, PeriodDays = 1, InclinationDeg = 0, NodeDeg = 0, LongitudeDeg = 0;
		double RadiusKm = 1;
		double RotationHours = 10;
		Rgb Color;
		cs::String Summary;

		/// <summary>A planetoid that blew apart rather than cracked: a thicker debris shell, more and smaller chunks.</summary>
		bool Exploded = false;

		/// <summary>An icy rock rather than a stony one.</summary>
		bool Icy = false;

		/// <summary>US-T130: the style of field a field rock belongs to (how it is drawn and how much rubble is round it).</summary>
		VoidFieldStyle Field = VoidFieldStyle::Drift;

		/// <summary>Varies its drawn shape (the model is built from it, so it is the same every time).</summary>
		uint32_t ShapeSeed = 0;
	};

	/// <summary>Everything about one void station (see <see cref="VoidSites.Plan"/>).</summary>
	class SPACEGODCORE_API VoidSitePlan final
	{
	public:
		cs::String Station;

		/// <summary>The asteroid drift's name ("Cinder Drift"), its rocks', and the band it circles in (km from the generator).</summary>
		cs::String FieldName;
		double FieldInnerKm = 0, FieldOuterKm = 0;
		DVec3 FieldNormal = DVec3::UnitZ;

		/// <summary>The rarer ore the drift is worked for besides its nickel-iron and ice.</summary>
		Good FieldOre{};

		/// <summary>The assay camp on the drift's biggest rock: the market its ore is sold at (see <see cref="VoidSites.Settle"/>).</summary>
		cs::String OutpostName;
		double OutpostPopulation = 0;

		cs::String PlanetoidName;

		/// <summary>US-T130: the station's mix (see <see cref="VoidSites.Mix"/>).</summary>
		VoidMix Mix;

		/// <summary>US-T130: the nebula the station sits in (centred on its generator), if <see cref="VoidMix.Nebula"/>:
		/// its name and reach, the rock its pirate haven is dug into, and the haven's name.</summary>
		cs::String NebulaName;
		double NebulaRadiusKm = 0;
		cs::String DenName, HavenName;

		/// <summary>US-T130: the megastructure, if <see cref="VoidMix.Megastructure"/>, and the city that lives in it.</summary>
		cs::String MegastructureName;
		double MegastructurePopulation = 0;

		/// <summary>Every site, parents before their children, in the order they join the catalogue.</summary>
		cs::List<VoidSite> Sites;

		/// <summary>How much of the station the overview frames: just past its farthest site.</summary>
		double OverviewSpanKm = 0;

		const VoidSite* Find(const cs::String& name) const;
	};

	/// <summary>
	/// The places about a void station (US-T123). A capital ship's drive reaches about 7 light-years, then has to cool
	/// and spool again, so its crew stop at void stations whether they like it or not; these are what is there when they
	/// do. Every station has, round its generator:
	///  - an asteroid drift: a clump of named rocks riding one orbit together (the generator's field holds them in
	///    formation), each a body a mining claim can be staked on (<see cref="Holdings.StakeClaim"/>), and an assay camp
	///    on the biggest (<see cref="Settle"/>) that buys their ore and mines it too, so the claims have a market;
	///  - a planetoid that cracked or blew apart: its core, a shell of its debris round it (drawn, not simulated), and
	///    its broken chunks circling it;
	///  - one or two sights, oddities worth the detour while the drive cools.
	///
	/// All of it comes from the station's name alone, through its own random stream (not the world's): the same station
	/// holds the same places in every game, and nothing else in the simulation draws a number differently for it.
	///
	/// Both scales are checked as it is laid out: every site is placed so that, at the realistic scale and at the game
	/// scale (<see cref="ScaleModel"/>), no two drawn sites (with the rubble and debris drawn round them) overlap, and
	/// none comes in over the generator's tiny suns. The drift's rocks share one period, and the chunks one period about
	/// their core, so the formations they were checked in hold as they turn.
	/// </summary>
	class SPACEGODCORE_API VoidSites final
	{
	public:
		/// <summary>
		/// US-T130: the one place a station's mix is chosen (field style, nebula, megastructure), from its name alone. A
		/// per-place biome (US-T132) replaces the body of this function and nothing else.
		/// </summary>
		static VoidMix Mix(const cs::String& station);

		/// <summary>The plan for <paramref name="station"/>: the same every time for the same name.</summary>
		static VoidSitePlan Plan(const cs::String& station);

		/// <summary>Appends the plan's sites to a void station's bodies (after the generator and its tiny suns).</summary>
		static void AddBodies(cs::List<cs::Ref<BodyDefinition>>& bodies, const VoidSitePlan& plan);

		/// <summary>The drift's band, drawn as a debris belt about the generator (body 0).</summary>
		static cs::List<cs::Ref<DebrisDisc>> Discs(const VoidSitePlan& plan);

		/// <summary>
		/// Opens the assay camp on the drift's biggest rock: a mining outpost of the system's independents with mines on
		/// the drift's nickel-iron, ice and rarer ore, and a market that buys what claims on the drift cut.
		/// </summary>
		static void Settle(const cs::Ref<World>& world, const VoidSitePlan& plan);

		/// <summary>
		/// The site a body is, for drawing and labelling it: looked up by its name and its parent's (a drift rock, the
		/// planetoid and the sights circle the station's generator, named after the station; a chunk circles the
		/// planetoid). Null for any body that is not one.
		/// </summary>
		static const VoidSite* Find(const cs::String& body, const cs::String& parent);

		/// <summary>What the HUD calls the body ("Mineable asteroid", "Shattered planetoid", "Sight"), or empty if it is no site.</summary>
		static cs::String KindLabel(const cs::Ref<BodyDefinition>& body);

		/// <summary>How far out the drawn debris shell round a planetoid reaches, in its radii (true distance).</summary>
		static constexpr double ShellOuterRadii = 2.1;
		static constexpr double ShellInnerRadii = 1.3;

		/// <summary>How far out the rubble drawn round a drift rock reaches, in its radii (true distance).</summary>
		static constexpr double RubbleRadii = 2.4;
	};
}
