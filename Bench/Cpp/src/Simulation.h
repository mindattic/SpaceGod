#pragma once
// Bench.Cpp: the simulation half of Unity.SpaceGod's UnitySpaceApp over the C++ Core (Cpp/Source/SpaceGodCore),
// a line-for-line port of Bench/CSharp/src/Simulation.cs. Every call is the one Bench.CSharp makes, in its order
// (see Bench/README.md, "What a tick is").

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class SimClock;
	class Galaxy;
	class StarSystem;
	class World;
	class Battle;
	class Ship;
	class Faction;
	enum class ShipClass : int;
}

namespace Bench
{
	namespace sg = SpaceGod::Core;

	class Simulation
	{
	public:
		cs::Ref<sg::SimClock> Clock;
		cs::Ref<sg::Galaxy> Galaxy;
		cs::Ref<sg::StarSystem> ActiveSystem;
		cs::Ref<sg::World> World() const;

		/// <summary>The ship whose battle the followed view puts into battle time (the camera's focus target), or null.</summary>
		cs::Ref<sg::Ship> Followed;

		/// <summary>A crew-flown plotted course being followed (UnitySpaceApp.Course with CourseByCrew and courseFollow).</summary>
		cs::Ref<sg::Ship> CourseShip;
		cs::Ref<sg::StarSystem> CourseDestination;
		bool CourseArrived = false, CourseEnded = false;

		int BattlesStarted = 0;

		/// <summary>The battle a "skirmish" scenario started (kept after it ends and leaves War.Battles).</summary>
		cs::Ref<sg::Battle> Skirmish;

		/// <summary>UnitySpaceApp.Awake: the clock (a fixed start date instead of now), the galaxy, the first system active.</summary>
		static cs::Ref<Simulation> Build(double startJd, int seed);

		void Activate(const cs::Ref<sg::StarSystem>& system);
		sg::DVec3 PlayerHelioKm() const;
		void Tick(double dt);
		void EnterBattleTime(const cs::Ref<sg::Battle>& battle);
		void WatchBattle(const cs::Ref<sg::Battle>& battle);
		cs::Ref<sg::Battle> TriggerSkirmish(int body, const cs::Ref<sg::Faction>& aggressor, const cs::Ref<sg::Faction>& defender, const cs::List<sg::ShipClass>& classes);
		bool StartCrewCourse(const cs::Ref<sg::Ship>& ship, const cs::Ref<sg::StarSystem>& to);
		cs::Ref<sg::StarSystem> SystemHolding(const cs::Ref<sg::Ship>& ship) const;

	private:
		/// <summary>The battle the clock was last dropped to real time for (UnitySpaceApp.battleTimeFor).</summary>
		cs::Ref<sg::Battle> battleTimeFor;

		void SubscribeWorld(const cs::Ref<sg::StarSystem>& s);
		void BattleTime();
		void UpdateCourseShip();
	};
}
