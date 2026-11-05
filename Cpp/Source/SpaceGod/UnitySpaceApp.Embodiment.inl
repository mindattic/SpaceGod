// UnitySpaceApp.Embodiment.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// Port of Runtime/UnitySpaceApp.Embodiment.cs; bodies in UnitySpaceApp.Embodiment.cpp.
//
// <summary>
// Take charge of anything, any time (U.SPC-T422, see docs/EMBODIMENT.md). From the strategy view, ' and ; jump
// straight into the next seat in the fight in view (a ship's helm, a launched fighter's or bomber's cockpit) and /
// mans a gun on the hull flown or selected, or on a station's defence platform (U.SPC-T445: the eye at the mount, looking
// down the barrel; Backspace moves up to the hull's helm); P hands whatever is held back to its AI and returns to command, the
// view and time warp the commander had. Nothing pauses and nothing loads: the clock runs on at real time while a
// seat is held, the camera glides from seat to seat, and every other seat goes on under its own AI. A ship's helm
// is flown by the existing pilot loop (<see cref="EnterPiloting"/>); every other seat is held by
// <see cref="session"/> and driven here through <see cref="IEmbodiable.Control"/>.
// </summary>
//
// C# nulls of string (ReturnToCommand's why / lostName, SeatLine's result) are empty strings here.

private:
	/// <summary>
	/// The session every non-helm seat (a fighter's cockpit, a gun mount) is held through (U.SPC-T410, docs/BIBLE.md
	/// U.SPC-LAW-1): input here only submits commands stamped with <see cref="LocalSession.LocalPlayer"/>, and reads back
	/// what the session's authority says the local player holds. Replaced with a fresh one when a game is loaded.
	/// </summary>
	cs::Ref<Core::LocalSession> session = Coop::CoopPeer::NewSession(); // US-T44: LocalSession::Create(/*seed:*/ 0) single-player; every seat in co-op

	/// <summary>What the local player holds at a non-helm seat, as the session's state says, or null.</summary>
	cs::Ref<Core::IEmbodiable> HeldNonHelm() const;

	/// <summary>Hands the local player's seat back to its AI, through the session.</summary>
	void ReleaseSeat();

	/// <summary>Takes <paramref name="next"/> for the local player through the session, handing back what was held: true
	/// if it is held now (as <see cref="EmbodimentSession.Switch"/>).</summary>
	bool SwitchSeat(const cs::Ref<Core::IEmbodiable>& next);

	/// <summary>Moves the local player up from a mounted seat to its host, through the session: true if the host is held now.</summary>
	bool SeatToHost(const cs::Ref<Core::IEmbodiable>& host);

	/// <summary>Where the stick points at a seat, Unity axes (unit): the mouse swings it, the view follows it.</summary>
	Vector3 seatAim = Vector3::forward();

	/// <summary>What the commander had before taking a seat: their time warp and what the camera was following,
	/// restored on <see cref="ReturnToCommand"/>. <see cref="commandWarp"/> is -1 when nothing is remembered.</summary>
	int commandWarp = -1;

	Target commandFocus = Target::None();

	/// <summary>US-T90: whether the embodied craft (true) or the command view and its camera (false) owned this frame's
	/// input, decided once as the frame starts (<see cref="ArbitrateInput"/>).</summary>
	bool craftHasInput = false;

	/// <summary>US-T90: the owner changed and the player's hands have not yet let go of what they held across it.</summary>
	bool inputHandover = false;

	/// <summary>
	/// US-T90, one owner per input per frame: <paramref name="frameStart"/>, decides who owns this frame (the craft held,
	/// else the command view) and strips the shared keys of the other's meanings; after the frame's commands, notices a
	/// seat taken or let go part-way through the frame and gives the new owner an idle frame. Whatever was held across a
	/// change (the stick, a trigger, the look) stays the old owner's until it is let go, so leaving a helm with W down does
	/// not swim the camera out of the focus it went back to, and taking a seat mid-burn does not open the throttle.
	/// </summary>
	void ArbitrateInput(bool frameStart);

public:
	/// <summary>True while a non-helm seat (a fighter, a gun) is held.</summary>
	bool IsSeated() const;

	/// <summary>What is held at a non-helm seat, or null.</summary>
	cs::Ref<Core::IEmbodiable> Seat() const;

	/// <summary>The player is at some controls (a helm or any other seat): the strategy view's keys and clicks stand aside.</summary>
	bool AtControls();

