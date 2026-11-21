// AmbientMixTests: unit checks of the space soundscape's mixing (US-T448, Cpp/Source/SpaceGod/Audio/AmbientMix.h): the
// void alone far from everything, each place bed coming in as its thing nears and gone past its reach, a gas giant heard
// farther out than a rocky world, between the stars the wider bed and deep space taking over far from any system, inside a
// hull the reactor's hum with outer space muffled (a docked port's clamps not), a suit's breathing, the reactor's note
// falling with heft, every level in range for any input, and the crossfade easing without overshoot.
//
//   AmbientMixTests.exe     exit code 0 = every check passed, 1 = failures (listed)
//
// Built only by tools/build-core.ps1 (SPACEGOD_STANDALONE); the UE module compiles it to nothing.

#ifdef SPACEGOD_STANDALONE

#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>

#include "../../../SpaceGod/Audio/AmbientMix.h"

using namespace SpaceGod::Audio::Ambient;

namespace
{
	int failures = 0, checks = 0;

	void Check(bool ok, const char* what)
	{
		checks++;
		if (ok) return;
		failures++;
		std::printf("FAIL  %s\n", what);
	}

	bool Silent(const Mix& m, Bed b) { return m[b] <= 1e-6f; }

	void NearShape()
	{
		Check(Near(1, 10, 100) == 1.0f, "Near: 1 inside the full distance");
		Check(Near(10, 10, 100) == 1.0f, "Near: 1 at the full distance");
		Check(Near(100, 10, 100) == 0.0f, "Near: 0 at the none distance");
		Check(Near(1e9, 10, 100) == 0.0f, "Near: 0 far past it");
		Check(Near(Far, 10, 100) == 0.0f, "Near: 0 at infinity");
		Check(Near(std::numeric_limits<double>::quiet_NaN(), 10, 100) == 0.0f, "Near: 0 for NaN");
		float last = 2;
		bool monotone = true, inRange = true;
		for (double d = 5; d < 200; d *= 1.05)
		{
			const float n = Near(d, 10, 100);
			if (n > last + 1e-6f) monotone = false;
			if (n < 0 || n > 1) inRange = false;
			last = n;
		}
		Check(monotone, "Near: never rises with distance");
		Check(inRange, "Near: always 0..1");
		Check(std::abs(Near(std::sqrt(1000.0), 10, 100) - 0.5f) < 1e-4f, "Near: half way in log distance is half");
	}

	void TheVoid()
	{
		const Mix m = MixFor(Place());
		Check(std::abs(m[Bed::Void] - Tune::VoidMax) < 1e-6f, "far from everything: the void at its full level");
		for (int i = 1; i < BedCount; i++) Check(Silent(m, static_cast<Bed>(i)), "far from everything: nothing but the void");
		Check(m.Loudest() == Bed::Void, "far from everything: the void loudest");
	}

	void NearAStar()
	{
		Place p;
		p.StarRadii = 5;
		Mix m = MixFor(p);
		Check(std::abs(m[Bed::Star] - Tune::StarMax) < 1e-6f, "skimming a star: its bed at full");
		Check(m[Bed::Star] > m[Bed::Void], "skimming a star: louder than the void");
		Check(m[Bed::Void] < Tune::VoidMax, "skimming a star: the void gives way");
		p.StarRadii = 215; // Earth's distance from the Sun
		m = MixFor(p);
		Check(m[Bed::Star] > 0 && m[Bed::Star] < 0.1f * Tune::StarMax, "at Earth's distance: only a breath of the Sun");
		p.StarRadii = 400;
		Check(Silent(MixFor(p), Bed::Star), "past the star's reach: none of it");
		Place a, b;
		a.StarRadii = 5;
		b.StarRadii = 100;
		Check(MixFor(a).PitchOf(Bed::Star) > MixFor(b).PitchOf(Bed::Star), "a star's rush rises as it nears");
	}

	void NearAPlanet()
	{
		Place rock, giant, moon;
		rock.PlanetRadii = giant.PlanetRadii = moon.PlanetRadii = 2;
		giant.GasGiant = true;
		moon.Moon = true;
		Check(MixFor(giant)[Bed::Planet] > MixFor(rock)[Bed::Planet], "close in: a gas giant louder than a rocky world");
		Check(MixFor(rock)[Bed::Planet] > MixFor(moon)[Bed::Planet], "close in: a rocky world louder than a moon");
		rock.PlanetRadii = giant.PlanetRadii = 40;
		Check(Silent(MixFor(rock), Bed::Planet), "40 radii out: a rocky world's magnetosphere gone");
		Check(!Silent(MixFor(giant), Bed::Planet), "40 radii out: a gas giant's still heard");
		Check(MixFor(giant).PitchOf(Bed::Planet) < MixFor(rock).PitchOf(Bed::Planet), "a gas giant's whistlers lower than a rocky world's");
	}

