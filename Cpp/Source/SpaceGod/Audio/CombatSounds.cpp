#include "Audio/CombatSounds.h"
#include "Audio/GameAudio.h"
#include "Audio/SynthSound.h"
#include "UnitySpaceApp.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Combat/WeaponSystems.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/StationSeat.h"
#include "Ships.h"
#include "World.h"

#include "HAL/PlatformTime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

using namespace SpaceGod::Core;
using SpaceGod::UnityCompat::Vector3;

namespace SpaceGod::Audio::CombatSounds
{
	namespace
	{
		using namespace Synth;

		// ================================================================ what is heard

		/// Every sound of a fight, one clip each (and its muffled copy for far away).
		enum class Snd : int
		{
			Gun, Vulcan, VulcanSpin, Railgun, Laser, PointDefense, Phaser, Plasma, Lance,
			Missile, Rocket, Hive, Torpedo, Flak, EmpLaunch, MineLaid,
			Shield, Hull, Intercept, HiveSplit,
			Subsystem, Section, MineBlast, EmpBurst, Explosion,
			Count
		};
		constexpr int SndCount = static_cast<int>(Snd::Count);

		/// The voice pools: each kind plays at most its pool's cap at once, nearest first.
		enum class Pool : int { LightFire, HeavyFire, Spin, Launch, Shield, Hull, Small, Blast, Explosion, Count };
		constexpr int PoolCount = static_cast<int>(Pool::Count);
		constexpr std::array<int, PoolCount> PoolCap = { 6, 4, 1, 3, 3, 4, 3, 3, 3 };

		struct Kind
		{
			Pool pool;
			/// Battle-space km over which it fades from full to nothing clean; past that it is only heard muffled.
			float reachKm;
			/// Its loudness at the listener (times the Effects bus).
			float volume;
			/// Its muffled loudness just past its reach, fading slowly (by orders of magnitude) further out.
			float farFloor;
		};

		constexpr std::array<Kind, SndCount> Kinds =
		{ {
			{ Pool::LightFire, 6, 0.45f, 0.03f },   // Gun
			{ Pool::LightFire, 6, 0.5f, 0.035f },   // Vulcan
			{ Pool::Spin, 5, 0.35f, 0.0f },         // VulcanSpin
			{ Pool::HeavyFire, 14, 0.7f, 0.07f },   // Railgun
			{ Pool::LightFire, 8, 0.45f, 0.03f },   // Laser
			{ Pool::LightFire, 5, 0.3f, 0.02f },    // PointDefense
			{ Pool::HeavyFire, 10, 0.55f, 0.05f },  // Phaser
			{ Pool::HeavyFire, 10, 0.6f, 0.06f },   // Plasma
			{ Pool::HeavyFire, 20, 0.85f, 0.1f },   // Lance
			{ Pool::Launch, 8, 0.5f, 0.04f },       // Missile
			{ Pool::Launch, 7, 0.45f, 0.035f },     // Rocket
			{ Pool::Launch, 8, 0.5f, 0.04f },       // Hive
			{ Pool::Launch, 12, 0.65f, 0.06f },     // Torpedo
			{ Pool::HeavyFire, 9, 0.5f, 0.05f },    // Flak
			{ Pool::Launch, 8, 0.5f, 0.04f },       // EmpLaunch
			{ Pool::Small, 4, 0.4f, 0.0f },         // MineLaid
			{ Pool::Shield, 9, 0.5f, 0.04f },       // Shield
			{ Pool::Hull, 9, 0.6f, 0.05f },         // Hull
			{ Pool::Small, 6, 0.4f, 0.03f },        // Intercept
			{ Pool::Small, 7, 0.4f, 0.03f },        // HiveSplit
			{ Pool::Blast, 18, 0.7f, 0.08f },       // Subsystem
			{ Pool::Blast, 18, 0.65f, 0.07f },      // Section
			{ Pool::Blast, 25, 0.8f, 0.1f },        // MineBlast
			{ Pool::Blast, 20, 0.65f, 0.07f },      // EmpBurst
			{ Pool::Explosion, 60, 1.0f, 0.16f },   // Explosion
		} };

