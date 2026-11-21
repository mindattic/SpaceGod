#include "Audio/AmbientSounds.h"
#include "Audio/GameAudio.h"
#include "Audio/SynthSound.h"
#include "Camera/StarTravel.h"
#include "UnitySpaceApp.h"
#include "UnityCompat/UnityRuntime.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/FlightFeel.h"
#include "Ephemeris.h"
#include "Infrastructure/Infrastructure.h"
#include "SimClock.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/SpaceFeatures.h"
#include "World.h"

#include "Async/Async.h"
#include "HAL/PlatformTime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace SpaceGod::Core;
using namespace SpaceGod::Audio::Ambient;

namespace SpaceGod::Audio::AmbientSounds
{
	namespace
	{
		using namespace Synth;

		constexpr double TwoPi = 6.283185307179586;

		// ================================================================ the beds (synthesized, US-T240's way)

		/// Every bed loops over this many seconds: each steady tone in it a whole number of cycles (a multiple of 1/8 Hz).
		constexpr float LoopSeconds = 8.0f;

		/// Filtered noise: white through one-pole filters, the same every run.
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
			/// 0..1, for placing events.
			float Unit() { return 0.5f * (White() + 1.0f); }
			float Low(float a) { low += a * (White() - low); return low; }
			float Band(float hi, float lo)
			{
				const float l = Low(hi);
				lower += lo * (l - lower);
				return l - lower;
			}
		};

		float Sin(double hz, double t) { return static_cast<float>(std::sin(TwoPi * hz * t)); }

		float Saw(double hz, double t, int harmonics)
		{
			double s = 0;
			for (int n = 1; n <= harmonics; n++) s += std::sin(TwoPi * hz * n * t) / n;
			return static_cast<float>(s * 0.55);
		}

		/// A loop's buffer (20 ms over, cross-faded into its start by FinishLoop) filled from a sample function of the time.
		template <class F>
		std::vector<float> Fill(F sample)
		{
			const int count = static_cast<int>(LoopSeconds * Rate);
			std::vector<float> b(static_cast<size_t>(count + Rate / 50), 0.0f);
			for (size_t i = 0; i < b.size(); i++) b[i] += sample(static_cast<double>(i) / Rate);
			return b;
		}

		/// Events land inside the loop, clear of its seam.
		constexpr float EventEnd = LoopSeconds - 0.15f;

		SoundClip VoidBed()
		{
			// A low, slow, barely-there bed: two deep tones beating every four seconds, a darker rumble under them.
			NoiseGen n(101);
			return FinishLoop(Fill([&n](double t)
			{
				const float swell = 0.8f + 0.2f * Sin(0.125, t);
				return swell * (Sin(36.0, t) * 0.5f + Sin(36.25, t) * 0.4f + Sin(54.125, t) * 0.15f) + n.Low(0.004f) * 6.0f;
			}), "AmbientVoid", 0.5f);
		}

		SoundClip StarBed()
		{
			// A hot rushing hum: a heavy rush of noise fluttering over a mains-deep hum, and the solar wind's crackle.
			NoiseGen n(102), m(103);
			auto b = Fill([&n, &m](double t)
			{
				const float flutter = 0.85f + 0.15f * Sin(0.375, t);
				return flutter * (n.Low(0.05f) * 2.0f + m.Band(0.4f, 0.05f) * 0.5f) + Sin(55.0, t) * 0.25f + Sin(110.0, t) * 0.12f + Sin(110.25, t) * 0.1f;
			});
			NoiseGen r(104);
			for (std::uint32_t k = 0; k < 320; k++)
			{
				const float at = r.Unit() * EventEnd, len = 0.003f + 0.015f * r.Unit(), gain = 0.12f + 0.4f * r.Unit() * r.Unit();
				Noise(b, at, len, gain, 0.92f, 0.0005f, 200 + k);
			}
			return FinishLoop(std::move(b), "AmbientStar", 0.55f);
		}

