#pragma once

// HudDock (US-T438): the HUD drawers' layout math, pure (nothing of the engine or the GUI: std only), so it is unit-checked
// on its own (SpaceGodCore/Tests/HudDock/HudDockTests.cpp, built by tools/build-core.ps1). UI/HudDrawers places every
// drawer's window with it each GUI pass.
//
//   - Edges:      a window docked to an edge sits in that edge's band (the screen less what the HUD keeps for itself) and
//                 stacks along it with the others docked there, in their order: down the left edge from the top, up the
//                 right edge from the bottom (the right-hand column's habit), left to right along the bottom, centred along
//                 the top. A slot is `length` along the edge (0: fill what is left) by `thickness` across it.
//   - Splitting:  when what the slots want does not fit, each shrinks toward its least length in proportion to what it
//                 has over it; fill slots share whatever is left equally (Split).
//   - On screen:  a floating window is kept inside the screen at any resolution and UI scale (the GUI's screen is the
//                 window's pixels over the UI scale): no bigger than the screen less its margins, and moved back in whole
//                 (KeepOnScreen).
//   - Snapping:   a window dragged by its title docks to another window when the pointer is over that window's title
//                 strip (it joins its group), else to the screen edge the pointer is within reach of (at the order the
//                 pointer falls at among the slots there), else it floats where it was dropped (FindSnap).
//   - Edge tabs:  the closed drawers' pull-out tabs sit centred along their edge (EdgeTabs).
// GUI units throughout (screen pixels at UI scale 1), y down from the top as the IMGUI layer draws.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace SpaceGod::UI::HudDock
{
	enum class Edge : int { Left = 0, Top = 1, Right = 2, Bottom = 3 };
	inline constexpr int EdgeCount = 4;

	inline const char* EdgeName(Edge e)
	{
		switch (e)
		{
			case Edge::Left: return "left";
			case Edge::Top: return "top";
			case Edge::Right: return "right";
			default: return "bottom";
		}
	}

	inline bool EdgeFromName(const std::string& s, Edge& out)
	{
		for (int i = 0; i < EdgeCount; i++)
			if (s == EdgeName(static_cast<Edge>(i))) { out = static_cast<Edge>(i); return true; }
		return false;
	}

	/// Whether slots stack down the edge (left, right) rather than across it (top, bottom).
	inline bool Vertical(Edge e) { return e == Edge::Left || e == Edge::Right; }

	struct Box
	{
		float x = 0, y = 0, w = 0, h = 0;
		Box() = default;
		Box(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
		float Right() const { return x + w; }
		float Bottom() const { return y + h; }
		bool Empty() const { return w <= 0 || h <= 0; }
		bool Contains(float px, float py) const { return !Empty() && px >= x && px < x + w && py >= y && py < y + h; }
		bool operator==(const Box& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
	};

	/// One slot on an edge: `length` along it (0 or less: fill what is left), at least `least`, `thickness` across it.
	struct Slot
	{
		float length = 0, least = 24, thickness = 200;
	};

	/// The lengths a run of `available` gives slots that want `want` (0 or less: fill) and need at least `least`, `gap`
	/// between them. Fixed slots take what they want and fill slots share the rest, each at least its least; when that
	/// does not fit, each shrinks toward its least in proportion to what it has over it, and when even the leasts do not
	/// fit, all of them shrink alike.
	inline std::vector<float> Split(const std::vector<float>& want, const std::vector<float>& least, float available, float gap)
	{
		size_t n = want.size();
		std::vector<float> len(n, 0.0f);
		if (n == 0) return len;
		auto leastOf = [&](size_t i) { return i < least.size() ? std::max(0.0f, least[i]) : 0.0f; };
		float space = std::max(0.0f, available - gap * static_cast<float>(n - 1));
		float fixed = 0;
		int fills = 0;
		for (size_t i = 0; i < n; i++)
		{
			if (want[i] > 0) { len[i] = std::max(want[i], leastOf(i)); fixed += len[i]; }
			else fills++;
		}
		if (fills > 0)
		{
			float share = (space - fixed) / static_cast<float>(fills);
			for (size_t i = 0; i < n; i++)
				if (!(want[i] > 0)) len[i] = std::max(share, leastOf(i));
		}
		float total = 0;
		for (float l : len) total += l;
		if (total <= space + 0.001f) return len;
		float excess = total - space, over = 0;
		for (size_t i = 0; i < n; i++) over += std::max(0.0f, len[i] - leastOf(i));
		if (over >= excess && over > 0)
		{
			for (size_t i = 0; i < n; i++)
			{
				float o = std::max(0.0f, len[i] - leastOf(i));
				len[i] -= excess * o / over;
			}
			return len;
		}
		// Even the leasts overrun: every slot at its least, all scaled down alike to fit.
		float leasts = 0;
		for (size_t i = 0; i < n; i++) { len[i] = leastOf(i); leasts += len[i]; }
		if (leasts > 0)
			for (size_t i = 0; i < n; i++) len[i] *= space / leasts;
		return len;
	}

	/// The boxes of the slots docked on edge `e`, in their order, inside `band` (the edge's usable area): down the left edge
	/// from the band's top, up the right edge from its bottom, left to right along the bottom, centred along the top. A
	/// slot's thickness is cut to the band.
	inline std::vector<Box> StackEdge(Edge e, const Box& band, const std::vector<Slot>& slots, float gap)
	{
		std::vector<float> want, least;
		for (const auto& s : slots) { want.push_back(s.length); least.push_back(s.least); }
		bool vertical = Vertical(e);
		auto len = Split(want, least, vertical ? band.h : band.w, gap);
		std::vector<Box> out;
		float total = 0;
		for (float l : len) total += l;
		total += gap * static_cast<float>(len.empty() ? 0 : len.size() - 1);
		float at = e == Edge::Left ? band.y : e == Edge::Right ? band.Bottom()
			: e == Edge::Top ? band.x + std::max(0.0f, (band.w - total) * 0.5f) : band.x;
		for (size_t i = 0; i < slots.size(); i++)
		{
			float t = std::min(slots[i].thickness, vertical ? band.w : band.h);
			switch (e)
			{
				case Edge::Left: out.emplace_back(band.x, at, t, len[i]); at += len[i] + gap; break;
				case Edge::Right: at -= len[i]; out.emplace_back(band.Right() - t, at, t, len[i]); at -= gap; break;
				case Edge::Top: out.emplace_back(at, band.y, len[i], t); at += len[i] + gap; break;
				default: out.emplace_back(at, band.Bottom() - t, len[i], t); at += len[i] + gap; break;
			}
		}
		return out;
	}

	/// The order a window dropped at (px, py) takes among the slots already on edge `e` (their boxes, in order): how many of
	/// them lie before the pointer in the way the edge stacks.
	inline int InsertOrder(Edge e, const std::vector<Box>& slots, float px, float py)
	{
		int n = 0;
		for (const auto& b : slots)
		{
			float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
			bool before = e == Edge::Left ? cy < py : e == Edge::Right ? cy > py : cx < px;
			if (before) n++;
		}
		return n;
	}

	/// `r` kept on a sw x sh screen: no bigger than the screen less `margin` all round (no smaller than minW x minH while
	/// that fits), and moved back in whole.
	inline Box KeepOnScreen(Box r, float sw, float sh, float margin, float minW, float minH)
	{
		float maxW = std::max(1.0f, sw - 2 * margin), maxH = std::max(1.0f, sh - 2 * margin);
		r.w = std::clamp(r.w, std::min(minW, maxW), maxW);
		r.h = std::clamp(r.h, std::min(minH, maxH), maxH);
		r.x = std::clamp(r.x, margin, std::max(margin, sw - margin - r.w));
		r.y = std::clamp(r.y, margin, std::max(margin, sh - margin - r.h));
		return r;
	}

	/// The screen edge (px, py) is within `reach` of, the nearest when two are; false when none is.
	inline bool NearEdge(float px, float py, float sw, float sh, float reach, Edge& out)
	{
		float d[EdgeCount] = { px, py, sw - px, sh - py };
		int best = -1;
		for (int i = 0; i < EdgeCount; i++)
			if (d[i] <= reach && (best < 0 || d[i] < d[best])) best = i;
		if (best < 0) return false;
		out = static_cast<Edge>(best);
		return true;
	}

	/// Where a dragged window goes when let go.
	struct Snap
	{
		enum class Kind { Float, Edge, Group };
		Kind kind = Kind::Float;
		Edge edge = Edge::Left;
		/// Kind::Edge: its order among the slots on that edge.
		int order = 0;
		/// Kind::Group: the index (in `strips`) of the window it joins.
		int target = -1;
	};

	/// Over another window's title strip it joins that window (`strips`: each window's strip, an empty box for one that
	/// cannot take it, the dragged window's own among them); within `reach` of a screen edge it docks there, at the order
	/// the pointer falls at among `edgeSlots[edge]` (the boxes docked on each edge, the dragged window's left out); else it
	/// floats.
	inline Snap FindSnap(float px, float py, float sw, float sh, float reach, const std::vector<Box>& strips,
		const std::vector<std::vector<Box>>& edgeSlots)
	{
		Snap s;
		for (size_t i = 0; i < strips.size(); i++)
			if (strips[i].Contains(px, py))
			{
				s.kind = Snap::Kind::Group;
				s.target = static_cast<int>(i);
				return s;
			}
		Edge e;
		if (NearEdge(px, py, sw, sh, reach, e))
		{
			s.kind = Snap::Kind::Edge;
			s.edge = e;
			int k = static_cast<int>(e);
			s.order = k < static_cast<int>(edgeSlots.size()) ? InsertOrder(e, edgeSlots[k], px, py) : 0;
		}
		return s;
	}

	/// The pull-out tabs of edge `e`, `lengths` their text widths, `thickness` deep, centred along `band`: on the left and
	/// right edges each tab lies flat against the edge, one under the other; on the top and bottom, side by side.
	inline std::vector<Box> EdgeTabs(Edge e, const Box& band, const std::vector<float>& lengths, float thickness, float gap)
	{
		std::vector<Box> out;
		if (lengths.empty()) return out;
		bool vertical = Vertical(e);
		float run = 0;
		for (float l : lengths) run += vertical ? thickness : l;
		run += gap * static_cast<float>(lengths.size() - 1);
		float at = vertical ? band.y + (band.h - run) * 0.5f : band.x + (band.w - run) * 0.5f;
		for (float l : lengths)
		{
			switch (e)
			{
				case Edge::Left: out.emplace_back(band.x, at, l, thickness); at += thickness + gap; break;
				case Edge::Right: out.emplace_back(band.Right() - l, at, l, thickness); at += thickness + gap; break;
				case Edge::Top: out.emplace_back(at, band.y, l, thickness); at += l + gap; break;
				default: out.emplace_back(at, band.Bottom() - thickness, l, thickness); at += l + gap; break;
			}
		}
		return out;
	}

	/// `r`, docked on edge `e` of a sw x sh screen, slid out toward that edge by 1 - t of the way off it (t 0: just off the
	/// screen, 1: in place): how a drawer pulls out.
	inline Box Slide(Box r, Edge e, float t, float sw, float sh)
	{
		float k = 1 - std::clamp(t, 0.0f, 1.0f);
		switch (e)
		{
			case Edge::Left: r.x -= k * r.Right(); break;
			case Edge::Right: r.x += k * (sw - r.x); break;
			case Edge::Top: r.y -= k * r.Bottom(); break;
			default: r.y += k * (sh - r.y); break;
		}
		return r;
	}

	/// The edge nearest the middle of `r` (a floating window's home edge, where its tab goes when it is closed).
	inline Edge NearestEdge(const Box& r, float sw, float sh)
	{
		float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
		float d[EdgeCount] = { cx, cy, sw - cx, sh - cy };
		int best = 0;
		for (int i = 1; i < EdgeCount; i++)
			if (d[i] < d[best]) best = i;
		return static_cast<Edge>(best);
	}
}