		const Kind& KindOf(Snd s) { return Kinds[static_cast<int>(s)]; }

		// ================================================================ the sounds (synthesized, US-T240's way)

		/// A burst of short pops (a vulcan's brrt, a hive's crackle, debris), each a click of noise over a low knock.
		void Pops(std::vector<float>& b, float at, int count, float spacing, float gain, float hz, std::uint32_t seed)
		{
			for (int i = 0; i < count; i++)
			{
				const float t = at + i * spacing;
				Noise(b, t, 0.03f, gain, 0.7f, 0.0005f, seed + i * 31);
				Tone(b, t, 0.035f, hz, hz * 0.6f, gain * 0.6f, Wave::Triangle, 0.0005f);
			}
		}

		SoundClip Build(Snd s)
		{
			switch (s)
			{
			case Snd::Gun:
			{
				auto b = Buffer(0.16f);
				Noise(b, 0, 0.09f, 0.9f, 0.6f, 0.0005f, 11);
				Tone(b, 0, 0.12f, 180, 70, 0.8f, Wave::Triangle, 0.0005f);
				Tone(b, 0, 0.03f, 1400, 600, 0.2f, Wave::Square, 0.0003f);
				return Finish(b, "Combat.Gun", 0.8f);
			}
			case Snd::Vulcan:
			{
				auto b = Buffer(0.2f);
				Pops(b, 0, 6, 0.026f, 0.7f, 260, 21);
				Noise(b, 0, 0.18f, 0.3f, 0.35f, 0.002f, 23);
				return Finish(b, "Combat.Vulcan", 0.8f);
			}
			case Snd::VulcanSpin:
			{
				auto b = Buffer(0.5f);
				Tone(b, 0, 0.5f, 220, 880, 0.5f, Wave::Saw, 0.05f);
				Tone(b, 0, 0.5f, 440, 1760, 0.2f, Wave::Triangle, 0.05f);
				Noise(b, 0, 0.5f, 0.2f, 0.5f, 0.05f, 29);
				return Finish(b, "Combat.VulcanSpin", 0.6f);
			}
			case Snd::Railgun:
			{
				auto b = Buffer(0.6f);
				Tone(b, 0, 0.06f, 400, 2600, 0.35f, Wave::Sine, 0.04f);          // the capacitor's whine
				Noise(b, 0.05f, 0.08f, 1.0f, 0.95f, 0.0003f, 31);                 // the crack
				Tone(b, 0.05f, 0.5f, 110, 40, 0.9f, Wave::Sine, 0.001f);          // the recoil's thump
				Tone(b, 0.05f, 0.25f, 3200, 1200, 0.15f, Wave::Saw, 0.0005f);
				return Finish(b, "Combat.Railgun", 0.85f);
			}
			case Snd::Laser:
			{
				auto b = Buffer(0.2f);
				Tone(b, 0, 0.18f, 1900, 420, 0.6f, Wave::Square, 0.001f);
				Tone(b, 0, 0.15f, 2850, 630, 0.25f, Wave::Sine, 0.001f);
				return Finish(b, "Combat.Laser", 0.7f);
			}
			case Snd::PointDefense:
			{
				auto b = Buffer(0.09f);
				Tone(b, 0, 0.08f, 3200, 1400, 0.6f, Wave::Square, 0.0005f);
				Noise(b, 0, 0.03f, 0.3f, 0.9f, 0.0005f, 37);
				return Finish(b, "Combat.PointDefense", 0.6f);
			}
			case Snd::Phaser:
			{
				auto b = Buffer(0.4f);
				Tone(b, 0, 0.38f, 560, 470, 0.5f, Wave::Saw, 0.01f);
				Tone(b, 0, 0.38f, 1120, 950, 0.3f, Wave::Sine, 0.01f);
				Tone(b, 0, 0.38f, 567, 474, 0.4f, Wave::Saw, 0.01f); // beating against the first: the shimmer
				return Finish(b, "Combat.Phaser", 0.7f);
			}
			case Snd::Plasma:
			{
				auto b = Buffer(0.4f);
				Tone(b, 0, 0.36f, 260, 80, 0.7f, Wave::Sine, 0.003f);
				Tone(b, 0, 0.3f, 390, 110, 0.35f, Wave::Saw, 0.003f);
				Noise(b, 0, 0.3f, 0.4f, 0.15f, 0.003f, 41);
				return Finish(b, "Combat.Plasma", 0.8f);
			}
			case Snd::Lance:
			{
				auto b = Buffer(1.0f);
				Tone(b, 0, 0.6f, 90, 1800, 0.5f, Wave::Saw, 0.15f);              // the charge
				Noise(b, 0.55f, 0.15f, 1.0f, 0.9f, 0.0005f, 43);                  // the discharge
				Tone(b, 0.55f, 0.45f, 2400, 500, 0.4f, Wave::Square, 0.001f);
				Tone(b, 0.55f, 0.45f, 70, 30, 0.9f, Wave::Sine, 0.002f);
				return Finish(b, "Combat.Lance", 0.9f);
			}
			case Snd::Missile:
			{
				auto b = Buffer(0.7f);
				Noise(b, 0, 0.05f, 0.7f, 0.5f, 0.0005f, 47);                      // the ignition
				Noise(b, 0.02f, 0.65f, 0.7f, 0.3f, 0.06f, 53);                     // the whoosh
				Tone(b, 0.02f, 0.6f, 220, 640, 0.25f, Wave::Triangle, 0.05f);
				return Finish(b, "Combat.Missile", 0.75f);
			}
			case Snd::Rocket:
			{
				auto b = Buffer(0.35f);
				Noise(b, 0, 0.04f, 0.9f, 0.7f, 0.0005f, 59);
				Noise(b, 0.01f, 0.32f, 0.7f, 0.5f, 0.01f, 61);
				Tone(b, 0, 0.3f, 500, 900, 0.2f, Wave::Saw, 0.005f);
				return Finish(b, "Combat.Rocket", 0.75f);
			}
			case Snd::Hive:
			{
				auto b = Buffer(0.75f);
				Noise(b, 0, 0.6f, 0.6f, 0.3f, 0.04f, 67);
				Tone(b, 0, 0.6f, 160, 420, 0.25f, Wave::Triangle, 0.04f);
				Pops(b, 0.25f, 5, 0.06f, 0.3f, 900, 71);                          // the swarm's motors ticking
				return Finish(b, "Combat.Hive", 0.75f);
			}
			case Snd::Torpedo:
			{
				auto b = Buffer(1.1f);
				Tone(b, 0, 0.25f, 90, 45, 1.0f, Wave::Sine, 0.001f);               // the tube's thunk
				Noise(b, 0, 0.06f, 0.6f, 0.4f, 0.0005f, 73);
				Noise(b, 0.08f, 1.0f, 0.6f, 0.12f, 0.1f, 79);                      // the heavy motor's roar
				Tone(b, 0.1f, 0.9f, 70, 140, 0.35f, Wave::Saw, 0.1f);
				return Finish(b, "Combat.Torpedo", 0.85f);
			}
			case Snd::Flak:
			{
				auto b = Buffer(0.45f);
				Noise(b, 0, 0.05f, 0.6f, 0.7f, 0.0005f, 83);                      // the gun
				Noise(b, 0.08f, 0.35f, 1.0f, 0.35f, 0.001f, 89);                   // the shell's burst
				Tone(b, 0.08f, 0.3f, 130, 50, 0.7f, Wave::Sine, 0.001f);
				return Finish(b, "Combat.Flak", 0.8f);
			}
			case Snd::EmpLaunch:
			{
				auto b = Buffer(0.6f);
				Noise(b, 0, 0.55f, 0.5f, 0.3f, 0.04f, 97);
				Tone(b, 0, 0.55f, 120, 120, 0.35f, Wave::Square, 0.04f);          // its charged buzz
				Tone(b, 0, 0.55f, 1500, 2200, 0.15f, Wave::Sine, 0.04f);
				return Finish(b, "Combat.EmpLaunch", 0.7f);
			}
			case Snd::MineLaid:
			{
				auto b = Buffer(0.4f);
				Tone(b, 0, 0.08f, 300, 160, 0.8f, Wave::Triangle, 0.0005f);       // the clunk out of the rack
				Noise(b, 0, 0.04f, 0.5f, 0.5f, 0.0005f, 101);
				Tone(b, 0.18f, 0.12f, 1800, 1800, 0.3f, Wave::Sine, 0.002f);      // armed
				return Finish(b, "Combat.MineLaid", 0.6f);
			}
			case Snd::Shield:
			{
				auto b = Buffer(0.4f);
				Tone(b, 0, 0.35f, 950, 280, 0.6f, Wave::Sine, 0.001f);
				Tone(b, 0, 0.3f, 1425, 420, 0.35f, Wave::Triangle, 0.001f);
				Tone(b, 0, 0.35f, 960, 290, 0.3f, Wave::Sine, 0.001f);
				Noise(b, 0, 0.12f, 0.5f, 0.85f, 0.0005f, 103);
				return Finish(b, "Combat.Shield", 0.7f);
			}
			case Snd::Hull:
			{
				auto b = Buffer(0.45f);
				Noise(b, 0, 0.25f, 0.9f, 0.25f, 0.0005f, 107);
				Tone(b, 0, 0.3f, 150, 65, 0.8f, Wave::Sine, 0.0005f);
				Tone(b, 0, 0.4f, 620, 600, 0.18f, Wave::Sine, 0.0005f);           // the plate ringing
				Tone(b, 0, 0.35f, 1475, 1440, 0.1f, Wave::Sine, 0.0005f);
				return Finish(b, "Combat.Hull", 0.8f);
			}
			case Snd::Intercept:
			{
				auto b = Buffer(0.2f);
				Noise(b, 0, 0.15f, 0.8f, 0.5f, 0.0005f, 109);
				Tone(b, 0, 0.12f, 520, 180, 0.5f, Wave::Triangle, 0.0005f);
				return Finish(b, "Combat.Intercept", 0.7f);
			}
			case Snd::HiveSplit:
			{
				auto b = Buffer(0.35f);
				Noise(b, 0, 0.05f, 0.7f, 0.8f, 0.0005f, 113);
				Pops(b, 0.03f, 8, 0.03f, 0.35f, 700, 127);
				return Finish(b, "Combat.HiveSplit", 0.7f);
			}
			case Snd::Subsystem:
			{
				auto b = Buffer(0.8f);
				Noise(b, 0, 0.5f, 1.0f, 0.3f, 0.001f, 131);
				Tone(b, 0, 0.45f, 120, 45, 0.8f, Wave::Sine, 0.001f);
				Pops(b, 0.1f, 9, 0.06f, 0.25f, 1600, 137);                        // its circuits shorting out
				return Finish(b, "Combat.Subsystem", 0.85f);
			}
			case Snd::Section:
			{
				auto b = Buffer(0.9f);
				Tone(b, 0, 0.8f, 190, 55, 0.6f, Wave::Saw, 0.02f);                // plate tearing
				Tone(b, 0, 0.7f, 205, 60, 0.4f, Wave::Saw, 0.02f);
				Noise(b, 0, 0.8f, 0.7f, 0.2f, 0.01f, 139);
				return Finish(b, "Combat.Section", 0.8f);
			}
			case Snd::MineBlast:
			{
				auto b = Buffer(1.2f);
				Noise(b, 0, 0.08f, 1.0f, 0.8f, 0.0005f, 149);
				Noise(b, 0, 1.1f, 1.0f, 0.12f, 0.002f, 151);
				Tone(b, 0, 0.9f, 80, 30, 1.0f, Wave::Sine, 0.001f);
				return Finish(b, "Combat.MineBlast", 0.9f);
			}
			case Snd::EmpBurst:
			{
				auto b = Buffer(0.8f);
				Tone(b, 0, 0.6f, 2400, 90, 0.6f, Wave::Square, 0.0005f);
				Noise(b, 0, 0.7f, 0.6f, 0.9f, 0.001f, 157);
				Tone(b, 0, 0.7f, 60, 60, 0.4f, Wave::Square, 0.001f);
				return Finish(b, "Combat.EmpBurst", 0.75f);
			}
			case Snd::Explosion:
			default:
			{
				auto b = Buffer(2.4f);
				Noise(b, 0, 0.1f, 1.0f, 0.85f, 0.0005f, 163);                     // the flash's crack
				Noise(b, 0, 2.3f, 1.2f, 0.07f, 0.004f, 167);                       // the fireball's roar
				Tone(b, 0, 1.6f, 70, 24, 1.0f, Wave::Sine, 0.002f);
				Tone(b, 0, 1.2f, 105, 35, 0.5f, Wave::Triangle, 0.002f);
				Pops(b, 0.3f, 10, 0.12f, 0.2f, 400, 173);                         // secondary blasts, debris
				return Finish(b, "Combat.Explosion", 0.95f);
			}
			}
		}

