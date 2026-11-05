#pragma once
// US-T127: the in-system cruise drive (see SystemCruise below).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	struct PilotInput;

	/// <summary>Where the cruise drive stands: off, spooling up (counting down to engage), or cruising.</summary>
	enum class SystemCruisePhase : int
	{
		Off,
		Spooling,
		Cruising,
	};
	SPACEGODCORE_API const char* ToString(SystemCruisePhase v);

	/// <summary>
	/// The cruise drive (US-T127): very fast travel inside a star system, so a hull can actually fly from one world to the
	/// next. It is not the interstellar jump drive (<see cref="CruiseDrive"/>, <see cref="HelmFtl"/>), nor the tactical
	/// cruise tier held on J (<see cref="PilotCruiseState"/>): it spools for a few seconds (longer the heavier the hull,
	/// <see cref="SpoolSecondsFor"/>), then carries the hull along its nose at a speed set by where it is (Elite's supercruise):
	/// it climbs a factor of e every <see cref="RiseS"/> seconds, up to the least of <see cref="MaxKmS"/>, the distance left to
	/// the drop point over <see cref="ApproachS"/> (so it slows as it closes, in about that many seconds per halving), and the
	/// clearance to the nearest surface over <see cref="ClearanceS"/> (a gravity well holds it back, so it never runs into a
	/// world), and never below <see cref="MinKmS"/>. From world to world that is tens of seconds: Earth to Mars about half a
	/// minute, Earth to Neptune under a minute. All in true kilometres and seconds, so it flies and reads the same at the
	/// realistic and the game scale; only the drawing differs.
	///
	/// Only a player's hands drive it (<see cref="PilotInput.Piloted"/>): crews never engage it, so AI flight and the Bench
	/// are exactly as before. Plain data, stepped by the helm's host (UnitySpaceApp.Cruise); nothing here is saved.
	/// </summary>
	struct SPACEGODCORE_API SystemCruise
	{
		/// <summary>The slowest it cruises, km/s (a thousandth of light's speed): the last stretch to the drop point.</summary>
		static constexpr double MinKmS = 300;

		/// <summary>The fastest, km/s: about a thousand times light's speed, Earth to Neptune's orbit in about fifteen seconds flat out.</summary>
		static constexpr double MaxKmS = 3.0e8;

		/// <summary>Seconds per factor of e as it comes up to speed.</summary>
		static constexpr double RiseS = 1.0;

		/// <summary>The speed held to the distance left to the drop point over this many seconds.</summary>
		static constexpr double ApproachS = 1.8;

		/// <summary>The speed held to the clearance to the nearest surface over this many seconds.</summary>
		static constexpr double ClearanceS = 1.5;

		/// <summary>Light's speed, km/s, for the readout.</summary>
		static constexpr double LightKmS = 299792.458;

		SystemCruisePhase Phase = SystemCruisePhase::Off;

		/// <summary>Seconds of spooling left, and the whole spool.</summary>
		double SpoolLeftS = 0, SpoolS = 0;

		/// <summary>The speed now, km/s (0 unless cruising).</summary>
		double SpeedKmS = 0;

		bool Active() const { return Phase != SystemCruisePhase::Off; }
		bool Cruising() const { return Phase == SystemCruisePhase::Cruising; }
		bool Spooling() const { return Phase == SystemCruisePhase::Spooling; }

		/// <summary>How far through its spool, 0..1.</summary>
		double SpoolFraction() const { return SpoolS > 0 ? 1 - SpoolLeftS / SpoolS : 1; }

		/// <summary>How long a hull of <paramref name="massKt"/> kilotonnes spools: 2.5 s a fighter, about 4 a corvette, up to 8 a
		/// carrier.</summary>
		static double SpoolSecondsFor(double massKt);

		/// <summary>Starts the spool, for a player's hands only (<paramref name="input"/>.Piloted); false otherwise or if
		/// already on.</summary>
		bool Engage(const PilotInput& input, double massKt);

		/// <summary>Off at once, wherever it is.</summary>
		void Drop();

		/// <summary>The most it may go with <paramref name="toDropKm"/> left to the drop point (infinite: none) and
		/// <paramref name="clearanceKm"/> to the nearest surface, km/s.</summary>
		static double SpeedLimit(double toDropKm, double clearanceKm);

		/// <summary>
		/// One frame of <paramref name="dt"/> seconds: the spool counted down (it engages at zero, if <paramref name="mayEngage"/>;
		/// else it holds there, ready), or the speed brought toward its limit. Returns how far the hull goes along its nose this
		/// frame, km: never past the drop point. Off for any input not a player's.
		/// </summary>
		double Step(const PilotInput& input, double toDropKm, double clearanceKm, bool mayEngage, double dt);

		/// <summary>Seconds still to the drop point <paramref name="toDropKm"/> off, straight ahead with nothing in the way,
		/// from the speed now (the spool left included); infinite with no drop point.</summary>
		double EtaS(double toDropKm) const;
	};
}
