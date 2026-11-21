// HudDrawers (US-T438): the HUD's drawers and their windows (see UI/HudDrawers.h for the rule, UI/HudDock.h for the layout
// math, docs/HUD.md for the player's view).

#include "UI/HudDrawers.h"

#include "UI/Icons.h"
#include "UI/UiMotion.h"
#include "UI/UiStyle.h"

#include "Shared/Json/JsonObject.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

using SpaceGod::Shared::Json::JsonArray;
using SpaceGod::Shared::Json::JsonObject;
using SpaceGod::Shared::Json::JsonValue;

namespace SpaceGod::UI
{
	namespace
	{
		using HudDock::Box;
		using HudDock::Edge;

		Box ToBox(const Rect& r) { return Box(r.x, r.y, r.width, r.height); }
		Rect ToRect(const Box& b) { return Rect(b.x, b.y, b.w, b.h); }

		float TextW(const cs::Ref<GUIStyle>& s, const cs::String& t)
		{
			return s ? s->CalcSize(GUIContent(t)).x : 7.0f * static_cast<float>(t.size());
		}

		/// `t` cut to `maxW` in `s`, an ellipsis ending it (whole when it fits).
		cs::String Fit(const cs::String& t, const cs::Ref<GUIStyle>& s, float maxW)
		{
			if (TextW(s, t) <= maxW) return t;
			cs::String cut = t;
			while (!cut.empty())
			{
				cut.pop_back();
				while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) cut.pop_back(); // whole UTF-8 characters
				if (TextW(s, cut + "\xE2\x80\xA6") <= maxW) return cut + "\xE2\x80\xA6";
			}
			return cs::String();
		}

		float Eased(float t)
		{
			t = std::clamp(t, 0.0f, 1.0f);
			return 1 - (1 - t) * (1 - t);
		}

		/// A drawer's alert as a colour: amber for a notice, a red pulse for something urgent.
		Color AlertColor(int level)
		{
			if (level >= 2)
			{
				float pulse = 0.6f + 0.4f * Mathf::Abs(Mathf::Sin(Time::unscaledTime * 5.0f));
				return Color(UiStyle::Danger.r, UiStyle::Danger.g, UiStyle::Danger.b, pulse);
			}
			return UiStyle::Warning;
		}

		void Fill(const Rect& r, Color c)
		{
			if (Event::current().type != EventType::Repaint) return;
			auto old = GUI::color;
			GUI::color = Color(old.r * c.r, old.g * c.g, old.b * c.b, old.a * c.a);
			GUI::DrawTexture(r, Icons::White());
			GUI::color = old;
		}

		void Outline(const Rect& r, Color c, float t)
		{
			Fill(Rect(r.x, r.y, r.width, t), c);
			Fill(Rect(r.x, r.yMax() - t, r.width, t), c);
			Fill(Rect(r.x, r.y, t, r.height), c);
			Fill(Rect(r.xMax() - t, r.y, t, r.height), c);
		}

