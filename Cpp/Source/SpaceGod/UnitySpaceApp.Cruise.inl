// UnitySpaceApp.Cruise.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// US-T127: the in-system cruise drive at the helm; bodies in UnitySpaceApp.Cruise.cpp.
//
// <summary>
// The cruise drive (Core::SystemCruise) flown from the helm: Shift+J (or the dev menu's Travel > Cruise drive) spools it,
// and then the hull goes along its nose at up to a thousand times light's speed, steered by the stick as ever, slowing as
// it nears the world, station or ship it is bound for and dropping out at that target's standoff, a few seconds' flight
// from it. Shift+J again drops out wherever it is; a fight, a hit, the drives knocked out or a world's gravity well drop it
// too. The target is the one picked (the menu's Cruise to, a locked hull), else whatever world the nose is put on.
// Distances and speeds are true km at both scales; the hull's frame follows it from world to world (RebaseCruiseFrame) and
// it is drawn as the scale model places it (PilotedScene), so it reads right at the realistic and the game scale alike.
// </summary>

public:
	/// <summary>The cruise drive of the hull flown is on (spooling or cruising).</summary>
	bool SystemCruiseActive() const;
	const Core::SystemCruise& SystemCruiseState() const { return systemCruise; }

	/// <summary>Shift+J: spools the drive (for the target picked, a locked hull or what the nose is on), or drops out.</summary>
	void ToggleSystemCruise();

	/// <summary>The menu's Cruise to: that target, the nose swung onto it until the stick is touched, the drive spooled.</summary>
	void CruiseTo(const Target& target);

	/// <summary>Off at once (out of cruise at a gentle pace along the nose, in the frame it has come to); says
	/// <paramref name="why"/> on a toast if given.</summary>
	void DropSystemCruise(const cs::String& why);

	/// <summary>Why the hull flown cannot cruise now, or empty.</summary>
	cs::String SystemCruiseBlocker();

	/// <summary>US-T197: the one speed regime of the hull flown (Core::DriveRegime): ordinary flight, the cruise drive
	/// spooling or engaged, riding a lane, or between the stars. The lane and FTL code reads the same answer.</summary>
	Core::DriveRegime PilotDriveRegime();

	/// <summary>US-T197: the regime as it changed frame to frame and the cue the last change gave (the screen's flash on
	/// engaging and dropping out reads it; it is where a sound would hook in too).</summary>
	const Core::DriveRegimeWatch& DriveRegimeNow() const { return driveRegime; }

	/// <summary>The HUD's cruise readout (from OnGUI): spool, speed, the target, how far, the drop-out's ETA; and the
	/// drive's look (US-T197): the spool's charging glow, the flash as it engages and drops out, streaks while it cruises.</summary>
	void DrawSystemCruise();

	/// <summary>The dev menu's Travel entries for the drive (US-T126's registry).</summary>
	static void RegisterCruiseCheats();

	/// <summary>The piloted hull's scene position: its frame body's local mapping inside that body's neighbourhood, and
	/// beyond it as the scale model maps any true point near that body (ScaleModel.MapNear), so a hull far from its frame
	/// body (a cruise between worlds) is drawn where the worlds are drawn, at either scale.</summary>
	Core::DVec3 PilotedScene();

private:
	Core::SystemCruise systemCruise;

	/// <summary>Bound for: the world, station or hull picked (<see cref="cruiseTargetPicked"/>), else what the nose was put on.</summary>
	Target cruiseTarget = Target::None();
	bool cruiseTargetPicked = false;

	/// <summary>The menu's Cruise to turns the nose onto the target itself, until the stick is touched.</summary>
	bool cruiseAlign = false;

	/// <summary>The hull the drive is on (another hull flown drops it), and its hull points last frame (a hit drops it).</summary>
	cs::Ref<Core::Ship> cruiseShip;
	double cruiseHull = -1;

	/// <summary>US-T197: the hull's shields last frame (a hit on them drops it too, before the hull is touched).</summary>
	double cruiseShield = -1;

	/// <summary>US-T197: hulls already close by when the drive engaged (a wingman off the nose): never a collision alert.</summary>
	cs::List<cs::Ref<Core::Ship>> cruiseCleared;

	/// <summary>US-T197: what in the path holds a spooled drive back from engaging this frame (empty: nothing).</summary>
	cs::String cruiseBlockedBy;

	/// <summary>US-T197: the regime frame to frame, and the cue its last change gave.</summary>
	Core::DriveRegimeWatch driveRegime;

	/// <summary>US-T197: the nearest station or hull in the cruise's path (not the target), km along the nose (infinite:
	/// none), how near it the drive drops out, and its name.</summary>
	double CruiseObstacleAheadKm(Core::DVec3 helio, Core::DVec3 forward, double& alertKm, cs::String& name);

	/// <summary>This frame's reckoning, for the readout: km to the target and to its drop point (infinite: none), its name.</summary>
	double cruiseTargetKm = cs::Double::PositiveInfinity, cruiseToDropKm = cs::Double::PositiveInfinity;
	cs::String cruiseTargetName;

	cs::Ref<GUIStyle> cruisePanel, cruiseStyle, cruiseSmall;
	cs::Ref<Texture2D> cruiseBack, cruiseBar;

	/// <summary>Before the helm flies (in UpdatePiloting): cruising, the stick turns the hull but its thrust is the drive's;
	/// aligning for the menu's Cruise to, the nose is put on the target.</summary>
	void CruiseStick(Core::EmbodimentInput& stick);

	/// <summary>After the helm flew (in UpdatePiloting): Shift+J, what drops it, the target, and the hull carried along its nose.</summary>
	void UpdateSystemCruise(const cs::Ref<Core::Ship>& ship, float dt);

	/// <summary>Where a cruise target is (heliocentric km), how far off it the drive drops out, and its name; false if it is gone.</summary>
	bool CruiseTargetAt(const Target& t, Core::DVec3& helio, double& standoffKm, cs::String& name);

	/// <summary>The world or station nearest the nose's line within <paramref name="coneDeg"/>, not already reached; none if none.</summary>
	Target CruiseNoseTarget(Core::DVec3 helio, Core::DVec3 forward, double coneDeg);

	/// <summary>The clearance to the nearest surface, km, and whose (-1: none).</summary>
	double CruiseClearanceKm(Core::DVec3 helio, int& nearest);

	/// <summary>Moves the hull's frame to the body it is now near (or out to that body's parent), keeping where it is.</summary>
	void RebaseCruiseFrame();

	/// <summary>Whether <paramref name="body"/> holds a hull at <paramref name="helio"/>: inside its neighbourhood, or beyond it
	/// while the scale model still draws the point by it (at the game scale a world's mapping eases out over a wide reach;
	/// changing frame only where the two mappings meet keeps the hull from jumping on the screen).</summary>
	bool CruiseHolds(int body, Core::DVec3 helio);

	/// <summary>US-T258: a cruise target as every co-op peer can read it back: a station in orbit ("t <index>") or a docking
	/// ring ("r <index>") by its place in the Infrastructure, anything else as <see cref="CoopTargetKey"/> has it.</summary>
	cs::String CruiseTargetWire(const Target& t);
	Target CruiseTargetFromWire(const cs::String& key);
