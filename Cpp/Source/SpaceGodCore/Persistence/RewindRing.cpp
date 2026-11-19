// US-T99 (docs/REWIND.md, phase 2): the in-game keyframe ring. See RewindRing.h.

#include "Persistence/RewindRing.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "Persistence/ObjectGraph.h"
#include "Persistence/SaveGame.h"

namespace SpaceGod::Core
{
	namespace
	{
		constexpr double Minute = 60, Hour = 3600, Day = 86400;
	}

	const std::vector<RewindRing::Tier>& RewindRing::Schedule()
	{
		static const std::vector<Tier> tiers = {
			{ Hour, Minute },
			{ Day, 10 * Minute },
			{ 7 * Day, Hour },
			{ 90 * Day, Day },
		};
		return tiers;
	}

	double RewindRing::SpacingFor(double ageS, bool battle)
	{
		if (battle && ageS <= BattleTier.AgeS) return BattleTier.SpacingS;
		for (const Tier& t : Schedule())
			if (ageS <= t.AgeS) return t.SpacingS;
		return 7 * Day;
	}

	std::vector<uint8_t> RewindRing::Pack(const std::vector<uint8_t>& graph) { return SaveGame::Deflate(graph); }

	const RewindKeyframe* RewindRing::Newest() const
	{
		if (head >= 0)
			if (const RewindKeyframe* h = Find(head)) return h;
		return frames.empty() ? nullptr : frames.back().get();
	}

	bool RewindRing::Due(double sessionS, double wallS, bool battle) const
	{
		const RewindKeyframe* last = Newest();
		if (last == nullptr) return true;
		double wallSince = head >= 0 ? wallS - std::max(last->WallS, headWallS) : wallS - last->WallS;
		if (wallSince < MinWallGapS) return false;
		return sessionS - last->SessionS >= SpacingFor(0, battle);
	}

	const RewindKeyframe& RewindRing::Capture(const cs::Ref<SaveData>& data, double sessionS, double jd, double wallS, bool battle)
	{
		// One timeline (the owner's call, 2026-11-05): play going on after a rewind replaces the future that was ahead.
		if (head >= 0)
		{
			if (const RewindKeyframe* h = Find(head))
			{
				double at = h->SessionS;
				frames.erase(std::remove_if(frames.begin(), frames.end(), [at](const std::shared_ptr<RewindKeyframe>& k) { return k->SessionS > at && !k->Pinned; }), frames.end());
			}
			head = -1;
		}
		// Never two keyframes at one moment (a capture right after a rewind, before the clock moved).
		frames.erase(std::remove_if(frames.begin(), frames.end(), [sessionS](const std::shared_ptr<RewindKeyframe>& k) { return k->SessionS >= sessionS && !k->Pinned; }), frames.end());

		auto k = std::make_shared<RewindKeyframe>();
		k->Id = nextId++;
		k->SessionS = sessionS;
		k->Jd = jd;
		k->WallS = wallS;
		k->Battle = battle;
		k->Pinned = frames.empty(); // the session start is always kept, whole
		auto started = std::chrono::steady_clock::now();
		cs::List<uint8_t> graph = ObjectGraph::ToBytes(data);
		k->CaptureMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		k->RawBytes = graph.Raw().size();
		// The walk has to happen here, on the simulation thread; the compression does not.
		k->packed = std::async(std::launch::async, [raw = std::move(graph.Raw())]() { return Pack(raw); }).share();
		frames.push_back(k);
		Relink();
		Thin(sessionS);
		return *k;
	}

	const RewindKeyframe* RewindRing::Before(double sessionS) const
	{
		for (auto it = frames.rbegin(); it != frames.rend(); ++it)
			if ((*it)->SessionS < sessionS) return it->get();
		return nullptr;
	}

	const RewindKeyframe* RewindRing::Back(double sessionS, double backS) const
	{
		for (auto it = frames.rbegin(); it != frames.rend(); ++it)
			if ((*it)->SessionS <= sessionS - backS) return it->get();
		return frames.empty() ? nullptr : frames.front().get();
	}

	const RewindKeyframe* RewindRing::Find(int id) const
	{
		for (const auto& k : frames)
			if (k->Id == id) return k.get();
		return nullptr;
	}

	cs::Ref<SaveData> RewindRing::Restore(int id, double wallS)
	{
		const RewindKeyframe* k = Find(id);
		if (k == nullptr) return nullptr;
		const std::vector<uint8_t>& packed = k->Packed();
		std::vector<uint8_t> graph = SaveGame::Inflate(packed.data(), packed.size());
		cs::Ref<SaveData> data = ObjectGraph::Read(graph.data(), graph.size()).As<SaveData>();
		if (data == nullptr) return nullptr;
		head = id;
		headWallS = wallS;
		return data;
	}

	void RewindRing::Clear()
	{
		frames.clear();
		head = -1;
		headWallS = 0;
	}

	size_t RewindRing::Bytes() const
	{
		size_t total = 0;
		for (const auto& k : frames) total += k->StoredBytes();
		return total;
	}

	bool RewindRing::ThinPass(double nowSessionS, double scale)
	{
		if (frames.size() <= 2) return false;
		// Oldest first, so what was kept further back does not churn as new keyframes arrive: a keyframe stays while it is
		// at least its age's spacing after the last one kept. The start, the newest, pinned keyframes and the head stay.
		std::vector<std::shared_ptr<RewindKeyframe>> kept;
		kept.reserve(frames.size());
		kept.push_back(frames.front());
		for (size_t i = 1; i + 1 < frames.size(); i++)
		{
			const auto& k = frames[i];
			double spacing = SpacingFor(nowSessionS - k->SessionS, k->Battle) * scale;
			if (k->Pinned || k->Id == head || k->SessionS - kept.back()->SessionS >= spacing * 0.999) kept.push_back(k);
		}
		kept.push_back(frames.back());
		bool dropped = kept.size() != frames.size();
		frames = std::move(kept);
		return dropped;
	}

	void RewindRing::Thin(double nowSessionS)
	{
		ThinPass(nowSessionS, 1);
		// Over budget: thin everything further (never cut off: the start stays, so the horizon is always the whole session).
		for (double scale = 2; Bytes() > BudgetBytes && scale <= 1 << 20; scale *= 2)
			if (!ThinPass(nowSessionS, scale) && frames.size() <= 2) break;
		Relink();
	}

	void RewindRing::Relink()
	{
		for (size_t i = 0; i < frames.size(); i++) frames[i]->Parent = i == 0 ? -1 : frames[i - 1]->Id;
	}
}
