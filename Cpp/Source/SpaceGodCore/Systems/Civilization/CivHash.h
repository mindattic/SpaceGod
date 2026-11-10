#pragma once
// US-T274: the fixed hashes the Civilization layer rolls every choice from (no random draws: every lockstep peer and every
// replay reaches the same galaxy). The same finaliser Society uses.

#include "Cs/Cs.h"

#include <cstdint>

namespace SpaceGod::Core::Civ
{
	inline std::uint32_t Mix(std::uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352du;
		x ^= x >> 15; x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	inline std::uint32_t H(std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0, std::uint32_t d = 0)
	{
		return Mix(a ^ Mix(b + 0x9e3779b9u) ^ Mix(c + 0x85ebca6bu) ^ Mix(d + 0xc2b2ae35u));
	}

	inline std::uint32_t DayOf(double jd) { return static_cast<std::uint32_t>(static_cast<long long>(cs::Math::Floor(jd))); }

	/// <summary>A share of 0..1 from a hash.</summary>
	inline double Unit(std::uint32_t h) { return (h % 100003u) / 100003.0; }

	/// <summary>Whether a hash comes up under <paramref name="chance"/> (0..1).</summary>
	inline bool Roll(std::uint32_t h, double chance) { return Unit(h) < chance; }

	template <class T> const T& Pick(const cs::List<T>& list, std::uint32_t h) { return list[static_cast<int>(h % static_cast<std::uint32_t>(list.Count()))]; }
	template <class T, std::size_t N> T Pick(const T (&arr)[N], std::uint32_t h) { return arr[h % N]; }

	inline std::uint32_t HashText(const cs::String& s)
	{
		std::uint32_t h = 2166136261u;
		for (char c : s) { h ^= static_cast<unsigned char>(c); h *= 16777619u; }
		return h;
	}
}
