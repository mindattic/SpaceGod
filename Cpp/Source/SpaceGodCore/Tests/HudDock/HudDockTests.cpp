// HudDockTests: unit checks of the HUD drawers' layout math (US-T438, SpaceGod/UI/HudDock.h): splitting an edge between
// the drawers docked on it, each edge's stack, the order a dropped window takes, keeping a floating window on screen at
// any resolution and UI scale, snapping a dragged window to an edge or another window's title strip, the edge tabs, the
// pull-out slide and a floating window's home edge.
//
//   HudDockTests.exe     exit code 0 = every check passed, 1 = failures (listed)
//
// Built only by tools/build-core.ps1 (SPACEGOD_STANDALONE); the UE module compiles it to nothing.

#ifdef SPACEGOD_STANDALONE

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../../../SpaceGod/UI/HudDock.h"

using namespace SpaceGod::UI::HudDock;

namespace
{
	int failures = 0, checks = 0;

	void Check(bool ok, const std::string& what)
	{
		checks++;
		if (ok) return;
		failures++;
		std::printf("FAIL  %s\n", what.c_str());
	}

	bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

	std::string S(float v) { return std::to_string(v); }

	float Sum(const std::vector<float>& v)
	{
		float t = 0;
		for (float x : v) t += x;
		return t;
	}

	void SplitChecks()
	{
		// Fixed slots that fit take what they want.
		auto a = Split({ 100, 200 }, { 20, 20 }, 1000, 6);
		Check(a.size() == 2 && Near(a[0], 100) && Near(a[1], 200), "split: fixed slots that fit");
		// A fill slot takes what is left.
		auto b = Split({ 100, 0 }, { 20, 20 }, 1000, 6);
		Check(Near(b[1], 1000 - 6 - 100), "split: fill takes the rest (" + S(b[1]) + ")");
		// Two fills share it equally.
		auto c = Split({ 0, 0 }, { 20, 20 }, 406, 6);
		Check(Near(c[0], 200) && Near(c[1], 200), "split: two fills share");
		// Too much wanted: shrink toward the leasts in proportion to what each has over, filling the run exactly.
		auto d = Split({ 300, 500 }, { 100, 100 }, 606, 6);
		Check(Near(Sum(d), 600), "split: overrun fits the run (" + S(Sum(d)) + ")");
		Check(Near(d[0], 300 - 200 * (200.0f / 600)) && Near(d[1], 500 - 200 * (400.0f / 600)), "split: shrinks in proportion to the excess over the least");
		Check(d[0] >= 100 && d[1] >= 100, "split: never under the least while the leasts fit");
		// The leasts themselves overrun: all scaled alike.
		auto e = Split({ 300, 300 }, { 200, 100 }, 156, 6);
		Check(Near(Sum(e), 150) && Near(e[0], 100) && Near(e[1], 50), "split: leasts overrun, scaled alike");
		// A fill squeezed out still gets its least.
		auto f = Split({ 400, 0 }, { 50, 60 }, 406, 6);
		Check(Near(Sum(f), 400) && f[1] >= 59.9f, "split: a fill squeezed keeps its least (" + S(f[1]) + ")");
		Check(Split({}, {}, 100, 6).empty(), "split: nothing to split");
	}

	void StackChecks()
	{
		Box band(10, 70, 1900, 960); // a 1920 x 1080 screen less the top bar and the status line
		std::vector<Slot> slots = { { 300, 30, 232 }, { 200, 30, 260 } };
		auto left = StackEdge(Edge::Left, band, slots, 6);
		Check(left.size() == 2 && left[0] == Box(10, 70, 232, 300) && left[1] == Box(10, 376, 260, 200), "stack: left, down from the top");
		auto right = StackEdge(Edge::Right, band, slots, 6);
		Check(right[0] == Box(1910 - 232, 1030 - 300, 232, 300), "stack: right, first slot at the bottom");
		Check(right[1] == Box(1910 - 260, 1030 - 300 - 6 - 200, 260, 200), "stack: right, the next above it");
		// US-T440: the bottom and top edges stack one over another, each slot centred across the band.
		auto bottom = StackEdge(Edge::Bottom, band, { { 300, 30, 0 }, { 40, 30, 600 } }, 6);
		Check(bottom[0] == Box(10, 1030 - 300, 1900, 300), "stack: a bottom slot of no thickness spans the band's width");
		Check(bottom[1] == Box(10 + (1900 - 600) * 0.5f, 1030 - 300 - 6 - 40, 600, 40), "stack: bottom, the next over it, centred");
		auto top = StackEdge(Edge::Top, band, { { 52, 40, 640 }, { 100, 40, 760 } }, 6);
		Check(top[0] == Box(10 + (1900 - 640) * 0.5f, 70, 640, 52) && top[1] == Box(10 + (1900 - 760) * 0.5f, 70 + 52 + 6, 760, 100), "stack: top, down from the top, centred");
		// US-T440: a far slot on the right edge stacks down from the band's top, the others still up from its bottom.
		std::vector<Slot> two = { { 300, 30, 232 }, { 200, 30, 380, true } };
		auto column = StackEdge(Edge::Right, band, two, 6);
		Check(column[0] == Box(1910 - 232, 1030 - 300, 232, 300) && column[1] == Box(1910 - 380, 70, 380, 200), "stack: right, a far slot at the top");
		auto leftFar = StackEdge(Edge::Left, band, two, 6);
		Check(leftFar[1] == Box(10, 1030 - 200, 380, 200), "stack: left, a far slot at the bottom");
		// Thickness is cut to the band.
		auto thick = StackEdge(Edge::Left, Box(0, 0, 150, 500), { { 100, 30, 400 } }, 6);
		Check(thick[0].w == 150, "stack: thickness cut to the band");
		// Too many for the edge: they split it, filling it, none overlapping.
		std::vector<Slot> many(6, Slot{ 300, 40, 232 });
		auto crowd = StackEdge(Edge::Left, band, many, 6);
		Check(Near(crowd.back().Bottom(), band.Bottom()), "stack: a crowded edge is filled exactly (" + S(crowd.back().Bottom()) + ")");
		bool apart = true;
		for (size_t i = 1; i < crowd.size(); i++) apart = apart && crowd[i].y >= crowd[i - 1].Bottom() + 5.99f;
		Check(apart, "stack: a crowded edge's slots never overlap");
	}

