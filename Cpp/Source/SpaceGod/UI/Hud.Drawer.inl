// Hud.Drawer.inl -- included INSIDE class Hud (see UI/Hud.h).
// US-T200: the bottom drawer; bodies in UI/Hud.Drawer.cpp, what it needs in UI/Hud.Drawer.deps.h.
//
// A handle at the bottom left of the status band (HudLayout::TakeBottomLeft) pulls a drawer up from the bottom of the
// screen, over the view, between the left edge and the right-hand column. It grows taller a step at a time (PgUp /
// PgDn, or the handle's grip) and has two tabs:
//   - Navigation: the other star systems (travel there, or plot the course over the gates at a helm) and this system's
//     worlds, docking rings and structures (cruise there at a helm, else select it and fly the view to it).
//   - Ships in area: every hull the unit held makes out near here, nearest first, with its range, faction, class and
//     role as far as the sensors tell them. A hull in hailing range is hailed by a click (UnitySpaceApp::HailShip,
//     US-T201); one that cannot be (Comms::CanHail) is greyed with why, and a click only selects it.
// Shift+U opens and closes it. While open it has the keys (the comms panel's pattern): Tab / Shift+Tab or Left / Right
// the tabs, Up / Down the rows, Enter acts on the row, PgUp / PgDn the height, Esc closes. The cursor is free meanwhile.

public:
	/// <summary>The frame's keys for the drawer, before the commands are read. <paramref name="keysFree"/>: no other panel
	/// (the dev menu, comms) has the keys this frame. True when the drawer took the keys (the caller hands input over).</summary>
	bool UpdateDrawer(bool keysFree);

	/// <summary>Whether the drawer is pulled up (the cursor stays free while it is).</summary>
	bool DrawerOpen() const { return drawerOpen; }

private:
	enum class DrawerTab : int { Navigation, Ships };

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
	};

	bool drawerOpen = false;
	DrawerTab drawerTab = DrawerTab::Navigation;
	/// Which of the height steps the drawer is pulled up to (DrawerSteps), and how far up it is drawn now (0..1 of it).
	int drawerStep = 1;
	float drawerShown = 0;
	/// The keyboard's row in the tab, and the row list it indexes (rebuilt on each Layout pass, read by UpdateDrawer).
	int drawerRow = 0;
	bool drawerFollowRow = false;
	Vector2 drawerScroll;
	std::vector<DrawerItem> drawerItems;
	float drawerRefreshAt = -10.0f;
	int drawerShipCount = 0;

	static constexpr int DrawerSteps = 3;
	static constexpr float DrawerHandleW = 150, DrawerRowH = 19;
	/// The area the ships tab lists, km round the unit held (or the view): ten hailing ranges.
	static constexpr double DrawerAreaKm = 2.0e7;
	static constexpr int DrawerMaxShips = 60;

	/// The HUD is up and nothing full-screen has the view, so the drawer can show.
	bool DrawerAvailable() const;

	/// The drawer's handle, in the status band's bottom-left slot (taken before the status line asks for its band).
	void DrawDrawerHandle();

	/// The drawer itself, pulled up over the view; drawn after the docked panels so it lies over them.
	void DrawDrawer();

	void RebuildDrawerItems();
	void AddDrawerNavigation();
	void AddDrawerShips();

	/// Acts on a row: travel, cruise, focus, or hail.
	void ActDrawerItem(int index);

	/// The next row that can take the cursor from `from` going `dir` (headings are skipped).
	int DrawerStepRow(int from, int dir) const;