		SoundClip PlanetBed()
		{
			// The magnetosphere: falling whistlers and rising chorus chirps over radio static, and two slow radio sweeps.
			NoiseGen n(105);
			double phaseA = 0, phaseB = 0;
			auto b = Fill([&](double t)
			{
				const double fa = 520.0 + 260.0 * std::sin(TwoPi * 0.25 * t), fb = 780.0 + 150.0 * std::sin(TwoPi * 0.125 * t + 1.0);
				phaseA += TwoPi * fa / Rate;
				phaseB += TwoPi * fb / Rate;
				const float am = 0.5f + 0.5f * Sin(0.5, t);
				const float sweeps = static_cast<float>(std::sin(phaseA)) * 0.14f * am + static_cast<float>(std::sin(phaseB)) * 0.08f * (1.0f - am);
				return n.Band(0.5f, 0.1f) * 0.3f * (0.7f + 0.3f * Sin(0.25, t)) + sweeps;
			});
			const float whistlers[] = { 0.3f, 1.9f, 3.2f, 4.6f, 5.9f };
			for (int i = 0; i < 5; i++) Tone(b, whistlers[i], 1.6f, 4200.0f - 300.0f * i, 700.0f, 0.3f, Wave::Sine, 0.02f);
			const float chirps[] = { 1.1f, 2.6f, 2.8f, 5.2f, 6.8f, 7.0f };
			for (float at : chirps) Tone(b, at, 0.22f, 1300.0f, 2500.0f, 0.1f, Wave::Sine, 0.03f);
			return FinishLoop(std::move(b), "AmbientPlanet", 0.5f);
		}

		SoundClip NebulaBed()
		{
			// Airy shimmering: a breath of bright noise, high tones beating against each other, a soft open pad below.
			NoiseGen n(106);
			return FinishLoop(Fill([&n](double t)
			{
				const float shimmer = (Sin(1760.0, t) + Sin(1762.5, t) * 0.8f + Sin(2640.125, t) * 0.5f + Sin(3521.0, t) * 0.25f) * 0.06f * (0.6f + 0.4f * Sin(0.25, t));
				const float air = n.Band(0.6f, 0.25f) * 0.5f * (0.7f + 0.3f * Sin(0.125, t + 2.0));
				const float pad = (Sin(220.0, t) + Sin(330.125, t) * 0.6f + Sin(440.25, t) * 0.3f) * 0.08f;
				return shimmer + air + pad;
			}), "AmbientNebula", 0.45f);
		}

		SoundClip PortBed()
		{
			// A busy port heard from outside: machinery thrumming, a far-off murmur of comms chatter, squelch blips, clanks
			// and a docking clamp closing.
			NoiseGen n(107), v(108), syl(109);
			float sylAmp = 0, sylTarget = 0;
			int sylLeft = 0;
			auto b = Fill([&](double t)
			{
				const float machinery = (Saw(50.0, t, 5) * 0.3f + Sin(100.0, t) * 0.15f) * (0.75f + 0.25f * Sin(1.5, t)) + n.Low(0.03f) * 1.2f;
				// Syllables: bursts of voice-band noise, some silent (the gaps between words), eased so they do not click.
				if (--sylLeft <= 0)
				{
					sylLeft = static_cast<int>((0.07f + 0.22f * syl.Unit()) * Rate);
					sylTarget = syl.Unit() < 0.3f ? 0.0f : 0.4f + 0.6f * syl.Unit();
				}
				sylAmp += (sylTarget - sylAmp) * 0.002f;
				const float murmur = v.Band(0.22f, 0.06f) * 0.9f * sylAmp * (0.6f + 0.4f * Sin(0.125, t));
				return machinery + murmur;
			});
			const float blips[] = { 1.3f, 4.1f, 6.6f };
			for (float at : blips)
			{
				Tone(b, at, 0.07f, 1400.0f, 1400.0f, 0.1f, Wave::Sine, 0.003f);
				Tone(b, at + 0.08f, 0.07f, 1050.0f, 1050.0f, 0.1f, Wave::Sine, 0.003f);
			}
			// A far clank, then the clamp: a thud, a ringing of steel, a smaller second bite.
			Noise(b, 2.7f, 0.18f, 0.35f, 0.12f, 0.001f, 301);
			Tone(b, 2.7f, 0.6f, 420.0f, 410.0f, 0.1f, Wave::Triangle, 0.001f);
			Noise(b, 5.6f, 0.25f, 0.7f, 0.15f, 0.001f, 302);
			Tone(b, 5.6f, 0.9f, 680.0f, 660.0f, 0.22f, Wave::Triangle, 0.001f);
			Tone(b, 5.6f, 1.2f, 1210.0f, 1190.0f, 0.1f, Wave::Sine, 0.001f);
			Noise(b, 5.86f, 0.15f, 0.4f, 0.2f, 0.001f, 303);
			Tone(b, 5.86f, 0.5f, 690.0f, 675.0f, 0.1f, Wave::Triangle, 0.001f);
			return FinishLoop(std::move(b), "AmbientPort", 0.5f);
		}

