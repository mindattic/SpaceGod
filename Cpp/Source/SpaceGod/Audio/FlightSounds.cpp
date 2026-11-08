#include "Audio/FlightSounds.h"
#include "Audio/GameAudio.h"
#include "Audio/SynthSound.h"
#include "Camera/StarTravel.h"
#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "Combat/Battle.h"
#include "Combat/BioMass.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/DriveRegime.h"
#include "Combat/SystemCruise.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/FlightFeel.h"
#include "Embodiment/HelmFlight.h"
#include "Embodiment/SelfDestruct.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/HelmFtl.h"
#include "World.h"

#include "HAL/PlatformTime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace SpaceGod::Core;
using SpaceGod::UnityCompat::Vector3;

namespace SpaceGod::Audio::FlightSounds
{
	namespace
	{
		using namespace Synth;

		constexpr double TwoPi = 6.283185307179586;

		// ================================================================ the sounds (synthesized, US-T240's way)

		/// Filtered noise for the loops: white through a one-pole low-pass, the same every run.
		struct NoiseGen
		{
			std::uint32_t state;
			float low = 0, lower = 0;
			explicit NoiseGen(std::uint32_t seed) : state(seed ? seed : 1) {}
			float White()
			{
				state ^= state << 13; state ^= state >> 17; state ^= state << 5;
				return static_cast<float>(state) / 2147483648.0f - 1.0f;
			}
			/// Low-passed at <paramref name="a"/> (0..1, brighter higher).
			float Low(float a) { low += a * (White() - low); return low; }
			/// A band: low-passed at <paramref name="hi"/>, less what lies under <paramref name="lo"/>.
			float Band(float hi, float lo)
			{
				const float l = Low(hi);
				lower += lo * (l - lower);
				return l - lower;
			}
		};

		/// A seamless loop of <paramref name="seconds"/> (every tone in it whole cycles over that length) from a sample
		/// function of the time: 20 ms more are made and cross-faded into the start (Synth::FinishLoop).
		template <class F>
		SoundClip MakeLoop(float seconds, const char* name, float peak, F sample)
		{
			const int count = static_cast<int>(seconds * Rate);
			std::vector<float> b(static_cast<size_t>(count + Rate / 50), 0.0f);
			for (size_t i = 0; i < b.size(); i++) b[i] = sample(static_cast<double>(i) / Rate);
			return FinishLoop(std::move(b), name, peak);
		}

		/// A band-limited saw of <paramref name="hz"/> at time <paramref name="t"/>, from its first harmonics.
		float Saw(double hz, double t, int harmonics = 6)
		{
			double s = 0;
			for (int n = 1; n <= harmonics; n++) s += std::sin(TwoPi * hz * n * t) / n;
			return static_cast<float>(s * 0.55);
		}

		float Sin(double hz, double t) { return static_cast<float>(std::sin(TwoPi * hz * t)); }

		// ---- the engine's three voices, by mass (each 2 s: every frequency a multiple of 0.5 Hz)

		const SoundClip& EngineLight()
		{
			// A fighter's whine: a bright saw and its slightly beating twin over a hiss of exhaust.
			static const SoundClip clip = [] { NoiseGen n(31); return MakeLoop(2.0f, "EngineLight", 0.55f, [&n](double t)
			{
				return Saw(220.0, t) * 0.45f + Saw(220.5, t, 4) * 0.25f + Sin(440.0, t) * 0.12f + Sin(1320.0, t) * 0.05f + n.Band(0.55f, 0.06f) * 0.9f;
			}); }();
			return clip;
		}

		const SoundClip& EngineMedium()
		{
			// A hauler's throb: a low drive note swelling twice a second, over a dark rush.
			static const SoundClip clip = [] { NoiseGen n(32); return MakeLoop(2.0f, "EngineMedium", 0.6f, [&n](double t)
			{
				const float swell = 0.7f + 0.3f * Sin(2.0, t);
				return swell * (Saw(60.0, t, 5) * 0.55f + Sin(90.0, t) * 0.3f + Sin(120.5, t) * 0.18f) + n.Low(0.08f) * 1.6f;
			}); }();
			return clip;
		}

		const SoundClip& EngineHeavy()
		{
			// A capital ship's rumble: a deep slow-heaving drone and a weight of low noise.
			static const SoundClip clip = [] { NoiseGen n(33); return MakeLoop(2.0f, "EngineHeavy", 0.65f, [&n](double t)
			{
				const float heave = 0.75f + 0.25f * Sin(0.5, t);
				return heave * (Sin(32.0, t) * 0.6f + Saw(48.0, t, 4) * 0.35f + Sin(64.5, t) * 0.2f) + n.Low(0.03f) * 2.4f;
			}); }();
			return clip;
		}

		const SoundClip& BoostLoop()
		{
			// The afterburner: a roar of brighter noise flickering over a hard low note.
			static const SoundClip clip = [] { NoiseGen n(34); return MakeLoop(2.0f, "BoostLoop", 0.6f, [&n](double t)
			{
				const float flicker = 0.85f + 0.15f * Sin(12.0, t);
				return flicker * n.Low(0.35f) * 1.4f + Saw(110.0, t, 5) * 0.25f;
			}); }();
			return clip;
		}

		const SoundClip& CruiseLoop()
		{
			// The cruise drive carrying the hull: a soft, airy chord breathing slowly.
			static const SoundClip clip = [] { NoiseGen n(35); return MakeLoop(2.0f, "CruiseLoop", 0.45f, [&n](double t)
			{
				const float breath = 0.75f + 0.25f * Sin(0.5, t);
				return breath * (Sin(150.0, t) * 0.4f + Sin(150.5, t) * 0.3f + Sin(225.0, t) * 0.25f + Sin(300.5, t) * 0.12f) + n.Band(0.6f, 0.2f) * 0.35f * breath;
			}); }();
			return clip;
		}

