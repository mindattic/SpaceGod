#pragma once

#include <cstdint>

namespace SpaceGod { class UnitySpaceApp; }

namespace SpaceGod::Audio
{
	/// What the score is playing (US-T243).
	enum class MusicState : std::uint8_t
	{
		Silent,
		/// The main menu's scene (and the app while it loads).
		Menu,
		/// In a system, nothing pressing: open and bright, the arpeggio fuller at the helm than in the god view.
		Exploration,
		/// Out between the stars on the hull's own FTL drive (or deep in hyperspace's toll): a low beating drone, the
		/// crew's dread (VoidWatcher) bringing in the dissonant shimmer and wind, and at its worst a heartbeat.
		Void,
		/// The player's hull (or the ship or unit the god view follows) in a battle that is not over: a driving
		/// ostinato and drums, the brass stabs louder the more enemies still fight.
		Combat,
	};

	inline constexpr int MusicStateCount = 5;

	const char* MusicStateName(MusicState state);

	/// The game's adaptive score (US-T243). There are no music files: every layer is synthesized in code at start-up (on
	/// a worker thread, so nothing hitches) into seamless loops of layered pads, drones, plucks, pulses and drums, and
	/// each state is a set of those layers played together. Poll, once a frame (the app's frame after its Update, the
	/// menu scene's otherwise), reads the simulation and picks the state; the layers crossfade between states (quickly
	/// into a fight, slowly out of one) and inside a state each layer's level follows what is going on (dread, enemies
	/// left, piloting). When the player's battle ends a short sting plays over the ducked score: victory if their side
	/// held the field, defeat if it did not or their hull was lost or taken.
	///
	/// All on the Music bus (Settings > Audio > Music volume, under the master volume) and local to this machine: it only
	/// reads the app, never writes it, and nothing enters the co-op frames or the Core, so each peer hears its own score
	/// and the bench digests are untouched. Silent wherever GameAudio is (no device, -muted, headless, offscreen, the
	/// smoke/shots/audits, -demo, -capture unless -GameAudio).
	namespace Music
	{
		/// The frame's look at the game: <paramref name="app"/> is null in a scene without the app (the main menu).
		void Poll(UnitySpaceApp* app);

		/// The state playing now (Silent until the score is built, or while audio is off).
		MusicState Current();
	}
}
