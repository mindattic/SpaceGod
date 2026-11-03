#pragma once
// Port of Unity.SpaceGod Core/GasGiantWeather.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Astro.h"

#include <array>

namespace SpaceGod::Core
{
	class BodyDefinition;

	/// <summary>
	/// Zonal wind profiles of the giant planets, and the Great Red Spot's position, so the gas giant
	/// shader can move cloud bands at their real speeds relative to each planet's rotation frame.
	///
	/// Winds are eastward (prograde) speeds in m/s at planetographic latitudes, relative to the
	/// rotation period in <see cref="BodyCatalog"/> (Jupiter: System III; Saturn, Uranus, Neptune:
	/// the Voyager radio periods). The tables are approximate digitisations, read to roughly
	/// ±15 m/s, of the published cloud-tracked profiles:
	///  - Jupiter: Cassini ISS, Porco et al. 2003 (Science 299, 1541), agreeing with Voyager
	///    (Limaye 1986). Strongest jet +165 m/s at 23.7°N; retrograde -60 m/s at 19.5°S.
	///  - Saturn: Voyager, Sánchez-Lavega et al. 2000 (Icarus 147, 405); equatorial jet ~400 m/s.
	///  - Uranus: Sromovsky &amp; Fry 2005 / Hammel et al. 2001; retrograde equator, +250 m/s near 60°.
	///  - Neptune: Voyager, Sromovsky et al. 1993; -400 m/s at the equator, +250 m/s near 70°.
	/// </summary>
	class GasGiantWeather
	{
	public:
		/// <summary>
		/// C#'s double[,] wind table: one row per sample, [0] planetographic latitude (deg), [1] eastward wind (m/s).
		/// profile[i, j] is (*profile)[i][j]; profile.GetLength(0) is profile->Count(); null is nullptr.
		/// </summary>
		using Profile = cs::List<std::array<double, 2>>;

	private:
		// Planetographic latitude (deg), eastward wind (m/s).
		static const Profile& Jupiter();
		static const Profile& Saturn();
		static const Profile& Uranus();
		static const Profile& Neptune();

	public:
		/// <summary>The wind table for a body, or null when it has none.</summary>
		static const Profile* ProfileFor(const cs::String& bodyName);

		/// <summary>Eastward wind (m/s) at a latitude, linearly interpolated in the table.</summary>
		static double WindMs(const Profile* profile, double latDeg);

		/// <summary>
		/// Drift of the clouds at a latitude in map longitude (deg/day, east-positive about the map's
		/// north) relative to the rotating frame. Prograde winds follow the rotation, so for a body
		/// spinning backwards about its map north they decrease map longitude.
		/// </summary>
		static double DriftDegPerDay(const cs::Ref<BodyDefinition>& body, const Profile* profile, double latDeg);

		/// <summary>Samples the drift (deg/day) at evenly spaced latitudes from -90° to +90° into <paramref name="output"/>.</summary>
		static void SampleDrift(const cs::Ref<BodyDefinition>& body, const Profile* profile, cs::List<float>& output);

		/// <summary>
		/// Jupiter's Great Red Spot. Position: BAA Jupiter Section report (2025 Sep 1, measured by
		/// S. Mizumoto): System II 77°, System III 292°. Drift: JUPOS (via projectpluto.com/grs_lon.txt)
		/// shows System II longitude rising from 410° (2024 Jan 1) to 439° (2025 Dec 1), +0.0415°/day
		/// westward; System III runs 8.0°/30 days faster than System II (BAA), so the spot drifts
		/// +0.308°/day in System III west longitude. Its drift oscillates (~90-day libration) and
		/// changes over years; this linear fit is good to a few degrees for 2024-2027.
		/// Internal circulation: ~4.5 to 6 days around the high-speed collar (Juno and Voyager/Galileo
		/// cloud tracking, e.g. Simon et al. 2018); the core is comparatively quiet.
		/// </summary>
		class GreatRedSpot
		{
		public:
			static constexpr double AnchorJd = 2460919.5; // 2025-09-01 00:00 UT
			static constexpr double AnchorSystemIIIWestDeg = 292.0;
			static constexpr double SystemIIDriftDegPerDay = 29.0 / 700.0; // 410 -> 439 deg over 2024-01-01 .. 2025-12-01
			static constexpr double SystemIIIMinusIIDegPerDay = 8.0 / 30.0;
			static constexpr double DriftDegPerDay = SystemIIDriftDegPerDay + SystemIIIMinusIIDegPerDay;
			static constexpr double LatitudeDeg = -22.6;
			static constexpr double RotationPeriodDays = 6.0;

			/// <summary>Half-extents of the oval (deg of longitude, latitude), measured on the 2015 Hubble map.</summary>
			static constexpr double HalfWidthDeg = 8.5;
			static constexpr double HalfHeightDeg = 5.5;

			/// <summary>Where the spot sits on the colour map (OPAL Hubble map of 2015 Jan 19), System III west longitude.</summary>
			static constexpr double MapSystemIIIWestDeg = 125.9;

			static double SystemIIIWestDeg(double jd) { return Astro::WrapDegrees360(AnchorSystemIIIWestDeg + DriftDegPerDay * (jd - AnchorJd)); }

			/// <summary>East longitude (map coordinates, -180..180) of the spot's centre.</summary>
			static double EastLongitudeDeg(double jd)
			{
				double e = -SystemIIIWestDeg(jd);
				return Astro::WrapDegrees360(e + 180.0) - 180.0;
			}

			/// <summary>
			/// How far (deg) the colour map must be turned in longitude to put the mapped spot where it is
			/// today: sample longitude = display longitude + this. The rest of the 2015 map turns with it.
			/// </summary>
			static double MapShiftDeg(double jd) { return Astro::WrapDegrees360(SystemIIIWestDeg(jd) - MapSystemIIIWestDeg + 180.0) - 180.0; }
		};
	};
}
