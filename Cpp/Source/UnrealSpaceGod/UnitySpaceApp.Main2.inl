// UnitySpaceApp.Main2.inl: included INSIDE the UnitySpaceApp class body (UnitySpaceApp.h). Port of
// Runtime/UnitySpaceApp.cs lines 1471-end (the second half of UnitySpaceApp.cs, split only for porting): the chase,
// gravity, propellant and tankers, jump gates, the pilot's battle, haven assaults, salvage, surrender prompts, the lock,
// the overview and the system's plane, reorienting, pause, scale, toasts, reset to now, charts, ship placement, the
// ICameraWorld queries, views, lights, wrecks, platforms, orbit and path lines, and picking.
//
// A C# null string is the empty string here (SalvageApproach, EndSalvage's toast). C#'s `object salvagePrompted`
// is a cs::Ref<void> (the hulk itself, a Ship or a Wreck).

private:
	/// <summary>Real seconds between looks for hostiles that might turn to run the ship down.</summary>
	static constexpr float PursuitCheckIntervalS = 4.0f;
	float pursuitCheckTimer = 0;

	/// <summary>Seconds left with the drives knocked out by a hunter's EMP out of battle (see <see cref="PilotStunSeconds"/>).</summary>
	double pilotEmpSeconds = 0;

	/// <summary>
	/// The chase (U.SPC-T273): every few seconds, hostiles whose scanners make the ship out may turn to run it down,
	/// showing up on its scanners some hundreds of km off; every frame they close, charge and set off EMPs, and start
	/// the fight where the ship is once they are close, or give up. Time is held at real speed when they make contact.
	/// </summary>
	void UpdatePursuit(const cs::Ref<Core::Ship>& ship, float dt);

	static cs::String HunterName(const cs::Ref<Core::Pursuer>& g);

	/// <summary>The pull on the piloted hull of every body it is close to (see <see cref="PilotFlight.Gravity"/>),
	/// km/s² -- its own frame's body above all, but a moon or a neighbour too when it flies near one.</summary>
	Core::DVec3 PilotGravity();

	/// <summary>A hull its thrusters cannot hold up sinks onto the surface and rests there rather than
	/// falling through it -- any body it is close enough to feel, not only its own frame's. Coming down
	/// hard does damage (see <see cref="SurfaceImpact"/>); false if the hull did not survive it.</summary>
	bool KeepPilotAboveSurfaces(const cs::Ref<Core::Ship>& ship);

	/// <summary>Warns as the piloted ship's tanks run low and dry, and brings a called tanker alongside when it is due.</summary>
	void UpdatePropellant(const cs::Ref<Core::Ship>& ship, float dt);

public:
	/// <summary>Calls an emergency tanker out to the piloted ship (dearer than a port: a call-out fee and three times the going rate).</summary>
	void CallTanker();

