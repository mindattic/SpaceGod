#pragma once

#include <memory>
#include <vector>
#include <cstdint>

namespace SpaceGod::Audio
{
	/// A sound as mono 16-bit PCM, made once and shared by every play of it. SpaceGod ships no recorded sounds (US-T240):
	/// its starter set is synthesized here from tones, sweeps and noise, so nothing third-party (and no licence) is
	/// involved and no asset has to be imported.
	struct SoundClip
	{
		std::shared_ptr<const std::vector<std::int16_t>> Samples;
		int SampleRate = 44100;
		/// For the log and the audit.
		const char* Name = "";

		bool IsValid() const { return Samples && !Samples->empty(); }
		float Seconds() const { return IsValid() ? static_cast<float>(Samples->size()) / static_cast<float>(SampleRate) : 0.0f; }
	};

	/// The building blocks: each writes (adds) into a float buffer at 44.1 kHz, then Finish clips it to 16-bit.
	namespace Synth
	{
		inline constexpr int Rate = 44100;

		enum class Wave : std::uint8_t { Sine, Triangle, Square, Saw };

		/// Room for <paramref name="seconds"/> of sound.
		std::vector<float> Buffer(float seconds);

		/// A tone from <paramref name="startHz"/> gliding (exponentially) to <paramref name="endHz"/>, starting
		/// <paramref name="at"/> seconds in, with a linear attack and an exponential-ish release over its length.
		void Tone(std::vector<float>& into, float at, float seconds, float startHz, float endHz, float gain, Wave wave = Wave::Sine,
			float attack = 0.004f);

		/// White noise through a one-pole low-pass (<paramref name="brightness"/> 0..1), shaped the same way.
		void Noise(std::vector<float>& into, float at, float seconds, float gain, float brightness = 0.5f, float attack = 0.002f,
			std::uint32_t seed = 1);

		/// The buffer as a clip, peak-normalized to <paramref name="peak"/> (0..1).
		SoundClip Finish(const std::vector<float>& buffer, const char* name, float peak = 0.8f);

		/// A seamless loop: like Finish but with no envelope, the ends cross-faded so it wraps without a click.
		SoundClip FinishLoop(std::vector<float> buffer, const char* name, float peak = 0.6f);
	}

	/// The starter set every later audio ticket can use (US-T240), each built on first use.
	namespace StarterSounds
	{
		/// A short tick: the simplest feedback.
		const SoundClip& Click();
		/// A rising two-note blip.
		const SoundClip& Blip();
		/// A soft bell-like chime.
		const SoundClip& Chime();
		/// A dull thud of filtered noise (an impact or a heavy switch).
		const SoundClip& Thud();
		/// A low, slowly beating hum: a loop.
		const SoundClip& HumLoop();
	}
}