		SoundClip BeltBed()
		{
			// Occasional far ticks: grains of rock and ice pinging somewhere off, over the faintest hiss of dust.
			NoiseGen n(110);
			auto b = Fill([&n](double) { return n.Band(0.7f, 0.3f) * 0.08f + n.Low(0.01f) * 0.6f; });
			NoiseGen r(111);
			for (std::uint32_t k = 0; k < 9; k++)
			{
				const float at = (k + 0.15f + 0.7f * r.Unit()) * (EventEnd / 9.0f);
				const float gain = 0.2f + 0.8f * r.Unit(), hz = 1800.0f + 2400.0f * r.Unit();
				Noise(b, at, 0.012f, gain, 0.95f, 0.0003f, 400 + k);
				Tone(b, at, 0.06f + 0.1f * r.Unit(), hz, hz * 0.97f, gain * 0.5f, Wave::Sine, 0.0005f);
			}
			return FinishLoop(std::move(b), "AmbientBelt", 0.5f);
		}

		SoundClip BetweenBed()
		{
			// Between the stars: a wide low chord breathing slowly, and a wind of noise opening and closing over it.
			NoiseGen n(112);
			return FinishLoop(Fill([&n](double t)
			{
				const float swell = 0.75f + 0.25f * Sin(0.125, t);
				const float chord = Sin(65.5, t) * 0.5f + Sin(65.75, t) * 0.4f + Sin(98.25, t) * 0.3f + Sin(131.0, t) * 0.25f + Sin(196.5, t) * 0.12f;
				const float open = 0.08f + 0.1f * (0.5f + 0.5f * Sin(0.125, t + 1.5));
				return swell * chord + n.Band(open, 0.02f) * 0.8f;
			}), "AmbientBetween", 0.5f);
		}

		SoundClip DeepBed()
		{
			// Far from anything charted: a low tone barely there, a glassy voice rising and gone once a loop, a lone ping and
			// its echo, mostly silence.
			NoiseGen n(113);
			auto b = Fill([&n](double t)
			{
				const float glass = std::pow(std::max(0.0f, Sin(0.125, t)), 3.0f) * (Sin(1046.5, t) * 0.12f + Sin(1569.75, t) * 0.06f + Sin(1047.0, t) * 0.05f);
				return Sin(43.625, t) * 0.35f * (0.6f + 0.4f * Sin(0.125, t + 4.0)) + glass + n.Low(0.003f) * 3.0f;
			});
			Tone(b, 5.5f, 2.0f, 2349.0f, 2340.0f, 0.15f, Wave::Sine, 0.001f);
			Tone(b, 6.1f, 1.6f, 2349.0f, 2340.0f, 0.05f, Wave::Sine, 0.001f);
			return FinishLoop(std::move(b), "AmbientDeep", 0.45f);
		}

		SoundClip HullBed()
		{
			// Inside a hull: the reactor's hum and its harmonics throbbing slowly, the air handlers, a creak of the frame.
			NoiseGen n(114);
			auto b = Fill([&n](double t)
			{
				const float throb = 0.85f + 0.15f * Sin(0.5, t);
				return throb * (Sin(60.0, t) * 0.45f + Sin(120.0, t) * 0.25f + Sin(180.0, t) * 0.12f + Sin(240.125, t) * 0.05f) + n.Low(0.12f) * 0.5f;
			});
			Tone(b, 3.3f, 0.35f, 310.0f, 290.0f, 0.06f, Wave::Triangle, 0.01f);
			return FinishLoop(std::move(b), "AmbientHull", 0.5f);
		}

		SoundClip SuitBed()
		{
			// A person in a pressure suit: slow breathing (in bright, out longer and darker, twice a loop), the suit's fan and
			// the radio's faint hiss.
			NoiseGen in(115), out(116), fan(117), hiss(118);
			return FinishLoop(Fill([&](double t)
			{
				const double c = std::fmod(t, 4.0);
				auto bell = [](double x) { const double s = std::sin(3.141592653589793 * std::clamp(x, 0.0, 1.0)); return static_cast<float>(s * s); };
				const float inhale = c < 1.4 ? bell(c / 1.4) : 0.0f;
				const float exhale = c >= 1.7 && c < 3.6 ? bell((c - 1.7) / 1.9) : 0.0f;
				const float breath = in.Band(0.45f, 0.12f) * 1.1f * inhale + out.Band(0.25f, 0.05f) * 1.4f * exhale;
				return breath + Sin(220.0, t) * 0.04f + Sin(440.0, t) * 0.015f + fan.Low(0.2f) * 0.08f + hiss.Band(0.9f, 0.5f) * 0.02f;
			}), "AmbientSuit", 0.5f);
		}