private:
	/// <summary>Keeps the commander's view and time warp, once, as a seat is first taken from command.</summary>
	void RememberCommand();

	/// <summary>
	/// Takes <paramref name="next"/>, handing back whatever was held first: a ship's helm through the pilot loop, any
	/// other seat through <see cref="session"/>. The clock drops to real time (a fight is flown at the pace it is fought)
	/// but never stops, and the camera glides to the new seat.
	/// </summary>
	void TakeSeat(const cs::Ref<Core::IEmbodiable>& next);

	static cs::String SeatKeys(const cs::Ref<Core::IEmbodiable>& s);

	/// <summary>How far above its mount the eye at a gun sits, in hull radii: just clear of the barrel, so the shot
	/// is seen leaving it and the hull's own plating does not fill the view.</summary>
	static constexpr double MountEyeRise = 0.04;

	/// <summary>
	/// Where the eye sits at a manned gun (U.SPC-T445): on the mount, as drawn on its hull (the mount's own offset from
	/// the hull's centre, at battle scale, from wherever the hull is drawn), lifted a touch off the deck.
	/// </summary>
	Core::DVec3 MountEye(const cs::Ref<Core::MountedTurret>& t);

	/// <summary>The roof of the view down a barrel: the mount's own (its hull's deck), or the view's up when the barrel
	/// points straight along it.</summary>
	Vector3 MountRoof(const Core::EmbodimentPose& pose, Vector3 aim);

	/// <summary>
	/// Backspace at a gun mount (U.SPC-T445): up from the gun to what it is mounted on, its <see cref="IEmbodiable.Host"/>.
	/// A ship's helm is then flown by the pilot loop, as any helm is (<see cref="TakeSeat"/>); any other host (a whole
	/// station, U.SPC-T447) is taken through <see cref="EmbodimentSession.MoveToHost"/>, the view leaving the mount for
	/// the chase about it.
	/// </summary>
	void MoveSeatToHost();

	/// <summary>What the camera rides with at a seat: the craft itself, or the hull a gun is mounted on.</summary>
	static Target SeatTarget(const cs::Ref<Core::IEmbodiable>& s);

	/// <summary>
	/// Hands whatever is held back to its AI and returns to command: the camera goes back to what the commander was
	/// following (or stays free where the seat was) and time to the warp they had. <paramref name="why"/>, when given,
	/// says why the seat was lost.
	/// </summary>
	/// (Empty why / lostName stand for C#'s null.)
	void ReturnToCommand(const cs::String& why = cs::String(), const cs::String& lostName = cs::String());

	/// <summary>The fight the strategy view is on: the seat's, the flown hull's, the selection's or the followed ship's,
	/// else the battle nearest the camera.</summary>
	cs::Ref<Core::Battle> BattleInView();

	/// <summary>US-T90: every seat in <paramref name="battle"/> the player could take right now: <see cref="Embodiables.SeatsIn"/>
	/// (each helm and cockpit) and then each station fighting there, whose guns / reaches from it.</summary>
	cs::List<cs::Ref<Core::IEmbodiable>> SeatsInFight(const cs::Ref<Core::Battle>& battle);

	/// <summary>Where a seat is in the scene.</summary>
	Core::DVec3 SeatScene(const cs::Ref<Core::IEmbodiable>& s);

	/// <summary>
	/// ' / ; (U.SPC-T422): from command, the seat in the fight in view nearest the middle of the screen; at a seat or
	/// a helm, the next (or previous) one in the same fight.
	/// </summary>
	void CycleSeat(int step);

	/// <summary>
	/// / (U.SPC-T422): the next gun mount on the hull held (or, from command, the one selected in a fight); past its
	/// last gun, back up to its helm.
	/// </summary>
	void ManNextGun();

	// ---------------------------------------------------------------- the helm's abilities (US-T106)

	/// <summary>The craft the local player's helm orders launched, newest first, for <see cref="JumpToCraft"/>.</summary>
	cs::List<cs::Ref<Core::BattleUnit>> launchedCraft;
	int helmAbilitySeq = 0;

	/// <summary>The carrier whose craft Shift+/ jumps between: the hull flown at the helm, or the carrier of the craft held;
	/// null when neither is in a fight.</summary>
	cs::Ref<Core::BattleUnit> JumpCarrier(cs::Ref<Core::Battle>& battle);

	/// <summary>Shift+/ (US-T106): into the newest craft the carrier launched; from one of its craft, the next; past the last,
	/// back to the carrier's helm. Every switch is the seats' instant one (<see cref="TakeSeat"/>).</summary>
	void JumpToCraft();

	/// <summary>Backspace in a launched craft (US-T106): back to the helm of the carrier it flew from, if that hull is still
	/// in the fight. True if it handled the key.</summary>
	bool BackToCarrier();

public:
	/// <summary>Shift+1..0 at the helm (US-T106): the ability in that slot of the hull's bar, the number key spent so it is not
	/// also a captain's order; 0 when the key is no ability.</summary>
	int HelmAbilityKey(const cs::Ref<Core::Ship>& ship);

	/// <summary>After the helm has flown a frame: what the last ability order came to, toasted, and its craft remembered.</summary>
	void NoteHelmAbility();

	/// <summary>US-T93 at the helm: Tab an active ping, Shift+F3 go dark (or, dark, power up), as a
	/// <see cref="Core.SensorKey"/> for the helm to land; said on a toast, or why not. 0 for no key.</summary>
	int SensorCommand(const cs::Ref<Core::Ship>& ship);

