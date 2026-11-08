#pragma once

#include "Cs/Cs.h"
#include "Audio/AudioBus.h"
#include "Audio/SynthSound.h"
#include "UnityCompat/UnityMath.h"

namespace SpaceGod::Core { class RangeSetting; }

namespace SpaceGod::Audio
{
	/// A loop started by GameAudio::StartLoop: 0 is none.
	using LoopId = int;

	/// The game's audio layer (US-T240): plays one-shots (2D, or 3D at a camera-relative scene point) and loops through
	/// Unreal's audio engine, each on a bus with its own sound class, submix and volume (Settings > Audio: Music,
	/// Effects, Interface, Ambience under the master volume, which stays audio.masterVolume and the listener's).
	///
	/// Everything here is local to this machine: nothing enters the Core or the co-op lockstep frames, so each peer
	/// plays its own sounds and the bench digests are untouched. It is silent and does nothing at all, safely, when
	/// there is no audio device or the run is scripted (--headless, -RenderOffscreen, the smoke/shots/audits, -demo,
	/// -capture) unless -GameAudio asks for sound, and under the CLI mute (-muted, SessionAudio).
	class GameAudio
	{
	public:
		/// Whether sounds play this run (a device, not scripted or -GameAudio, not muted).
		static bool Enabled();

		/// The volume of a bus, 0..1, as its setting has it (Master: the master volume).
		static float BusVolume(AudioBus bus);

		/// A one-shot that is not placed in the world (the interface, music stings). <paramref name="volume"/> scales
		/// the bus's volume; <paramref name="pitch"/> changes speed and pitch together.
		static void Play2D(const SoundClip& clip, AudioBus bus, float volume = 1.0f, float pitch = 1.0f);

		/// A one-shot at a camera-relative scene point (scene units, Unity axes): heard from the camera, fading out
		/// between <paramref name="minDistance"/> and <paramref name="maxDistance"/> scene units.
		static void Play3D(const SoundClip& clip, AudioBus bus, UnityCompat::Vector3 position, float volume = 1.0f, float pitch = 1.0f,
			float minDistance = 50.0f, float maxDistance = 5000.0f);

		/// A loop until StopLoop: 2D, or (<paramref name="positional"/>) at <paramref name="position"/>, which
		/// SetLoopPosition moves. Returns 0 when nothing plays (disabled).
		static LoopId StartLoop(const SoundClip& clip, AudioBus bus, float volume = 1.0f, bool positional = false,
			UnityCompat::Vector3 position = UnityCompat::Vector3(), float minDistance = 50.0f, float maxDistance = 5000.0f);
		static void SetLoopVolume(LoopId loop, float volume);
		static void SetLoopPosition(LoopId loop, UnityCompat::Vector3 position);
		static void StopLoop(LoopId loop, float fadeSeconds = 0.1f);
		/// Whether a loop is still playing (US-T243): false once stopped, or when its world went away under it.
		static bool LoopPlaying(LoopId loop);
		/// Stops every sound playing (a scene change).
		static void StopAll();

		/// Settings > Audio's bus volumes (0-100%), saved to the profile like audio.masterVolume; registered by
		/// GameSettingsHost right after it.
		static void Declare();
		static const cs::Ref<Core::RangeSetting>& MusicVolume();
		static const cs::Ref<Core::RangeSetting>& EffectsVolume();
		static const cs::Ref<Core::RangeSetting>& InterfaceVolume();
		static const cs::Ref<Core::RangeSetting>& AmbienceVolume();

		/// How many one-shots and loops are playing (the audit, the log).
		static int Playing();
	};
}
