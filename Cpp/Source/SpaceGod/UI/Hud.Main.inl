// Hud.Main: the members of Unity.SpaceGod's Runtime/UI/Hud.cs lines 1-1222 and 3746-3868 (fields, Draw, saves, world
// overlay, flight markers, the chase, aimed combat, hyperlane rides; formatting; styles), declared inside class Hud
// (Hud.h). Bodies in Hud.cpp; what they need in Hud.Main.deps.h.

private:
	UnitySpaceApp* app;
	bool stylesReady = false;
	cs::Ref<GUIStyle> panel, title, text, small, minorLabel, label, labelShadow, button, buttonActive, row, rowSelected, toast;
	cs::Ref<Texture2D> panelTexture, rowHoverTexture, rowSelectedTexture, buttonTexture, buttonActiveTexture;

	Vector2 navigatorScroll;
	bool navigatorOpen = false;
	float lastRowClick = -10.0f;
	Target lastRowTarget = Target::None();

	/// The crate currently picked up by a source click in the cargo panel, awaiting a
	/// destination click; null means nothing is picked up.
	cs::Ref<Core::CargoItem> cargoSelected;
	cs::Ref<Core::CargoHold> cargoSelectedHold; // the hold the picked-up crate is in: a crate never carries over to another ship's hold

	/// Crates per manual Buy/Sell click in the market board, shared by every good's row.
	int tradeCrates = 1;

	/// Dev/sandbox spawn panel: which faction the next spawned ship joins.
	bool spawnHostile = false;

	/// Collapse state for panels that can sit over the 3D view and get in the way --
	/// same "[-]/[+] TITLE" bar <see cref="DrawNavigator"/> already uses, so Market/Cargo/Help can
	/// be shrunk to just their title bar without closing them outright.
	bool marketOpen = true, cargoOpen = true;

	/// The outfitting panel's chosen slot (-1: none) and how far its slot list is scrolled.
	int outfitSlot = -1;
	Vector2 outfitScroll;

	/// How far the selection card's lines are scrolled when they are too many for the screen, and for which target.
	Vector2 selectionScroll;
	Target selectionScrollFor = Target::None();

public:
	static constexpr Color Accent{0.45f, 0.85f, 1.0f};
	static constexpr Color Dim{0.62f, 0.7f, 0.78f};

private:
	/// An unidentified contact: no faction colour until the scanners know whose it is.
	static constexpr Color ContactGrey{0.78f, 0.8f, 0.82f};

	/// The piloted ship's own sensor picture, or null when only watching (then everything is shown).
	cs::Ref<Core::ContactBook> Picture() const;

	/// Where each contact was last drawn while the scanners held it, so a lost one stays marked there.
	cs::Dictionary<cs::Ref<Core::Ship>, Core::DVec3> lastSeenScene;
	cs::Ref<Core::Fleet> lastSeenFleet;

	static constexpr Color LostEnemy{1.0f, 0.35f, 0.3f, 0.8f};
	static constexpr Color LostAlly{0.35f, 0.65f, 1.0f, 0.8f};
	static constexpr Color LostNeutral{ContactGrey.r, ContactGrey.g, ContactGrey.b, 0.7f};

	/// A lost contact's question mark: red if hostile to the hull flown, blue if of its own flag, grey if
	/// neutral -- or if the scanners never made out whose it was.
	Color LostColor(const cs::Ref<Core::Contact>& contact);

	/// Where a ship is drawn: where it is, unless it is a contact the scanners have lost -- then where they last
	/// had it (false if they never had it here).
	bool ContactScene(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::Contact>& contact, Core::DVec3& scene);

	/// The galaxy map screen ([), made the first time it is opened; and whether it was up last frame.
	cs::Ref<GalaxyMapScreen> galaxyMap;
	bool galaxyMapOpen = false;

	/// The Help screen (H / F1, or Help in the Esc menu), made the first time it is opened.
	cs::Ref<HelpScreen> helpScreen;

	/// US-T152..T155: where every docked panel goes this draw (HudLayout.h has the rule), reset at the top of Draw.
	HudLayout layout;
	/// The news ticker's or the open news panel's rect, taken from TopCentre first thing in Draw so every alert and
	/// banner stacks under it (empty when neither is drawn).
	Rect newsRect;

	/// Takes TopCentre's first slot for the news (the ticker or the open feed) before anything else is drawn.
	void LayoutNews();

	/// The Bio Mass contact warning (US-T125) in the TopCentre stack, under the news (US-T154).
	void DrawBioMassWarning();

	/// Text cut to fit maxW in style, with an ellipsis (unchanged when it fits).
	static cs::String FitText(const cs::String& s, const cs::Ref<GUIStyle>& style, float maxW);

