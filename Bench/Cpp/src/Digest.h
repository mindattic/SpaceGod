#pragma once
// Bench.Cpp: the canonical state digest (Bench/README.md, "Digest format"), a port of Bench/CSharp/src/Digest.cs. It
// must come out byte for byte the same as Bench.CSharp's for the same simulation state.

#include "Cs/Cs.h"

#include <cstdint>
#include <string>

namespace Bench
{
	class Simulation;

	struct Digest
	{
		static constexpr const char* Version = "spacegod-digest 1";

		/// <summary>A double as its IEEE-754 bits: 16 upper-case hex digits of BitConverter.DoubleToInt64Bits.</summary>
		static std::string H(double d);

		/// <summary>64-bit FNV-1a over the UTF-8 bytes of <paramref name="text"/>.</summary>
		static uint64_t Fnv1a64(const std::string& text);

		static std::string Hex(uint64_t h);

		static std::string Write(Simulation& sim, const std::string& scenario);

		/// <summary>
		/// Trace mode: one line for the digest <paramref name="text"/>: the FNV-1a of its head (everything before the first
		/// system) and of each system's block, so the first differing tick and system show between the two runners.
		/// </summary>
		static std::string TraceLine(int tick, const std::string& text);
	};
}
