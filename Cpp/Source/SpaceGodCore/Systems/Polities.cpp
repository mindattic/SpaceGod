// Port of Assets/UnitySpace/Core/Systems/Polities.cs (US-T17).

#include "Systems/Polities.h"
#include "Ships.h"
#include "World.h"
#include "Fleet.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Combat/Diplomacy.h"
#include "Systems/StrategicCommand.h"
#include "Systems/Statecraft.h"

namespace SpaceGod::Core
{
	const char* ToString(PolityKind v)
	{
		switch (v)
		{
		case PolityKind::State: return "State";
		case PolityKind::Company: return "Company";
		case PolityKind::FreeTraders: return "FreeTraders";
		case PolityKind::PirateClan: return "PirateClan";
		}
		return "";
	}

	Polities::Polities()
		: Diplomacy(cs::New<SpaceGod::Core::Diplomacy>())
	{
		// C# passes `this`: a non-owning reference (the Polities owns them, not the other way round).
		cs::Ref<Polities> self(cs::Ref<Polities>(), this);
		Command = cs::New<StrategicCommand>(self);
		Statecraft = cs::New<SpaceGod::Core::Statecraft>(self);
	}

	void Polities::Register(const cs::Ref<World>& world)
	{
		if (!worlds.Contains(world)) worlds.Add(world);
	}

	cs::Ref<Faction> Polities::Find(const cs::String& name) const
	{
		for (const auto& f : factions)
			if (f->Name == name) return f;
		return nullptr;
	}

	void Polities::Found(const cs::Ref<Faction>& f, const cs::Ref<World>& home)
	{
		factions.Add(f);
		homes[f] = home;
	}

	cs::Ref<World> Polities::HomeOf(const cs::Ref<Faction>& f) const
	{
		cs::Ref<World> w;
		return f != nullptr && homes.TryGetValue(f, w) ? w : nullptr;
	}

	cs::List<cs::Ref<Settlement>> Polities::Settlements() const
	{
		cs::List<cs::Ref<Settlement>> all;
		for (const auto& w : worlds) all.AddRange(w->Economy->Settlements);
		return all;
	}

	cs::List<cs::Ref<Settlement>> Polities::SettlementsOf(const cs::Ref<Faction>& f) const
	{
		cs::List<cs::Ref<Settlement>> r;
		for (const auto& w : worlds)
			for (const auto& s : w->Economy->Settlements)
				if (s->Owner == f && !s->Fallen) r.Add(s);
		return r;
	}

	cs::Ref<World> Polities::WorldOf(const cs::Ref<Settlement>& s) const
	{
		for (const auto& w : worlds)
			for (const auto& x : w->Economy->Settlements)
				if (x == s) return w;
		return nullptr;
	}

	cs::Ref<Faction> Polities::HolderOf(const cs::Ref<World>& w)
	{
		cs::Ref<Settlement> best = nullptr;
		for (const auto& s : w->Economy->Settlements)
		{
			if (s->Fallen || s->Owner == nullptr || s->Owner->IsPirate) continue;
			if (best == nullptr || s->Population > best->Population) best = s;
		}
		return best ? best->Owner : nullptr;
	}

	cs::List<cs::Ref<World>> Polities::SystemsHeldBy(const cs::Ref<Faction>& f) const
	{
		cs::List<cs::Ref<World>> held;
		for (const auto& w : worlds)
			if (HolderOf(w) == f) held.Add(w);
		return held;
	}

	cs::List<cs::Ref<World>> Polities::SystemsWithHoldings(const cs::Ref<Faction>& f) const
	{
		cs::List<cs::Ref<World>> list;
		for (const auto& w : worlds)
			for (const auto& s : w->Economy->Settlements)
				if (s->Owner == f && !s->Fallen)
				{
					list.Add(w);
					break;
				}
		return list;
	}

	cs::Ref<Settlement> Polities::CapitalOf(const cs::Ref<Faction>& f) const
	{
		if (f == nullptr) return nullptr;
		auto c = f->Capital;
		if (c != nullptr && c->Owner == f && !c->Fallen) return c;
		cs::Ref<Settlement> best = nullptr;
		for (const auto& s : SettlementsOf(f))
			if (best == nullptr || s->Population > best->Population || (f->IsPirate && s->Kind == SettlementKind::Haven && best->Kind != SettlementKind::Haven)) best = s;
		if (best != nullptr) f->Capital = best;
		return best;
	}

	cs::Ref<World> Polities::CapitalSystemOf(const cs::Ref<Faction>& f) const
	{
		auto c = CapitalOf(f);
		return c == nullptr ? nullptr : WorldOf(c);
	}

	cs::List<cs::Ref<Ship>> Polities::Navy(const cs::Ref<Faction>& f) const
	{
		cs::List<cs::Ref<Ship>> navy;
		for (const auto& w : worlds)
			for (const auto& s : w->Fleet->Ships())
				if (!s->Destroyed && s->Faction == f && s->Spec->IsMilitary) navy.Add(s);
		if (InTransit != nullptr)
			for (const auto& s : InTransit())
				if (!s->Destroyed && s->Faction == f && s->Spec->IsMilitary) navy.Add(s);
		return navy;
	}

	int Polities::ConvoyCount(const cs::Ref<Faction>& f) const
	{
		int n = 0;
		for (const auto& w : worlds)
			for (const auto& s : w->Fleet->Ships())
				if (!s->Destroyed && s->Faction == f && s->Convoy && !s->IsEscort()) n++;
		if (InTransit != nullptr)
			for (const auto& s : InTransit())
				if (!s->Destroyed && s->Faction == f && s->Convoy && !s->IsEscort()) n++;
		return n;
	}

	bool Polities::AtWar(const cs::Ref<Faction>& f) const
	{
		for (const auto& g : factions)
			if (g != f && !g->IsPirate && Diplomacy->Get(f, g) == Relation::War) return true;
		return false;
	}

	cs::List<cs::Ref<Faction>> Polities::KeptBy(const cs::Ref<World>& world, const cs::List<cs::Ref<Faction>>& present) const
	{
		cs::List<cs::Ref<Faction>> kept;
		for (const auto& f : present)
		{
			auto home = HomeOf(f);
			if (home == nullptr || home == world) kept.Add(f);
		}
		return kept;
	}
}
