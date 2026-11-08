#include "Audio/Music.h"
#include "Audio/GameAudio.h"
#include "Audio/SynthSound.h"
#include "Rendering/HyperspaceToll.h"
#include "Target.h"
#include "UnitySpaceApp.h"
#include "UnityCompat/UnityRuntime.h"

#include "Combat/Battle.h"
#include "Ships.h"
#include "World.h"
#include "Systems/DeepSpace.h"
#include "Systems/VoidWatcher.h"

#include "Async/Async.h"
#include "HAL/PlatformTime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::Audio
{
	const char* MusicStateName(MusicState state)
	{
		switch (state)
		{
		case MusicState::Menu: return "Menu";
		case MusicState::Exploration: return "Exploration";
		case MusicState::Void: return "Void";
		case MusicState::Combat: return "Combat";
		default: return "Silent";
		}
	}

	namespace
	{
		// ================================================================ the score, synthesized (no music files)

		/// The score's sample rate: everything in it sits well under 11 kHz, and half of 44.1 kHz halves its memory.
		constexpr int Rate = 22050;
		constexpr double TwoPi = 6.283185307179586;

		/// A sine of <paramref name="cycles"/> (phase in whole cycles) from a table: the score is millions of samples.
		float Sine(double cycles)
		{
			static const std::vector<float> table = []
			{
				std::vector<float> t(4097);
				for (int i = 0; i <= 4096; i++) t[static_cast<size_t>(i)] = static_cast<float>(std::sin(TwoPi * i / 4096.0));
				return t;
			}();
			const double p = (cycles - std::floor(cycles)) * 4096.0;
			const int i = std::min(4095, static_cast<int>(p));
			const float f = static_cast<float>(p - i);
			return table[static_cast<size_t>(i)] + (table[static_cast<size_t>(i) + 1] - table[static_cast<size_t>(i)]) * f;
		}

		double Hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

		/// A loop being written: every sample lands modulo its length, so a note's tail past the end wraps round to the
		/// start and the loop plays on with no seam.
		struct Track
		{
			int n;
			std::vector<float> b;

			explicit Track(double seconds) : n(std::max(1, static_cast<int>(seconds * Rate))), b(static_cast<size_t>(n), 0.0f) {}

			void Add(long long i, float v)
			{
				long long k = i % n;
				if (k < 0) k += n;
				b[static_cast<size_t>(k)] += v;
			}

			double Seconds() const { return static_cast<double>(n) / Rate; }

			/// <paramref name="hz"/> nudged to a whole number of cycles in the loop, so a voice that never stops closes.
			double Closed(double hz) const { return std::max(1.0, std::round(hz * n / Rate)) * Rate / n; }
		};

		/// A smooth rise over <paramref name="attack"/>, full until <paramref name="len"/>, then a fall to silence over
		/// <paramref name="release"/>.
		float Envelope(double t, double attack, double len, double release)
		{
			if (t < 0 || t >= len + release) return 0;
			double up = attack > 0 ? std::min(1.0, t / attack) : 1.0;
			up = up * up * (3 - 2 * up);
			double down = t <= len ? 1.0 : 1.0 - (t - len) / release;
			return static_cast<float>(up * down * down);
		}

		/// A click-free start and end for a percussive voice <paramref name="len"/> long.
		float Edges(double t, double len) { return static_cast<float>(std::min({ 1.0, t / 0.004, std::max(0.0, (len - t) / 0.05) })); }

		enum class Timbre : std::uint8_t
		{
			/// Three detuned soft saws (a few harmonics each), a slow swell in and a long release: the beds.
			Pad,
			/// Inharmonic partials decaying at their own rates: a glassy bell.
			Bell,
			/// A plucked string's quick decay: the arpeggios.
			Pluck,
			/// Harmonics that open up with the envelope: the brass stabs.
			Brass,
			/// A short, dark, decaying saw: the combat ostinato.
			Bass,
			/// A plain sine with a slow release: the root under a pad.
			Sub,
		};

		void Note(Track& tr, double at, double len, int midi, float gain, Timbre timbre)
		{
			const double hz = Hz(midi);
			double attack = 0.004, release = 0;
			switch (timbre)
			{
			case Timbre::Pad: attack = 1.8; release = 2.6; break;
			case Timbre::Brass: attack = 0.06; release = 0.35; break;
			case Timbre::Bass: attack = 0.004; release = 0.06; break;
			case Timbre::Sub: attack = 0.4; release = 2.2; break;
			default: break;
			}
			const long long first = std::llround(at * Rate);
			const long long count = std::llround((len + release) * Rate);
			for (long long i = 0; i < count; i++)
			{
				const double t = static_cast<double>(i) / Rate;
				float s = 0;
				switch (timbre)
				{
				case Timbre::Pad:
				{
					const float e = Envelope(t, attack, len, release);
					for (double detune : { -0.0042, 0.0, 0.0047 })
					{
						const double f = hz * (1 + detune);
						s += Sine(f * t) + 0.45f * Sine(2 * f * t) + 0.2f * Sine(3 * f * t) + 0.08f * Sine(4 * f * t);
					}
					s *= e / 3.0f * (1.0f + 0.1f * Sine(0.17 * t));
					break;
				}
				case Timbre::Bell:
					s = Edges(t, len) * static_cast<float>(std::exp(-t / 1.6) * Sine(hz * t) + 0.5 * std::exp(-t / 0.7) * Sine(2.76 * hz * t) +
						0.25 * std::exp(-t / 0.3) * Sine(5.4 * hz * t) + 0.1 * std::exp(-t / 0.12) * Sine(8.93 * hz * t));
					break;
				case Timbre::Pluck:
				{
					const float d = static_cast<float>(std::exp(-t / 0.28));
					s = Edges(t, len) * d * (Sine(hz * t) + 0.35f * d * Sine(2 * hz * t) + 0.12f * d * d * Sine(3 * hz * t));
					break;
				}
				case Timbre::Brass:
				{
					const float e = Envelope(t, attack, len, release);
					for (int k = 1; k <= 6; k++) s += (k == 1 ? 1.0f : std::pow(e, 0.5f * k)) / k * Sine(k * hz * t);
					s *= e * 0.6f;
					break;
				}
				case Timbre::Bass:
				{
					const float e = Envelope(t, attack, len, release) * static_cast<float>(std::exp(-t / 0.22));
					s = e * (Sine(hz * t) + 0.5f * Sine(2 * hz * t) + 0.3f * e * Sine(3 * hz * t) + 0.2f * e * Sine(4 * hz * t));
					break;
				}
				case Timbre::Sub:
					s = Envelope(t, attack, len, release) * Sine(hz * t);
					break;
				}
				tr.Add(first + i, gain * s);
			}
		}

		/// A pad chord: its first note an octave-doubling root (a sub under the pad), the rest pad voices.
		void Chord(Track& tr, double at, double len, const std::vector<int>& notes, float gain)
		{
			for (size_t i = 0; i < notes.size(); i++)
			{
				if (i == 0) Note(tr, at, len, notes[i], gain * 1.4f, Timbre::Sub);
				Note(tr, at, len, notes[i], i == 0 ? gain * 0.8f : gain, Timbre::Pad);
			}
		}

		/// A drum-like body: a sine falling from <paramref name="fromHz"/> to <paramref name="toHz"/> (fast), dying away.
		void Thump(Track& tr, double at, float gain, double fromHz, double toHz, double decay)
		{
			const long long first = std::llround(at * Rate);
			const double len = decay * 5;
			const long long count = std::llround(len * Rate);
			double phase = 0;
			for (long long i = 0; i < count; i++)
			{
				const double t = static_cast<double>(i) / Rate;
				phase += (toHz + (fromHz - toHz) * std::exp(-t / 0.035)) / Rate;
				tr.Add(first + i, gain * Edges(t, len) * static_cast<float>(std::exp(-t / decay)) * Sine(phase));
			}
		}

		/// A burst of noise: low-passed (<paramref name="brightness"/> 0..1), or with <paramref name="high"/> what the
		/// low-pass took away (a hat's hiss).
		void Hiss(Track& tr, double at, double len, float gain, float brightness, bool high, double decay, std::uint32_t seed)
		{
			const long long first = std::llround(at * Rate);
			const long long count = std::llround(len * Rate);
			std::uint32_t state = seed ? seed : 1;
			float low = 0;
			for (long long i = 0; i < count; i++)
			{
				const double t = static_cast<double>(i) / Rate;
				state ^= state << 13; state ^= state >> 17; state ^= state << 5;
				const float white = static_cast<float>(state) / 2147483648.0f - 1.0f;
				low += brightness * (white - low);
				tr.Add(first + i, gain * Edges(t, len) * static_cast<float>(std::exp(-t / decay)) * (high ? white - low : low));
			}
		}

		/// A voice that never stops (closed on the loop): <paramref name="harmonics"/> partials falling away, its level
		/// swelling <paramref name="swells"/> times a loop by up to <paramref name="depth"/>.
		void Drone(Track& tr, double hz, float gain, int harmonics, int swells, float depth, double swellPhase = 0)
		{
			const double f = tr.Closed(hz);
			const double loop = tr.Seconds();
			for (int i = 0; i < tr.n; i++)
			{
				const double t = static_cast<double>(i) / Rate;
				float s = 0;
				for (int k = 1; k <= harmonics; k++) s += Sine(k * f * t) / (k * k);
				const float swell = 1.0f - depth * (0.5f - 0.5f * Sine(swells * t / loop + swellPhase + 0.25));
				tr.b[static_cast<size_t>(i)] += gain * swell * s;
			}
		}

		/// The wind between the stars: low-passed noise swelling <paramref name="swells"/> times a loop, its last half
		/// second cross-faded into its first so it wraps unheard.
		void Wind(Track& tr, float gain, float brightness, int swells, std::uint32_t seed)
		{
			const int fade = Rate / 2;
			std::vector<float> g(static_cast<size_t>(tr.n + fade));
			std::uint32_t state = seed ? seed : 1;
			float low = 0, lower = 0;
			for (float& v : g)
			{
				state ^= state << 13; state ^= state >> 17; state ^= state << 5;
				low += brightness * ((static_cast<float>(state) / 2147483648.0f - 1.0f) - low);
				lower += brightness * (low - lower);
				v = lower;
			}
			const double loop = tr.Seconds();
			for (int i = 0; i < tr.n; i++)
			{
				float v = g[static_cast<size_t>(i)];
				if (i < fade)
				{
					const float w = static_cast<float>(i) / fade;
					v = v * w + g[static_cast<size_t>(tr.n + i)] * (1 - w);
				}
				const float swell = 0.55f + 0.45f * Sine(swells * static_cast<double>(i) / Rate / loop);
				tr.b[static_cast<size_t>(i)] += gain * swell * v;
			}
		}

		SoundClip Clip(const Track& tr, const char* name, float peak)
		{
			SoundClip clip = Synth::Finish(tr.b, name, peak);
			clip.SampleRate = Rate;
			return clip;
		}

		/// Every state's layers (played together, in step: each of a state's loops is the same length) and the stings.
		struct Score
		{
			std::array<std::vector<SoundClip>, MusicStateCount> Layers;
			SoundClip Victory, Defeat;
		};

		// Layers of each state, by index.
		enum MenuLayer { MenuPad, MenuBells };
		enum ExploreLayer { ExplorePad, ExploreArp, ExploreDrone };
		enum VoidLayer { VoidDrone, VoidDread, VoidHeart };
		enum CombatLayer { CombatDrive, CombatDrums, CombatStabs };
		constexpr int MaxLayers = 3;

		/// The main menu: D minor, slow (Dm9, Bbmaj7, Gm, A, 8 s each) with a sparse bell melody over it.
		void ComposeMenu(Score& score)
		{
			const double loop = 32, bar = 8;
			const std::vector<std::vector<int>> chords = { { 38, 50, 57, 60, 64 }, { 34, 53, 57, 62, 65 }, { 31, 50, 55, 58, 62 }, { 33, 52, 57, 61, 64 } };
			Track pad(loop);
			for (size_t c = 0; c < chords.size(); c++) Chord(pad, c * bar, bar, chords[c], 0.2f);
			Track bells(loop);
			const std::pair<double, int> melody[] = { { 0.0, 74 }, { 2.0, 76 }, { 3.0, 77 }, { 6.0, 72 }, { 8.5, 74 }, { 10, 77 }, { 12, 81 }, { 14.5, 79 },
				{ 16.5, 74 }, { 18, 70 }, { 20, 74 }, { 22, 79 }, { 24.5, 76 }, { 26, 73 }, { 28, 76 }, { 30, 69 } };
			for (const auto& [at, midi] : melody) Note(bells, at, 4.0, midi, 0.3f, Timbre::Bell);
			auto& layers = score.Layers[static_cast<int>(MusicState::Menu)];
			layers.push_back(Clip(pad, "MusicMenuPad", 0.6f));
			layers.push_back(Clip(bells, "MusicMenuBells", 0.45f));
		}

		/// Exploration: D lydian, open (Dmaj9, Bm7, Gmaj7#11, Asus4, 8 s each), a plucked arpeggio in eighths at 90 bpm,
		/// a low drone and the faint wind of space.
		void ComposeExploration(Score& score)
		{
			const double loop = 32, bar = 8, eighth = 60.0 / 90.0 / 2.0;
			const std::vector<std::vector<int>> chords = { { 38, 54, 57, 61, 64 }, { 35, 54, 57, 62, 66 }, { 31, 50, 54, 59, 61 }, { 33, 52, 57, 62, 64 } };
			Track pad(loop), arp(loop), drone(loop);
			for (size_t c = 0; c < chords.size(); c++)
			{
				Chord(pad, c * bar, bar, chords[c], 0.2f);
				const auto& ch = chords[c];
				const int up[] = { ch[1] + 12, ch[2] + 12, ch[3] + 12, ch[4] + 12, ch[2] + 24 };
				const int pattern[] = { 0, 1, 2, 3, 4, 3, 2, 1 };
				const int steps = static_cast<int>(std::lround(bar / eighth));
				for (int i = 0; i < steps; i++)
					Note(arp, c * bar + i * eighth, 1.0, up[pattern[i % 8]], i % 4 == 0 ? 0.3f : 0.2f, Timbre::Pluck);
			}
			Drone(drone, Hz(26), 0.5f, 3, 2, 0.5f);
			Drone(drone, Hz(33), 0.3f, 2, 3, 0.6f, 0.5);
			Wind(drone, 0.6f, 0.04f, 4, 17);
			auto& layers = score.Layers[static_cast<int>(MusicState::Exploration)];
			layers.push_back(Clip(pad, "MusicExplorePad", 0.55f));
			layers.push_back(Clip(arp, "MusicExploreArp", 0.4f));
			layers.push_back(Clip(drone, "MusicExploreDrone", 0.45f));
		}

		/// The void: a low cluster beating against a tritone and slow dark swells; the dread layer a tremolo of high
		/// minor seconds, wind and falling whispers; and a heartbeat (lub-dub, 56 a minute).
		void ComposeVoid(Score& score)
		{
			const double loop = 32;
			Track drone(loop), dread(loop), heart(loop);
			Drone(drone, Hz(38), 0.5f, 4, 2, 0.4f);
			Drone(drone, Hz(39), 0.35f, 3, 3, 0.7f, 0.33);
			Drone(drone, Hz(44), 0.25f, 3, 1, 0.9f, 0.66);
			Note(drone, 0, 13, 50, 0.12f, Timbre::Pad);
			Note(drone, 0, 13, 56, 0.1f, Timbre::Pad);
			Note(drone, 16, 13, 49, 0.12f, Timbre::Pad);
			Note(drone, 16, 13, 55, 0.1f, Timbre::Pad);

			const double shimmer[] = { Hz(86), Hz(87), Hz(92) };
			for (int i = 0; i < 3; i++) Drone(dread, shimmer[i], 0.12f, 1, 24 + 13 * i, 0.95f, i * 0.3);
			Wind(dread, 1.2f, 0.12f, 3, 29);
			for (double at : { 5.0, 13.0, 21.0, 29.0 })
			{
				// A whisper: a bell bent downwards, out of tune with everything.
				const long long first = std::llround(at * Rate);
				double phase = 0;
				for (long long i = 0; i < 3 * Rate; i++)
				{
					const double t = static_cast<double>(i) / Rate;
					phase += 900.0 * std::exp(-t * 0.08) / Rate;
					dread.Add(first + i, 0.08f * Edges(t, 3.0) * static_cast<float>(std::exp(-t / 1.0)) * (Sine(phase) + 0.4f * Sine(2.76 * phase)));
				}
			}

			const double beat = loop / 30; // 56.25 a minute, whole beats in the loop
			for (int i = 0; i < 30; i++)
			{
				Thump(heart, i * beat, 1.0f, 70, 38, 0.11);
				Thump(heart, i * beat + 0.27, 0.7f, 62, 34, 0.1);
			}
			auto& layers = score.Layers[static_cast<int>(MusicState::Void)];
			layers.push_back(Clip(drone, "MusicVoidDrone", 0.55f));
			layers.push_back(Clip(dread, "MusicVoidDread", 0.4f));
			layers.push_back(Clip(heart, "MusicVoidHeart", 0.6f));
		}

		/// Combat at 120 bpm (eight 2 s bars: Dm, Bb, C, A two bars each): a driving bass ostinato in eighths over a kick
		/// on the beat; hats, snare on two and four and a tom fill; brass stabs on each chord.
		void ComposeCombat(Score& score)
		{
			const double loop = 16, beat = 0.5, eighth = 0.25, chordLen = 4;
			const int roots[] = { 38, 34, 36, 33 };
			const std::vector<std::vector<int>> stabs = { { 50, 53, 57, 62 }, { 46, 50, 53, 58 }, { 48, 52, 55, 60 }, { 45, 49, 52, 57 } };
			Track drive(loop), drums(loop), brass(loop);
			const int pattern[] = { 0, 0, 12, 0, 0, 12, 0, 7, 0, 0, 12, 0, 10, 0, 7, 0 };
			for (int c = 0; c < 4; c++)
			{
				for (int i = 0; i < 16; i++) Note(drive, c * chordLen + i * eighth, 0.2, roots[c] + pattern[i], i % 2 == 0 ? 0.5f : 0.38f, Timbre::Bass);
				const double at = c * chordLen;
				for (const auto& [t, len] : { std::pair{ 0.0, 1.4 }, std::pair{ 1.75, 0.22 }, std::pair{ 2.0, 1.6 } })
					for (int note : stabs[static_cast<size_t>(c)]) Note(brass, at + t, len, note, 0.22f, Timbre::Brass);
			}
			for (int b = 0; b < 32; b++)
			{
				Thump(drive, b * beat, 0.9f, 120, 44, 0.13);
				if (b % 4 == 1 || b % 4 == 3)
				{
					Hiss(drums, b * beat, 0.25, 0.55f, 0.45f, false, 0.07, 101u + b);
					Thump(drums, b * beat, 0.35f, 240, 180, 0.05);
				}
			}
			for (int i = 0; i < 64; i++)
				if (!(i >= 60 && i % 2 == 0)) Hiss(drums, i * eighth, 0.08, i % 2 == 1 ? 0.35f : 0.22f, 0.5f, true, 0.025, 7u + i);
			for (int i = 0; i < 4; i++) Thump(drums, 15.0 + i * eighth, 0.6f, 160 - 25 * i, 90 - 12 * i, 0.12); // the fill into the top
			auto& layers = score.Layers[static_cast<int>(MusicState::Combat)];
			layers.push_back(Clip(drive, "MusicCombatDrive", 0.6f));
			layers.push_back(Clip(drums, "MusicCombatDrums", 0.5f));
			layers.push_back(Clip(brass, "MusicCombatStabs", 0.5f));
		}

		/// The stings (one-shots, nothing wraps): victory a rising D major fanfare settling on a bright chord; defeat a
		/// falling line onto a low D minor and a sinking tone.
		void ComposeStings(Score& score)
		{
			Track win(7.0);
			const int rise[] = { 62, 66, 69, 74 };
			for (int i = 0; i < 4; i++) Note(win, i * 0.18, 0.4, rise[i], 0.3f, Timbre::Brass);
			for (int note : { 50, 57, 62, 66, 69, 74 }) Note(win, 0.75, 3.0, note, 0.18f, Timbre::Brass);
			Note(win, 0.75, 3.0, 38, 0.3f, Timbre::Sub);
			Note(win, 0.75, 4.5, 86, 0.25f, Timbre::Bell);
			Note(win, 1.1, 4.5, 81, 0.2f, Timbre::Bell);
			score.Victory = Clip(win, "MusicVictory", 0.7f);

			Track lose(8.0);
			const int fall[] = { 69, 65, 62 };
			for (int i = 0; i < 3; i++) Note(lose, i * 0.5, 0.55, fall[i], 0.3f, Timbre::Brass);
			Chord(lose, 1.6, 3.0, { 38, 50, 53, 57 }, 0.22f);
			{
				const long long first = std::llround(1.6 * Rate);
				double phase = 0;
				for (long long i = 0; i < 5 * Rate; i++)
				{
					const double t = static_cast<double>(i) / Rate;
					phase += (Hz(38) * std::exp(-t * 0.18)) / Rate;
					lose.Add(first + i, 0.3f * Edges(t, 5.0) * static_cast<float>(1.0 - t / 5.0) * Sine(phase));
				}
			}
			score.Defeat = Clip(lose, "MusicDefeat", 0.7f);
		}

		std::shared_ptr<const Score> Compose()
		{
			auto score = std::make_shared<Score>();
			ComposeMenu(*score);
			ComposeExploration(*score);
			ComposeVoid(*score);
			ComposeCombat(*score);
			ComposeStings(*score);
			return score;
		}

		// ================================================================ the director: the simulation picks the state

		/// The score's level under the Music bus's volume (the stings and the loops alike).
		constexpr float Overall = 0.6f;
		/// The least a playing layer is set to: inaudible, but kept going (in step with its state's other layers).
		constexpr float Floor = 0.0005f;

		struct Voice
		{
			LoopId Id = 0;
			float Gain = 0;
			float Applied = -1;
		};

		struct Director
		{
			TFuture<std::shared_ptr<const Score>> Composing;
			std::shared_ptr<const Score> Composed;
			std::array<std::array<Voice, MaxLayers>, MusicStateCount> Voices{};
			MusicState Current = MusicState::Silent;
			MusicState Pending = MusicState::Silent;
			double PendingSince = 0;
			double Last = 0;
			double DuckUntil = 0;
			// The player's battle, watched for how it ends: the piloted hull, the battle and the side it fought on.
			cs::Ref<Battle> Fight;
			cs::Ref<Ship> Hull;
			int Side = -1;
			const void* SeenWorld = nullptr;
		};

		Director& D()
		{
			static Director director;
			return director;
		}

		void StopState(Director& d, int s, float fade)
		{
			for (Voice& v : d.Voices[static_cast<size_t>(s)])
			{
				if (v.Id != 0) GameAudio::StopLoop(v.Id, fade);
				v = Voice();
			}
		}

		void Silence(Director& d)
		{
			if (d.Current == MusicState::Silent) return;
			for (int s = 0; s < MusicStateCount; s++) StopState(d, s, 0.3f);
			d.Current = d.Pending = MusicState::Silent;
			d.Fight = nullptr;
			d.Hull = nullptr;
			d.SeenWorld = nullptr;
		}

		using Levels = std::array<std::array<float, MaxLayers>, MusicStateCount>;

		/// How the player's battle ended, if it just did: +1 victory, -1 defeat, 0 neither (fled, broken off, the helm
		/// left for another hull).
		int Outcome(const Director& d, const cs::Ref<Ship>& piloted)
		{
			if (d.Hull->Destroyed) return -1;
			if (d.Hull != piloted && !d.Fight->Over) return 0;
			const cs::Ref<BattleUnit> unit = d.Fight->UnitOf(d.Hull);
			if (unit != nullptr && (unit->Dead || unit->Captured)) return -1;
			if (unit != nullptr && unit->Fled) return 0;
			if (d.Fight->Over && d.Fight->Winner >= 0 && d.Side >= 0) return d.Fight->Winner == d.Side ? 1 : -1;
			return 0;
		}

		/// The state the game calls for now, each layer's level in it, and a sting when the player's battle has ended.
		MusicState Decide(Director& d, UnitySpaceApp* app, Levels& levels, int& sting)
		{
			sting = 0;
			for (auto& state : levels) state.fill(1.0f);
			levels[static_cast<int>(MusicState::Menu)][MenuBells] = 0.8f;
			if (app == nullptr) return MusicState::Menu;
			// Loading (no system yet): whatever was playing plays on.
			if (app->Galaxy == nullptr || app->ActiveSystem == nullptr || app->World() == nullptr)
				return d.Current == MusicState::Silent ? MusicState::Menu : d.Current;

			const auto& world = app->World();
			if (world.get() != d.SeenWorld)
			{
				// A new world (a load, a new game, a co-op sync, a system change): no battle carried over from the old one.
				d.SeenWorld = world.get();
				d.Fight = nullptr;
				d.Hull = nullptr;
			}

			// What is at stake: the piloted hull; in the god view the ship (or battle unit) the camera follows.
			const cs::Ref<Ship>& piloted = world->PilotedShip;
			cs::Ref<Ship> ship = piloted;
			cs::Ref<Battle> battle;
			cs::Ref<BattleUnit> unit;
			if (ship != nullptr) battle = ship->Battle;
			else if (app->SpaceCamera != nullptr && app->SpaceCamera->Mode == Cameras::SpaceCamera::CameraMode::Focus)
			{
				const Target& focus = app->SpaceCamera->FocusTarget;
				if (focus.IsShip() && focus.Ship != nullptr)
				{
					ship = focus.Ship;
					battle = ship->Battle;
				}
				else if (focus.Kind == Target::TargetKind::Unit)
				{
					battle = focus.Battle;
					unit = focus.Unit;
				}
			}
			const bool fighting = battle != nullptr && !battle->Over && (ship == nullptr || !ship->Destroyed) && (unit == nullptr || !unit->Dead);

			// The player's own battle: watched until it ends, then a sting for how it went.
			if (fighting && piloted != nullptr && ship == piloted)
			{
				if (d.Fight != battle || d.Hull != ship)
				{
					d.Fight = battle;
					d.Hull = ship;
					const cs::Ref<BattleUnit> mine = battle->UnitOf(ship);
					d.Side = mine != nullptr ? mine->FoughtOn() : -1;
				}
			}
			else if (d.Fight != nullptr)
			{
				sting = Outcome(d, piloted);
				d.Fight = nullptr;
				d.Hull = nullptr;
			}

			if (fighting)
			{
				// The more still fight on the other side, the louder the brass.
				if (unit == nullptr && ship != nullptr) unit = battle->UnitOf(ship);
				const int side = unit != nullptr ? unit->FoughtOn() : -1;
				int enemies = 0;
				for (const auto& u : battle->Units)
					if (u != nullptr && !u->Dead && !u->Fled && !u->Captured && side >= 0 && u->Side != side) enemies++;
				const float intensity = side < 0 ? 0.6f : std::clamp(enemies / 4.0f, 0.15f, 1.0f);
				levels[static_cast<int>(MusicState::Combat)][CombatStabs] = 0.3f + 0.7f * intensity;
				levels[static_cast<int>(MusicState::Combat)][CombatDrums] = 0.6f + 0.4f * intensity;
				return MusicState::Combat;
			}

			// Out between the stars on the drive, or deep in hyperspace's toll: the void, its dread driving the layers.
			const float toll = Rendering::HyperspaceToll::Severity();
			if (app->InFtlCruise() || toll > 0.4f)
			{
				float dread = app->InFtlCruise() ? static_cast<float>(VoidWatcher::DreadOf(*app->FtlCruise)) : 0.0f;
				dread = std::max(dread, toll * 0.7f);
				auto& v = levels[static_cast<int>(MusicState::Void)];
				v[VoidDread] = 0.2f + 0.8f * dread;
				v[VoidHeart] = std::clamp((dread - 0.3f) / 0.45f, 0.0f, 1.0f);
				return MusicState::Void;
			}

			levels[static_cast<int>(MusicState::Exploration)][ExploreArp] = app->IsPiloting() ? 0.9f : 0.45f;
			levels[static_cast<int>(MusicState::Exploration)][ExploreDrone] = 0.8f;
			return MusicState::Exploration;
		}

		/// A state's loops all running (started together, in step), restarting them if any has gone (a world change).
		void EnsurePlaying(Director& d, int s)
		{
			const auto& clips = d.Composed->Layers[static_cast<size_t>(s)];
			auto& voices = d.Voices[static_cast<size_t>(s)];
			bool whole = true;
			for (size_t l = 0; l < clips.size(); l++)
				if (voices[l].Id == 0 || !GameAudio::LoopPlaying(voices[l].Id)) whole = false;
			if (whole) return;
			StopState(d, s, 0.0f);
			for (size_t l = 0; l < clips.size(); l++)
			{
				voices[l].Id = GameAudio::StartLoop(clips[l], AudioBus::Music, Floor);
				voices[l].Applied = Floor;
			}
		}
	}

	namespace Music
	{
		MusicState Current() { return D().Current; }

		void Poll(UnitySpaceApp* app)
		{
			Director& d = D();
			const double now = FPlatformTime::Seconds();
			const float dt = d.Last > 0 ? static_cast<float>(std::min(0.25, now - d.Last)) : 0.0f;
			d.Last = now;
			if (!GameAudio::Enabled())
			{
				Silence(d);
				return;
			}
			if (d.Composed == nullptr)
			{
				// Composed once, off the game thread; until it is ready there is no music.
				if (!d.Composing.IsValid()) d.Composing = Async(EAsyncExecution::ThreadPool, [] { return Compose(); });
				if (!d.Composing.IsReady()) return;
				d.Composed = d.Composing.Get();
				UnityCompat::Debug::Log("[Unity.SpaceGod] Music: score composed (menu, exploration, void, combat; victory and defeat stings).");
			}

			Levels levels{};
			int sting = 0;
			const MusicState want = Decide(d, app, levels, sting);
			if (sting != 0)
			{
				const SoundClip& clip = sting > 0 ? d.Composed->Victory : d.Composed->Defeat;
				GameAudio::Play2D(clip, AudioBus::Music, Overall * 1.2f);
				d.DuckUntil = now + clip.Seconds() - 1.5;
			}

			// A state has to hold a moment before the score follows it (a fight at once), so a flicker changes nothing.
			if (want != d.Current)
			{
				if (want != d.Pending)
				{
					d.Pending = want;
					d.PendingSince = now;
				}
				const bool immediate = d.Current == MusicState::Silent || d.Current == MusicState::Menu || want == MusicState::Menu;
				const double hold = immediate ? 0.0 : want == MusicState::Combat ? 0.4 : 1.5;
				if (now - d.PendingSince >= hold)
				{
					UnityCompat::Debug::Log(cs::Cat("[Unity.SpaceGod] Music: ", MusicStateName(d.Current), " -> ", MusicStateName(want), "."));
					d.Current = want;
				}
			}
			else
			{
				d.Pending = want;
			}

			// Crossfade: the state playing up to its levels, every other one down and stopped once silent.
			const float duck = now < d.DuckUntil ? 0.2f : 1.0f;
			const bool leavingCombat = d.Current != MusicState::Combat;
			for (int s = 1; s < MusicStateCount; s++)
			{
				const auto state = static_cast<MusicState>(s);
				const bool active = state == d.Current;
				const auto& clips = d.Composed->Layers[static_cast<size_t>(s)];
				auto& voices = d.Voices[static_cast<size_t>(s)];
				if (active) EnsurePlaying(d, s);
				// Seconds a full fade takes: into a fight fast, out of one slowly, the void creeping in.
				const float in = state == MusicState::Combat ? 1.2f : state == MusicState::Void ? 5.0f : 3.0f;
				const float out = state == MusicState::Combat && leavingCombat ? 4.5f : d.Current == MusicState::Combat ? 0.8f : 3.0f;
				bool sounding = false;
				for (size_t l = 0; l < clips.size(); l++)
				{
					Voice& v = voices[l];
					const float target = active ? levels[static_cast<size_t>(s)][l] * duck * Overall : 0.0f;
					const float step = dt / (target > v.Gain ? in : out);
					v.Gain = target > v.Gain ? std::min(target, v.Gain + step) : std::max(target, v.Gain - step);
					if (v.Gain > 0) sounding = true;
					if (v.Id == 0) continue;
					const float applied = std::max(Floor, v.Gain);
					if (std::abs(applied - v.Applied) > 1e-4f)
					{
						GameAudio::SetLoopVolume(v.Id, applied);
						v.Applied = applied;
					}
				}
				if (!active && !sounding) StopState(d, s, 0.05f);
			}
		}
	}
}
