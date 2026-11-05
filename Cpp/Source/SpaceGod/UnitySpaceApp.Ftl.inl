// UnitySpaceApp.Ftl.inl: included INSIDE the UnitySpaceApp class body (UnitySpaceApp.h). Port of Runtime/UnitySpaceApp.Ftl.cs.
//
/// The helm's own FTL drive, I-War 2 style (U.SPC-T417, the rules in <see cref="HelmFtl"/>). At the helm of a capital
/// ship or a survey hull, flown out beyond the system's outer rim (canon, U.SPC-T455: no jump is possible inside a
/// solar system's gravity wells), the nose is put on another star (the HUD names it), Enter spools the drive, and once
/// charged the hull itself goes out into open space with the rest of the traffic between the stars
/// (<see cref="DeepSpace"/>), the view goes over to the stars sliding by, and it drops out at the edge of that star's
/// well, the helm back in hand. The clock runs fast enough that the charge and the crossing each take seconds to a
/// minute or so, and slows again after. Small craft carry no drive.
//
// A C# null string is the empty string here (FtlBlocked, DisengageFtlAutopilot's why).

public:
	/// <summary>The helm's FTL drive and its charge.</summary>
	cs::Ref<Core::HelmFtl> Ftl = cs::New<Core::HelmFtl>();

	/// <summary>The star the nose is on, that the drive could be spooled for (null: none, or a hull with no drive).</summary>
	cs::Ref<Core::FtlTarget> FtlPointed;

	/// <summary>Whether the hull at the helm is beyond its system's outer rim, clear of the gravity wells to jump.</summary>
	bool FtlBeyondRim = false;

	/// <summary>Beyond the rim with the nose on a star, what still keeps the drive from spooling (another well, a
	/// gravity projector, the drive cooling, too little reaction mass: U.SPC-T418), or null.</summary>
	cs::String FtlBlocked;

	/// <summary>The player's hull out between the stars under its own drive, or null.</summary>
	cs::Ref<Core::Cruise> FtlCruise;

	/// <summary>Whether the player's hull is out between the stars under its own drive (the helm is back at the far end).</summary>
	bool InFtlCruise() const { return FtlCruise != nullptr; }

private:
	/// <summary>The hull flown out between the stars (the same object all the way, see <see cref="Fleet.LetGo"/>).</summary>
	cs::Ref<Core::Ship> ftlShip;

	/// <summary>The warp the clock ran at before the drive was spooled (back to it after), or -1.</summary>
	int ftlWarpBefore = -1;

	/// <summary>Whether the system bound for is already built (hidden), part way across.</summary>
	bool ftlDestinationBuilt = false;

	/// <summary>What there is to point at here, gathered now and then rather than every frame.</summary>
	cs::Ref<cs::List<cs::Ref<Core::FtlTarget>>> ftlTargets;
	cs::Ref<Core::StarSystem> ftlTargetsFor;
	int ftlTargetsChart = -1;
	float ftlTargetsAge = 0;

	/// <summary>The charge runs at the slowest warp that gets it over in about this many real seconds; the run out and
	/// the crossing take the settings menu's long trip compression (<see cref="FtlNavigation.TripRealSeconds"/>, U.SPC-T420).</summary>
	static constexpr double FtlSpoolRealSeconds = 8;

public:
	/// <summary>The course plotted for the helm's FTL drive (U.SPC-T420): to a star or a beacon, picked with the nose, Page
	/// Up/Down or the galaxy map, with its ETA and fuel. Null: none.</summary>
	cs::Ref<Core::FtlCourse> PlottedCourse;

	/// <summary>The FTL autopilot that flies the plotted course: out beyond the rim, onto the star, spooled and engaged (U.SPC-T420).</summary>
	cs::Ref<Core::FtlAutopilot> FtlAutopilot = cs::New<Core::FtlAutopilot>();

	/// <summary>Whether the hull at the helm carries its own FTL drive, so a course can be plotted and flown for it.</summary>
	bool CanPlotFtl();

private:
	/// <summary>Real seconds until the plotted course is worked out again from where the hull has got to.</summary>
	float ftlCourseAge = 0;

	/// <summary>The leg of the crossing the clock was last set for (a new one: pulled out by a well, or spooled again past it).</summary>
	double ftlCruiseLegJd = cs::Double::NaN;

public:
	/// <summary>Real seconds left of the charge or of the crossing at the clock's present rate.</summary>
	double FtlSecondsLeft();

private:
	/// <summary>Where the hull is, km from the system's root.</summary>
	Core::DVec3 PilotRootKm();

public:
	/// <summary>The system's outer rim, AU from its star: beyond it the drive can spool.</summary>
	double FtlRimAu();

	/// <summary>How far the hull at the helm is from its system's star, AU.</summary>
	double PilotFromStarAu();

	// ---------------------------------------------------------------- at the helm

