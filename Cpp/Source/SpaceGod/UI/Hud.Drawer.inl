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
	/// US-T440: every other HUD panel's drawer (UI/Hud.DrawerPanels.cpp registers them; docs/HUD.md lists them).
	static constexpr const char* DrawerTopBarId = "topbar";
	static constexpr const char* DrawerNewsId = "news";
	static constexpr const char* DrawerTickerId = "ticker";
	static constexpr const char* DrawerRewindBarId = "rewindbar";
	static constexpr const char* DrawerCruiseId = "cruise";
	static constexpr const char* DrawerNavigatorId = "navigator";
	static constexpr const char* DrawerInfluenceId = "influence";
	static constexpr const char* DrawerCargoId = "cargo";
	static constexpr const char* DrawerHoldRewindId = "holdrewind";
	static constexpr const char* DrawerOpportunitiesId = "opportunities";
	static constexpr const char* DrawerMarketId = "market";
	static constexpr const char* DrawerHarvestId = "harvest";
	static constexpr const char* DrawerHuntersId = "hunters";
	static constexpr const char* DrawerSelectionId = "selection";
	static constexpr const char* DrawerTargetId = "target";
	static constexpr const char* DrawerStatusId = "status";
	static constexpr const char* DrawerSeatId = "seat";
	static constexpr const char* DrawerHelmId = "helm";
	static constexpr const char* DrawerTractorId = "tractor";
	static constexpr const char* DrawerLaneRideId = "laneride";
	static constexpr const char* DrawerInterdictionId = "interdiction";
	static constexpr const char* DrawerFtlId = "ftl";
	static constexpr const char* DrawerBountiesId = "bounties";
	static constexpr const char* DrawerContractsId = "contracts";
	static constexpr const char* DrawerFleetId = "fleet";
	static constexpr const char* DrawerOutfitId = "outfit";
	static constexpr const char* DrawerTradeId = "trade";
	static constexpr const char* DrawerPortId = "port";
	static constexpr const char* DrawerPickerId = "picker";
	static constexpr const char* DrawerSavesId = "saves";
	static constexpr const char* DrawerCommsId = "comms";
	static constexpr const char* DrawerTutorialId = "tutorial";

	/// <summary>US-T440: the rect a panel made a drawer draws in, from inside its drawer's draw callback: the content rect its
	/// window was handed (it asks for w x h, which sizes its window from the next pass). The app's own panels (comms, the
	/// cruise and tractor readouts, Influence, the rewind's, the tutorial's card) call it too.</summary>
	Rect Place(float w, float h);
	/// <summary>US-T440: the most content height the drawing drawer could have (what its edge leaves it).</summary>
	float PlaceRoom() const;

	// ---- US-T441: layout presets (UI/Hud.Layouts.cpp; docs/HUD.md, "Layouts")
	// A preset is a whole drawer layout (HudDrawers::Save's JSON) under a name. Five are built in (Default, today's layout;
	// Combat, Commerce, Piracy, Exploration: each a recipe on the default), and the player saves their own, or a copy of a
	// built-in over it (reset puts the built-in back). Kept per profile in settings.json's game block, never in a save or
	// co-op: game.hudLayouts = { active, slots: [5 names], presets: { name: layout } }. The Layouts drawer lists them.
	static constexpr const char* DrawerLayoutsId = "layouts";
	static constexpr int LayoutSlots = 5;
	/// The built-in presets, in the list's order.
	static const std::vector<cs::String>& BuiltinLayouts();
	static bool IsBuiltinLayout(const cs::String& name);
	/// Every preset: the built-ins, then the player's own as they were made.
	std::vector<cs::String> LayoutNames();
	/// The player's chosen preset (the one the HUD goes back to after a situation's).
	cs::String ActiveLayout();
	/// The preset on screen now (a situation's, while one holds and the option is on).
	const cs::String& ShownLayout() const { return layoutShown; }
	/// A built-in the player saved a copy of (reset puts it back).
	bool LayoutOverridden(const cs::String& name);
	/// Puts the preset on screen; <paramref name="makeActive"/>: and makes it the player's chosen one. Drawers opened by a key
	/// (the market, the cargo hold, the boards, the port) and the Layouts drawer keep whether they are open. False: no such preset.
	bool ApplyLayout(const cs::String& name, bool makeActive = true);
	/// The layout on screen saved as <paramref name="name"/> (over the player's own of that name, or as a copy of a built-in).
	bool SaveLayout(const cs::String& name);
	/// A player's own preset (or a built-in's copy, the built-in coming back) under a new name, not a built-in's or another's.
	bool RenameLayout(const cs::String& from, const cs::String& to);
	/// A player's own preset gone (a built-in cannot be; reset its copy instead).
	bool DeleteLayout(const cs::String& name);
	/// A built-in's copy dropped: the built-in as it ships (on screen again when it is the one shown).
	bool ResetLayoutPreset(const cs::String& name);
	/// The preset a Layout 1-5 key (slot 0-4) switches to ("" when the slot is free), and setting it (a name in one slot only).
	cs::String LayoutInSlot(int slot);
	void SetLayoutSlot(int slot, const cs::String& name);
	/// A Layout 1-5 key pressed: its preset applied and made the chosen one.
	void LayoutKey(int slot);
	/// The whole game.hudLayouts block as JSON text, and put back (sg.HudLayout leaves the profile as it found it).
	cs::String LayoutsJson();
	void RestoreLayoutsJson(const cs::String& json);
	/// What a preset name may be: trimmed, 1 to 24 characters ("" when it is none).
	static cs::String CleanLayoutName(const cs::String& name);

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

	// ---- US-T440: every panel a drawer (UI/Hud.DrawerPanels.cpp)
	/// The drawer drawing now (its content rect), for Place.
	cs::String placeId;
	Rect placeRect;
	/// The width the bottom edge's band had in the last frame (the status line shortens its text to fit it).
	float bottomBandW = 1200;
	/// A panel's flag (app->ShowMarket...) and its drawer kept in step: the flag opens and closes it, its edge tab and its
	/// close button set the flag.
	struct FlagLink
	{
		cs::String id;
		bool* flag = nullptr;
		bool lastFlag = false, lastShown = false;
	};
	std::vector<FlagLink> flagLinks;
	int flagsSyncedFrame = -1;
	/// Registers a panel as a fit drawer whose draw callback runs `body` (which calls Place); `flag`: kept in step with it.
	void AddPanel(DrawerSpec s, std::function<void()> body, bool* flag = nullptr);
	/// Every panel's drawer and its default edge (called from RegisterDrawers).
	void RegisterPanelDrawers();
	/// Once a frame: the flags and their drawers in step.
	void SyncPanelFlags();
	/// After the drawers drew: the right edge's inner bound published (HudLayout::PublishedColumnLeft).
	void PublishColumn();

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

	// ---- US-T441: layout presets (UI/Hud.Layouts.cpp)
	cs::String layoutShown = "Default";
	bool layoutsLoaded = false;
	/// The Layouts drawer: the row picked, the name being typed (save as, rename), whether its field has the keys, its scroll.
	cs::String layoutPicked;
	cs::String layoutName;
	bool layoutTyping = false;
	/// 0: the name field saves as; 1: it renames the picked preset.
	int layoutNameMode = 0;
	Vector2 layoutScroll;
	cs::String layoutStatus;
	float layoutStatusAt = -10.0f;
	cs::Ref<GUIStyle> layoutField;
	cs::Ref<Texture2D> layoutFieldTexture;
	/// "switch layout with the situation": the situation last acted on ("" none, else the preset it calls for), the one
	/// seen now and since when (it must hold a moment before the layout follows).
	cs::String situationActed, situationSeen;
	float situationSince = 0;
	/// The profile's game.hudLayouts block (made when missing), written back after a change.
	cs::Ref<Shared::Json::JsonObject> LayoutsBlock();
	void SaveLayoutsBlock();
	/// The built-in's recipe on the layout as it is (after ResetLayout).
	void ApplyBuiltinRecipe(const cs::String& name);
	/// Once, after the drawers are registered: the chosen preset on screen.
	void LoadActiveLayout();
	/// Each frame: the option "switch layout with the situation" (combat: Combat, docked: Commerce, else the chosen one).
	void UpdateLayoutSituation(float dt);
	/// The situation's preset now ("" none).
	cs::String LayoutSituation() const;
	/// The Layouts drawer's content.
	void DrawLayouts(Rect r);
	void LayoutNotice(const cs::String& text);
