#pragma once

namespace SpaceGod { class UnitySpaceApp; }

namespace SpaceGod::Audio
{
	/// The flight and alert sounds of the craft the player flies (US-T242), all synthesized in code (no sound files) and all
	/// local to this machine: Poll only reads the app, never writes it, so nothing enters the Core or the co-op frames and
	/// each peer hears only the craft it flies itself. Silent wherever GameAudio is (no device, -muted, headless, offscreen,
	/// the smoke/shots/audits, -demo, -capture unless -GameAudio).
	///
	/// On the Effects bus (Settings > Audio > Effects volume):
	/// - the engine: a loop whose loudness and pitch follow the throttle and the drive's real push (the flight feel's,
	///   US-T170), its voice by the hull's role and mass: a fighter's whine, a hauler's throb, a capital ship's rumble;
	///   an afterburner layer and its ignition, the engine cut to a murmur while drifting (US-T165) or with the drives down;
	/// - the RCS jets' puffs as they fire (Views/RcsJets), the flip of a turn-and-burn (Alt at the helm);
	/// - the drives, from the regime's cues (Core::DriveRegime, US-T197): the cruise drive's spool, engage, drop-out and a
	///   called-off charge, a hyperlane's entry and exit, a jump gate's crossing, the FTL drive's spool, jump and arrival,
	///   and a loop under each while it carries the hull;
	/// - other hulls' engines heard in passing: the nearest few within close range, placed where they are drawn.
	///
	/// On the Interface bus (its volume): a missile lock on the craft (a hostile with its nose on it), a missile or torpedo
	/// inbound on it, its shields collapsing, its hull critical, its propellant low or dry, a self-destruct counting down,
	/// and Bio Mass contact.
	namespace FlightSounds
	{
		/// Once a frame after the app's Update: <paramref name="app"/> is null in a scene without the app (the main menu),
		/// which stills every engine.
		void Poll(UnitySpaceApp* app);
	}
}
