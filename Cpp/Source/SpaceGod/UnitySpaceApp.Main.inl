// UnitySpaceApp.Main.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// Port of Runtime/UnitySpaceApp.cs lines 1-1470 (through ToRotation); bodies in UnitySpaceApp.cpp.
//
// C#: [DefaultExecutionOrder(-100)] -- the host ticks the app before everything else.
// C#: [SettingsSource] -- the app's one setting (AutoReorientSetting, declared in Main2) is registered with the settings
// registry by GameSettingsHost (SettingsRegistry::AddDeclarationSource([] { UnitySpaceApp::AutoReorientSetting(); })).
//
// C# nulls of string (SalvageApproach, Toast) are empty strings here. `object salvagePrompted` is a std::shared_ptr<void>
// (the hulk last offered, a Ship or a Wreck: compared by identity). C# arrays are cs::List; `shipViews = null` is an
// empty list (BuildShipViews then has nothing to tear down, as in C#).

public:
	/// <summary>Raised when the galaxy is rebuilt from scratch (a backwards time reset): anything holding
	/// references into the old one (a running tour, a capture) must stop rather than run on stale data.</summary>
	cs::Event<> GalaxyRebuilt;

	cs::Ref<Core::Galaxy> Galaxy;

	/// <summary>The star system being looked at; the properties below are its.</summary>
	cs::Ref<Core::StarSystem> ActiveSystem;

	/// ICameraWorld.Catalog.
	const cs::Ref<Core::BodyCatalog>& Catalog() const override;
	const cs::Ref<Core::Ephemeris>& Ephemeris() const;
	const cs::Ref<Core::World>& World() const;
	cs::Ref<Core::SimClock> Clock;

	/// ICameraWorld.Scale: the auto-property `Scale { get; private set; }` is the field scaleModel behind this method.
	const cs::Ref<Core::ScaleModel>& Scale() const override { return scaleModel; }

private:
	cs::Ref<Core::ScaleModel> scaleModel;

public:
	const cs::Ref<Core::Fleet>& Fleet() const;
	const cs::Ref<Core::Economy>& Economy() const;
	cs::Ref<Cameras::SpaceCamera> SpaceCamera;
	Camera* UnityCamera = nullptr;
	cs::Ref<Cameras::StarTravel> Travel;
	cs::Ref<Views::InterstellarView> Interstellar;

	/// <summary>Yards, factories, habitats, spacecraft and docking rings of the active system.</summary>
	cs::Ref<Views::StructureViews> Structures;

	/// <summary>The real main belt and Jupiter Trojans (JPL SBDB), drawn on the GPU (Sol only).</summary>
	cs::Ref<Views::AsteroidBelt> AsteroidBelt;

	/// <summary>The skybox behind the system in view, kept per system (U.SPC-T595).</summary>
	cs::Ref<Views::SkyView> Sky;
	Input::InputState Input;

	Target Selected = Target::None();
	Target Hovered = Target::None();

	/// <summary>The ship under direct player control, if any (ship-only, or <see cref="Target.None"/>).</summary>
	Target Piloted = Target::None();
	bool IsPiloting() const { return !Piloted.IsNone() && CoopHelmHere(); } // US-T44 (US-T43): in co-op the helm is everyone's in the shared steps, the holder's elsewhere

	/// <summary>Where the piloted ship really is (its body frame and true km offset) and how fast it is
	/// flying: its schedule shows only the spot it was pinned at when the helm was taken.</summary>
	int PilotFrameBody() const { return pilotFrameBody; }
	Core::DVec3 PilotOffsetKm() const { return pilotOffsetKm(); }
	double PilotSpeedKmS() const { return pilotVelKmS().Length(); }

	/// <summary>The piloted hull's velocity in its body's frame (ecliptic axes, km/s), nose and roof:
	/// what the HUD's velocity vector and prograde/retrograde markers are drawn from.</summary>
	Core::DVec3 PilotVelocityKmS() const { return pilotVelKmS(); }
	Core::DVec3 PilotForward() const { return pilot().Forward; }
	Core::DVec3 PilotUp() const { return pilot().Up; }

	/// <summary>Whether the flight computer is flying the hull to the stick (on each time the helm is
	/// taken; F toggles it) or the hull is drifting on raw thrust. See <see cref="PilotFlight"/>.</summary>
	bool PilotFlightAssist() const { return !pilotFlightAssistOff; }

