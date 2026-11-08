// US-T200: the bottom drawer (see UI/Hud.Drawer.inl for the declarations and the rule): a handle at the bottom left
// pulls it up; Navigation lists the systems and this system's places, Ships in area the hulls about, a click hailing one
// in range (UnitySpaceApp::HailShip, US-T201).

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Input/InputDevice.h"
#include "Input/KeyBindings.h" // US-T249

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Comms/Comms.h"
#include "Infrastructure/Infrastructure.h"
#include "Sensors/Sensors.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"

#include <algorithm>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		/// "▲" (Arial's WGL4 triangle, like UiStyle's fold marks): the handle's pull-up mark.
		constexpr const char* GlyphUp = "\xE2\x96\xB2"; // U+25B2

		/// How much of the free height (under the top bar, over the status line) each drawer step takes.
		constexpr float StepFraction[] = { 0.3f, 0.52f, 0.8f };

		cs::String RoleName(ShipRole r)
		{
			return cs::Strings::ToLowerInvariant(cs::String(Core::ToString(r)));
		}
	}

	bool Hud::DrawerAvailable() const
	{
		return app->ShowHud && !app->ScreenCleared && !app->ShowSpawn && !app->ShowHelp && !app->ShowLogs /* US-T261 */ && !app->ShowDiscoveries /* US-T259 */ && !app->ShowEncyclopedia /* US-T271 */ && !(app->ShowGalaxyMap && !app->Travel->Active) && !(app->ShowSystemMap && !app->Travel->Active) /* US-T258 */;
	}

	bool Hud::UpdateDrawer(bool keysFree)
	{
		float dt = Mathf::Min(Time::unscaledDeltaTime, 0.1f);
		bool available = DrawerAvailable();
		if (!available) drawerOpen = false;
		// Pulled up (and let down) over a fifth of a second.
		drawerShown = Mathf::MoveTowards(drawerShown, drawerOpen ? 1.0f : 0.0f, dt * 5.0f);
		if (!keysFree || !available) return false;

		const Input::InputDevice& kb = Input::InputDevice::current();
		auto pressed = [&](Input::Key k) { return kb[k].wasPressedThisFrame; };
		bool shift = kb[Input::Key::LeftShift].isPressed || kb[Input::Key::RightShift].isPressed;
		if (Input::KeyBindings::Pressed(Input::KeyAction::Drawer, kb)) // US-T249: Shift+U unless rebound
		{
			drawerOpen = !drawerOpen;
			drawerFollowRow = true;
			return true; // the key is spent (U alone is the HUD's: it does not go off under it)
		}
		if (!drawerOpen) return false;

		if (pressed(Input::Key::Escape))
		{
			drawerOpen = false;
			return true;
		}
		bool back = (pressed(Input::Key::Tab) && shift) || pressed(Input::Key::LeftArrow);
		bool next = (pressed(Input::Key::Tab) && !shift) || pressed(Input::Key::RightArrow);
		if (back || next)
		{
			drawerTab = drawerTab == DrawerTab::Navigation ? DrawerTab::Ships : DrawerTab::Navigation;
			drawerRow = 0;
			drawerScroll = Vector2::zero();
			drawerItems.clear();
			drawerRefreshAt = -10.0f;
		}
		if (pressed(Input::Key::PageUp)) drawerStep = std::min(drawerStep + 1, DrawerSteps - 1);
		if (pressed(Input::Key::PageDown))
		{
			if (drawerStep == 0) drawerOpen = false;
			else drawerStep--;
		}
		if (pressed(Input::Key::DownArrow)) { drawerRow = DrawerStepRow(drawerRow, 1); drawerFollowRow = true; }
		if (pressed(Input::Key::UpArrow)) { drawerRow = DrawerStepRow(drawerRow, -1); drawerFollowRow = true; }
		if (pressed(Input::Key::Enter) || pressed(Input::Key::NumpadEnter)) ActDrawerItem(drawerRow);
		return true; // open, the drawer has the keys (the comms panel's pattern)
	}

	int Hud::DrawerStepRow(int from, int dir) const
	{
		int n = static_cast<int>(drawerItems.size());
		if (n == 0) return 0;
		int i = std::clamp(from, 0, n - 1);
		for (int j = i + dir; j >= 0 && j < n; j += dir)
			if (!drawerItems[j].heading) return j;
		return i;
	}

	// ---------------------------------------------------------------- rows

	void Hud::RebuildDrawerItems()
	{
		drawerItems.clear();
		if (drawerTab == DrawerTab::Navigation) AddDrawerNavigation();
		else AddDrawerShips();
		// The keyboard's row on a row that can take it.
		int n = static_cast<int>(drawerItems.size());
		if (n == 0) { drawerRow = 0; return; }
		drawerRow = std::clamp(drawerRow, 0, n - 1);
		if (drawerItems[drawerRow].heading) drawerRow = DrawerStepRow(drawerRow, 1);
		if (drawerItems[drawerRow].heading) drawerRow = DrawerStepRow(drawerRow, -1);
	}

	void Hud::AddDrawerNavigation()
	{
		auto heading = [&](const cs::String& name, Color c)
		{
			DrawerItem h;
			h.name = name;
			h.color = c;
			h.heading = true;
			drawerItems.push_back(h);
		};
		double jd = app->Clock->Jd;
		bool helm = app->IsPiloting();

		// The other stars: travel there, or (at a helm) the course over the gates.
		heading(helm ? "STAR SYSTEMS  ·  Enter plots the course over the gates" : "STAR SYSTEMS  ·  Enter travels there", UiStyle::Accent);
		auto network = app->Galaxy->Network;
		for (const auto& system : app->Galaxy->Systems)
		{
			DrawerItem it;
			it.system = system;
			it.name = system->Name;
			it.indent = 1;
			bool here = system == app->ActiveSystem;
			bool going = app->Travel->Active && app->Travel->To == system;
			if (here)
			{
				it.details = "here";
				it.enabled = false;
				it.why = "you are here";
				it.color = UiStyle::Good;
			}
			else
			{
				auto route = network->Route(app->ActiveSystem, system, nullptr, jd);
				it.details = going ? cs::String("travelling…")
					: cs::Cat(cs::Fmt(Galaxy::DistanceLy(app->ActiveSystem, system), "0.00"), " ly",
						route == nullptr ? cs::String("  ·  no open way") : cs::Cat("  ·  ", route->Count(), route->Count() == 1 ? " gate" : " gates"));
				it.color = UiStyle::Text;
			}
			drawerItems.push_back(it);
		}

		// This system's worlds, with how far each is from the unit held (or the view).
		heading(cs::Cat(cs::Strings::ToUpperInvariant(app->ActiveSystem->Name), helm ? "  ·  Enter cruises there" : "  ·  Enter flies the view there"), UiStyle::Accent);
		cs::List<NavigatorRow> rows;
		AddBody(rows, 0, 0);
		auto me = app->PlayerHelioKm();
		for (const auto& nav : rows)
		{
			DrawerItem it;
			it.name = nav.name;
			it.color = nav.color;
			it.indent = 1 + nav.indent;
			it.target = nav.target;
			it.heading = nav.target.IsNone();
			if (nav.target.IsBody())
			{
				double km = (app->Ephemeris()->Helio(nav.target.Body) - me).Length();
				it.details = nav.details.empty() ? RangeText(km) : cs::Cat(RangeText(km), "  ·  ", nav.details);
			}
			else it.details = nav.details;
			drawerItems.push_back(it);
		}

		// Docking rings and what is in orbit.
		auto catalog = app->Catalog();
		auto infra = app->World()->Infrastructure;
		if (infra != nullptr && infra->Rings.Count() > 0)
		{
			heading("DOCKING RINGS", UiStyle::Accent);
			for (const auto& ring : infra->Rings)
			{
				DrawerItem it;
				it.name = ring->Name;
				it.details = cs::Cat("· ", (*catalog)[ring->Body]->Name);
				it.target = Target::OfRing(ring);
				it.indent = 1;
				it.color = Color(0.6f, 0.9f, 1.0f);
				drawerItems.push_back(it);
			}
		}
		if (infra != nullptr && infra->Structures.Count() > 0)
		{
			heading("IN ORBIT", UiStyle::Accent);
			for (const auto& s : infra->Structures)
			{
				DrawerItem it;
				it.name = s->Name;
				it.details = cs::Cat("· ", (*catalog)[s->Body]->Name);
				it.target = Target::OfStructure(s);
				it.indent = 1;
				it.color = s->Owner() != nullptr ? Color(s->Owner()->Color.R, s->Owner()->Color.G, s->Owner()->Color.B) : UiStyle::Text;
				drawerItems.push_back(it);
			}
		}
	}

	void Hud::AddDrawerShips()
	{
		auto c = app->CommsHere();
		auto world = app->World();
		double jd = app->Clock->Jd;
		// Round the unit held, else round the view.
		DVec3 from = c.Piloted ? c.HelioKm : app->PlayerHelioKm();
		struct Found { cs::Ref<Ship> Ship; double Km; ContactLevel Level; };
		std::vector<Found> found;
		for (const auto& s : app->Fleet()->Ships())
		{
			if (s == nullptr || s->Destroyed || s->GateDeparted || s->Scrapped || s->Planetside() || s == c.Speaker) continue;
			double km = (world->Sensors->HelioOf(s, jd) - from).Length();
			if (km > DrawerAreaKm) continue;
			// At the controls, only what the unit held makes out; the god's view sees everything.
			auto level = c.Piloted ? Comms::Seen(c, s) : ContactLevel::Identified;
			if (level == ContactLevel::None) continue;
			found.push_back({ s, km, level });
		}
		std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.Km != b.Km ? a.Km < b.Km : a.Ship->Id < b.Ship->Id; });
		if (found.size() > static_cast<size_t>(DrawerMaxShips)) found.resize(DrawerMaxShips);
		drawerShipCount = static_cast<int>(found.size());

		DrawerItem head;
		head.heading = true;
		head.color = UiStyle::Accent;
		head.name = c.Piloted ? cs::Cat("WITHIN ", RangeText(DrawerAreaKm), "  ·  hailing range ", RangeText(Comms::HailRangeKm), "  ·  click or Enter hails")
			: cs::Cat("WITHIN ", RangeText(DrawerAreaKm), " OF THE VIEW  ·  take a ship, a gun or a station (P) to hail");
		drawerItems.push_back(head);
		if (found.empty())
		{
			DrawerItem none;
			none.heading = true;
			none.color = UiStyle::Dim;
			none.name = "Nothing on the sensors near here.";
			drawerItems.push_back(none);
			return;
		}

		auto mine = c.SpeakerFaction;
		for (const auto& f : found)
		{
			const auto& s = f.Ship;
			DrawerItem it;
			it.ship = s;
			it.target = Target::OfShip(s);
			it.indent = 1;
			bool identified = f.Level == ContactLevel::Identified;
			bool classed = static_cast<int>(f.Level) >= static_cast<int>(ContactLevel::Class);
			it.name = identified ? s->Name : Contact::Describe(s, f.Level);
			cs::List<cs::String> parts;
			parts.Add(RangeText(f.Km));
			if (identified && s->Faction != nullptr) parts.Add(s->Faction->Name);
			if (classed && s->Spec != nullptr) parts.Add(s->Spec->DisplayName);
			if (identified) parts.Add(RoleName(s->Role));
			if (s->Derelict) parts.Add("derelict");
			it.enabled = Comms::CanHail(c, s, &it.why);
			if (it.enabled) parts.Add("in hailing range");
			else if (c.Piloted) parts.Add(it.why);
			cs::String details;
			for (int i = 0; i < parts.Count(); i++) details = i == 0 ? parts[i] : cs::Cat(details, "  ·  ", parts[i]);
			it.details = details;
			it.color = identified && s->Faction != nullptr ? StanceColor(mine, s->Faction) : ContactGrey;
			if (!c.Piloted && identified && s->Faction != nullptr) it.color = Color(s->Faction->Color.R, s->Faction->Color.G, s->Faction->Color.B);
			drawerItems.push_back(it);
		}
	}

	void Hud::ActDrawerItem(int index)
	{
		if (index < 0 || index >= static_cast<int>(drawerItems.size())) return;
		auto it = drawerItems[index]; // a copy: what it does may rebuild the list
		if (it.heading) return;
		drawerRow = index;
		if (it.system != nullptr)
		{
			if (it.system == app->ActiveSystem) return;
			app->TravelTo(it.system); // at a helm, the course over the gates (U.SPC-T516)
			return;
		}
		if (it.ship != nullptr)
		{
			// In range, hailed (US-T201): the comms panel takes over, so the drawer lets itself down.
			if (it.enabled)
			{
				if (app->HailShip(it.ship)) drawerOpen = false;
				return;
			}
			app->Select(it.target);
			if (!app->AtControls()) app->SpaceCamera->FocusOn(it.target);
			else app->ShowToast(cs::Cat("Can't hail ", it.name, ": ", it.why));
			return;
		}
		if (it.target.IsNone()) return;
		if (app->IsPiloting()) app->CruiseTo(it.target);
		else
		{
			app->Select(it.target);
			if (!app->AtControls()) app->SpaceCamera->FocusOn(it.target); // US-T205: a seat keeps its own view
		}
	}

	// ---------------------------------------------------------------- drawing

	void Hud::DrawDrawerHandle()
	{
		auto r = Panel(layout.TakeBottomLeft(DrawerHandleW, 30));
		cs::String caption = cs::Cat(drawerOpen ? UiStyle::Glyph::FoldOpen : GlyphUp, "  DRAWER  [Shift+U]");
		if (GUI::Button(Rect(r.x + 4, r.y + 4, r.width - 8, 22), caption,drawerOpen ? buttonActive : button))
		{
			drawerOpen = !drawerOpen;
			drawerFollowRow = true;
		}
	}

	void Hud::DrawDrawer()
	{
		if (drawerShown <= 0.001f) return;
		// Rebuilt on a Layout pass only (a few times a second), so the Repaint after it places the same rows.
		if (Event::current().type == EventType::Layout && (drawerItems.empty() || Time::unscaledTime - drawerRefreshAt > 0.25f))
		{
			RebuildDrawerItems();
			drawerRefreshAt = Time::unscaledTime;
		}

		// Between the left edge and the right-hand column, its foot on the status line, pulled up to the step's height.
		float x0 = SafeLeft() + HudLayout::Margin;
		float x1 = Mathf::Min(layout.BottomColumnLeft(), SafeRight()) - HudLayout::Margin;
		if (x1 - x0 < 520) x1 = Mathf::Min(x0 + 520, SafeRight() - HudLayout::Margin);
		float foot = Screen::height - 40 - HudLayout::Gap;
		float room = Mathf::Max(120.0f, foot - (HudLayout::TopBarBottom + HudLayout::Margin));
		float full = Mathf::Max(120.0f, room *StepFraction[std::clamp(drawerStep, 0, DrawerSteps - 1)]);
		// Eased up from the foot.
		float t = drawerShown;
		float eased = 1 - (1 - t) * (1 - t);
		float h = Mathf::Max(36.0f, full * eased);
		auto r = Panel(Rect(x0, foot - h, x1 - x0, h));
		auto old = GUI::contentColor;

		// The grip and the tabs.
		float pad = UiStyle::Space::Pad;
		float x = r.x + pad, y = r.y + pad;
		const DrawerTab tabs[] = { DrawerTab::Navigation, DrawerTab::Ships };
		for (auto tab : tabs)
		{
			cs::String name = tab == DrawerTab::Navigation ? cs::String("NAVIGATION")
				: drawerTab == DrawerTab::Ships ? cs::Cat("SHIPS IN AREA  (", drawerShipCount, ")") : cs::String("SHIPS IN AREA");
			if (GUI::Button(Rect(x, y, 160, 22), name, tab == drawerTab ? buttonActive : button) && tab != drawerTab)
			{
				drawerTab = tab;
				drawerRow = 0;
				drawerScroll = Vector2::zero();
				drawerItems.clear();
				drawerRefreshAt = -10.0f;
			}
			x += 164;
		}
		// US-T258: the system map, this system drawn top-down (Shift+[).
		if (GUI::Button(Rect(x, y, 150, 22), "SYSTEM MAP  Shift+[", button)) app->ShowSystemMap = true;
		x += 154;
		// US-T271: the Encyclopedia, every article in one place.
		if (GUI::Button(Rect(x, y, 170, 22), cs::Cat("ENCYCLOPEDIA  ", Input::KeyBindings::Keys(Input::KeyAction::Encyclopedia)), button)) app->ShowEncyclopedia = true;
		x += 174;
		// The height: a step up or down (the keys' PgUp / PgDn), and close.
		float right = r.xMax() - pad;
		if (GUI::Button(Rect(right - 24, y, 24, 22), "X", button)) drawerOpen = false;
		if (GUI::Button(Rect(right - 52, y, 24, 22), UiStyle::Glyph::FoldOpen, button))
		{
			if (drawerStep == 0) drawerOpen = false;
			else drawerStep--;
		}
		if (GUI::Button(Rect(right - 80, y, 24, 22), GlyphUp, drawerStep >= DrawerSteps - 1 ? buttonActive : button))
			drawerStep = std::min(drawerStep + 1, DrawerSteps - 1);
		GUI::contentColor = UiStyle::Dim;
		float hintX = x + 8;
		if (right - 88 - hintX > 60)
			GUI::Label(Rect(hintX, y + 2, right - 88 - hintX, 18),
				FitText("Tab tabs  ·  Up/Down rows  ·  Enter acts  ·  PgUp/PgDn height  ·  Esc or Shift+U closes", small, right - 88 - hintX), small);
		GUI::contentColor = old;

		// The rows, scrolled.
		float top = y + 22 + UiStyle::Space::Tight + 2;
		if (r.yMax() - pad - top < DrawerRowH) return; // still coming up
		auto view = Rect(r.x + 4, top, r.width - 8, r.yMax() - pad + 4 - top);
		int n = static_cast<int>(drawerItems.size());
		auto content = Rect(0, 0, view.width - 18, n * DrawerRowH);
		if (drawerFollowRow && n > 0)
		{
			float rowY = drawerRow * DrawerRowH;
			if (rowY < drawerScroll.y) drawerScroll.y = rowY;
			if (rowY + DrawerRowH > drawerScroll.y + view.height) drawerScroll.y = rowY + DrawerRowH - view.height;
			drawerFollowRow = false;
		}
		drawerScroll = GUI::BeginScrollView(view, drawerScroll, content);
		for (int i = 0; i < n; i++)
		{
			const auto& it = drawerItems[i];
			auto rr = Rect(0, i * DrawerRowH, content.width, DrawerRowH);
			// Only the rows in sight are drawn (a long list scrolls).
			if (rr.yMax() < drawerScroll.y || rr.y > drawerScroll.y + view.height) continue;
			float indentX = it.indent * 14.0f;
			if (it.heading)
			{
				if (it.name.empty()) continue;
				GUI::contentColor = it.color;
				NameAndDetails(Rect(rr.x + 6 + indentX, rr.y + 2, rr.width - 6 - indentX, rr.height - 2), it.name, it.details, small);
				GUI::contentColor = old;
				continue;
			}
			bool cursor = i == drawerRow;
			bool selected = !it.target.IsNone() && app->Selected == it.target;
			auto style = cursor || selected ? rowSelected : row;
			Color tint = it.color;
			if (!it.enabled) tint = Color::Lerp(tint, UiStyle::Dim, 0.6f);
			GUI::contentColor = cursor ? Color::Lerp(tint, Color::white(), 0.5f) : tint;
			auto rowRect = Rect(rr.x + 4 + indentX, rr.y, rr.width - indentX - 4, rr.height);
			if (GUI::Button(rowRect, GUIContent::none(), style))
			{
				drawerRow = i;
				ActDrawerItem(i);
			}
			// ActDrawerItem may have rebuilt nothing (the list is rebuilt on Layout only), so `it` still stands.
			NameAndDetails(rowRect, it.name, it.details, style);
			GUI::contentColor = old;
		}
		GUI::EndScrollView();
		GUI::contentColor = old;
	}
}
