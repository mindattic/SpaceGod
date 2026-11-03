// Port of Assets/UnitySpace/Core/Systems/GateNetwork.cs (US-T17).

#include "Systems/GateNetwork.h"
#include "Cs/Cs.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"
#include "Systems/GateConstruction.h"
#include "Ships.h"
#include "World.h"
#include "Combat/CombatState.h"
#include "Combat/CombatSpecs.h"

namespace SpaceGod::Core
{
	static_assert(GateNetwork::MaxLinkLy == GateConstruction::MaxLinkLy, "GateNetwork.MaxLinkLy is GateConstruction.MaxLinkLy");

	namespace
	{
		// The C# (int, int) value tuple, as a HashSet key.
		struct IntPair
		{
			int Item1 = 0, Item2 = 0;
			bool operator==(const IntPair& o) const { return Item1 == o.Item1 && Item2 == o.Item2; }
			size_t HashCode() const { return cs::HashCombine(std::hash<int>{}(Item1), std::hash<int>{}(Item2)); }
		};

		struct PairLy { double ly = 0; int i = 0, j = 0; };
	}

	// ---------------------------------------------------------------- GateLink

	double GateLink::TransitDays() const { return GateNetwork::TransitDays(Ly); }

	GateLink::GateLink(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b, const cs::Ref<JumpGate>& atA, const cs::Ref<JumpGate>& atB)
		: A(a), B(b), AtA(atA), AtB(atB)
	{
		Ly = Galaxy::DistanceLy(a, b);
	}

	bool GateLink::Working() const { return AtA->Working() && AtB->Working(); }

	bool GateLink::Open(const cs::Ref<StarSystem>& from, const cs::Ref<Faction>& faction, double jd) const
	{
		auto near = GateIn(from);
		return near != nullptr && Working() && near->Admits(faction, jd) && near->Partner->Admits(faction, jd);
	}

	bool GateLink::Forcible(const cs::Ref<StarSystem>& from, const cs::Ref<Faction>& faction, double jd) const
	{
		auto near = GateIn(from);
		return near != nullptr && Working() && GateNetwork::Passable(near, faction, jd);
	}

	cs::String GateLink::ToString() const { return AtA->Name + " / " + AtB->Name; }

	// ---------------------------------------------------------------- GateNetwork

