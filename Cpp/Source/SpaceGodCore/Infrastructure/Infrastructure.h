#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Infrastructure/Infrastructure.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Astro.h"
#include "BodyDefinition.h"
#include "DVec3.h"
#include "Economy/Industry.h"
#include "Lagrange.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class BodyCatalog;
	class Economy;
	class Ephemeris;
	class Faction;
	class ParkingOrbit;
	class Satellite;
	class Settlement;
	class Ship;

	enum class StructureKind : int
	{
		/// <summary>An orbital shipyard: an open frame with the hull being built in its cradle.</summary>
		Shipyard,

		/// <summary>A factory station (foundry, fab, works, arsenal, labs) with its radiators.</summary>
		Factory,

		/// <summary>A spinning habitat ring for a sizeable settlement's orbital population.</summary>
		Habitat,

		/// <summary>A crewed station that really flies (the ISS, Tiangong).</summary>
		CrewedStation,

		/// <summary>A space telescope (Hubble, JWST).</summary>
		Telescope,

		/// <summary>A robotic orbiter (Mars Reconnaissance Orbiter, LRO...).</summary>
		Probe,

		/// <summary>A propellant depot: tank farms parked where ships leaving a world pass (game layer).</summary>
		Depot,

		/// <summary>A communications relay (Queqiao over the Moon's far side, the game layer's signal stations).</summary>
		Relay,

		/// <summary>A crewed transfer station, the gateway between a world and its neighbours (game layer).</summary>
		Gateway,
	};
	SPACEGODCORE_API const char* ToString(StructureKind v);

	/// <summary>
	/// A permanent thing in orbit: game-layer industry made physical (a settlement's shipyard and
	/// factories, its habitat), or a real spacecraft. It holds a circular orbit about its body, or
	/// sits at one of the Lagrange points of its body and what that orbits (<see cref="Core.Lagrange"/>),
	/// moving with the pair so it stays co-orbital.
	/// </summary>
	class SPACEGODCORE_API OrbitalStructure final
	{
	public:
		cs::String Name;
		StructureKind Kind = StructureKind::Shipyard;
		int Body = 0;
		cs::Ref<ParkingOrbit> Orbit;
		cs::Ref<SpaceGod::Core::Settlement> Settlement;
		IndustryKind Industry = static_cast<IndustryKind>(0);

	private:
		cs::Ref<Faction> owner;

	public:
		/// <summary>Its settlement's owner, whoever that is now (a port taken in a siege takes its yards
		/// and stations with it); its own for a structure with no settlement, such as a real spacecraft.</summary>
		cs::Ref<Faction> Owner() const;
		void SetOwner(const cs::Ref<Faction>& value) { owner = value; }

		/// <summary>Real size (km), for drawing at the ship scale.</summary>
		double LengthKm = 0;

		/// <summary>
		/// Sits at this Lagrange point of <see cref="Body"/> and what it orbits (JWST at Sun-Earth L2, a
		/// depot at Earth-Moon L1) instead of an orbit about the body; None for an ordinary orbit.
		/// </summary>
		LagrangePoint Lagrange = LagrangePoint::None;

		/// <summary>At a Lagrange point rather than in orbit about its body.</summary>
		bool AtLagrange() const { return Lagrange != LagrangePoint::None; }

		/// <summary>A real spacecraft (geometry from published orbits; phases illustrative).</summary>
		bool Real = false;
		cs::String Note;

		DVec3 HelioAt(const cs::Ref<Ephemeris>& eph, double jd) const;

		/// <summary>The same, from the ephemeris' current frame.</summary>
		DVec3 Helio(const cs::Ref<Ephemeris>& eph) const;

		/// <summary>A yard's progress on the hull in its cradle (0..1), or 0.</summary>
		double SlipProgress() const;

		cs::String ToString() const { return Name; }
		SG_SERIALIZABLE(OrbitalStructure); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// A satellite constellation drawn as a cloud of points: shells of circular orbits in evenly
	/// spaced planes (a Walker pattern). Plane counts, altitudes and inclinations are the real ones;
	/// where each satellite is along its orbit is illustrative (there are no live orbital elements).
	/// </summary>
	// Detach passes `this` to a new Satellite, hence enable_shared_from_this.
	class SPACEGODCORE_API Constellation final : public std::enable_shared_from_this<Constellation>
	{
	public:
		cs::String Name;
		int Body = 0;
		Rgb Color;

		struct Shell
		{
			double RadiusKm = 0, IncDeg = 0;
			int Planes = 0, PerPlane = 0;

			Shell() = default;
			Shell(double radiusKm, double incDeg, int planes, int perPlane)
			{
				RadiusKm = radiusKm;
				IncDeg = incDeg;
				Planes = planes;
				PerPlane = perPlane;
			}
		};

		cs::List<Shell> Shells;

		int Count() const;

		/// <summary>
		/// The satellites flying on a state of their own (U.SPC-T448), by index: one the player has taken, or one its AI is
		/// still flying back to its slot. Every other satellite is exactly in its slot.
		/// </summary>
		cs::Dictionary<int, cs::Ref<Satellite>> Flying;

		/// <summary>A satellite's name: the constellation's and its number in it.</summary>
		cs::String NameOf(int index) const { return cs::Cat(Name, " ", index + 1); }

		/// <summary>
		/// Every satellite's offset from its body (km, ecliptic) at a date: its slot, or for one flying on its own
		/// (<see cref="Flying"/>) where its own state has it.
		/// </summary>
		void Offsets(const cs::Ref<BodyDefinition>& body, double jd, cs::List<DVec3>& output) const;

		/// <summary>
		/// Where satellite <paramref name="index"/>'s slot is at a date (km from the body's centre) and how fast it moves
		/// along it (km/s), both ecliptic: the same circular orbits <see cref="Offsets"/> places the cloud on. False (and
		/// zeros) for an index past the last satellite.
		/// </summary>
		bool SlotAt(const cs::Ref<BodyDefinition>& body, int index, double jd, DVec3& offsetKm, DVec3& velocityKmS) const;

		/// <summary>
		/// Satellite <paramref name="index"/> flying on its own state from <paramref name="jd"/>, starting from its slot at
		/// its working attitude (or the one already flying).
		/// </summary>
		cs::Ref<Satellite> Detach(const cs::Ref<BodyDefinition>& body, int index, double jd);

		/// <summary>Steps every satellite flying on its own on to <paramref name="jd"/>, letting go of those back on station.</summary>
		void Step(const cs::Ref<BodyDefinition>& body, double jd);

	private:
		static void PlaneAxes(DVec3 axis, DVec3 node0, DVec3 east0, const Shell& s, int shellIndex, int p, DVec3& n, DVec3& v);

		static double Phase(const Shell& s, int p, int k, double rate, double jd);
	};

	/// <summary>
	/// A docking ring: the one way down to a populous world through its atmosphere. A powered
	/// station holds position directly over a city at the planet's stationary-orbit altitude (or three
	/// radii out where it has none), turning with the ground (fiction: it keeps station by thrust,
	/// since only the equator is truly geostationary). Ships queue at it, are cleared down one at a
	/// time, descend to the city, land, and climb back to it to leave.
	/// </summary>
	class SPACEGODCORE_API DockingRing final
	{
	public:
		int Id = 0;
		cs::String Name;
		cs::String City;
		int Body = 0;
		double LatDeg = 0, LonDeg = 0;

		/// <summary>From the body's centre, km.</summary>
		double RadiusKm = 0;

		/// <summary>The body's radius and where its atmosphere starts (km above the surface).</summary>
		double SurfaceKm = 0, EntryInterfaceKm = 0;

		/// <summary>One ship is cleared to descend per interval (days).</summary>
		double EntryIntervalDays = 30.0 / 1440;

		/// <summary>When the next ship may begin its descent.</summary>
		double NextEntryJd = cs::Double::MinValue;

		/// <summary>Hours from the ring to the ground (and back).</summary>
		double DescentDays = 0;

		cs::Ref<SpaceGod::Core::Settlement> Settlement;

	private:
		cs::Ref<Faction> owner;

	public:
		/// <summary>The city's settlement's owner, whoever that is now (it changes hands with the port).</summary>
		cs::Ref<Faction> Owner() const;
		void SetOwner(const cs::Ref<Faction>& value) { owner = value; }

		/// <summary>How far out along the holding line arriving ships wait (km past the ring).</summary>
		double HoldKm() const { return cs::Math::Max(RadiusKm * 0.05, SurfaceKm * 0.3); }

	private:
		cs::Ref<BodyDefinition> body;

	public:
		/// <summary>For a world that keeps one face to its parent: the ephemeris, to find that face.</summary>
		cs::Ref<SpaceGod::Core::Ephemeris> Ephemeris;

	private:
		double lockedEpoch = cs::Double::NaN, lockedW0 = 0, lockedRate = 0;

	public:
		DockingRing(cs::Ref<BodyDefinition> body);

		/// <summary>
		/// A body-fixed point over the ring's city at a distance from the body's centre, as a circle
		/// about the spin axis that turns with the planet (so flight planning can intercept it).
		/// <paramref name="lonOffsetDeg"/> moves it east along the holding line's parallel.
		/// </summary>
		cs::Ref<ParkingOrbit> Point(double radiusKm, double lonOffsetDeg = 0);

		/// <summary>The ring itself.</summary>
		cs::Ref<ParkingOrbit> RingPoint() { return Point(RadiusKm); }

		/// <summary>
		/// Where a ship arrives to wait its turn: off the top of the line, spread east or west and up or
		/// down by ship (golden-ratio steps, so no two ships share a spot).
		/// </summary>
		cs::Ref<ParkingOrbit> HoldPoint(const cs::Ref<Ship>& ship);

		/// <summary>Unit vector from the body's centre up through the ring at a date.</summary>
		DVec3 Up(double jd);

		cs::String ToString() const { return Name; }
	private:
		DockingRing() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(DockingRing); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>Everything a system has in orbit, and the docking rings over its populous worlds.</summary>
	class SPACEGODCORE_API Infrastructure final
	{
	public:
		cs::List<cs::Ref<OrbitalStructure>> Structures;
		cs::List<cs::Ref<Constellation>> Constellations;
		cs::List<cs::Ref<DockingRing>> Rings;

	private:
		cs::Ref<BodyCatalog> catalog;
		cs::Ref<SpaceGod::Core::Ephemeris> ephemeris;

	public:
		Infrastructure(cs::Ref<BodyCatalog> catalog, cs::Ref<SpaceGod::Core::Ephemeris> ephemeris = nullptr);

		/// <summary>
		/// Steps every constellation satellite flying on its own (U.SPC-T448: taken by the player, or its AI bringing it
		/// back to its slot) on to <paramref name="jd"/>.
		/// </summary>
		void UpdateSatellites(double jd);

		cs::Ref<OrbitalStructure> YardOf(const cs::Ref<SpaceGod::Core::Settlement>& s) const;

		cs::List<cs::Ref<DockingRing>> RingsAt(int body) const;

		// ---------------------------------------------------------------- building

	private:
		static cs::String FacilityName(IndustryKind k);

	public:
		/// <summary>
		/// Each settlement's industry, put in orbit of its world: the shipyard as a yard, each factory
		/// as a station, and a habitat for the big ones. Mines and farms stay on the ground.
		/// </summary>
		void AddSettlementStructures(const cs::Ref<Economy>& economy, int seed);

		/// <summary>
		/// A circular orbit about a body's equator: <paramref name="incDeg"/> from it, its node
		/// <paramref name="nodeDeg"/> round from the equator's node, prograde with the spin.
		/// </summary>
		static cs::Ref<ParkingOrbit> EquatorialOrbit(const cs::Ref<BodyDefinition>& b, double radiusKm, double incDeg, double nodeDeg, double phaseDeg, double epochJd = Astro::J2000);

	private:
		void Real(const cs::String& name, StructureKind kind, const cs::String& body, double altitudeKm, double incDeg, double nodeDeg, double phaseDeg, double km, const cs::String& note);

	public:
		/// <summary>
		/// Sol's real spacecraft and constellations (orbits from published altitudes and
		/// inclinations, circular; positions along them illustrative). See Core/Systems/SOURCES.md.
		/// </summary>
		void AddSolSpacecraft();

		/// <summary>Puts a structure at a Lagrange point of a body and what it orbits (a real spacecraft when it has no owner).</summary>
		cs::Ref<OrbitalStructure> AtPoint(const cs::String& name, StructureKind kind, const cs::String& body, LagrangePoint point, double km, const cs::Ref<Faction>& owner, const cs::String& note);

		/// <summary>
		/// Sol's game-layer infrastructure at the Lagrange points, where it belongs: a depot and a
		/// gateway at Earth-Moon L1 (the cheapest stop between Earth and the Moon), a depot at Earth-Moon
		/// L2 for ships bound out of cislunar space, a storm-warning relay at Sun-Earth L1 (an hour of
		/// warning before the solar wind arrives), a depot at Sun-Earth L2 for outbound traffic, and at
		/// Jupiter's L4 and L5 the Trojan camps' depot and relay, co-orbital with Jupiter 60 degrees ahead
		/// and behind. The inner points are <paramref name="inner"/>'s, the Trojan camps <paramref name="trojans"/>'.
		/// </summary>
		void AddSolLagrangeStations(const cs::Ref<Faction>& inner, const cs::Ref<Faction>& trojans);

		// ---------------------------------------------------------------- docking rings

		/// <summary>
		/// Radius of the body's stationary orbit (km from its centre), where a satellite keeps pace with
		/// the ground; or three radii out where that lies beyond the body's neighbourhood (slow
		/// rotators such as Venus, tidally locked worlds).
		/// </summary>
		static double RingRadiusKm(const cs::Ref<BodyDefinition>& b);

	private:
		static double EntryInterface(const cs::String& body);

	public:
		/// <summary>Adds a ring over a site; it belongs to the world's largest settlement.</summary>
		cs::Ref<DockingRing> AddRing(const cs::Ref<Economy>& economy, const cs::String& body, const cs::String& name, const cs::String& city, double latDeg, double lonDeg);

		/// <summary>
		/// How many rings a world's population warrants: one per order of magnitude past 100,000
		/// (⌈log₁₀(population / 10⁵)⌉, one to seven).
		/// </summary>
		static int RingsFor(double population);

		void AddSolRings(const cs::Ref<Economy>& economy);

		void AddCentauriRings(const cs::Ref<Economy>& economy);
	private:
		Infrastructure() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Infrastructure); // ObjectGraph registration (Persistence/Registrations)
	};
}
