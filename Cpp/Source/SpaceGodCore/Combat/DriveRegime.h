#pragma once
// US-T197: the one speed-regime / drive-state concept the helm's fast travel shares (the in-system cruise drive,
// riding a hyperlane, the interstellar jump drive), so the HUD, the visual and audio cues and the drop-out rules all
// ask one thing what the hull flown is doing, rather than each reading the cruise, the lane and the FTL drive apart.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// How the hull flown is moving now, slowest to fastest: under its own sublight drive, spooling the cruise drive,
	/// in cruise (<see cref="SystemCruise"/>), riding a hyperlane, or between the stars under its jump drive. Exactly one
	/// at a time; the host works it out each frame from the drives themselves (nothing here is saved).
	/// </summary>
	enum class DriveRegime : int
	{
		/// <summary>Ordinary flight: the throttle, boost and the tactical cruise tier held on J.</summary>
		Normal,

		/// <summary>The cruise drive charging: still ordinary flight, counting down to engage.</summary>
		CruiseSpool,

		/// <summary>The cruise drive engaged: the drive carries the hull along its nose, the stick only turns it.</summary>
		Cruise,

		/// <summary>On a hyperlane, at the lane's speed.</summary>
		Lane,

		/// <summary>Between the stars: the jump drive, or a jump gate's crossing.</summary>
		Ftl,
	};
	SPACEGODCORE_API const char* ToString(DriveRegime v);

	/// <summary>What changed as a regime came or went, for the cues (the screen's flash, a sound): one per change.</summary>
	enum class DriveCue : int
	{
		None,

		/// <summary>A drive began to charge.</summary>
		Spool,

		/// <summary>A faster-than-sublight regime engaged (cruise, a lane, FTL).</summary>
		Engage,

		/// <summary>Out of it again, back to ordinary flight, as asked.</summary>
		Drop,

		/// <summary>Cut short: a charge called off before it engaged.</summary>
		Abort,
	};
	SPACEGODCORE_API const char* ToString(DriveCue v);

	/// <summary>The regimes' traits, as data.</summary>
	struct SPACEGODCORE_API DriveRegimes
	{
		/// <summary>The HUD's word for it ("CRUISE", "LANE", ...; empty for ordinary flight).</summary>
		static const char* Label(DriveRegime v);

		/// <summary>Whether the drive, not the throttle, sets the hull's speed (cruise, a lane, FTL).</summary>
		static bool DriveCarries(DriveRegime v) { return v == DriveRegime::Cruise || v == DriveRegime::Lane || v == DriveRegime::Ftl; }

		/// <summary>The cue going from <paramref name="was"/> to <paramref name="now"/> gives (None when they are the same).</summary>
		static DriveCue CueFor(DriveRegime was, DriveRegime now);
	};

	/// <summary>
	/// Follows the regime frame to frame and says what changed: the regime now and the one before it, the cue the last
	/// change gave and how long ago (seconds, real time), so a flash or a sound plays once per change and fades after.
	/// </summary>
	struct SPACEGODCORE_API DriveRegimeWatch
	{
		DriveRegime Now = DriveRegime::Normal;
		DriveRegime Was = DriveRegime::Normal;
		DriveCue LastCue = DriveCue::None;

		/// <summary>Seconds since the regime last changed.</summary>
		double SinceS = 1e9;

		/// <summary>Seconds since <see cref="LastCue"/> played.</summary>
		double CueSinceS = 1e9;

		/// <summary>Takes the regime this frame, <paramref name="dt"/> seconds on; returns the cue a change gives (None if it held).</summary>
		DriveCue Update(DriveRegime now, double dt);

		/// <summary>A cue's strength <paramref name="fadeS"/> seconds long: 1 as <paramref name="cue"/> plays, falling to 0 (0 for another cue).</summary>
		double Pulse(DriveCue cue, double fadeS) const;
	};
}