		bool LayoutOrRepaint()
		{
			auto t = Event::current().type;
			return t == EventType::Layout || t == EventType::Repaint;
		}
	}

	// ---------------------------------------------------------------- the registry

	int HudDrawers::SpecIndex(const cs::String& id) const
	{
		for (size_t i = 0; i < specs.size(); i++)
			if (specs[i].id == id) return static_cast<int>(i);
		return -1;
	}

	bool HudDrawers::Has(const cs::String& id) const { return SpecIndex(id) >= 0; }

	const DrawerSpec* HudDrawers::Spec(const cs::String& id) const
	{
		int i = SpecIndex(id);
		return i < 0 ? nullptr : &specs[i];
	}

	bool HudDrawers::SpecAvailable(const cs::String& id) const
	{
		auto s = Spec(id);
		return s != nullptr && (!s->available || s->available());
	}

	bool HudDrawers::Available(const cs::String& id) const
	{
		if (!SpecAvailable(id)) return false;
		// US-T440: a fit drawer that asked for nothing the last time it drew has nothing to show.
		auto e = empties.find(id);
		return e == empties.end() || !e->second;
	}

	bool HudDrawers::HasAlways(const DrawerWindow& w) const
	{
		for (const auto& m : w.members)
			if (auto s = Spec(m); s != nullptr && s->always) return true;
		return false;
	}

	void HudDrawers::Want(const cs::String& id, Vector2 size)
	{
		wants[id] = size;
		empties[id] = false;
		if (id == drawing) asked = true;
	}

	void HudDrawers::DrawContent(const DrawerSpec& s, Rect r)
	{
		if (!s.draw) return;
		if (!s.fit)
		{
			s.draw(r);
			return;
		}
		cs::String was = drawing;
		bool wasAsked = asked;
		drawing = s.id;
		asked = false;
		s.draw(r);
		// Asked for nothing: the panel had nothing to show (it is asked again, off the screen, on every Layout pass).
		if (!asked) empties[s.id] = true;
		drawing = was;
		asked = wasAsked;
	}

	bool HudDrawers::FitSize(const DrawerWindow& w, bool bare, Vector2& size) const
	{
		if (!w.fitted || w.members.empty()) return false;
		const auto& id = w.members[std::clamp(w.active, 0, static_cast<int>(w.members.size()) - 1)];
		auto s = Spec(id);
		if (s == nullptr || !s->fit) return false;
		auto it = wants.find(id);
		if (it == wants.end()) return false;
		float side = bare ? 0.0f : Pad, top = bare ? 0.0f : TitleH, foot = bare ? 0.0f : Pad;
		size = Vector2(it->second.x + 2 * side, it->second.y + top + foot);
		return true;
	}

	float HudDrawers::Room(const cs::String& id) const
	{
		int i = WindowOf(id);
		if (i < 0) return haveLast ? last.screenH : 1080.0f;
		return windows[i].room;
	}

	HudDock::Box HudDrawers::LastEdgeExtent(HudDock::Edge edge) const
	{
		Box out;
		bool any = false;
		for (const auto& w : windows)
		{
			if (!w.drawn || !w.docked || w.edge != edge) continue;
			Box b = ToBox(w.rect);
			if (!any) { out = b; any = true; continue; }
			float x0 = std::min(out.x, b.x), y0 = std::min(out.y, b.y);
			float x1 = std::max(out.Right(), b.Right()), y1 = std::max(out.Bottom(), b.Bottom());
			out = Box(x0, y0, x1 - x0, y1 - y0);
		}
		return out;
	}

	DrawerAlert HudDrawers::AlertOf(const cs::String& id) const
	{
		auto s = Spec(id);
		if (s == nullptr || !s->alert || !Available(id)) return DrawerAlert();
		return s->alert();
	}

	DrawerWindow HudDrawers::DefaultWindow(const DrawerSpec& s) const
	{
		DrawerWindow w;
		w.members = { s.id };
		w.shown = s.shown;
		w.pinned = s.pinned;
		w.docked = true;
		w.edge = s.edge;
		w.size = s.size;
		w.far = s.far;
		w.floating = Rect(240, 160, s.size.x > 0 ? s.size.x : 480, s.size.y > 0 ? s.size.y : 320);
		// US-T440: a modal-like drawer starts floating, centred across the screen (kept so until it is moved).
		if (s.floatY >= 0)
		{
			w.docked = false;
			w.centred = true;
			w.floating.y = s.floatY;
		}
		int n = 0;
		for (const auto& o : windows)
			if (o.docked && o.edge == s.edge && o.far == s.far) n++;
		w.order = n;
		return w;
	}

	void HudDrawers::Register(DrawerSpec spec)
	{
		if (spec.id.empty() || Has(spec.id)) return;
		specs.push_back(std::move(spec));
		if (!laidOut) return; // placed by the ResetLayout (or Load) to come
		const auto& s = specs.back();
		if (!s.group.empty())
			for (auto& w : windows)
				for (const auto& m : w.members)
				{
					auto o = Spec(m);
					if (o != nullptr && o->group == s.group)
					{
						w.members.push_back(s.id);
						return;
					}
				}
		windows.push_back(DefaultWindow(s));
	}

	void HudDrawers::ResetLayout()
	{
		windows.clear();
		laidOut = true;
		std::unordered_map<cs::String, size_t> groups;
		for (const auto& s : specs)
		{
			if (!s.group.empty())
			{
				auto it = groups.find(s.group);
				if (it != groups.end())
				{
					windows[it->second].members.push_back(s.id);
					continue;
				}
				groups[s.group] = windows.size();
			}
			windows.push_back(DefaultWindow(s));
		}
		drag = Drag();
		previewing = false;
	}

	// ---------------------------------------------------------------- driving it

	int HudDrawers::WindowOf(const cs::String& id) const
	{
		for (size_t i = 0; i < windows.size(); i++)
			for (const auto& m : windows[i].members)
				if (m == id) return static_cast<int>(i);
		return -1;
	}

	std::vector<int> HudDrawers::AvailableMembers(const DrawerWindow& w) const
	{
		std::vector<int> out;
		for (size_t k = 0; k < w.members.size(); k++)
			if (Available(w.members[k])) out.push_back(static_cast<int>(k));
		return out;
	}

	bool HudDrawers::VisibleNow(const DrawerWindow& w, bool hudMode, bool bareFrame) const
	{
		auto avail = AvailableMembers(w);
		if (avail.empty()) return false;
		bool keyed = false;
		for (int k : avail)
		{
			auto s = Spec(w.members[k]);
			if (s->always) return true; // US-T440: an alert shows whenever it can
			keyed = keyed || s->keyed;
		}
		if (!w.shown) return false;
		if (bareFrame) return keyed || w.peek; // the HUD's panels hidden (U): what a key opened still shows
		return hudMode || w.pinned || w.peek || keyed;
	}

	void HudDrawers::Activate(DrawerWindow& w, const cs::String& id)
	{
		for (size_t k = 0; k < w.members.size(); k++)
			if (w.members[k] == id) w.active = static_cast<int>(k);
	}

	void HudDrawers::Open(const cs::String& id, bool peek)
	{
		int i = WindowOf(id);
		if (i < 0) return;
		auto& w = windows[i];
		Activate(w, id);
		w.shown = true;
		w.collapsed = false;
		if (peek) w.peek = true;
	}

	void HudDrawers::Close(const cs::String& id)
	{
		int i = WindowOf(id);
		if (i < 0) return;
		windows[i].shown = false;
		windows[i].peek = false;
	}

	bool HudDrawers::Toggle(const cs::String& id, bool peek)
	{
		if (Visible(id))
		{
			Close(id);
			return false;
		}
		Open(id, peek);
		return true;
	}

	bool HudDrawers::Visible(const cs::String& id) const
	{
		int i = WindowOf(id);
		if (i < 0) return false;
		const auto& w = windows[i];
		if (!VisibleNow(w, lastHudMode, haveLast && last.bare) || w.collapsed || !Available(id)) return false;
		if (!w.tabbed) return true;
		auto avail = AvailableMembers(w);
		int active = w.active;
		if (std::find(avail.begin(), avail.end(), active) == avail.end() && !avail.empty()) active = avail[0];
		return active >= 0 && active < static_cast<int>(w.members.size()) && w.members[active] == id;
	}

	bool HudDrawers::Shown(const cs::String& id) const
	{
		int i = WindowOf(id);
		return i >= 0 && windows[i].shown;
	}

	void HudDrawers::SetPinned(const cs::String& id, bool pinned)
	{
		int i = WindowOf(id);
		if (i >= 0) windows[i].pinned = pinned;
	}

	bool HudDrawers::Pinned(const cs::String& id) const
	{
		int i = WindowOf(id);
		return i >= 0 && windows[i].pinned;
	}

	void HudDrawers::SetCollapsed(const cs::String& id, bool collapsed)
	{
		int i = WindowOf(id);
		if (i >= 0) windows[i].collapsed = collapsed;
	}

	void HudDrawers::Renumber(Edge edge)
	{
		// Each of the edge's two stacks (US-T440) numbered on its own.
		for (int far = 0; far < 2; far++)
		{
			std::vector<int> on;
			for (size_t i = 0; i < windows.size(); i++)
				if (windows[i].docked && windows[i].edge == edge && windows[i].far == (far == 1)) on.push_back(static_cast<int>(i));
			std::stable_sort(on.begin(), on.end(), [&](int a, int b) { return windows[a].order < windows[b].order; });
			for (size_t k = 0; k < on.size(); k++) windows[on[k]].order = static_cast<int>(k);
		}
	}

	void HudDrawers::Float(const cs::String& id, Rect r)
	{
		int i = WindowOf(id);
		if (i < 0) return;
		auto& w = windows[i];
		bool was = w.docked;
		Edge old = w.edge;
		w.docked = false;
		w.centred = false; // US-T440: put somewhere, it stays there
		w.floating = r;
		if (was) Renumber(old);
	}

	void HudDrawers::Dock(const cs::String& id, Edge edge, int order, bool far)
	{
		int i = WindowOf(id);
		if (i < 0) return;
		auto& w = windows[i];
		bool was = w.docked;
		Edge old = w.edge;
		w.centred = false;
		w.far = HudDock::Vertical(edge) && far;
		// The others in that stack of that edge in their order, this one put in at `order`.
		std::vector<int> on;
		for (size_t k = 0; k < windows.size(); k++)
			if (static_cast<int>(k) != i && windows[k].docked && windows[k].edge == edge && windows[k].far == w.far) on.push_back(static_cast<int>(k));
		std::stable_sort(on.begin(), on.end(), [&](int a, int b) { return windows[a].order < windows[b].order; });
		int at = std::clamp(order, 0, static_cast<int>(on.size()));
		on.insert(on.begin() + at, i);
		w.docked = true;
		w.edge = edge;
		for (size_t k = 0; k < on.size(); k++) windows[on[k]].order = static_cast<int>(k);
		if (was) Renumber(old);
	}

	void HudDrawers::RemoveEmpty()
	{
		bool removed = false;
		for (size_t i = windows.size(); i-- > 0;)
			if (windows[i].members.empty())
			{
				windows.erase(windows.begin() + static_cast<std::ptrdiff_t>(i));
				removed = true;
			}
		if (removed)
			for (int e = 0; e < HudDock::EdgeCount; e++) Renumber(static_cast<Edge>(e));
	}

	void HudDrawers::Join(const cs::String& id, const cs::String& into)
	{
		int a = WindowOf(id), b = WindowOf(into);
		if (a < 0 || b < 0 || a == b) return;
		auto& to = windows[b];
		for (const auto& m : windows[a].members) to.members.push_back(m);
		Activate(to, id);
		to.shown = true;
		to.collapsed = false;
		windows[a].members.clear();
		RemoveEmpty();
	}

	int HudDrawers::TearOff(const cs::String& id, Rect r)
	{
		int a = WindowOf(id);
		if (a < 0) return -1;
		auto& from = windows[a];
		if (from.members.size() <= 1)
		{
			bool was = from.docked;
			Edge old = from.edge;
			from.docked = false;
			from.centred = false;
			from.floating = r;
			from.shown = true;
			if (was) Renumber(old);
			return a;
		}
		cs::String active = from.members[std::clamp(from.active, 0, static_cast<int>(from.members.size()) - 1)];
		from.members.erase(std::find(from.members.begin(), from.members.end(), id));
		from.active = 0;
		if (active != id) Activate(from, active);
		DrawerWindow w;
		w.members = { id };
		w.shown = true;
		w.docked = false;
		w.edge = from.edge;
		w.size = Spec(id) != nullptr ? Spec(id)->size : from.size;
		w.floating = r;
		w.slide = 1;
		w.peek = from.peek;
		w.far = false;
		w.centred = false;
		windows.push_back(w);
		return static_cast<int>(windows.size()) - 1;
	}

	void HudDrawers::Detach(const cs::String& id, Rect r) { TearOff(id, r); }

	void HudDrawers::SetTabbed(const cs::String& id, bool tabbed)
	{
		int i = WindowOf(id);
		if (i >= 0) windows[i].tabbed = tabbed;
	}

	bool HudDrawers::Grow(const cs::String& id, int dir)
	{
		int i = WindowOf(id);
		if (i < 0) return false;
		auto& w = windows[i];
		constexpr float Step = 1.35f;
		w.fitted = false; // US-T440: sized by hand, it no longer follows its content
		if (!w.docked)
		{
			float sh = haveLast ? last.screenH : 1080.0f;
			float h = w.floating.height;
			if (dir < 0 && h <= MinH + 0.5f) return false;
			w.floating.height = std::clamp(dir > 0 ? h * Step : h / Step, MinH, sh - 8);
			return true;
		}
		bool vertical = HudDock::Vertical(w.edge);
		Box band = haveLast ? last.bands[static_cast<int>(w.edge)] : Box(0, 0, 1920, 1080);
		float across = vertical ? band.w : band.h;
		float maxT = across * (vertical ? 0.6f : 0.85f), minT = vertical ? MinW : 120.0f;
		float& t = vertical ? w.size.x : w.size.y;
		if (t <= 0) t = across * 0.5f;
		if (dir < 0 && t <= minT + 0.5f) return false;
		t = std::clamp(dir > 0 ? t * Step : t / Step, minT, std::max(minT, maxT));
		return true;
	}

	Rect HudDrawers::RectOf(const cs::String& id) const
	{
		int i = WindowOf(id);
		return i >= 0 && windows[i].drawn ? windows[i].rect : Rect();
	}

	Rect HudDrawers::PublishedRect(const cs::String& id)
	{
		auto it = published.find(id);
		return it == published.end() ? Rect() : it->second;
	}

	Rect HudDrawers::PublishedContent(const cs::String& id)
	{
		auto it = publishedContent.find(id);
		return it == publishedContent.end() ? Rect() : it->second;
	}

	bool HudDrawers::BareNow(const DrawerWindow& w, const Frame& f) const
	{
		if (f.hudMode || w.members.size() != 1) return false;
		auto s = Spec(w.members[0]);
		return s != nullptr && s->bare;
	}

	HudDock::Edge HudDrawers::HomeEdge(const DrawerWindow& w, const Frame& f) const
	{
		return w.docked ? w.edge : HudDock::NearestEdge(ToBox(w.floating), f.screenW, f.screenH);
	}

	// ---------------------------------------------------------------- per frame

	void HudDrawers::Tick(float dt, bool hudMode)
	{
		if (!laidOut) ResetLayout();
		if (hudMode != lastHudMode)
		{
			for (auto& w : windows) w.peek = false;
			lastHudMode = hudMode;
		}
		bool motion = UiMotion::Enabled();
		for (auto& w : windows)
		{
			float target = VisibleNow(w, hudMode, haveLast && last.bare) ? 1.0f : 0.0f;
			w.slide = motion ? Mathf::MoveTowards(w.slide, target, dt / UiMotion::PanelSeconds) : target;
		}
	}

	void HudDrawers::Layout(const Frame& f)
	{
		if (!laidOut) ResetLayout();
		bool moving = drag.kind == DragKind::Move || drag.kind == DragKind::Resize;
		for (auto& w : windows)
		{
			w.drawn = false;
			w.bareNow = BareNow(w, f);
			// US-T440: a fit window takes the size its content asked for; a centred one stays centred across the screen.
			Vector2 fs;
			bool dragged = moving && drag.window >= 0 && drag.window < static_cast<int>(windows.size()) && &windows[drag.window] == &w;
			if (!dragged && FitSize(w, w.bareNow, fs))
			{
				if (w.docked) w.size = fs;
				else { w.floating.width = fs.x; w.floating.height = fs.y; }
			}
			if (!w.docked && w.centred && !dragged && !w.members.empty())
			{
				auto s = Spec(w.members[0]);
				float y = s != nullptr && s->floatY >= 0 ? s->floatY : w.floating.y;
				w.floating.x = (f.screenW - w.floating.width) * 0.5f;
				w.floating.y = s != nullptr && s->floatBottom ? f.screenH - y - w.floating.height : y;
			}
		}
		for (int e = 0; e < HudDock::EdgeCount; e++)
		{
			auto edge = static_cast<Edge>(e);
			std::vector<int> on;
			for (size_t i = 0; i < windows.size(); i++)
			{
				const auto& w = windows[i];
				if (!w.docked || w.edge != edge) continue;
				if (!(VisibleNow(w, f.hudMode, f.bare) || w.slide > 0.001f) || AvailableMembers(w).empty()) continue;
				on.push_back(static_cast<int>(i));
			}
			if (on.empty()) continue;
			// The near stack in its order, then the far one (US-T440).
			std::stable_sort(on.begin(), on.end(), [&](int a, int b)
			{
				if (windows[a].far != windows[b].far) return !windows[a].far;
				return windows[a].order < windows[b].order;
			});
			bool vertical = HudDock::Vertical(edge);
			std::vector<HudDock::Slot> slots;
			for (int i : on)
			{
				const auto& w = windows[i];
				float top = w.bareNow ? 0.0f : TitleH;
				// US-T440: every edge stacks its windows one over another: a slot is the window's height by its width.
				HudDock::Slot s;
				s.length = w.collapsed ? top : w.size.y;
				s.thickness = w.size.x > 0 ? w.size.x : (vertical ? 260.0f : 0.0f);
				s.least = w.collapsed ? top : (s.length > 0 ? std::min(TitleH + 36, s.length) : TitleH + 36);
				s.far = w.far;
				slots.push_back(s);
			}
			const auto& band = f.bands[e];
			auto boxes = HudDock::StackEdge(edge, band, slots, UiStyle::Space::Gap);
			// What each could have: the band less what the others ask for (a yielding drawer's asking counts for no one).
			float askedRoom = 0;
			for (size_t k = 0; k < on.size(); k++)
			{
				auto s0 = windows[on[k]].members.empty() ? nullptr : Spec(windows[on[k]].members[0]);
				if (s0 != nullptr && s0->yields) continue;
				askedRoom += (slots[k].length > 0 ? slots[k].length : slots[k].least) + UiStyle::Space::Gap;
			}
			for (size_t k = 0; k < on.size(); k++)
			{
				auto& w = windows[on[k]];
				float t = UiMotion::Enabled() ? Eased(w.slide) : (w.slide > 0 ? 1.0f : 0.0f);
				w.rect = ToRect(HudDock::Slide(boxes[k], edge, t, f.screenW, f.screenH));
				w.drawn = true;
				auto s0 = w.members.empty() ? nullptr : Spec(w.members[0]);
				float mine = s0 != nullptr && s0->yields ? 0.0f : (slots[k].length > 0 ? slots[k].length : slots[k].least) + UiStyle::Space::Gap;
				float chrome = w.bareNow ? 0.0f : TitleH + Pad;
				w.room = std::max(0.0f, band.h - (askedRoom - mine) - chrome);
			}
		}
		for (size_t i = 0; i < windows.size(); i++)
		{
			auto& w = windows[i];
			if (w.docked || AvailableMembers(w).empty()) continue;
			bool dragged = moving && drag.window == static_cast<int>(i);
			if (!dragged && !(VisibleNow(w, f.hudMode, f.bare) || w.slide > 0.001f)) continue;
			float top = w.bareNow ? 0.0f : TitleH;
			auto r = HudDock::KeepOnScreen(ToBox(w.floating), f.screenW, f.screenH, 4, MinW, w.collapsed ? top : MinH);
			if (!dragged) w.floating = ToRect(r); // kept on screen at this resolution and UI scale
			if (w.collapsed) r.h = top;
			w.rect = ToRect(r);
			w.drawn = true;
			w.room = std::max(0.0f, f.screenH - 8 - (w.bareNow ? 0.0f : TitleH + Pad));
		}
	}

	HudDock::Box HudDrawers::EdgeExtent(Edge edge, const Frame& f)
	{
		Layout(f);
		Box out;
		bool any = false;
		for (const auto& w : windows)
		{
			if (!w.drawn || !w.docked || w.edge != edge) continue;
			Box b = ToBox(w.rect);
			if (!any) { out = b; any = true; continue; }
			float x0 = std::min(out.x, b.x), y0 = std::min(out.y, b.y);
			float x1 = std::max(out.Right(), b.Right()), y1 = std::max(out.Bottom(), b.Bottom());
			out = Box(x0, y0, x1 - x0, y1 - y0);
		}
		return out;
	}

	std::vector<Box> HudDrawers::TitleStrips(int except) const
	{
		std::vector<Box> out;
		for (size_t i = 0; i < windows.size(); i++)
		{
			const auto& w = windows[i];
			if (static_cast<int>(i) == except || !w.drawn || w.bareNow) out.emplace_back();
			else out.emplace_back(w.rect.x, w.rect.y, w.rect.width, TitleH);
		}
		return out;
	}

	void HudDrawers::Draw(const Frame& f)
	{
		last = f;
		haveLast = true;
		auto& e = Event::current();
		if (e.type == EventType::Repaint)
		{
			published.clear();
			publishedContent.clear();
		}
		if (drag.kind != DragKind::None && GUIUtility::hotControl != DragControl)
		{
			drag = Drag(); // something else took the pointer
			previewing = false;
		}
		// US-T440: a fit drawer that had nothing to show is asked again on each Layout pass, drawn off the screen (nothing of it
		// is seen and nothing takes a click there), so it comes back the moment it has something.
		if (e.type == EventType::Layout)
			for (const auto& s : specs)
			{
				if (!s.fit || !s.draw) continue;
				auto it = empties.find(s.id);
				if (it == empties.end() || !it->second || !SpecAvailable(s.id)) continue;
				int wi = WindowOf(s.id);
				if (wi < 0 || !(windows[wi].shown || s.always)) continue;
				auto want = wants.find(s.id);
				Vector2 size = want != wants.end() ? want->second : s.size;
				DrawContent(s, Rect(-30000, -30000, std::max(1.0f, size.x), std::max(1.0f, size.y)));
			}
		Layout(f);

		// Docked first, then floating, the one dragged last (on top).
		std::vector<int> order;
		for (size_t i = 0; i < windows.size(); i++)
			if (windows[i].drawn && windows[i].docked) order.push_back(static_cast<int>(i));
		for (size_t i = 0; i < windows.size(); i++)
			if (windows[i].drawn && !windows[i].docked && static_cast<int>(i) != drag.window) order.push_back(static_cast<int>(i));
		if (drag.window >= 0 && drag.window < static_cast<int>(windows.size()) && windows[drag.window].drawn && !windows[drag.window].docked)
			order.push_back(drag.window);
		// A press goes to the topmost window under it (the last drawn).
		topHit = -1;
		if (e.type == EventType::MouseDown)
			for (int i : order)
				if (windows[i].rect.Contains(e.mousePosition)) topHit = i;
		// The edge tabs over the docked windows and under the floating ones (US-T440: a centred board covers the top's tabs).
		size_t docked = 0;
		while (docked < order.size() && windows[order[docked]].docked) docked++;
		for (size_t k = 0; k < docked; k++) DrawWindow(order[k], f);
		DrawEdgeTabs(f);
		for (size_t k = docked; k < order.size(); k++) DrawWindow(order[k], f);
		HandleDrag(f);
		if (drag.kind == DragKind::Move) DrawPreview(f);
		// While dragging, the whole screen is the drag's: nothing under the pointer takes the release.
		if (drag.kind != DragKind::None && LayoutOrRepaint()) f.registerUi(Rect(0, 0, f.screenW, f.screenH));
	}

	void HudDrawers::DrawWindow(int index, const Frame& f)
	{
		auto& w = windows[index];
		auto avail = AvailableMembers(w);
		if (avail.empty()) return;
		if (std::find(avail.begin(), avail.end(), w.active) == avail.end()) w.active = avail[0];
		const auto& st = f.styles;
		auto& e = Event::current();
		Vector2 m = e.mousePosition;
		Rect r = w.rect;
		bool repaint = e.type == EventType::Repaint;

		float oldFade = GUI::fade;
		if (!w.docked && UiMotion::Enabled()) GUI::fade = oldFade * Eased(w.slide);

		// US-T440: a bare drawer in the minimal HUD: the overlay it was, no title bar (and a frameless one no panel either).
		if (w.bareNow)
		{
			const auto& id = w.members[w.active];
			auto s = Spec(id);
			if (s != nullptr && !s->frameless) f.panel(r);
			if (repaint)
			{
				published[id] = r;
				publishedContent[id] = r;
			}
			if (s != nullptr && !w.collapsed) DrawContent(*s, r);
			GUI::fade = oldFade;
			return;
		}
		f.panel(r);

		// The worst alert of what it holds tints its title bar.
		int alertLevel = 0;
		for (int k : avail) alertLevel = std::max(alertLevel, AlertOf(w.members[k]).level);
		if (alertLevel > 0) Fill(Rect(r.x + 2, r.y + 2, r.width - 4, 2), AlertColor(alertLevel));

		// The title bar's buttons, right to left: close (none for an alert, US-T440), pin, tabs / stack; a narrow window's as
		// one-letter buttons, so its title or tabs keep their room (US-T440).
		bool narrow = r.width < 320;
		float bx = r.xMax() - Pad;
		float by = r.y + 3, bh = TitleH - 6;
		if (!HasAlways(w))
		{
			bx -= 20;
			if (GUI::Button(Rect(bx, by, 20, bh), "x", st.button))
			{
				w.shown = false;
				w.peek = false;
			}
		}
		float pw = narrow ? 20.0f : 52.0f;
		bx -= pw + 3;
		if (GUI::Button(Rect(bx, by, pw, bh), narrow ? "P" : w.pinned ? "PINNED" : "PIN", w.pinned ? st.buttonActive : st.button)) w.pinned = !w.pinned;
		if (w.members.size() > 1)
		{
			float mw = narrow ? 20.0f : 50.0f;
			bx -= mw + 3;
			if (GUI::Button(Rect(bx, by, mw, bh), narrow ? (w.tabbed ? "=" : "T") : w.tabbed ? "STACK" : "TABS", st.button)) w.tabbed = !w.tabbed;
		}

		// The handle: the rest of the title bar (fold mark, then the title or the tabs).
		Rect handle(r.x, r.y, std::max(0.0f, bx - 3 - r.x), TitleH);
		auto old = GUI::contentColor;
		GUI::contentColor = UiStyle::Accent;
		GUI::Label(Rect(r.x + 6, r.y + 4, 14, 16), w.collapsed ? UiStyle::Glyph::FoldShut : UiStyle::Glyph::FoldOpen, st.small);
		GUI::contentColor = old;
		float tx = r.x + 22;
		std::vector<std::pair<Rect, cs::String>> tabs;
		if (w.tabbed && avail.size() > 1)
		{
			std::vector<float> want, least;
			for (int k : avail)
			{
				auto sp = Spec(w.members[k]);
				want.push_back(TextW(st.small, sp->title) + 16);
				// US-T440: a tab never narrower than its short title (or the first letters of its title) needs.
				cs::String brief = sp->shortTitle.empty() ? cs::String(sp->title.substr(0, std::min<size_t>(4, sp->title.size()))) : sp->shortTitle;
				least.push_back(std::max(36.0f, TextW(st.small, brief) + 14));
			}
			auto len = HudDock::Split(want, least, handle.xMax() - tx - 2, 3);
			float x = tx;
			for (size_t j = 0; j < avail.size(); j++)
			{
				const auto& id = w.members[avail[j]];
				Rect tr(x, by, len[j], bh);
				bool on = avail[j] == w.active;
				auto a = AlertOf(id);
				if (a.level > 0) GUI::contentColor = AlertColor(a.level);
				// US-T440: too narrow for its title, a tab shows its short title (whole, not cut to "RE...").
				auto sp = Spec(id);
				cs::String tabText = TextW(st.small, sp->title) + 8 > len[j] && !sp->shortTitle.empty() ? sp->shortTitle : sp->title;
				GUI::Box(tr, GUIContent(Fit(tabText, st.small, len[j] - 8)), on ? st.buttonActive : st.button);
				GUI::contentColor = old;
				tabs.push_back({ tr, id });
				x += len[j] + 3;
			}
		}
		else
		{
			cs::String text;
			if (avail.size() == 1 || w.tabbed) text = Spec(w.members[w.active])->title;
			else
				for (size_t j = 0; j < avail.size(); j++) text = j == 0 ? Spec(w.members[avail[j]])->title : cs::Cat(text, " + ", Spec(w.members[avail[j]])->title);
			GUI::Label(Rect(tx, r.y + 3, handle.xMax() - tx, TitleH - 4), Fit(text, st.title, handle.xMax() - tx - 2), st.title);
		}
		auto press = [&](DragFrom from, const cs::String& member)
		{
			drag = Drag();
			drag.kind = DragKind::Pending;
			drag.from = from;
			drag.window = index;
			drag.member = member;
			drag.down = m;
			drag.grab = Vector2(m.x - r.x, m.y - r.y);
			drag.start = r;
			GUIUtility::hotControl = DragControl;
			e.Use();
		};
		if (e.type == EventType::MouseDown && e.button == 0 && topHit == index && GUIUtility::hotControl == 0 && handle.Contains(m))
		{
			cs::String member;
			for (const auto& t : tabs)
				if (t.first.Contains(m)) member = t.second;
			press(member.empty() ? DragFrom::Title : DragFrom::Tab, member);
		}

		if (!w.collapsed)
		{
			Rect content(r.x + Pad, r.y + TitleH, r.width - 2 * Pad, r.height - TitleH - Pad);
			if (content.height > 4 && content.width > 4)
			{
				if (w.tabbed || avail.size() == 1)
				{
					const auto& id = w.members[w.active];
					if (repaint)
					{
						published[id] = r;
						publishedContent[id] = content;
					}
					if (auto s = Spec(id); s != nullptr) DrawContent(*s, content);
				}
				else
				{
					// Stacked: each member under its own header, the height split between them.
					std::vector<float> want(avail.size(), 0.0f), least(avail.size(), SectionH + 24);
					auto len = HudDock::Split(want, least, content.height, Pad);
					float y = content.y;
					for (size_t j = 0; j < avail.size(); j++)
					{
						const auto& id = w.members[avail[j]];
						Rect sec(content.x, y, content.width, len[j]);
						Rect head(sec.x, sec.y, sec.width, SectionH);
						Fill(Rect(head.x, head.yMax() - 1, head.width, 1), UiStyle::Edge);
						auto a = AlertOf(id);
						GUI::contentColor = a.level > 0 ? AlertColor(a.level) : UiStyle::Accent;
						GUI::Label(Rect(head.x + 4, head.y + 1, head.width - 8, SectionH - 2), Fit(Spec(id)->title, st.small, head.width - 8), st.small);
						GUI::contentColor = old;
						if (e.type == EventType::MouseDown && e.button == 0 && topHit == index && GUIUtility::hotControl == 0 && head.Contains(m))
							press(DragFrom::Section, id);
						Rect body(sec.x, sec.y + SectionH + 2, sec.width, sec.height - SectionH - 2);
						if (repaint)
						{
							published[id] = sec;
							publishedContent[id] = body;
						}
						if (auto s = Spec(id); s != nullptr && body.height > 4) DrawContent(*s, body);
						y += len[j] + Pad;
					}
				}
			}
			// A floating window's size: the grip in its bottom-right corner.
			if (!w.docked)
			{
				Rect grip(r.xMax() - 14, r.yMax() - 14, 14, 14);
				Fill(Rect(grip.xMax() - 10, grip.yMax() - 3, 8, 1), UiStyle::Dim);
				Fill(Rect(grip.xMax() - 3, grip.yMax() - 10, 1, 8), UiStyle::Dim);
				if (e.type == EventType::MouseDown && e.button == 0 && topHit == index && GUIUtility::hotControl == 0 && grip.Contains(m))
				{
					press(DragFrom::Grip, cs::String());
					drag.kind = DragKind::Resize;
				}
			}
		}
		GUI::fade = oldFade;
	}

	void HudDrawers::CollectEdgeTabs(const Frame& f, std::vector<EdgeTab> (&on)[HudDock::EdgeCount], std::vector<float> (&lens)[HudDock::EdgeCount]) const
	{
		const auto& st = f.styles;
		for (const auto& s : specs)
		{
			if (f.bare || s.always || !Available(s.id)) continue; // US-T440: no tabs with the HUD's panels hidden; an alert has none
			int wi = WindowOf(s.id);
			if (wi < 0) continue;
			const auto& w = windows[wi];
			if (VisibleNow(w, f.hudMode, f.bare) && w.drawn) continue; // in its window (a group's tab strip shows the rest)
			auto a = AlertOf(s.id);
			if (!f.hudMode && a.level < 2) continue; // the minimal HUD shows only the tabs that raise something urgent (US-T439)
			cs::String label = !f.hudMode && !a.text.empty() ? cs::Cat(s.title, UiStyle::Glyph::Separator, a.text) : s.title;
			int e = static_cast<int>(HomeEdge(w, f));
			on[e].push_back({ s.id, label, a.level });
			lens[e].push_back(std::min(260.0f, TextW(st.small, label) + 18));
		}
	}

	HudDock::Box HudDrawers::EdgeTabExtent(Edge edge, const Frame& f)
	{
		Layout(f);
		std::vector<EdgeTab> on[HudDock::EdgeCount];
		std::vector<float> lens[HudDock::EdgeCount];
		CollectEdgeTabs(f, on, lens);
		int e = static_cast<int>(edge);
		Box out;
		bool any = false;
		for (const auto& b : HudDock::EdgeTabs(edge, TabBand(edge, f), lens[e], TabH, 4))
		{
			if (!any) { out = b; any = true; continue; }
			float x0 = std::min(out.x, b.x), y0 = std::min(out.y, b.y);
			float x1 = std::max(out.Right(), b.Right()), y1 = std::max(out.Bottom(), b.Bottom());
			out = Box(x0, y0, x1 - x0, y1 - y0);
		}
		return out;
	}

	HudDock::Box HudDrawers::TabBand(Edge edge, const Frame& f) const
	{
		// US-T440: the tabs keep out of the windows docked there: in the room between the edge's two stacks (past the top or
		// bottom edge's windows), as the band was when that room is too small.
		Box band = f.bands[static_cast<int>(edge)];
		float top = band.y, bottom = band.Bottom();
		for (const auto& w : windows)
		{
			if (!w.drawn || !w.docked || w.edge != edge) continue;
			if (HudDock::FromTop(edge, w.far)) top = std::max(top, w.rect.yMax() + UiStyle::Space::Gap);
			else bottom = std::min(bottom, w.rect.y - UiStyle::Space::Gap);
		}
		if (bottom - top < TabH) return band;
		return Box(band.x, top, band.w, bottom - top);
	}

	void HudDrawers::DrawEdgeTabs(const Frame& f)
	{
		const auto& st = f.styles;
		std::vector<EdgeTab> on[HudDock::EdgeCount];
		std::vector<float> lens[HudDock::EdgeCount];
		CollectEdgeTabs(f, on, lens);
		auto& ev = Event::current();
		auto old = GUI::contentColor;
		for (int e = 0; e < HudDock::EdgeCount; e++)
		{
			tabExtents[e] = Box();
			if (on[e].empty()) continue;
			auto edge = static_cast<Edge>(e);
			auto boxes = HudDock::EdgeTabs(edge, TabBand(edge, f), lens[e], TabH, 4);
			for (const auto& b : boxes)
			{
				auto& x = tabExtents[e];
				if (x.Empty()) { x = b; continue; }
				float x0 = std::min(x.x, b.x), y0 = std::min(x.y, b.y);
				x = Box(x0, y0, std::max(x.Right(), b.Right()) - x0, std::max(x.Bottom(), b.Bottom()) - y0);
			}
			for (size_t k = 0; k < boxes.size(); k++)
			{
				Rect r = ToRect(boxes[k]);
				const auto& t = on[e][k];
				if (LayoutOrRepaint()) f.registerUi(r);
				if (t.level > 0)
				{
					// The glow: a halo round the tab and its text in the alert's colour.
					Color c = AlertColor(t.level);
					Fill(Rect(r.x - 2, r.y - 2, r.width + 4, r.height + 4), Color(c.r, c.g, c.b, 0.35f * c.a));
					GUI::contentColor = c;
				}
				GUI::Box(r, GUIContent(Fit(t.label, st.small, r.width - 8)), st.button);
				GUI::contentColor = old;
				// Its edge's side marked, the way it pulls out.
				Rect mark = edge == Edge::Left ? Rect(r.x, r.y, 2, r.height) : edge == Edge::Right ? Rect(r.xMax() - 2, r.y, 2, r.height)
					: edge == Edge::Top ? Rect(r.x, r.y, r.width, 2) : Rect(r.x, r.yMax() - 2, r.width, 2);
				Fill(mark, t.level > 0 ? AlertColor(t.level) : UiStyle::Accent);
				bool covered = false; // US-T440: under a floating window the press is the window's
				for (const auto& w : windows)
					if (w.drawn && !w.docked && w.rect.Contains(ev.mousePosition)) covered = true;
				if (ev.type == EventType::MouseDown && ev.button == 0 && GUIUtility::hotControl == 0 && !covered && r.Contains(ev.mousePosition))
				{
					drag = Drag();
					drag.kind = DragKind::Pending;
					drag.from = DragFrom::EdgeTab;
					drag.window = WindowOf(t.id);
					drag.member = t.id;
					drag.down = ev.mousePosition;
					drag.grab = Vector2(40, 12);
					drag.start = r;
					GUIUtility::hotControl = DragControl;
					ev.Use();
				}
			}
		}
	}

	void HudDrawers::HandleDrag(const Frame& f)
	{
		if (drag.kind == DragKind::None || GUIUtility::hotControl != DragControl) return;
		auto& e = Event::current();
		Vector2 m = e.mousePosition;
		if (e.type == EventType::MouseDrag)
		{
			if (drag.kind == DragKind::Pending && Vector2::Distance(m, drag.down) >= DragStart)
			{
				// Becomes a move: a docked window comes off its edge where it is; a tab (of a group's strip, a stacked member's
				// header or an edge's) tears its drawer off into a window of its own, under the pointer.
				int wi = drag.window;
				if (wi >= 0 && wi < static_cast<int>(windows.size()))
				{
					auto& w = windows[wi];
					bool tear = (drag.from == DragFrom::Tab || drag.from == DragFrom::Section || drag.from == DragFrom::EdgeTab) && !drag.member.empty();
					if (tear)
					{
						auto s = Spec(drag.member);
						Vector2 size = w.docked ? w.size : Vector2(w.floating.width, w.floating.height);
						if (w.members.size() > 1 && s != nullptr) size = s->size;
						float fw = size.x > 0 ? size.x : 480, fh = size.y > 0 ? size.y : 320;
						drag.window = TearOff(drag.member, Rect(m.x - drag.grab.x, m.y - drag.grab.y, fw, fh));
						if (drag.window >= 0)
						{
							auto& torn = windows[drag.window];
							torn.collapsed = false;
							if (drag.from == DragFrom::EdgeTab)
							{
								torn.peek = !f.hudMode;
								torn.slide = 1;
							}
							Activate(torn, drag.member);
						}
					}
					else
					{
						if (w.docked)
						{
							Edge old = w.edge;
							w.docked = false;
							w.floating = w.rect;
							Renumber(old);
						}
						else w.floating = w.rect;
						w.centred = false; // US-T440: moved by hand, it stays where it is put
					}
					drag.kind = DragKind::Move;
				}
				else drag.kind = DragKind::None;
			}
			if (drag.kind == DragKind::Move && drag.window >= 0)
			{
				auto& w = windows[drag.window];
				w.floating.x = m.x - drag.grab.x;
				w.floating.y = m.y - drag.grab.y;
			}
			else if (drag.kind == DragKind::Resize && drag.window >= 0)
			{
				auto& w = windows[drag.window];
				w.fitted = false; // US-T440: sized by hand, it no longer follows its content
				w.centred = false;
				w.floating.width = std::max(MinW, drag.start.width + (m.x - drag.down.x));
				w.floating.height = std::max(MinH, drag.start.height + (m.y - drag.down.y));
			}
			e.Use();
		}
		else if (e.type == EventType::MouseUp)
		{
			if (drag.kind == DragKind::Pending && drag.window >= 0 && drag.window < static_cast<int>(windows.size()))
			{
				auto& w = windows[drag.window];
				switch (drag.from)
				{
					case DragFrom::Title: w.collapsed = !w.collapsed; break; // a click folds it, as every panel's title does
					case DragFrom::Tab:
					{
						int was = w.active;
						Activate(w, drag.member);
						if (w.active == was) w.collapsed = !w.collapsed;
						else w.collapsed = false;
						break;
					}
					case DragFrom::EdgeTab: Open(drag.member, !f.hudMode); break;
					default: break;
				}
			}
			else if (drag.kind == DragKind::Move) Drop(f);
			drag = Drag();
			previewing = false;
			GUIUtility::hotControl = 0;
			e.Use();
		}
	}

	void HudDrawers::Drop(const Frame& f)
	{
		int wi = drag.window;
		if (wi < 0 || wi >= static_cast<int>(windows.size())) return;
		Vector2 m = Event::current().mousePosition;
		std::vector<std::vector<Box>> slots(HudDock::EdgeCount);
		std::vector<std::vector<bool>> far(HudDock::EdgeCount);
		for (size_t i = 0; i < windows.size(); i++)
		{
			const auto& o = windows[i];
			if (static_cast<int>(i) == wi || !o.drawn || !o.docked) continue;
			slots[static_cast<int>(o.edge)].push_back(ToBox(o.rect));
			far[static_cast<int>(o.edge)].push_back(o.far);
		}
		// The slots in their order on each edge (InsertOrder counts them by where they lie, so order is all it needs).
		auto snap = HudDock::FindSnap(m.x, m.y, f.screenW, f.screenH, EdgeReach, TitleStrips(wi), slots, far);
		auto& w = windows[wi];
		switch (snap.kind)
		{
			case HudDock::Snap::Kind::Group:
			{
				if (snap.target < 0 || snap.target >= static_cast<int>(windows.size()) || snap.target == wi) break;
				cs::String lead = w.members[std::clamp(w.active, 0, static_cast<int>(w.members.size()) - 1)];
				Join(lead, windows[snap.target].members.front());
				return;
			}
			case HudDock::Snap::Kind::Edge:
			{
				// Docked at the size it floated at (a window across a whole top or bottom edge fills it again).
				bool vertical = HudDock::Vertical(snap.edge);
				const auto& band = f.bands[static_cast<int>(snap.edge)];
				w.size = Vector2(!vertical && w.floating.width >= band.w * 0.8f ? 0.0f : w.floating.width,
					vertical && w.floating.height >= band.h * 0.8f ? 0.0f : w.floating.height);
				Dock(w.members.front(), snap.edge, snap.order, snap.far);
				return;
			}
			default:
				w.floating = ToRect(HudDock::KeepOnScreen(ToBox(w.floating), f.screenW, f.screenH, 4, MinW, MinH));
				return;
		}
	}

	void HudDrawers::DrawPreview(const Frame& f)
	{
		if (Event::current().type != EventType::Repaint || drag.window < 0) return;
		Vector2 m = Event::current().mousePosition;
		std::vector<std::vector<Box>> slots(HudDock::EdgeCount);
		std::vector<std::vector<bool>> far(HudDock::EdgeCount);
		for (size_t i = 0; i < windows.size(); i++)
		{
			const auto& o = windows[i];
			if (static_cast<int>(i) == drag.window || !o.drawn || !o.docked) continue;
			slots[static_cast<int>(o.edge)].push_back(ToBox(o.rect));
			far[static_cast<int>(o.edge)].push_back(o.far);
		}
		preview = HudDock::FindSnap(m.x, m.y, f.screenW, f.screenH, EdgeReach, TitleStrips(drag.window), slots, far);
		previewing = true;
		Color c(UiStyle::Accent.r, UiStyle::Accent.g, UiStyle::Accent.b, 0.85f);
		if (preview.kind == HudDock::Snap::Kind::Group && preview.target >= 0)
		{
			const auto& t = windows[preview.target];
			Rect strip(t.rect.x, t.rect.y, t.rect.width, TitleH);
			Fill(strip, Color(c.r, c.g, c.b, 0.25f));
			Outline(strip, c, 2);
		}
		else if (preview.kind == HudDock::Snap::Kind::Edge)
		{
			const auto& b = f.bands[static_cast<int>(preview.edge)];
			Rect bar = preview.edge == Edge::Left ? Rect(b.x, b.y, 6, b.h) : preview.edge == Edge::Right ? Rect(b.Right() - 6, b.y, 6, b.h)
				: preview.edge == Edge::Top ? Rect(b.x, b.y, b.w, 6) : Rect(b.x, b.Bottom() - 6, b.w, 6);
			Fill(bar, c);
		}
	}

	// ---------------------------------------------------------------- saving

	cs::Ref<JsonObject> HudDrawers::Save() const
	{
		auto o = cs::New<JsonObject>();
		o->Set("version", 1);
		auto list = cs::New<JsonArray>();
		for (const auto& w : windows)
		{
			auto j = cs::New<JsonObject>();
			auto ids = cs::New<JsonArray>();
			for (const auto& m : w.members) ids->Add(JsonValue(m));
			j->Set("drawers", ids);
			j->Set("active", w.active);
			j->Set("tabbed", w.tabbed);
			j->Set("shown", w.shown);
			j->Set("collapsed", w.collapsed);
			j->Set("pinned", w.pinned);
			j->Set("docked", w.docked);
			j->Set("edge", HudDock::EdgeName(w.edge));
			j->Set("order", w.order);
			j->Set("w", w.size.x);
			j->Set("h", w.size.y);
			j->Set("x", w.floating.x);
			j->Set("y", w.floating.y);
			j->Set("fw", w.floating.width);
			j->Set("fh", w.floating.height);
			j->Set("far", w.far); // US-T440
			j->Set("fit", w.fitted);
			j->Set("centred", w.centred);
			list->Add(JsonValue(j));
		}
		o->Set("windows", list);
		return o;
	}

	bool HudDrawers::Load(const cs::Ref<JsonObject>& layout)
	{
		if (layout == nullptr) return false;
		auto list = layout->GetArray("windows");
		if (list == nullptr) return false;
		std::vector<DrawerWindow> loaded;
		std::unordered_set<cs::String> placed;
		for (int i = 0; i < list->Count(); i++)
		{
			auto j = (*list)[i].AsObject();
			if (j == nullptr) continue;
			auto ids = j->GetArray("drawers");
			if (ids == nullptr) continue;
			DrawerWindow w;
			for (int k = 0; k < ids->Count(); k++)
			{
				auto s = (*ids)[k].AsString();
				if (s != nullptr && Has(*s) && placed.insert(*s).second) w.members.push_back(*s);
			}
			if (w.members.empty()) continue;
			w.active = std::clamp(j->GetInt("active"), 0, static_cast<int>(w.members.size()) - 1);
			w.tabbed = j->GetBool("tabbed", true);
			w.shown = j->GetBool("shown", true);
			w.collapsed = j->GetBool("collapsed");
			w.pinned = j->GetBool("pinned");
			w.docked = j->GetBool("docked", true);
			if (!HudDock::EdgeFromName(j->GetString("edge"), w.edge)) w.edge = Edge::Right;
			w.order = j->GetInt("order");
			w.size = Vector2(j->GetFloat("w", 260), j->GetFloat("h", 200));
			w.floating = Rect(j->GetFloat("x", 200), j->GetFloat("y", 200), j->GetFloat("fw", 320), j->GetFloat("fh", 240));
			w.far = j->GetBool("far"); // US-T440
			w.fitted = j->GetBool("fit", true);
			w.centred = j->GetBool("centred");
			loaded.push_back(w);
		}
		windows = std::move(loaded);
		laidOut = true;
		drag = Drag();
		previewing = false;
		for (const auto& s : specs)
			if (placed.find(s.id) == placed.end())
			{
				auto w = DefaultWindow(s);
				windows.push_back(w);
			}
		for (int e = 0; e < HudDock::EdgeCount; e++) Renumber(static_cast<Edge>(e));
		return true;
	}
}
