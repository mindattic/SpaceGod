#include "Audio/SynthSound.h"

#include <algorithm>
#include <cmath>

namespace SpaceGod::Audio
{
	namespace
	{
		constexpr double TwoPi = 6.283185307179586;

		float Oscillator(Synth::Wave wave, double phase)
		{
			const double p = phase - std::floor(phase); // 0..1
			switch (wave)
			{
			case Synth::Wave::Triangle: return static_cast<float>(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
			case Synth::Wave::Square: return p < 0.5 ? 0.7f : -0.7f;
			case Synth::Wave::Saw: return static_cast<float>(2.0 * p - 1.0) * 0.7f;
			default: return static_cast<float>(std::sin(TwoPi * p));
			}
		}

		/// A linear attack, then a fall to silence at the end (a squared ramp: quick at first, gentle to zero).
		float Envelope(float t, float seconds, float attack)
		{
			if (t < 0 || t >= seconds) return 0;
			const float rise = attack > 0 ? std::min(1.0f, t / attack) : 1.0f;
			const float left = 1.0f - t / seconds;
			return rise * left * left;
		}

		SoundClip Make(const std::vector<float>& buffer, const char* name, float peak)
		{
			float max = 0;
			for (float s : buffer) max = std::max(max, std::fabs(s));
			const float scale = max > 0 ? peak / max : 0.0f;
			auto samples = std::make_shared<std::vector<std::int16_t>>(buffer.size());
			for (size_t i = 0; i < buffer.size(); i++)
				(*samples)[i] = static_cast<std::int16_t>(std::clamp(buffer[i] * scale, -1.0f, 1.0f) * 32767.0f);
			SoundClip clip;
			clip.Samples = std::move(samples);
			clip.SampleRate = Synth::Rate;
			clip.Name = name;
			return clip;
		}
	}

	namespace Synth
	{
		std::vector<float> Buffer(float seconds) { return std::vector<float>(static_cast<size_t>(std::max(0.0f, seconds) * Rate) + 1, 0.0f); }

		void Tone(std::vector<float>& into, float at, float seconds, float startHz, float endHz, float gain, Wave wave, float attack)
		{
			const int first = static_cast<int>(at * Rate);
			const int count = static_cast<int>(seconds * Rate);
			const double ratio = startHz > 0 && endHz > 0 ? std::log(static_cast<double>(endHz) / startHz) : 0.0;
			double phase = 0;
			for (int i = 0; i < count; i++)
			{
				const int at_ = first + i;
				if (at_ < 0) continue;
				if (at_ >= static_cast<int>(into.size())) break;
				const float t = static_cast<float>(i) / Rate;
				const double hz = startHz * std::exp(ratio * (static_cast<double>(i) / std::max(1, count)));
				phase += hz / Rate;
				into[at_] += gain * Envelope(t, seconds, attack) * Oscillator(wave, phase);
			}
		}

		void Noise(std::vector<float>& into, float at, float seconds, float gain, float brightness, float attack, std::uint32_t seed)
		{
			const int first = static_cast<int>(at * Rate);
			const int count = static_cast<int>(seconds * Rate);
			const float a = std::clamp(brightness, 0.01f, 1.0f);
			std::uint32_t state = seed ? seed : 1;
			float low = 0;
			for (int i = 0; i < count; i++)
			{
				const int at_ = first + i;
				if (at_ < 0) continue;
				if (at_ >= static_cast<int>(into.size())) break;
				state ^= state << 13; state ^= state >> 17; state ^= state << 5; // xorshift32: the same noise every run
				const float white = static_cast<float>(state) / 2147483648.0f - 1.0f;
				low += a * (white - low);
				into[at_] += gain * Envelope(static_cast<float>(i) / Rate, seconds, attack) * low;
			}
		}

		SoundClip Finish(const std::vector<float>& buffer, const char* name, float peak) { return Make(buffer, name, peak); }

		SoundClip FinishLoop(std::vector<float> buffer, const char* name, float peak)
		{
			// Cross-fade the last 20 ms into the first so the wrap has no step.
			const size_t fade = std::min(buffer.size() / 4, static_cast<size_t>(Rate / 50));
			if (fade > 0 && buffer.size() > fade * 2)
			{
				const size_t tail = buffer.size() - fade;
				for (size_t i = 0; i < fade; i++)
				{
					const float w = static_cast<float>(i) / fade;
					buffer[i] = buffer[i] * w + buffer[tail + i] * (1.0f - w);
				}
				buffer.resize(tail);
			}
			return Make(buffer, name, peak);
		}
	}

	namespace StarterSounds
	{
		using namespace Synth;

		const SoundClip& Click()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.03f);
				Tone(b, 0, 0.025f, 2400, 1800, 0.6f, Wave::Triangle, 0.0005f);
				Noise(b, 0, 0.012f, 0.4f, 0.9f, 0.0005f, 7);
				return Finish(b, "Click", 0.55f);
			}();
			return clip;
		}

		const SoundClip& Blip()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.14f);
				Tone(b, 0, 0.06f, 880, 880, 0.5f, Wave::Square);
				Tone(b, 0.06f, 0.08f, 1320, 1320, 0.5f, Wave::Square);
				return Finish(b, "Blip", 0.5f);
			}();
			return clip;
		}

		const SoundClip& Chime()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.9f);
				Tone(b, 0, 0.9f, 1046.5f, 1046.5f, 0.6f);
				Tone(b, 0, 0.6f, 2093.0f, 2093.0f, 0.25f);
				Tone(b, 0, 0.35f, 3139.5f, 3139.5f, 0.1f);
				return Finish(b, "Chime", 0.6f);
			}();
			return clip;
		}

		const SoundClip& Thud()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.35f);
				Tone(b, 0, 0.3f, 120, 45, 0.8f, Wave::Sine, 0.001f);
				Noise(b, 0, 0.2f, 0.6f, 0.12f, 0.001f, 3);
				return Finish(b, "Thud", 0.8f);
			}();
			return clip;
		}

		const SoundClip& HumLoop()
		{
			static const SoundClip clip = []
			{
				// 2 s of 55 Hz and 55.5 Hz (whole cycles in the loop, a slow beat) with a fifth above, unenveloped.
				const int count = 2 * Rate;
				std::vector<float> b(static_cast<size_t>(count) + Rate / 50, 0.0f);
				for (size_t i = 0; i < b.size(); i++)
				{
					const double t = static_cast<double>(i) / Rate;
					b[i] = static_cast<float>(0.5 * std::sin(TwoPi * 55.0 * t) + 0.4 * std::sin(TwoPi * 55.5 * t) + 0.15 * std::sin(TwoPi * 82.5 * t));
				}
				return FinishLoop(std::move(b), "HumLoop", 0.5f);
			}();
			return clip;
		}
	}
}
