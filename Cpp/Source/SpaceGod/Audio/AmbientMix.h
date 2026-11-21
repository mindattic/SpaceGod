#pragma once

// US-T448: the space soundscape's pure part: which beds play, how loud and at what pitch, from where the listener is and
// how close things are. Header-only and engine-free (std only), so tools/build-core.ps1 unit-checks it standalone
// (Tests/Ambience/AmbientMixTests.cpp); AmbientSounds.cpp reads the app into a Place, asks MixFor and plays the result.
// Every tunable of the soundscape is in Ambient::Tune below.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace SpaceGod::Audio::Ambient
{
	/// The beds of the soundscape, each one synthesized loop on the Ambience bus.
	enum class Bed : std::uint8_t
	{
		/// The deep void: a low, slow, barely-there bed, the floor under everything in a system.
		Void,
		/// Near a star: a hot rushing hum and the crackle of the solar wind.
		Star,
		/// Near a planet or a moon: magnetosphere whistlers and radio-like sweeps (a gas giant's louder and farther out).
		Planet,
		/// Inside a nebula: airy shimmering.
		Nebula,
		/// Near a station or a busy port: distant machinery, a murmur of comms chatter, docking clamps.
		Port,
		/// In an asteroid belt or an ice field: occasional far ticks.
		Belt,
		/// Between the stars, under way (the FTL drive, the god's own flight): a different, wider bed.
		Between,
		/// Deep space far from any charted system (or stopped out there, or a starless void station): eerie and sparse.
		Deep,
		/// Inside a hull (a helm, a cockpit, a gun, a station): the hull itself, its reactor's hum.
		Hull,
		/// A person out in a pressure suit (an EVA worker, an ejected pilot): breathing and the suit.
		Suit,
	};

	inline constexpr int BedCount = 10;

	inline const char* BedName(Bed bed)
	{
		switch (bed)
		{
		case Bed::Void: return "Void";
		case Bed::Star: return "Star";
		case Bed::Planet: return "Planet";
		case Bed::Nebula: return "Nebula";
		case Bed::Port: return "Port";
		case Bed::Belt: return "Belt";
		case Bed::Between: return "Between";
		case Bed::Deep: return "Deep";
		case Bed::Hull: return "Hull";
		case Bed::Suit: return "Suit";
		default: return "?";
		}
	}

	/// Whether a bed is the outside of space (muffled from inside a hull) rather than the listener's own surroundings.
	inline constexpr bool Exterior(Bed bed) { return bed != Bed::Hull && bed != Bed::Suit; }

	/// What the listener is inside of: open space (the god's eye), a hull, a craft's cockpit, a station, or a suit.
	enum class Interior : std::uint8_t { Open, Hull, Cockpit, Station, Suit };

	inline const char* InteriorName(Interior i)
	{
		switch (i)
		{
		case Interior::Hull: return "Hull";
		case Interior::Cockpit: return "Cockpit";
		case Interior::Station: return "Station";
		case Interior::Suit: return "Suit";
		default: return "Open";
		}
	}

	inline constexpr double Far = std::numeric_limits<double>::infinity();

	/// Where the listener is (the camera, or the seat held), as AmbientSounds reads it from the app a few times a second.
	struct Place
	{
		/// Distance to the nearest star, in that star's radii (Far: none in this system).
		double StarRadii = Far;
		/// The planet or moon heard best: its distance in its radii, and whether it is a gas giant or a moon.
		double PlanetRadii = Far;
		bool GasGiant = false;
		bool Moon = false;
		/// How deep in a nebula, 0 clear .. 1 inside a knot.
		double Nebula = 0;
		/// The port heard best: km to it, how busy a thing it is (0..1), and whether the hull held is docked there.
		double PortKm = Far;
		double PortBusy = 1;
		bool Docked = false;
		/// How far into an asteroid belt or ice field (0..1, BeltBand and the cpp's field and rock checks).
		double Belt = 0;
		/// The system has no star at all (a void station's own gravity well, deep in the dark).
		bool Starless = false;
		/// 0 in a star system .. 1 out between the stars (no system shown).
		double Outside = 0;
		/// Out between the stars: 1 under way (the drive carrying it, the god's flight), 0 stopped out there (a void stop).
		double Riding = 1;
		/// Out between the stars: light-years to the nearest simulated system.
		double LyFromCharted = 0;
		/// What the listener is inside of, and the hull's heft (FlightFeelProfile::MassScale: ~0 a fighter, ~1 a capital).
		Interior Inside = Interior::Open;
		double HullMassScale = 0.5;
	};

	/// The wanted volume (0..1, before the Ambience bus) and pitch of every bed.
	struct Mix
	{
		std::array<float, BedCount> Level{};
		std::array<float, BedCount> Pitch{};

		float operator[](Bed b) const { return Level[static_cast<int>(b)]; }
		float PitchOf(Bed b) const { return Pitch[static_cast<int>(b)]; }

		/// The loudest bed (Void when all are silent).
		Bed Loudest() const
		{
			int best = 0;
			for (int i = 1; i < BedCount; i++)
				if (Level[static_cast<size_t>(i)] > Level[static_cast<size_t>(best)]) best = i;
			return static_cast<Bed>(best);
		}
	};

	/// Every tunable of the soundscape (US-T448).
	namespace Tune
	{
		// ---- each bed's loudest (the volume handed to GameAudio, which the Ambience bus and the master scale again)
		inline constexpr float VoidMax = 0.30f;
		inline constexpr float StarMax = 0.45f;
		inline constexpr float PlanetMax = 0.40f;
		inline constexpr float NebulaMax = 0.40f;
		inline constexpr float PortMax = 0.40f;
		inline constexpr float BeltMax = 0.45f;
		inline constexpr float BetweenMax = 0.40f;
		inline constexpr float DeepMax = 0.38f;
		inline constexpr float HullMax = 0.32f;
		inline constexpr float SuitMax = 0.42f;

		// ---- how close is close (log-space crossfades between the full and the none distance)
		/// A star: full within 15 of its radii (the Sun: 0.07 AU), gone past 250 (1.2 AU: Earth hears only a breath of it).
		inline constexpr double StarFullRadii = 15, StarNoneRadii = 250;
		/// A planet or moon: full within 3 of its radii; a rocky world gone past 25, a gas giant's magnetosphere past 90.
		inline constexpr double PlanetFullRadii = 3, PlanetNoneRadii = 25, GiantNoneRadii = 90;
		/// A rocky world's magnetosphere is quieter than a gas giant's, a moon's quieter still.
		inline constexpr float RockyShare = 0.7f, MoonShare = 0.5f;
		/// A port: full within 300 km, gone past 30,000 km.
		inline constexpr double PortFullKm = 300, PortNoneKm = 30000;
		/// A belt body (Psyche, Ceres's kind): full within 3,000 km, gone past 300,000 km.
		inline constexpr double RockFullKm = 3000, RockNoneKm = 300000;

		/// Sol's main belt: 2.1 to 3.3 AU from the Sun, softened over 0.25 AU at each edge; full within 0.15 AU of the
		/// ecliptic, gone 0.5 AU off it.
		inline constexpr double BeltInnerAu = 2.1, BeltOuterAu = 3.3, BeltEdgeAu = 0.25, BeltFlatAu = 0.15, BeltThickAu = 0.5;
		/// An ice field: full inside 60% of its radius, gone at 120%.
		inline constexpr double FieldFullShare = 0.6, FieldNoneShare = 1.2;

		// ---- between the stars
		/// Deep space: none within 1 ly of a simulated system, all of it 4 ly out.
		inline constexpr double DeepFromLy = 1, DeepFullLy = 4;
		/// How much of the wider bed deep space takes over.
		inline constexpr float BetweenDeepGive = 0.7f;
		/// A starless system's share of the deep bed (and what it takes from the void).
		inline constexpr float StarlessDeep = 0.6f;

		/// How much the void floor gives way to the loudest place bed.
		inline constexpr float VoidGive = 0.6f;

		// ---- inside
		/// How much of outer space is still heard: inside a hull, a cockpit, a station, a suit.
		inline constexpr float HullMuffle = 0.4f, CockpitMuffle = 0.55f, SuitMuffle = 0.7f;
		/// The hull bed's share in a cockpit and a station (a full helm is 1).
		inline constexpr float CockpitHull = 0.7f, StationHull = 0.85f;
		/// The reactor's note by heft: a fighter's at HullPitchLight, falling HullPitchDrop per mass scale, kept in range.
		inline constexpr float HullPitchLight = 1.2f, HullPitchDrop = 0.45f, HullPitchMin = 0.6f, HullPitchMax = 1.25f;

		// ---- pitch elsewhere
		/// A star's rush rises a little as it nears; a gas giant's whistlers sit lower than a rocky world's sweeps.
		inline constexpr float StarPitchFar = 0.92f, StarPitchNear = 1.04f, GiantPitch = 0.85f, RockyPitch = 1.1f;

		// ---- crossfades (per second, toward the wanted level: ExpEase)
		inline constexpr float PlaceUpPerS = 0.7f, PlaceDownPerS = 0.5f;
		inline constexpr float StarsUpPerS = 0.5f, StarsDownPerS = 0.4f;
		inline constexpr float InsideUpPerS = 2.5f, InsideDownPerS = 2.0f;
		inline constexpr float PitchPerS = 1.5f;

		/// How often the place is read from the app (s).
		inline constexpr double ProbeSeconds = 0.25;
		/// A loop is started once its bed is wanted above this, stopped once it has faded under StopBelow with nothing wanted.
		inline constexpr float StartAbove = 0.003f, StopBelow = 0.002f;
	}

	inline float Clamp01(double v) { return std::isfinite(v) ? static_cast<float>(std::clamp(v, 0.0, 1.0)) : 0.0f; }

	/// 1 within <paramref name="full"/>, 0 from <paramref name="none"/> on, a smoothstep in log distance between them (so the
	/// fade is as gradual from 2x to 4x as from 20x to 40x). NaN or infinite: 0.
	inline float Near(double d, double full, double none)
	{
		if (!(d < none)) return 0.0f;
		if (d <= full) return 1.0f;
		const double t = 1.0 - (std::log(d) - std::log(full)) / (std::log(none) - std::log(full));
		return static_cast<float>(t * t * (3.0 - 2.0 * t));
	}

	/// How loud a planet or moon <paramref name="radii"/> of its radii away is heard (0..1, before PlanetMax).
	inline float PlanetCloseness(double radii, bool gasGiant, bool moon)
	{
		const float near = Near(radii, Tune::PlanetFullRadii, gasGiant ? Tune::GiantNoneRadii : Tune::PlanetNoneRadii);
		return near * (gasGiant ? 1.0f : moon ? Tune::MoonShare : Tune::RockyShare);
	}

	/// How loud a port <paramref name="km"/> away and <paramref name="busy"/> (0..1) is heard (0..1, before PortMax).
	inline float PortCloseness(double km, double busy) { return Near(km, Tune::PortFullKm, Tune::PortNoneKm) * Clamp01(busy); }

	/// How far into Sol's main belt a point <paramref name="radiusAu"/> from the Sun in the ecliptic plane and
	/// <paramref name="heightAu"/> off it is (0..1).
	inline float BeltBand(double radiusAu, double heightAu)
	{
		if (!std::isfinite(radiusAu) || !std::isfinite(heightAu)) return 0.0f;
		const double e = Tune::BeltEdgeAu;
		auto smooth = [](double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3 - 2 * x); };
		const double across = smooth((radiusAu - (Tune::BeltInnerAu - e)) / e) * smooth(((Tune::BeltOuterAu + e) - radiusAu) / e);
		const double h = std::abs(heightAu);
		const double flat = h <= Tune::BeltFlatAu ? 1.0 : smooth(1.0 - (h - Tune::BeltFlatAu) / (Tune::BeltThickAu - Tune::BeltFlatAu));
		return static_cast<float>(across * flat);
	}

	/// How far into an ice field a point at <paramref name="share"/> of the field's radius from its centre is (0..1).
	inline float FieldDepth(double share)
	{
		if (!std::isfinite(share) || share >= Tune::FieldNoneShare) return 0.0f;
		if (share <= Tune::FieldFullShare) return 1.0f;
		const double t = (Tune::FieldNoneShare - share) / (Tune::FieldNoneShare - Tune::FieldFullShare);
		return static_cast<float>(t * t * (3 - 2 * t));
	}

	/// The reactor hum's pitch for a hull of <paramref name="massScale"/>.
	inline float HullPitch(double massScale)
	{
		const double m = std::isfinite(massScale) ? std::max(0.0, massScale) : 0.5;
		return std::clamp(Tune::HullPitchLight - Tune::HullPitchDrop * static_cast<float>(m), Tune::HullPitchMin, Tune::HullPitchMax);
	}

	/// What plays where the listener is: every bed's wanted level and pitch.
	inline Mix MixFor(const Place& p)
	{
		using namespace Tune;
		Mix m;
		m.Pitch.fill(1.0f);
		auto set = [&m](Bed b, float level, float pitch = 1.0f)
		{
			m.Level[static_cast<size_t>(b)] = std::clamp(std::isfinite(level) ? level : 0.0f, 0.0f, 1.0f);
			m.Pitch[static_cast<size_t>(b)] = pitch;
		};

		const float out = Clamp01(p.Outside), in = 1.0f - out;
		const float riding = std::isfinite(p.Riding) ? Clamp01(p.Riding) : 1.0f;
		const double ly = std::isfinite(p.LyFromCharted) ? std::max(0.0, p.LyFromCharted) : 0.0;
		const float deep = 1.0f - Near(std::max(ly, 1e-9), DeepFromLy, DeepFullLy);

		const float star = Near(p.StarRadii, StarFullRadii, StarNoneRadii);
		const float planet = PlanetCloseness(p.PlanetRadii, p.GasGiant, p.Moon);
		const float nebula = Clamp01(p.Nebula);
		const float port = p.Docked ? 1.0f : PortCloseness(p.PortKm, p.PortBusy);
		const float belt = Clamp01(p.Belt);
		const float loudest = std::max({ star, planet, nebula, port, belt });
		const float starless = p.Starless ? StarlessDeep : 0.0f;

		set(Bed::Void, in * (1.0f - VoidGive * loudest) * (1.0f - 0.5f * starless) * VoidMax);
		set(Bed::Star, in * star * StarMax, StarPitchFar + (StarPitchNear - StarPitchFar) * star);
		set(Bed::Planet, in * planet * PlanetMax, p.GasGiant ? GiantPitch : RockyPitch);
		set(Bed::Nebula, in * nebula * NebulaMax);
		set(Bed::Port, in * port * PortMax);
		set(Bed::Belt, in * belt * BeltMax);
		set(Bed::Between, out * riding * (1.0f - BetweenDeepGive * deep) * BetweenMax);
		set(Bed::Deep, std::max(out * std::max(deep, 1.0f - riding), in * starless) * DeepMax);

		// Inside: the hull (or the suit) heard, outer space through it.
		float muffle = 1.0f, hull = 0.0f, suit = 0.0f;
		switch (p.Inside)
		{
		case Interior::Hull: hull = 1.0f; muffle = HullMuffle; break;
		case Interior::Cockpit: hull = CockpitHull; muffle = CockpitMuffle; break;
		case Interior::Station: hull = StationHull; muffle = HullMuffle; break;
		case Interior::Suit: suit = 1.0f; muffle = SuitMuffle; break;
		default: break;
		}
		for (int i = 0; i < BedCount; i++)
		{
			const Bed b = static_cast<Bed>(i);
			if (!Exterior(b)) continue;
			if (b == Bed::Port && p.Docked) continue; // moored: the port's machinery and clamps come through the hull itself
			m.Level[static_cast<size_t>(i)] *= muffle;
		}
		set(Bed::Hull, hull * HullMax, HullPitch(p.HullMassScale));
		set(Bed::Suit, suit * SuitMax);
		return m;
	}

	/// <paramref name="current"/> eased toward <paramref name="target"/> over <paramref name="dt"/> seconds, rising at
	/// <paramref name="upPerS"/> and falling at <paramref name="downPerS"/> (exponential: never overshoots).
	inline float ExpEase(float current, float target, float dt, float upPerS, float downPerS)
	{
		if (!(dt > 0.0f)) return current;
		const float rate = target > current ? upPerS : downPerS;
		return current + (target - current) * (1.0f - std::exp(-dt * rate));
	}

	/// The crossfade rates of a bed (rising, falling), per second.
	inline void RatesOf(Bed bed, float& upPerS, float& downPerS)
	{
		switch (bed)
		{
		case Bed::Hull:
		case Bed::Suit: upPerS = Tune::InsideUpPerS; downPerS = Tune::InsideDownPerS; return;
		case Bed::Between:
		case Bed::Deep: upPerS = Tune::StarsUpPerS; downPerS = Tune::StarsDownPerS; return;
		default: upPerS = Tune::PlaceUpPerS; downPerS = Tune::PlaceDownPerS; return;
		}
	}
}
