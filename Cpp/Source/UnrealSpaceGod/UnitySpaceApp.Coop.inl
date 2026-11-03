// UnitySpaceApp.Coop.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// US-T44: co-op's side of the app, the C# track's Runtime/UnitySpaceApp.Coop.cs (US-T39) in C++; bodies in
// UnitySpaceApp.Coop.cpp. The ported originals call in here only at lines marked US-T44, and every call is the
// original's own behaviour when Coop::CoopPeer::Current() is null (single-player). An empty string stands for C#'s null.

private:
	std::weak_ptr<Core::LocalSession> coopWatched;
	Coop::ClockState coopLeft;
	bool coopLeftValid = false;
	double coopSimRate = std::numeric_limits<double>::quiet_NaN();
	bool coopStepped = false;
	cs::String coopLastControl;
	cs::String coopLastSelection = "n";

	/// Clock->Advance(dt) (the original's), in lockstep in co-op. On the host: the clients' clock requests, the frame's
	/// commands, then the clock as it stands is recorded and advanced. On a client: the same commands, any change this
	/// machine made to its clock since the last frame goes to the host as a request, then the clock is put in the host's
	/// state and advanced by the host's seconds, which lands it on the host's date to the bit.
	void CoopAdvance(float dt);

	/// A client's warp or pause, on the host's clock (the one every peer's follows).
	void CoopClockRequest(int seat, const cs::String& request);

	/// The frame's commands, in order, on every peer: session commands through the original's session (its
	/// SeatAuthority validates and applies them), roster entries to the co-op peer.
	void CoopApplyEntries(Coop::CoopPeer& peer, const std::vector<cs::String>& entries);

	void CoopShareSelection(Coop::CoopPeer& peer);

	/// World()->UpdateAdventures(Clock->Jd, PlayerHelioKm()) when `wanted` (the original's line). In co-op the host's
	/// call is everyone's: whether it ran this frame and from the host's player point.
	void CoopUpdateAdventures(bool wanted);

	void CoopWatchSession();

	/// What the session's authority said, on every peer: logged, and for this machine's player the seat's view, its
	/// loss, or the refusal.
	void CoopSessionEvent(const cs::Ref<Core::IGameEvent>& e);

	/// The tail of TakeSeat (the original's lines), run when the host's frame hands the seat over.
	void CoopSeatTakenHere(const cs::Ref<Core::IEmbodiable>& next);

	/// In co-op a take goes to the host as a command, with the seat's key; the view follows when a frame grants it.
	/// False in single-player: TakeSeat goes on as the original.
	bool CoopTakeSeat(const cs::Ref<Core::IEmbodiable>& next);

	/// A release or a move up to the host, in co-op: to the host as a command (true); false in single-player.
	bool CoopRoutes(const cs::Ref<Core::IGameCommand>& command);

	/// Co-op's UpdateSeat: this machine's stick goes to the host as the local player's control command (when it
	/// changes), and every player's seat is stepped, here as on every peer, on the frame's sim seconds.
	void CoopUpdateSeat(float dt);

	/// The pilot loop (the helm) acts on the Core from this machine's keys every frame and is not lockstepped yet: in
	/// co-op it is refused (true) and the other seats are the way in.
	bool CoopRefusesHelm();

	/// A trip to another star from a client: the host picks the system everyone is in (true: refused).
	bool CoopRefusesTravel();

	/// At the end of Activate: the host's frame tells clients to make the same system the active one.
	void CoopActivated(const cs::Ref<Core::StarSystem>& system);

	/// At the end of ApplyLoaded: the clock is a new one; a load the host made itself goes to everyone.
	void CoopLoaded();

public:
	/// What a client does after replaying a host frame's Update: the frame's post steps.
	void CoopApplyPost(const Coop::FrameRecord& rec);

	/// A seat by what it is in the shared world, so every peer finds the same one: "F b u" a fighter's cockpit, "T b u m"
	/// a gun, "S i" a station (i-th of Embodiables::StationsIn), "SB b u" a station fighting as unit u. Empty for one
	/// co-op does not share.
	cs::String CoopSeatKey(const cs::Ref<Core::IEmbodiable>& seat);
	cs::Ref<Core::IEmbodiable> CoopResolveSeat(const cs::String& key);

	/// A selection as every peer can read it back: "n", "b <body>", "s <ship id>", "u <battle> <unit>", else "o <kind>".
	cs::String CoopTargetKey(const Target& t);
	/// A selection key's name in the active system, for the HUD (empty for nothing).
	cs::String CoopTargetName(const cs::String& key);

	/// Why a sync save cannot be taken now (between the stars, under FTL), or empty.
	cs::String CoopSaveBlocker() const;
	/// The whole simulation as Core/Persistence writes it (what a save slot holds).
	cs::List<uint8_t> CoopCaptureSave();
	/// Loads those bytes as a save is loaded (the original's ApplyLoaded): every peer's world from the same bytes.
	void CoopApplySave(const cs::List<uint8_t>& bytes);

	/// A cheap digest of the shared state, the C# track's fields in the C# track's text: the clock as the frame advanced
	/// it, every system's date, detail and counts, every faction's treasury, and in the active system every ship's
	/// position, every settlement's people and stock, every battle unit, and who holds which seat; hashed with FNV-1a.
	cs::String CoopDigest();

	/// Where this machine's player is, for the others' markers: the seat held, else the camera, as a body of the active
	/// system and a true-km offset from it.
	void CoopPresence(int& body, Core::DVec3& offsetKm);

	/// Co-op's HUD (an OnGUI handler after the app's): who is playing, what each holds and selected, and a marker with
	/// the name on every other player where they are.
	void CoopGui();
