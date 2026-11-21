// US-T200: the bottom drawer (see UI/Hud.Drawer.inl for the declarations and the rule), four drawers of the HUD's drawer
// system since US-T438 (and the reactor's and the sensors' drawers registered beside them); Navigation lists the systems
// and this system's places, Ships in area the hulls about, a click hailing one in range (UnitySpaceApp::HailShip, US-T201).

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Input/InputDevice.h"
#include "Input/KeyBindings.h" // US-T249

#include "Astro.h" // US-T345
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
#include "Systems/LastingConsequences.h" // US-T405
#include "Coop/CoopPeer.h" // US-T405: objectives are taken up in single-player only, as sg.Objectives
#include "Embodiment/CraftPower.h" // US-T438: the reactor's drawer
#include "Embodiment/SelfDestruct.h"
#include "Sensors/SensorPosture.h" // US-T438: the sensors' drawer

#include <algorithm>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		cs::String RoleName(ShipRole r)
		{
			return cs::Strings::ToLowerInvariant(cs::String(Core::ToString(r)));
		}
	}

	bool Hud::DrawerAvailable() const
	{
		return app->ShowHud && !app->ScreenCleared && !app->ShowSpawn && !app->ShowHelp && !app->ShowLogs /* US-T261 */ && !app->ShowDiscoveries /* US-T259 */ && !app->ShowEncyclopedia /* US-T271 */ && !app->ShowSociety /* US-T293 */ && !(app->ShowGalaxyMap && !app->Travel->Active) && !(app->ShowSystemMap && !app->Travel->Active) /* US-T258 */;
	}

	// ---------------------------------------------------------------- the drawers (US-T438)

	const char* Hud::DrawerTabId(DrawerTab tab)
	{
		switch (tab)
		{
			case DrawerTab::Ships: return DrawerShipsId;
			case DrawerTab::Notices: return DrawerNoticesId;
			case DrawerTab::Objectives: return DrawerObjectivesId;
			default: return DrawerNavigationId;
		}
	}

	const char* Hud::DrawerTabTitle(DrawerTab tab)
	{
		switch (tab)
		{
			case DrawerTab::Ships: return "SHIPS IN AREA";
			case DrawerTab::Notices: return "NOTICES";
			case DrawerTab::Objectives: return "OBJECTIVES";
			default: return "NAVIGATION";
		}
	}

	void Hud::RegisterDrawers()
	{
		if (drawersRegistered) return;
		drawersRegistered = true;
		using HudDock::Edge;
		// The reactor (US-T94) and the sensors (US-T93): up the right edge from the bottom, the reactor first, at the controls.
		{
			DrawerSpec s;
			s.id = DrawerReactorId;
			s.title = "REACTOR";
			s.shortTitle = "PWR"; // US-T440: a tab's room in a narrow group
			s.edge = Edge::Right;
			s.size = Vector2(240, 158);
			s.available = [this] { return ReactorAvailable(); };
			s.draw = [this](Rect r) { DrawReactorBody(r); };
			s.alert = [this]
			{
				auto seat = app->HeldSeat();
				return seat != nullptr && SensorPosture::Offline(SelfDestruct::CombatOf(seat)) ? DrawerAlert{ 2, "OFFLINE" } : DrawerAlert();
			};
			drawers.Register(std::move(s));
		}
		{
			DrawerSpec s;
			s.id = DrawerSensorsId;
			s.title = "SENSORS";
			s.shortTitle = "SNS"; // US-T440
			s.edge = Edge::Right;
			s.size = Vector2(240, 82);
			s.available = [this] { return SensorsAvailable(); };
			s.draw = [this](Rect r) { DrawSensorsBody(r); };
			s.alert = [this]
			{
				auto ship = app->Piloted.Ship;
				auto c = ship != nullptr ? ship->Combat : nullptr;
				if (c == nullptr) return DrawerAlert();
				if (SensorPosture::Lit(c.get())) return DrawerAlert{ 2, "LIT UP" }; // every sensor in reach hears the hull
				if (SensorPosture::StateOf(c) == PowerState::Restarting) return DrawerAlert{ 1, "RESTARTING" };
				return DrawerAlert();
			};
			drawers.Register(std::move(s));
		}
		// US-T440: every other panel's drawer, at its default edge (the reactor and the sensors stay first up the right edge).
		RegisterPanelDrawers();
		// The bottom drawer's four, one window of tabs along the bottom edge (filling it), closed to begin with as it was; last on
		// that edge, so pulled out it stands over the status line and the helm's lines (US-T440).
		for (int t = 0; t < DrawerTabCount; t++)
		{
			auto tab = static_cast<DrawerTab>(t);
			DrawerSpec s;
			s.id = DrawerTabId(tab);
			s.title = DrawerTabTitle(tab);
			s.edge = Edge::Bottom;
			s.size = Vector2(0, 420);
			s.shown = false;
			s.group = "bottom";
			s.available = [this] { return DrawerAvailable(); };
			s.draw = [this, tab](Rect r) { DrawDrawerTab(tab, r); };
			if (tab == DrawerTab::Ships) s.title = "SHIPS";
			s.shortTitle = tab == DrawerTab::Navigation ? "NAV" : tab == DrawerTab::Ships ? "SHIPS" : tab == DrawerTab::Notices ? "NOTES" : "OBJ"; // US-T440
			if (tab == DrawerTab::Objectives) // US-T405: the offers waiting on the SpaceGod
				s.alert = [this]
				{
					DrawerAlert a;
					auto lc = app->Galaxy != nullptr ? LastingConsequences::Find(*app->Galaxy) : nullptr;
					if (lc == nullptr) return a;
					int offered = 0;
					for (const auto& o : lc->Objectives)
						if (o != nullptr && o->Status == GodObjectiveStatus::Offered) offered++;
					if (offered > 0) a = DrawerAlert{ 1, cs::Cat(offered, offered == 1 ? " offer" : " offers") };
					return a;
				};
			drawers.Register(std::move(s));
		}
		drawers.ResetLayout();
	}

	HudDrawers::Frame Hud::DrawerFrame()
	{
		HudDrawers::Frame f;
		f.hudMode = app->HudModeOn(); // US-T439: the minimal HUD shows the pinned drawers and the urgent tabs only
		f.screenW = static_cast<float>(Screen::width);
		f.screenH = static_cast<float>(Screen::height);
		float left = SafeLeft() + HudLayout::Margin, right = SafeRight() - HudLayout::Margin;
		float top = HudLayout::Margin, foot = f.screenH - HudLayout::Margin;
		using HudDock::Box;
		using HudDock::Edge;
		// US-T440: every panel is a drawer, so the edges' bands are the screen less its margins: the top bar and the navigator
		// are drawers at the top of the top and left edges, the status line at the foot of the bottom edge.
		// The left edge stops over the status line's window (its foot stack, the cargo hold, opened over it as it always did).
		float leftFoot = foot - 64;
		f.bands[static_cast<int>(Edge::Left)] = Box(left, top, Mathf::Max(0.0f, right - left), Mathf::Max(0.0f, leftFoot - top));
		f.bands[static_cast<int>(Edge::Top)] = Box(left, top, Mathf::Max(0.0f, right - left), Mathf::Max(0.0f, foot - top));
		// The right edge: the right-hand column, from over the dev menu's hint up to the hold rewind's line (the readout itself
		// is the first drawer down from its top, where the line always was).
		float rightTop = 44, rightBottom = f.screenH - HudLayout::Margin - HudLayout::DevHintStrip;
		f.bands[static_cast<int>(Edge::Right)] = Box(left, rightTop, Mathf::Max(0.0f, right - left), Mathf::Max(0.0f, rightBottom - rightTop));
		// The bottom edge: from the left edge to the right-hand column's foot stack (the reactor, the sensors, the cards over
		// them) as the last pass laid it out, on the screen's foot.
		float x1 = right;
		for (const auto& w : drawers.Windows())
			if (w.drawn && w.docked && w.edge == Edge::Right && !w.far) x1 = Mathf::Min(x1, w.rect.x - HudLayout::Margin);
		x1 = Mathf::Min(x1, f.screenW - HudLayout::DevHintWidth - HudLayout::Margin);
		if (x1 - left < 520) x1 = Mathf::Min(left + 520, right);
		f.bands[static_cast<int>(Edge::Bottom)] = Box(left, top, Mathf::Max(0.0f, x1 - left), Mathf::Max(0.0f, foot - top));
		bottomBandW = Mathf::Max(0.0f, x1 - left);
		f.panel = [this](Rect r) { return Panel(r); };
		f.registerUi = [this](Rect r) { RegisterUi(r); };
		f.styles = HudDrawers::Styles{ panel, title, small, button, buttonActive };
		return f;
	}

	bool Hud::DrawerOpen() const
	{
		return drawerKeys && drawers.Visible(DrawerTabId(drawerTab));
	}

	bool Hud::UpdateDrawer(bool keysFree)
	{
		RegisterDrawers();
		float dt = Mathf::Min(Time::unscaledDeltaTime, 0.1f);
		bool available = DrawerAvailable();
		drawers.Tick(dt, app->HudModeOn()); // US-T439
		// The keys are the drawer's while the one opened by its key shows.
		if (drawerKeys && !drawers.Visible(DrawerTabId(drawerTab))) drawerKeys = false;
		if (!keysFree || !available) return false;

		const Input::InputDevice& kb = Input::InputDevice::current();
		auto pressed = [&](Input::Key k) { return kb[k].wasPressedThisFrame; };
		bool shift = kb[Input::Key::LeftShift].isPressed || kb[Input::Key::RightShift].isPressed;
		auto open = [&](DrawerTab tab)
		{
			if (tab != drawerTab) SetDrawerTab(tab);
			drawers.Open(DrawerTabId(tab), true);
			drawerKeys = true;
			drawerTabs[static_cast<int>(tab)].followRow = true;
		};
		// US-T345: Shift+X (unless rebound) opens the notice history, and closes it from there.
		if (Input::KeyBindings::Pressed(Input::KeyAction::Notices, kb))
		{
			if (drawers.Visible(DrawerNoticesId)) { drawers.Close(DrawerNoticesId); drawerKeys = false; }
			else open(DrawerTab::Notices);
			return true; // the key is spent (X alone is the news feed's: it does not go off under it)
		}
		if (Input::KeyBindings::Pressed(Input::KeyAction::Drawer, kb)) // US-T249: Shift+U unless rebound
		{
			if (drawers.Visible(DrawerTabId(drawerTab))) { drawers.Close(DrawerTabId(drawerTab)); drawerKeys = false; }
			else open(drawerTab);
			return true; // the key is spent (U alone is the HUD's: it does not go off under it)
		}
		if (!DrawerOpen()) return false;

		auto id = DrawerTabId(drawerTab);
		auto& st = drawerTabs[static_cast<int>(drawerTab)];
		if (pressed(Input::Key::Escape))
		{
			drawers.Close(id);
			drawerKeys = false;
			return true;
		}
		bool back = (pressed(Input::Key::Tab) && shift) || pressed(Input::Key::LeftArrow);
		bool next = (pressed(Input::Key::Tab) && !shift) || pressed(Input::Key::RightArrow);
		if (back || next) // US-T345, US-T405: the four round in either direction
		{
			open(static_cast<DrawerTab>((static_cast<int>(drawerTab) + (next ? 1 : DrawerTabCount - 1)) % DrawerTabCount));
			return true;
		}
		// US-T345: F (Shift+F back) steps the notice history's filter.
		if (drawerTab == DrawerTab::Notices && pressed(Input::Key::F))
		{
			noticeFilter = (noticeFilter + (shift ? NoticeFilterCount - 1 : 1)) % NoticeFilterCount;
			st = DrawerTabState();
		}
		// PgUp / PgDn: its window a step bigger or smaller (smaller than the least closes it, as the drawer's lowest step did).
		if (pressed(Input::Key::PageUp)) drawers.Grow(id, 1);
		if (pressed(Input::Key::PageDown) && !drawers.Grow(id, -1))
		{
			drawers.Close(id);
			drawerKeys = false;
			return true;
		}
		if (pressed(Input::Key::DownArrow)) { st.row = DrawerStepRow(drawerTab, st.row, 1); st.followRow = true; }
		if (pressed(Input::Key::UpArrow)) { st.row = DrawerStepRow(drawerTab, st.row, -1); st.followRow = true; }
		if (pressed(Input::Key::Enter) || pressed(Input::Key::NumpadEnter)) ActDrawerItem(drawerTab, st.row);
		return true; // open by its key, the drawer has the keys (the comms panel's pattern)
	}

	int Hud::DrawerStepRow(DrawerTab tab, int from, int dir) const
	{
		const auto& items = drawerTabs[static_cast<int>(tab)].items;
		int n = static_cast<int>(items.size());
		if (n == 0) return 0;
		int i = std::clamp(from, 0, n - 1);
		for (int j = i + dir; j >= 0 && j < n; j += dir)
			if (!items[j].heading) return j;
		return i;
	}

	// ---------------------------------------------------------------- rows

	void Hud::RebuildDrawerItems(DrawerTab tab)
	{
		drawerItems.clear();
		if (tab == DrawerTab::Navigation) AddDrawerNavigation();
		else if (tab == DrawerTab::Notices) AddDrawerNotices(); // US-T345
		else if (tab == DrawerTab::Objectives) AddDrawerObjectives(); // US-T405
		else AddDrawerShips();
		auto& st = drawerTabs[static_cast<int>(tab)];
		st.items.swap(drawerItems);
		drawerItems.clear();
		// The keyboard's row on a row that can take it.
		int n = static_cast<int>(st.items.size());
		if (n == 0) { st.row = 0; return; }
		st.row = std::clamp(st.row, 0, n - 1);
		if (st.items[st.row].heading) st.row = DrawerStepRow(tab, st.row, 1);
		if (st.items[st.row].heading) st.row = DrawerStepRow(tab, st.row, -1);
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

	// US-T345: the notice history.
	const char* Hud::NoticeFilterName(int filter)
	{
		switch (filter)
		{
			case 1: return "SYSTEM";
			case 2: return "COMBAT";
			case 3: return "TRADE";
			case 4: return "SOCIETY";
			default: return "ALL";
		}
	}

	void Hud::SetDrawerTab(DrawerTab tab)
	{
		drawerTab = tab;
		auto& st = drawerTabs[static_cast<int>(tab)];
		st.row = 0;
		st.scroll = Vector2::zero();
		st.items.clear();
		st.refreshAt = -10.0f;
	}

	void Hud::AddDrawerNotices()
	{
		using NoticeKind = UnitySpaceApp::NoticeKind;
		const auto& all = app->Notices();
		auto kindName = [](NoticeKind k)
		{
			switch (k)
			{
				case NoticeKind::Combat: return "combat";
				case NoticeKind::Trade: return "trade";
				case NoticeKind::Society: return "society";
				default: return "system";
			}
		};
		auto kindColor = [](NoticeKind k)
		{
			switch (k)
			{
				case NoticeKind::Combat: return UiStyle::Danger;
				case NoticeKind::Trade: return UiStyle::Warning;
				case NoticeKind::Society: return UiStyle::Good;
				default: return UiStyle::Text;
			}
		};
		int shown = 0;
		for (int i = 0; i < all.Count(); i++)
			if (noticeFilter == 0 || static_cast<int>(all[i].Kind) + 1 == noticeFilter) shown++;

		DrawerItem head;
		head.heading = true;
		head.color = UiStyle::Accent;
		head.name = cs::Cat("LAST ", UnitySpaceApp::NoticesKept, " NOTICES  ·  ", NoticeFilterName(noticeFilter), " (", shown, " of ", all.Count(),
			")  ·  F or the filter button steps the kind  ·  newest first");
		drawerItems.push_back(head);
		if (shown == 0)
		{
			DrawerItem none;
			none.heading = true;
			none.color = UiStyle::Dim;
			none.name = all.Count() == 0 ? cs::String("No notices yet this session.") : cs::String("No notices of this kind yet.");
			drawerItems.push_back(none);
			return;
		}
		int year = app->Clock != nullptr ? app->Clock->Utc().Year() : 0;
		for (int i = all.Count() - 1; i >= 0; i--)
		{
			const auto& n = all[i];
			if (noticeFilter != 0 && static_cast<int>(n.Kind) + 1 != noticeFilter) continue;
			DrawerItem it;
			it.name = n.Text;
			it.indent = 1;
			it.color = kindColor(n.Kind);
			cs::String when = n.Jd <= 0 ? cs::String("?")
				: Astro::ToDateTime(n.Jd).ToString(Astro::ToDateTime(n.Jd).Year() == year ? "MMM dd HH:mm" : "yyyy MMM dd HH:mm");
			it.details = n.Repeats > 1 ? cs::Cat("x", n.Repeats, "  ·  ", kindName(n.Kind), "  ·  ", when) : cs::Cat(kindName(n.Kind), "  ·  ", when);
			drawerItems.push_back(it);
		}
	}

	// US-T405: the war objectives (LastingConsequences, US-T105), read only here: Find, not Of, so drawing it makes nothing.
	void Hud::AddDrawerObjectives()
	{
		auto heading = [&](const cs::String& name, const cs::String& details, Color c, int indent)
		{
			DrawerItem h;
			h.name = name;
			h.details = details;
			h.color = c;
			h.heading = true;
			h.indent = indent;
			drawerItems.push_back(h);
		};
		auto lc = app->Galaxy != nullptr ? LastingConsequences::Find(*app->Galaxy) : nullptr;
		int year = app->Clock != nullptr ? app->Clock->Utc().Year() : 0;
		auto when = [&](double jd) -> cs::String
		{
			if (!(jd > 0)) return cs::String("?");
			auto t = Astro::ToDateTime(jd);
			return t.ToString(t.Year() == year ? "MMM dd" : "yyyy MMM dd");
		};
		auto where = [](const cs::Ref<GodObjective>& o) { return o->System != nullptr ? cs::Cat("in ", o->System->Name) : cs::String(); };
		bool coop = Coop::CoopPeer::Current() != nullptr;

		cs::List<cs::Ref<GodObjective>> open, closed;
		if (lc != nullptr)
			for (const auto& o : lc->Objectives)
			{
				if (o == nullptr) continue;
				if (o->Open()) open.Add(o);
				else closed.Add(o);
			}

		heading(cs::Cat("OPEN WAR OBJECTIVES  (", open.Count(), ")"),
			coop ? cs::String("co-op: taken up in single-player only, as sg.Objectives") : cs::String("Enter or a click takes the row's choice"), UiStyle::Accent, 0);
		if (open.Count() == 0)
			heading("No war offers the SpaceGod anything now.", cs::String(), UiStyle::Dim, 1);
		for (const auto& o : open)
		{
			bool offered = o->Status == GodObjectiveStatus::Offered;
			cs::String state = offered ? cs::Cat("offered ", when(o->OfferedJd)) : cs::Cat("taken up ", when(o->AcceptedJd), ": the war still runs");
			cs::String at = where(o);
			heading(LastingConsequences::Describe(o), at.empty() ? state : cs::Cat(at, "  ·  ", state), offered ? UiStyle::Text : UiStyle::Good, 1);
			if (!offered) continue;
			for (int a = 0; a < 2; a++)
			{
				DrawerItem it;
				it.objectiveAccept = a == 0;
				it.objective = o->Id;
				it.indent = 2;
				it.name = it.objectiveAccept ? cs::String("Take it up") : cs::String("Turn it down");
				it.details = it.objectiveAccept ? cs::String("side with them in this war; the war's other offers are set aside") : cs::String("let it pass");
				it.enabled = !coop;
				if (coop) it.why = "single-player only";
				it.color = it.objectiveAccept ? UiStyle::Good : UiStyle::Warning;
				drawerItems.push_back(it);
			}
		}

		heading(cs::Cat("CLOSED  (", closed.Count(), ")"), closed.Count() > DrawerClosedObjectives ? cs::Cat("the latest ", DrawerClosedObjectives) : cs::String(), UiStyle::Accent, 0);
		if (closed.Count() == 0)
			heading("None yet.", cs::String(), UiStyle::Dim, 1);
		int listed = 0;
		for (int i = closed.Count() - 1; i >= 0 && listed < DrawerClosedObjectives; i--, listed++)
		{
			const auto& o = closed[i];
			cs::String how;
			Color c = UiStyle::Dim;
			switch (o->Status)
			{
				case GodObjectiveStatus::Completed: how = "carried through"; c = UiStyle::Good; break;
				case GodObjectiveStatus::Failed: how = "failed: the war went the other way"; c = UiStyle::Danger; break;
				case GodObjectiveStatus::Declined: how = "turned down"; break;
				case GodObjectiveStatus::Lapsed: how = "lapsed: never taken up"; break;
				default: how = cs::String(Core::ToString(o->Status)); break;
			}
			cs::String at = where(o);
			cs::String ended = cs::Cat(how, " ", when(o->ResolvedJd));
			heading(LastingConsequences::Describe(o), at.empty() ? ended : cs::Cat(at, "  ·  ", ended), c, 1);
		}
	}

	void Hud::ActDrawerItem(DrawerTab tab, int index)
	{
		auto& st = drawerTabs[static_cast<int>(tab)];
		if (index < 0 || index >= static_cast<int>(st.items.size())) return;
		auto it = st.items[index]; // a copy: what it does may rebuild the list
		if (it.heading) return;
		st.row = index;
		// US-T405: a war objective's choice, through the calls sg.Objectives makes (single-player only, as there).
		if (it.objective != 0)
		{
			if (!it.enabled)
			{
				app->ShowToast(cs::Cat("Objectives are taken up in ", it.why));
				return;
			}
			if (app->Galaxy == nullptr || app->Clock == nullptr) return;
			auto lc = LastingConsequences::Of(*app->Galaxy);
			auto o = lc->ObjectiveById(it.objective);
			cs::String why = it.objectiveAccept ? lc->Accept(*app->Galaxy, o, app->Clock->Jd) : lc->Decline(o, app->Clock->Jd);
			app->ShowToast(why.empty()
				? cs::Cat(it.objectiveAccept ? "Objective taken up: " : "Objective declined: ", LastingConsequences::Describe(o))
				: cs::Cat("Can't: ", why));
			// Rebuilt on the next Layout pass (not cleared here: the drawing loop that clicked still holds its row).
			st.refreshAt = -10.0f;
			return;
		}
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
				if (app->HailShip(it.ship))
				{
					drawers.Close(DrawerTabId(tab));
					drawerKeys = false;
				}
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

	void Hud::DrawDrawerTab(DrawerTab tab, Rect r)
	{
		auto& st = drawerTabs[static_cast<int>(tab)];
		// Rebuilt on a Layout pass only (a few times a second), so the Repaint after it places the same rows.
		if (Event::current().type == EventType::Layout && (st.items.empty() || Time::unscaledTime - st.refreshAt > 0.25f))
		{
			RebuildDrawerItems(tab);
			st.refreshAt = Time::unscaledTime;
		}
		auto old = GUI::contentColor;

		// The tab's buttons: the Notices filter, then the full screens a drawer opens.
		float x = r.x, y = r.y + 2;
		float right = r.xMax();
		auto fits = [&](float w) { return x + w <= right; };
		// US-T345: on the Notices tab, its filter (a click steps it, as F does).
		if (tab == DrawerTab::Notices && fits(140))
		{
			if (GUI::Button(Rect(x, y, 140, 22), cs::Cat("KIND: ", NoticeFilterName(noticeFilter), "  F"), noticeFilter != 0 ? buttonActive : button))
			{
				noticeFilter = (noticeFilter + 1) % NoticeFilterCount;
				st = DrawerTabState();
			}
			x += 144;
		}
		// US-T258: the system map, this system drawn top-down (Shift+[).
		if (fits(150))
		{
			if (GUI::Button(Rect(x, y, 150, 22), "SYSTEM MAP  Shift+[", button)) app->ShowSystemMap = true;
			x += 154;
		}
		// US-T271: the Encyclopedia, every article in one place.
		if (fits(170))
		{
			if (GUI::Button(Rect(x, y, 170, 22), cs::Cat("ENCYCLOPEDIA  ", Input::KeyBindings::Keys(Input::KeyAction::Encyclopedia)), button)) app->ShowEncyclopedia = true;
			x += 174;
		}
		// US-T293: the Society screen, the galaxy's people and the SpaceGod's hand in their affairs.
		if (fits(150))
		{
			if (GUI::Button(Rect(x, y, 150, 22), cs::Cat("SOCIETY  ", Input::KeyBindings::Keys(Input::KeyAction::Society)), button)) app->ShowSociety = true;
			x += 154;
		}
		GUI::contentColor = UiStyle::Dim;
		float hintX = x + 8;
		if (right - hintX > 60)
		{
			cs::String count = tab == DrawerTab::Ships ? cs::Cat(drawerShipCount, " in sight  ·  ") : cs::String();
			cs::String keys = tab == DrawerTab::Notices // US-T345
				? cs::String("Tab next  ·  Up/Down rows  ·  F kind  ·  PgUp/PgDn size  ·  Esc or Shift+U closes")
				: tab == DrawerTab::Objectives // US-T405
				? cs::String("Tab next  ·  Up/Down rows  ·  Enter takes the choice  ·  PgUp/PgDn size  ·  Esc or Shift+U closes")
				: cs::String("Tab next  ·  Up/Down rows  ·  Enter acts  ·  PgUp/PgDn size  ·  Esc or Shift+U closes");
			GUI::Label(Rect(hintX, y + 2, right - hintX, 18), FitText(cs::Cat(count, keys), small, right - hintX), small);
		}
		GUI::contentColor = old;

		// The rows, scrolled.
		float top = y + 22 + UiStyle::Space::Tight + 2;
		if (r.yMax() - top < DrawerRowH) return; // still coming out
		auto view = Rect(r.x, top, r.width, r.yMax() - top);
		int n = static_cast<int>(st.items.size());
		auto content = Rect(0, 0, view.width - 18, n * DrawerRowH);
		if (st.followRow && n > 0)
		{
			float rowY = st.row * DrawerRowH;
			if (rowY < st.scroll.y) st.scroll.y = rowY;
			if (rowY + DrawerRowH > st.scroll.y + view.height) st.scroll.y = rowY + DrawerRowH - view.height;
			st.followRow = false;
		}
		bool keyed = drawerKeys && drawerTab == tab;
		st.scroll = GUI::BeginScrollView(view, st.scroll, content);
		for (int i = 0; i < n; i++)
		{
			const auto& it = st.items[i];
			auto rr = Rect(0, i * DrawerRowH, content.width, DrawerRowH);
			// Only the rows in sight are drawn (a long list scrolls).
			if (rr.yMax() < st.scroll.y || rr.y > st.scroll.y + view.height) continue;
			float indentX = it.indent * 14.0f;
			if (it.heading)
			{
				if (it.name.empty()) continue;
				GUI::contentColor = it.color;
				NameAndDetails(Rect(rr.x + 6 + indentX, rr.y + 2, rr.width - 6 - indentX, rr.height - 2), it.name, it.details, small);
				GUI::contentColor = old;
				continue;
			}
			bool cursor = keyed && i == st.row;
			bool selected = !it.target.IsNone() && app->Selected == it.target;
			auto style = cursor || selected ? rowSelected : row;
			Color tint = it.color;
			if (!it.enabled) tint = Color::Lerp(tint, UiStyle::Dim, 0.6f);
			GUI::contentColor = cursor ? Color::Lerp(tint, Color::white(), 0.5f) : tint;
			auto rowRect = Rect(rr.x + 4 + indentX, rr.y, rr.width - indentX - 4, rr.height);
			if (GUI::Button(rowRect, GUIContent::none(), style))
			{
				st.row = i;
				ActDrawerItem(tab, i);
			}
			// ActDrawerItem may have rebuilt nothing (the list is rebuilt on Layout only), so `it` still stands.
			NameAndDetails(rowRect, it.name, it.details, style);
			GUI::contentColor = old;
		}
		GUI::EndScrollView();
		GUI::contentColor = old;
	}
}
