// Hud.Drawer.inl -- included INSIDE class Hud (see UI/Hud.h).
// US-T200: the bottom drawer, since US-T438 four drawers of the HUD's drawer system (UI/HudDrawers.h, docs/HUD.md); bodies
// in UI/Hud.Drawer.cpp, what it needs in UI/Hud.Drawer.deps.h.
//
// The four start out as one window of tabs docked along the bottom edge, between the left edge and the right-hand column,
// on the status line, closed to their edge tabs; like any drawer each can be pulled out, collapsed, pinned, torn off,
// docked elsewhere or grouped with others:
//   - Navigation: the other star systems (travel there, or plot the course over the gates at a helm) and this system's
//     worlds, docking rings and structures (cruise there at a helm, else select it and fly the view to it).
//   - Ships in area: every hull the unit held makes out near here, nearest first, with its range, faction, class and
//     role as far as the sensors tell them. A hull in hailing range is hailed by a click (UnitySpaceApp::HailShip,
//     US-T201); one that cannot be (Comms::CanHail) is greyed with why, and a click only selects it.
//   - Notices (US-T345): the last 100 notices shown here (UnitySpaceApp::Notices), newest first, each with its game time
//     and how many times in a row it showed, filtered by kind (all, combat, trade, society, system) with F or the
//     filter button. Shift+X opens it (and closes it from there).
//   - Objectives (US-T405): the war objectives the live wars offer the SpaceGod (LastingConsequences, US-T105), open
//     ones first with a Take it up / Turn it down row under each offered one (the calls sg.Objectives makes; single-player
//     only, as the console: in co-op they are shown greyed with why), then how the closed ones ended and when.
// Shift+U opens and closes the last of them used. One opened by its key has the keys while it shows (the comms panel's
// pattern): Tab / Shift+Tab or Left / Right the next of the four, Up / Down the rows, Enter acts on the row, PgUp / PgDn
// its window's size, Esc closes. The cursor is free meanwhile. Each keeps its own rows, cursor and scroll.
//
// US-T438 also makes the reactor (US-T94) and the sensors (US-T93) drawers, docked up the right edge, at the controls only.

public:
	/// <summary>The frame's keys for the drawers, before the commands are read. <paramref name="keysFree"/>: no other panel
	/// (the dev menu, comms) has the keys this frame. True when a drawer took the keys (the caller hands input over).</summary>
	bool UpdateDrawer(bool keysFree);

	/// <summary>Whether one of the four bottom drawers was opened by its key and has the keys (the cursor stays free while
	/// it does).</summary>
	bool DrawerOpen() const;

	/// <summary>US-T438: the HUD's drawers (their registry, windows and layout).</summary>
	HudDrawers& Drawers() { return drawers; }

	/// <summary>US-T438: the drawers' ids, for code that drives them (the screenshots' command, the tutorial).</summary>
	static constexpr const char* DrawerNavigationId = "navigation";
	static constexpr const char* DrawerShipsId = "ships";
	static constexpr const char* DrawerNoticesId = "notices";
	static constexpr const char* DrawerObjectivesId = "objectives";
	static constexpr const char* DrawerReactorId = "reactor";
	static constexpr const char* DrawerSensorsId = "sensors";

private:
	enum class DrawerTab : int { Navigation, Ships, Notices /* US-T345 */, Objectives /* US-T405 */ };
	static constexpr int DrawerTabCount = 4;

	/// One row of the drawer: a heading (no action), a star system, a place in this system, or a hull.
	struct DrawerItem
	{
		cs::String name;
		cs::String details;
		Color color;
		int indent = 0;
		bool heading = false;
		/// Can be acted on now (a hull: can be hailed). A row that cannot is greyed, and `why` says so.
		bool enabled = true;
		cs::String why;
		cs::Ref<Core::StarSystem> system;
		Target target = Target::None();
		cs::Ref<Core::Ship> ship;
		/// US-T405: a war objective's action row: the objective's Id, and whether it takes it up (else turns it down).
		int objective = 0;
		bool objectiveAccept = false;
	};

	/// US-T438: each bottom drawer's own rows, keyboard row and scroll (they can show side by side now).
	struct DrawerTabState
	{
		/// The keyboard's row, and the row list it indexes (rebuilt on Layout passes, read by UpdateDrawer).
		int row = 0;
		bool followRow = false;
		Vector2 scroll;
		std::vector<DrawerItem> items;
		float refreshAt = -10.0f;
	};

	HudDrawers drawers;
	bool drawersRegistered = false;
	DrawerTabState drawerTabs[DrawerTabCount];
	/// The bottom drawer the keys go to: the last opened by Shift+U / Shift+X or Tab.
	DrawerTab drawerTab = DrawerTab::Navigation;
	/// Whether that drawer was opened by its key and has the keys while it shows.
	bool drawerKeys = false;
	/// Where Add* fill rows (moved into the tab's state after).
	std::vector<DrawerItem> drawerItems;
	int drawerShipCount = 0;
	/// US-T345: the Notices tab's filter: 0 every kind, else 1 + UnitySpaceApp::NoticeKind's value (NoticeFilterCount in all).
	int noticeFilter = 0;
	static constexpr int NoticeFilterCount = 5;

	static constexpr float DrawerRowH = 19;
	/// The area the ships tab lists, km round the unit held (or the view): ten hailing ranges.
	static constexpr double DrawerAreaKm = 2.0e7;
	static constexpr int DrawerMaxShips = 60;

	/// The HUD is up and nothing full-screen has the view, so the drawer can show.
	bool DrawerAvailable() const;

	/// US-T438: the drawers registered (once, the first Draw or UpdateDrawer).
	void RegisterDrawers();
	/// US-T438: this pass's frame for the drawers: the mode, the screen, each edge's room, the HUD's Panel and styles.
	HudDrawers::Frame DrawerFrame();
	/// US-T438: before the right-hand column's panels: the right edge's docked drawers take their place in it (HudLayout's
	/// BottomRight), so the selection card stacks over them as it did over the reactor and the sensors.
	void ReserveDrawerEdges();

	static const char* DrawerTabId(DrawerTab tab);
	static const char* DrawerTabTitle(DrawerTab tab);

	/// One bottom drawer's content: its buttons and hint, then its rows, scrolled.
	void DrawDrawerTab(DrawerTab tab, Rect r);

	void RebuildDrawerItems(DrawerTab tab);
	void AddDrawerNavigation();
	void AddDrawerShips();
	/// US-T345: the Notices tab's rows, newest first, under the filter.
	void AddDrawerNotices();
	/// US-T405: the Objectives tab's rows: open objectives (each offered one with its two action rows), then closed ones.
	void AddDrawerObjectives();
	/// US-T405: how many closed objectives the tab lists, newest first.
	static constexpr int DrawerClosedObjectives = 30;

	/// US-T345: the drawer `tab` opened by its key, its rows and scroll back to the top.
	void SetDrawerTab(DrawerTab tab);

	/// US-T345: the filter's name (ALL, COMBAT, TRADE, SOCIETY, SYSTEM).
	static const char* NoticeFilterName(int filter);

	/// Acts on a row: travel, cruise, focus, or hail.
	void ActDrawerItem(DrawerTab tab, int index);

	/// The next row that can take the cursor from `from` going `dir` (headings are skipped).
	int DrawerStepRow(DrawerTab tab, int from, int dir) const;

	/// US-T438: the reactor's and the sensors' drawers: whether each can show now, and their content.
	bool ReactorAvailable() const;
	bool SensorsAvailable() const;
	void DrawReactorBody(Rect r);
	void DrawSensorsBody(Rect r);
