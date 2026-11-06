// UnitySpaceApp.Practice.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// Port of Runtime/UnitySpaceApp.Practice.cs; bodies in UnitySpaceApp.Practice.cpp.
//
// <summary>
// The practice fighter (U.SPC-T601, <see cref="PracticeSpawn"/>): Shift+V, or the button on the Spawn &amp; Pilot
// screen, puts a home-side fighter at the camera's spot in whatever system and view the player is in, clear of
// anything there, and the player straight at its helm, to fly alongside the hulls around it and practise the flight.
// The system keeps one: pressing again brings the same hull, mended and refuelled, to the new spot.
// </summary>

private:
	cs::List<Core::SpawnObstacle> practiceObstacles = cs::List<Core::SpawnObstacle>();

public:
	/// <summary>Spawns (or brings over) this system's practice fighter at the camera and takes its helm.</summary>
	void SpawnPracticeFighter();

private:
	/// <summary>
	/// This system's practice fighter, if it can simply be brought over: still here, whole enough to fly, on the home
	/// side, nobody's prize and in no fight. A lost or taken one stays where it is (a wreck, a prize) and a new one is made.
	/// </summary>
	cs::Ref<Core::Ship> ReusablePracticeFighter();

	/// <summary>Everything near enough to matter that the fighter must not start inside, heliocentric km: the system's
	/// hulls (and the craft of its fights), stations, wrecks' debris and its worlds. <paramref name="self"/> is left out.</summary>
	void CollectPracticeObstacles(const cs::Ref<Core::Ship>& self);

	// ---------------------------------------------------------------- the dogfight demo (US-T88)

public:
	/// <summary>
	/// The dogfight demo (US-T88): a home-side fighter (a light fighter, or with <paramref name="heavy"/> a heavy one) off a
	/// quiet giant world, its sunlit face and rings behind the enemy, with the player at its helm and four pirates a few km
	/// ahead -- an interceptor and a fighter, a corvette and a destroyer -- already in the fight. Started from the main
	/// menu, the Spawn &amp; Pilot screen, the <c>-dogfight</c> / <c>-dogfight-heavy</c> switch or the console's
	/// <c>sg.DogfightDemo [heavy]</c>. Laid out in true km about the world, the fight in battle space, so it reads the
	/// same at the gameplay and the realistic scale. <paramref name="kit"/> (US-T101, <c>sg.DogfightDemo [heavy] [kit]</c>)
	/// rearms the player's craft first: <c>vulcan</c> (rotary cannons for its guns), <c>rockets</c>, <c>hive</c> (on its
	/// missile mounts) or <c>arsenal</c> (vulcans and hive missiles); empty, it flies as built.
	/// </summary>
	void StartDogfightDemo(bool heavy, const cs::String& kit = cs::String());

	/// <summary>US-T173: once a frame while a showcase tour dogfight stop runs (the tour idles the input): flies the demo craft at
	/// the nearest raider, holds its hull up, and takes the helm again or restarts the demo if it was lost.</summary>
	void FlyDogfightTourCraft();

private:
	/// <summary>Frames left before a dogfight demo asked for at launch starts (-1: none due); the scene settles first.</summary>
	int dogfightDemoDueFrames = -1;
	bool dogfightDemoHeavy = false;
	bool dogfightDemoAsked = false;

	/// <summary>Once a frame: starts the dogfight demo the game was launched for (GameLaunch::TakeDogfightDemo).</summary>
	void UpdateDogfightDemo();

	/// <summary>Where the demo is fought: the first quiet giant (no fight, no hulls there), else the system's largest world.</summary>
	int DogfightDemoBody();
