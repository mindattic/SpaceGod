#pragma once
// US-T194: freighters set upon by raiders call for help, and the navy patrols near enough answer.
//
// The robbery itself was already fought by the doctrine (Combat/RaidDoctrine.h): raiders pull a freighter out of its lane
// (War::Interdict) or fall on it at a port, strip its shields and engines, hold their fire once it lies dead, demand its
// cargo (Battle::MaybeDemandCargo), take what it jettisons home (War::Loot, so the cargo never reaches the market it was
// bound for) and withdraw; the freighter runs for it while it can. What it lacked was the other half: the freighter's
// distress call, and the patrols that answer it.
//
// Everything here is world state, changed only by the simulation, with no random draws: who answers is picked by how
// soon they can be there (fleet order breaking ties), and when they arrive is a function of where they were, so every
// lockstep peer and every replay reaches the same answer. Battle space is in true km, so the response reads the same at
// the gameplay and the realistic scale.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Battle;
	class Faction;
	class Ship;
	class World;

	/// <summary>A patrol group answering a distress call (US-T194): its leader, and the battle second it arrives.</summary>
	class SPACEGODCORE_API RaidDistressResponse final
	{
	public:
		cs::Ref<Ship> Leader;
		/// <summary>The battle's own clock (<see cref="Battle.Seconds"/>) when it is there and joins the fight.</summary>
		double ArriveAtS = 0;
		bool Arrived = false;
	};

	/// <summary>One freighter's call for help (US-T194): the fight it is in, who called, who is robbing it and who answers.</summary>
	class SPACEGODCORE_API RaidDistressCall final
	{
	public:
		cs::Ref<Battle> Fight;
		cs::Ref<Ship> Caller;
		cs::Ref<Faction> Raiders;
		/// <summary>The side the caller fights on: the side the answering patrols join.</summary>
		int Side = 1;
		double Jd = 0;
		cs::List<cs::Ref<RaidDistressResponse>> Responses;
	};

	/// <summary>
	/// The distress calls of the fights under way (US-T194), held on <see cref="War.Distress"/>. A merchant or civilian
	/// hull raiders mean to rob calls for help as soon as it is set upon; the armed navy groups of every power not at war
	/// with it that are parked where they could reach it in time (at either end of the lane it was pulled out of, or at the
	/// body the fight is at, close enough to burn over) answer, the nearest <see cref="MaxResponders"/> of them. Each
	/// arrives after <see cref="ScrambleS"/> to get under way and the time to cover the distance, and joins the fight on the
	/// caller's side; raiders who see more guns arrive than their nerve allows (<see cref="RaidDoctrine.BaseNerve"/>)
	/// before they have their loot break off and run. Too slow, and the raiders are gone with the cargo.
	/// </summary>
	class SPACEGODCORE_API RaidDistress final
	{
	public:
		cs::List<cs::Ref<RaidDistressCall>> Calls;

		/// <summary>Seconds a patrol takes to get under way once it has the call.</summary>
		static constexpr double ScrambleS = 45;

		/// <summary>Seconds to ride a whole lane out to a fight on it: a fight a fraction of the way along takes that fraction.</summary>
		static constexpr double LaneRideS = 240;

		/// <summary>How fast (km/s) a patrol burns across a body's neighbourhood to a fight there.</summary>
		static constexpr double BurnKmS = 5000;

		/// <summary>The longest a patrol may take to get there and still answer, seconds: further off, the fight is not theirs.</summary>
		static constexpr double MaxResponseS = 300;

		/// <summary>The most patrol groups that answer one call.</summary>
		static constexpr int MaxResponders = 2;

		/// <summary>The war's book (made the first time a call is raised: older saves have none); null without a war.</summary>
		static cs::Ref<RaidDistress> Of(const cs::Ref<World>& world);

		/// <summary>The book if it exists, never making it.</summary>
		static cs::Ref<RaidDistress> Find(const cs::Ref<World>& world);

		/// <summary>The call raised in <paramref name="b"/>, or null.</summary>
		static cs::Ref<RaidDistressCall> CallIn(const cs::Ref<World>& world, const cs::Ref<Battle>& b);

		/// <summary>
		/// How long (seconds) the patrol group led by <paramref name="leader"/> takes to reach <paramref name="b"/>, or a
		/// negative number if it cannot be there within <see cref="MaxResponseS"/>: from either end of the lane a lane fight
		/// was pulled out of (<see cref="LaneRideS"/> times the share of the lane to ride), or from its parking orbit at the
		/// body a fight there is at (at <see cref="BurnKmS"/>), after <see cref="ScrambleS"/> either way.
		/// </summary>
		static double ResponseSeconds(const cs::Ref<World>& world, const cs::Ref<Ship>& leader, const cs::Ref<Battle>& b, double jd);

		/// <summary>
		/// Raises the call of every fight in which raiders have set on a hull they mean to rob and that has none yet, sends
		/// the patrols that answer, brings in those whose time has come, and forgets the calls of fights that are over.
		/// Called once a tick from <see cref="War.Update"/>.
		/// </summary>
		static void Advance(const cs::Ref<World>& world, double jd);
	};
}