	void NebulaPortBelt()
	{
		Place p;
		p.Nebula = 1;
		Check(std::abs(MixFor(p)[Bed::Nebula] - Tune::NebulaMax) < 1e-6f, "deep in a nebula: its shimmer at full");
		p = Place();
		p.Nebula = 0.3;
		Check(std::abs(MixFor(p)[Bed::Nebula] - 0.3f * Tune::NebulaMax) < 1e-6f, "a nebula's edge: its shimmer by density");

		p = Place();
		p.PortKm = 100;
		Check(std::abs(MixFor(p)[Bed::Port] - Tune::PortMax) < 1e-6f, "beside a port: its bed at full");
		p.PortBusy = 0.4;
		Check(std::abs(MixFor(p)[Bed::Port] - 0.4f * Tune::PortMax) < 1e-6f, "beside a quiet relay: less of it");
		p = Place();
		p.PortKm = 50000;
		Check(Silent(MixFor(p), Bed::Port), "50,000 km from a port: none of it");
		p.Docked = true;
		Check(std::abs(MixFor(p)[Bed::Port] - Tune::PortMax) < 1e-6f, "docked: the port at full whatever the distance read");

		Check(BeltBand(2.7, 0) == 1.0f, "Sol's main belt: inside it at 2.7 AU");
		Check(BeltBand(1.0, 0) == 0.0f, "Sol's main belt: not at Earth");
		Check(BeltBand(5.2, 0) == 0.0f, "Sol's main belt: not at Jupiter");
		Check(BeltBand(2.7, 1.0) == 0.0f, "Sol's main belt: not an AU off the ecliptic");
		Check(BeltBand(2.0, 0) > 0 && BeltBand(2.0, 0) < 1, "Sol's main belt: soft at its inner edge");
		Check(FieldDepth(0.3) == 1.0f && FieldDepth(1.5) == 0.0f && FieldDepth(0.9) > 0 && FieldDepth(0.9) < 1, "an ice field: deep, out, edge");
		p = Place();
		p.Belt = 1;
		Check(std::abs(MixFor(p)[Bed::Belt] - Tune::BeltMax) < 1e-6f, "in the belt: its ticks at full");
	}

	void BetweenTheStars()
	{
		Place p;
		p.Outside = 1;
		p.Riding = 1;
		p.LyFromCharted = 0.5;
		p.StarRadii = 5; // stale: no system's beds are heard out here
		p.Nebula = 1;
		Mix m = MixFor(p);
		Check(std::abs(m[Bed::Between] - Tune::BetweenMax) < 1e-6f, "under way near the systems: the wider bed at full");
		Check(Silent(m, Bed::Deep), "under way near the systems: no deep space yet");
		Check(Silent(m, Bed::Void) && Silent(m, Bed::Star) && Silent(m, Bed::Nebula), "between the stars: no system's beds");

		p.LyFromCharted = 6;
		m = MixFor(p);
		Check(m[Bed::Deep] > m[Bed::Between], "6 ly from any system: deep space over the wider bed");
		Check(!Silent(m, Bed::Between), "6 ly out under way: the wider bed still under it");

		p.LyFromCharted = 0.5;
		p.Riding = 0; // a void stop
		m = MixFor(p);
		Check(Silent(m, Bed::Between) && std::abs(m[Bed::Deep] - Tune::DeepMax) < 1e-6f, "stopped out there: deep space alone");

		Place half;
		half.Outside = 0.5;
		half.StarRadii = 5;
		m = MixFor(half);
		Check(m[Bed::Star] > 0 && m[Bed::Between] > 0, "half way out: the star and the wider bed crossfading");

		Place starless;
		starless.Starless = true;
		m = MixFor(starless);
		Check(!Silent(m, Bed::Deep) && m[Bed::Void] < Tune::VoidMax, "a starless system: eerie deep space, less void");
	}

