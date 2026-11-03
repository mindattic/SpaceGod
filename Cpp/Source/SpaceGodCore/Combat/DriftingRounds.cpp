// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/DriftingRounds.cs (see docs/MIGRATION.md).

#include "Combat/DriftingRounds.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/Crews.h"
#include "Combat/Diplomacy.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	DVec3 DriftingRounds::Glance(DVec3 relVel, DVec3 normal)
	{
		if (normal.LengthSquared() < 1e-18) return relVel * -GlanceKeep;
		auto n = normal.Normalized();
		double into = DVec3::Dot(relVel, n);
		auto off = into < 0 ? relVel - n * (2 * into) : relVel;
		return off * GlanceKeep;
	}

	bool DriftingRounds::CanTow(const cs::Ref<Ship>& s)
	{
		return s != nullptr && s->Faction != nullptr && (s->Faction->IsPirate || (s->Spec != nullptr && s->Spec->IsMilitary) || s->Role == ShipRole::Tug);
	}

	cs::String DriftingRounds::NameOf(const cs::Ref<Missile>& m)
	{
		return cs::Cat("Breacher round from the ", (m->Source ? m->Source->Name() : (m->Owner != nullptr ? cs::Cat(m->Owner->Name, " line") : cs::String("fight"))));
	}

	bool DriftingRounds::Manned(const cs::Ref<Missile>& m)
	{
		return m != nullptr && !m->Gone && m->Kind == MissileKind::Breacher && m->Owner != nullptr && m->Marines >= 0.5;
	}

	cs::List<cs::Ref<EscapePod>> DriftingRounds::CastAdrift(const cs::Ref<Battle>& b, const cs::Ref<Crews>& crews, double jd)
	{
		cs::List<cs::Ref<EscapePod>> list;
		if (b == nullptr || crews == nullptr) return list;
		for (const auto& m : b->Missiles)
		{
			if (!Manned(m)) continue;
			auto enemy = m->Side == 0 || m->Side == 1 ? b->SideLeaders[1 - m->Side] : cs::Ref<Faction>();
			auto pod = crews->Adrift(NameOf(m), m->Owner, m->Marines, enemy, b->Body, b->Anchor, m->Pos, m->Vel, b->Site, jd);
			if (pod == nullptr) continue;
			m->Marines = 0;
			m->Gone = true;
			list.Add(pod);
		}
		return list;
	}

	cs::Ref<Faction> DriftingRounds::Buyer(const cs::Ref<Captives>& c, const cs::Ref<Diplomacy>& diplomacy, double& price)
	{
		price = 0;
		auto buyer = c ? c->Buyer : cs::Ref<Faction>();
		if (buyer == nullptr || !c->Marines || buyer->IsPirate || buyer == c->Owners || buyer == c->Captors) return nullptr;
		if (diplomacy != nullptr && !diplomacy->Hostile(buyer, c->Owners)) return nullptr;
		price = c->People * SlavePricePerHead;
		return buyer->Treasury >= price ? buyer : nullptr;
	}
}