private:
	bool pilotFlightAssistOff = false;

public:
	/// <summary>Where a piloted capital ship's engine telegraph stands (U.SPC-T499, <see cref="CapitalFlightModel"/>);
	/// null for any other hull.</summary>
	cs::Nullable<Core::EngineTelegraph> PilotTelegraph;

	/// <summary>The helm's own flight line for the HUD (US-T87, <see cref="HelmFlight.Note"/>): with flight assist off,
	/// what stopping costs; empty otherwise.</summary>
	cs::String PilotFlightNote;

private:
	int pilotFrameBody = 0;

	/// <summary>What the helm flies (U.SPC-T449): the piloted hull's motion and the world's part in this frame,
	/// flown by <see cref="ShipHelm.Control"/>; kept here across frames (and across hulls, a gate handing the
	/// same session to the next).</summary>
	const cs::Ref<Core::HelmFlight> helmFlight = cs::New<Core::HelmFlight>();

	/// <summary>The helm being flown, holding <see cref="helmFlight"/>; remade when the hull changes.</summary>
	cs::Ref<Core::ShipHelm> pilotHelm;

	/// <summary>The piloted hull's whole motion (position, velocity, attitude, spin, cruise ramp),
	/// integrated by its helm every frame; the properties below are its parts.</summary>
	Core::PilotState& pilot() { return helmFlight->State; }
	const Core::PilotState& pilot() const { return helmFlight->State; }

	Core::DVec3 pilotOffsetKm() const { return pilot().Pos; }
	void SetpilotOffsetKm(Core::DVec3 value) { pilot().Pos = value; }
	Core::DVec3 pilotVelKmS() const { return pilot().Vel; }
	void SetpilotVelKmS(Core::DVec3 value) { pilot().Vel = value; }
	Core::DVec3 pilotForward() const { return pilot().Forward; }

	/// <summary>Where the stick points the nose (ecliptic, unit): mouse movement swings it about the
	/// hull's own roof and starboard axes, so steering stays screen-relative however the hull has
	/// rolled, and it can point anywhere -- straight up, down or behind -- with no gimbal clamp.</summary>
	Core::DVec3 pilotAim = Core::DVec3::UnitX;
	static float PilotLookSensitivity();

	/// <summary>The free-aim view's roof (see <see cref="PilotAim.Look"/>): it turns with the crosshair and
	/// follows the hull's roll, so the mouse stays screen-relative.</summary>
	Core::DVec3 pilotViewUp;

	/// <summary>At a capital ship's helm the mouse looks around the hull rather than steering it: how far
	/// the view is swung from dead astern, degrees.</summary>
	float helmLookYaw = 0, helmLookPitch = 0;

	/// <summary>The string aim's owed mouse travel (US-T168, <see cref="PilotAim.Look"/>), radians right and up: what a heavy
	/// hull's taut string has yet to pay out at its look rate.</summary>
	double pilotLookOwedRight = 0, pilotLookOwedUp = 0;

public:
	/// <summary>How the flown hull is steered (see <see cref="PilotControl"/>).</summary>
	Core::PilotControl PilotControlMode{};

	/// <summary>Where the stick points the hull (ecliptic, unit): the crosshair in free aim.</summary>
	Core::DVec3 PilotAimDirection() const { return pilotAim; }

