#pragma once
// Bench.Rewind (US-T99, phase 1: measurement only): the pieces the rewind probe (RewindProbe.inl, `--rewind-probe`)
// needs. Compiled only with /DBENCH_REWIND (tools/build-bench-cpp.ps1 -Rewind builds Bench.Rewind.exe); the plain
// Bench.Cpp.exe is unchanged by it.
//
// A keyframe is the game's own save graph (SaveData: clock, galaxy, session) written by ObjectGraph, wrapped in a tiny
// root that also holds the Bench app shell's handles (the followed ship, the skirmish, the crew course), so a restore
// gets every reference back without any id mapping. The wrapper adds a few dozen bytes to the SaveData graph.

#include "Cs/Cs.h"

#include <cstdint>
#include <string>
#include <vector>

namespace SpaceGod::Core
{
	class SaveData;
	class Ship;
	class StarSystem;
	class Battle;
}

namespace Bench
{
	class Simulation;

	class RewindKeyframe final
	{
	public:
		cs::Ref<SpaceGod::Core::SaveData> Data;
		cs::Ref<SpaceGod::Core::Ship> Followed;
		cs::Ref<SpaceGod::Core::Ship> CourseShip;
		cs::Ref<SpaceGod::Core::StarSystem> CourseDestination;
		cs::Ref<SpaceGod::Core::Battle> Skirmish;
		cs::Ref<SpaceGod::Core::Battle> BattleTimeFor;
		int BattlesStarted = 0;
		bool CourseArrived = false;
		bool CourseEnded = false;
	};

	namespace Rewind
	{
		using Bytes = std::vector<uint8_t>;

		/// <summary>The live simulation as one keyframe: the ObjectGraph bytes, uncompressed.</summary>
		Bytes Capture(Simulation& sim);

		/// <summary>A fresh Simulation from a keyframe's ObjectGraph bytes (graph read, Core rewired, Bench shell rebuilt).</summary>
		cs::Ref<Simulation> Restore(const Bytes& raw);

		/// <summary>The game's real save file (SaveGame: header + raw DEFLATE level 1) of the same state, for reference.</summary>
		Bytes SaveFile(Simulation& sim);

		// ---- codecs. Name() is what the report prints.
		enum class Codec { Deflate1, KrakenFast, KrakenNormal, MermaidFast };
		const char* Name(Codec c);
		bool Available(Codec c);

		/// <summary>Compresses <paramref name="cur"/> alone, or with <paramref name="dict"/> (the previous keyframe's raw
		/// bytes) as a preset dictionary placed before it (Oodle's dictionaryBase; DEFLATE has only a 32 KB window, so
		/// it is never given one).</summary>
		Bytes Compress(Codec c, const Bytes& cur, const Bytes* dict = nullptr);

		/// <summary>The inverse; <paramref name="rawLen"/> is the original size.</summary>
		Bytes Decompress(Codec c, const Bytes& comp, size_t rawLen, const Bytes* dict = nullptr);

		/// <summary>Bytes at the same offset that differ, over the common length (a naive in-place binary diff).</summary>
		size_t SameOffsetDiffBytes(const Bytes& a, const Bytes& b);

		/// <summary>Diagnostics: parses two keyframes and lists the first <paramref name="max"/> entries whose contents
		/// differ (entry id, field, both values, and a path of referrers back towards the root).</summary>
		std::string GraphDiff(const Bytes& a, const Bytes& b, int max);
	}
}