private:

	// ---------------------------------------------------------------- the picker (U.SPC-T446)

public:
	/// <summary>The take-charge picker (F9): every seat on the thing in hand and around the camera, to click into.</summary>
	bool ShowPicker = false;

private:
	/// <summary>How many ships near the camera, outside any fight, the picker offers the helms of.</summary>
	static constexpr int PickerNearbyShips = 12;

	cs::List<cs::Ref<Core::IEmbodiable>> pickerSeats = cs::List<cs::Ref<Core::IEmbodiable>>();
	float pickerBuiltAt = -10.0f;

	void TogglePicker();

public:
	/// <summary>What the player holds now, a helm or any other seat, or null in command.</summary>
	cs::Ref<Core::IEmbodiable> HeldSeat();

	/// <summary>
	/// The picker's list (U.SPC-T446), rebuilt a few times a second from <see cref="Embodiables.Pickable"/>: the seats on
	/// the hull held (or flown, or selected) and its gun mounts, then every seat in the fight in view, then the helms of
	/// the ships nearest the camera. Never a list of its own: a new kind registered in <see cref="Embodiables"/> shows here.
	/// </summary>
	cs::List<cs::Ref<Core::IEmbodiable>> PickerSeats();

private:
	/// <summary>The <paramref name="count"/> ships in view of the system nearest the camera and not in a fight (those
	/// come with their fight), nearest first.</summary>
	cs::List<cs::Ref<Core::Ship>> ShipsNearCamera(int count);

public:
	/// <summary>
	/// A seat clicked in the picker: taken through <see cref="TakeSeat"/>, which always hands the seat held back to its
	/// AI first (<see cref="EmbodimentSession.Switch"/>, or the pilot loop for a helm). The picker closes so the mouse
	/// goes back to the stick.
	/// </summary>
	void PickSeat(const cs::Ref<Core::IEmbodiable>& next);

	/// <summary>The picker's Back to command button: what P does at the controls.</summary>
	void PickCommand();

private:
	/// <summary>
	/// The defence platform fighting at what is selected (U.SPC-T445): a station's own when a station or ring is
	/// selected, or any at the world selected, that still has a gun to man; null if none is in a fight there.
	/// </summary>
	cs::Ref<Core::BattleUnit> DefencePlatformAt(const Target& t, cs::Ref<Core::Battle>& battle);

	/// <summary>
	/// One frame at a non-helm seat: the mouse swings the stick (and the view after it), W/S work a craft's throttle,
	/// the triggers go to what is held, and a seat that is lost (shot down, the fight over) returns the player to
	/// command, saying why. Runs after <see cref="UpdatePiloting"/>, which clears the view's aim each frame.
	/// </summary>
	void UpdateSeat(float dt);

	/// <summary>
	/// The dogfight game's keys in a cockpit (U.SPC-T424): F flight assist, T a lock on the hostile nearest the crosshair
	/// (again for the next), Ctrl flares, and the number keys: 1 the next weapon group on the second trigger, 2-5 orders
	/// to the wing (attack my target, cover me, form up, engage at will), 6 the next weak point on the locked hull, 7/8/9
	/// a pip of power to weapons, shields or engines, 0 an even share.
	/// </summary>
	void UpdateCockpit(const cs::Ref<Core::FighterSeat>& f, Core::DVec3 aim);

	void OrderWing(const cs::Ref<Core::FighterSeat>& f, Core::WingOrder order);

	// ---------------------------------------------------------------- the self-destruct (US-T97)

	/// <summary>Shift+Backspace at any controls as a <see cref="Core.SelfDestructKey"/> for <paramref name="held"/>: the press
	/// aborts a countdown under way, held a second it arms one (not on a press that has just aborted). Toasts what it did,
	/// and keeps where the craft is while a countdown runs, for <see cref="NoteSelfDestruct"/>.</summary>
	int SelfDestructCommand(const cs::Ref<Core::IEmbodiable>& held);

	/// <summary>Once the craft the local player armed has gone off and they are back in command: what the blast did, and the
	/// camera (free where the chase left it) backed out to a safe distance, looking at it.</summary>
	void NoteSelfDestruct();

	bool selfDestructWatching = false, selfDestructAborted = false;
	int selfDestructSeq = 0;
	Core::DVec3 selfDestructScene;

public:
	/// <summary>The self-destruct key held now is the press that aborted a countdown: it arms nothing (the HUD's arming bar).</summary>
	bool SelfDestructHoldSpent() const { return selfDestructAborted; }

private:

public:
	/// <summary>The HUD's line at a non-helm seat (null when none is held): what is held, its state, and the keys out.</summary>
	/// (Empty stands for C#'s null.)
	cs::String SeatLine();
