// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Diplomacy.cs (see docs/MIGRATION.md).

#include "Combat/Diplomacy.h"

#include "Ships.h"
#include "Economy/Settlement.h"

namespace SpaceGod::Core
{
	const char* ToString(Relation v)
	{
		switch (v)
		{
		case Relation::Peace: return "Peace";
		case Relation::Ceasefire: return "Ceasefire";
		case Relation::War: return "War";
		}
		return "";
	}

	cs::Ref<Faction> Treaty::Loser() const { return Winner == nullptr ? nullptr : Winner == A ? B : A; }

	bool Treaty::Between(const cs::Ref<Faction>& x, const cs::Ref<Faction>& y) const { return (A == x && B == y) || (A == y && B == x); }

	Diplomacy::FactionPair Diplomacy::Key(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b)
	{
		return cs::Strings::CompareOrdinal(a->Name, b->Name) <= 0 ? FactionPair(a, b) : FactionPair(b, a);
	}

	Relation Diplomacy::Get(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const
	{
		if (a == b) return Relation::Peace;
		Relation r{};
		return relations.TryGetValue(Key(a, b), r) ? r : Relation::Peace;
	}

	void Diplomacy::Set(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, Relation r) { relations[Key(a, b)] = r; }

	// ---------------------------------------------------------------- treaties

	const cs::List<cs::Ref<Treaty>>& Diplomacy::Treaties() const { return treaties; }

	cs::Ref<Treaty> Diplomacy::Sign(const cs::Ref<Treaty>& t)
	{
		Set(t->A, t->B, Relation::Peace);
		treaties.Add(t);
		return t;
	}

	bool Diplomacy::InTruce(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd) const
	{
		for (const auto& t : treaties)
			if (t->Between(a, b) && jd < t->TruceUntilJd) return true;
		return false;
	}

	bool Diplomacy::Hostile(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const
	{
		if (a == nullptr || b == nullptr || a == b) return false;
		if (a->IsPirate || b->IsPirate) return true;
		return Get(a, b) == Relation::War;
	}

	bool Diplomacy::CanDock(const cs::Ref<Faction>& ship, const cs::Ref<Faction>& port) const
	{
		if (ship == port) return true;
		if (port->IsPirate) return ship->IsPirate;
		if (ship->IsPirate) return false;
		if (IsEmbargoed(port, ship)) return false;
		return Get(ship, port) != Relation::War;
	}

	bool Diplomacy::CanDockAt(const cs::Ref<Faction>& ship, const cs::Ref<Settlement>& port) const
	{
		return !blockades.ContainsKey(port) && CanDock(ship, port->Owner);
	}

	// ---------------------------------------------------------------- embargoes

	const cs::List<cs::Ref<Embargo>>& Diplomacy::Embargoes() const { return embargoes; }

	bool Diplomacy::IsEmbargoed(const cs::Ref<Faction>& by, const cs::Ref<Faction>& against) const
	{
		for (const auto& e : embargoes)
			if (e->By == by && e->Against == against) return true;
		return false;
	}

	cs::Ref<Embargo> Diplomacy::DeclareEmbargo(const cs::Ref<Faction>& by, const cs::Ref<Faction>& against, double jd, double untilJd, const cs::String& reason)
	{
		if (by == nullptr || against == nullptr || by == against || IsEmbargoed(by, against)) return nullptr;
		auto e = cs::New<Embargo>();
		e->By = by;
		e->Against = against;
		e->SinceJd = jd;
		e->UntilJd = untilJd;
		e->Reason = reason;
		embargoes.Add(e);
		return e;
	}

	bool Diplomacy::LiftEmbargo(const cs::Ref<Embargo>& e) { return embargoes.Remove(e); }

	// ---------------------------------------------------------------- blockades

	const cs::Dictionary<cs::Ref<Settlement>, cs::Ref<Faction>>& Diplomacy::Blockades() const { return blockades; }

	cs::Ref<Faction> Diplomacy::BlockadedBy(const cs::Ref<Settlement>& port) const
	{
		cs::Ref<Faction> f;
		return port != nullptr && blockades.TryGetValue(port, f) ? f : nullptr;
	}

	bool Diplomacy::DeclareBlockade(const cs::Ref<Settlement>& port, const cs::Ref<Faction>& by)
	{
		if (port == nullptr || by == nullptr || blockades.ContainsKey(port)) return false;
		blockades[port] = by;
		return true;
	}

	bool Diplomacy::LiftBlockade(const cs::Ref<Settlement>& port) { return port != nullptr && blockades.Remove(port); }
}