	void OrderChecks()
	{
		Box band(10, 70, 1900, 960);
		std::vector<Slot> slots = { { 300, 30, 232 }, { 200, 30, 232 } };
		auto left = StackEdge(Edge::Left, band, slots, 6);
		Check(InsertOrder(Edge::Left, left, 20, 80) == 0, "order: left, above the first");
		Check(InsertOrder(Edge::Left, left, 20, 300) == 1, "order: left, between");
		Check(InsertOrder(Edge::Left, left, 20, 900) == 2, "order: left, after the last");
		auto right = StackEdge(Edge::Right, band, slots, 6);
		Check(InsertOrder(Edge::Right, right, 1900, 1020) == 0, "order: right, below the first (it stacks up)");
		Check(InsertOrder(Edge::Right, right, 1900, 100) == 2, "order: right, at the top is last");
		Check(InsertOrder(Edge::Bottom, {}, 500, 1000) == 0, "order: an empty edge");
		// US-T440: the bottom edge stacks up, the top edge down; a far stack the other way.
		auto bottom = StackEdge(Edge::Bottom, band, slots, 6);
		Check(InsertOrder(Edge::Bottom, bottom, 500, 1020) == 0 && InsertOrder(Edge::Bottom, bottom, 500, 100) == 2, "order: bottom, it stacks up");
		auto top = StackEdge(Edge::Top, band, slots, 6);
		Check(InsertOrder(Edge::Top, top, 500, 80) == 0 && InsertOrder(Edge::Top, top, 500, 1000) == 2, "order: top, it stacks down");
		Check(InsertOrder(Edge::Right, { Box(1500, 70, 400, 200) }, 1900, 400, true) == 1, "order: right's far stack, under its first");
	}

	void ScreenChecks()
	{
		// A window dragged half off the right edge comes back whole.
		auto a = KeepOnScreen(Box(1800, 100, 400, 300), 1920, 1080, 10, 160, 60);
		Check(a == Box(1510, 100, 400, 300), "on screen: back in from the right");
		auto b = KeepOnScreen(Box(-300, -50, 400, 300), 1920, 1080, 10, 160, 60);
		Check(b == Box(10, 10, 400, 300), "on screen: back in from the top left");
		// A smaller resolution: bigger than the screen, cut to it.
		auto c = KeepOnScreen(Box(100, 100, 2000, 900), 1280, 720, 10, 160, 60);
		Check(c == Box(10, 10, 1260, 700), "on screen: cut to a 1280 x 720 screen");
		// UI scale 2 on 1920 x 1080: the GUI's screen is 960 x 540.
		auto d = KeepOnScreen(Box(800, 500, 400, 300), 1920 / 2.0f, 1080 / 2.0f, 10, 160, 60);
		Check(d.Right() <= 950.01f && d.Bottom() <= 530.01f && d.x >= 10 && d.y >= 10, "on screen: at UI scale 2");
		// Too small to keep: the least is kept while it fits.
		auto e = KeepOnScreen(Box(100, 100, 20, 10), 1920, 1080, 10, 160, 60);
		Check(e.w == 160 && e.h == 60, "on screen: no smaller than the least");
		// A tiny screen: never bigger than it.
		auto f = KeepOnScreen(Box(0, 0, 400, 300), 120, 50, 10, 160, 60);
		Check(f.w <= 100.01f && f.h <= 30.01f && f.x >= 10 && f.y >= 10, "on screen: a tiny screen");
	}