private:
	/// <summary>
	/// While piloting, checks (and on the player's command, performs) a transit through whichever
	/// of this system's jump gates the ship is in range of (one that is shut will not open): a flyable, in-world alternative to the menu-driven STAR SYSTEMS jump (see
	/// <see cref="StarTravel"/>, untouched -- the camera still uses it on its own). The eligibility
	/// and position-handoff math is Core's (<see cref="GateTransit"/>); this just wires the
	/// activation key and re-seeds the pilot session onto the arriving hull in its new system,
	/// exactly as <see cref="TogglePiloting"/> seeds one at the start.
	/// </summary>
	bool UpdateGateActivation(const cs::Ref<Core::Ship>& ship);

	/// <summary>Starts (or joins) a fight the instant piloting brings the ship within range of a
	/// hostile -- mirroring what <see cref="War.OnArrived"/> does for every AI ship, since a player
	/// flying there is otherwise invisible to that check (see <see cref="War.PresentGroups"/>) -- and,
	/// every frame afterward, hands the battle this frame's actual flown pose and fire/lock input
	/// instead of letting its own AI steer and pick targets for the hull.</summary>
	void UpdatePilotBattle(const cs::Ref<Core::Ship>& ship, float dt);

	/// <summary>
	/// Flying over a pirate haven whose garrison platforms are gone (and with no raiders holding it):
	/// the number keys finish the assault, 1 to board and take it (the group needs marines), 2 to
	/// destroy it (see <see cref="War.AssaultHaven"/>).
	/// </summary>
	void UpdateHavenAssault(const cs::Ref<Core::Ship>& ship);

	cs::Ref<Core::Settlement> havenPrompted;

	// ---------------------------------------------------------------- salvage and boarding of hulks

	/// <summary>A hulk's position and velocity in the pilot's frame (true km, km/s), from where it is at two nearby moments.</summary>
	std::tuple<Core::DVec3, Core::DVec3> HulkInPilotFrame(const cs::Func<Core::DVec3, double>& helioAt);

	static Core::DVec3 DerelictHelio(const cs::Ref<Core::Ephemeris>& eph, const cs::Ref<Core::Ship>& s, double jd);

	static cs::String Num(double v, const char* format);

	/// <summary>
	/// Salvage while piloting, out of any battle: the nearest derelict (<see cref="Ship.Derelict"/>) or wreck
	/// gets an approach readout; matched alongside it (<see cref="SalvageSession.Check"/>), 1 latches on.
	/// Latched, the ship rides with the hulk and the number keys choose: with raiders aboard, 1 sends the
	/// marines through the airlock; once it is clear, 1 salvages cargo into the grid hold, 2 strips the
	/// most useful component, 3 claims the hull if it can fly; 4 (or throttling away) casts off. Returns
	/// true while it has the number keys (latched, or alongside and offering to latch).
	/// </summary>
	bool UpdateSalvage(const cs::Ref<Core::Ship>& ship, float dt);

	/// <summary>The number key for the salvage choices: none while a patrol's interdiction has the keys.</summary>
	int SalvageKey();

	bool UpdateLatched(const cs::Ref<Core::Ship>& ship, float dt);

	/// <summary>Toasts each new line of the latched session's log.</summary>
	void ShowSalvageLog();

public:
	/// <summary>The number-key choices open on the latched hulk right now, for the HUD.</summary>
	cs::String SalvageChoices();

private:
	/// <summary>Lets go of the latched hulk: the session ends, is logged, and pays out any adventure on it.</summary>
	/// C++: an empty toast stands for C#'s null (no toast).
	void EndSalvage(const cs::String& toast);

	/// <summary>
	/// While piloting in a battle, the number keys answer hails: a pirate demand for this ship's cargo
	/// (1 hand it over, 2 refuse), or an enemy hull that has struck its colours to this side (1 demand
	/// its cargo, 2 take the ship, 3 let it go). Unanswered, the battle settles it after
	/// <see cref="Battle.SurrenderDecisionSeconds"/>.
	/// </summary>
	void UpdateSurrenderPrompts(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& unit);

	/// <summary>Locks the camera's tracking onto whatever's hovered/selected, or clears an existing
	/// lock -- the ship's own facing stays fully under the player's mouse control either way (the
	/// camera itself leans toward the lock in <see cref="Cameras.SpaceCamera"/>'s focus update).</summary>
	void ToggleLockTarget();

	/// <summary>Whether the helm's trigger was held last frame, so letting go of it fires the volley.</summary>
	bool helmTriggerWas = false;

public:
	/// <summary>Where the player is looking (ecliptic, unit): around a capital ship at its helm, else down the crosshair.</summary>
	Core::DVec3 PilotLookDirection();

private:
	/// <summary>A fighter locked in the flown hull's fight (a ship is held in <see cref="pilotLockTarget"/>).</summary>
	cs::Ref<Core::BattleUnit> pilotLockUnit;

	/// <summary>The hostile in the flown hull's fight that sits nearest the crosshair, within <see cref="LockPickConeDeg"/> of it.</summary>
	cs::Ref<Core::BattleUnit> UnitNearestCrosshair();

	static constexpr double LockPickConeDeg = 15;

public:
	/// <summary>The flown hull's own battle unit, if it is in a fight.</summary>
	cs::Ref<Core::BattleUnit> PilotUnit();

	/// <summary>The locked hull's battle unit, if it is in the flown hull's fight.</summary>
	cs::Ref<Core::BattleUnit> PilotLockUnit();

	/// <summary>Camera elevation of the system overview, degrees.</summary>
	static constexpr float OverviewPitch = 52.0f;

