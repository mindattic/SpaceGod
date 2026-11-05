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
