#pragma once
// Port of Unity.SpaceGod Core/BodyCatalog.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Astro.h"
#include "BodyDefinition.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// The bodies of the simulation, with their real physical and orbital data.
	///
	/// Sources:
	///  - Planets: JPL "Keplerian Elements for Approximate Positions of the Major Planets", table 1
	///    (valid 1800-2050), E. M. Standish. Earth's entry is the Earth-Moon barycentre.
	///  - Eris: JPL Small-Body Database osculating elements, epoch JD 2459000.5.
	///  - Ceres: mean elements least-squares fitted to JPL Horizons positions every 30 days over 2000-2050.
	///  - Psyche, Lutetia and Ida (main-belt asteroids, game-layer pirate havens), and the Jupiter Trojans
	///    Hektor, Patroclus and Eurybates (mining grounds and a haven): JPL Small-Body Database
	///    osculating elements, epoch JD 2461200.5; sizes, GM and spin periods from the SBDB physical
	///    parameters (see Systems/SOURCES.md).
	///  - The Moon: principal ELP-2000/82 terms (Meeus ch. 47), see LunarTheory; its catalogue elements
	///    are the mean orbit, used only to draw its path.
	///  - Other moons: JPL satellite mean elements (ssd.jpl.nasa.gov/sats/elem, epoch J2000), in each
	///    moon's Laplace plane or its parent's equator, refined by a least-squares fit to JPL Horizons
	///    positions sampled every 73 days over 2000-2050. Across that span every moon stays within
	///    0.5 degrees of Horizons along its orbit, except Miranda (1.7, strongly perturbed by its
	///    neighbours). Phobos was refitted 2026-10 with its tidal decay (a quadratic mean-longitude term):
	///    within 0.07 degrees. Nodes are measured from the reference plane's ascending node on
	///    the ecliptic, not on the ICRF equator as in JPL's table.
	///  - Radii, GM, poles and rotation: IAU WGCCRE report and NASA planetary fact sheets.
	/// </summary>
	class BodyCatalog final
	{
	public:
		const cs::List<cs::Ref<BodyDefinition>>& Bodies() const { return bodies; }
		cs::Ref<BodyDefinition> Sun() const { return bodies[0]; }
		int Count() const { return bodies.Count(); }

	private:
		cs::List<cs::Ref<BodyDefinition>> bodies;
		// C#: Dictionary<string, BodyDefinition>(StringComparer.OrdinalIgnoreCase). cs::Dictionary has no comparer
		// parameter, so names are keyed by their lower-case form (NameKey); the map is only looked up, never enumerated.
		cs::Dictionary<cs::String, cs::Ref<BodyDefinition>> byName;
		cs::List<cs::List<int>> children;
		static cs::String NameKey(const cs::String& name) { return cs::Strings::ToLowerInvariant(name); }

	public:
		explicit BodyCatalog(const cs::List<cs::Ref<BodyDefinition>>& definitions);

		/// <summary>The root of the system: the Sun, or the barycentre a multiple star orbits.</summary>
		cs::Ref<BodyDefinition> Root() const { return bodies[0]; }

	private:
		mutable double outerEdgeKm = cs::Double::NaN;

		/// <summary>A companion star on an orbit wider than this (Proxima, 13,000 AU out) is a system of its own, not part of the
		/// root's reach: it and what orbits it are left out of <see cref="OuterEdgeKm"/>.</summary>
		static constexpr double WideCompanionKm = 1000 * Astro::AuKm;

	public:
		/// <summary>
		/// How far out the system's own bodies reach from its root, km (U.SPC-T515): the far side of the outermost
		/// planet's, asteroid's or station's orbit (dwarf planets of the far Kuiper reaches and moons are not counted; a
		/// moon stays within its planet's reach), with a tenth of margin; never less than the root's own radius.
		/// Where the system's jump gates sit (see <see cref="Galaxy.GateDistanceKm"/>).
		/// </summary>
		double OuterEdgeKm() const;

		cs::Ref<BodyDefinition> operator[](int index) const { return bodies[index]; }
		cs::Ref<BodyDefinition> operator[](const cs::String& name) const { return byName[NameKey(name)]; }
		bool TryGet(const cs::String& name, cs::Ref<BodyDefinition>& body) const { return byName.TryGetValue(NameKey(name), body); }
		const cs::List<int>& ChildrenOf(int index) const { return children[index]; }

		static cs::Ref<BodyCatalog> CreateSolSystem();

		static cs::Ref<BodyCatalog> CreateAlphaCentauri();

	private:
		static cs::List<cs::Ref<BodyDefinition>> SolDefinitions();

		static cs::Ref<BodyDefinition> Trojan(cs::Ref<BodyDefinition> body, LagrangePoint point);

		/// <summary>
		/// A minor planet on its JPL Small-Body Database osculating orbit (heliocentric ecliptic
		/// J2000, epoch JD 2461200.5), with its period from Kepler's third law.
		/// </summary>
		static cs::Ref<BodyDefinition> Asteroid(const cs::String& name, double radiusKm, double gm, double aAu, double e, double iDeg, double nodeDeg,
			double argPeriDeg, double meanAnomalyDeg, double rotationHours, Rgb color, const cs::String& summary);

		/// <summary>
		/// A regular satellite with synchronous rotation. Elements are at J2000, in the Laplace plane
		/// whose pole is given (RA/Dec, equatorial J2000), or in the parent's equator when it is not.
		/// Rates are degrees per day.
		/// </summary>
		static cs::Ref<BodyDefinition> Moon(const cs::String& name, const cs::String& parent, double radiusKm, double gm, double aKm, double e, double iDeg,
			double nodeDeg, double argPeriDeg, double meanAnomalyDeg, double periodDays, double nodeRate, double varpiRate,
			double laplaceRaDeg, double laplaceDecDeg, Rgb color);

		/// <summary>
		/// Attaches a colour map (Resources/Textures; sources in SOURCES.md). Single-band mosaics are
		/// tinted with the body's catalogue colour.
		/// </summary>
		static cs::Ref<BodyDefinition> WithMap(cs::Ref<BodyDefinition> body, const cs::String& texture, bool tint, bool mapNorthFlipped = false);

		static cs::Ref<BodyDefinition> Moon(const cs::String& name, const cs::String& parent, double radiusKm, double gm, double aKm, double e, double iDeg,
			double nodeDeg, double argPeriDeg, double meanAnomalyDeg, double periodDays, double nodeRate, double varpiRate, Rgb color);
	};
}