		using Beds = std::array<SoundClip, BedCount>;

		std::shared_ptr<const Beds> Compose()
		{
			auto beds = std::make_shared<Beds>();
			(*beds)[static_cast<int>(Bed::Void)] = VoidBed();
			(*beds)[static_cast<int>(Bed::Star)] = StarBed();
			(*beds)[static_cast<int>(Bed::Planet)] = PlanetBed();
			(*beds)[static_cast<int>(Bed::Nebula)] = NebulaBed();
			(*beds)[static_cast<int>(Bed::Port)] = PortBed();
			(*beds)[static_cast<int>(Bed::Belt)] = BeltBed();
			(*beds)[static_cast<int>(Bed::Between)] = BetweenBed();
			(*beds)[static_cast<int>(Bed::Deep)] = DeepBed();
			(*beds)[static_cast<int>(Bed::Hull)] = HullBed();
			(*beds)[static_cast<int>(Bed::Suit)] = SuitBed();
			return beds;
		}

		// ================================================================ reading the place

		/// How busy a port each kind of structure is (0: not a port at all: a telescope, a probe).
		double BusyOf(StructureKind kind)
		{
			switch (kind)
			{
			case StructureKind::Telescope:
			case StructureKind::Probe: return 0.0;
			case StructureKind::Relay: return 0.4;
			case StructureKind::CrewedStation: return 0.8;
			default: return 1.0;
			}
		}

		/// Light-years from <paramref name="ly"/> (Sol-relative) to the nearest simulated system.
		double LyToNearest(UnitySpaceApp* app, DVec3 ly, std::string& name)
		{
			double best = Far;
			if (app->Galaxy != nullptr)
				for (const auto& s : app->Galaxy->Systems)
				{
					if (s == nullptr) continue;
					const double d = (s->PositionLy - ly).Length();
					if (d < best) { best = d; name = s->Name.c_str(); }
				}
			return best;
		}

		void ReadInside(UnitySpaceApp* app, Place& p, Readout& r)
		{
			auto seat = app->HeldSeat();
			if (seat == nullptr)
			{
				r.Where = "god view";
				return;
			}
			double fallback = 0.5;
			const double flown = app->FlightFeelNow().Profile.MassTonnes;
			switch (seat->Kind())
			{
			case EmbodimentKind::Ship: p.Inside = Ambient::Interior::Hull; break;
			case EmbodimentKind::Turret: p.Inside = Ambient::Interior::Hull; fallback = 0.7; break;
			case EmbodimentKind::Station: p.Inside = Ambient::Interior::Station; fallback = 1.0; break;
			case EmbodimentKind::Fighter: p.Inside = Ambient::Interior::Cockpit; fallback = 0.15; break;
			case EmbodimentKind::Satellite: p.Inside = Ambient::Interior::Cockpit; fallback = 0.05; break;
			case EmbodimentKind::Mech: p.Inside = Ambient::Interior::Cockpit; fallback = 0.25; break;
			default: p.Inside = Ambient::Interior::Suit; break; // a repair worker, an ejected pilot, a marine
			}
			const bool flies = seat->Kind() == EmbodimentKind::Ship || seat->Kind() == EmbodimentKind::Fighter;
			p.HullMassScale = flies && flown > 0 ? FlightFeelProfile::MassScale(flown) : fallback;
			r.Where = std::string(ToString(seat->Kind())) + " " + seat->Name().c_str();
		}