private:
	/// <summary>The stick straight ahead again, and the view behind the hull: on taking the helm and
	/// whenever the hull is put on a new heading by something other than the stick.</summary>
	void ResetPilotAim();

	/// <summary>What the pilot camera leans toward tracking, independent of the ship's own facing.</summary>
	Target pilotLockTarget = Target::None();

	/// <summary>Base field of view (matches <see cref="SetupCamera"/>'s initial value); what is flown widens it with its
	/// pace, its afterburner and its cruise drive by its role's feel (US-T170, <see cref="Core::FlightFeelProfile"/>) -- a
	/// classic sense-of-speed cue (Freelancer, Elite, Star Citizen all do this).</summary>
	static constexpr float BaseFov = 60.0f, FovResponseHz = 6.0f;
	float cameraFov = BaseFov;
	bool pilotBoosting = false;

	/// <summary>US-T170: the feel of what is being flown (the helm's hull, a craft's cockpit, an EVA worker): its speed
	/// streaks, the field of view it widens to, and the camera's shake by its role and mass. Stepped by the helm and the
	/// seats each frame they fly (<see cref="feelStepped"/>), settled back to still otherwise.</summary>
	Core::FlightFeel flightFeel;
	bool feelStepped = false;

	/// <summary>What <see cref="flightFeel"/> was last set up for (a hull, a seat), so a new one starts on its own feel.</summary>
	std::shared_ptr<void> feelFor;

	/// <summary>US-T170: a hauler helm's own flare-and-chaff magazine, full each time a hull's helm is taken.</summary>
	Core::HelmFlares helmFlares;

	/// <summary>The feel set up for <paramref name="owner"/> (a new one: reset to <paramref name="profile"/>).</summary>
	void FeelFor(const std::shared_ptr<void>& owner, const Core::FlightFeelProfile& profile);

	/// <summary>The feel stepped for the helm's hull this frame, after it has flown.</summary>
	void StepHelmFeel(const cs::Ref<Core::Ship>& ship, const Core::PilotTuning& tuning, const Core::HullDynamics& hull, float dt);

public:
	/// <summary>US-T170: the flown hull's speed streaks and dust, 0..1, for the HUD.</summary>
	float FlightStreaks() const { return static_cast<float>(flightFeel.Streaks); }

private:

	/// <summary>The cruise drive's own ramp state (see <see cref="PilotFlight.Step"/>) -- persists
	/// across frames like <see cref="pilotVelKmS"/> does, and only while piloting.</summary>
	Core::PilotCruiseState pilotCruise() const { return pilot().Cruise; }
	void SetpilotCruise(Core::PilotCruiseState value) { pilot().Cruise = value; }

public:
	/// <summary>Real seconds until the emergency tanker the pilot called reaches the ship, or below 0 for none on its way.</summary>
	float TankerDueS = -1;

private:
	/// <summary>The propellant warning last shown (0 none, 1 low, 2 dry), so each is toasted once as it is crossed.</summary>
	int propellantWarning = 0;

public:
	/// <summary>Real seconds a tanker the pilot calls takes to come alongside.</summary>
	static constexpr float TankerCallS = 20.0f;

private:
	/// <summary>Seconds until the next check for a hostile worth proactively starting a fight over
	/// (see <see cref="War.TryStartPlayerBattle"/>) -- a few times a second is plenty responsive
	/// and cheap enough not to bother running every single frame.</summary>
	float pilotBattleCheckTimer = 0;
	static constexpr float PilotBattleCheckIntervalS = 0.5f;

	/// <summary>The surrendered hull (or pirate cargo demand) the player was last prompted about, so the
	/// choice toast shows once per hail rather than every frame. See <see cref="UpdateSurrenderPrompts"/>.</summary>
	cs::Ref<Core::BattleUnit> termsPrompted;
	bool demandPrompted = false;

	/// <summary>The jump gate the piloted ship is close enough to activate, or null
	/// (see <see cref="UpdateGateActivation"/>); drives that gate's glow and the HUD prompt.</summary>
	cs::Ref<Core::JumpGate> nearGate;

public:
	/// <summary>The hulk the piloted ship is latched on to (see <see cref="UpdateSalvage"/>), or null.</summary>
	cs::Ref<Core::SalvageSession> Salvage;

	/// <summary>The nearest derelict or wreck within <see cref="SalvageApproachKm"/> while piloting and not
	/// latched: a line for the HUD saying how far off it is and how fast the gap is closing.</summary>
	cs::String SalvageApproach;

private:
	/// <summary>How near a hulk must be for its approach readout to show, km.</summary>
	static constexpr double SalvageApproachKm = 60;

	/// <summary>The hulk last offered "1 latch on", and how far into the session's log the toasts have reached.</summary>
	std::shared_ptr<void> salvagePrompted;
	int salvageLogShown = 0;

	/// <summary>The piloted ship's offset from the hulk it is latched to, held fixed while the clamps are on.</summary>
	Core::DVec3 salvageLatchOffsetKm;

