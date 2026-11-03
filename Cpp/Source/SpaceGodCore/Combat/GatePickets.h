#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/GatePickets.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class Faction;
	class JumpGate;
	class GateNetwork;
	class StarSystem;
	class World;
	class RangeSetting;
	struct DVec3;

	/// <summary>
	/// One warning a gate picket has given its navy (U.SPC-T480): who came in, how, and when. Kept by the system's
	/// <see cref="War"/> (<see cref="War.PicketWarnings"/>), whose command acts on a fresh one by sending a group to hold
	/// the gate while there is still time (see <see cref="GatePickets.WarningDays"/>).
	/// </summary>
	class PicketWarning final
	{
	public:
		/// <summary>The gate the warning came from (the picket's), and the navy that holds it.</summary>
		cs::Ref<JumpGate> Gate;
		cs::Ref<Faction> Owner;

		/// <summary>The picket that saw it.</summary>
		cs::Ref<Ship> Picket;

		/// <summary>The flagship of the group that came in, its faction, and how many hulls it had.</summary>
		cs::Ref<Ship> Intruder;
		cs::Ref<Faction> By;
		int Hulls = 0;

		/// <summary>Dropped out of FTL nearby (true), or came through the gate (false).</summary>
		bool SelfJump = false;

		double Jd = 0;

		cs::String ToString() const;
	};

	/// <summary>
	/// Gate pickets (U.SPC-T480): small, sensor-heavy hulls (<see cref="ShipClass.GatePicket"/>) a navy stations at the exit
	/// of every jump gate it holds, for early warning. A picket carries a long-range search suite (<see cref="SensorSuite.Picket"/>)
	/// that joins its navy's picture like any other hull's, so raiders lying dark off the gate are made out and command
	/// sends a group to clear them (see <see cref="War"/>'s gate orders). Whatever hostile and armed comes through the gate,
	/// or drops out of FTL within <see cref="JumpWatchAu"/> of a picket, is reported at once: the intruders go straight into
	/// the navy's picture, the news carries the warning, and command sends a group strong enough to hold the gate against
	/// them while they are still there (<see cref="War.PicketWarnings"/>). A gate held with pickets gives its owner warning
	/// and time to react; one without them is blind until the intruders reach a port.
	/// <para>Raiders know it: a band lying off a gate goes for its pickets first, taking on more guns to silence one than
	/// it would for warships alone (<see cref="RaidDoctrine.PicketNerve"/>), and its gunners shoot a picket before anything
	/// else in the fight (<see cref="RaidDoctrine.TargetBias"/>).</para>
	/// <para>A picket flies as a patrol wing (<see cref="Ship.PatrolWing"/>: never merged, sent away or counted as a warship),
	/// holds its post off the gate (<see cref="Ship.PicketPostKm"/>) and never opens a fight; badly hurt it puts in at a yard
	/// and comes back. One lost is replaced, at the navy's expense, after <see cref="RebuildDays"/>; a gate that changes hands
	/// sends the old holder's pickets off to patrol its ports, and the new holder posts its own. Distinct from a navy's
	/// destroyer patrol wing (<see cref="PatrolWings"/>), which flies a beat of its ports.</para>
	/// </summary>
	/// [SettingsSource]
	class GatePickets
	{
	public:
		/// <summary>The settings menu's pickets per gate (AI): how many a navy keeps at each gate it holds.</summary>
		static const cs::Ref<RangeSetting>& PerGate();

		/// <summary>The settings menu's picket FTL watch (AI): how far from a picket a ship dropping out of FTL is reported.</summary>
		static const cs::Ref<RangeSetting>& JumpWatchAu();

		/// <summary>How far inside its gate (toward the system root) a picket holds, km: clear of the ring, where everything
		/// coming out passes it, and nearer the gate than raiders lying off its approach (<see cref="JumpGate.ApproachInsetKm"/>).</summary>
		static constexpr double PostInsetKm = 250;

		/// <summary>How far apart a gate's pickets hold, side by side across the way in, km.</summary>
		static constexpr double PostSpacingKm = 120;

		/// <summary>How long command acts on a picket's warning (days): it sends a group to hold the gate while the warned-of
		/// intruders are still there.</summary>
		static constexpr double WarningDays = 2;

		/// <summary>The most warnings a system's war keeps (the oldest go).</summary>
		static constexpr int WarningsKept = 64;

		/// <summary>How long a gate goes short of its pickets before its navy posts a new one (days).</summary>
		static constexpr double RebuildDays = 10;

		/// <summary>The damage (share of its hull and systems) past which a picket leaves its post for a yard.</summary>
		static constexpr double RepairDamage = 0.5;

		/// <summary>Whether <paramref name="gate"/>'s holder keeps pickets there: a state with a navy, as for its forts.</summary>
		static bool Keeps(const cs::Ref<JumpGate>& gate);

		/// <summary>How many pickets <paramref name="gate"/>'s holder keeps there.</summary>
		static int Wanted(const cs::Ref<JumpGate>& gate);

		/// <summary>The post of the <paramref name="index"/>th of <paramref name="count"/> pickets at <paramref name="gate"/>, km off
		/// its system's root: <see cref="PostInsetKm"/> inside the gate, spread side by side across the way in.</summary>
		static DVec3 Post(const cs::Ref<JumpGate>& gate, int index, int count);

	private:
		/// <summary>Whether <paramref name="s"/> is a picket of <paramref name="gate"/>, afloat in its world.</summary>
		static bool Serves(const cs::Ref<World>& w, const cs::Ref<Ship>& s, const cs::Ref<JumpGate>& gate);

	public:
		/// <summary>The pickets of <paramref name="gate"/> (its holder's or not), afloat in its system.</summary>
		static cs::List<cs::Ref<Ship>> Of(const cs::Ref<JumpGate>& gate);

		/// <summary>Whether <paramref name="picket"/> is at its post off its gate (not away at a yard, not in flight).</summary>
		static bool OnPost(const cs::Ref<World>& w, const cs::Ref<Ship>& picket);

		/// <summary>Posts every gate's pickets in <paramref name="network"/> (the galaxy's start: free, as its forts are).</summary>
		static void PostAll(const cs::Ref<GateNetwork>& network, double jd);

		/// <summary>Posts as many pickets at <paramref name="gate"/> as its holder keeps there (a gate just built or rebuilt
		/// opens watched, as it opens guarded).</summary>
		static void PostFull(const cs::Ref<JumpGate>& gate, double jd);

		/// <summary>
		/// Builds a picket for <paramref name="gate"/>'s holder and posts it off the gate (the <paramref name="index"/>th of
		/// <paramref name="count"/>). Added to the system's fleet directly, drawing nothing from its random stream.
		/// </summary>
		static cs::Ref<Ship> Station(const cs::Ref<JumpGate>& gate, int index, int count, double jd);

		/// <summary>
		/// A day's upkeep of <paramref name="gate"/>'s pickets (run by <see cref="InterstellarWars"/>): the old holder's sent
		/// off to patrol its ports once the gate has changed hands, and one lost replaced at its navy's expense once the gate
		/// has been short of them for <see cref="RebuildDays"/>, when it can pay and crew it and nobody is fighting there.
		/// </summary>
		static void Upkeep(const cs::Ref<JumpGate>& gate, double jd);

	private:
		/// <summary>A picket whose gate is no longer its navy's: off its post, to patrol its navy's ports here as a wing of one.</summary>
		static void StandDown(const cs::Ref<World>& w, const cs::Ref<Ship>& picket);

		// ---------------------------------------------------------------- warnings

	public:
		/// <summary>
		/// <paramref name="ships"/> (a group, flagship first) have come out of the gate <paramref name="exit"/> (U.SPC-T480):
		/// its holder's pickets on post there report them if they are hostile and armed. Returns the warning, or null.
		/// </summary>
		static cs::Ref<PicketWarning> OnArrival(const cs::Ref<JumpGate>& exit, const cs::List<cs::Ref<Ship>>& ships, double jd);

		/// <summary>
		/// <paramref name="ships"/> have dropped out of FTL at <paramref name="atKm"/> (off the system root) in
		/// <paramref name="system"/>: any picket on post within <see cref="JumpWatchAu"/> reports them to its navy if they
		/// are hostile and armed. Returns the first warning given, or null.
		/// </summary>
		static cs::Ref<PicketWarning> OnDropOut(const cs::Ref<StarSystem>& system, const cs::List<cs::Ref<Ship>>& ships, const DVec3& atKm, double jd);

		/// <summary>Whether a picket of <paramref name="owner"/> warns of <paramref name="s"/>: an armed hull (a warship or a
		/// raider) of a power it is hostile to.</summary>
		static bool Threat(const cs::Ref<World>& w, const cs::Ref<Faction>& owner, const cs::Ref<Ship>& s);

	private:
		static cs::Ref<PicketWarning> Warn(const cs::Ref<World>& w, const cs::Ref<JumpGate>& gate, const cs::Ref<Ship>& picket, const cs::List<cs::Ref<Ship>>& ships, bool selfJump, double jd);
	};
}
