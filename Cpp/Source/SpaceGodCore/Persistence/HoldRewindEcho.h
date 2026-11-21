#pragma once

// US-T427: the hold rewind's sound, played backward (docs/REWIND.md, "Hold to rewind"). What the game heard in the last
// seconds is kept with the moment it was heard on the effects' clock (the clock US-T421's reversed effects age by); while a
// hold plays the past back that clock runs backward, and each sound it passes is played again, reversed. On letting go the
// sounds after the moment played on from never were, and the rest move on with the clock. Pure bookkeeping, no audio: the
// game's CombatSounds owns the playing, and Tests/HoldRewind checks this.

#include <cstddef>
#include <deque>
#include <utility>

namespace SpaceGod::Core
{
	template <class T>
	class HoldRewindEcho
	{
	public:
		struct Heard
		{
			double At = 0;
			T What{};
		};

		/// <summary>A sound heard at <paramref name="at"/> (the effects' clock, rising). Only the last
		/// <paramref name="keepS"/> seconds are kept.</summary>
		void Record(double at, T what, double keepS)
		{
			heard.push_back(Heard { at, std::move(what) });
			while (!heard.empty() && heard.front().At < at - keepS) heard.pop_front();
		}

		/// <summary>A hold moved the clock back from <paramref name="from"/> to <paramref name="to"/> (to &lt;= from): each sound
		/// heard in (to, from] is handed to <paramref name="play"/>, newest first (the order the hold passes them), at most
		/// <paramref name="cap"/> of them. Returns how many.</summary>
		template <class F>
		int Crossed(double from, double to, F&& play, int cap) const
		{
			if (!(to < from)) return 0;
			int n = 0;
			for (auto it = heard.rbegin(); it != heard.rend() && n < cap; ++it)
			{
				if (it->At > from) continue;
				if (it->At <= to) break;
				play(it->What);
				n++;
			}
			return n;
		}

		/// <summary>The hold ended, play going on from <paramref name="resumeAt"/>: the sounds heard after it never were;
		/// the rest move on by <paramref name="shift"/> (now less the moment played on from), as the effects do.</summary>
		void Resumed(double resumeAt, double shift)
		{
			while (!heard.empty() && heard.back().At > resumeAt) heard.pop_back();
			for (auto& h : heard) h.At += shift;
		}

		void Clear() { heard.clear(); }
		std::size_t Count() const { return heard.size(); }
		const std::deque<Heard>& All() const { return heard; }

	private:
		std::deque<Heard> heard;
	};
}