private:
	/// <summary>
	/// A frame of the drive at the helm, after the hull has flown: whether it is beyond the rim, what star the nose is
	/// on, Enter to spool (again to call it off), the charge held or lost, and once charged the jump. True when the hull
	/// has left the system this frame.
	/// </summary>
	bool UpdateHelmFtl(const cs::Ref<Core::Ship>& ship);

	/// <summary>The star dead ahead of the nose, from a list of what is charted brought up to date every second or so.</summary>
	cs::Ref<Core::FtlTarget> PointedTarget(Core::DVec3 rootKm, Core::DVec3 forward);

public:
	/// <summary>How far <paramref name="t"/> is from the hull, light-years.</summary>
	cs::String Distance(const cs::Ref<Core::FtlTarget>& t);

private:
	static cs::String DistanceText(double km);

	// ---------------------------------------------------------------- between the stars

	/// <summary>
	/// Charged, for another star: the hull itself goes out into open space under its own drive, the helm let go of
	/// for the crossing (nothing to steer between the stars yet), and the view goes over to the stars. True if it went.
	/// </summary>
	bool EngageCruise(const cs::Ref<Core::Ship>& ship, double jd);

	/// <summary>
	/// A frame of the crossing: the charts follow the hull, the far system is built (hidden) half way, and once the hull
	/// has dropped out into it the helm is taken again there; lost out there, the session ends.
	/// </summary>
	void UpdateFtlCruise();

	/// <summary>The view between the stars during the crossing: from where the hull is, looking the way it goes.</summary>
	void UpdateFtlCruiseView(float dt);

	/// <summary>The crossing is over (arrived, or lost): back to the view of a system.</summary>
	void EndFtlCruise();

	/// <summary>The clock back to the pace it had before the drive was spooled.</summary>
	void EndFtlWarp();

	/// <summary>The helm let go of mid-charge or mid-run: the drive stops where it is, the autopilot lets go and the
	/// course (the hull's, from this system) is forgotten.</summary>
	void StopFtl();

	// ---------------------------------------------------------------- navigation (U.SPC-T420)

public:
	/// <summary>
	/// Plots a course for the hull at the helm to <paramref name="target"/>, from where it is now: the run out to the rim,
	/// the charge, the light-years, any well or projector on the way, the ETA and the fuel. Said in a toast unless
	/// <paramref name="quiet"/>. True if it can be flown.
	/// </summary>
	bool PlotFtlCourse(const cs::Ref<Core::FtlTarget>& target, bool quiet = false);

	/// <summary>A plotted course in a line, or why it cannot be flown.</summary>
	static cs::String CourseSummary(const cs::Ref<Core::FtlCourse>& c);

	/// <summary>Clears the plotted course (and lets go of the autopilot flying it).</summary>
	void ClearFtlCourse();

private:
	/// <summary>Plots a course to the next star or beacon out from the one plotted (<paramref name="step"/> 1) or the one
	/// before (-1), nearest first.</summary>
	void CycleFtlCourse(int step);

public:
	/// <summary>
	/// Engages the FTL autopilot for the plotted course (or, with none, the star the nose is on), plotted afresh from
	/// where the hull is: it flies out beyond the rim on the clock's long trip compression, turns onto the star, spools
	/// and engages. True if it took the helm.
	/// </summary>
	bool EngageFtlAutopilot();

private:
	/// <summary>Aligned: waits for a drive still cooling, else starts the charge. False if the autopilot let go.</summary>
	bool BeginFtlAutopilotSpool(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::FtlAutopilot>& ap, double jd);

	/// <summary>The autopilot lets go of the helm where the hull is, moving as it was (<paramref name="why"/> said in a
	/// toast, unless null), and a charge it had started is called off.</summary>
	void DisengageFtlAutopilot(const cs::String& why);

	/// <summary>The plotted course worked out again, now and then, from where the hull has got to (while nothing flies it).</summary>
	void RefreshFtlCourse(const cs::Ref<Core::Ship>& ship);

public:
	/// <summary>The way to the plotted course's star or beacon from the hull (unit), and how far, km; false with none.</summary>
	bool PlottedBearing(Core::DVec3& direction, double& km);

private:
	/// <summary>
	/// A frame with the autopilot at the helm: the run out flown closed-form on the sim clock, the turn onto the star, and
	/// the charge held on it until the drive engages; a fight, hunters closing, the drives knocked out or End let go of
	/// it. True if it flew the hull this frame (the stick is not read), false if it let go.
	/// </summary>
	bool UpdateFtlAutopilot(const cs::Ref<Core::Ship>& ship, float dt);

	/// <summary>The hull's nose put on <paramref name="forward"/> (ecliptic, unit), its roof kept as square to it as it was.</summary>
	void SetPilotFacing(Core::DVec3 forward);