	cs::Ref<GateNetwork> GateNetwork::Build(cs::List<cs::Ref<StarSystem>>& systems, double jd, const cs::Ref<Polities>& polities, int neighbours, double maxLinkLy)
	{
		auto net = cs::New<GateNetwork>();
		net->systems.AddRange(systems);
		int n = systems.Count();
		cs::HashSet<IntPair> linked;
		auto Join = [&](int i, int j)
		{
			auto key = i < j ? IntPair{ i, j } : IntPair{ j, i };
			if (i == j || !linked.Add(key)) return;
			net->Link(systems[key.Item1], systems[key.Item2]);
		};

		// Each system's nearest few within reach (by distance, ties by list order so the lattice is the same every run).
		for (int i = 0; i < n; i++)
		{
			cs::List<int> order;
			for (int j = 0; j < n; j++) if (j != i) order.Add(j);
			int me = i;
			order.Sort([&](int a, int b)
			{
				int c = cs::CompareTo(Galaxy::DistanceLy(systems[me], systems[a]), Galaxy::DistanceLy(systems[me], systems[b]));
				return c != 0 ? c : cs::CompareTo(a, b);
			});
			int joined = 0;
			for (int k = 0; k < order.Count() && joined < neighbours; k++)
			{
				// Sorted ascending: the first one out of reach means every one after it is too.
				if (Galaxy::DistanceLy(systems[i], systems[order[k]]) > maxLinkLy) break;
				Join(i, order[k]);
				joined++;
			}
		}

		// Join any islands by their shortest links (Kruskal over all pairs, keeping only those that bridge two pieces);
		// one too wide for a direct gate is bridged by Void Bases instead (U.SPC-T372).
		auto parent = cs::NewArray<int>(n);
		for (int i = 0; i < n; i++) parent[i] = i;
		auto Find = [&](int x)
		{
			while (parent[x] != x) x = parent[x] = parent[parent[x]];
			return x;
		};
		// C# evaluates parent[Find(i)]'s index before Find(j) (C++17 would do the right side first): spelled out.
		for (const auto& key : linked) { int i = key.Item1, j = key.Item2; int fi = Find(i); parent[fi] = Find(j); }
		cs::List<PairLy> pairs;
		for (int i = 0; i < n; i++)
			for (int j = i + 1; j < n; j++)
				pairs.Add({ Galaxy::DistanceLy(systems[i], systems[j]), i, j });
		pairs.Sort([](const PairLy& a, const PairLy& b) { return a.ly != b.ly ? cs::CompareTo(a.ly, b.ly) : a.i != b.i ? cs::CompareTo(a.i, b.i) : cs::CompareTo(a.j, b.j); });
		int nextVoidBase = 1;
		for (const auto& s : systems) if (s->IsVoidBase) nextVoidBase++;
		for (const auto& [ly, i, j] : pairs)
		{
			if (Find(i) == Find(j)) continue;
			// C# evaluates the left side's index before the right side; spelled out so C++ does too.
			int fi = Find(i);
			parent[fi] = Find(j);
			if (ly <= maxLinkLy) Join(i, j);
			else BridgeWithRelays(net, systems, systems[i], systems[j], jd, polities, maxLinkLy, nextVoidBase);
		}
		net->AssignOwners();
		return net;
	}

	void GateNetwork::BridgeWithRelays(const cs::Ref<GateNetwork>& net, cs::List<cs::Ref<StarSystem>>& systems, const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b, double jd, const cs::Ref<Polities>& polities,
		double maxLinkLy, int& nextVoidBase)
	{
		auto prev = a;
		for (const auto& positionLy : RelayPositions(a->PositionLy, b->PositionLy, maxLinkLy))
		{
			auto relay = StarSystem::CreateVoidBase(cs::Cat("Void Base ", nextVoidBase++), positionLy, jd, polities);
			systems.Add(relay);
			net->Link(prev, relay);
			prev = relay;
		}
		net->Link(prev, b);
	}

	cs::List<DVec3> GateNetwork::RelayPositions(DVec3 aLy, DVec3 bLy, double maxLy)
	{
		cs::List<DVec3> list;
		double total = (bLy - aLy).Length();
		int hops = cs::Math::Max(1, cs::ToInt(cs::Math::Ceiling(total / maxLy - 1e-9)));
		for (int k = 1; k < hops; k++) list.Add(aLy + (bLy - aLy) * (static_cast<double>(k) / hops));
		return list;
	}

	cs::Ref<GateLink> GateNetwork::Link(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b)
	{
		auto atA = cs::New<JumpGate>(GateName(b), a, Galaxy::Direction(a, b) * Galaxy::GateDistanceKm(a));
		auto atB = cs::New<JumpGate>(GateName(a), b, Galaxy::Direction(b, a) * Galaxy::GateDistanceKm(b));
		atA->Partner = atB;
		atB->Partner = atA;
		auto link = cs::New<GateLink>(a, b, atA, atB);
		atA->Link = atB->Link = link;
		a->AddGate(atA);
		b->AddGate(atB);
		links.Add(link);
		if (!systems.Contains(a)) systems.Add(a);
		if (!systems.Contains(b)) systems.Add(b);
		return link;
	}

	cs::String GateNetwork::GateName(const cs::Ref<StarSystem>& to)
	{
		return (to->Name == "Sol System" ? cs::String("Sol") : to->Name == "Alpha Centauri" ? cs::String("Centauri") : to->Name) + " Gate";
	}