private:
	/// <summary>How much of the system the overview frames: the inner planets in Sol, the A-B pair in Alpha Centauri, the planets elsewhere.</summary>
	double OverviewSpanKm() const;

public:
	/// <summary>The overview's camera distance from the system's root, scene units.</summary>
	double OverviewDistance();

	void FocusSystemOverview();

	/// <summary>
	/// The plane the active system is laid out in, as its north-side normal (Unity axes): the ecliptic in Sol, the
	/// orbit of the first star round the barycentre for a binary (Alpha Centauri's A-B plane, steeply tilted to the
	/// ecliptic), and for any other star the plane its own planets sweep, weighted by their masses.
	/// </summary>
	Vector3 SystemPlaneNormal();

	/// <summary>The plane <paramref name="system"/> is laid out in (see <see cref="SystemPlaneNormal"/>), active or not.</summary>
	static Vector3 PlaneNormal(const cs::Ref<Core::StarSystem>& system);

	/// <summary>
	/// How far out <paramref name="system"/>'s Oort cloud reaches, light-years: the Sun's about 100,000 AU, other
	/// systems' scaled by the cube root of their stars' mass, as a star's gravitational reach is.
	/// </summary>
	static double OortCloudLy(const cs::Ref<Core::StarSystem>& system);

private:
	static constexpr double SunOortCloudAu = 100000, AuPerLy = 63241.077;

public:
	cs::String SystemPlaneName();

	/// <summary>Whether the view keeps the active system's plane level by itself (R or the on-screen toggle; remembered between sessions).</summary>
	bool AutoReorient() const;

	/// <summary>Controls (U.SPC-T431), under the key the R toggle has always been remembered by.</summary>
	/// C#: a [SettingsSource] static readonly field; here a function-local static (see SimClock::StartingWarp), which
	/// GameSettingsHost must touch so the setting registers with the others.
	static const cs::Ref<Core::ToggleSetting>& AutoReorientSetting();

	void ToggleAutoReorient();

	/// <summary>Swings the view smoothly round until the system's plane is level.</summary>
	void Reorient();

private:
	/// <summary>With auto-reorient on, swings the view level again whenever the system's plane is not (a new system, the helm let go).</summary>
	void KeepReoriented();

public:
	/// <summary>Space: the whole simulation (every system) eases to a stop, or back up to speed.</summary>
	void TogglePause();

	void ToggleScale();

private:
	void AnimateScale(float dt);

public:
	void ShowToast(const cs::String& message);

	void ResetToNow();

private:
	/// <summary>The galaxy's <see cref="Galaxy.ChartVersion"/> the view between the stars was last built from.</summary>
	int chartsShown = 0;

	/// <summary>
	/// Rebuilds the view between the stars when exploring has charted new stars or built new systems (U.SPC-T413),
	/// never in the middle of a trip through it.
	/// </summary>
	void RefreshCharts();

	// ---------------------------------------------------------------- ships

	void UpdateShips(float dt);

	/// <summary>Scene units per km around a ship (sideways), from the mapping of the world it is nearest, or the star's out in open space.</summary>
	double KmToSceneNear(const cs::Ref<Core::Ship>& ship, Core::DVec3 helio);

	/// <summary>
	/// Ships fighting are drawn where the battle has them; ships entering or leaving a fight ease
	/// between that and their orbit over two seconds.
	/// </summary>
	void PlaceInBattles(float dt);

	cs::List<Core::DVec3> lastBattleScene;

	cs::List<bool> lastDrawnValid;

	// Where the ship was last drawn (after any blend), so a blend interrupted or restarted mid-way starts from what
	// the player saw rather than the raw schedule or battle position (U.SPC-T568).
	Core::DVec3 DrawnScene(int i);

public:
	/// <summary>Scene position of a transiting ship at a fraction of its path (see <see cref="ScaleModel.TransitScene"/>).</summary>
	Core::DVec3 TransitScene(const cs::Ref<Core::Ship>& ship, double progress, Core::DVec3& travelDirection);

