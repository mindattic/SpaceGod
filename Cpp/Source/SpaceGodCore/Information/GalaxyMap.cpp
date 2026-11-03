// Port of Unity.SpaceGod Assets/UnitySpace/Core/Information/GalaxyMap.cs (see docs/MIGRATION.md).

#include "Information/GalaxyMap.h"

#include "Economy/Settlement.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"

namespace SpaceGod::Core
{
	const char* ToString(PirateActivity v)
	{
		switch (v)
		{
			case PirateActivity::None: return "None";
			case PirateActivity::Low: return "Low";
			case PirateActivity::High: return "High";
		}
		return "";
	}

	cs::String MapSystem::ToString() const { return System->Name; }

	cs::String MapLink::ToString() const { return cs::Cat(A->Name, " - ", B->Name); }

	cs::Ref<MapSystem> GalaxyMapModel::Of(const cs::Ref<StarSystem>& s) const
	{
		for (const auto& m : Systems)
			if (m->System == s) return m;
		return nullptr;
	}

	cs::Ref<MapLink> GalaxyMapModel::Between(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b) const
	{
		for (const auto& l : Links)
			if ((l->A == a && l->B == b) || (l->A == b && l->B == a)) return l;
		return nullptr;
	}

	bool GalaxyMapModel::AtWar(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const
	{
		if (a == nullptr || b == nullptr || a == b) return false;
		for (const auto& w : Wars)
			if (w.Involves(a) && w.Involves(b)) return true;
		return false;
	}

	cs::List<cs::Ref<MapLink>> GalaxyMapModel::Fronts() const
	{
		cs::List<cs::Ref<MapLink>> r;
		for (const auto& l : Links)
			if (l->WarFront) r.Add(l);
		return r;
	}

	cs::Ref<GalaxyMapModel> GalaxyMap::Build(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, double jd, const cs::List<cs::Ref<GateWork>>& works)
	{
		auto model = cs::New<GalaxyMapModel>();
		model->Here = here;
		model->Jd = jd;
		auto news = galaxy->Traffic ? galaxy->Traffic->News : nullptr;
		const cs::List<cs::Ref<GateCrossing>>* log = galaxy->Traffic ? &galaxy->Traffic->CrossingLog() : nullptr;
		cs::Ref<SystemReport> newest = nullptr;

		for (const auto& s : galaxy->Systems)
		{
			auto m = cs::New<MapSystem>();
			m->System = s;
			m->Here = s == here;
			m->Report = m->Here ? SystemReport::Take(s, jd) : (news ? news->Known(here, s) : nullptr);
			if (m->Report != nullptr)
			{
				m->AgeDays = m->Here ? 0 : cs::Math::Max(0.0, jd - m->Report->AsOfJd);
				if (newest == nullptr || m->Report->AsOfJd > newest->AsOfJd) newest = m->Report;
			}
			model->Systems.Add(m);
		}
		if (newest != nullptr) model->Wars.AddRange(newest->Wars);

		for (const auto& m : model->Systems)
		{
			auto r = m->Report;
			if (r == nullptr) continue;
			m->Holder = r->Holder;
			m->Markets = r->Markets.Count();
			for (const auto& st : r->Markets)
			{
				if (st->Fallen || st->Owner == nullptr || st->Owner->IsPirate) continue;
				cs::Ref<MapHolding> h = nullptr;
				for (const auto& x : m->Holdings)
					if (x->Faction == st->Owner) h = x;
				if (h == nullptr)
				{
					h = cs::New<MapHolding>();
					h->Faction = st->Owner;
					m->Holdings.Add(h);
				}
				h->Population += st->Population;
				h->Ports++;
			}
			m->Holdings.Sort([](const cs::Ref<MapHolding>& a, const cs::Ref<MapHolding>& b) { return cs::CompareTo(b->Population, a->Population); });
			for (int i = 0; i < m->Holdings.Count() && !m->Contested; i++)
				for (int j = i + 1; j < m->Holdings.Count(); j++)
					if (model->AtWar(m->Holdings[i]->Faction, m->Holdings[j]->Faction))
					{
						m->Contested = true;
						m->ContestedBy = FactionPair(m->Holdings[i]->Faction, m->Holdings[j]->Faction);
						break;
					}
			m->Threats.AddRange(r->PortThreats);
			m->PirateShips = r->PirateShips;
			m->Havens = r->Havens;
			if (log != nullptr)
				for (const auto& c : *log)
					if (c->Purpose == VoyagePurpose::Raid && c->Jd <= r->AsOfJd && c->Jd > r->AsOfJd - FlowWindowDays && c->Link->Other(c->From) == m->System)
						m->Raids++;
			int score = m->PirateShips + 4 * m->Havens + 3 * m->Raids;
			m->Pirates = score == 0 ? PirateActivity::None : score < 8 ? PirateActivity::Low : PirateActivity::High;
		}

		if (galaxy->Network != nullptr)
			for (const auto& link : galaxy->Network->Links())
				model->Links.Add(LinkOf(model, link, log));
		for (const auto& w : works)
		{
			if ((w ? w->A : nullptr) == nullptr || w->B == nullptr || model->Between(w->A, w->B) != nullptr) continue;
			auto a = model->Of(w->A);
			auto b = model->Of(w->B);
			// A gate being built is news of its two ends: shown once either is known.
			if (a == nullptr || b == nullptr || (!a->Known() && !b->Known())) continue;
			auto l = cs::New<MapLink>();
			l->A = w->A; l->B = w->B; l->UnderConstruction = true; l->Progress = cs::Math::Max(0.0, cs::Math::Min(1.0, w->Progress)); l->Builder = w->Builder;
			l->KnownAsOfJd = cs::Math::Max(a->Known() ? a->Report->AsOfJd : cs::Double::NegativeInfinity, b->Known() ? b->Report->AsOfJd : cs::Double::NegativeInfinity);
			model->Links.Add(l);
		}

		for (const auto& l : model->Links) model->BusiestCrossings = cs::Math::Max(model->BusiestCrossings, l->Crossings);
		for (const auto& l : model->Links) l->Flow = model->BusiestCrossings > 0 ? static_cast<double>(l->Crossings) / model->BusiestCrossings : 0;
		return model;
	}

	cs::Ref<MapLink> GalaxyMap::LinkOf(const cs::Ref<GalaxyMapModel>& model, const cs::Ref<GateLink>& link, const cs::List<cs::Ref<GateCrossing>>* log)
	{
		auto l = cs::New<MapLink>();
		l->Link = link;
		l->A = link->A;
		l->B = link->B;
		auto a = model->Of(link->A);
		auto b = model->Of(link->B);
		double asOf = cs::Double::NegativeInfinity;
		if (a != nullptr && a->Known())
		{
			asOf = cs::Math::Max(asOf, a->Report->AsOfJd);
			Gate(a->Report, link->AtA, model->Jd, l->ShutAtA, l->DownAtA, l->OwnerA);
		}
		if (b != nullptr && b->Known())
		{
			asOf = cs::Math::Max(asOf, b->Report->AsOfJd);
			Gate(b->Report, link->AtB, model->Jd, l->ShutAtB, l->DownAtB, l->OwnerB);
		}
		if (cs::Double::IsNegativeInfinity(asOf)) return l;
		l->KnownAsOfJd = asOf;

		if (log != nullptr)
			for (const auto& c : *log)
			{
				if (c->Link != link || c->Jd > asOf || c->Jd <= asOf - FlowWindowDays) continue;
				l->Crossings++;
				if (c->From == link->A) l->TowardB++;
				else l->TowardA++;
				l->Tonnes += c->Tonnes;
				switch (c->Purpose)
				{
					case VoyagePurpose::Trade: l->Trade++; break;
					case VoyagePurpose::Courier: l->Couriers++; break;
					case VoyagePurpose::Raid: l->Raids++; break;
					case VoyagePurpose::Expedition: l->Navy++; break;
					case VoyagePurpose::Survey: l->Surveys++; break;
					default: break;
				}
			}

		auto ha = a ? a->Holder : nullptr;
		auto hb = b ? b->Holder : nullptr;
		if (model->AtWar(ha, hb))
		{
			l->WarFront = true;
			l->Front = FactionPair(ha, hb);
		}
		return l;
	}

	void GalaxyMap::Gate(const cs::Ref<SystemReport>& r, const cs::Ref<JumpGate>& gate, double jd, bool& shut, bool& down, cs::Ref<Faction>& owner)
	{
		shut = down = false;
		owner = nullptr;
		for (const auto& g : r->Gates)
			if (g.Gate == gate)
			{
				down = g.Down();
				shut = g.Shut(jd) || down;
				owner = g.Owner;
				return;
			}
	}
}