		Place Read(UnitySpaceApp* app, Readout& r)
		{
			Place p;
			r.Star.clear();
			r.Planet.clear();
			r.Port.clear();
			ReadInside(app, p, r);
			const double jd = app->Clock->Jd;

			// Out between the stars: on the FTL drive (or stopped out there), or the god's own flight across.
			if (app->InFtlCruise())
			{
				p.Outside = 1;
				p.Riding = app->FtlCruise->Engaged(jd) ? 1 : 0;
				p.LyFromCharted = LyToNearest(app, app->FtlCruise->PositionAt(jd).Ly(), r.Star);
				return p;
			}
			if (app->Travel != nullptr && app->Travel->Active && (app->Travel->Stage == Cameras::StarTravel::Leg::Cross || !app->SystemVisible))
			{
				p.Outside = 1;
				p.Riding = 1;
				// Through the jump gates the crossing is from one system's gate to the next's: never far from either.
				if (app->Travel->ThroughGate) { p.LyFromCharted = 0; r.Star = "through the gates"; }
				else p.LyFromCharted = LyToNearest(app, app->Travel->CameraLy, r.Star);
				return p;
			}

			const auto& catalog = app->Catalog();
			const auto& eph = app->Ephemeris();
			if (catalog == nullptr || eph == nullptr || catalog->Count() == 0) return p;
			const DVec3 me = app->PlayerHelioKm();

			bool anyStar = false;
			float planetBest = 0, portBest = 0;
			double rockKm = Far;
			for (int i = 0; i < catalog->Count(); i++)
			{
				const auto body = (*catalog)[i];
				if (body == nullptr || !body->IsVisible()) continue;
				const double radius = std::max(1.0, body->RadiusKm);
				const double km = (eph->Helio(i) - me).Length();
				switch (body->Kind)
				{
				case BodyKind::Star:
					anyStar = true;
					if (km / radius < p.StarRadii) { p.StarRadii = km / radius; r.Star = body->Name.c_str(); }
					break;
				case BodyKind::Planet:
				case BodyKind::DwarfPlanet:
				case BodyKind::Moon:
				{
					const bool moon = body->Kind == BodyKind::Moon;
					const float heard = PlanetCloseness(km / radius, body->GasGiant, moon);
					if (heard > planetBest || (planetBest == 0 && km / radius < p.PlanetRadii))
					{
						planetBest = std::max(planetBest, heard);
						p.PlanetRadii = km / radius;
						p.GasGiant = body->GasGiant;
						p.Moon = moon;
						r.Planet = body->Name.c_str();
					}
					if (body->Kind == BodyKind::DwarfPlanet) rockKm = std::min(rockKm, std::max(0.0, km - radius));
					break;
				}
				case BodyKind::Station:
				{
					const double surface = std::max(0.0, km - radius);
					const float heard = PortCloseness(surface, 1.0);
					if (heard > portBest || (portBest == 0 && surface < p.PortKm)) { portBest = std::max(portBest, heard); p.PortKm = surface; p.PortBusy = 1; r.Port = body->Name.c_str(); }
					break;
				}
				case BodyKind::Asteroid: rockKm = std::min(rockKm, std::max(0.0, km - radius)); break;
				default: break;
				}
			}
			p.Starless = !anyStar;

			// Ports: every structure in orbit that ships use (not a telescope or a probe), and the hull held docked.
			const auto& world = app->World();
			if (world != nullptr && world->Infrastructure != nullptr)
				for (const auto& s : world->Infrastructure->Structures)
				{
					if (s == nullptr) continue;
					const double busy = BusyOf(s->Kind);
					if (busy <= 0) continue;
					const double km = std::max(0.0, (s->Helio(eph) - me).Length() - 0.5 * s->LengthKm);
					const float heard = PortCloseness(km, busy);
					if (heard > portBest || (portBest == 0 && km < p.PortKm)) { portBest = std::max(portBest, heard); p.PortKm = km; p.PortBusy = busy; r.Port = s->Name.c_str(); }
				}
			if (app->DockedAt != nullptr || app->DockedStation != nullptr)
			{
				p.Docked = true;
				if (app->DockedStation != nullptr) r.Port = app->DockedStation->Name.c_str();
			}

			// A nebula (how much it masks sensors here, its density's measure), an ice field, Sol's main belt, a belt rock.
			const cs::String root = (*catalog)[0]->Name;
			const double mask = SpaceFeatures::SensorMask(root, me);
			p.Nebula = std::clamp((1.0 - mask) / (1.0 - SpaceFeatures::DeepMask), 0.0, 1.0);
			double belt = Near(rockKm, Tune::RockFullKm, Tune::RockNoneKm);
			for (const auto& field : SpaceFeatures::Of(root).IceFields)
				if (field.RadiusKm > 0) belt = std::max(belt, static_cast<double>(FieldDepth((me - field.CentreKm).Length() / field.RadiusKm)));
			if (app->IsSol())
			{
				const DVec3 fromSun = me - eph->Helio(0);
				belt = std::max(belt, static_cast<double>(BeltBand(std::sqrt(fromSun.X * fromSun.X + fromSun.Y * fromSun.Y) / Astro::AuKm, fromSun.Z / Astro::AuKm)));
			}
			p.Belt = belt;
			return p;
		}

		// ================================================================ playing

		struct Voice
		{
			LoopId Id = 0;
			float Volume = 0, Pitch = 1;
		};