public:
	bool ShowOrbits = true;
	bool ShowLabels = true;
	/// <summary>The Help screen (H / F1, or Help in the Esc menu, U.SPC-T494): every key and control over the whole view; closed at the start.</summary>
	bool ShowHelp = false;

	/// <summary>U: the lighter toggle; off, the HUD's panels and readouts go while the names and markers on the view stay.</summary>
	bool ShowHud = true;

	/// <summary>
	/// F11 (U.SPC-T496): the screen cleared of all its UI, in the strategy view and at the helm alike -- HUD, navigator,
	/// help, panels, banners, toasts, names and markers -- all but the reticle in use (the helm's aim, a seat's sight).
	/// F11 again brings it all back as it was; opening a panel or a screen meanwhile brings it back too, so nothing
	/// opens unseen. The pause menu still shows over it.
	/// </summary>
	bool ScreenCleared = false;

	bool ShowMarket = false;
	bool ShowCargo = false;

	/// <summary>The shipyard's outfitting panel (F2): fit modules into the piloted hull's slots at a yard, hand over prisoners.</summary>
	bool ShowOutfit = false;

	/// <summary>The bounty board (F4): the outlaws (and the player, when wanted) the port in view has bounties on.</summary>
	bool ShowBounties = false;

	/// <summary>The contracts board (F7) of the port in view: jobs to take, and the player's own to see through or give up.</summary>
	bool ShowContracts = false;

	/// <summary>The player's fleet and property (F10): buy hulls and property, hire captains, give standing orders, read the books.</summary>
	bool ShowFleet = false;

	/// <summary>The trade desk (backslash, U.SPC-T426): the trader's rank, runs, brokers, trade news, charters, insurance, warehouses, the back door.</summary>
	bool ShowTrade = false;

	// ---------------------------------------------------------------- the port screen (US-T266)

	/// <summary>The port screen's tabs, in the order its strip shows them.</summary>
	enum class PortTab : int { Market, Outfitting, Repair, Contracts, Bounties, Fleet, Trade, Crew, News };

	/// <summary>US-T266: the port the held hull is docked at (Shift+M alongside it at a helm, with the port's clearance), or
	/// null. Docked, the port screen gathers what the port offers under one strip of tabs; flying off (or leaving the helm)
	/// undocks and closes it.</summary>
	cs::Ref<Core::Settlement> DockedAt;

	/// <summary>The port screen (US-T266), open while docked; Esc closes it without undocking (Shift+M opens it again).</summary>
	bool ShowPort = false;

	/// <summary>The port screen's tab open now.</summary>
	PortTab PortTabOpen = PortTab::Repair;

	/// <summary>A discovered, standing port the held hull is alongside now (a yard first), or null.</summary>
	cs::Ref<Core::Settlement> PortAlongside();

	/// <summary>Shift+M at a helm: asks the port alongside for docking clearance (by its owner's standing toward the pilot and
	/// whether it trades with the hull at all); docked, opens the port screen, or closes it when it is open.</summary>
	void DockKey();

	/// <summary>Asks <paramref name="port"/> for clearance and docks there; says why not on a toast.</summary>
	bool Dock(const cs::Ref<Core::Settlement>& port);

	/// <summary>Casts off from the port docked at: closes the port screen and the panel its tab opened. Says <paramref name="why"/>.</summary>
	void Undock(const cs::String& why);

	/// <summary>Opens a tab of the port screen: the panel the tab stands for (the market, outfitting, the boards, the fleet,
	/// the trade desk, the news) in place of the one the last tab opened. The Repair and Crew tabs are the screen's own.</summary>
	void OpenPortTab(PortTab tab);

	/// <summary>Every frame: undocks once the hull has left the port (or the helm), or the port no longer lets it in.</summary>
	void UpdateDocking();

private:
	/// <summary>The panel a tab of the port screen opens, or null for one the screen draws itself.</summary>
	bool* PortTabPanel(PortTab tab);

public:

	/// <summary>The galaxy map ([, U.SPC-T103): a full screen of every system, its holder, the gate traffic, fronts and raiders.</summary>
	bool ShowGalaxyMap = false;

	/// <summary>The polity the galaxy map's envoy screen opens on next (U.SPC-T427), set by a hail; the map takes it and clears it.</summary>
	cs::Ref<Core::Faction> EnvoyTo;