		/// The clip low-passed hard (three one-pole passes): what a fight sounds like heard from far off.
		SoundClip Muffle(const SoundClip& clip)
		{
			if (!clip.IsValid()) return clip;
			const auto& in = *clip.Samples;
			std::vector<float> b(in.size() + Rate / 10, 0.0f);
			for (size_t i = 0; i < in.size(); i++) b[i] = in[i] / 32768.0f;
			for (int pass = 0; pass < 3; pass++)
			{
				float low = 0;
				for (float& s : b)
				{
					low += 0.06f * (s - low);
					s = low;
				}
			}
			return Finish(b, clip.Name, 0.7f);
		}

		const SoundClip& Clip(Snd s, bool muffled)
		{
			static std::array<SoundClip, SndCount> clean, far;
			static std::array<bool, SndCount> built{};
			const int i = static_cast<int>(s);
			if (!built[i])
			{
				clean[i] = Build(s);
				far[i] = Muffle(clean[i]);
				built[i] = true;
			}
			return muffled ? far[i] : clean[i];
		}

		// ================================================================ the cues the fight raises

		struct Pending
		{
			Snd snd = Snd::Gun;
			DVec3 at;
			int battleId = 0;
			float volume = 1;
			float pitch = 1;
			bool own = false;
		};

		constexpr size_t MaxPending = 1024;

