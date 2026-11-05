#pragma once
// US-T130: the nebulae and ice fields of a place: drawn clouds and glittering ice, and the sensor masking a nebula brings.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "BodyDefinition.h"

namespace SpaceGod::Core
{
	/// <summary>One puff of a nebula: a soft glowing blob, where it is (km from the system's root) and how big.</summary>
	struct SPACEGODCORE_API NebulaPuff
	{
		DVec3 OffsetKm;
		double RadiusKm = 1;
		Rgb Color;
		float Alpha = 0.3f;
	};

	/// <summary>A nebula: layered puffs of coloured gas, densest in a few knots, that swallow sensors.</summary>
	class SPACEGODCORE_API Nebula final
	{
	public:
		cs::String Name;
		DVec3 CentreKm;
		double RadiusKm = 0;
		cs::List<NebulaPuff> Puffs;

		/// <summary>How dense the gas is at a point (0 clear; about 1 inside a knot).</summary>
		double DensityAt(DVec3 offsetKm) const;
	};

	/// <summary>One drawn chunk of an ice field (not a body: nothing to claim, just a hazard to look at and fly through).</summary>
	struct SPACEGODCORE_API IceChunk
	{
		DVec3 OffsetKm;
		double RadiusKm = 0.1;
		uint32_t Seed = 0;
	};

	/// <summary>A drawn ice field in a star system: a cloud of glittering ice chunks.</summary>
	class SPACEGODCORE_API IceFieldCloud final
	{
	public:
		cs::String Name;
		DVec3 CentreKm;
		double RadiusKm = 0;
		cs::List<IceChunk> Chunks;
	};

	/// <summary>Everything of the kind about one place, keyed by its root body's name.</summary>
	class SPACEGODCORE_API PlaceFeatures final
	{
	public:
		cs::List<Nebula> Nebulae;
		cs::List<IceFieldCloud> IceFields;
	};

	/// <summary>
	/// The nebulae and drawn ice fields of a place (US-T130). Outside Sol, which is kept as it really is, the galaxy is wild:
	/// some star systems hold a nebula a few AU from their star, or a cloud of ice chunks, and a void station may sit inside
	/// a nebula of its own (<see cref="VoidSites"/>), with a pirate haven hidden in it.
	///
	/// All of it comes from the place's root name through its own random stream, built the first time it is asked for
	/// (when the place streams in) and the same every time: nothing in the simulation draws a number differently for it.
	///
	/// A nebula masks sensors (<see cref="SensorMask"/>): inside it, or looking into it, the helm's passive reach and an
	/// active ping's both fall short (<see cref="Sensors.UpdatePilot"/>, the haven scan), so a haven inside is found only by
	/// flying in close or pinging. Every crew's own sensors are untouched, so the Bench runs exactly as before.
	///
	/// Positions are km from the root, laid out by the root's own mapping (as a ship in open space is), so the clouds and
	/// chunks sit where the ships inside them are drawn in both scales.
	/// </summary>
	class SPACEGODCORE_API SpaceFeatures final
	{
	public:
		/// <summary>The features of the place whose root body is <paramref name="root"/> (empty for Sol's Sun).</summary>
		static const PlaceFeatures& Of(const cs::String& root);

		/// <summary>The share of sensor reach left at a point (1 clear; down to <see cref="DeepMask"/> deep in a nebula).</summary>
		static double SensorMask(const cs::String& root, DVec3 offsetKm);

		/// <summary>The reach left deep inside a nebula's knot.</summary>
		static constexpr double DeepMask = 0.12;

		/// <summary>
		/// How far a hull picks out a hidden haven (<paramref name="reachKm"/> in clear space) when either end is in a nebula:
		/// passive, no farther than <see cref="FlyInKm"/> (fly in to find it); pinging (<see cref="SensorPosture"/>), no
		/// farther than <see cref="PingInNebulaKm"/>.
		/// </summary>
		static double HavenScanKm(double reachKm, double maskAtHaven, double maskAtScanner, bool pinging);
		static constexpr double FlyInKm = 4000;
		static constexpr double PingInNebulaKm = 40000;
	};
}
