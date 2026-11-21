#pragma once

#include "Audio/AmbientMix.h"

#include <array>
#include <string>

namespace SpaceGod { class UnitySpaceApp; }

namespace SpaceGod::Audio
{
	/// The sound of space itself (US-T448), under the flight and combat sounds, on the Ambience bus (Settings > Audio >
	/// Ambience volume, under the master). Every bed is a seamless loop synthesized in code (no sound files, built once off
	/// the game thread): the deep void, a star's rushing hum and solar-wind crackle, a planet's magnetosphere whistlers and
	/// radio sweeps, a nebula's airy shimmer, a port's distant machinery, comms murmur and docking clamps, a belt's far
	/// ticks, the wider bed between the stars, eerie deep space far from any charted system, and from inside: the hull's
	/// reactor hum (a helm, a cockpit, a gun, a station) or a suit's breathing (a person on EVA).
	///
	/// A few times a second Poll reads where the listener is (the seat held, else the camera; Ambient::Place) and asks
	/// Ambient::MixFor what plays; each frame every bed crossfades toward its level (Ambient::ExpEase), so flying from the
	/// void past a planet into a port, or taking a helm, fades smoothly. Local to this machine: it only reads the app, never
	/// writes it, so nothing enters the Core or the co-op frames and the bench digests are untouched. Silent wherever
	/// GameAudio is (no device, -muted, headless, offscreen, the smoke/shots/audits, -demo, -capture unless -GameAudio).
	namespace AmbientSounds
	{
		/// Once a frame after the app's Update: <paramref name="app"/> is null in a scene without the app (the main menu),
		/// which fades every bed out.
		void Poll(UnitySpaceApp* app);

		/// What the soundscape is doing now (the console command's log).
		struct Readout
		{
			/// Whether the beds are built and sound plays.
			bool Sounding = false;
			Ambient::Place Place;
			/// The levels wanted, and each bed's volume and pitch as last applied (0 and not live while its loop is stopped).
			Ambient::Mix Wanted;
			std::array<float, Ambient::BedCount> Volume{};
			std::array<float, Ambient::BedCount> Pitch{};
			std::array<bool, Ambient::BedCount> Live{};
			/// What the place was read from: the nearest star, the planet and the port heard best, the interior's kind.
			std::string Star, Planet, Port, Where;
		};

		const Readout& Now();

		/// Keeps the place read even while GameAudio is silent (the console command's tour in a headless run).
		void Watch(bool on);
	}
}