		struct State
		{
			std::mutex lock;
			std::vector<Pending> pending;
			bool bound = false;
			const void* ownUnit = nullptr; // the unit the listener holds, so its own shots are heard first
			const World* world = nullptr;
			std::array<std::array<double, 8>, PoolCount> busyUntil{};
			std::uint32_t jitter = 0x9E3779B9u;
		};

		State& S()
		{
			static State s;
			return s;
		}

		/// A small, steady pitch wobble so a burst of the same gun is not one sound repeated.
		float Jitter(float spread)
		{
			auto& j = S().jitter;
			j ^= j << 13; j ^= j >> 17; j ^= j << 5;
			return 1.0f + spread * (static_cast<float>(j & 0xFFFF) / 32767.5f - 1.0f);
		}

		/// The launcher behind a launch: an EMP round, a rocket salvo, a hive round, a torpedo, or a homing missile.
		Snd LaunchOf(const CombatFx* fx, WeaponFamily family)
		{
			if (fx != nullptr && fx->Unit != nullptr && fx->Unit->Combat != nullptr && fx->Sub >= 0 && fx->Sub < fx->Unit->Combat->Subsystems.Count())
			{
				const auto& sub = fx->Unit->Combat->Subsystems[fx->Sub];
				const cs::Ref<WeaponSpec> w = sub != nullptr && sub->Def != nullptr ? sub->Def->Weapon : nullptr;
				if (w != nullptr)
				{
					if (w->EmpSeconds > 0) return Snd::EmpLaunch;
					if (w->Unguided) return Snd::Rocket;
					if (w->SplitInto > 0) return Snd::Hive;
				}
			}
			return family == WeaponFamily::Torpedo ? Snd::Torpedo : Snd::Missile;
		}