		const SoundClip& LaneLoop()
		{
			// Riding a hyperlane: a pulsing drone and a thin high shimmer.
			static const SoundClip clip = [] { return MakeLoop(2.0f, "LaneLoop", 0.45f, [](double t)
			{
				const float pulse = 0.6f + 0.4f * Sin(4.0, t);
				return pulse * (Sin(90.0, t) * 0.5f + Sin(135.0, t) * 0.3f) + (Sin(1800.0, t) + Sin(1803.0, t)) * 0.05f;
			}); }();
			return clip;
		}

		const SoundClip& SpoolLoop()
		{
			// A drive charging: a tremoloed stack of harmonics, its pitch raised as the charge builds (SetLoopPitch).
			static const SoundClip clip = [] { return MakeLoop(2.0f, "SpoolLoop", 0.45f, [](double t)
			{
				const float trem = 0.55f + 0.45f * Sin(8.0, t);
				return trem * (Sin(200.0, t) * 0.45f + Sin(300.0, t) * 0.3f + Sin(400.0, t) * 0.2f + Sin(600.5, t) * 0.08f);
			}); }();
			return clip;
		}

		const SoundClip& FtlLoop()
		{
			// Between the stars on the jump drive: a deep beating drone under the score's own void.
			static const SoundClip clip = [] { NoiseGen n(36); return MakeLoop(2.0f, "FtlLoop", 0.5f, [&n](double t)
			{
				return Sin(40.0, t) * 0.5f + Sin(40.5, t) * 0.4f + Sin(60.0, t) * 0.2f + n.Low(0.02f) * 1.5f;
			}); }();
			return clip;
		}

		// ---- flight one-shots

