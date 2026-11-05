// UnitySpaceApp.Cheats.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// US-T126: the dev menu (Shift+F1, UI/CheatMenu.h) and the cheats it runs; bodies in UnitySpaceApp.Cheats.cpp.
//
// <summary>
// The dev menu pulls down over the top of the view and gathers every dev and debug function in one place. Its built-in
// entries are registered here (<see cref="RegisterBuiltInCheats"/>); any other part of the game adds its own with
// UI::CheatRegistry::Add from its own file. While it is open it owns the keys and the mouse (see UpdateCheatMenu).
// </summary>

public:
	/// <summary>The dev menu (US-T126).</summary>
	const cs::Ref<UI::CheatMenu>& CheatMenuUi() const { return cheatMenu; }

	/// <summary>The menu's built-in entries, by category: Spawn, Enemies, Travel, Helm, Combat, Time and scale. A static
	/// member, so the entries' lambdas reach the app's own members as a partial's code does.</summary>
	static void RegisterBuiltInCheats();

	/// <summary>One hull of a cheat spawn, placed about the player: <see cref="Ahead"/>, <see cref="Right"/> and
	/// <see cref="Up"/> in kilometres along the player's own axes, each scaled up for big hulls so none starts inside another.</summary>
	struct CheatSlot
	{
		Core::ShipClass Class{};
		cs::String Name;
		double Ahead = 0, Right = 0, Up = 0;
	};

	/// <summary>
	/// Spawns <paramref name="slots"/> for <paramref name="faction"/> beside the player (the hull flown, else the camera), each
	/// clear of whatever is there (<see cref="PracticeSpawn.Clear"/>); hostile ones face the player. At a helm, enemies are
	/// put straight into a fight with it, laid out where they were spawned, and friends join the fight it is in.
	/// </summary>
	cs::List<cs::Ref<Core::Ship>> CheatSpawn(const cs::List<CheatSlot>& slots, const cs::Ref<Core::Faction>& faction);

	/// <summary>Takes the hull flown (or, out of a helm, the camera) to <paramref name="body"/>: off its sunlit side at the
	/// distance a fight there is laid out at (<see cref="BodyStandoffKm"/>), nose on it. In true km, so the same at both scales.</summary>
	void CheatTeleportToBody(int body);

	/// <summary>As <see cref="CheatTeleportToBody"/>, to an orbital station: a few of its lengths off it, outward of its world.</summary>
	void CheatTeleportToStructure(const cs::Ref<Core::OrbitalStructure>& st);

	/// <summary>Out of any seat, then the camera's trip to <paramref name="system"/> (Esc skips it).</summary>
	void CheatTravelTo(const cs::Ref<Core::StarSystem>& system);

	/// <summary>
	/// Where a hull stands off <paramref name="body"/> to meet it (the dogfight demo's layout, US-T88): out on the sunlit side
	/// and above the ring plane (<paramref name="outward"/>, unit, from the body), clear of the rings and as far out as a fight
	/// there is laid out (<see cref="Battle.AnchorDistanceKm"/>), inside its neighbourhood. True km.
	/// </summary>
	double BodyStandoffKm(int body, Core::DVec3& outward);

	/// <summary>Why the world-changing cheats cannot run now (co-op, between the stars), or empty.</summary>
	cs::String CheatBlocker() const;

private:
	const cs::Ref<UI::CheatMenu> cheatMenu = cs::New<UI::CheatMenu>();

	/// <summary>
	/// Once a frame, straight after the pause menu has had its turn (US-T90's ownership: the pause menu, then this menu,
	/// then the panels, the craft held and the camera): Shift+F1 opens or closes it, and while it is open it has the keys
	/// and the mouse, so the caller idles the frame's input. Closed, keys still held from it wait to be let go. True while
	/// it holds them (the frame it closes too: its key is spent).
	/// </summary>
	bool UpdateCheatMenu();

	/// <summary>The player's place to spawn beside: the hull flown (its frame, offset, nose and roof), else the camera's.</summary>
	void CheatPlayerFrame(int& frame, Core::DVec3& offsetKm, Core::DVec3& forward, Core::DVec3& up);

	/// <summary>The hull flown made whole, rearmed and its tanks full.</summary>
	void CheatMendHull();

	/// <summary>A polity at war with the player's (not the pirates), whose warships the menu can spawn; null if none is.</summary>
	cs::Ref<Core::Faction> CheatHostileNavy();