		/// Loudness for a hit of <paramref name="damage"/> hit points: a scratch quiet, a broadside loud.
		float HitVolume(double damage) { return static_cast<float>(std::clamp(0.45 + 0.2 * std::log10(1.0 + std::max(0.0, damage)), 0.35, 1.15)); }

		/// Called by the Core for every cue (WeaponSystems::SoundHook): only noted down, nothing in the fight is touched.
		void Take(const WeaponSoundCue& cue)
		{
			Pending p;
			p.at = cue.At;
			p.battleId = cue.BattleId;
			const CombatFx* fx = cue.Fx;
			bool spin = false;
			float spinPitch = 1;
			switch (cue.Event)
			{
			case WeaponCueEvent::Fire:
				switch (cue.Family)
				{
				case WeaponFamily::Kinetic:
					if (fx != nullptr && fx->Kind == FxKind::Vulcan)
					{
						// The barrels' spin: a cold vulcan coughs low and whirs up; at full spin it screams.
						const double s = fx->ByMount != nullptr ? std::clamp(fx->ByMount->Spin, 0.0, 1.0) : 1.0;
						p.snd = Snd::Vulcan;
						p.pitch = static_cast<float>(0.8 + 0.4 * s);
						spin = s < 0.7;
						spinPitch = static_cast<float>(0.7 + 0.6 * s);
					}
					else p.snd = Snd::Gun;
					break;
				case WeaponFamily::Laser:
					p.snd = fx != nullptr && fx->Kind == FxKind::PointDefense ? Snd::PointDefense
						: fx != nullptr && (fx->Kind == FxKind::Phaser || fx->Kind == FxKind::PhaserMiss) ? Snd::Phaser : Snd::Laser;
					break;
				case WeaponFamily::Plasma: p.snd = Snd::Plasma; break;
				case WeaponFamily::Railgun: p.snd = Snd::Railgun; break;
				case WeaponFamily::Lance: p.snd = Snd::Lance; break;
				case WeaponFamily::Flak: p.snd = Snd::Flak; break;
				case WeaponFamily::Missile:
				case WeaponFamily::Torpedo: p.snd = LaunchOf(fx, cue.Family); break;
				default: return;
				}
				break;
			case WeaponCueEvent::ShieldImpact:
				p.snd = Snd::Shield;
				p.volume = HitVolume(cue.Size);
				break;
			case WeaponCueEvent::Intercepted:
				p.snd = Snd::Intercept;
				break;
			case WeaponCueEvent::Impact:
			default:
				if (fx != nullptr && cue.Family == WeaponFamily::None)
				{
					switch (fx->Kind)
					{
					case FxKind::UnitDestroyed:
					{
						// A fighter pops; a capital ship's death is a long, low roar.
						const double m = std::log10(1.0 + std::max(0.0, cue.Size) * 1000.0);
						p.snd = Snd::Explosion;
						p.pitch = static_cast<float>(std::clamp(1.35 - 0.25 * m, 0.55, 1.3));
						p.volume = static_cast<float>(std::clamp(0.55 + 0.17 * m, 0.55, 1.1));
						break;
					}
					case FxKind::SubsystemDestroyed: p.snd = Snd::Subsystem; break;
					case FxKind::SectionLost: p.snd = Snd::Section; break;
					case FxKind::MineBlast: p.snd = Snd::MineBlast; break;
					case FxKind::EmpBurst: p.snd = Snd::EmpBurst; break;
					case FxKind::Deployed: p.snd = Snd::MineLaid; break;
					case FxKind::HiveSplit: p.snd = Snd::HiveSplit; break;
					default: return;
					}
					break;
				}
				p.snd = Snd::Hull;
				p.volume = HitVolume(cue.Size);
				// A railgun's slug rings sharper, a plasma bolt or a warhead lands heavier.
				if (cue.Family == WeaponFamily::Railgun) p.pitch = 1.25f;
				else if (cue.Family == WeaponFamily::Plasma) p.pitch = 0.85f;
				else if (cue.Family == WeaponFamily::Torpedo || cue.Family == WeaponFamily::Missile) { p.pitch = 0.75f; p.volume *= 1.15f; }
				else if (cue.Family == WeaponFamily::Kinetic) p.pitch = 1.15f;
				break;
			}
			auto& s = S();
			p.own = fx != nullptr && fx->By != nullptr && s.ownUnit != nullptr && static_cast<const void*>(fx->By.get()) == s.ownUnit;
			std::lock_guard<std::mutex> guard(s.lock);
			if (s.pending.size() >= MaxPending) return; // a backlog (a warp, a co-op catch-up): the newest are dropped
			s.pending.push_back(p);
			if (spin)
			{
				Pending w = p;
				w.snd = Snd::VulcanSpin;
				w.pitch = spinPitch;
				s.pending.push_back(w);
			}
		}

		void Bind()
		{
			auto& s = S();
			if (s.bound) return;
			WeaponSystems::SoundHook() = [](const WeaponSoundCue& cue) { Take(cue); };
			s.bound = true;
		}

		void Clear()
		{
			auto& s = S();
			std::lock_guard<std::mutex> guard(s.lock);
			s.pending.clear();
		}

		// ================================================================ the listener

		/// The unit the local player holds in a fight (the helm's hull, a fighter's seat, a turret, a station), and its fight.
		cs::Ref<BattleUnit> Held(UnitySpaceApp* app, cs::Ref<Battle>& battle)
		{
			battle = nullptr;
			if (app->IsPiloting())
			{
				auto me = app->PilotUnit();
				if (me != nullptr && app->Piloted.Ship != nullptr) battle = app->Piloted.Ship->Battle;
				return battle != nullptr ? me : nullptr;
			}
			auto held = app->HeldSeat();
			if (auto f = cs::As<FighterSeat>(held)) { battle = f->Battle; return f->Unit; }
			if (auto m = cs::As<MountedTurret>(held)) { battle = m->Battle; return m->Unit; }
			if (auto st = cs::As<StationSeat>(held)) return st->Unit(battle);
			return nullptr;
		}

		/// Scene units a sound is placed from the camera: its direction is what is heard from the placing, its loudness this
		/// file's own falloff by battle km (as FlightSounds places a passing engine).
		constexpr float PlaceUnits = 30.0f;