private:
	/// <summary>
	/// F12 (U.SPC-T427): hails the polity of the ship locked at the helm, the one selected, or the patrol hailing you, in the
	/// name of the polity you speak for (the one commanded from the map, else your own ship's), shows its answer and opens
	/// the galaxy map's DIPLOMACY panel on it to treat.
	/// </summary>
	void HailPolity();

public:
	/// <summary>The news feed panel in place of the four-line ticker (X).</summary>
	bool ShowNews = false;
	bool ShowSpawn = false;

private:
	/// <summary>A screen the player clicks through is open (market, cargo hold, spawn grid): the cursor is
	/// freed for it, so the mouse stops steering (and firing) the piloted hull meanwhile.</summary>
	bool ClickPanelOpen() const;

public:
	bool ShowLanes = true;

	/// <summary>Satellite constellations (thousands of points round Earth); off by default, K toggles.</summary>
	bool ShowSatellites = false;

	/// <summary>False while the camera is between the stars (the system's views are hidden).</summary>
	bool SystemVisible = true;

	bool IsSol() const;

	/// <summary>Every body's scene position this frame.</summary>
	const cs::List<Core::DVec3>& BodyScenes() const { return bodyScene; }

	/// <summary>1 = gameplay, 0 = realistic; the value the scale is animating toward.</summary>
	double ScaleGoal = 1;
	bool ScaleAnimating() const { return scaleT < 1.0f; }

	cs::String Toast;
	float ToastAge = 99.0f;

	/// <summary>GUI-space rects drawn by the HUD last frame; clicks inside them do not reach the world.</summary>
	cs::List<Rect> UiRects;

	/// <summary>Where the HUD drew each ship's name last repaint (U.SPC-T376): clicking a name picks its ship, like its icon.</summary>
	cs::List<std::tuple<Rect, cs::Ref<Core::Ship>>> ShipNameRects; // (rect, ship)

	/// <summary>Ships the HUD folded into another's mark last repaint (U.SPC-T415): clicking the mark picks the ship that stands for it, not one hidden in it.</summary>
	cs::HashSet<cs::Ref<Core::Ship>> FoldedShips;

private:
	cs::List<Core::DVec3> bodyScene;
	cs::List<double> bodyRadius;
	cs::List<cs::Ref<Views::BodyView>> bodyViews;
	cs::List<cs::Ref<Views::LineStrip>> orbitLines;
	cs::List<Core::DVec3> orbitSamples;
	cs::List<Vector3> orbitPoints;
	float orbitRefreshTimer = 0;

	cs::List<cs::Ref<Views::ShipView>> shipViews;
	cs::List<Core::DVec3> shipScene;
	cs::List<Quaternion> shipRotation;
	cs::List<double> shipLength;
	cs::List<Core::ShipState> shipStates;
	cs::List<bool> shipRotationInitialised;

	// Moving into and out of battle space is eased over a couple of seconds, from wherever the
	// ship was drawn (kept relative to its body, which keeps moving).
	cs::List<Core::DVec3> shipBlendFrom;
	cs::List<float> shipBlendT;
	cs::List<cs::Ref<Core::Battle>> shipLastBattle;

	// Wrecks burn out where the ship died, in its battle's frame.
	cs::List<float> wreckStart;
	cs::Ref<Views::BattleFx> battleFx;
	cs::Ref<Views::DebrisFields> debrisFields;
	cs::Ref<Views::PodBeacons> podBeacons;
	cs::Ref<Views::BioMassView> bioMassView; // US-T125
	cs::Ref<Views::DiscView> discView;
	/// <summary>US-T131: the place's graveyards.</summary>
	cs::Ref<Views::GraveyardView> graveyardView;
	/// <summary>US-T146: the gas giants' works (skimmer platforms, their plumes and skimmers, the tankers).</summary>
	cs::Ref<Views::HarvesterView> harvesterView;
	/// <summary>US-T130: the place's nebulae and ice fields.</summary>
	cs::Ref<Views::SpaceFeaturesView> spaceFeaturesView;
	cs::Ref<Views::FighterViews> fighterViews;
	cs::Ref<Views::LaneView> laneView;
	cs::Ref<Views::GateView> gateView;
	cs::Ref<Views::EmbeddedPods> embeddedPods;
	cs::Ref<Views::SatelliteSwarm> swarm;
	cs::List<cs::Ref<Core::Faction>> viewFaction;
	cs::Dictionary<cs::Ref<Core::ShipCombat>, cs::Ref<GameObject>> platformViews;
	cs::List<std::tuple<cs::Ref<Transform>, cs::Ref<Material>>> entryGlows; // (t, m)
	cs::Ref<Core::Battle> battleTimeFor;

