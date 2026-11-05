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

	/// A trip to another star (US-T43): the party shares one system, so a trip is the party's, and none starts while
	/// another player holds a seat or the helm there. True: refused.
	bool CoopRefusesTravel(const cs::Ref<Core::StarSystem>& system);

	/// The trip itself (the tail of TravelTo), into the frames: carried out at its frame on every peer, where the host's
	/// view makes it and every peer arrives with the host's Activate (a post step).
	bool CoopPartyTrip(const cs::Ref<Core::StarSystem>& system);

	/// The name of a player other than `seat` holding a seat or the helm, or empty.
	cs::String CoopOtherAtControls(int seat);

	/// Whether this player is at some controls, or in co-op anyone is (a reset of time or a trip would take them).
	bool CoopAtControls();

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
	/// Loads those bytes as a save is loaded (the original's ApplyLoaded): every peer's world from the same bytes; then
	/// (US-T43) the sync's state: the host's shared settings, who flies the helm, every player's seat taken again.
	void CoopApplySave(const cs::List<uint8_t>& bytes, const cs::String& state);
	/// What a sync carries besides the save (US-T43): "helm <seat>" and a "seat <player> <key>" per player holding one.
	cs::String CoopSyncState(bool keepView);

	/// A cheap digest of the shared state, the C# track's fields in the C# track's text: the clock as the frame advanced
	/// it, every system's date, detail and counts, every faction's treasury, and in the active system every ship's
	/// position, every settlement's people and stock, every battle unit, and who holds which seat; hashed with FNV-1a.
	cs::String CoopDigest();
	/// US-T47: the strategy layer and statecraft, as the galaxy map's screens write them (the C# track's text): the polity
	/// the party speaks for, every polity's directives, the objectives, pacts, proposals waiting and the record, and between
	/// every two states how they stand and what each thinks of the other.
	void CoopDigestStatecraft(cs::String& sb);

	/// Where this machine's player is, for the others' markers: the seat held, else the camera, as a body of the active
	/// system and a true-km offset from it.
	void CoopPresence(int& body, Core::DVec3& offsetKm);

	/// Co-op's HUD (an OnGUI handler after the app's): who is playing, what each holds and selected, and a marker with
	/// the name on every other player where they are.
	void CoopGui();

	// ==================================================================== US-T43: the helm, the cockpit, and the Core's other writers

private:
	/// The player flying the helm (the app's one pilot loop, Piloted), or -1. Every peer flies it, on that player's input;
	/// only that player's machine treats it as its own (IsPiloting outside the shared steps, its view, its toasts).
	int coopHelmSeat = -1;
	/// Above 0 inside a step every peer runs alike on the shared state (the pilot loop, a frame's entry carried out, a
	/// synced load): there the helm is everyone's.
	int coopShared = 0;
	/// Above 0 where every peer makes the same Activate itself (no post step needed).
	int coopEverywhere = 0;
	/// Above 0 while a frame's "x" entry is carried out (its own hooks do not send it again).
	int coopExecuting = 0;
	bool coopExecMine = false;
	/// Above 0 while another player's action or helm is replayed here: their toasts are theirs.
	int coopMuteToasts = 0;
	/// This frame's hands at the helm (the holder's), from the frame's "pi" entry.
	std::optional<Coop::PilotFrame> coopPilotFrame;
	std::optional<bool> coopPanelOverride;
	long long coopKeyTick = -1;
	static inline float CoopLookSensitivity = std::numeric_limits<float>::quiet_NaN();
	/// The camera up frame a fresh helm is levelled on, while the holder's frame is flown (its camera's, not this one's).
	static inline std::optional<Quaternion> CoopUpFrame;
	/// The asker's "now" while a reset of time is carried out (every peer resets to the same date).
	std::optional<double> CoopNow;

public:
	int CoopHelmSeat() const { return coopHelmSeat; }
	int CoopPilotFrames = 0;
	int CoopCockpitKeys = 0;
	int CoopActions = 0;
	/// The HUD (its "x" entries are carried out through it).
	UI::Hud* CoopHud() const { return hud.get(); }
	bool CoopActingHere() const { return CoopActing(); }
	/// A target key (CoopTargetKey) read back in the active system.
	Target CoopTarget(const cs::String& key) { return CoopResolveTarget(key); }
	/// In co-op an action that writes the Core goes into the frames as "x <seat> <verb> <args>" and is carried out at its
	/// frame on every peer (CoopExecute), which calls the same method again: there (and in single-player) this is false.
	bool CoopSends(const cs::String& verb, const cs::String& args = cs::String());

private:
	bool CoopHelmHere() const;
	bool CoopHelmMine() const;
	bool CoopIsClient() const;
	bool CoopExecuting() const { return coopExecuting > 0; }
	/// Whether this machine is the one acting (single-player, its own input, or its own entry carried out).
	bool CoopActing() const;
	void CoopShared(const std::function<void()>& step);
	/// A spawn at this machine's camera (the dev grid, the practice fighter): where, in true km, goes with it.
	bool CoopSpawns(const cs::String& verb, const cs::String& args, int frame, Core::DVec3 offsetKm, Core::DVec3 forward);
	/// A take of the helm from this machine's input goes into the frames as "h <seat> take <ship> <up frame>".
	bool CoopRoutesHelmTake(const Target& t);
	/// A release from this machine's input (P, another seat taken): into the frames.
	bool CoopRoutesHelmRelease();
	void CoopHelmEntry(Coop::CoopPeer& peer, int seat, const std::vector<cs::String>& f);
	Coop::PilotFrame CoopLocalPilotFrame();
	void CoopHostPilotFrame(Coop::CoopPeer& peer, Coop::FrameRecord& rec);
	/// UpdatePiloting (the original's whole pilot loop) on every peer alike, on the helm holder's frame of input.
	void CoopUpdatePiloting(float dt);
	Target CoopResolveTarget(const cs::String& key);
	bool CoopAppEntry(Coop::CoopPeer& peer, const cs::String& e);
	void CoopCockpitKey(Coop::CoopPeer& peer, int seat, const std::vector<cs::String>& f);
	void CoopExecute(Coop::CoopPeer& peer, int seat, const cs::String& verb, const cs::String& args);
	void CoopSpawnShip(Coop::CoopPeer& peer, int seat, Core::ShipClass cls, bool hostile, const std::vector<cs::String>& a, int at);
	void CoopSpawnPractice(Coop::CoopPeer& peer, int seat, const std::vector<cs::String>& a, int at);
	void CoopRetake(const std::vector<std::pair<int, cs::String>>& seats);
	/// A client's load asks the host to load that slot of its own (true: asked).
	bool CoopRoutesLoad(const cs::String& slot);
	/// The host's own load: its helm is everyone's (shared), and stays with whoever flew it, else the host.
	void CoopLoadingHere(const std::function<void()>& load);
