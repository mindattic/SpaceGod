#pragma once

namespace SpaceGod { class UnitySpaceApp; }

namespace SpaceGod::Audio
{
	/// The sounds of battle (US-T241): every gun, launcher, beam, hit and blast in a fight, all synthesized in code (no sound
	/// files) on the Effects bus.
	///
	/// The Core raises them already: Battle hands each shot fired, round shot down, shield or hull hit and blast to
	/// Core::WeaponSystems::SoundHook (US-T189). Poll binds that hook while sound plays (and clears it otherwise, so a
	/// scripted or headless run never builds a cue), and the hook only queues the cue: nothing is written back, so the fight,
	/// the co-op frames and the bench digests are untouched, and each peer hears its own copy of the shared fight.
	///
	/// Each frame the queued cues are placed and played: distinct fire sounds per weapon (autocannon, vulcan with its spin,
	/// railgun, laser, point defence, phaser, plasma, tachyon lance, homing missile, rocket, hive, torpedo, flak, EMP, a mine
	/// laid) and the shield hit, hull hit, round shot down, hardpoint or hull section lost, mine and EMP bursts and a hull
	/// exploding. Loudness falls with battle-space distance from the listener (the craft flown when it is in that fight,
	/// else the camera), direction is where it is drawn, and each kind keeps a voice cap filled nearest first (the player's
	/// own shots first of all) so a fleet battle does not flood the mixer. Far from the fight (the free SpaceGod view) the
	/// sounds come through low-passed and quiet rather than full or not at all. Silent wherever GameAudio is.
	namespace CombatSounds
	{
		/// Once a frame after the app's Update: <paramref name="app"/> is null in a scene without the app (the main menu).
		void Poll(UnitySpaceApp* app);

		/// Clears the hook and anything queued (the app going away).
		void Shutdown();
	}
}