public:
	/// <summary>Scene units per km inside a battle: the ship scale, so a fight reads at ship size in both views.</summary>
	double BattleKmScale() const;

	/// <summary>Scene position of a point in a battle (km from its anchor).</summary>
	Core::DVec3 BattleScene(const cs::Ref<Core::Battle>& b, Core::DVec3 localKm);

private:
	/// <summary>Where a battle is drawn: in orbit of its body, or beside the drawn hyperlane for an ambush.</summary>
	Core::DVec3 BattleAnchorScene(const cs::Ref<Core::Battle>& b);

public:
	/// <summary>Scene position of a lasting wreck: where its battle was drawn, at the spot its unit died.</summary>
	Core::DVec3 WreckScene(const cs::Ref<Core::Wreck>& w);

	/// <summary>Scene position of a drifting escape pod cluster: where its hull's battle was drawn, drifted away from the hull.</summary>
	Core::DVec3 PodScene(const cs::Ref<Core::EscapePod>& p);

private:
	Core::DVec3 AnchorScene(const cs::Ref<Core::LaneSite>& site, int body, Core::DVec3 anchor);

	/// <summary>A sideways offset (true km) beside a lane, in scene units at that distance from the system's root.</summary>
	Core::DVec3 SideScene(Core::DVec3 nearHelio, Core::DVec3 sideKm);

	/// <summary>A ship lurking beside a lane, or drifting after being pulled out of one.</summary>
	Core::DVec3 LaneHoldScene(const cs::Ref<Core::Ship>& ship, Core::DVec3& along);

public:
	/// <summary>The model a battle unit is drawn with (a ship's view, or a platform), for parenting things to its hull.</summary>
	cs::Ref<Transform> UnitTransform(const cs::Ref<Core::BattleUnit>& u);

private:
	cs::Ref<Views::LineStrip> pathLine;
	cs::List<Vector3> pathPoints = cs::NewArray<Vector3>(64);

	cs::Ref<Transform> worldRoot;
	cs::Ref<Transform> systemRoot;
	cs::Ref<UI::Hud> hud;

	/// <summary>Esc's pause menu (Resume, Settings > Display, Quit): while it is open the frame stops after input.</summary>
	const cs::Ref<UI::PauseMenu> pauseMenu = cs::New<UI::PauseMenu>();
	double scaleFrom = 1;
	float scaleT = 1.0f;
	static constexpr float ScaleSeconds = 2.4f;
	int lastNumberKey = -1;
	float lastNumberTime = -10.0f;

	// ---------------------------------------------------------------- lifecycle

	// Awake: declared in UnitySpaceApp.h.

	/// <summary>Battle toasts from whichever system is being watched, in every system there is and any a new gate opens (U.SPC-T97).</summary>
	void SubscribeWorlds();

	void SubscribeWorld(const cs::Ref<Core::StarSystem>& s);

public:
	/// <summary>
	/// Makes <paramref name="system"/> the one drawn: its bodies, orbits, ships, lanes, battles,
	/// structures and rings get views (the previous system's are torn down). The camera keeps its
	/// scene position; callers place it.
	/// </summary>
	void Activate(const cs::Ref<Core::StarSystem>& system);

	/// <summary>Hides the active system's views (between the stars) or shows them again.</summary>
	void SetSystemVisible(bool visible);

	/// <summary>Flies the camera to another star system.</summary>
	void TravelTo(const cs::Ref<Core::StarSystem>& system);

