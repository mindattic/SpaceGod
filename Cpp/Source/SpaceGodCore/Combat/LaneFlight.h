#pragma once
// US-T196: flying the hull inside a hyperlane, and the speed it bleeds off after it leaves one (see LaneFlight below).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// Hyperlane flight at the helm (US-T196, X Rebirth's highways): riding a lane the hull is carried along it at the lane's
	/// pace (<see cref="Fleet.LaneProgress"/>, the same ride AI traffic takes), and the stick moves it about inside the tube
	/// meanwhile, to line up an off-ramp or slip round other traffic, which rides the lane's centre line. The tube is as wide
	/// as the ring gates and the rings strung along it (<see cref="Hyperlanes.GateRadiusKm"/>): steered out past that, the hull
	/// is out of the lane, still going at the lane's speed, and <see cref="LaneWake"/> bleeds it down to its own.
	///
	/// The offset is held in the lane's own cross-section (the hull's right and roof as it rides, square to the lane line),
	/// in true kilometres and real seconds, so it flies and reads the same at the realistic and the game scale and at any
	/// time warp the ride runs at; only the drawing differs (<see cref="Hyperlanes.GateRadiusKm"/> is drawn the same way the
	/// rings are). Only a player's hands drive it; plain data stepped by the helm's host (UnitySpaceApp.Lanes), never saved.
	/// </summary>
	struct SPACEGODCORE_API LaneFlight
	{
		/// <summary>How fast the stick moves the hull across the tube at full deflection, km a real second: centre to wall in
		/// about two seconds.</summary>
		static constexpr double StrafeKmS = 750;

		/// <summary>Seconds per factor of e the hull's sideways speed takes to follow the stick (so it drifts, not snaps).</summary>
		static constexpr double ResponseS = 0.3;

		/// <summary>The tube's radius, km: the ring gates' and the rings' (<see cref="Hyperlanes.GateRadiusKm"/>).</summary>
		static double TubeRadiusKm();

		/// <summary>Where the hull is off the lane's centre line, km: along its right and along its roof.</summary>
		double RightKm = 0, UpKm = 0;

		/// <summary>Its sideways speed across the tube, km a real second.</summary>
		double RightKmS = 0, UpKmS = 0;

		/// <summary>Back on the centre line, still (a ride just begun).</summary>
		void Reset() { RightKm = UpKm = RightKmS = UpKmS = 0; }

		/// <summary>How far off the centre line, km.</summary>
		double OffsetKm() const { return cs::Math::Sqrt(RightKm * RightKm + UpKm * UpKm); }

		/// <summary>Whether it has been steered out past the tube's wall.</summary>
		bool Outside() const { return OffsetKm() > TubeRadiusKm(); }

		/// <summary>
		/// One frame of <paramref name="dt"/> real seconds with the stick at <paramref name="strafeRight"/>,
		/// <paramref name="strafeUp"/> (each -1..1): the sideways speed eased toward what the stick asks, and the hull moved by it.
		/// Returns whether it is now out of the tube.
		/// </summary>
		bool Step(double strafeRight, double strafeUp, double dt);

		/// <summary>The offset as a vector (true km), given the hull's right and roof square to the lane line.</summary>
		DVec3 Offset(DVec3 right, DVec3 up) const { return right * RightKm + up * UpKm; }

		/// <summary>The sideways velocity as a vector (km a real second), given the hull's right and roof.</summary>
		DVec3 Velocity(DVec3 right, DVec3 up) const { return right * RightKmS + up * UpKmS; }
	};

	/// <summary>
	/// The speed a hull carries out of a hyperlane, bled smoothly down to its own flight speed (US-T196): no snap from the
	/// lane's pace to the hull's top speed. It eases in log space (a smoothstep across the ratio of the two speeds), so the
	/// fall is gentle at its start and at its end and takes longer the more there is to lose (<see cref="DurationFor"/>).
	/// In real seconds, like the helm. Plain data, never saved.
	/// </summary>
	struct SPACEGODCORE_API LaneWake
	{
		/// <summary>The shortest and the longest the bleed takes, real seconds.</summary>
		static constexpr double MinS = 1.2;
		static constexpr double MaxS = 4.5;

		/// <summary>Seconds of bleed per factor of e between the speed out of the lane and the hull's own.</summary>
		static constexpr double SecondsPerE = 0.45;

		double FromKmS = 0, ToKmS = 0, ElapsedS = 0, DurationS = 0;

		/// <summary>Whether the bleed is still going.</summary>
		bool Active() const { return DurationS > 0 && ElapsedS < DurationS; }

		/// <summary>How long a bleed from <paramref name="fromKmS"/> down to <paramref name="toKmS"/> takes, real seconds
		/// (0 when there is nothing to lose).</summary>
		static double DurationFor(double fromKmS, double toKmS);

		/// <summary>Starts a bleed from <paramref name="fromKmS"/> (the lane's speed at the hull's way out) down to
		/// <paramref name="toKmS"/> (its own top speed); none if it was going no faster than that.</summary>
		void Begin(double fromKmS, double toKmS);

		/// <summary>Over at once.</summary>
		void Clear() { FromKmS = ToKmS = ElapsedS = DurationS = 0; }

		/// <summary>The speed <paramref name="elapsedS"/> real seconds into the bleed, km/s: <see cref="FromKmS"/> at 0, falling
		/// smoothly to <see cref="ToKmS"/> at <see cref="DurationS"/> and held there after.</summary>
		double SpeedAt(double elapsedS) const;

		/// <summary>One frame of <paramref name="dt"/> real seconds; returns the speed now, km/s.</summary>
		double Step(double dt);
	};
}
