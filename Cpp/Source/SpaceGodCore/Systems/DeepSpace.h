#pragma once
// Port of Assets/UnitySpace/Core/Systems/DeepSpace.cs, and the DeepSpace partial half of DeepSpaceLife.cs
// (see docs/MIGRATION.md: partial classes share one header; DeepSpaceLife.cs's own types are in DeepSpaceLife.h).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "GalacticPosition.h"
#include "Systems/CruiseDrive.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Ship;
	class StarSystem;
	class CruiseDriveSpec;
	class SystemReport;
	class Faction;
	class Galaxy;
	class GravityProjector;
	class DeepSpace;
	class DeepSpaceRoute;
	class DeepSpaceAmbush;
	class DeepSpaceFeatureSpec;
	struct GravityField;
	struct NearbyStar;

	/// <summary>
	/// A group (flagship first, then its escorts) crossing open space between the stars under its own cruise drive
	/// (U.SPC-T412, see <see cref="CruiseDriveSpec"/>). Like a ship inside a gate it belongs to no system's fleet while it
	/// is out here (the same hulls that left the system behind, a gone marker left in their old slots, see
	/// <see cref="Fleet.LetGo"/>), but unlike a gate passage it is somewhere the whole way: a real place in the
	/// galaxy (<see cref="PositionAt"/>), moving at a real speed, where it can meet other groups and find what lies out
	/// here, and from where it flies into the far system at the edge of its star's well.
	/// </summary>
	class SPACEGODCORE_API Cruise final
	{
	public:
		/// <summary>Flagship first, then its escorts.</summary>
		cs::List<cs::Ref<Ship>> Ships;

		/// <summary>Which of <see cref="Ships"/> were hired fighters in the system it left (so the far system's escorts take them on).</summary>
		cs::HashSet<cs::Ref<Ship>> Hired;

		/// <summary>Which of <see cref="Ships"/> ride docked aboard another of them, and aboard which (U.SPC-T419, see
		/// <see cref="FtlCarriage"/>): fighters and small craft with no drive of their own, carried in a capital ship's
		/// hangars, and escorts clamped to a carrier's hull (U.SPC-T473). They come out with the group where it drops out,
		/// and are lost with the hull carrying them.</summary>
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<Ship>> Aboard;

		/// <summary>The craft docked aboard <paramref name="carrier"/>.</summary>
		cs::List<cs::Ref<Ship>> CarriedBy(cs::Ref<Ship> carrier);

		/// <summary>The system it set out from.</summary>
		cs::Ref<StarSystem> From;

		/// <summary>The system it is bound for, or null when it is bound for a star nobody simulates (see <see cref="ToName"/>).</summary>
		cs::Ref<StarSystem> To;

		/// <summary>The name of where it is bound: the system's, or the plain star's.</summary>
		cs::String ToName;

		/// <summary>The drive it flies on (the flagship's).</summary>
		cs::Ref<CruiseDriveSpec> Drive;

		/// <summary>Where it will drop out: the edge of the far star's well, on the side it comes in from.</summary>
		GalacticPosition Target;

		/// <summary>This leg: where it began, where it ends (the <see cref="Target"/>), when its charge began, and its speed through it.</summary>
		GalacticPosition Start, End;
		double LegJd = 0;
		CruiseProfile Profile;

		/// <summary>Dropped out in open space (to fight, to salvage, or at a plain star it was bound for), until <see cref="HoldUntilJd"/>
		/// (infinite: until it is given somewhere else to go, see <see cref="DeepSpace.Retarget"/>).</summary>
		bool Holding = false;
		double HoldUntilJd = 0;

		/// <summary>When it first engaged, from its home system.</summary>
		double SetOutJd = 0;

		/// <summary>The news it carries from the system it left (U.SPC-T100), handed over where it arrives.</summary>
		cs::Ref<cs::List<cs::Ref<SystemReport>>> News;

		/// <summary>The player's own hull, jumped here from the helm (U.SPC-T417, see <see cref="HelmFtl"/>): when it drops
		/// out nobody else sends it anywhere, the helm takes it on from there.</summary>
		bool Piloted = false;

		/// <summary>Not a group flying but a course being worked out for one (see <see cref="FtlNavigation"/>): it reckons with
		/// only the stars on the charts, so a course never gives away a star nobody has discovered (U.SPC-T456).</summary>
		bool Plan = false;

		/// <summary>The hull's own velocity in normal space when it engaged (ecliptic, km/s): FTL carries momentum, so it
		/// comes out with the velocity it went in with (U.SPC-T418, see <see cref="FtlRules"/>).</summary>
		DVec3 CarriedVelocityKmS;

		/// <summary>Pulled out of FTL by a star's well or a gravity projector (U.SPC-T418): crossing it now under its
		/// ordinary drive, from the edge it was pulled out at to the far side (this leg's <see cref="End"/>), where it spools
		/// again for its <see cref="Target"/>.</summary>
		bool Crossing = false;

		/// <summary>What pulled it out last (a star's or a projector's name), or null.</summary>
		cs::String InterdictedBy;

		cs::Ref<Ship> Leader() const;

		/// <summary>When this leg ends (it drops out at its <see cref="End"/>); infinite while it holds.</summary>
		double ArriveJd() const;

		/// <summary>The way this leg heads, ecliptic (unit).</summary>
		DVec3 Direction() const;

		/// <summary>Under way at <paramref name="jd"/>: past its charge and not yet dropped out.</summary>
		bool Engaged(double jd) const;

		/// <summary>Where it is at <paramref name="jd"/>: on its leg's line, by its speed profile, from where it began.</summary>
		GalacticPosition PositionAt(double jd) const;

		/// <summary>Speed at <paramref name="jd"/>, km/s.</summary>
		double SpeedKmS(double jd) const;

		/// <summary>Light-years still to go to its <see cref="Target"/>.</summary>
		double LyToGo(double jd) const;

		/// <summary>
		/// Sets a new leg from <paramref name="from"/> to <see cref="Target"/>, its charge starting at <paramref name="jd"/>
		/// (none if the drive is already <paramref name="charged"/>, as at the helm, U.SPC-T417), once every hull's drive has
		/// cooled from its last drop-out; every hull pays the jump's reaction mass (U.SPC-T418, <see cref="FtlRules.PropellantFor"/>).
		/// </summary>
		void SetLeg(GalacticPosition from, double jd, bool charged = false);

		/// <summary>Pulled out at <paramref name="entry"/> at <paramref name="jd"/>: across the field to <paramref name="exit"/>
		/// under the ordinary drive, at <see cref="FtlRules.CrossingSpeedKmS"/>.</summary>
		void SetCrossing(GalacticPosition entry, GalacticPosition exit, double jd);

		cs::String ToString() const;
	};

	/// <summary>What lies out between the stars for a passing group to find.</summary>
	enum class DeepSpaceObjectKind : int
	{
		/// <summary>What is left of a hull destroyed out here.</summary>
		Wreck,

		/// <summary>A hull left drifting whole, its crew gone.</summary>
		Derelict,

		/// <summary>A planet with no star, wandering between them (U.SPC-T421): its own small gravity well pulls a passing
		/// group out of FTL like a star's does (<see cref="DeepSpace.RogueWells"/>).</summary>
		RoguePlanet,

		/// <summary>A cloud of gas and dust light-hours or more across (U.SPC-T421): inside it sensors see only a fraction as
		/// far (<see cref="DeepSpace.SensorRangeKm"/>), so groups slip past each other and wrecks go unseen.</summary>
		Nebula,

		/// <summary>A comet or an iceball adrift between the stars (U.SPC-T421): water a group short of reaction mass can
		/// drop out to mine (<see cref="IceRefuelling"/>).</summary>
		IceBody,

		/// <summary>A power's deep-space relay on the line between two systems (U.SPC-T421): it passes the news between them
		/// when the gates cannot (<see cref="DeepSpace.RelayLagDays"/>).</summary>
		Relay,

		/// <summary>A power's listening post out on an approach (U.SPC-T421): it hears its enemies pass and tells home
		/// (<see cref="DeepSpaceOutposts"/>).</summary>
		ListeningPost,

		/// <summary>A hull adrift calling for help, its crew still aboard (U.SPC-T421, <see cref="DistressCalls"/>), or a
		/// pirate lure dressed up as one.</summary>
		Distress,
	};
	SPACEGODCORE_API const char* ToString(DeepSpaceObjectKind v);

	/// <summary>Something in open space between the stars (U.SPC-T412): a wreck or a derelict, where it was left, and what is aboard.</summary>
	class SPACEGODCORE_API DeepSpaceObject final
	{
	public:
		DeepSpaceObjectKind Kind = DeepSpaceObjectKind::Wreck;
		cs::String Name;
		GalacticPosition Position;

		/// <summary>What a salvager takes off it, credits.</summary>
		double SalvageCredits = 0;

		/// <summary>Whose hull it was.</summary>
		cs::Ref<Faction> LostBy;

		/// <summary>When it was left out here.</summary>
		double SinceJd = 0;

		/// <summary>How far it reaches, km: a nebula's extent (0 for a point-like thing).</summary>
		double RadiusKm = 0;

		/// <summary>A rogue planet's gravitational parameter, km³/s² (its well is sized from it by <see cref="FtlRules"/>).</summary>
		double Gm = 0;

		/// <summary>Whose it is: a relay's or a listening post's power.</summary>
		cs::Ref<Faction> Owner;

		/// <summary>The system at the end of its route that it reports to (a relay's or a listening post's home).</summary>
		cs::String HomeName;

		/// <summary>The two stars of the route it lies on (U.SPC-T421), in name order; null for what was left off any route.</summary>
		cs::String RouteA, RouteB;

		/// <summary>What the first survey of it pays its surveyor, credits (more for one off the gate network), and who made it.</summary>
		double SurveyCredits = 0;
		cs::Ref<Faction> SurveyedBy;

		/// <summary>Water an ice body still holds for a passing group's tanks, tonnes.</summary>
		double IceTonnes = 0;

		/// <summary>A distress call's crew still alive aboard, until it falls silent at <see cref="ExpiresJd"/>.</summary>
		int Survivors = 0;
		double ExpiresJd = cs::Double::PositiveInfinity;

		/// <summary>A false distress call: the pirate clan lying in wait at it.</summary>
		cs::Ref<Faction> LureBy;

		/// <summary>The leaders a listening post has already heard pass.</summary>
		cs::Ref<cs::HashSet<cs::Ref<Ship>>> Heard;

		/// <summary>Lies off the gate network (on a route no gate link joins).</summary>
		bool OffNetwork = false;

		cs::String ToString() const;
	};

	/// <summary>A line of what has happened out between the stars, for the logs and the HUD.</summary>
	class DeepSpaceEvent final
	{
	public:
		double Jd = 0;
		cs::String Text;
		GalacticPosition Where;
	};

	/// <summary>
	/// Something that can happen to the groups out between the stars (U.SPC-T412): asked once a <see cref="DeepSpace.StepDays"/>
	/// step for what happened in it. A new kind of deep-space encounter plugs in by registering a factory in
	/// <see cref="DeepSpace.EncounterKinds"/>, never by editing the loop that runs them.
	/// </summary>
	class IDeepSpaceEncounter
	{
	public:
		virtual ~IDeepSpaceEncounter() = default;
		// (The deep space is passed by reference: it hands itself to its encounters, from inside its own Update.)
		virtual void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng) = 0;
	};

	/// <summary>
	/// The open space between the star systems (U.SPC-T412): the groups crossing it under their own cruise drives
	/// (<see cref="Cruise"/>), the wrecks and derelicts left out here, and what happens when they meet. It is not empty
	/// and it does not stop: every step the groups fly on, the encounters (<see cref="IDeepSpaceEncounter"/>) are run,
	/// and a group at the end of its way drops out at the edge of its star's well and flies into that system as any
	/// ship there would, from the far side of the star it came from. A group bound for a star nobody simulates drops
	/// out there and holds in open space until it is sent on (<see cref="ReachedStar"/>, <see cref="Retarget"/>).
	///
	/// Positions are <see cref="GalacticPosition"/>s, sector and offset, so a group a long way from Sol is placed as
	/// exactly as one next to it.
	/// </summary>
	class SPACEGODCORE_API DeepSpace final
	{
		// The galaxy this belongs to (a back-reference: Galaxy makes its DeepSpace with `this`). Written Core::Galaxy
		// throughout the class, as its member Galaxy() hides the type's name in here.
		Core::Galaxy* galaxy;
		cs::Random rng;
		cs::List<cs::Ref<Cruise>> cruises;
		cs::List<cs::Ref<DeepSpaceObject>> objects;
		cs::List<cs::Ref<DeepSpaceEvent>> events;
		double nextMarkJd = cs::Double::NaN;

	public:
		/// <summary>News carried by a cruise (a C# List&lt;SystemReport&gt;, null when there is none).</summary>
		using NewsList = cs::Ref<cs::List<cs::Ref<SystemReport>>>;

		/// <summary>The groups out between the stars now, under way or holding.</summary>
		const cs::List<cs::Ref<Cruise>>& Cruises() const;

		/// <summary>The wrecks and derelicts out between the stars.</summary>
		const cs::List<cs::Ref<DeepSpaceObject>>& Objects() const;

		/// <summary>What has happened out here lately, oldest first (the last <see cref="MaxEvents"/>).</summary>
		const cs::List<cs::Ref<DeepSpaceEvent>>& Events() const;

		/// <summary>Every hull out between the stars now (in no system's fleet until it drops out into one).</summary>
		cs::List<cs::Ref<Ship>> InFlight() const;

		/// <summary>The encounters run every step: one of each kind in <see cref="EncounterKinds"/>, made when the deep space was.</summary>
		cs::List<cs::Ref<IDeepSpaceEncounter>> Encounters;

		/// <summary>The kinds of encounter every new deep space runs (the extension point: add a factory to add a kind).</summary>
		static cs::List<cs::Func<cs::Ref<IDeepSpaceEncounter>>>& EncounterKinds();

		/// <summary>How often (days) the encounters are run: about every hour and a quarter, on a fixed grid, so what
		/// happens never depends on the frame rate.</summary>
		static constexpr double StepDays = 0.05;

		/// <summary>How many lines of <see cref="Events"/> are kept.</summary>
		static constexpr int MaxEvents = 200;

		/// <summary>The most wrecks and derelicts kept: past it, the oldest are lost to the dark.</summary>
		static constexpr int MaxObjects = 400;

		/// <summary>Raised when a group drops out into a simulated system and is in its fleet: (the group, when). The traffic takes it from there.</summary>
		cs::Event<cs::Ref<Cruise>, double> Arrived;

		/// <summary>Raised when a group drops out at a plain star it was bound for and holds there: (the group, when).</summary>
		cs::Event<cs::Ref<Cruise>, double> ReachedStar;

		explicit DeepSpace(Core::Galaxy* galaxy, int seed = 4127);

		// ---------------------------------------------------------------- setting out

		/// <summary>
		/// Where a ship in <paramref name="from"/> engages its drive for <paramref name="target"/>, km from the system's root:
		/// at the edge of the star's well, on the line to the target.
		/// </summary>
		static DVec3 DeparturePointKm(cs::Ref<StarSystem> from, GalacticPosition target);

		/// <summary>Where a group from <paramref name="from"/> drops out at <paramref name="star"/>: the edge of its well
		/// (<paramref name="wellKm"/> out), on the side facing the way it comes in.</summary>
		static GalacticPosition ArrivalPoint(GalacticPosition star, GalacticPosition from, double wellKm);

		/// <summary>
		/// Puts <paramref name="ships"/> (the same hulls, flagship first, already out of every fleet) into open space at
		/// <paramref name="fromKm"/> in <paramref name="from"/> (km from its root), bound for <paramref name="to"/> on
		/// <paramref name="drive"/>; it charges from <paramref name="jd"/> (or engages at once if already <paramref name="charged"/>).
		/// It drops out at the edge of <paramref name="to"/>'s well on the side facing the way it comes, or, given
		/// <paramref name="dropDir"/> (a way out from the star in its system's frame), there: a jump strike group coming in on
		/// a flank (U.SPC-T474).
		/// </summary>
		cs::Ref<Cruise> Launch(const cs::List<cs::Ref<Ship>>& ships, cs::Ref<StarSystem> from, DVec3 fromKm, cs::Ref<StarSystem> to, cs::Ref<CruiseDriveSpec> drive, double jd,
			NewsList news = nullptr, bool charged = false, cs::Nullable<DVec3> dropDir = std::nullopt);

		/// <summary>As <see cref="Launch(IEnumerable{Ship}, StarSystem, DVec3, StarSystem, CruiseDriveSpec, double, List{SystemReport})"/>, but bound
		/// for a plain star (one nobody simulates yet): it drops out at the edge of its well and holds there.</summary>
		cs::Ref<Cruise> LaunchToStar(const cs::List<cs::Ref<Ship>>& ships, cs::Ref<StarSystem> from, DVec3 fromKm, NearbyStar star, cs::Ref<CruiseDriveSpec> drive, double jd,
			NewsList news = nullptr);

	private:
		cs::Ref<Cruise> Launch(const cs::List<cs::Ref<Ship>>& ships, cs::Ref<StarSystem> from, GalacticPosition start, cs::Ref<StarSystem> to, const cs::String& toName, GalacticPosition target,
			cs::Ref<CruiseDriveSpec> drive, double jd, NewsList news, bool charged);

		static cs::String More(const cs::Ref<Cruise>& c);

	public:
		/// <summary>Sends <paramref name="c"/> on to <paramref name="to"/> from where it is, its charge beginning at <paramref name="jd"/>.</summary>
		void Retarget(cs::Ref<Cruise> c, cs::Ref<StarSystem> to, double jd);

		/// <summary>Drops <paramref name="c"/> out of its cruise where it is at <paramref name="jd"/>, for <paramref name="days"/> (then on
		/// toward its target again, charging first), or for good with an infinite hold.</summary>
		void DropOut(cs::Ref<Cruise> c, double jd, double days);

		// ---------------------------------------------------------------- gravity fields (U.SPC-T418)

	private:
		cs::List<cs::Ref<GravityProjector>> projectors;

	public:
		/// <summary>The gravity projectors out between the stars.</summary>
		const cs::List<cs::Ref<GravityProjector>>& Projectors() const;

		/// <summary>Places <paramref name="p"/> in open space: from now it pulls passing hulls (not its owner's) out of FTL.</summary>
		cs::Ref<GravityProjector> AddProjector(cs::Ref<GravityProjector> p);

		void RemoveProjector(cs::Ref<GravityProjector> p);

		/// <summary>A source of fields that pull a group out of FTL: (the deep space, the group, a place on its way, how far
		/// about it to look, km) to the fields there. A new kind of interdiction plugs in by adding one.</summary>
		using FieldSource = cs::Func<cs::List<GravityField>, DeepSpace&, cs::Ref<Cruise>, GalacticPosition, double>;

		/// <summary>Where interdicting fields come from: the stars' own mass, and the gravity projectors.</summary>
		static cs::List<FieldSource>& FieldSources();

		/// <summary>The wells of the stars (running systems and plain ones on the charts) within <paramref name="km"/> of <paramref name="near"/>.</summary>
		static cs::List<GravityField> StarWells(DeepSpace& space, cs::Ref<Cruise> c, GalacticPosition near, double km);

		/// <summary>The fields of the projectors that would pull <paramref name="c"/> out (any but its own side's).</summary>
		static cs::List<GravityField> ProjectorFields(DeepSpace& space, cs::Ref<Cruise> c, GalacticPosition near, double km);

		/// <summary>Every field about the run from <paramref name="from"/> to <paramref name="to"/> that would pull <paramref name="c"/> out.</summary>
		cs::List<GravityField> FieldsAlong(cs::Ref<Cruise> c, GalacticPosition from, GalacticPosition to);

		/// <summary>
		/// Pulls <paramref name="c"/> out of FTL at <paramref name="entry"/> on the edge of <paramref name="field"/> at
		/// <paramref name="jd"/>: its drives start cooling, and it crosses the field under its ordinary drive, straight on
		/// toward where it was going, to spool again on the far side.
		/// </summary>
		void Interdict(cs::Ref<Cruise> c, GravityField field, GalacticPosition entry, double jd);

		// ---------------------------------------------------------------- the step

		/// <summary>
		/// Everything out between the stars to <paramref name="jd"/>: groups holding go on when their hold is up, groups due
		/// drop out at their stars (at the moment each is due, in that order), and the encounters run on the fixed grid of
		/// <see cref="StepDays"/>.
		/// </summary>
		void Update(double jd);

	private:
		static double Mark(double jd);

		/// <summary>Holds that are up, and arrivals due, by <paramref name="jd"/>, in the order they fall.</summary>
		void Due(double jd);

		/// <summary>At the end of its way: into the far system's fleet, flying in from the well's edge, or holding at a plain star.</summary>
		void Drop(cs::Ref<Cruise> c, double jd);

	public:
		// ---------------------------------------------------------------- what happens out here

		/// <summary>Notes <paramref name="text"/> in the deep-space log.</summary>
		void Log(double jd, const cs::String& text, GalacticPosition where);

		/// <summary>Leaves <paramref name="o"/> out here (the oldest wrecks, derelicts and distress calls lost when there are
		/// more than <see cref="MaxObjects"/> of them; what lies along the routes, U.SPC-T421, stays).</summary>
		void Add(cs::Ref<DeepSpaceObject> o);

		/// <summary>Takes <paramref name="o"/> away (salvaged).</summary>
		void Remove(cs::Ref<DeepSpaceObject> o);

		/// <summary>Whether <paramref name="a"/> and <paramref name="b"/> would fight on sight (the one diplomacy table every system shares).</summary>
		bool Hostile(cs::Ref<Faction> a, cs::Ref<Faction> b);

		/// <summary>
		/// Destroys <paramref name="ship"/> of <paramref name="c"/> out here at <paramref name="jd"/>, leaving its wreck: the
		/// next hull takes the lead (and the voyage) if it was the flagship, and the group is gone when none is left.
		/// </summary>
		void Destroy(cs::Ref<Cruise> c, cs::Ref<Ship> ship, double jd, const cs::String& how);

		/// <summary>What a salvager gets off a wreck: some of its cargo's worth, and its parts.</summary>
		static double WreckValue(cs::Ref<Ship> ship);

		/// <summary>What a wreck's parts are worth to a salvager, credits (three times as much for a warship's).</summary>
		static constexpr double WreckPartsCredits = 40000;

		/// <summary>
		/// How close (km) the paths of <paramref name="a"/> and <paramref name="b"/> come between <paramref name="fromJd"/> and
		/// <paramref name="toJd"/>: the nearest point of their separation, taken as straight over so short a step.
		/// </summary>
		static double ClosestKm(cs::Ref<Cruise> a, cs::Ref<Cruise> b, double fromJd, double toJd);

		/// <summary>How close (km) <paramref name="c"/> passes <paramref name="place"/> between <paramref name="fromJd"/> and <paramref name="toJd"/>.</summary>
		static double ClosestKm(cs::Ref<Cruise> c, GalacticPosition place, double fromJd, double toJd);

	private:
		static double Closest(DVec3 p0, DVec3 p1);

	public:
		/// <summary>A group's fighting strength out here: its hulls' together.</summary>
		static double Strength(cs::Ref<Cruise> c);

		// ================================================================ DeepSpaceLife.cs

		/// <summary>
		/// Whether routes are seeded with what lies along them (U.SPC-T421). The default galaxy turns it on; a bare galaxy
		/// built by hand (a test's two empty beacons) keeps open space empty unless it asks.
		/// </summary>
		bool RouteLife = false;

	private:
		cs::HashSet<cs::String> seededRoutes;
		cs::List<cs::Ref<DeepSpaceRoute>> routes;
		cs::List<cs::Ref<DeepSpaceAmbush>> ambushes;
		cs::HashSet<cs::String, cs::OrdinalIgnoreCaseComparer> surveyedStars;

		/// <summary>Numbers for the life out here, apart from the fights' (so they draw as they did before it).</summary>
		cs::Random lifeRng{ 9157 };

	public:
		cs::Random& LifeRng();
		Core::Galaxy* Galaxy() const;

		/// <summary>The routes seeded so far.</summary>
		const cs::List<cs::Ref<DeepSpaceRoute>>& Routes() const;

		/// <summary>The bands lying in wait on the routes now.</summary>
		const cs::List<cs::Ref<DeepSpaceAmbush>>& Ambushes() const;

		/// <summary>Souls brought home from distress calls, and finds surveyed, so far.</summary>
		int Rescued = 0;
		int SurveyFindsMade = 0;

		/// <summary>How far sensors see inside a nebula, as a share of the open-space range.</summary>
		static constexpr double NebulaSensorShare = 0.1;

		/// <summary>The field a band lying in wait throws, km: big enough to pull a passer out within reach of its guns.</summary>
		static constexpr double AmbushFieldKm = 2e8;

		/// <summary>Where a star or a system named <paramref name="name"/> is, if it is one.</summary>
		bool TryStar(const cs::String& name, GalacticPosition& at);

	private:
		static cs::String RouteKey(const cs::String& a, const cs::String& b);

	public:
		/// <summary>Whether the route between <paramref name="a"/> and <paramref name="b"/> has been seeded.</summary>
		bool Seeded(const cs::String& a, const cs::String& b);

		/// <summary>
		/// Seeds the route between two stars with what lies along it, once (U.SPC-T421; nothing when <see cref="RouteLife"/>
		/// is off): drawn from the route's names and the galaxy's seed, so it is the same whichever way it is first flown.
		/// Returns whether it seeded anything new.
		/// </summary>
		bool SeedRoute(cs::String aName, GalacticPosition a, cs::String bName, GalacticPosition b);

		/// <summary>Every pair of running systems within <see cref="DeepSpaceFeatures.MaxRouteLy"/>, seeded now (the routes the
		/// powers' relays and listening posts already stand on when the galaxy begins).</summary>
		void SeedSystemRoutes();

	private:
		bool Linked(const cs::String& a, const cs::String& b);

		/// <summary>A settled system's own power (its government's navy), or null.</summary>
		cs::Ref<StarSystem> SettledEnd(const cs::String& name, cs::Ref<Faction>& power);

		void PlaceOwned(cs::Ref<DeepSpaceFeatureSpec> spec, cs::Ref<DeepSpaceRoute> route, cs::Random& r);

		cs::Ref<DeepSpaceObject> Place(cs::Ref<DeepSpaceFeatureSpec> spec, cs::Ref<DeepSpaceRoute> route, double t, double offKm, cs::Random& r, cs::Ref<Faction> owner, const cs::String& home);

	public:
		// ---------------------------------------------------------------- what the features do

		/// <summary>The wells of the rogue planets near <paramref name="near"/>: they pull a passing group out of FTL like a star's.</summary>
		static cs::List<GravityField> RogueWells(DeepSpace& space, cs::Ref<Cruise> c, GalacticPosition near, double km);

		/// <summary>The nebula <paramref name="at"/> is inside, or null.</summary>
		cs::Ref<DeepSpaceObject> NebulaAt(GalacticPosition at);

		/// <summary>How far sensors at <paramref name="at"/> see, from their open-space range <paramref name="openKm"/>: only
		/// <see cref="NebulaSensorShare"/> of it inside a nebula.</summary>
		double SensorRangeKm(GalacticPosition at, double openKm);

		/// <summary>How slow a deep-space relay chain passes the news against a gate's relay over the same light-years.</summary>
		static constexpr double RelaySlowdown = 2;

		/// <summary>
		/// Days the news takes between two systems by a deep-space relay on the route joining them (U.SPC-T421), not counting
		/// the light time to and from the relay's dishes; infinite when no relay stands there.
		/// </summary>
		double RelayLagDays(cs::Ref<StarSystem> a, cs::Ref<StarSystem> b);

		/// <summary>Marks the plain star <paramref name="name"/> surveyed; true the first time (its survey is a find).</summary>
		bool SurveyStar(const cs::String& name);

		/// <summary>Whether a group's flagship surveys what it passes: a deep-space explorer, or any hull out on a survey.</summary>
		static bool Surveys(cs::Ref<Cruise> c);

		// ---------------------------------------------------------------- ambushes on the routes

		/// <summary>
		/// Sends <paramref name="ships"/> (out of every fleet already, flagship first) from <paramref name="from"/> to a point in
		/// open space, <paramref name="station"/>, named <paramref name="name"/>: it drops out there and holds, as at a plain star.
		/// </summary>
		cs::Ref<Cruise> LaunchToPoint(const cs::List<cs::Ref<Ship>>& ships, cs::Ref<StarSystem> from, DVec3 fromKm, GalacticPosition station, const cs::String& name, cs::Ref<CruiseDriveSpec> drive,
			double jd, NewsList news = nullptr);

		/// <summary>
		/// <paramref name="band"/>, at its station (already given its leg home, starting when the wait is over), lies in wait
		/// there for <paramref name="days"/>: a gravity projector of its own pulls every passer not of its side out of FTL within
		/// reach of its guns (<see cref="CruiseMeeting"/> does the rest).
		/// </summary>
		cs::Ref<DeepSpaceAmbush> LieInWait(cs::Ref<Cruise> band, double jd, double days, const cs::String& where);

		/// <summary>Lifts the ambushes whose time is up, whose band has gone, or whose band has set off.</summary>
		void LiftAmbushes(double jd);
	private:
		DeepSpace() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(DeepSpace); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// A group under way whose straight run takes it into a gravity field (U.SPC-T418): a star's well (not the one it
	/// left nor the one it is bound for) or a gravity projector's. It is pulled out at the field's edge, at the moment
	/// it reaches it, and crosses under its ordinary drive (see <see cref="DeepSpace.Interdict"/>).
	/// </summary>
	class SPACEGODCORE_API FtlInterdiction final : public IDeepSpaceEncounter
	{
	public:
		void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng) override;

	private:
		/// <summary>When <paramref name="c"/> reaches <paramref name="at"/> on its leg, between <paramref name="t0"/> and <paramref name="t1"/>.</summary>
		static double When(const cs::Ref<Cruise>& c, GalacticPosition at, double t0, double t1);
	};

	/// <summary>
	/// Hostile groups whose paths cross between the stars (U.SPC-T412): a navy and its enemy's ships, or raiders and a
	/// hull worth their while, within <see cref="RangeKm"/> of each other. The stronger hand may pull the other out of
	/// cruise and fight. Raiders who win rob the hold and let the hull go (U.SPC-T374: a hull is worth more taken than
	/// shot apart); a navy that wins leaves the beaten group wrecked out here. The loser's escorts are lost with it.
	/// Either way the winners hold a while (the fight, the boarding) and then go on.
	/// </summary>
	class SPACEGODCORE_API CruiseMeeting final : public IDeepSpaceEncounter
	{
	public:
		/// <summary>How near (km) two groups' paths must come for either to pull the other out of cruise: about a light-hour.</summary>
		static constexpr double RangeKm = 1e9;

		/// <summary>How long a fight out here keeps the winners from going on, days.</summary>
		static constexpr double FightDays = 0.25;

		/// <summary>A hand must be at least this share as strong as the other to start a fight.</summary>
		static constexpr double NerveRatio = 0.5;

		void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng) override;

	private:
		static bool Aggressive(const cs::Ref<Ship>& leader);

		static bool Loot(const cs::Ref<Cruise>& c);

		/// <summary>Whether a fight happens between <paramref name="a"/> and <paramref name="b"/>, and its outcome.</summary>
		static bool Resolve(DeepSpace& space, cs::Ref<Cruise> a, cs::Ref<Cruise> b, double jd, cs::Random& rng);
	};

	/// <summary>
	/// A group passing within <see cref="RangeKm"/> of a wreck or derelict (U.SPC-T412) drops out to strip it, keeps what
	/// it finds (the flagship's purse and its flag's treasury), and goes on after <see cref="SalvageDays"/>. A war fleet
	/// on campaign does not stop for it.
	/// </summary>
	class SPACEGODCORE_API WreckSalvage final : public IDeepSpaceEncounter
	{
	public:
		/// <summary>How near (km) a group must pass for its sensors to pick out a wreck: about a light-hour.</summary>
		static constexpr double RangeKm = 1e9;

		/// <summary>How long stripping a wreck takes, days.</summary>
		static constexpr double SalvageDays = 0.5;

		void Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng) override;
	};
}