private:
	void UpdateRadii();

	void SetupCamera();

	/// <summary>Adds views (and per-ship arrays) for hulls a yard has launched since the last frame.
	/// Returns whether a resize actually happened, so a caller reached off the normal per-frame
	/// path (<see cref="TargetScene"/>) knows whether it must also force a fresh <see cref="UpdateShips"/>
	/// before trusting the newcomer's <see cref="shipScene"/> entry.</summary>
	bool SyncShipViews();

	void BuildShipViews();

	// OnDestroy: declared in UnitySpaceApp.h --
	// <summary>The game scene is unloading (back to the main menu): its materials and view meshes go with it,
	// and so do the HUD's and the pause menu's own DontSave textures (U.SPC-T366) -- otherwise every session
	// (and every galaxy map one opened) would leave its textures behind past this scene.</summary>

	// Update: declared in UnitySpaceApp.h.

	/// <summary>The pause menu is clicked through, whether or not the helm had the cursor locked.</summary>
	static void FreeCursorForMenu();

	/// <summary>A ship destroyed while selected or followed: let go of it.</summary>
	void DropLostTargets();

	/// <summary>Following a ship into battle slows the clock to battle time once, so the fight can be watched.</summary>
	void BattleTime();

public:
	/// <summary>Real time, so a capital ship engagement plays out at the pace it is fought.</summary>
	void EnterBattleTime(const cs::Ref<Core::Battle>& battle);

	/// <summary>The largest battle under way, or null.</summary>
	cs::Ref<Core::Battle> BiggestBattle();

	/// <summary>Flies the camera to a battle's flagship and drops to battle time.</summary>
	void WatchBattle(const cs::Ref<Core::Battle>& battle);

private:
	// OnGUI: declared in UnitySpaceApp.h.

	bool IsPointerOverUi();

	// ---------------------------------------------------------------- commands

	/// <summary>The panels and full screens open now, one bit each: what a cleared screen (F11) watches for newly opened.</summary>
	int OpenScreens() const;

	void HandleCommands();

public:
	/// <summary>Speeds time up a step (the key and the top bar's button alike), unless the ship at the helm is being
	/// run down: then time stays at the pace the chase is flown (U.SPC-T273).</summary>
	void TimeFaster();

private:
	/// <summary>
	/// Esc (U.SPC-T276): closes the panels being clicked through, else leaves the camera's focus, else clears
	/// the selection, and with nothing left to back out of opens the pause menu. At the helm it goes from the
	/// panels straight to the menu: P, not Esc, hands the ship back. See <see cref="EscapeKey"/>.
	/// </summary>
	void BackOut();

	static const cs::List<cs::String>& SolNumberBodies();

	static const cs::List<cs::String>& CentauriNumberBodies();

	/// <summary>The bodies the number keys pick: Sol's and Alpha Centauri's chosen lists, elsewhere the visible bodies in catalogue order.</summary>
	cs::List<cs::String> NumberBodies();

	void NumberHotkey(int key);

	void CycleShips(int direction);

public:
	void Select(const Target& target);

	void FocusSelected();

	// ---------------------------------------------------------------- piloting

	void TogglePiloting();

private:
	/// <summary>
	/// Makes <paramref name="ship"/> a freshly Parked hull fixed at the pilot's spot and facing.
	/// Taken control may start mid-lane, mid-burn, queued, landed or in formation (see
	/// <see cref="EnterPiloting"/>), so the old phase's bookkeeping (path, journey, ring queue) is
	/// stale and dropped: done on taking the helm, so nothing treats a flown hull as landed (hidden,
	/// unpickable) or reads an escort's missing orbit, and again on release.
	/// </summary>
	void PinWherePiloted(const cs::Ref<Core::Ship>& ship);

	/// <summary>The body whose frame best describes a ship's current position, and its offset
	/// there in true km -- the same per-phase placement <see cref="UpdateShips"/> computes for
	/// drawing, used instead to seed piloting from wherever a ship actually is (mid-lane,
	/// mid-burn, queued, landed...), not only a parked orbit.</summary>
	std::tuple<int, Core::DVec3> CurrentFrame(const cs::Ref<Core::Ship>& ship, double jd); // (body, offsetKm)

	/// <summary>
	/// Takes direct control of any flyable hull, wherever it is -- parked, mid-transit, riding or
	/// lurking off a hyperlane, queued or landed at a ring, even mid-battle (an escort is detached
	/// from its formation; a ship already fighting has its existing <see cref="BattleUnit"/> handed
	/// to the player instead of starting a new fight) -- via <see cref="TogglePiloting"/>, or a
	/// freshly spawned one via <see cref="SpawnShip"/>. Seeds every per-frame pilot field and snaps
	/// the camera into chase.
	/// </summary>
	void EnterPiloting(const Target& t);