	void SnapChecks()
	{
		Edge e;
		Check(NearEdge(5, 500, 1920, 1080, 24, e) && e == Edge::Left, "near: the left edge");
		Check(NearEdge(1910, 1075, 1920, 1080, 24, e) && e == Edge::Bottom, "near: the corner goes to the nearer edge");
		Check(!NearEdge(500, 500, 1920, 1080, 24, e), "near: none mid-screen");

		std::vector<Box> strips = { Box(10, 70, 232, 24), Box(), Box(600, 400, 300, 24) };
		std::vector<std::vector<Box>> slots(EdgeCount);
		slots[0] = { Box(10, 70, 232, 300) };
		auto g = FindSnap(700, 410, 1920, 1080, 24, strips, slots);
		Check(g.kind == Snap::Kind::Group && g.target == 2, "snap: onto another window's title strip");
		auto g2 = FindSnap(15, 80, 1920, 1080, 24, strips, slots);
		Check(g2.kind == Snap::Kind::Group && g2.target == 0, "snap: a title strip beats the edge under it");
		auto ed = FindSnap(4, 600, 1920, 1080, 24, strips, slots);
		Check(ed.kind == Snap::Kind::Edge && ed.edge == Edge::Left && ed.order == 1, "snap: the left edge, after the window there");
		auto fl = FindSnap(900, 600, 1920, 1080, 24, strips, slots);
		Check(fl.kind == Snap::Kind::Float, "snap: floats mid-screen");
		// US-T440: the top third of the right edge takes its far stack (the right-hand column's top), counted on its own.
		std::vector<std::vector<Box>> col(EdgeCount);
		std::vector<std::vector<bool>> colFar(EdgeCount);
		col[2] = { Box(1678, 730, 232, 300), Box(1530, 70, 380, 200) };
		colFar[2] = { false, true };
		auto hi = FindSnap(1916, 300, 1920, 1080, 24, { Box(), Box() }, col, colFar);
		Check(hi.kind == Snap::Kind::Edge && hi.edge == Edge::Right && hi.far && hi.order == 1, "snap: the right edge's top third, the far stack, under its window");
		auto lo = FindSnap(1916, 1000, 1920, 1080, 24, { Box(), Box() }, col, colFar);
		Check(lo.kind == Snap::Kind::Edge && !lo.far && lo.order == 0, "snap: the right edge's foot, the near stack, first");
		auto past = FindSnap(900, 2000, 1920, 1080, 24, { Box() }, slots);
		Check(past.kind == Snap::Kind::Edge && past.edge == Edge::Bottom, "snap: dragged past the bottom of the screen docks there");
	}

	void TabChecks()
	{
		Box band(0, 0, 1920, 1080);
		auto left = EdgeTabs(Edge::Left, band, { 100, 120 }, 22, 4);
		Check(left.size() == 2 && left[0].x == 0 && Near(left[0].y, (1080 - 48) * 0.5f) && left[1].y == left[0].Bottom() + 4 && left[1].w == 120,
			"tabs: left, flat against it, centred, one under the other");
		auto right = EdgeTabs(Edge::Right, band, { 100 }, 22, 4);
		Check(right[0].Right() == 1920 && Near(right[0].y, (1080 - 22) * 0.5f), "tabs: right, against the right edge");
		auto bottom = EdgeTabs(Edge::Bottom, band, { 100, 200 }, 22, 4);
		Check(Near(bottom[0].x, (1920 - 304) * 0.5f) && bottom[1].x == bottom[0].Right() + 4 && bottom[0].Bottom() == 1080, "tabs: bottom, side by side, centred");
		Check(EdgeTabs(Edge::Top, band, {}, 22, 4).empty(), "tabs: none");
	}

	void SlideChecks()
	{
		Box r(10, 70, 232, 300);
		Check(Slide(r, Edge::Left, 1, 1920, 1080) == r, "slide: in place at 1");
		Check(Near(Slide(r, Edge::Left, 0, 1920, 1080).Right(), 0), "slide: just off the left at 0");
		Box b(10, 700, 1900, 300);
		Check(Near(Slide(b, Edge::Bottom, 0, 1920, 1080).y, 1080), "slide: just off the bottom at 0");
		Check(Near(Slide(b, Edge::Bottom, 0.5f, 1920, 1080).y, 700 + 0.5f * 380), "slide: half way");
		Box rr(1678, 700, 232, 300);
		Check(Near(Slide(rr, Edge::Right, 0, 1920, 1080).x, 1920), "slide: just off the right at 0");
		Check(NearestEdge(Box(1700, 500, 100, 100), 1920, 1080) == Edge::Right, "home edge: right");
		Check(NearestEdge(Box(900, 950, 100, 100), 1920, 1080) == Edge::Bottom, "home edge: bottom");
		Edge back;
		bool names = true;
		for (int i = 0; i < EdgeCount; i++) names = names && EdgeFromName(EdgeName(static_cast<Edge>(i)), back) && back == static_cast<Edge>(i);
		Check(names && !EdgeFromName("middle", back), "edge names round trip, nonsense refused");
	}
}

int main()
{
	SplitChecks();
	StackChecks();
	OrderChecks();
	ScreenChecks();
	SnapChecks();
	TabChecks();
	SlideChecks();
	std::printf("HudDockTests: %d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

#endif
