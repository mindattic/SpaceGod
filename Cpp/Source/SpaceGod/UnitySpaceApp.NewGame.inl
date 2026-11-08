// UnitySpaceApp.NewGame.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Not a port of a C# file.
// Bodies in UnitySpaceApp.NewGame.cpp.
//
// <summary>
// US-T247: the starting situation picked on the main menu's New Game setup page (Core::NewGameSetup). A new game made from
// the page is built on its start day and from its galaxy seed (Awake); once the scene has laid itself out, the player is
// put into the situation: the god view by Earth (as always), a home-side fighter's helm on patrol near Earth, the captain of
// their own freighter at Earth with a small purse, or a capital ship's helm in a fight.
// </summary>

public:
	/// <summary>
	/// US-T247: puts the player into <paramref name="situation"/> now: GodView does nothing; FighterPatrol takes the helm of a
	/// home-side fighter flying patrol in this system (else one put on patrol near Earth); FreighterCaptain gives the player
	/// their own freighter in orbit at Earth (enlisted in the house fleet) and the house account
	/// <see cref="Core::NewGameSetup.FreighterPurse"/>, at its helm; CapitalWarZone takes the helm of the largest warship of a
	/// lawful side in a fight already on in this system, else of a home-side cruiser off a quiet giant world with raiders
	/// closing. Returns whether the player was put there (a toast says so either way).
	/// </summary>
	bool StartSituationNow(Core::StartSituation situation);

private:
	/// <summary>The situation the game was set up with, still to be started (GodView: none).</summary>
	Core::StartSituation newGameStart = Core::StartSituation::GodView;

	/// <summary>Frames left before <see cref="newGameStart"/> is started (-1: none due); the scene settles first.</summary>
	int newGameStartDueFrames = -1;

	/// <summary>Once a frame: starts the situation the game was set up with when it is due.</summary>
	void UpdateNewGameStart();

	bool StartFighterPatrol();
	bool StartFreighterCaptain();
	bool StartCapitalHelm();

	/// <summary>A spot clear of everything near <paramref name="body"/>, on its sunlit side and within its neighbourhood, for a
	/// hull of <paramref name="lengthKm"/>: the offset from the body (km) and the way to face (at the body).</summary>
	void NewGameSpot(int body, double lengthKm, Core::DVec3& offsetKm, Core::DVec3& forward);