public:
	/// <summary>
	/// Dev/sandbox tool (U.SPC-T9): builds a ship of <paramref name="cls"/> (a random design if the
	/// class has more than one, see <see cref="CombatSpec.RandomOf"/>) via <see cref="Fleet.SpawnAt"/>,
	/// parked exactly where the camera is looking from, and immediately pilots it -- for testing
	/// flight feel and combat without hunting down an existing hull in the live simulation. Releases
	/// whatever was already being piloted first, same as pressing Enter would.
	/// </summary>
	void SpawnShip(Core::ShipClass cls, bool hostile);

private:
	int spawnedCount = 0;

public:
	/// <summary>Where the player is, true heliocentric km: the piloted ship, else the camera (an observer
	/// still has adventures offered around them, but only a piloted ship can answer one).</summary>
	Core::DVec3 PlayerHelioKm();

	/// <summary>Scene position of an opportunity's HUD marker: on its ship or wreck, else its point in its body's frame.</summary>
	bool OpportunityScene(const cs::Ref<Core::Opportunity>& op, Core::DVec3& scene);

private:
	/// <summary>Starts the hull's motion at rest in attitude, nose on a facing (ecliptic, unit) and
	/// level with the camera's up frame unless that facing is itself near-vertical, with the stick
	/// pointing straight ahead -- shared by <see cref="TogglePiloting"/> and a jump gate handing the
	/// same pilot session to a new hull in the destination system.</summary>
	void SeedPilot(Core::DVec3 offsetKm, Core::DVec3 velocityKmS, Core::DVec3 facing);

	/// <summary>The piloted ship was taken from the player (boarded and captured): the session ends with
	/// nothing handed back, since the hull is its captor's now and the war disposes of it.</summary>
	void LoseHelm();

	void ExitPiloting();

public:
	/// <summary>Whether the piloted ship can trade at <paramref name="s"/> right now: piloting is a local
	/// flight within one body's frame (<see cref="pilotFrameBody"/> never changes mid-flight, see
	/// <see cref="TogglePiloting"/>), so reach is that frame matching the market's body and the ship still
	/// being within its neighbourhood -- the same "near a world" radius <see cref="Fleet.WanderOrbit"/>
	/// and the hyperlane ramps use -- on top of the usual diplomatic can-dock check.</summary>
	bool CanTradeNow(const cs::Ref<Core::Settlement>& s);

	/// <summary>Whether the piloted ship is close enough to <paramref name="s"/> to deal with its port (trade, pay fines), welcome or not.</summary>
	bool NearPort(const cs::Ref<Core::Settlement>& s);

private:
	/// <summary>Shows what the law has to tell the player (their crimes seen, hails, scans, bounties collected) as toasts.</summary>
	void ShowLawNotices();

	/// <summary>
	/// A patrol's order to heave to (see <see cref="Law.UpdatePilot"/>): 1 pays the fine it asks from the
	/// player's purse. Returns true while that hail has the number keys.
	/// </summary>
	bool UpdateLawHail(const cs::Ref<Core::Ship>& ship);

	/// <summary>Turns mouse delta + throttle/strafe keys into a flight command and steps the hull's
	/// position every frame piloting is active, before <see cref="UpdateShips"/> reads it.</summary>
	void UpdatePiloting(float dt);

	/// <summary>Where the chase camera looks, for the hulls whose view is not simply the hull's own facing:
	/// along the crosshair in free aim, and swung about the hull by the mouse at a capital ship's helm.</summary>
	cs::Nullable<Quaternion> PilotViewRotation(Core::PilotControl control);

	/// <summary>A Unity rotation facing <paramref name="forward"/> with its roof on <paramref name="up"/> (ecliptic axes).</summary>
	static Quaternion ToRotation(Core::DVec3 forward, Core::DVec3 up);
