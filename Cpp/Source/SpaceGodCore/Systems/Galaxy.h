#pragma once
// Port of Assets/UnitySpace/Core/Systems/Galaxy.cs (US-T17), with the Galaxy partial class's other parts
// (GalaxyStreaming.cs, StarDiscovery.cs: their members are declared here and defined in their own .cpp).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "GalacticPosition.h"
#include "BodyDefinition.h"
#include "Cs/Cs.h"
#include "Systems/StarDiscovery.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class BodyCatalog;
	class Ephemeris;
	class World;
	class Hyperlanes;
	class JumpGate;
	class SystemSpec;
	class DebrisDisc;
	class Faction;
	class Polities;
	class GateNetwork;
	class DeepSpace;
	class InterstellarTraffic;
	class GateConstruction;
	class InterstellarColonisation;
	class InterstellarWars;
	class StarEntry;
	class StarDiscoveredEvent;
	class Territory;
	class DiscoveryLog; // US-T259

	/// <summary>
	/// One star system as a self-contained world: its own bodies (body 0 is its root, the Sun or a
	/// barycentre), ephemeris, hyperlanes, fleet, markets and wars. Every system runs on the shared
	/// clock, whether or not it is the one being looked at.
	/// </summary>
	class SPACEGODCORE_API StarSystem final
	{
	public:
		cs::String Name;

		/// <summary>Position of the system's root relative to the Sun, light-years (J2000 ecliptic axes, as inside every system).</summary>
		DVec3 PositionLy;

		/// <summary>
		/// Where the system's root sits in the galaxy, as a sector and an offset (U.SPC-T412, see <see cref="GalacticPosition"/>):
		/// the frame its own km coordinates (ephemeris, fleet, gates) are measured from.
		/// </summary>
		GalacticPosition Origin;

		/// <summary>The place in the galaxy <paramref name="localKm"/> from this system's root.</summary>
		GalacticPosition At(DVec3 localKm) const { return Origin.Offset(localKm); }

		/// <summary>Where <paramref name="place"/> is from this system's root, km (exact for anywhere near the system).</summary>
		DVec3 LocalKm(GalacticPosition place) const { return place.KmFrom(Origin); }

		cs::Ref<BodyCatalog> Catalog;
		cs::Ref<SpaceGod::Core::Ephemeris> Ephemeris;
		cs::Ref<SpaceGod::Core::World> World;
		cs::Ref<Hyperlanes> Lanes() const;

		/// <summary>This system's jump gates, one for each link of the <see cref="GateNetwork"/> it is on; empty until linked.</summary>
		const cs::List<cs::Ref<JumpGate>>& Gates() const { return gates; }

	private:
		cs::List<cs::Ref<JumpGate>> gates;

	public:
		// internal
		void AddGate(const cs::Ref<JumpGate>& gate);

		/// <summary>This system's gate straight to <paramref name="other"/>, or null if they are not directly linked.</summary>
		cs::Ref<JumpGate> GateTo(const cs::Ref<StarSystem>& other) const;

		/// <summary>The gate a ship at <paramref name="helioKm"/> (from this system's root) is close enough to activate, or null.</summary>
		cs::Ref<JumpGate> GateInRange(DVec3 helioKm) const;

		/// <summary>
		/// Nobody has settled it yet, but a working gate leads in (U.SPC-T97): the first gate into an empty system opens it
		/// to settlement, and stays its way in while it holds (see <see cref="GateConstruction"/>). A Void Base is never
		/// open to settlement (U.SPC-T372): there is no planet here for colonists to found anything on, only the relay
		/// itself, which is the one thing that keeps the stretch it sits in the galaxy's single lawless territory --
		/// no state ever comes to hold it.
		/// </summary>
		bool OpenToSettlement() const;

		/// <summary>
		/// Built purely to relay a gate network gap too wide for one link (U.SPC-T372, see <see cref="CreateVoidBase"/>):
		/// no star, no planet, just the artificial gravity/sun generator that gives its station an orbit to sit on. It
		/// belongs to no system-holding polity (see <see cref="OpenToSettlement"/>) and no navy's law reaches it, so a
		/// raider can lie up here in plain sight with no world nearby to notice.
		/// </summary>
		bool IsVoidBase = false;

		/// <summary>When its first gate was finished and it was opened to settlement (see <see cref="OpenToSettlement"/>); NaN for one
		/// that was on the network from the start, or is not yet.</summary>
		double OpenedJd = cs::Double::NaN;

		/// <summary>Has settlements to trade with (Sol, Alpha Centauri), rather than only stars and worlds.</summary>
		bool Settled() const;

		/// <summary>The catalogue entry it was built from; null for Sol, whose bodies are listed in <see cref="BodyCatalog"/>.</summary>
		cs::Ref<SystemSpec> Spec;

		/// <summary>Debris belts and discs (Epsilon Eridani's belts, Tau Ceti's disc); empty where none is known.</summary>
		cs::List<cs::Ref<DebrisDisc>> Discs;

		/// <summary>How much of the system the overview frames (km from the root): the planets, or a binary's two stars.</summary>
		double OverviewSpanKm = 0;

		/// <summary>The farthest any member or disc reaches from the root (km), when the system was built.</summary>
		double ExtentKm = 0;

		// private in C#; public so cs::New can make one (only the Create* factories do).
		StarSystem(const cs::String& name, DVec3 positionLy, const cs::Ref<SpaceGod::Core::Ephemeris>& ephemeris, const cs::Ref<SpaceGod::Core::World>& world, const cs::Ref<SystemSpec>& spec, const cs::List<cs::Ref<DebrisDisc>>& discs, double overviewSpanKm,
			bool isVoidBase = false);

		void Update(double jd);

		/// <summary>Who holds the system: the owner of its most populous settlement (see <see cref="Polities.HolderOf"/>); null if unsettled.</summary>
		cs::Ref<Faction> Holder() const;

		static cs::Ref<StarSystem> CreateSol(double jd, const cs::Ref<Polities>& polities = nullptr);

		/// <summary>
		/// A relay built in open space to bridge a gate-network gap wider than <see cref="GateNetwork.MaxLinkLy"/>
		/// (U.SPC-T372, see <see cref="GateNetwork.Build"/>): one body, the artificial gravity/sun generator at its
		/// centre (<see cref="VoidBaseBuilder"/>), no star and no planet anywhere near it to settle. Its world is
		/// unsettled the same way an empty real system's is (<see cref="World.CreateUnsettled"/>), so it never gains
		/// a state to hold it or a navy to watch its gates (see <see cref="IsVoidBase"/>).
		/// </summary>
		static cs::Ref<StarSystem> CreateVoidBase(const cs::String& name, DVec3 positionLy, double jd, const cs::Ref<Polities>& polities = nullptr);

		/// <summary>
		/// A void station of the real neighbourhood (<see cref="VoidStations"/>): the relay a Void Base is, plus the places
		/// about it (US-T123, <see cref="VoidSites"/>) -- an asteroid drift with its assay camp, a cracked or shattered
		/// planetoid and a sight or two -- all from the station's name alone.
		/// </summary>
		static cs::Ref<StarSystem> CreateVoidStation(const cs::String& name, DVec3 positionLy, double jd, const cs::Ref<Polities>& polities = nullptr);

		static cs::Ref<StarSystem> CreateAlphaCentauri(double jd, const cs::Ref<Polities>& polities = nullptr);

		/// <summary>
		/// A nearby system with the society planned for it (<see cref="NearbySocieties"/>, U.SPC-T90): its stations
		/// added to the catalogue entry, its settlements, lanes and traffic built by <see cref="World.CreateSettled"/>.
		/// A system with no society planned is left unsettled.
		/// </summary>
		static cs::Ref<StarSystem> CreateSettled(const cs::Ref<SystemSpec>& spec, double jd, const cs::Ref<Polities>& polities = nullptr);

		/// <summary>
		/// Builds a system from its catalogue entry. <paramref name="world"/> makes its game layer
		/// (settlements, fleets); left out, the system is unsettled (see <see cref="World.CreateUnsettled"/>), its
		/// survey service one of <paramref name="polities"/>.
		/// </summary>
		static cs::Ref<StarSystem> Create(const cs::Ref<SystemSpec>& spec, double jd, cs::Func<cs::Ref<SpaceGod::Core::World>, cs::Ref<SpaceGod::Core::Ephemeris>, double, int> world = nullptr, const cs::Ref<Polities>& polities = nullptr);

	private:
		/// <summary>Alpha Centauri keeps the seed its world was tuned with; the others get one from their names.</summary>
		static int SeedOf(const cs::String& name);

		/// <summary>
		/// The overview's reach: a little past the farthest planet (or binary member) about a star or
		/// barycentre, leaving out wide companions hundreds of AU out; at least 0.12 AU.
		/// </summary>
		static double OverviewSpan(const cs::Ref<BodyCatalog>& catalog);

	public:
		cs::String ToString() const { return Name; }
	private:
		StarSystem() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(StarSystem); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>A real star near the Sun, for the interstellar view (not simulated).</summary>
	struct SPACEGODCORE_API NearbyStar
	{
		cs::String Name;
		DVec3 PositionLy;
		cs::String SpectralType;
		double ApparentMagnitude = 0;
		Rgb Color;

		/// <summary>The photosphere's temperature, K, the colour comes from.</summary>
		double TeffK = 0;

		NearbyStar() = default;

		NearbyStar(const cs::String& name, double raDeg, double decDeg, double distanceLy, const cs::String& spectralType, double magnitude, double teffK);

		/// <summary>A star at <paramref name="positionLy"/> from Sol's root (a charted one, U.SPC-T413, see <see cref="StarEntry"/>).</summary>
		NearbyStar(const cs::String& name, DVec3 positionLy, const cs::String& spectralType, double magnitude, double teffK);

		/// <summary>Where it is in the galaxy (U.SPC-T412): a ship under its own drive can fly to it like to any system.</summary>
		GalacticPosition Origin() const { return GalacticPosition::FromLy(PositionLy); }

		/// <summary>Absolute magnitude (as seen from 10 parsecs), from the apparent one and the distance; NaN when unknown.</summary>
		double AbsoluteMagnitude() const
		{
			return cs::Double::IsNaN(ApparentMagnitude) ? cs::Double::NaN
				: ApparentMagnitude - 5 * (cs::Math::Log10(PositionLy.Length() / 3.261563777) - 1);
		}
	};

	/// <summary>
	/// The neighbourhood: the simulated star systems at their real positions, plus the nearest real
	/// stars around them for the view between systems. The seed of a larger map.
	/// </summary>
	class SPACEGODCORE_API Galaxy final
	{
		// C# passes `this` (to DeepSpace, the polities' Command and Statecraft, the lookouts): a non-owning reference.
		cs::Ref<Galaxy> Self() { return cs::Ref<Galaxy>(cs::Ref<Galaxy>(), this); }

	public:
		cs::List<cs::Ref<StarSystem>> Systems;
		cs::List<NearbyStar> NearbyStars;

		/// <summary>The jump-gate lattice between the systems (see <see cref="GateNetwork"/>).</summary>
		cs::Ref<GateNetwork> Network;

		/// <summary>
		/// The open space between the stars (U.SPC-T412): ships crossing it under their own cruise drives, the wrecks and
		/// derelicts left out there, and what happens when they meet (see <see cref="DeepSpace"/>). It runs every step with
		/// the systems, gates or no gates.
		/// </summary>
		cs::Ref<SpaceGod::Core::DeepSpace> DeepSpace;

		Galaxy();

		/// <summary>The ships crossing between systems and the voyages that send them (see <see cref="InterstellarTraffic"/>).</summary>
		cs::Ref<InterstellarTraffic> Traffic;

		/// <summary>The gates being surveyed, built and rebuilt (U.SPC-T97): how the network grows outward and heals after a war.</summary>
		cs::Ref<GateConstruction> Construction;

		/// <summary>Colony ships through the gates to the systems the new gates open (U.SPC-T293).</summary>
		cs::Ref<InterstellarColonisation> Colonisation;

		/// <summary>Raised when a system joins the simulation (a frontier star reached by its first gate): (the system, when).</summary>
		cs::Event<cs::Ref<StarSystem>, double> SystemAdded;

		/// <summary>
		/// Raised when the first gate into an empty system is finished and it is open to settlement (see
		/// <see cref="StarSystem.OpenToSettlement"/>): (the system, the faction that built the gate, when). Colonisation
		/// takes it from there: crowded factions anywhere the gates reach send colony ships to it (<see cref="InterstellarColonisation"/>).
		/// </summary>
		cs::Event<cs::Ref<StarSystem>, cs::Ref<Faction>, double> SystemOpened;

		/// <summary>The wars between the systems' powers: their goals, campaigns, occupations and treaties (U.SPC-T92, see <see cref="InterstellarWars"/>).</summary>
		cs::Ref<InterstellarWars> Wars;

		/// <summary>
		/// The interstellar polities (U.SPC-T91): every system's factions, one object each however many systems they
		/// hold, with one diplomacy table between them all.
		/// </summary>
		cs::Ref<SpaceGod::Core::Polities> Polities;

		/// <summary>
		/// Faction territory driven by the economy (US-T190, see <see cref="Territory"/>): reviewed daily, rich states paying
		/// colonists out, indebted ones selling stations, and every change of hands logged for the map. Made on the first
		/// <see cref="Update"/>; absent from older saves.
		/// </summary>
		cs::Ref<SpaceGod::Core::Territory> Territory;

		/// <summary>
		/// What the player has found (US-T259, see <see cref="DiscoveryLog"/>): kept by the game as the player goes, read by the
		/// discoveries screen. Display only: no simulation code reads it. Null until the game first sweeps; absent from older saves.
		/// </summary>
		cs::Ref<SpaceGod::Core::DiscoveryLog> DiscoveryLog;

		/// <summary>
		/// The difficulty this game is played at (US-T253, see <see cref="Difficulty.Record"/>): written by the game when it captures
		/// a save, put back into the setting when one is loaded. Empty in a save from before difficulty (which then keeps the setting).
		/// </summary>
		cs::String DifficultyRecord;

		/// <summary>
		/// How this game began (US-T247, see <see cref="NewGameSetup.Record"/>): the starting situation, the seed and the start day
		/// picked on the New Game setup page, written once when the game is made. Empty for a game started without the page (Quick
		/// Start, a script) and in a save from before it; display only (the save header and Load Game's line), nothing simulated reads it.
		/// </summary>
		cs::String SetupRecord;

		/// <summary>
		/// The system simulated at full detail (U.SPC-T104): the one the player is in. Every other system keeps
		/// running at once, but at coarse detail (<see cref="World.Coarse"/>), stepped on a fixed grid of
		/// <see cref="CoarseStepDays"/> rather than every frame. Null runs every system at full detail.
		/// </summary>
		cs::Ref<StarSystem> Focus;

		/// <summary>How often (days) a system at coarse detail is stepped: every six hours, on the hour.</summary>
		double CoarseStepDays = 0.25;

		/// <summary>Whether <paramref name="s"/> runs at full detail: it is the <see cref="Focus"/>, or there is none.</summary>
		bool AtFullDetail(const cs::Ref<StarSystem>& s) const { return Focus == nullptr || s == Focus; }

		/// <summary>
		/// Every system to <paramref name="jd"/>, then the traffic between them. The <see cref="Focus"/> is run to
		/// the moment; the others to the last <see cref="CoarseStepDays"/> mark at or before it, so where they
		/// stand never depends on the frame rate. A system that has just become the focus is first brought up
		/// to the moment at coarse detail, from its last mark, and only then promoted: it carries on from exactly
		/// the state it had reached, with every ship, cargo and battle where it was. One the player has left is
		/// demoted as it stands and steps on from the next mark.
		/// </summary>
		void Update(double jd);

		/// <summary>
		/// Brings <paramref name="system"/> into the simulation from <paramref name="jd"/> (a frontier star reached by its
		/// first gate, U.SPC-T97): it runs with the rest from now on (at coarse detail unless it is the focus), its gate
		/// traffic is taken on, and the plain star it was shown as among the <see cref="NearbyStars"/> gives way to it.
		/// </summary>
		void AddSystem(const cs::Ref<StarSystem>& system, double jd);

		// internal
		void RaiseOpened(const cs::Ref<StarSystem>& system, const cs::Ref<Faction>& by, double jd) { SystemOpened.Invoke(system, by, jd); }

		/// <summary>
		/// Makes <paramref name="system"/> the <see cref="Focus"/> now (the player has arrived there): it is brought
		/// up to <paramref name="jd"/> from its last coarse step, at the detail it was running at, and goes on at
		/// full detail from there, with nothing it was doing interrupted. The old focus drops to coarse detail
		/// at the next <see cref="Update"/>.
		/// </summary>
		void Promote(const cs::Ref<StarSystem>& system, double jd);

		/// <summary>The last coarse step at or before <paramref name="jd"/>.</summary>
		double CoarseMark(double jd) const { return cs::Math::Floor(jd / CoarseStepDays + 1e-9) * CoarseStepDays; }

		cs::Ref<StarSystem> Find(const cs::String& name) const;

		/// <summary>How far a system's gate sits from its root (U.SPC-T515): at the system's extreme edge, past its
		/// outermost planet, belt or station (<see cref="BodyCatalog.OuterEdgeKm"/>) by <see cref="JumpGate.RadiusKm"/>, so
		/// it is clear of the star and of every world; a lone star or barycentre is its radius plus that.</summary>
		static double GateDistanceKm(const cs::Ref<StarSystem>& s);

		/// <summary>Unit direction (J2000 ecliptic) from one system to another.</summary>
		static DVec3 Direction(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to) { return (to->PositionLy - from->PositionLy).Normalized(); }

		static double DistanceLy(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to) { return (to->PositionLy - from->PositionLy).Length(); }

		/// <summary>
		/// Sol, Alpha Centauri, and the nearest real systems beyond (<see cref="NearbySystems"/>), each
		/// settled more thinly the farther out it is (<see cref="NearbySocieties"/>), all joined by the jump-gate
		/// lattice (<see cref="GateNetwork"/>) that the interstellar traffic flies; the rest of the
		/// nearest stars are shown, not simulated, until a faction's gate reaches one (<see cref="GateConstruction"/>,
		/// <see cref="FrontierSystems"/>). Every other real system out to 100 ly and the void stations between them are in the
		/// galaxy from the start, charted within <see cref="StarDiscovery.KnownToLy"/> of Sol and undiscovered past it
		/// (<see cref="SetUpNeighbourhood"/>). Beyond them the rest of the galaxy, real catalogue stars and then stars made up
		/// from <paramref name="seed"/>, is charted and built only as a pilot comes near (<see cref="Viewpoint"/>, <see cref="Explore"/>).
		/// </summary>
		static cs::Ref<Galaxy> CreateDefault(double jd, int seed = DefaultSeed);

		/// <summary>
		/// Joins <see cref="Systems"/> (built into this galaxy's <see cref="Polities"/>) by the jump gates: the lattice
		/// <see cref="GateNetwork.Build"/> makes, or <paramref name="network"/> if given (a test's own map); arms the gates
		/// its states hold (<see cref="GateNetwork.Fortify"/>); and starts the traffic, the gate building and the wars between them.
		/// </summary>
		void Connect(double jd, const cs::Ref<GateNetwork>& network = nullptr);

		/// <summary>
		/// The nearest stars beyond Alpha Centauri (Wikipedia "List of nearest stars", from RECONS and
		/// Gaia DR3): name, right ascension, declination, distance (ly), spectral type, visual magnitude
		/// (NaN for brown dwarfs too faint to list), and a photosphere temperature for the colour.
		/// </summary>
		// IEnumerable + yield: built eagerly (no side effects).
		static cs::List<NearbyStar> Neighbours();

		// ================================================================ GalaxyStreaming.cs

		/// <summary>The seed the galaxy's own stars are made from, past the real ones near Sol (U.SPC-T413).</summary>
		static constexpr int DefaultSeed = 1977;

		/// <summary>This galaxy's seed: the same seed is the same galaxy, star for star, wherever it is explored from.</summary>
		int Seed = DefaultSeed;

		/// <summary>
		/// Where the pilot is (U.SPC-T413): the galaxy is charted about it and built near it as it moves
		/// (<see cref="Explore"/>), every <see cref="ExploreStepLy"/> it goes. Null charts nothing beyond what the galaxy began with.
		/// </summary>
		cs::Nullable<GalacticPosition> Viewpoint;

		/// <summary>How far the <see cref="Viewpoint"/> moves before the charts are brought up to date, light-years.</summary>
		static constexpr double ExploreStepLy = 0.25;

		/// <summary>A star charted or a system built this far beyond the chart's radius is let go again (if nothing holds it).</summary>
		static constexpr double ForgetMarginLy = 5;

	private:
		/// <summary>Where the charts were last brought up to date from (NaN: never).</summary>
		DVec3 exploredAtLy = DVec3(cs::Double::NaN, cs::Double::NaN, cs::Double::NaN);

		/// <summary>The stars put on the charts by exploring (the rest were there from the start and stay).</summary>
		cs::HashSet<cs::String, cs::OrdinalIgnoreCaseComparer> charted;

		/// <summary>The systems built by exploring, and so let go again when left untouched far behind.</summary>
		cs::HashSet<cs::String, cs::OrdinalIgnoreCaseComparer> streamed;

	public:
		/// <summary>Counts every change to the charts or the systems explored in, so a view of them knows to rebuild.</summary>
		int ChartVersion = 0;

		/// <summary>Raised when a system built by exploring is let go (see <see cref="Explore"/>): (the system, when).</summary>
		cs::Event<cs::Ref<StarSystem>, double> SystemRemoved;

		/// <summary>The systems built on demand by exploring that are running now.</summary>
		cs::List<cs::String> Streamed() const;

		/// <summary>
		/// Brings the charts up to date about <paramref name="atLy"/> (light-years from Sol): every star the
		/// <see cref="SystemSources"/> have within <see cref="GalaxyStreaming.ChartRadius"/> is charted (up to
		/// <see cref="GalaxyStreaming.MaxCharted"/>, nearest first) as a plain star among the <see cref="NearbyStars"/>; any
		/// within <see cref="GalaxyStreaming.SystemRadius"/> is built into a running system (<see cref="Realize(StarEntry, double)"/>);
		/// and what exploring put on the charts or built that is now well beyond them, and that nothing holds, is let go.
		/// Everything comes from the seed, so a star let go and reached again is the same star.
		/// </summary>
		void Explore(DVec3 atLy, double jd);

	private:
		/// <summary>Brings the charts up to date about the <see cref="Viewpoint"/> if it has moved <see cref="ExploreStepLy"/> since they were.</summary>
		void ExploreIfMoved(double jd);

		bool ChartedStar(const cs::String& name) const;

	public:
		/// <summary>The running system <paramref name="e"/> already is (by name, by the star it replaced, or by place), or null.</summary>
		cs::Ref<StarSystem> SimulatedAs(const cs::Ref<StarEntry>& e) const;

		/// <summary>
		/// The charted star called <paramref name="name"/> built into a running system from <paramref name="jd"/> (a pilot
		/// bound for it, or come near): the system it already is if it is one; null if no star of that name is charted.
		/// </summary>
		cs::Ref<StarSystem> Realize(const cs::String& name, double jd);

		/// <summary>
		/// <paramref name="e"/> built into a running system from <paramref name="jd"/> by the source that charted it, joined to
		/// the galaxy (<see cref="AddSystem"/>) in place of the plain star it was shown as; the system it already is, if it is one.
		/// </summary>
		cs::Ref<StarSystem> Realize(const cs::Ref<StarEntry>& e, double jd);

	private:
		/// <summary>
		/// Whether a system built by exploring can be let go: it is not where the player is, no gate leads in or is being
		/// built to it, nobody has settled it, no ship is in it, and no ship between the stars is bound for it.
		/// </summary>
		bool CanRelease(const cs::Ref<StarSystem>& s) const;

		/// <summary>Takes a system built by exploring out of the galaxy again, back to the plain star it was built from.</summary>
		void Release(const cs::Ref<StarSystem>& s, double jd);

		/// <summary>Whether a ship is bound for the plain star called <paramref name="name"/>, between the stars or on its way out to it.</summary>
		bool Held(const cs::String& name) const;

		// ================================================================ StarDiscovery.cs

	public:
		/// <summary>
		/// The stars of the real neighbourhood nobody has discovered yet (U.SPC-T456, see <see cref="DiscoveryState.Undiscovered"/>):
		/// held apart from the <see cref="NearbyStars"/>, so nothing that reads the charts (the interstellar view, the galaxy
		/// map, the FTL targets and courses, the surveys and cruises) sees them, until <see cref="Discover"/> moves one across.
		/// </summary>
		const cs::List<NearbyStar>& Undiscovered() const { return undiscovered; }

	private:
		cs::List<NearbyStar> undiscovered;

		/// <summary>The <see cref="Undiscovered"/> by name, for a quick look (the charts ask for every star they see).</summary>
		cs::HashSet<cs::String, cs::OrdinalIgnoreCaseComparer> undiscoveredNames;

	public:
		/// <summary>The stars found since the start, in order (see <see cref="Discover"/>).</summary>
		cs::List<cs::Ref<StarDiscoveredEvent>> Discoveries;

		/// <summary>Raised when a star is discovered (see <see cref="Discover"/>).</summary>
		cs::Event<cs::Ref<StarDiscoveredEvent>> StarDiscovered;

	private:
		/// <summary>When the lookouts last looked for stars (NaN: never); they look every <see cref="CoarseStepDays"/>.</summary>
		double lookedJd = cs::Double::NaN;

	public:
		/// <summary>Whether the star or void station called <paramref name="name"/> is still undiscovered.</summary>
		bool IsUndiscovered(const cs::String& name) const;

		/// <summary>Whether the star called <paramref name="name"/> is known (<see cref="DiscoveryState.Discovered"/>) or not yet.</summary>
		DiscoveryState DiscoveryOf(const cs::String& name) const { return IsUndiscovered(name) ? DiscoveryState::Undiscovered : DiscoveryState::Discovered; }

		/// <summary>
		/// Puts every system the real sources hold (Sol's neighbourhood out to <see cref="CatalogueStars.ChartedToLy"/>: the
		/// hand-built systems, the catalogue stars and the void stations; see <see cref="SystemSource.ChartedFromStart"/>) in the
		/// galaxy at the start (U.SPC-T456): those within <see cref="StarDiscovery.KnownToLy"/> of Sol charted among the
		/// <see cref="NearbyStars"/> (unless already running), the rest <see cref="Undiscovered"/>.
		/// </summary>
		void SetUpNeighbourhood();

		/// <summary>
		/// Discovers the star called <paramref name="name"/> (found by <paramref name="by"/>): it leaves the
		/// <see cref="Undiscovered"/> for the charts, where every view, course and voyage can see it from now on. False if it
		/// was not undiscovered.
		/// </summary>
		bool Discover(const cs::String& name, double jd, const cs::String& by = cs::String());

		/// <summary>Discovers every undiscovered star within <see cref="StarDiscovery.Range"/> of <paramref name="at"/>; how many it found.</summary>
		int DiscoverAbout(GalacticPosition at, double jd, const cs::String& by = cs::String());

	private:
		/// <summary>Every <see cref="CoarseStepDays"/>, each lookout (<see cref="StarDiscovery.Lookouts"/>) discovers what is within range of it.</summary>
		void LookForStars(double jd);

		/// <summary>Takes the star called <paramref name="name"/> off the <see cref="Undiscovered"/> without news (it is running now); whether it was there.</summary>
		bool Unhide(const cs::String& name);
		SG_SERIALIZABLE(Galaxy); // ObjectGraph registration (Persistence/Registrations)
	};
}