		const SoundClip& BoostIgnite()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.6f);
				Noise(b, 0, 0.55f, 1.0f, 0.5f, 0.005f, 41);
				Tone(b, 0, 0.35f, 90, 40, 0.8f, Wave::Sine, 0.002f);
				Tone(b, 0, 0.3f, 400, 900, 0.2f, Wave::Saw, 0.01f);
				return Finish(b, "BoostIgnite", 0.7f);
			}();
			return clip;
		}

		const SoundClip& RcsPuff()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.1f);
				Noise(b, 0, 0.08f, 0.8f, 0.85f, 0.001f, 42);
				Noise(b, 0, 0.06f, 0.5f, 0.3f, 0.001f, 43);
				return Finish(b, "RcsPuff", 0.5f);
			}();
			return clip;
		}

		const SoundClip& FlipWhoosh()
		{
			static const SoundClip clip = []
			{
				// The jets kick the nose round (two pairs of puffs, one to start the swing, one to stop it) over the slide.
				auto b = Buffer(0.9f);
				const float at[] = { 0.0f, 0.05f, 0.52f, 0.58f };
				for (int i = 0; i < 4; i++) Noise(b, at[i], 0.07f, 0.7f, 0.85f, 0.001f, 44 + static_cast<std::uint32_t>(i));
				Noise(b, 0.05f, 0.65f, 0.35f, 0.22f, 0.25f, 48);
				return Finish(b, "FlipWhoosh", 0.55f);
			}();
			return clip;
		}

		const SoundClip& DriftCut()
		{
			static const SoundClip clip = []
			{
				// The drive let go: its note falls away and the exhaust sighs out.
				auto b = Buffer(0.45f);
				Tone(b, 0, 0.4f, 220, 60, 0.6f, Wave::Saw, 0.002f);
				Noise(b, 0, 0.3f, 0.35f, 0.2f, 0.002f, 49);
				return Finish(b, "DriftCut", 0.5f);
			}();
			return clip;
		}

		// ---- the drives

		const SoundClip& SpoolStart()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.35f);
				Tone(b, 0, 0.3f, 300, 620, 0.5f, Wave::Triangle, 0.004f);
				Noise(b, 0, 0.03f, 0.4f, 0.9f, 0.0005f, 51);
				return Finish(b, "SpoolStart", 0.5f);
			}();
			return clip;
		}

		const SoundClip& SpoolAbort()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.6f);
				Tone(b, 0, 0.55f, 500, 90, 0.5f, Wave::Triangle, 0.003f);
				Tone(b, 0, 0.55f, 750, 135, 0.25f, Wave::Sine, 0.003f);
				return Finish(b, "SpoolAbort", 0.5f);
			}();
			return clip;
		}

		const SoundClip& CruiseEngage()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(1.4f);
				Tone(b, 0, 1.2f, 80, 30, 0.9f, Wave::Sine, 0.003f);
				Tone(b, 0, 0.6f, 200, 1600, 0.4f, Wave::Saw, 0.02f);
				Noise(b, 0, 1.3f, 0.7f, 0.4f, 0.02f, 52);
				return Finish(b, "CruiseEngage", 0.75f);
			}();
			return clip;
		}

		const SoundClip& CruiseDrop()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(1.0f);
				Tone(b, 0, 0.9f, 1400, 120, 0.45f, Wave::Saw, 0.002f);
				Noise(b, 0, 0.6f, 0.6f, 0.5f, 0.002f, 53);
				Tone(b, 0.05f, 0.5f, 70, 35, 0.7f, Wave::Sine, 0.002f);
				return Finish(b, "CruiseDrop", 0.7f);
			}();
			return clip;
		}

		const SoundClip& LaneEnter()
		{
			static const SoundClip clip = []
			{
				// A rising chord and a wash of shimmer: onto the lane.
				auto b = Buffer(1.6f);
				Tone(b, 0, 1.4f, 220.0f, 880.0f, 0.4f, Wave::Sine, 0.1f);
				Tone(b, 0, 1.4f, 277.2f, 1108.7f, 0.3f, Wave::Sine, 0.1f);
				Tone(b, 0, 1.4f, 329.6f, 1318.5f, 0.3f, Wave::Sine, 0.1f);
				Noise(b, 0.2f, 1.3f, 0.25f, 0.9f, 0.4f, 54);
				return Finish(b, "LaneEnter", 0.6f);
			}();
			return clip;
		}

		const SoundClip& LaneExit()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(1.2f);
				Tone(b, 0, 1.1f, 880.0f, 220.0f, 0.4f, Wave::Sine, 0.01f);
				Tone(b, 0, 1.1f, 1108.7f, 277.2f, 0.3f, Wave::Sine, 0.01f);
				Tone(b, 0, 1.1f, 1318.5f, 329.6f, 0.3f, Wave::Sine, 0.01f);
				Noise(b, 0, 0.5f, 0.3f, 0.6f, 0.002f, 55);
				return Finish(b, "LaneExit", 0.6f);
			}();
			return clip;
		}

		const SoundClip& GateJump()
		{
			static const SoundClip clip = []
			{
				// The gate takes the hull: a long rising sweep in a roar, a bell as it opens and the thump of the far side.
				auto b = Buffer(2.3f);
				Tone(b, 0, 2.0f, 60, 600, 0.6f, Wave::Saw, 0.3f);
				Noise(b, 0, 2.0f, 0.6f, 0.5f, 0.8f, 56);
				Tone(b, 1.6f, 0.65f, 1046.5f, 1046.5f, 0.4f, Wave::Sine, 0.002f);
				Tone(b, 1.6f, 0.4f, 2093.0f, 2093.0f, 0.15f, Wave::Sine, 0.002f);
				Tone(b, 1.7f, 0.55f, 90, 30, 0.8f, Wave::Sine, 0.002f);
				return Finish(b, "GateJump", 0.75f);
			}();
			return clip;
		}

		const SoundClip& FtlJump()
		{
			static const SoundClip clip = []
			{
				// The jump drive lets go: a deep climbing howl, then a boom and a blast of noise as space folds.
				auto b = Buffer(2.6f);
				Tone(b, 0, 1.6f, 40, 320, 0.7f, Wave::Saw, 0.2f);
				Tone(b, 1.4f, 1.1f, 120, 25, 1.0f, Wave::Sine, 0.002f);
				Noise(b, 1.3f, 1.2f, 1.0f, 0.3f, 0.005f, 57);
				return Finish(b, "FtlJump", 0.8f);
			}();
			return clip;
		}

		const SoundClip& FtlArrive()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(1.4f);
				Tone(b, 0, 1.2f, 900, 60, 0.5f, Wave::Saw, 0.003f);
				Noise(b, 0, 0.8f, 0.7f, 0.4f, 0.002f, 58);
				Tone(b, 0.1f, 0.8f, 80, 30, 0.8f, Wave::Sine, 0.002f);
				return Finish(b, "FtlArrive", 0.7f);
			}();
			return clip;
		}

		// ---- alerts (the Interface bus)

		const SoundClip& LockTone()
		{
			static const SoundClip clip = [] { auto b = Buffer(0.14f); Tone(b, 0, 0.12f, 1000, 1000, 0.5f, Wave::Square, 0.002f); return Finish(b, "LockTone", 0.4f); }();
			return clip;
		}

		const SoundClip& MissileTone()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.16f);
				Tone(b, 0, 0.06f, 1700, 1700, 0.5f, Wave::Square, 0.001f);
				Tone(b, 0.08f, 0.06f, 1700, 1700, 0.5f, Wave::Square, 0.001f);
				return Finish(b, "MissileTone", 0.5f);
			}();
			return clip;
		}

		const SoundClip& ShieldsDown()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.75f);
				Tone(b, 0, 0.3f, 880, 660, 0.5f, Wave::Triangle, 0.003f);
				Tone(b, 0.3f, 0.42f, 660, 330, 0.5f, Wave::Triangle, 0.003f);
				Noise(b, 0, 0.4f, 0.15f, 0.9f, 0.001f, 61);
				return Finish(b, "ShieldsDown", 0.55f);
			}();
			return clip;
		}

		const SoundClip& HullKlaxon()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(1.0f);
				for (int i = 0; i < 4; i++) Tone(b, i * 0.25f, 0.24f, i % 2 == 0 ? 620.0f : 470.0f, i % 2 == 0 ? 620.0f : 470.0f, 0.5f, Wave::Square, 0.005f);
				return Finish(b, "HullKlaxon", 0.55f);
			}();
			return clip;
		}

		const SoundClip& PropellantLow()
		{
			static const SoundClip clip = []
			{
				auto b = Buffer(0.6f);
				const float notes[] = { 880.0f, 740.0f, 587.3f };
				for (int i = 0; i < 3; i++) Tone(b, i * 0.18f, 0.16f, notes[i], notes[i], 0.5f, Wave::Sine, 0.003f);
				return Finish(b, "PropellantLow", 0.5f);
			}();
			return clip;
		}

		const SoundClip& SelfDestructTick()
		{
			static const SoundClip clip = [] { auto b = Buffer(0.1f); Tone(b, 0, 0.09f, 2000, 2000, 0.6f, Wave::Square, 0.001f); return Finish(b, "SelfDestructTick", 0.5f); }();
			return clip;
		}

		const SoundClip& BioMassContact()
		{
			static const SoundClip clip = []
			{
				// Something alive out there: two voices sliding apart, and a thin cry above them.
				auto b = Buffer(1.2f);
				Tone(b, 0, 1.1f, 300, 420, 0.4f, Wave::Triangle, 0.15f);
				Tone(b, 0, 1.1f, 310, 280, 0.4f, Wave::Triangle, 0.15f);
				Tone(b, 0.2f, 0.9f, 900, 700, 0.15f, Wave::Sine, 0.2f);
				return Finish(b, "BioMassContact", 0.5f);
			}();
			return clip;
		}

		void Fx(const SoundClip& clip, float volume = 1.0f, float pitch = 1.0f) { GameAudio::Play2D(clip, AudioBus::Effects, volume, pitch); }
		void Alert(const SoundClip& clip, float volume = 1.0f, float pitch = 1.0f) { GameAudio::Play2D(clip, AudioBus::Interface, volume, pitch); }

		// ================================================================ loops that ease toward a level

		/// A loop eased toward a wanted volume and pitch each frame: started on the way up, stopped once it has faded out.
		struct Layer
		{
			LoopId id = 0;
			const SoundClip* clip = nullptr;
			AudioBus bus = AudioBus::Effects;
			float volume = 0, pitch = 1;

			void Step(const SoundClip& want, float targetVolume, float targetPitch, float dt, float upPerS, float downPerS, AudioBus onBus = AudioBus::Effects)
			{
				if (clip != &want && id != 0)
				{
					// Another voice (a hull of another class): the old one out, the new one in from silence.
					GameAudio::StopLoop(id, 0.25f);
					id = 0;
					volume = 0;
				}
				clip = &want;
				bus = onBus;
				const float k = targetVolume > volume ? 1 - std::exp(-dt * upPerS) : 1 - std::exp(-dt * downPerS);
				volume += (targetVolume - volume) * k;
				pitch += (targetPitch - pitch) * (1 - std::exp(-dt * 6.0f));
				if (id == 0 && targetVolume > 0.01f)
				{
					pitch = targetPitch;
					id = GameAudio::StartLoop(want, bus, volume);
				}
				if (id == 0) return;
				if (targetVolume <= 0.0f && volume < 0.005f)
				{
					Stop(0.05f);
					return;
				}
				if (!GameAudio::LoopPlaying(id))
				{
					id = 0; // its world went away under it: started afresh next frame it is wanted
					return;
				}
				GameAudio::SetLoopVolume(id, volume);
				GameAudio::SetLoopPitch(id, pitch);
			}

			void Stop(float fade = 0.2f)
			{
				if (id != 0) GameAudio::StopLoop(id, fade);
				id = 0;
				volume = 0;
			}
		};

		// ================================================================ what Poll keeps between frames

		struct Passing
		{
			cs::Ref<Ship> ship;
			cs::Ref<BattleUnit> unit;
			cs::Ref<Battle> battle;
			const void* Key() const { return unit != nullptr ? static_cast<const void*>(unit.get()) : static_cast<const void*>(ship.get()); }
		};

		struct PassingVoice
		{
			Passing who;
			LoopId id = 0;
			float volume = 0;
			float rangeKm = 5;
			float basePitch = 1;
		};

		constexpr int PassingVoices = 3;

		struct Seen
		{
			double lastAt = 0;
			const UnitySpaceApp* app = nullptr;
			const World* world = nullptr;

			// The engine and its afterburner.
			Layer engine, boost;
			bool boosting = false, drifting = false, manoeuvre = false;
			float rcs = 0;
			double rcsAt = 0;
			std::uint32_t puffs = 0;
			float flyingFor = 0, sinceFlown = 99;

			// The drives.
			DriveRegimeWatch regime;
			bool regimePrimed = false;
			bool ftlSpooling = false;
			Layer spool, cruise, lane, ftl;

			// Alerts, on the craft flown (primed afresh for each one so taking a battered hull is not a barrage).
			const void* craft = nullptr;
			double lockAt = 0, missileAt = 0, hullAt = 0;
			bool shieldsArmed = false, hullArmed = false, propellantArmed = false, dryArmed = false;
			int selfDestructStep = -1;
			bool bioArmed = true;
			double bioAt = 0;

			// Other hulls' engines.
			std::array<PassingVoice, PassingVoices> passing;
			double passingAt = 0;
		} seen;

		void StopPassing()
		{
			for (auto& v : seen.passing)
			{
				if (v.id != 0) GameAudio::StopLoop(v.id, 0.4f);
				v = PassingVoice();
			}
		}

		void StopEverything()
		{
			seen.engine.Stop(0.3f);
			seen.boost.Stop(0.2f);
			seen.spool.Stop(0.2f);
			seen.cruise.Stop(0.4f);
			seen.lane.Stop(0.4f);
			seen.ftl.Stop(0.6f);
			StopPassing();
			seen.boosting = seen.drifting = seen.manoeuvre = false;
			seen.rcs = 0;
			seen.flyingFor = 0;
			seen.sinceFlown = 99;
			seen.regime = DriveRegimeWatch();
			seen.regimePrimed = false;
			seen.ftlSpooling = false;
			seen.craft = nullptr;
		}

		// ---------------------------------------------------------------- the craft flown

		struct Craft
		{
			cs::Ref<Ship> ship;          // the hull at the helm (null in a launched craft's seat)
			cs::Ref<BattleUnit> unit;    // its place in a fight, if it is in one
			cs::Ref<Battle> battle;
			cs::Ref<ShipCombat> combat;
			bool helm = false;

			bool Any() const { return ship != nullptr || unit != nullptr; }
			const void* Key() const { return ship != nullptr ? static_cast<const void*>(ship.get()) : static_cast<const void*>(unit.get()); }
		};

		Craft Flown(UnitySpaceApp* app)
		{
			Craft c;
			if (app->IsPiloting())
			{
				c.ship = app->World()->PilotedShip;
				if (c.ship == nullptr || c.ship->Destroyed) return Craft();
				c.helm = true;
				c.battle = c.ship->Battle;
				if (c.battle != nullptr) c.unit = c.battle->UnitOf(c.ship);
				c.combat = c.unit != nullptr && c.unit->Combat != nullptr ? c.unit->Combat : c.ship->Combat;
				return c;
			}
			if (auto f = cs::As<FighterSeat>(app->HeldSeat()))
			{
				if (f->Unit == nullptr || f->Unit->Dead) return Craft();
				c.unit = f->Unit;
				c.battle = f->Battle;
				c.combat = f->Unit->Combat;
			}
			return c;
		}

		/// The engine's voice for a hull of <paramref name="role"/> and its pitch for <paramref name="massTonnes"/>.
		const SoundClip& EngineVoice(FeelRole role, double massTonnes, float& basePitch)
		{
			const float m = static_cast<float>(FlightFeelProfile::MassScale(massTonnes));
			switch (role)
			{
			case FeelRole::Capital: basePitch = std::max(0.55f, 1.15f - 0.35f * m); return EngineHeavy();
			case FeelRole::Hauler: basePitch = std::max(0.6f, 1.2f - 0.35f * m); return EngineMedium();
			default: basePitch = std::max(0.7f, 1.3f - 0.4f * m); return EngineLight();
			}
		}

		void StepEngine(UnitySpaceApp* app, const Craft& craft, float dt)
		{
			const FlightFeel& feel = app->FlightFeelNow();
			const FlightFeelInput& last = feel.Last;
			const bool flying = craft.Any() && feel.Profile.MassTonnes > 0;
			float basePitch = 1;
			const SoundClip& voice = EngineVoice(feel.Profile.Role, feel.Profile.MassTonnes, basePitch);

			float throttle = 0;
			bool drivesDown = false;
			if (craft.helm)
			{
				const HelmFlight& helm = app->PilotHelmFlight();
				throttle = static_cast<float>(std::min(1.0, std::abs(helm.Input.Throttle)));
				drivesDown = helm.DrivesDown || craft.ship->OutOfPropellant;
			}
			const float thrust = static_cast<float>(std::clamp(last.Thrust, 0.0, 1.5));
			const float drive = std::max(thrust, 0.6f * throttle);
			const float pace = static_cast<float>(std::clamp(last.Pace, 0.0, 1.5));
			const bool drifting = flying && app->PilotDrifting();

			float level = 0;
			if (flying)
			{
				level = 0.2f + 0.6f * std::min(1.0f, drive);
				if (drivesDown) level = 0.04f;
				else if (drifting) level = 0.08f;
				else if (last.Cruising) level = 0.3f;
			}
			// A heavy hull's note comes on and dies away slowly, a fighter's at once (its heft, US-T170).
			const float heft = static_cast<float>(std::max(0.15, feel.Profile.HeftS));
			const float pitch = basePitch * (0.8f + 0.3f * std::min(drive, 1.2f) + 0.12f * pace + (last.Boosting ? 0.12f : 0.0f));
			seen.engine.Step(voice, level, pitch, dt, 3.0f / heft, 1.5f / heft);

			// The afterburner: its roar while lit, and the ignition as it catches (a drift's boost-out too).
			const bool boosting = flying && last.Boosting && !drivesDown;
			seen.boost.Step(BoostLoop(), boosting ? 0.5f : 0.0f, basePitch, dt, 10.0f, 4.0f);
			if (boosting && !seen.boosting) Fx(BoostIgnite(), 0.75f, basePitch);
			seen.boosting = boosting;

			// Drift: the drive let go.
			if (drifting && !seen.drifting) Fx(DriftCut(), 0.6f, basePitch);
			seen.drifting = drifting;

			// The RCS jets: a puff as they fire, more while they keep firing hard.
			const float rcs = flying ? app->PilotRcsFiring() : 0.0f;
			const double now = FPlatformTime::Seconds();
			if (rcs > 0.25f && (seen.rcs <= 0.25f || (rcs > 0.5f && now - seen.rcsAt > 0.16)) && now - seen.rcsAt > 0.06)
			{
				// Each puff a little different, so a run of them does not machine-gun.
				const float vary = 0.9f + 0.05f * static_cast<float>(seen.puffs++ % 5);
				Fx(RcsPuff(), 0.2f + 0.45f * rcs, vary * (feel.Profile.Role == FeelRole::Capital ? 0.6f : 1.0f));
				seen.rcsAt = now;
			}
			seen.rcs = rcs;

			// The turn-and-burn's flip (Alt at a non-capital helm; a capital's Alt is its broadside stance).
			const bool manoeuvre = craft.helm && app->Input.Manoeuvre && !app->PilotTelegraph.has_value() && !drivesDown;
			if (manoeuvre && !seen.manoeuvre) Fx(FlipWhoosh(), 0.6f, basePitch);
			seen.manoeuvre = manoeuvre;
		}

		// ---------------------------------------------------------------- the drives

		void DriveCueHeard(DriveRegime was, DriveRegime now, bool inFtl)
		{
			auto cue = DriveRegimes::CueFor(was, now);
			// One carried regime into another (a lane's end straight into a jump): heard as the new one engaging.
			if (cue == DriveCue::None && was != now && DriveRegimes::DriveCarries(was) && DriveRegimes::DriveCarries(now)) cue = DriveCue::Engage;
			switch (cue)
			{
			case DriveCue::Spool: Fx(SpoolStart(), 0.6f); break;
			case DriveCue::Abort: Fx(SpoolAbort(), 0.6f); break;
			case DriveCue::Engage:
				if (now == DriveRegime::Cruise) Fx(CruiseEngage(), 0.8f);
				else if (now == DriveRegime::Lane) Fx(LaneEnter(), 0.7f);
				else if (now == DriveRegime::Ftl) Fx(inFtl ? FtlJump() : GateJump(), 0.85f);
				break;
			case DriveCue::Drop:
				if (was == DriveRegime::Cruise) Fx(CruiseDrop(), 0.75f);
				else if (was == DriveRegime::Lane) Fx(LaneExit(), 0.7f);
				else if (was == DriveRegime::Ftl) Fx(FtlArrive(), 0.8f);
				break;
			default: break;
			}
		}

		void StepDrives(UnitySpaceApp* app, bool flying, float dt)
		{
			const bool inFtl = app->InFtlCruise();
			// The god view's own flights between the stars are the camera's, not a drive: heard only for a craft flown
			// (just now: a gate hands the helm over mid-crossing) or the hull out on its jump drive.
			const bool ours = flying || inFtl || seen.sinceFlown < 1.0f;
			DriveRegime now = ours ? app->PilotDriveRegime() : DriveRegime::Normal;
			if (!ours && seen.regime.Now != DriveRegime::Normal && app->Travel != nullptr && app->Travel->Active) now = seen.regime.Now; // still crossing
			const DriveRegime was = seen.regime.Now;
			seen.regime.Update(now, dt);
			if (seen.regimePrimed && now != was) DriveCueHeard(was, now, inFtl);
			seen.regimePrimed = true;

			// The FTL drive's charge at the helm (not a regime of its own: the hull still flies until it jumps).
			const bool ftlSpooling = flying && app->Ftl != nullptr && app->Ftl->Stage == FtlStage::Spooling;
			if (ftlSpooling && !seen.ftlSpooling) Fx(SpoolStart(), 0.7f, 0.8f);
			else if (!ftlSpooling && seen.ftlSpooling && !inFtl && now != DriveRegime::Ftl) Fx(SpoolAbort(), 0.6f, 0.8f);
			seen.ftlSpooling = ftlSpooling;

			// The loops under each: the charge (rising as it builds), the cruise, the lane, the jump drive.
			float spoolLevel = 0, spoolPitch = 1;
			if (now == DriveRegime::CruiseSpool)
			{
				spoolLevel = 0.35f;
				spoolPitch = 0.6f + 0.9f * static_cast<float>(app->SystemCruiseState().SpoolFraction());
			}
			else if (ftlSpooling)
			{
				spoolLevel = 0.4f;
				spoolPitch = 0.45f + 0.9f * static_cast<float>(app->Ftl->SpoolFraction(app->Clock->Jd));
			}
			seen.spool.Step(SpoolLoop(), spoolLevel, spoolPitch, dt, 6.0f, 4.0f);
			seen.cruise.Step(CruiseLoop(), now == DriveRegime::Cruise ? 0.4f : 0.0f, 1.0f, dt, 1.5f, 2.0f);
			seen.lane.Step(LaneLoop(), now == DriveRegime::Lane ? 0.4f : 0.0f, 1.0f, dt, 1.0f, 1.5f);
			seen.ftl.Step(FtlLoop(), now == DriveRegime::Ftl && inFtl ? 0.3f : 0.0f, 1.0f, dt, 0.5f, 1.0f);
		}

		// ---------------------------------------------------------------- alerts

		/// Whether a hostile in the fight has its nose on <paramref name="me"/>: the lock tone's "someone is lining up".
		bool LockedOn(const Craft& c)
		{
			if (c.battle == nullptr || c.unit == nullptr || c.battle->Over) return false;
			const double cosLock = std::cos(8.0 * 3.141592653589793 / 180.0);
			for (const auto& u : c.battle->Units)
			{
				if (u == nullptr || u == c.unit || u->Dead || u->Fled || u->Target != c.unit || !c.battle->Hostile(u, c.unit)) continue;
				const DVec3 to = c.unit->Pos - u->Pos;
				const double d = to.Length();
				if (d > 1e-9 && DVec3::Dot(u->Forward, to / d) > cosLock) return true;
			}
			return false;
		}

		/// Whether a missile or torpedo is running at the craft (not a boarding pod or a Breacher, which board, not blow).
		bool MissileInbound(const Craft& c)
		{
			if (c.battle == nullptr || c.unit == nullptr) return false;
			for (const auto& m : c.battle->Missiles)
			{
				if (m == nullptr || m->Gone || m->Target != c.unit || m->Side == c.unit->Side) continue;
				if (m->Kind == MissileKind::Pod || m->Kind == MissileKind::Breacher) continue;
				return true;
			}
			return false;
		}

		double ShieldShare(const Craft& c)
		{
			if (c.combat == nullptr || c.combat->Spec == nullptr || !c.combat->HasShieldGenerator() || !(c.combat->Spec->Shield > 0)) return -1;
			return c.combat->Shield() / c.combat->Spec->Shield;
		}

		/// Km from the player to the nearest live Bio Mass globule in this system (infinite: none).
		double NearestBioMassKm(UnitySpaceApp* app)
		{
			const auto& world = app->World();
			const auto bio = world != nullptr ? world->BioMass : nullptr;
			if (bio == nullptr || bio->Globules.Count() == 0 || app->Catalog() == nullptr) return 1e300;
			const double jd = app->Clock->Jd;
			const DVec3 player = app->PlayerHelioKm();
			double nearest = 1e300;
			for (const auto& g : bio->Globules)
			{
				if (g == nullptr || !g->Alive() || g->Body < 0 || g->Body >= app->Catalog()->Count()) continue;
				nearest = std::min(nearest, (bio->Helio(g, jd) - player).Length());
			}
			return nearest;
		}

		/// The HUD's Bio Mass contact range (Hud::BioMassWarnKm), and a margin out past it before contact is heard again.
		constexpr double BioMassWarnKm = 2e6, BioMassRearmKm = 2.4e6;

		void StepAlerts(UnitySpaceApp* app, const Craft& craft)
		{
			if (!craft.Any())
			{
				seen.craft = nullptr;
				seen.selfDestructStep = -1;
				return;
			}
			const double now = FPlatformTime::Seconds();
			const double shields = ShieldShare(craft);
			const double hull = craft.combat != nullptr ? craft.combat->HullFraction() : 1.0;
			const double propellant = craft.ship != nullptr ? craft.ship->PropellantFraction() : 1.0;
			const bool dry = craft.ship != nullptr && craft.ship->Propellant() <= 1e-6;

			// A new craft starts from how it is: only what changes from here on is heard.
			if (seen.craft != craft.Key())
			{
				seen.craft = craft.Key();
				seen.shieldsArmed = shields > 0.1;
				seen.hullArmed = hull > 0.25;
				seen.propellantArmed = propellant >= 0.15;
				seen.dryArmed = !dry;
				seen.hullAt = now;
				seen.selfDestructStep = -1;
				seen.bioArmed = NearestBioMassKm(app) > BioMassWarnKm;
				return;
			}

			// The fight: a missile inbound outranks a lock, each a tone repeating while it lasts.
			if (MissileInbound(craft))
			{
				if (now - seen.missileAt >= 0.3) { Alert(MissileTone(), 0.6f); seen.missileAt = now; }
			}
			else if (LockedOn(craft))
			{
				if (now - seen.lockAt >= 0.7) { Alert(LockTone(), 0.4f); seen.lockAt = now; }
			}

			// Shields collapsing (heard again only once they have come back up past a quarter).
			if (shields >= 0)
			{
				if (seen.shieldsArmed && shields < 0.02) { Alert(ShieldsDown(), 0.7f); seen.shieldsArmed = false; }
				else if (shields > 0.25) seen.shieldsArmed = true;
			}

			// The hull critical: the klaxon as it falls under a quarter, again every few seconds while it is under fire there.
			if (hull < 0.25 && hull > 0)
			{
				if (seen.hullArmed || (craft.battle != nullptr && !craft.battle->Over && now - seen.hullAt >= 6.0))
				{
					Alert(HullKlaxon(), 0.65f);
					seen.hullAt = now;
				}
				seen.hullArmed = false;
			}
			else if (hull > 0.35) seen.hullArmed = true;

			// Propellant: low, then dry (the toast says it too; this is the cockpit's chime under it).
			if (seen.propellantArmed && propellant < 0.15 && !dry) { Alert(PropellantLow(), 0.6f); seen.propellantArmed = false; }
			else if (propellant > 0.2) seen.propellantArmed = true;
			if (seen.dryArmed && dry) { Alert(PropellantLow(), 0.7f, 0.75f); seen.dryArmed = false; }
			else if (!dry) seen.dryArmed = true;

			// The self-destruct counting down: a tick each second, four a second over the last three.
			auto seat = app->HeldSeat();
			const double left = seat != nullptr && SelfDestruct::Can(seat) ? SelfDestruct::Remaining(seat) : -1;
			if (left >= 0)
			{
				const int step = left < 3 ? 1000 + static_cast<int>(std::floor(left * 4)) : static_cast<int>(std::floor(left));
				if (step != seen.selfDestructStep)
				{
					if (seen.selfDestructStep >= 0 || left > 0) Alert(SelfDestructTick(), 0.7f, left < 3 ? 1.3f : 1.0f);
					seen.selfDestructStep = step;
				}
			}
			else seen.selfDestructStep = -1;

			// Bio Mass contact (the HUD's warning line), checked twice a second.
			if (now - seen.bioAt >= 0.5)
			{
				seen.bioAt = now;
				const double km = NearestBioMassKm(app);
				if (seen.bioArmed && km < BioMassWarnKm) { Alert(BioMassContact(), 0.7f); seen.bioArmed = false; }
				else if (km > BioMassRearmKm) seen.bioArmed = true;
			}
		}

		// ---------------------------------------------------------------- other hulls' engines

		/// Scene units the listener (the camera, at the floating origin) puts a passing engine at: its direction is what is
		/// heard from the placing, its loudness this file's own falloff by km.
		constexpr float PassingPlaceUnits = 30.0f;

		/// How close a hull of <paramref name="massScale"/> (FlightFeelProfile::MassScale) is heard: a fighter a few km, a
		/// capital ship over ten.
		float PassingRangeKm(double massScale) { return static_cast<float>(1.5 + 10.0 * massScale); }

		bool PassingScene(UnitySpaceApp* app, const Passing& p, DVec3& scene)
		{
			if (p.unit != nullptr)
			{
				if (p.battle == nullptr || p.unit->Dead || p.unit->Fled) return false;
				scene = app->BattleScene(p.battle, p.unit->Pos);
				return true;
			}
			if (p.ship == nullptr || p.ship->Destroyed || p.ship->GateDeparted) return false;
			scene = app->ShipScene(p.ship);
			return true;
		}

		double PassingMassScale(const Passing& p)
		{
			if (p.ship != nullptr) return FlightFeelProfile::MassScale(p.ship->MassTonnes());
			if (p.unit != nullptr && p.unit->Ship != nullptr) return FlightFeelProfile::MassScale(p.unit->Ship->MassTonnes());
			// A craft with no hull of its own (a launched fighter): by its length, ~15 m light, a km immense.
			const double m = p.unit != nullptr ? p.unit->LengthKm * 1000 : 15;
			return std::clamp(std::log10(std::max(1.0, m)) / 3.0, 0.0, 1.2);
		}

		void PickPassing(UnitySpaceApp* app, const Craft& craft)
		{
			const double kmPerUnit = 1.0 / std::max(1e-12, app->BattleKmScale());
			struct Near { Passing who; double km; float range; };
			std::vector<Near> nearby;
			auto consider = [&](const Passing& p)
			{
				DVec3 scene;
				if (!PassingScene(app, p, scene)) return;
				const Vector3 rel = app->Relative(scene);
				const double km = rel.magnitude() * kmPerUnit;
				const float range = PassingRangeKm(PassingMassScale(p));
				if (km < range) nearby.push_back({ p, km, range });
			};
			if (craft.battle != nullptr)
				for (const auto& u : craft.battle->Units)
					if (u != nullptr && u != craft.unit && !u->Dead && !u->Fled && (craft.ship == nullptr || u->Ship != craft.ship))
						consider({ nullptr, u, craft.battle });
			if (const auto& fleet = app->Fleet())
				for (const auto& s : fleet->Ships())
				{
					if (s == nullptr || s == craft.ship || s->Destroyed || s->GateDeparted || s->Battle != nullptr) continue;
					if (s->Phase == ShipPhase::Parked || s->Phase == ShipPhase::Landed || s->Phase == ShipPhase::Adrift) continue;
					consider({ s, nullptr, nullptr });
				}
			// Nearest first, by how far into its own range it is.
			std::sort(nearby.begin(), nearby.end(), [](const Near& a, const Near& b) { return a.km / a.range < b.km / b.range; });
			if (static_cast<int>(nearby.size()) > PassingVoices) nearby.resize(PassingVoices);

			// Voices whose hull is no longer among the nearest fade out; the newly nearby take the free ones.
			for (auto& v : seen.passing)
			{
				if (v.id == 0) continue;
				const bool kept = std::any_of(nearby.begin(), nearby.end(), [&](const Near& n) { return n.who.Key() == v.who.Key(); });
				if (!kept)
				{
					GameAudio::StopLoop(v.id, 0.5f);
					v = PassingVoice();
				}
			}
			for (const auto& n : nearby)
			{
				const bool held = std::any_of(seen.passing.begin(), seen.passing.end(), [&](const PassingVoice& v) { return v.id != 0 && v.who.Key() == n.who.Key(); });
				if (held) continue;
				for (auto& v : seen.passing)
				{
					if (v.id != 0) continue;
					const double ms = PassingMassScale(n.who);
					const FeelRole role = ms > 0.95 ? FeelRole::Capital : ms > 0.6 ? FeelRole::Hauler : FeelRole::Fighter;
					const SoundClip& voice = EngineVoice(role, std::pow(10.0, ms * 5) - 1, v.basePitch);
					v.who = n.who;
					v.rangeKm = n.range;
					v.volume = 0;
					v.id = GameAudio::StartLoop(voice, AudioBus::Effects, 0.0f, true, Vector3(0, 0, PassingPlaceUnits), PassingPlaceUnits, 1e6f);
					if (v.id == 0) v = PassingVoice();
					break;
				}
			}
		}

		void StepPassing(UnitySpaceApp* app, const Craft& craft, float dt)
		{
			if (!craft.Any())
			{
				StopPassing();
				return;
			}
			const double now = FPlatformTime::Seconds();
			if (now - seen.passingAt >= 0.25)
			{
				seen.passingAt = now;
				PickPassing(app, craft);
			}
			const double kmPerUnit = 1.0 / std::max(1e-12, app->BattleKmScale());
			for (auto& v : seen.passing)
			{
				if (v.id == 0) continue;
				DVec3 scene;
				if (!PassingScene(app, v.who, scene) || !GameAudio::LoopPlaying(v.id))
				{
					GameAudio::StopLoop(v.id, 0.3f);
					v = PassingVoice();
					continue;
				}
				const Vector3 rel = app->Relative(scene);
				const float units = rel.magnitude();
				const float km = static_cast<float>(units * kmPerUnit);
				const float nearby = std::clamp(1.0f - km / std::max(0.1f, v.rangeKm), 0.0f, 1.0f);
				const float want = 0.5f * nearby * nearby;
				v.volume += (want - v.volume) * (1 - std::exp(-dt * 5.0f));
				// Closing, the note rises a touch; going away, it falls (a battle's hulls carry their velocity).
				float doppler = 1.0f;
				if (v.who.unit != nullptr && craft.unit != nullptr)
				{
					const DVec3 to = v.who.unit->Pos - craft.unit->Pos;
					const double d = to.Length();
					if (d > 1e-9)
					{
						const double closing = -DVec3::Dot(v.who.unit->Vel - craft.unit->Vel, to / d);
						doppler = 1.0f + static_cast<float>(std::clamp(closing / 2.0, -0.12, 0.12));
					}
				}
				GameAudio::SetLoopPosition(v.id, units > 1e-4f ? rel.normalized() * PassingPlaceUnits : Vector3(0, 0, PassingPlaceUnits));
				GameAudio::SetLoopVolume(v.id, v.volume);
				GameAudio::SetLoopPitch(v.id, v.basePitch * doppler);
			}
		}
	}

	void Poll(UnitySpaceApp* app)
	{
		const double nowS = FPlatformTime::Seconds();
		const float dt = seen.lastAt > 0 ? static_cast<float>(std::clamp(nowS - seen.lastAt, 0.0, 0.25)) : 0.0f;
		seen.lastAt = nowS;
		if (app == nullptr || app->Galaxy == nullptr || app->ActiveSystem == nullptr || app->World() == nullptr || !GameAudio::Enabled())
		{
			StopEverything();
			seen.app = nullptr;
			seen.world = nullptr;
			return;
		}
		// Another app or world (a load, a new game, a co-op sync): every loop started over, nothing heard from the change.
		if (seen.app != app || seen.world != app->World().get())
		{
			StopEverything();
			seen.app = app;
			seen.world = app->World().get();
		}

		const Craft craft = Flown(app);
		const bool flying = craft.Any();
		if (flying)
		{
			seen.flyingFor += dt;
			seen.sinceFlown = 0;
		}
		else
		{
			seen.flyingFor = 0;
			seen.sinceFlown = std::min(99.0f, seen.sinceFlown + dt);
		}

		StepEngine(app, craft, dt);
		StepDrives(app, flying, dt);
		StepAlerts(app, craft);
		StepPassing(app, craft, dt);
	}
}