	void GateNetwork::AssignOwners()
	{
		for (const auto& l : links)
		{
			l->AtA->Owner = l->A->World->Sol;
			l->AtB->Owner = l->B->World->Sol;
		}
	}

	void GateNetwork::Fortify()
	{
		for (const auto& l : links)
		{
			Fortify(l->AtA);
			Fortify(l->AtB);
		}
	}

	void GateNetwork::Fortify(const cs::Ref<JumpGate>& g)
	{
		if (g == nullptr) return;
		g->MaxForts = g->Owner != nullptr && g->Owner->IsMilitary && g->Owner->Kind == PolityKind::State ? GateForts : 0;
		while (g->Forts.Count() < g->MaxForts) g->Forts.Add(cs::New<ShipCombat>(CombatSpec::Platform()));
	}

	bool GateNetwork::Passable(const cs::Ref<JumpGate>& near, const cs::Ref<Faction>& faction, double jd)
	{
		return near != nullptr && near->Partner != nullptr && (near->Admits(faction, jd) || near->HostileTo(faction)) && (near->Partner->Admits(faction, jd) || near->Partner->HostileTo(faction));
	}

	cs::List<cs::Ref<GateLink>> GateNetwork::LinksOf(const cs::Ref<StarSystem>& s) const
	{
		cs::List<cs::Ref<GateLink>> r;
		for (const auto& l : links)
			if (l->A == s || l->B == s) r.Add(l);
		return r;
	}

	cs::Ref<GateLink> GateNetwork::Between(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b) const
	{
		for (const auto& l : links)
			if ((l->A == a && l->B == b) || (l->A == b && l->B == a)) return l;
		return nullptr;
	}

	GateNetwork::RouteList GateNetwork::Route(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Faction>& traveller, double jd, bool force) const
	{
		if (from == nullptr || to == nullptr) return nullptr;
		if (from == to) return cs::New<cs::List<cs::Ref<JumpGate>>>();
		cs::Dictionary<cs::Ref<StarSystem>, double> best;
		best[from] = 0;
		cs::Dictionary<cs::Ref<StarSystem>, cs::Ref<JumpGate>> via;
		cs::HashSet<cs::Ref<StarSystem>> done;
		while (true)
		{
			cs::Ref<StarSystem> here = nullptr;
			double hereDays = cs::Double::MaxValue;
			for (const auto& [key, value] : best)
				if (!done.Contains(key) && value < hereDays)
				{
					hereDays = value;
					here = key;
				}
			if (here == nullptr) return nullptr;
			if (here == to) break;
			done.Add(here);
			for (const auto& l : LinksOf(here))
			{
				auto next = l->Other(here);
				if (done.Contains(next) || !l->Working()) continue;
				double forcing = 0;
				if (traveller != nullptr && !l->Open(here, traveller, jd))
				{
					if (!force || !l->Forcible(here, traveller, jd)) continue;
					forcing = ForcingDays;
				}
				double days = hereDays + l->TransitDays() + (here == from ? 0 : CrossingDays) + forcing;
				double known;
				if (!best.TryGetValue(next, known) || days < known)
				{
					best[next] = days;
					via[next] = l->GateIn(here);
				}
			}
		}
		auto route = cs::New<cs::List<cs::Ref<JumpGate>>>();
		for (auto s = to; s != from; s = via.At(s)->Home) route->Add(via.At(s));
		route->Reverse();
		return route;
	}

	double GateNetwork::RouteDays(const RouteList& route)
	{
		if (route == nullptr || route->Count() == 0) return 0;
		double days = (route->Count() - 1) * CrossingDays;
		for (const auto& g : *route) days += g->Link->TransitDays();
		return days;
	}

	double GateNetwork::RouteLy(const RouteList& route)
	{
		double ly = 0;
		if (route != nullptr)
			for (const auto& g : *route) ly += g->Link->Ly;
		return ly;
	}
}