		struct State
		{
			TFuture<std::shared_ptr<const Beds>> Composing;
			std::shared_ptr<const Beds> Composed;
			std::array<Voice, BedCount> Voices{};
			double Last = 0, ReadAt = -1e9;
			bool Watching = false;
			Readout Out;
			const UnitySpaceApp* App = nullptr;
		};

		State& S()
		{
			static State s;
			return s;
		}

		void StopAll(State& s, float fade)
		{
			for (auto& v : s.Voices)
			{
				if (v.Id != 0) GameAudio::StopLoop(v.Id, fade);
				v = Voice();
			}
			s.Out.Volume.fill(0);
			s.Out.Live.fill(false);
		}

		void Step(State& s, float dt)
		{
			for (int i = 0; i < BedCount; i++)
			{
				Voice& v = s.Voices[static_cast<size_t>(i)];
				const Bed bed = static_cast<Bed>(i);
				const float want = s.Out.Wanted.Level[static_cast<size_t>(i)];
				const float pitch = s.Out.Wanted.Pitch[static_cast<size_t>(i)];
				float up, down;
				RatesOf(bed, up, down);
				v.Volume = ExpEase(v.Volume, want, dt, up, down);
				v.Pitch = ExpEase(v.Pitch, pitch, dt, Tune::PitchPerS, Tune::PitchPerS);
				if (v.Id != 0 && !GameAudio::LoopPlaying(v.Id)) v.Id = 0; // its world went away under it: started again below
				if (v.Id == 0 && want > Tune::StartAbove)
				{
					if (v.Volume <= 0) v.Pitch = pitch;
					v.Id = GameAudio::StartLoop((*s.Composed)[static_cast<size_t>(i)], AudioBus::Ambience, v.Volume);
				}
				if (v.Id != 0 && want <= 0 && v.Volume < Tune::StopBelow)
				{
					GameAudio::StopLoop(v.Id, 0.05f);
					v.Id = 0;
					v.Volume = 0;
				}
				if (v.Id != 0)
				{
					GameAudio::SetLoopVolume(v.Id, v.Volume);
					GameAudio::SetLoopPitch(v.Id, v.Pitch);
				}
				else if (want <= 0) v.Volume = 0;
				s.Out.Volume[static_cast<size_t>(i)] = v.Id != 0 ? v.Volume : 0.0f;
				s.Out.Pitch[static_cast<size_t>(i)] = v.Pitch;
				s.Out.Live[static_cast<size_t>(i)] = v.Id != 0;
			}
		}
	}

	const Readout& Now() { return S().Out; }

	void Watch(bool on) { S().Watching = on; }

	void Poll(UnitySpaceApp* app)
	{
		State& s = S();
		const double now = FPlatformTime::Seconds();
		const float dt = s.Last > 0 ? static_cast<float>(std::clamp(now - s.Last, 0.0, 0.25)) : 0.0f;
		s.Last = now;
		const bool sound = GameAudio::Enabled();
		const bool present = app != nullptr && app->Galaxy != nullptr && app->ActiveSystem != nullptr && app->World() != nullptr && app->Clock != nullptr;

		if (!sound)
		{
			StopAll(s, 0.1f);
			s.Out.Sounding = false;
		}
		// The place, read a few times a second (also while silent if the console command is watching).
		if (present && (sound || s.Watching) && now - s.ReadAt >= Tune::ProbeSeconds)
		{
			s.ReadAt = now;
			s.Out.Place = Read(app, s.Out);
			s.Out.Wanted = MixFor(s.Out.Place);
		}
		if (!sound) return;

		if (s.Composed == nullptr)
		{
			// Built once, off the game thread; until then the soundscape is silent.
			if (!s.Composing.IsValid()) s.Composing = Async(EAsyncExecution::ThreadPool, [] { return Compose(); });
			if (!s.Composing.IsReady()) return;
			s.Composed = s.Composing.Get();
			UnityCompat::Debug::Log("[Unity.SpaceGod] Ambience: space beds built (void, star, planet, nebula, port, belt, between, deep, hull, suit).");
		}

		if (!present)
		{
			// The menu, or the app going away: every bed fades out.
			s.Out.Wanted = Mix();
			s.App = nullptr;
		}
		else if (s.App != app)
		{
			// Another app (a new game, a load): the beds start afresh from silence.
			StopAll(s, 0.3f);
			s.App = app;
		}
		s.Out.Sounding = true;
		Step(s, dt);
	}
}
