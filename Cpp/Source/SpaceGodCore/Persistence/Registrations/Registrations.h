#pragma once
// Not a C# file (US-T36): what every Persistence/Registrations/*.cpp shares. The value-tuple stand-ins (C++ structs a
// porter named for a C# `(A, B)` tuple: they save as System.ValueTuple`N, as the C# fields are declared) and helpers for
// the few fields whose C# declared type the C++ member's type does not name.

#include "Persistence/ObjectGraph.h"

#include "Combat/War.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/InterstellarWars.h"
#include "Systems/Statecraft.h"

// C#: List<(double, string)> Appraisal.Reasons
SG_TUPLE(SpaceGod::Core::Appraisal::Reason, &SpaceGod::Core::Appraisal::Reason::Weight, &SpaceGod::Core::Appraisal::Reason::Why)
// C#: List<(StarSystem, Ship)> InterstellarTraffic.atGates
SG_TUPLE(SpaceGod::Core::InterstellarTraffic::AtGateEntry, &SpaceGod::Core::InterstellarTraffic::AtGateEntry::system,
	&SpaceGod::Core::InterstellarTraffic::AtGateEntry::ship)
// C#: List<(StarSystem from, StarSystem to, Faction quarry, double jd)> InterstellarTraffic.expeditionsWanted
SG_TUPLE(SpaceGod::Core::InterstellarTraffic::ExpeditionWanted, &SpaceGod::Core::InterstellarTraffic::ExpeditionWanted::from,
	&SpaceGod::Core::InterstellarTraffic::ExpeditionWanted::to, &SpaceGod::Core::InterstellarTraffic::ExpeditionWanted::quarry,
	&SpaceGod::Core::InterstellarTraffic::ExpeditionWanted::jd)
// C#: Dictionary<JumpGate, (Conflict, Faction)> InterstellarWars.takenGates
SG_TUPLE(SpaceGod::Core::InterstellarWars::TakenGate, &SpaceGod::Core::InterstellarWars::TakenGate::War,
	&SpaceGod::Core::InterstellarWars::TakenGate::Before)
// C#: Dictionary<(Faction, Faction), double> Statecraft.opinions; Dictionary<(Faction, Faction), (Faction, double)> warsBegun
SG_TUPLE(SpaceGod::Core::Statecraft::FactionPair, &SpaceGod::Core::Statecraft::FactionPair::Item1, &SpaceGod::Core::Statecraft::FactionPair::Item2)
SG_TUPLE(SpaceGod::Core::Statecraft::WarBegun, &SpaceGod::Core::Statecraft::WarBegun::By, &SpaceGod::Core::Statecraft::WarBegun::Since)
// C#: Dictionary<Ship, (Lane, double)> War.laneSeen
SG_TUPLE(SpaceGod::Core::War::LanePass, &SpaceGod::Core::War::LanePass::lane, &SpaceGod::Core::War::LanePass::at)

namespace SpaceGod::Core::Registrations
{
	/// System.Collections.Generic.IEnumerable`1 / IReadOnlyList`1 of <paramref name="item"/>, as a field's declared type.
	inline const Graph::NetType* IEnumerableOf(const Graph::NetType* item) { return Graph::Generic("System.Collections.Generic.IEnumerable`1", Graph::CoreLibAssembly, { item }, Graph::Kind::Object); }
	inline const Graph::NetType* IReadOnlyListOf(const Graph::NetType* item) { return Graph::Generic("System.Collections.Generic.IReadOnlyList`1", Graph::CoreLibAssembly, { item }, Graph::Kind::Object); }
	template <class T> const Graph::NetType* TypeOf() { return Graph::Codec<T>::Type(); }
}
