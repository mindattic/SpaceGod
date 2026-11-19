#pragma once
// US-T99 (docs/REWIND.md, phase 2): the in-game keyframe ring behind time reversal. A keyframe is the world in the save
// format (the ObjectGraph of a SaveData, as a save file holds it), captured on the simulation thread and compressed on a
// worker; the ring keeps them thinned by game age under a fixed memory budget (dense recently, sparse further back, the
// session start always kept, never cut off) and hands one back as a SaveData to load in place.
//
// What this phase does not do yet (docs/REWIND.md, "Build phases"): no intervention log, so a rewind lands exactly on a
// keyframe instead of replaying to any tick; whole DEFLATE keyframes (the save file's codec) instead of Oodle deltas;
// no disk spill; one timeline. The storage is already branch-friendly: every keyframe carries a branch id (0) and a
// parent link to the retained keyframe before it, and is keyed by session game time, not by date.
//
// Taking a keyframe only reads the world (the same walk a save makes), so it never changes the simulation.

#include "Cs/Cs.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <vector>

namespace SpaceGod::Core
{
	class SaveData;

	/// <summary>One retained moment of the session.</summary>
	struct RewindKeyframe
	{
		int Id = 0;
		/// <summary>Always 0 until split timelines (US-T104); kept so the store needs no rewrite to grow a tree.</summary>
		int Branch = 0;
		/// <summary>The retained keyframe before this one on its line (-1 for the session start).</summary>
		int Parent = -1;
		/// <summary>Game seconds since the session (or the loaded save) began, along the timeline: the ring's key.</summary>
		double SessionS = 0;
		/// <summary>The clock's Julian date, for showing.</summary>
		double Jd = 0;
		/// <summary>Real seconds since the app started when it was taken.</summary>
		double WallS = 0;
		/// <summary>Taken while a battle ran in the watched system or a craft was held: the dense tier applies.</summary>
		bool Battle = false;
		/// <summary>Never thinned (the session start, a checkpoint the player marked).</summary>
		bool Pinned = false;
		size_t RawBytes = 0;
		double CaptureMs = 0;

		/// <summary>The bytes held: the compressed size once the worker is done, the raw size until then.</summary>
		size_t StoredBytes() const
		{
			if (packedSize == 0 && packed.valid() && packed.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
				packedSize = packed.get().size();
			return packedSize != 0 ? packedSize : RawBytes;
		}

		/// <summary>The compressed graph (waits for the worker if it is still at it).</summary>
		const std::vector<uint8_t>& Packed() const { return packed.get(); }

		std::shared_future<std::vector<uint8_t>> packed;
		mutable size_t packedSize = 0;
	};

	/// <summary>
	/// The keyframes of one session, oldest first. Not thread-safe: called from the simulation thread only (the worker
	/// only compresses bytes it was handed).
	/// </summary>
	class SPACEGODCORE_API RewindRing final
	{
	public:
		/// <summary>One step of the thinning schedule: keyframes up to <c>AgeS</c> old are kept <c>SpacingS</c> apart.</summary>
		struct Tier
		{
			double AgeS;
			double SpacingS;
		};

		/// <summary>The schedule by game age (docs/REWIND.md, Recommendation): 1 min for the last hour, 10 min for a day,
		/// 1 h for a week, 1 day for 90 days, then 1 week. The battle tier (10 s for 15 min) is <see cref="BattleTier"/>.</summary>
		static const std::vector<Tier>& Schedule();
		static constexpr Tier BattleTier = { 15 * 60.0, 10.0 };

		/// <summary>The memory the ring may hold (docs/REWIND.md: 512 MB of RAM).</summary>
		size_t BudgetBytes = size_t(512) * 1024 * 1024;

		/// <summary>Fewest real seconds between two keyframes. A capture is a synchronous walk of the world (110-490 ms,
		/// docs/REWIND.md), so until capture moves off the simulation thread this keeps the hitch rare.</summary>
		double MinWallGapS = 20;

		/// <summary>A keyframe is wanted now: the ring is empty, or enough game time and real time have passed since the
		/// newest one on the current line.</summary>
		bool Due(double sessionS, double wallS, bool battle) const;

		/// <summary>
		/// Takes a keyframe of <paramref name="data"/> at <paramref name="sessionS"/>: walks the world here, compresses on
		/// a worker, drops the abandoned future after a rewind (one timeline: what was ahead of the moment rewound to is
		/// replaced), then thins. The first keyframe of a ring is the session start and is pinned.
		/// </summary>
		const RewindKeyframe& Capture(const cs::Ref<SaveData>& data, double sessionS, double jd, double wallS, bool battle);

		/// <summary>The newest keyframe strictly before <paramref name="sessionS"/>, or null.</summary>
		const RewindKeyframe* Before(double sessionS) const;

		/// <summary>The newest keyframe at least <paramref name="backS"/> game seconds before <paramref name="sessionS"/>,
		/// else the oldest (the session start), or null when the ring is empty.</summary>
		const RewindKeyframe* Back(double sessionS, double backS) const;

		/// <summary>The keyframe with this id, or null.</summary>
		const RewindKeyframe* Find(int id) const;

		/// <summary>
		/// Rebuilds the world of keyframe <paramref name="id"/> (a fresh graph: the live one is not touched) and makes it the
		/// head of the timeline: keyframes after it stay until the next capture, so stepping back further (or forward
		/// again) still works until play goes on.
		/// </summary>
		cs::Ref<SaveData> Restore(int id, double wallS);

		/// <summary>The keyframe last restored (-1: none since the last capture).</summary>
		int Head() const { return head; }
		double HeadWallS() const { return headWallS; }

		/// <summary>Drops everything (a save was loaded or a new game began: a new timeline starts).</summary>
		void Clear();

		/// <summary>Keeps keyframes spaced by the schedule for their age and, while over budget, widens every spacing.</summary>
		void Thin(double nowSessionS);

		const std::vector<std::shared_ptr<RewindKeyframe>>& Keyframes() const { return frames; }
		size_t Bytes() const;

		/// <summary>The spacing the schedule wants for a keyframe this old.</summary>
		static double SpacingFor(double ageS, bool battle);

		/// <summary>A world's object graph compressed as a save file compresses it (what a keyframe holds).</summary>
		static std::vector<uint8_t> Pack(const std::vector<uint8_t>& graph);

	private:
		std::vector<std::shared_ptr<RewindKeyframe>> frames;
		int nextId = 0;
		int head = -1;
		double headWallS = 0;

		/// <summary>The newest keyframe on the current line: the head after a rewind, else the newest taken.</summary>
		const RewindKeyframe* Newest() const;

		/// <summary>One thinning pass with every spacing multiplied by <paramref name="scale"/>; true when it dropped any.</summary>
		bool ThinPass(double nowSessionS, double scale);

		void Relink();
	};
}