	void Inside()
	{
		Place open;
		open.StarRadii = 10;
		open.Nebula = 0.5;
		Place helm = open;
		helm.Inside = Interior::Hull;
		const Mix o = MixFor(open), h = MixFor(helm);
		Check(std::abs(h[Bed::Hull] - Tune::HullMax) < 1e-6f, "at a helm: the reactor's hum");
		Check(std::abs(h[Bed::Star] - o[Bed::Star] * Tune::HullMuffle) < 1e-6f, "at a helm: the star muffled through the hull");
		Check(std::abs(h[Bed::Nebula] - o[Bed::Nebula] * Tune::HullMuffle) < 1e-6f, "at a helm: the nebula muffled through the hull");
		Check(Silent(o, Bed::Hull) && Silent(o, Bed::Suit), "in the open: neither hull nor suit");

		Place docked;
		docked.Docked = true;
		docked.Inside = Interior::Hull;
		Check(std::abs(MixFor(docked)[Bed::Port] - Tune::PortMax) < 1e-6f, "docked at a helm: the clamps come through unmuffled");

		Place cockpit = open, station = open, suit = open;
		cockpit.Inside = Interior::Cockpit;
		station.Inside = Interior::Station;
		suit.Inside = Interior::Suit;
		Check(MixFor(cockpit)[Bed::Hull] < h[Bed::Hull] && MixFor(cockpit)[Bed::Hull] > 0, "a cockpit: less hull than a helm");
		Check(MixFor(station)[Bed::Hull] > 0, "a station's seat: its hull heard");
		const Mix s = MixFor(suit);
		Check(std::abs(s[Bed::Suit] - Tune::SuitMax) < 1e-6f && Silent(s, Bed::Hull), "in a suit: breathing, no hull");
		Check(s[Bed::Star] > h[Bed::Star] && s[Bed::Star] < o[Bed::Star], "in a suit: space less muffled than in a hull");

		Place light = helm, heavy = helm;
		light.HullMassScale = 0.1;
		heavy.HullMassScale = 1.1;
		Check(MixFor(light).PitchOf(Bed::Hull) > MixFor(heavy).PitchOf(Bed::Hull), "a capital's reactor lower than a fighter's");
		Check(HullPitch(50) >= Tune::HullPitchMin && HullPitch(-3) <= Tune::HullPitchMax, "the reactor's note kept in range");
	}

	void AlwaysInRange()
	{
		bool ok = true;
		const double radii[] = { 0, 0.5, 2, 10, 60, 300, Far, std::numeric_limits<double>::quiet_NaN() };
		const double shares[] = { -1, 0, 0.3, 1, 7, std::numeric_limits<double>::quiet_NaN() };
		for (double r : radii)
			for (double s : shares)
				for (int inside = 0; inside < 5; inside++)
				{
					Place p;
					p.StarRadii = r;
					p.PlanetRadii = r;
					p.GasGiant = inside % 2 == 0;
					p.PortKm = r * 1000;
					p.Nebula = s;
					p.Belt = s;
					p.Outside = s;
					p.Riding = s;
					p.LyFromCharted = r;
					p.Inside = static_cast<Interior>(inside);
					p.HullMassScale = s;
					const Mix m = MixFor(p);
					for (int i = 0; i < BedCount; i++)
					{
						const float l = m.Level[static_cast<size_t>(i)], pitch = m.Pitch[static_cast<size_t>(i)];
						if (!(l >= 0 && l <= 1) || !(pitch >= 0.5f && pitch <= 1.3f)) ok = false;
					}
				}
		Check(ok, "every bed's level 0..1 and pitch 0.5..1.3 for any input");
	}

	void Easing()
	{
		Check(ExpEase(0.2f, 0.8f, 0.0f, 1, 1) == 0.2f, "no time, no change");
		float v = 0;
		bool rising = true, under = true;
		for (int i = 0; i < 600; i++)
		{
			const float next = ExpEase(v, 0.4f, 1.0f / 60, Tune::PlaceUpPerS, Tune::PlaceDownPerS);
			if (next < v) rising = false;
			if (next > 0.4f + 1e-6f) under = false;
			v = next;
		}
		Check(rising && under, "fading in: always rising, never past the wanted level");
		Check(v > 0.39f, "fading in: there within ten seconds");
		float w = 0;
		w = ExpEase(w, 0.4f, 1.0f / 60, Tune::PlaceUpPerS, Tune::PlaceDownPerS);
		Check(w < 0.01f, "fading in: no jump in one frame (a smooth crossfade)");
		float d = 0.4f;
		for (int i = 0; i < 60; i++) d = ExpEase(d, 0, 1.0f / 60, Tune::PlaceUpPerS, Tune::PlaceDownPerS);
		Check(d > 0.1f && d < 0.4f, "fading out: a second in, part way down");
		float up, down;
		RatesOf(Bed::Hull, up, down);
		float placeUp, placeDown;
		RatesOf(Bed::Star, placeUp, placeDown);
		Check(up > placeUp, "stepping into a hull is heard sooner than flying near a star");
		Check(std::string_view(BedName(Bed::Between)) == "Between" && std::string_view(InteriorName(Interior::Suit)) == "Suit", "names");
	}
}

int main()
{
	NearShape();
	TheVoid();
	NearAStar();
	NearAPlanet();
	NebulaPortBelt();
	BetweenTheStars();
	Inside();
	AlwaysInRange();
	Easing();
	std::printf("AmbientMixTests: %d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

#endif
