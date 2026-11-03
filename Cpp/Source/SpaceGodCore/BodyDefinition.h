#pragma once
// Port of Unity.SpaceGod Core/BodyDefinition.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class OrbitalElements;
	// Opaque declarations (the full enums live in Lagrange.h and Systems/StarSystemSpec.h); held by value here.
	enum class LagrangePoint : int;
	enum class HabitableZone : int;

	enum class BodyKind : int
	{
		Star,
		Planet,
		DwarfPlanet,
		Moon,

		/// <summary>The empty centre of mass a multiple star orbits (Alpha Centauri A and B): invisible, the system's root.</summary>
		Barycentre,

		/// <summary>
		/// A large artificial station on its own orbit about a star (game layer): a port and a lane stop. A Void
		/// Base's lone body (<see cref="VoidBaseBuilder"/>) is this kind too: there it is the gravity generator
		/// itself, with no star to orbit at all.
		/// </summary>
		Station,

		/// <summary>A minor body smaller than a dwarf planet: the main belt's Psyche, Lutetia and Ida, Jupiter's Trojans Hektor, Patroclus and Eurybates.</summary>
		Asteroid,
	};
	const char* ToString(BodyKind v);

	/// <summary>How sure astronomy is that a body exists.</summary>
	enum class BodyStatus : int
	{
		Confirmed,

		/// <summary>Seen, but not yet confirmed (Alpha Centauri A's JWST giant).</summary>
		Candidate,

		/// <summary>Claimed, then contested (Proxima c).</summary>
		Disputed,

		/// <summary>Invented for the game layer (stations).</summary>
		Fictional,
	};
	const char* ToString(BodyStatus v);

	/// <summary>Which plane a body's orbital elements are measured against.</summary>
	enum class OrbitPlane : int
	{
		/// <summary>J2000 ecliptic (planets, the Moon, minor planets).</summary>
		Ecliptic,

		/// <summary>
		/// The parent's equator, or the moon's Laplace plane when its pole is set (regular satellites).
		/// The node is measured from the plane's ascending node on the ecliptic.
		/// </summary>
		ParentEquator,
	};
	const char* ToString(OrbitPlane v);

	struct Rgb
	{
		float R = 0;
		float G = 0;
		float B = 0;

		Rgb() = default;
		Rgb(float r, float g, float b)
		{
			R = r;
			G = g;
			B = b;
		}
	};

	/// <summary>Static, physical description of one body. Everything here is real data unless noted.</summary>
	/// <remarks>C++: a C# null string is an empty cs::String (ParentName, Texture, NormalMap, Summary, CoOrbitalWith, SpectralType).</remarks>
	class BodyDefinition final
	{
	public:
		cs::String Name;
		BodyKind Kind{};
		cs::String ParentName;
		double RadiusKm = 0;

		/// <summary>Gravitational parameter GM in km³/s².</summary>
		double Gm = 0;

		cs::Ref<OrbitalElements> Elements;
		OrbitPlane Plane = OrbitPlane::Ecliptic;

		/// <summary>IAU north pole, equatorial J2000. NaN = unknown (falls back to ecliptic north).</summary>
		double PoleRaDeg = cs::Double::NaN;
		double PoleDecDeg = cs::Double::NaN;

		/// <summary>Sidereal rotation period in hours; negative = retrograde about the IAU pole.</summary>
		double RotationHours = 0;

		Rgb Color = Rgb(0.7f, 0.7f, 0.7f);
		cs::String Texture;

		/// <summary>Fraction of the map's height at each pole with no data (stretched over from the last valid row).</summary>
		float TexturePoleMargin = 0;

		/// <summary>
		/// The colour map is a single-band (greyscale) mosaic, so it is multiplied by <see cref="Color"/>
		/// (the body's documented average colour). False for genuine colour maps.
		/// </summary>
		bool TintTexture = false;

		/// <summary>Tangent-space relief map derived from real elevation data (Resources/Textures), or null.</summary>
		cs::String NormalMap;

		/// <summary>Drawn with the banded, wind-driven gas giant shader instead of the solid-surface one.</summary>
		bool GasGiant = false;

		/// <summary>
		/// How much of the surface an opaque atmosphere hides in visible light (0 = none, 1 = all),
		/// drawn as a layer of <see cref="AtmosphereColor"/> over the map (Titan's haze, Venus's clouds).
		/// </summary>
		float SurfaceHaze = 0;

		// ---- Orientation (see BodyOrientation)

		/// <summary>IAU prime meridian angle W at J2000 (deg). NaN = unknown, the meridian starts at the equator's node.</summary>
		double PrimeMeridianDeg = cs::Double::NaN;

		/// <summary>IAU dW/dt (deg/day) about the map's north pole. NaN = derived from <see cref="RotationHours"/>.</summary>
		double PrimeMeridianRateDegPerDay = cs::Double::NaN;

		/// <summary>
		/// Synchronous rotator whose 0° meridian faces its parent (the IAU convention for the Moon and
		/// the regular satellites). The meridian is pointed at the parent each frame; libration is not modelled.
		/// </summary>
		bool FacesParent = false;

		/// <summary>
		/// The map's north is the opposite of <see cref="Pole"/>. Pluto's maps use the IAU positive
		/// (right-hand) pole, and the Uranian satellites' maps use Uranus's IAU north, which is the
		/// opposite of the spin axis their catalogue pole follows.
		/// </summary>
		bool MapNorthFlipped = false;
		Rgb AtmosphereColor;
		float AtmosphereStrength = 0;

		/// <summary>Ring system radii (km). Zero = no rings.</summary>
		double RingInnerKm = 0;
		double RingOuterKm = 0;

		/// <summary>Pole of a moon's Laplace plane, equatorial J2000. NaN = the parent's equator.</summary>
		double LaplacePoleRaDeg = cs::Double::NaN;
		double LaplacePoleDecDeg = cs::Double::NaN;

		/// <summary>
		/// Game layer: an empty orbit marked out for settlers to build a habitat in (a frontier star's, see
		/// <see cref="StationSpec.HabitatSite"/>), surveyed as a colony site though nothing stands there yet.
		/// </summary>
		bool HabitatSite = false;

		cs::String Summary;

		/// <summary>
		/// A Trojan: the Lagrange point of its parent's planet (<see cref="CoOrbitalWith"/>) whose swarm it
		/// librates about, sharing that planet's orbit 60° ahead (L4) or behind (L5). None otherwise.
		/// </summary>
		LagrangePoint TrojanPoint{};
		cs::String CoOrbitalWith;

		BodyStatus Status = BodyStatus::Confirmed;

		/// <summary>Exoplanets: the mass is m sin i (radial velocity alone), a lower limit.</summary>
		bool MassIsMinimum = false;

		/// <summary>Exoplanets: where the orbit sits against the host star's habitable zone (see <see cref="HabitableZones"/>).</summary>
		SpaceGod::Core::HabitableZone HabitableZone{};

		/// <summary>Exoplanets: year of discovery (0 = not recorded).</summary>
		int DiscoveryYear = 0;

		/// <summary>Stars: spectral type, when catalogued ("M4.0V", "DA2").</summary>
		cs::String SpectralType;

		/// <summary>
		/// Multiplies the body's orbit about its parent. A binary star's two members share their
		/// relative orbit, each scaled by the other's share of the mass (with a minus sign for one),
		/// so they circle their barycentre on opposite sides (as JPL's Earth-Moon pair does).
		/// </summary>
		double OrbitScale = 1;

		/// <summary>Stars: photosphere temperature (K) and luminosity (solar), for colour and lighting.</summary>
		double TeffK = 5772;
		double LuminositySolar = 1;

		/// <summary>
		/// Game layer: a star body drawn dark and light-swallowing instead of lit (U.SPC-T372) -- a Void Base's
		/// generator, a "black hole in a bottle": its <see cref="Color"/> is used as-is instead of the usual
		/// blackbody tint, with a tight violet glow standing in for an event horizon instead of a corona.
		/// </summary>
		bool Singularity = false;

		// Resolved by BodyCatalog.
		int Index = -1;
		int ParentIndex = -1;
		DVec3 Pole = DVec3::UnitZ;

		/// <summary>
		/// Direction of the spin angular momentum: the IAU pole, flipped for retrograde rotators.
		/// Regular moons orbit in this sense (Uranus's and Pluto's moons go "backwards" about the IAU pole).
		/// </summary>
		DVec3 SpinAxis() const { return RotationHours < 0 ? -Pole : Pole; }

		/// <summary>Sphere of influence radius (km), Laplace's a·(m/M)^0.4, floored at a few body radii.</summary>
		double SoiKm = cs::Double::PositiveInfinity;

		/// <summary>Farthest apoapsis of the body's moons (km); zero when it has none.</summary>
		double MoonReachKm = 0;

		/// <summary>For a star with a parent (in a multiple system): farthest apoapsis of what orbits it (km).</summary>
		double SystemReachKm = 0;

		/// <summary>Closest periapsis of anything orbiting the body (km); zero when nothing does.</summary>
		double InnermostChildKm = 0;

		/// <summary>Drawn and listed (the barycentre is neither).</summary>
		bool IsVisible() const { return Kind != BodyKind::Barycentre; }

		bool HasRings() const { return RingOuterKm > 0; }
		bool IsStar() const { return Kind == BodyKind::Star; }

		/// <summary>Its children are laid out with the stellar (AU power-law) mapping: a star or a barycentre.</summary>
		bool UsesStellarMapping() const { return Kind == BodyKind::Star || Kind == BodyKind::Barycentre; }

		cs::String ToString() const { return Name; }
	};
}