private:
	// A ship added since this frame's sync (spawned mid-frame by a tour coroutine, say) gets its views first.
	void EnsureShipSynced(const cs::Ref<Core::Ship>& ship);

public:
	Core::DVec3 ShipScene(const cs::Ref<Core::Ship>& ship);
	double ShipDisplayLength(const cs::Ref<Core::Ship>& ship);
	Core::ShipState ShipStateOf(const cs::Ref<Core::Ship>& ship);

	// ---------------------------------------------------------------- ICameraWorld

	Core::DVec3 BodyScene(int body) override;
	double BodyDisplayRadius(int body);

	Core::DVec3 TargetScene(const Target& target) override;

	double TargetRadius(const Target& target) override;

	Quaternion TargetRotation(const Target& target) override;

private:
	static Vector3 ToVector(Core::DVec3 v);

public:
	double NearestSurface(Core::DVec3 p) override;

	// ---------------------------------------------------------------- views

private:
	void UpdateClipPlanes(float dt);

public:
	Vector3 Relative(Core::DVec3 scene);

private:
	/// <summary>
	/// Lights the scene with the two stars that shine brightest where the camera is (luminosity
	/// over distance squared): the Sun alone in Sol; A with B beside it in Alpha Centauri, or
	/// Proxima out by Proxima. Light colours follow each star's temperature, the Sun's being white.
	/// </summary>
	void UpdateLights();

	bool atmosphereSet = false;

	/// <summary>
	/// Tells a pipeline that draws a physically based sky (HDRP) which planet's air the camera is in (U.SPC-T430),
	/// scoped to docking-ring atmospheric entry only (U.SPC-T452): the sky draws while the ship the camera rides
	/// (flown, or followed) is coming down through a ring, landed at its city or climbing back, for that ringed world
	/// alone, and is cleared everywhere else, where the shaders' own atmosphere rims draw every planet's air. A
	/// pipeline without such a sky is not asked.
	/// </summary>
	void UpdateAtmosphere();

	void ClearAtmosphere();

	/// C#: static readonly Rgb SunLight = AlphaCentauri.StarColor(5772).
	static const Core::Rgb& SunLight();

public:
	/// <summary>A star's light relative to sunlight (so the Sun lights white), scaled to its brightest channel.</summary>
	static Vector4 LightColor(const cs::Ref<Core::BodyDefinition>& star, float strength);

private:
	void UpdateViews(float dt);

	static inline cs::Ref<Mesh> glowQuad;

	/// <summary>The plasma sheath round a ship coming down through an atmosphere: a hot glow at its nose.</summary>
	void PlaceEntryGlow(int index, int ship, Vector3 rel, float heat, double pixelsPerUnit);

	/// <summary>
	/// A ship lost in battle breaks up and burns out where it died, then lingers as a slowly
	/// tumbling wreck (<see cref="World.War"/>'s persistent <see cref="Wreck"/>, not just the
	/// transient burn-out) until its own configurable lifetime runs out and the Core clears it away.
	/// </summary>
	void DrawWreck(int i, const cs::Ref<Core::Ship>& ship);

	/// <summary>Defence platforms are drawn while they fight.</summary>
	void UpdatePlatforms();

	void UpdateOrbitLines(float dt);

	void UpdatePathLine();

public:
	double PixelsPerUnitAtUnitDistance();

	/// <summary>Where another star system shines in this one's sky (a Unity-axes unit vector).</summary>
	Vector3 SkyDirection(const cs::Ref<Core::StarSystem>& other);

	// ---------------------------------------------------------------- picking

	/// <summary>Projects a scene point to GUI coordinates (origin top-left). False if behind the camera.</summary>
	bool ToGui(Core::DVec3 scene, Vector2& gui, float& depth);

	/// <summary>Projects a camera-relative point (or direction far away) to GUI coordinates.</summary>
	bool RelativeToGui(Vector3 rel, Vector2& gui);

	/// <summary>True if a sphere of radius <paramref name="ignoreRadius"/> at scene point p is hidden behind any body.</summary>
	bool Occluded(Core::DVec3 p, int ignoreBody = -1);

private:
	void UpdateHover();

	void HandleSelectionClicks();

public:
	Target Pick(Vector2 gui);
