#pragma once
// Port of Assets/UnitySpace/Core/Systems/FleetDoctrine.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class World;
	class Galaxy;
	class Faction;
	class StarSystem;
	class Campaign;
	class ToggleSetting;
	class RangeSetting;
	struct DVec3;
	struct GalacticPosition;

	/// <summary>The two kinds of force a navy raises (U.SPC-T474, see <see cref="FleetDoctrine"/>).</summary>
	enum class ForceKind : int
	{
		/// <summary>A large group that must come through the gates: predictable, and met at the gate exits by the forts and
		/// pickets there.</summary>
		GateFleet,

		/// <summary>A carrier, the capitals with FTL drives of their own and the escorts docked or clamped to the carrier: smaller,
		/// but it can jump in anywhere under its own drives, gates or none.</summary>
		JumpStrikeGroup,
	};
	SPACEGODCORE_API const char* ToString(ForceKind v);

	/// <summary>What a campaign is sent to do (U.SPC-T474): its objective decides which kind of force goes.</summary>
	enum class CampaignMission : int
	{
		/// <summary>Take an enemy system: the gate fleet's work, forcing the gates on the way.</summary>
		Invade,

		/// <summary>A jump strike group dropping into another enemy system the war is fought over, while the gate fleet marches
		/// on its own goal, to take it from the flank.</summary>
		SecondFront,

		/// <summary>A jump strike group sent to one of its own systems under attack, to break the enemy over its ports and put
		/// its marines down to stiffen their garrisons.</summary>
		Relieve,

		/// <summary>A jump strike group falling on an enemy system's weakest ports and jumping out again before the enemy can
		/// gather.</summary>
		Raid,
	};
	SPACEGODCORE_API const char* ToString(CampaignMission v);

	/// <summary>
	/// AI fleet doctrine (U.SPC-T474): navies raise two kinds of force and use them differently. A gate fleet is large but
	/// has to come through the gates, so it is predictable and met at the gate exits; a jump strike group (a carrier, the
	/// capitals with FTL drives of their own and the escorts docked aboard or clamped to the carrier, <see cref="FtlCarriage"/>)
	/// is smaller but can jump in anywhere, dropping out on the flank of a system away from its gates and their pickets.
	/// Strategic command (<see cref="InterstellarWars"/>) picks between them by objective: a gate fleet to invade, a strike
	/// group to relieve one of its own systems under attack, open a second front or raid. Defenders keep pickets at their
	/// gates (<see cref="GatePickets"/>) and, against an enemy that has strike groups, a reserve at home in every system they
	/// hold that no campaign may draw below (<see cref="ReserveShare"/>), which meets a strike group at the ports it falls on.
	/// Which hulls make a strike group is the carriage data (drives, berths and clamps), never a list of classes here.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API FleetDoctrine
	{
	public:
		/// <summary>The settings menu's strike groups (AI): whether navies send jump strike groups on missions of their own.</summary>
		static const cs::Ref<ToggleSetting>& StrikeGroups();

		/// <summary>The settings menu's home reserve (AI): how much of the strongest enemy strike group a navy keeps at home.</summary>
		static const cs::Ref<RangeSetting>& ReserveShare();

		/// <summary>The settings menu's strike range (AI): how far a strike group is sent under its own drive.</summary>
		static const cs::Ref<RangeSetting>& StrikeRangeLy();

		/// <summary>The settings menu's strike length (AI): how long a strike group raids or relieves before it jumps home.</summary>
		static const cs::Ref<RangeSetting>& StrikeDays();

		/// <summary>How far round from the way it comes in a strike group may drop out, degrees: never past the side of the
		/// system, so it does not fly through the star to get there.</summary>
		static constexpr double MaxFlankDegrees = 90;

	private:
		/// <summary>The flanks tried, degrees either way from the way it comes in.</summary>
		static const cs::List<double>& flanks();

	public:
		// ---------------------------------------------------------------- what a group is

		/// <summary>Whether <paramref name="ship"/> is a carrier in the doctrine's sense: a hull with an FTL drive of its own and
		/// docking clamps to take escorts through its jump.</summary>
		static bool IsCarrier(cs::Ref<Ship> ship);

		/// <summary>
		/// Which kind of force <paramref name="leader"/>'s group in <paramref name="world"/> is: a jump strike group when its
		/// flagship has an FTL drive, a carrier is among it and every hull that leaves home either has a drive of its own or is
		/// docked or clamped aboard one (nothing would be left behind at the well's edge); otherwise a gate fleet.
		/// </summary>
		static ForceKind KindOf(cs::Ref<World> world, cs::Ref<Ship> leader);

		/// <summary>Whether <paramref name="leader"/>'s group is a jump strike group.</summary>
		static bool IsStrikeGroup(cs::Ref<World> world, cs::Ref<Ship> leader);

	private:
		/// <summary>Whether <paramref name="s"/> leads a navy group of its own here: a warship flagship, not a patrol wing or a
		/// picket, not inside a voyage, not the helm's.</summary>
		static bool Leads(cs::Ref<World> w, cs::Ref<Ship> s);

	public:
		// ---------------------------------------------------------------- reserves

		/// <summary>
		/// The strength of the strongest jump strike group fielded by anyone at war with <paramref name="f"/> (in any system,
		/// or between the stars): what a system of <paramref name="f"/>'s must be ready to meet. 0 when none.
		/// </summary>
		static double StrikeThreat(cs::Ref<Galaxy> galaxy, cs::Ref<Faction> f);

		/// <summary>The warship strength <paramref name="f"/> keeps at home in <paramref name="world"/> against <paramref name="threat"/>
		/// (<see cref="StrikeThreat"/>): its share of it, where <paramref name="f"/> holds a port; none elsewhere.</summary>
		static double ReserveWanted(cs::Ref<World> world, cs::Ref<Faction> f, double threat);

		/// <summary>
		/// Whether sending <paramref name="leader"/>'s group away from <paramref name="world"/> leaves <paramref name="f"/> the
		/// reserve it keeps there (<see cref="ReserveWanted"/>): the other groups it would leave at home are strong enough.
		/// </summary>
		static bool LeavesReserve(cs::Ref<World> world, cs::Ref<Ship> leader, cs::Ref<Faction> f, double threat);

		// ---------------------------------------------------------------- jumping in

		/// <summary>
		/// The way out from <paramref name="to"/>'s star (a unit vector, its system's km frame) at which a strike group of
		/// <paramref name="f"/> coming from <paramref name="from"/> drops out: of the flanks up to <see cref="MaxFlankDegrees"/>
		/// round from the way it comes in, the one farthest from every gate and gate picket held against it there, so it is not
		/// met at the exit as a gate fleet is. Straight in when nothing there watches for it.
		/// </summary>
		static DVec3 DropDirection(cs::Ref<StarSystem> to, GalacticPosition from, cs::Ref<Faction> f);

	private:
		/// <summary>Where <paramref name="f"/> would be seen dropping out in <paramref name="to"/>: the gates of anyone hostile to it
		/// and the posts of their pickets, km off its root.</summary>
		static cs::List<DVec3> Watchers(cs::Ref<StarSystem> to, cs::Ref<Faction> f);

	public:
		/// <summary>The words for a campaign's force and mission, for the logs.</summary>
		static cs::String Describe(cs::Ref<Campaign> c);
	};
}
