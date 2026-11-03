#pragma once
#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class World;
	class Faction;
	class Ship;
	class RangeSetting;

	/// <summary>
	/// Corvettes (U.SPC-T476): system defence and local law enforcement. A corvette (<see cref="ShipClass.Corvette"/>) has no
	/// FTL drive, and puts the weight saved into patrol: deep tanks, a patrol search suite (<see cref="SensorSuite.Patrol"/>)
	/// and a brawler's guns. Every navy stations one for good at each of its ports in a system, the most populous first, up to
	/// <see cref="PerNavy"/>: a planet, moon or station (<see cref="Ship.CorvetteStation"/>). From there it rides the hyper lanes
	/// of its home system on a beat (<see cref="Beat"/>): its station, the navy's other ports round the same planet, and the
	/// navy's port at the far end of each lane out of it, always coming back to its station between runs.
	/// <para>It is the law there. As a navy patrol it hails and scans the player's ship in its navy's space (see
	/// <see cref="Law.UpdatePilot"/>); it defends the ports on its beat; and when its navy has seen pirates or a clan's
	/// smugglers (<see cref="Ship.Runner"/>) at a port on its beat or anywhere round its home planet, it goes to run them down
	/// (<see cref="OrderKind.Hunt"/>), fighting in the law's name where it finds them.</para>
	/// <para>It never leaves that system: no drive, no gate, no carriage (<see cref="FtlAccess.HomeSystem"/>), never merged into
	/// a battle group, given an objective elsewhere or counted as a warship (it flies as a <see cref="Ship.PatrolWing"/>). One
	/// lost is replaced at its navy's expense after <see cref="RebuildDays"/>; one whose port has changed hands is posted to
	/// another of its navy's ports.</para>
	/// </summary>
	// [SettingsSource]
	class Corvettes
	{
	public:
		/// <summary>The settings menu's corvettes per navy (AI): how many a navy keeps in each system it holds ports in.</summary>
		static const cs::Ref<RangeSetting>& PerNavy();

		/// <summary>The most ports a corvette's beat takes in round its own planet besides its station.</summary>
		static constexpr int HomePorts = 1;

		/// <summary>The most hyper lanes out of its home planetary system a corvette's beat runs down.</summary>
		static constexpr int LanePorts = 2;

		/// <summary>How long a navy goes short of its corvettes in a system before it commissions a new one (days).</summary>
		static constexpr double RebuildDays = 15;

		/// <summary>The damage (share of its hull and systems) past which a corvette leaves its beat for a yard.</summary>
		static constexpr double RepairDamage = 0.3;

	private:
		static const cs::List<cs::String>& Names();

	public:
		/// <summary>Whether <paramref name="f"/> keeps corvettes: a power with a navy, not a pirate clan.</summary>
		static bool Keeps(const cs::Ref<Faction>& f);

		/// <summary>How many corvettes a navy holding <paramref name="ports"/> ports in a system keeps there: one a port, up to
		/// <see cref="PerNavy"/>.</summary>
		static int Wanted(int ports);

		/// <summary>Whether <paramref name="s"/> is a corvette on its station's books (not a corvette hull the player flies or
		/// bought, which has no station).</summary>
		static bool Stationed(const cs::Ref<Ship>& s);

		/// <summary><paramref name="f"/>'s stationed corvettes afloat in <paramref name="w"/>.</summary>
		static cs::List<cs::Ref<Ship>> Of(const cs::Ref<World>& w, const cs::Ref<Faction>& f);

		/// <summary>
		/// The beat of a corvette of <paramref name="f"/> stationed at <paramref name="station"/> in <paramref name="w"/>: its
		/// station first, then up to <see cref="HomePorts"/> of the navy's other ports round the same planet (its moons and
		/// stations), then, down each hyper lane out of that planetary system, the navy's most populous port at the far end
		/// (up to <see cref="LanePorts"/> lanes). Every body on it is in the corvette's home system.
		/// </summary>
		static cs::List<int> Beat(const cs::Ref<World>& w, const cs::Ref<Faction>& f, int station);

		/// <summary>Whether <paramref name="body"/> is on <paramref name="corvette"/>'s beat, or round its home planet: the
		/// ground it polices.</summary>
		static bool Polices(const cs::Ref<World>& w, const cs::Ref<Ship>& corvette, int body);

		/// <summary>The next body on <paramref name="corvette"/>'s patrol from <paramref name="here"/>: back to its station
		/// from anywhere else, and from its station out down the next run of its beat in turn.</summary>
		static int NextOnBeat(const cs::Ref<Ship>& corvette, int here);

		/// <summary>
		/// Stations the corvettes of every navy holding ports in <paramref name="w"/> (the system's start: free, as its patrol
		/// wings are), one at each of its most populous ports up to <see cref="PerNavy"/>. Returns them.
		/// </summary>
		static cs::List<cs::Ref<Ship>> Raise(const cs::Ref<World>& w, double jd);

		/// <summary>
		/// Commissions a corvette for <paramref name="f"/> and stations it at <paramref name="station"/> in <paramref name="w"/>,
		/// holding off the port. Added to the system's fleet directly, drawing nothing from its random stream.
		/// </summary>
		static cs::Ref<Ship> Station(const cs::Ref<World>& w, const cs::Ref<Faction>& f, int station, double jd);

		/// <summary>
		/// A day's upkeep of <paramref name="w"/>'s corvettes (run by its <see cref="War"/>): one whose port is no longer its
		/// navy's is posted to another of its navy's ports (one without a corvette first), and a navy short of corvettes
		/// there commissions one at its expense for a port without one, once it has been short for <see cref="RebuildDays"/>,
		/// when it can pay and crew it and nobody is fighting at that port. <paramref name="shortSince"/> keeps, per navy, when
		/// it went short.
		/// </summary>
		static void Upkeep(const cs::Ref<World>& w, cs::Dictionary<cs::Ref<Faction>, double>& shortSince, double jd);

	private:
		/// <summary>The most populous of <paramref name="ports"/> with none of <paramref name="corvettes"/> stationed at it, or null.</summary>
		static cs::Nullable<int> Unmanned(const cs::List<int>& ports, const cs::List<cs::Ref<Ship>>& corvettes);

	public:
		/// <summary>Posts <paramref name="corvette"/> to <paramref name="station"/>: its new beat starts from there.</summary>
		static void Repost(const cs::Ref<World>& w, const cs::Ref<Ship>& corvette, int station);
	};
}
