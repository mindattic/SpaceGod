// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/GatePickets.cs (see docs/MIGRATION.md).

#include "Combat/GatePickets.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "DVec3.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/PatrolWings.h"
#include "Combat/War.h"
#include "Combat/Diplomacy.h"
#include "Economy/Economy.h"
#include "Sensors/Sensors.h"
#include "Settings/GameSetting.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"

namespace SpaceGod::Core
{
	namespace
	{
		// C#'s `gate?.Home?.World`.
		cs::Ref<World> WorldOf(const cs::Ref<JumpGate>& gate)
		{
			return gate != nullptr && gate->Home != nullptr ? gate->Home->World : nullptr;
		}
	}

	cs::String PicketWarning::ToString() const
	{
		return cs::Cat(Picket != nullptr ? Picket->Name : cs::String(), ": ", Hulls, " of the ", By != nullptr ? By->Name : cs::String(), " ",
			(SelfJump ? "out of FTL" : "through the gate"));
	}

	const cs::Ref<RangeSetting>& GatePickets::PerGate()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("pickets.perGate", SettingGroups::Ai,
			"Gate pickets per gate", 1, 0, 4, 1,
			"How many gate pickets (small, sensor-heavy early-warning hulls) a navy keeps at the exit of each jump gate it holds. 0 leaves the gates blind.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0"); }), "picket gate sensor early warning navy");
		return s;
	}

	const cs::Ref<RangeSetting>& GatePickets::JumpWatchAu()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("pickets.jumpWatch", SettingGroups::Ai,
			"Picket FTL watch", 4, 0, 20, 0.5,
			"How far (AU) from a gate picket a hostile group dropping out of FTL under its own drive is seen and reported to the picket's navy.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0.#"), " AU"); }), "picket gate ftl jump drop out warning");
		return s;
	}

	bool GatePickets::Keeps(const cs::Ref<JumpGate>& gate)
	{
		return (gate != nullptr && gate->Owner != nullptr) && gate->Owner->IsMilitary && !gate->Owner->IsPirate && gate->Owner->Kind == PolityKind::State;
	}

	int GatePickets::Wanted(const cs::Ref<JumpGate>& gate) { return Keeps(gate) ? cs::Math::Max(0, cs::ToInt(cs::Math::Round(PerGate()->Value))) : 0; }

	DVec3 GatePickets::Post(const cs::Ref<JumpGate>& gate, int index, int count)
	{
		auto axis = gate->PositionKm.LengthSquared() > 1e-9 ? gate->PositionKm.Normalized() : DVec3::UnitX;
		auto side = DVec3::Cross(axis, cs::Math::Abs(axis.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX).Normalized();
		double across = (index - 0.5 * (cs::Math::Max(1, count) - 1)) * PostSpacingKm;
		return gate->PositionKm - axis * PostInsetKm + side * across;
	}

	bool GatePickets::Serves(const cs::Ref<World>& w, const cs::Ref<Ship>& s, const cs::Ref<JumpGate>& gate)
	{
		return s->PicketGate == gate && !s->Destroyed && !s->Derelict && w->Fleet->IsPresent(s);
	}

	cs::List<cs::Ref<Ship>> GatePickets::Of(const cs::Ref<JumpGate>& gate)
	{
		auto list = cs::List<cs::Ref<Ship>>();
		auto w = WorldOf(gate);
		if (w == nullptr) return list;
		for (const auto& s : w->Fleet->Ships())
			if (Serves(w, s, gate)) list.Add(s);
		return list;
	}

	bool GatePickets::OnPost(const cs::Ref<World>& w, const cs::Ref<Ship>& picket)
	{
		return (picket != nullptr && picket->PicketGate != nullptr) && !picket->Destroyed && Fleet::IsAt(picket, w->Catalog()->Root()->Index) && picket->Orbit != nullptr &&
			(picket->Orbit->OffsetAt(w->Jd) - picket->PicketPostKm).Length() <= War::GateReachKm;
	}

	void GatePickets::PostAll(const cs::Ref<GateNetwork>& network, double jd)
	{
		if (network == nullptr) return;
		for (const auto& l : network->Links())
		{
			PostFull(l->AtA, jd);
			PostFull(l->AtB, jd);
		}
	}

	void GatePickets::PostFull(const cs::Ref<JumpGate>& gate, double jd)
	{
		if (WorldOf(gate) == nullptr) return;
		int wanted = Wanted(gate);
		int have = 0;
		for (const auto& s : Of(gate))
			if (s->Faction == gate->Owner) have++;
		for (int i = have; i < wanted; i++) Station(gate, i, wanted, jd);
	}

	cs::Ref<Ship> GatePickets::Station(const cs::Ref<JumpGate>& gate, int index, int count, double jd)
	{
		auto w = gate->Home->World;
		auto f = gate->Owner;
		// Gates are named for where they lead, so two systems can each hold a "Centauri Gate"; the home system keeps hull names unique.
		cs::String name = cs::Cat(gate->Name, " Picket", (index > 0 ? cs::Cat(" ", (index + 1)) : cs::String("")),
			(gate->Home != nullptr /* && Name != null: a C++ string is never null */ ?cs::Cat(" (", gate->Home->Name, ")") : cs::String("")));
		auto ship = cs::New<Ship>();
		ship->Name = f->Prefix.size() > 0 ? cs::Cat(f->Prefix, " ", name) : name;
		ship->Spec = ShipClassSpec::Of(ShipClass::GatePicket);
		ship->Faction = f;
		ship->Role = ShipRole::Patrol;
		ship->Combat = cs::New<ShipCombat>(CombatSpec::Of(ShipClass::GatePicket), cs::Nullable<ShipClass>(ShipClass::GatePicket));
		ship->PatrolWing = true;
		ship->PicketGate = gate;
		ship->PicketPostKm = Post(gate, index, count);
		w->Fleet->Receive(ship);
		ship->BandId = ship->Id;
		Fleet::PinAt(ship, w->Catalog()->Root()->Index, ship->PicketPostKm, -gate->PositionKm.Normalized(), jd);
		w->NotifyAdded(ship);
		return ship;
	}

	void GatePickets::Upkeep(const cs::Ref<JumpGate>& gate, double jd)
	{
		auto w = WorldOf(gate);
		if (w == nullptr) return;
		int have = 0;
		for (const auto& s : Of(gate))
		{
			if (s->Faction == gate->Owner && Keeps(gate)) have++;
			else StandDown(w, s);
		}
		int wanted = Wanted(gate);
		if (have >= wanted)
		{
			gate->PicketsShortSinceJd = cs::Double::NaN;
			return;
		}
		if (cs::Double::IsNaN(gate->PicketsShortSinceJd)) gate->PicketsShortSinceJd = jd;
		if (jd - gate->PicketsShortSinceJd < RebuildDays) return;
		auto f = gate->Owner;
		double price = Economy::HullPrice(ShipClass::GatePicket);
		if (f->Treasury < price || !Crews::CanCrew(f, ShipClass::GatePicket) || w->War->BattleAt(w->Catalog()->Root()->Index) != nullptr) return;
		f->Treasury -= price;
		Crews::Draft(f, Crews::Complement(ShipClass::GatePicket));
		Station(gate, have, wanted, jd);
		gate->PicketsShortSinceJd = have + 1 >= wanted ? cs::Double::NaN : jd;
	}

	void GatePickets::StandDown(const cs::Ref<World>& w, const cs::Ref<Ship>& picket)
	{
		picket->PicketGate = nullptr;
		auto ports = PatrolWings::Ports(w, picket->Faction);
		if (ports.Count() == 0) return;
		picket->Route = PatrolWings::Beat(ports, 0, 1);
		picket->RouteIndex = 0;
	}

	// ---------------------------------------------------------------- warnings

	cs::Ref<PicketWarning> GatePickets::OnArrival(const cs::Ref<JumpGate>& exit, const cs::List<cs::Ref<Ship>>& ships, double jd)
	{
		auto w = WorldOf(exit);
		if (w == nullptr || ships.Count() == 0) return nullptr;
		for (const auto& p : Of(exit))
			if (p->Faction == exit->Owner && OnPost(w, p)) return Warn(w, exit, p, ships, false, jd);
		return nullptr;
	}

	cs::Ref<PicketWarning> GatePickets::OnDropOut(const cs::Ref<StarSystem>& system, const cs::List<cs::Ref<Ship>>& ships, const DVec3& atKm, double jd)
	{
		auto w = system != nullptr ? system->World : nullptr;
		if (w == nullptr || ships.Count() == 0 || JumpWatchAu()->Value <= 0) return nullptr;
		double reach = JumpWatchAu()->Value * Astro::AuKm;
		cs::Ref<PicketWarning> first = nullptr;
		auto warned = cs::HashSet<cs::Ref<Faction>>();
		for (const auto& p : w->Fleet->Ships())
		{
			if (p->PicketGate == nullptr || warned.Contains(p->Faction) || !Serves(w, p, p->PicketGate) || !OnPost(w, p)) continue;
			if ((p->PicketPostKm - atKm).Length() > reach) continue;
			auto warning = Warn(w, p->PicketGate, p, ships, true, jd);
			if (warning == nullptr) continue;
			warned.Add(p->Faction);
			if (!first) first = warning;
		}
		return first;
	}

	bool GatePickets::Threat(const cs::Ref<World>& w, const cs::Ref<Faction>& owner, const cs::Ref<Ship>& s)
	{
		return s != nullptr && !s->Destroyed && s->Faction != nullptr && s->Faction != owner && (s->Spec->IsMilitary || s->Faction->IsPirate) &&
			w->Diplomacy()->Hostile(owner, s->Faction);
	}

	cs::Ref<PicketWarning> GatePickets::Warn(const cs::Ref<World>& w, const cs::Ref<JumpGate>& gate, const cs::Ref<Ship>& picket, const cs::List<cs::Ref<Ship>>& ships, bool selfJump, double jd)
	{
		auto owner = picket->Faction;
		auto leader = ships[0]->Controller() ? ships[0]->Controller() : ships[0];
		int hulls = 0;
		for (const auto& s : ships)
			if (Threat(w, owner, s)) hulls++;
		if (hulls == 0) return nullptr;
		// Straight into its navy's picture: the picket has them in its sights (raiders squawk nothing, but a picket
		// makes out a hull's class as it comes through).
		if (w->Sensors != nullptr)
			for (const auto& s : ships)
				if (Threat(w, owner, s)) w->Sensors->Knows(owner, s, jd);
		auto warning = cs::New<PicketWarning>();
		warning->Gate = gate; warning->Owner = owner; warning->Picket = picket; warning->Intruder = leader; warning->By = leader->Faction; warning->Hulls = hulls; warning->SelfJump = selfJump; warning->Jd = jd;
		auto& list = w->War->PicketWarnings;
		list.Add(warning);
		if (list.Count() > WarningsKept) list.RemoveRange(0, list.Count() - WarningsKept);
		cs::String what = hulls == 1 ? cs::String(leader->Faction->IsPirate ? "a raider" : "a warship") : cs::Cat(hulls, (leader->Faction->IsPirate ? " raiders" : " warships"));
		cs::String how = selfJump ? "dropping out of FTL nearby" : "coming through the gate";
		w->Log(jd, cs::Cat(picket->Name, " warns the ", owner->Name, ": ", what, " of the ", leader->Faction->Name, " (", leader->Name, ") ", how), WorldEventKind::Battle,
			w->Catalog()->Root()->Index);
		return warning;
	}
}
