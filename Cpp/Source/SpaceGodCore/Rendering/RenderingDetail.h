#pragma once
// Not a C# file (US-T22): small .NET behaviours the Rendering port needs that Cs/Cs.h does not have. If Cs.h grows
// these, point the users at it and delete this header.

#include "Cs/Cs.h"

#include <bit>
#include <cstdint>

namespace SpaceGod::Core::RenderingDetail
{
	/// <summary>C# double.Equals(double): NaN equals NaN, and 0 equals -0 (unlike ==, which has NaN != NaN).</summary>
	inline bool DoubleEquals(double a, double b) { return a == b || (cs::Double::IsNaN(a) && cs::Double::IsNaN(b)); }

	/// <summary>.NET Core's double.GetHashCode: the bits folded to an int, with every NaN and both zeros hashing alike.</summary>
	inline int DoubleHash(double v)
	{
		int64_t bits = std::bit_cast<int64_t>(v);
		if (((static_cast<uint64_t>(bits) - 1) & 0x7FFFFFFFFFFFFFFFull) >= 0x7FF0000000000000ull) bits &= 0x7FF0000000000000ll;
		return static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(bits))) ^ static_cast<int>(static_cast<uint32_t>(static_cast<uint64_t>(bits) >> 32));
	}
}
