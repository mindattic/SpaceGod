// Port of Assets/UnitySpace/Core/Combat/BoardingShuttles.cs (see docs/MIGRATION.md).

#include "Combat/BoardingShuttles.h"

#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	cs::String BoardingShuttles::Seconds(double v) { return cs::Fmt(v, "0") + " s"; }

	const cs::Ref<RangeSetting>& BoardingShuttles::TeamSize()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.shuttleTeam", SettingGroups::Broadsides,
			"Boarding shuttle team", 24, 6, 60, 1,
			"How many marines a carrier's boarding shuttle carries across to a prize. The carrier keeps enough aboard to defend "
			"itself and sends a shuttle only with a whole team to spare.", cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0") + " marines"; }),
			"boarding shuttle assault shuttle marines team carrier");
		return s;
	}

	const cs::Ref<RangeSetting>& BoardingShuttles::LatchSeconds()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.shuttleLatch", SettingGroups::Broadsides,
			"Boarding shuttle docking", 12, 2, 60, 1,
			"How long (seconds) a boarding shuttle lies matched alongside its prize, docking and latching on, before its team goes in "
			"through the airlock.", cs::Func<cs::String, double>(&Seconds), "boarding shuttle dock latch airlock");
		return s;
	}

	const cs::Ref<RangeSetting>& BoardingShuttles::RangeKm()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.shuttleRange", SettingGroups::Broadsides,
			"Boarding shuttle range", 40, 5, 120, 5,
			"How far (km) from its carrier a prize may lie for the carrier to send a boarding shuttle to it.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0") + " km"; }), "boarding shuttle range carrier prize");
		return s;
	}

	const cs::Ref<RangeSetting>& BoardingShuttles::GateSeizeDays()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.shuttleGateDays", SettingGroups::Broadsides,
			"Gate seizure with shuttles", 1, 1, 2, 1,
			"How many days a war fleet whose group carries boarding shuttles holds a jump gate whose forts it has silenced before its "
			"boarding parties have taken it (a fleet without them takes two).", cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0") + " d"; }),
			"boarding shuttle gate seize capture days");
		return s;
	}

	bool BoardingShuttles::Is(const cs::Ref<BattleUnit>& u) { return u != nullptr && u->Fighter != nullptr && u->Fighter->Role == FighterRole::AssaultShuttle; }

	bool BoardingShuttles::CanSeizeGate(const cs::List<cs::Ref<Ship>>& group)
	{
		// C# `if (group == null) return false;`: a list reference here is never null.
		for (const auto& s : group)
			if (s != nullptr && !s->Destroyed && s->Combat != nullptr && CarrierWings::ReadyShuttle(s->Combat) != nullptr && s->Combat->MarinesAvailable() >= TeamSize()->Value + Reserve)
				return true;
		return false;
	}

	double BoardingShuttles::GateHoldDays(double holdDays, bool shuttles) { return shuttles ? cs::Math::Min(holdDays, GateSeizeDays()->Value) : holdDays; }

	// The battle's side of the boarding shuttles (U.SPC-T486): who sends them, at what, and what happens when one docks.

	/// <summary>
	/// A carrier sends a boarding shuttle (U.SPC-T486): with one docked in its shuttle berths fit to fly (a crippled one
	/// stays aboard, U.SPC-T590) and a working hangar, marines enough for a team and its own reserve, and a prize in reach
	/// (<see cref="ShuttleTarget"/>), one shuttle every <see cref="BoardingShuttles.LaunchSeconds"/>, each to a prize no
	/// other shuttle is already flying to.
	/// </summary>
	void Battle::SendShuttles(const cs::Ref<BattleUnit>& u, double dt)
	{
		auto c = u->Combat;
		if (u->Kind != UnitKind::Ship || c->Shuttles() <= 0) return;
		if (u->ShuttleLaunchIn > 0)
		{
			u->ShuttleLaunchIn -= dt;
			return;
		}
		if (u->Fleeing || c->Disabled() || u->Captured || u->Surrendering || u->PlayerControlled()) return;
		if (!(u->Faction->IsPirate || u->Faction->IsMilitary || (Boards != nullptr && Boards(u)))) return;
		if (!HangarWorks(u)) return;
		double team = BoardingShuttles::TeamSize()->Value;
		if (c->MarinesAvailable() < team + BoardingShuttles::Reserve) return;
		auto hull = CarrierWings::ReadyShuttle(c);
		if (hull == nullptr) return;
		auto target = ShuttleTarget(u);
		if (target == nullptr) return;
		FighterAI::LaunchShuttle(shared_from_this(), u, target, c->Interior->TakeMarines(team), hull);
		u->ShuttleLaunchIn = BoardingShuttles::LaunchSeconds;
	}

	bool Battle::HangarWorks(const cs::Ref<BattleUnit>& u)
	{
		for (const auto& s : u->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::Hangar && !s->Destroyed()) return true;
		return false;
	}

	/// <summary>Whether <paramref name="carrier"/> boards by shuttle: it has one in its hangars or out, so it sends them
	/// rather than closing to board with its own hull.</summary>
	bool Battle::BoardsByShuttle(const cs::Ref<BattleUnit>& carrier)
	{
		if (CarrierWings::ReadyShuttle(carrier->Combat) != nullptr) return true;
		for (const auto& o : Units)
			if (o->Present() && BoardingShuttles::Is(o) && o->Fighter->Carrier == carrier) return true;
		return false;
	}

	/// <summary>
	/// Whether a shuttle (or the carrier sending it), <paramref name="seeker"/>, may board <paramref name="o"/> now: a
	/// hostile hull or station on the field, not taken and not being boarded by anyone else, that its side may board
	/// (a hull waiting on terms only once they open it; a merchant's hold is asked for first), and that has stopped
	/// fighting: disabled, struck on terms that open it, or stunned.
	/// </summary>
	bool Battle::ShuttleMayBoard(const cs::Ref<BattleUnit>& seeker, const cs::Ref<BattleUnit>& o)
	{
		if (seeker == nullptr || o == nullptr || !o->Present() || !Hostile(seeker, o) || o->Captured || o->Kind == UnitKind::Fighter) return false;
		auto boarder = seeker->Fighter != nullptr && seeker->Fighter->Carrier != nullptr ? seeker->Fighter->Carrier : seeker;
		auto inside = o->Combat->Interior;
		// A rival's boarding is theirs; an ally's on the same side is joined (U.SPC-T392).
		if (!inside->Admits(seeker->Faction, seeker->Side)) return false;
		if (!MayBoard(boarder, o) || (o->Surrendering && !o->SurrenderOpenToBoarding)) return false;
		if (o->Ship != nullptr && Spares(boarder, o) && o->Ship->HasCargo && !o->SurrenderOpenToBoarding) return false;
		return o->Combat->Disabled() || o->SurrenderOpenToBoarding || o->Stunned > 10;
	}

	/// <summary>The prize a shuttle (or the carrier sending it) goes for: the nearest it may board within
	/// <see cref="BoardingShuttles.RangeKm"/>, with no armed enemy near it and no other shuttle of its side already
	/// flying to it (one its side is already boarding only while its defenders outnumber the boarders); or null.</summary>
	cs::Ref<BattleUnit> Battle::ShuttleTarget(const cs::Ref<BattleUnit>& seeker)
	{
		cs::Ref<BattleUnit> best = nullptr;
		double bestD = BoardingShuttles::RangeKm()->Value;
		for (const auto& o : Units)
		{
			if (!ShuttleMayBoard(seeker, o)) continue;
			double d = (o->Pos - seeker->Pos).Length();
			if (d > bestD || ThreatNear(seeker, o->Pos, BoardingShuttles::ThreatKm) || ShuttleBound(seeker, o)) continue;
			// A hull its side is already boarding gets another team only while the defenders still outnumber them.
			auto inside = o->Combat->Interior;
			if (inside->Boarder != nullptr && inside->Boarders() >= inside->Marines() + inside->Crew() * 0.15) continue;
			bestD = d;
			best = o;
		}
		return best;
	}

	/// <summary>Whether another shuttle of <paramref name="seeker"/>'s side is already flying to <paramref name="o"/>.</summary>
	bool Battle::ShuttleBound(const cs::Ref<BattleUnit>& seeker, const cs::Ref<BattleUnit>& o)
	{
		for (const auto& x : Units)
			if (x != seeker && x->Present() && x->Side == seeker->Side && BoardingShuttles::Is(x) && x->BoardTarget == o) return true;
		return false;
	}

	/// <summary>
	/// A shuttle docked alongside <paramref name="t"/> latches on and opens its airlock (U.SPC-T486): its team goes in
	/// at the compartment it docked against, fighting as well as it drilled in its carrier's habitats, with no breach
	/// and no damage to the hull. If the hull is no longer theirs to take (boarded by another faction, or already
	/// held), the team stays aboard and the shuttle casts off.
	/// </summary>
	void Battle::ShuttleLatch(const cs::Ref<BattleUnit>& shuttle, const cs::Ref<BattleUnit>& t)
	{
		shuttle->Latched = t;
		shuttle->LatchLocal = t->ToLocal(shuttle->Pos);
		shuttle->BoardTarget = nullptr;
		shuttle->LatchTimer = 0;
		auto inside = t->Combat->Interior;
		auto at = inside->Nearest(shuttle->LatchLocal.Z, shuttle->LatchLocal.Y);
		double marines = shuttle->Combat->Interior->TakeMarines(shuttle->Combat->Interior->Marines());
		double drill = shuttle->Fighter != nullptr ? shuttle->Fighter->Drill : 1;
		if (marines < 0.5 || !inside->Board(shuttle->Faction, shuttle->Faction->MarineQuality * drill, marines, at, false, Seconds, rng, shuttle->Side))
		{
			shuttle->Combat->Interior->Embark(0, marines);
			shuttle->Latched = nullptr;
			return;
		}
		Emit(FxKind::Latch, shuttle->Pos, t->Pos, shuttle->LengthKm, shuttle->Side, t, at->Index);
	}
}