public:
	void Draw();

private:
	// ---------------------------------------------------------------- save and load (U.SPC-T67)

	/// C# string[] saveNames, null until first read (a Ref so null stays distinct from no saves).
	cs::Ref<cs::List<cs::String>> saveNames;
	cs::String nextManualSlot;
	cs::List<cs::Ref<Core::SaveHeader>> saveHeaders;
	float savesReadAt = -10.0f;
	Vector2 savesScroll;

	/// The save the player has asked to delete, waiting for the second click that confirms it (empty: none).
	cs::String deleteArmed;

	/// The saves (F6, or Save / Load in the pause menu), of the profile in use: New Save makes another of the
	/// player's own, and each row shows where and when it was saved, who was flying and their purse, with
	/// Save to overwrite it (the quicksave and the player's saves), Load, and Delete (asked twice). Save and
	/// Load act at the next frame.
	void DrawSaves();

	// ---------------------------------------------------------------- world overlay

	/// Between the stars: each star named, the two systems highlighted, and what the camera is doing.
	void DrawTravel();

	/// Between the stars under the hull's own FTL drive (U.SPC-T417): the stars' names, warp streaks pouring from the
	/// star ahead, and one caption: where it is bound, how far is left, and how long at this pace.
	void DrawFtlCruise();

	/// <summary>At a void stop between jumps (US-T124): the hull's sensor posture, any ghost closing, and the keys.</summary>
	void DrawVoidStopSensors(const cs::Ref<Core::Cruise>& c, double jd);

	/// The helm's FTL drive (U.SPC-T417): the star the nose is on and the key to spool for it (or, inside the system's
	/// gravity wells, how far out the rim is: canon, U.SPC-T455, no jump inside them), and the charge as it builds.
	void DrawFtl();

	/// A plotted course's numbers in a line: the run out, the jump, any interdictions, the ETA and the fuel (U.SPC-T420).
	static cs::String CourseLine(const cs::Ref<Core::FtlCourse>& c);

	/// The plotted course's star or beacon marked where it lies in the view, named, with how far (U.SPC-T420).
	void DrawCourseMarker();

	/// The names of the stars in the view between them (with who holds a system's).
	void DrawInterstellarLabels();

	/// One quiet caption for the whole trip, over the view itself: no panel or progress bar to read as a loading screen.
	void DrawBanner(const cs::String& banner);

	void DrawWorldOverlay();

	/// C# (Ship ship, Texture2D icon, float size, Color tint, Vector2 p): one far-off ship's icon this repaint.
	struct IconDraw
	{
		cs::Ref<Core::Ship> ship;
		cs::Ref<Texture2D> icon;
		float size = 0;
		Color tint;
		Vector2 p;
	};

	/// C# (Ship ship, Contact contact): a ship drawn as an icon whose name is wanted.
	struct IconName
	{
		cs::Ref<Core::Ship> ship;
		cs::Ref<Core::Contact> contact;
	};

	/// The far-off ships' icons this repaint, before they are folded into marks (U.SPC-T415).
	cs::List<Core::LodMark> iconMarks;
	cs::List<IconDraw> iconDraws;
	cs::Dictionary<cs::Ref<Core::Faction>, int> factionGroups;

	/// Which side an icon folds with: one group per faction.
	int GroupOf(const cs::Ref<Core::Faction>& f);

	/// Ship name labels drawn this repaint, so an icon's name can stay clear of them.
	cs::List<Rect> shipLabelRects;

	/// Ships drawn as icons whose names are wanted this repaint, drawn after the names that must show.
	cs::List<IconName> iconNamed;

	/// A ship's name beside it. <paramref name="iconName"/>: an icon's name (U.SPC-T376), drawn only while the ship is
	/// an icon and only where it overlaps no name already drawn. Every name is clickable, like the icon.
	/// <paramref name="must"/> (US-T157): the hovered, selected, followed or locked hull, named where it is whatever else is
	/// there; any other name (a flagship on sight, a dogfight's raiders) steps down a line or two to clear the names
	/// already drawn, and is left off if it still cannot.
	void DrawShipName(const cs::Ref<Core::Ship>& ship, const cs::Ref<Core::Contact>& contact, double ppu, bool iconName, bool must = true);

	/// US-T157: the names that are not a must, nearest first, and at most this many of them (the rest show as hulls).
	static constexpr int MaxOptionalNames = 6;
	cs::List<IconName> optionalNamed;

	/// Unconfirmed worlds are marked as such wherever they are named.
	static cs::String StatusMark(const cs::Ref<Core::BodyDefinition>& b);

	/// The other simulated systems shine in this one's sky: Alpha Centauri from Sol, the Sun in Cassiopeia from Alpha Centauri.
	void DrawSkyMarkers();

	/// Yards, factories, spacecraft and docking rings are named once they are drawn big enough to see.
	void DrawStructureLabels(double ppu);

	float ParentSeparation(const cs::Ref<Core::BodyDefinition>& moon, Vector2 p);

	// ---------------------------------------------------------------- flight markers (U.SPC-T59)

	static constexpr Color ProgradeColor{0.55f, 1.0f, 0.6f, 0.95f};
	static constexpr Color RetrogradeColor{1.0f, 0.62f, 0.35f, 0.95f};

	/// While piloting, the flight instruments a Newtonian hull needs: a boresight where the nose
	/// points, the prograde marker where the hull is actually going (its velocity relative to the
	/// body it flies near) and the retrograde marker opposite it, joined to the boresight by the
	/// velocity vector so a slide (flight assist off, or a hull still killing its drift) reads at a
	/// glance, and a readout of flight assist, speed and how far the track is off the nose.
	void DrawFlightMarkers();

	/// The helm's aim reticle: the boresight where the nose points and, where the stick points the hull elsewhere
	/// (U.SPC-T269: far ahead of the nose in free aim, a little ahead of it on a hull steered by the nose; a capital
	/// ship's helm is its heading, the nose itself), the crosshair. Returns whether the nose is on screen, and where.
	bool DrawAimReticle(Vector2& nose);

	/// The cleared screen (F11, U.SPC-T496): only the reticle in use -- a seat's sight when one is held (U.SPC-T422),
	/// else the helm's aim reticle while piloting -- and nothing at all in the strategy view or between the stars.
	void DrawReticleOnly();

	/// <summary>A fighter's seat (US-T129): the second reticle, the boresight where the craft's nose points, swinging over to
	/// the seat's crosshair (the middle of the view, where the mouse aims) as the craft turns after it. Hud.Sights.cpp.</summary>
	void DrawSeatNose();

	// ---------------------------------------------------------------- off-screen target arrows (US-T128)

	/// <summary>Arrows grow from <see cref="ArrowMinPx"/> (at <see cref="ArrowReachGuns"/> gun ranges or more) to
	/// <see cref="ArrowMaxPx"/> (alongside): true km against this craft's own gun reach, so they read the same at either scale.</summary>
	static constexpr float ArrowMinPx = 14.0f, ArrowMaxPx = 46.0f;
	static constexpr double ArrowReachGuns = 4;
	/// <summary>A gun reach to measure by for a craft that has none (a station seat with only point defence, say), km.</summary>
	static constexpr double ArrowFallbackGunKm = 20;
	/// <summary>At most this many hostile arrows, the nearest (a locked hull always has one).</summary>
	static constexpr int ArrowMaxCount = 16;
	static constexpr Color ArrowFriendlyColor{0.45f, 0.95f, 0.6f, 0.95f};
	static constexpr Color ArrowNeutralColor{0.85f, 0.85f, 0.8f, 0.9f};

	/// <summary>
	/// At any controls (US-T128): an arrow on the screen's edge for each target off it, pointing the way to turn: every hostile
	/// in the fight (the nearest named by its range), the locked hull ringed and named, the hunters running the ship down out
	/// of a fight, and a plotted course's next star or beacon. Bigger the closer it is, against this craft's own gun reach;
	/// coloured by stance (hostile red, own side green, neutral grey, the course its streak blue). The selection keeps its own
	/// pointer (<see cref="DrawOffscreenPointer"/>), drawn the same way. Hud.Sights.cpp.
	/// </summary>
	void DrawOffscreenArrows();

	/// <summary>One edge arrow, toward the camera-relative <paramref name="rel"/>, <paramref name="size"/> px, with an optional
	/// label inside it and a lock ring round it.</summary>
	void DrawEdgeArrow(Vector3 rel, float size, Color tint, const cs::String& text_, bool ringed);

	/// <summary>The arrow's size for a target <paramref name="km"/> off, against <paramref name="reachKm"/> (nothing, the least).</summary>
	static float ArrowSize(double km, double reachKm);

	/// <summary>The reach an arrow is sized against: <see cref="ArrowReachGuns"/> times the craft's longest gun, km.</summary>
	static double ArrowReachKm(const cs::Ref<Core::BattleUnit>& me);
	static double LongestGunKm(const cs::Ref<Core::BattleUnit>& u);

	/// <summary>Hostile red, own faction green, anyone else grey (<see cref="Core::Diplomacy.Hostile"/>).</summary>
	Color StanceColor(const cs::Ref<Core::Faction>& mine, const cs::Ref<Core::Faction>& theirs);

	/// <summary>The unit held at the controls (the helm's hull in its fight, a fighter, a gun's or a station's hull), and its fight; null out of one.</summary>
	cs::Ref<Core::BattleUnit> HeldUnit(cs::Ref<Core::Battle>& battle);
	/// <summary>Whose side the controls held are on.</summary>
	cs::Ref<Core::Faction> HeldFaction();
	/// <summary>At the controls, the selection's off-screen pointer as an edge arrow (US-T128): sized by its distance, coloured
	/// by stance, named with its range. False in the command view, which keeps the plain pointer.</summary>
	bool DrawSelectionArrow(Core::DVec3 scene, const cs::String& name);

	// ---------------------------------------------------------------- the chase (U.SPC-T273)

	/// Hostiles running the ship down: each group's name, range and closing speed, and an EMP charging with the seconds
	/// left and how far off it reaches, top right under the readouts.
	void DrawHunters();

	// ---------------------------------------------------------------- aimed combat (U.SPC-T270)

	static constexpr Color HostileColor{1.0f, 0.4f, 0.35f, 0.95f};
	static constexpr Color LockColor{1.0f, 0.85f, 0.3f, 0.95f};

	/// In a fight at the helm: the locked hull's box with the lock building on it, the lead pip where the
	/// guns' rounds will meet it, and the gauges a pilot fights by -- heat, boost, shields and hull, missiles.
	void DrawCombatHud();

	/// At any controls with a reactor to share (US-T94, <see cref="Core::CraftPower"/>): the weapons / shields / engines pips,
	/// what each runs at, the preset when it is one, and the keys. Screen space only, so it reads the same at the gameplay
	/// and realistic scales.
	void DrawReactor();

	/// At any controls (US-T97, <see cref="Core::SelfDestruct"/>): the arming bar while Shift+Backspace is held, then the
	/// countdown, big, in the middle of the screen, and the key to abort it. Screen space, so it reads the same at the
	/// gameplay and realistic scales; drawn with the HUD hidden too, as a warning should be.
	void DrawSelfDestruct();

	/// At a ship's helm (US-T93, <see cref="Core::SensorPosture"/>): the sensors' and the reactor's state, above the reactor
	/// panel: PASSIVE (and when the next ping is ready), PING / LIT UP nn s after an active ping, DARK with the hull's heat
	/// cooling toward its floor, RESTARTING nn s with its bar; the signature, and the keys. Screen space only, so it reads the
	/// same at the gameplay and realistic scales.
	void DrawSensors();

	/// At a capital ship's helm (U.SPC-T272): how many guns bear where the player is looking (their fire cones take that
	/// line in, U.SPC-T533) and how many of them are loaded, the volley building while the trigger is held, and the hull
	/// it will be fired at.
	void DrawBroadside(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& me, float x, float y);

	/// The captain's orders at a capital ship's helm (U.SPC-T425), above the gauges: the order of the guns bearing where the
	/// player is looking (U.SPC-T533), whether the gunners focus a weakened shield patch
	/// (U.SPC-T501), the brace, damage control and the line's formation, the turning circle at this speed, and a hostile
	/// broadside building on this hull, which is the moment to brace.
	void DrawCaptain(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& me, float x, float y);

	/// The hull's shield patches at the helm (U.SPC-T502, see <see cref="WeakPatches"/>): which are weak (holed, or their
	/// field how far down), which one faces the heaviest fire as the hull lies now, and the order to roll them away.
	/// Red while a weak patch is the one taking the fire.
	void DrawPatches(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& me, const cs::Ref<Core::BroadsideCommand>& cmd, float x, float y);

	/// A big ship's heat at the helm (U.SPC-T491): how full its sinks are, its radiators standing, and whether
	/// the crew is throttling for it or running hot. Empty for a hull with none to manage.
	static cs::String HeatText(const cs::Ref<Core::BattleUnit>& me);

	/// A mount's rounds left and its key, or that it is still cycling.
	static void AddMount(cs::List<cs::String>& line, const cs::Ref<Core::ShipCombat>& c, Core::SubsystemKind kind, const cs::String& name, const cs::String& key);

	/// The fastest round the flown hull's own guns fire, km/s; 0 if they are all beams.
	static double RoundSpeed(const cs::Ref<Core::ShipCombat>& c);

	/// The hostile hull nearest the crosshair, within 20 degrees of it.
	cs::Ref<Core::BattleUnit> NearestHostileToAim(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& me);

	void DrawGauge(Rect r, float fraction, Color color, const cs::String& caption);

	/// A small open ring of eight dots.
	void DrawRing(Vector2 p, float radius, Color color);

	static cs::String RangeText(double km);

	// ---------------------------------------------------------------- hyperlane rides (U.SPC-T41)

	static constexpr Color StreakColor{0.6f, 0.88f, 1.0f};
	static constexpr int StreakCount = 72;

	/// Riding a lane at the helm: streaks of light stream out past the view from the point ahead, longer
	/// and faster with the ride's speed (fading for a moment after coming off), and a ring gate gone
	/// through washes the screen in a flash.
	void DrawLaneEffects();

	static constexpr Color GateGlowColor{0.4f, 0.28f, 0.85f};
	static constexpr Color GateFlashColor{0.78f, 0.93f, 1.0f};

	/// Going through a jump gate between the stars: the screen tinted the gate's violet inside it, or washed
	/// in a flash at each ring (<paramref name="amount"/> 0..1, up to <paramref name="opacity"/>).
	static void DrawGateLight(float amount, Color color, float opacity);

	static float Frac(float x);

	/// Streaks of light streaming out from <paramref name="centre"/>, longer and faster with
	/// <paramref name="strength"/>; <paramref name="seed"/> gives a set its own pattern.
	void DrawStreaks(Vector2 centre, float strength, int count, Color tint, int seed, float maxLength);

	// ---------------------------------------------------------------- formatting

	cs::String TargetName(const Target& t);

	static cs::String KindName(const cs::Ref<Core::BodyDefinition>& b);

	static cs::String Km(double km);

	/// A distance in a star's frame: AU as a rule, but a Void Base's tiny suns circle their generator within a few hundred km, which would read 0.000 AU.
	static cs::String StellarDistance(double km);

	static cs::String Au(double km);

	static cs::String LightTime(double km);

	static cs::String Duration(double days);

	static cs::String SpeedText(double kmPerSec);

	// ---------------------------------------------------------------- styles

	void EnsureStyles();

public:
	static cs::Ref<Texture2D> Solid(Color fill, Color edge);

	/// The HUD's own styles are going with the app that owns it (U.SPC-T366): DontSave outlives the scene
	/// otherwise, so each game session (and every galaxy map it opened) would leave its textures behind.
	void DestroyOwnedTextures();

private:
