// Port of Assets/UnitySpace/Core/Combat/Wreck.cs (see docs/MIGRATION.md).

#include "Combat/Wreck.h"

#include "Ships.h"
#include "Combat/Battle.h"
#include "Combat/CargoPods.h"
#include "Combat/Collisions.h"
#include "Combat/CombatState.h"
#include "Combat/ModularHull.h"

namespace SpaceGod::Core
{
	void Wreck::BreakUp()
	{
		Pieces.Clear();
		auto combat = Ship ? Ship->Combat : nullptr;
		if (combat == nullptr) return;
		auto hull = combat->Structure();
		auto fragments = hull->BreakUp(Id);
		for (int i = 0; i < fragments.Count(); i++)
		{
			auto piece = cs::New<WreckPiece>();
			piece->Index = i;
			piece->Fragment = fragments[i];
			piece->Scrap.AddRange(HullSalvage::Scrap(hull, fragments[i], combat->Spec, combat));
			Pieces.Add(piece);
		}
	}

	int Wreck::DebrisPieces() const { return cs::ToInt(cs::Math::Clamp(6 + LengthKm * 40, 6.0, 24.0)); }

	double Wreck::DebrisPieceMassKt() const
	{
		return 0.5 * ((Ship ? Ship->Combat : nullptr) != nullptr ? Collisions::MassOf(Ship->Combat->Spec)
		                                                         : 3500 * cs::Math::Pow(cs::Math::Max(0.005, LengthKm), 3)) / DebrisPieces();
	}

	CollisionVolume Wreck::DebrisCollider(int k, double jd, DVec3 centreKm, DVec3 velKmS) const
	{
		auto c = CollisionVolume::Sphere(centreKm + DebrisOffsetKm(k, jd), velKmS, DebrisPieceRadiusKm(jd), DebrisPieceMassKt());
		c.Pinned = true;
		return c;
	}

	double Wreck::DebrisRadiusKm(double jd) const
	{
		double days = cs::Math::Max(0.0, jd - CreatedJd);
		double lengths = 0.5 + (DebrisMaxLengths - 0.5) * (1 - cs::Math::Exp(-days * DebrisSpreadLengthsPerDay / DebrisMaxLengths));
		return LengthKm * lengths;
	}

	double Wreck::Fade(double jd) const
	{
		double left = LifetimeDays - (jd - CreatedJd);
		return cs::Math::Clamp(left / (LifetimeDays * 0.2), 0.0, 1.0);
	}

	DVec3 Wreck::DebrisOffsetKm(int k, double jd) const
	{
		uint32_t h = Hash(static_cast<uint32_t>(Id) * 7919u + static_cast<uint32_t>(k) * 104729u);
		double u = (h & 0xFFFF) / 65535.0, v = ((h >> 16) & 0xFFFF) / 65535.0;
		double z = 2 * u - 1, a = 2 * cs::Math::PI * v, r = cs::Math::Sqrt(1 - z * z);
		double share = 0.35 + 0.65 * ((Hash(h) & 0xFFFF) / 65535.0);
		return DVec3(r * cs::Math::Cos(a), r * cs::Math::Sin(a), z) * (DebrisRadiusKm(jd) * share);
	}

	uint32_t Wreck::Hash(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352du;
		x ^= x >> 15; x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	// ---------------------------------------------------------------- Wrecks

	cs::Ref<Wreck> Wrecks::Add(const cs::Ref<BattleUnit>& unit, int body, DVec3 anchor, double jd, double lifetimeDays, const cs::Ref<LaneSite>& site)
	{
		auto w = cs::New<Wreck>();
		w->Id = nextId++;
		w->Name = unit->Name();
		w->Faction = unit->Faction;
		w->Ship = unit->Ship;
		w->Body = body;
		w->Pos = anchor + unit->Pos;
		w->Anchor = anchor;
		w->Local = unit->Pos;
		w->Site = site;
		w->LengthKm = unit->LengthKm;
		w->CreatedJd = jd;
		w->LifetimeDays = lifetimeDays > 0 ? lifetimeDays : DefaultLifetimeDays;
		if (unit->Ship != nullptr && unit->Ship->HasCargo && unit->Ship->CargoTonnes > 0)
			w->Salvage.Add(SalvageStack(unit->Ship->CargoGood, unit->Ship->CargoTonnes));
		w->BreakUp();
		// US-T95: most of what it carried spills as drifting pods; the rest stays in its hold for latch-and-salvage.
		SpilledPods()->Spill(w, jd);
		Items.Add(w);
		return w;
	}

	cs::Ref<Wreck> Wrecks::Add(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd, double lifetimeDays)
	{
		auto w = cs::New<Wreck>();
		w->Id = nextId++;
		w->Name = ship->Name;
		w->Faction = ship->Faction;
		w->Ship = ship;
		w->Body = body;
		w->Pos = offsetKm;
		w->Anchor = offsetKm;
		w->Local = DVec3::Zero;
		w->LengthKm = ship->LengthKm();
		w->CreatedJd = jd;
		w->LifetimeDays = lifetimeDays > 0 ? lifetimeDays : DefaultLifetimeDays;
		if (ship->HasCargo && ship->CargoTonnes > 0) w->Salvage.Add(SalvageStack(ship->CargoGood, ship->CargoTonnes));
		w->BreakUp();
		// US-T95: most of what it carried spills as drifting pods; the rest stays in its hold for latch-and-salvage.
		SpilledPods()->Spill(w, jd);
		Items.Add(w);
		return w;
	}

	cs::Ref<Wreck> Wrecks::Of(const cs::Ref<Ship>& ship) const
	{
		return Items.Find([&](const cs::Ref<Wreck>& w) { return w->Ship == ship; });
	}

	cs::Ref<CargoPods> Wrecks::SpilledPods()
	{
		if (Pods == nullptr) Pods = cs::New<CargoPods>();
		return Pods;
	}

	void Wrecks::AdvanceTo(double jd)
	{
		if (Pods != nullptr) Pods->AdvanceTo(jd);
		for (int i = Items.Count() - 1; i >= 0; i--)
			if (Items[i]->Expired(jd)) Items.RemoveAt(i);
	}
}