		struct Placed
		{
			Pending p;
			Vector3 dir;
			float km = 0;
			float rank = 0; // how far into its reach: the voices go nearest first
		};
	}

	void Shutdown()
	{
		auto& s = S();
		if (s.bound) WeaponSystems::SoundHook() = nullptr;
		s.bound = false;
		s.ownUnit = nullptr;
		s.world = nullptr;
		Clear();
	}

	void Poll(UnitySpaceApp* app)
	{
		if (app == nullptr || app->World() == nullptr || app->World()->War == nullptr || !GameAudio::Enabled())
		{
			Shutdown();
			return;
		}
		auto& s = S();
		Bind();
		// Another world (a load, a new game, a co-op sync): what was raised before it is not heard.
		if (s.world != app->World().get())
		{
			s.world = app->World().get();
			Clear();
		}

		cs::Ref<Battle> heldBattle;
		const cs::Ref<BattleUnit> me = Held(app, heldBattle);
		s.ownUnit = me != nullptr ? static_cast<const void*>(me.get()) : nullptr;

		std::vector<Pending> cues;
		{
			std::lock_guard<std::mutex> guard(s.lock);
			cues.swap(s.pending);
		}
		if (cues.empty()) return;

		const auto& battles = app->World()->War->Battles;
		auto find = [&](int id) -> cs::Ref<Battle>
		{
			for (const auto& b : battles)
				if (b != nullptr && b->Id == id) return b;
			return nullptr;
		};

		const double kmPerUnit = 1.0 / std::max(1e-12, app->BattleKmScale());
		std::array<std::vector<Placed>, PoolCount> byPool;
		cs::Ref<Battle> last;
		int lastId = -1;
		for (const auto& p : cues)
		{
			if (p.battleId != lastId)
			{
				last = find(p.battleId);
				lastId = p.battleId;
			}
			if (last == nullptr) continue; // a fight already over
			const Vector3 rel = app->Relative(app->BattleScene(last, p.at));
			const float units = rel.magnitude();
			Placed q;
			q.p = p;
			q.dir = units > 1e-4f ? rel.normalized() : Vector3(0, 0, 1);
			// In the listener's own fight, by the fight's own km from the craft held; anywhere else, from the camera.
			q.km = me != nullptr && last == heldBattle ? static_cast<float>((p.at - me->Pos).Length()) : static_cast<float>(units * kmPerUnit);
			const Kind& k = KindOf(p.snd);
			q.rank = p.own ? -1.0f : q.km / std::max(0.1f, k.reachKm);
			byPool[static_cast<int>(k.pool)].push_back(q);
		}

		const double now = FPlatformTime::Seconds();
		for (int pool = 0; pool < PoolCount; pool++)
		{
			auto& list = byPool[pool];
			if (list.empty()) continue;
			std::sort(list.begin(), list.end(), [](const Placed& a, const Placed& b) { return a.rank < b.rank; });
			auto& busy = s.busyUntil[pool];
			const int cap = std::min(PoolCap[pool], static_cast<int>(busy.size()));
			for (const auto& q : list)
			{
				int slot = -1;
				for (int i = 0; i < cap; i++)
					if (busy[i] <= now) { slot = i; break; }
				if (slot < 0) break; // the pool is full: the rest (all further off) go unheard

				const Kind& k = KindOf(q.p.snd);
				const float into = q.km / std::max(0.1f, k.reachKm);
				const float clean = into < 1 ? (1 - into) * (1 - into) : 0.0f;
				// Past its reach a sound thins to a low rumble that fades only by orders of magnitude of distance.
				const float far = k.farFloor * std::clamp(1.0f - std::log10(std::max(1.0f, into)) / 5.0f, 0.2f, 1.0f);
				const bool muffled = clean < far;
				float volume = k.volume * q.p.volume * (muffled ? far : clean);
				if (q.p.own) volume = std::min(1.3f, volume * 1.25f + 0.1f * k.volume);
				if (volume < 0.004f) continue;
				float pitch = q.p.pitch * Jitter(0.05f) * (muffled ? 0.9f : 1.0f);
				const SoundClip& clip = Clip(q.p.snd, muffled);
				GameAudio::Play3D(clip, AudioBus::Effects, q.dir * PlaceUnits, volume, pitch, PlaceUnits, 1e6f);
				busy[slot] = now + clip.Seconds() / std::max(0.2f, pitch);
			}
		}
	}
}
