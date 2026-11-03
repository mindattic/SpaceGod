// UnitySpaceApp.Lanes.inl: included INSIDE the UnitySpaceApp class body (UnitySpaceApp.h). Port of Runtime/UnitySpaceApp.Lanes.cs.
//
/// The player riding the hyperlanes (U.SPC-T41). Flying the hull in through an on-ramp's ring gate
/// (drawn by <see cref="Views.LaneView"/>) joins the lane by the same <see cref="Fleet.JoinLane"/> /
/// <see cref="Fleet.LaneProgress"/> path AI traffic rides: the hull is drawn on the lane and the chase
/// camera rides the tube with it, the clock runs fast enough that a ride of hours takes under a minute,
/// and at the far ramp the pilot comes off (or, having picked one, carries on along another lane from
/// there). Going in and coming out the gate flashes and the view kicks wide; on the lane, tunnel
/// streaks stream past and the field of view opens with the ride's speed, closing again as it eases
/// to rest at the off-ramp.
///
/// The lane is a chain of rings the hull flies through (U.SPC-T379, drawn by <see cref="Views.LaneView"/> when
/// near enough to see), speeding it up through the first half and braking it through the second; each
/// system's network leads on to its jump gates by a spur per gate, joined by flying through the ring at
/// either end the way it runs, so a ride can end in range of a gate and, through it, pick up the far
/// system's own network at the spur beside its gate.

public:
	/// <summary>Whether the piloted hull is riding a hyperlane right now.</summary>
	bool RidingLane();

	/// <summary>The lanes the ride's off-ramp leads on to (keys 2 and up), and the one picked (-1: come off, key 1).</summary>
	cs::List<Core::LaneHop> RideOnward;
	int RideChoice = -1;

	/// <summary>The ring chain of the lane ridden (U.SPC-T379): how many rings it strings, how many the hull has flown
	/// through, and whether they are speeding it up or braking it; 0 rings when not riding.</summary>
	int RideRingCount(int& passed, bool& accelerating);

	/// <summary>The ride's speed as a fraction of its peak (0 at either ramp, 1 midway).</summary>
	float LaneRideSpeed = 0;

	/// <summary>The ring gate's flash as the hull goes through it (1, fading to 0).</summary>
	float GateFlash = 0;

	/// <summary>Tunnel streaks still fading after coming off a lane (1, fading to 0).</summary>
	float LaneExitFade = 0;

	/// <summary>How strongly the tunnel streaks show: with the ride's speed, and briefly after it.</summary>
	float LaneStreaks();

	/// <summary>Real seconds left of the ride at the clock's present rate, or 0 when not riding.</summary>
	double RideSecondsLeft();

private:
	/// <summary>A ride of hours runs at the slowest warp that gets it over in this many real seconds.</summary>
	static constexpr double RideRealSeconds = 45;

	static constexpr float LaneFov = 104.0f, GateFovKick = 24.0f, GateFlashS = 0.7f, LaneExitFadeS = 1.6f;

	/// <summary>The warp the clock ran at before the ride (restored coming off), or -1 when not riding.</summary>
	int rideWarpBefore = -1;

	/// <summary>Whether the hull was inside a ring gate last frame: a ride starts only on flying in, not on
	/// taking the helm at a ramp or sitting at the off-ramp just come out of.</summary>
	bool insideRampGate = false;

	float fovKick = 0;

	// ---------------------------------------------------------------- interdiction (U.SPC-T42)

public:
	/// <summary>The armed interdictor the ride's scanners have picked up ahead, or null (see <see cref="War.ThreatAhead"/>).</summary>
	cs::Nullable<Core::LaneThreat> RideThreat;

	/// <summary>Real seconds, at the clock's present rate, until the pulse ahead can reach the hull (0 with none).</summary>
	double ThreatRealSeconds();

	/// <summary>Seconds left with drives and shields knocked out by a pulse (0 when they are up).</summary>
	double PilotStunSeconds();

	/// <summary>Whether the hull, out of its lane part-way (dropped out, or thrown out and fought clear), can get back in where it left (Y).</summary>
	bool CanRejoinLane();

private:
	/// <summary>A warning shows long enough to answer: the clock slows until the countdown takes at least this many real seconds.</summary>
	static constexpr double WarningRealSeconds = 10;

	/// <summary>The interdictor last warned of (a new one warns afresh), and whether this hull left a lane part-way and may get back in.</summary>
	cs::Ref<Core::Ship> warnedOf;
	bool laneRejoinable = false;

	bool rideBoostHeld = false, rideDropHeld = false;

	/// <summary>
	/// The scanners' look ahead along the lane: an interdictor's field picked up slows the clock so its countdown can be
	/// read and answered (Shift to surge through, Ctrl to drop out short of it); once it is behind, the ride's pace comes back.
	/// </summary>
	void UpdateRideThreat(const cs::Ref<Core::Ship>& ship, double jd);

	/// <summary>Shift: the rest of the ride surged through at <see cref="Fleet.SurgeFactor"/> times the pace, the boost charge spent on it.</summary>
	void SurgeThroughLane(const cs::Ref<Core::Ship>& ship);

	/// <summary>Ctrl: off the lane right here, on the pilot's own terms, coasting out at the hull's own top speed.</summary>
	void DropOutOfLane(const cs::Ref<Core::Ship>& ship);

	/// <summary>Y, out of a lane near where the hull left it: back in, easing onto the lane line, and on to the far ramp.</summary>
	void UpdateLaneRejoin(const cs::Ref<Core::Ship>& ship);

	/// <summary>Where the hull was last frame, and in which system, for flying through a gate spur's ring (U.SPC-T379); null after a ride or a jump.</summary>
	cs::Nullable<DVec3> lastSpurCheckHelio;
	cs::Ref<Core::StarSystem> lastSpurCheckSystem;

	/// <summary>
	/// The gate spur ring the hull has just flown through this frame, heading along the spur (U.SPC-T379), or null.
	/// A spur is joined by threading its ring the way it runs rather than by being inside it, so a hull just come out
	/// of a gate (right beside the spur's ring there) or just brought off at its ramp is not swept back in.
	/// </summary>
	cs::Nullable<Core::LaneHop> SpurRingFlownThrough(const cs::Ref<Core::Hyperlanes>& lanes, DVec3 helio);

	/// <summary>What the war has to say about the lane ridden (a pulse slipped, or one that caught the hull), as toasts.</summary>
	void ShowWarNotices();

	/// <summary>Flying in through an on-ramp's ring gate, out of any fight or salvage, starts a ride.</summary>
	void UpdateLaneEntry(const cs::Ref<Core::Ship>& ship);

	/// <summary>Sets the helm up for a ride the hull has just joined (flying in, carrying on at a junction, or taken mid-ride).</summary>
	void BeginRide(const cs::Ref<Core::Ship>& ship, bool throughGate);

	/// <summary>The ride goes on, or is over: the clock back to the pace it had before.</summary>
	void EndRide();

	void GateEffect(const cs::Ref<Core::Lane>& lane, int at);

	/// <summary>
	/// A frame of the ride: the hull goes where its lane has it (so the sensors, the law and the HUD see it
	/// there), the number keys pick the off-ramp's exit, and at the far ramp it comes off or carries on.
	/// </summary>
	void UpdateLaneRide(const cs::Ref<Core::Ship>& ship);

	/// <summary>The far ramp: on along the lane picked, or off through the gate into the system there, coasting out at the hull's own speed.</summary>
	void ReachRamp(const cs::Ref<Core::Ship>& ship);
