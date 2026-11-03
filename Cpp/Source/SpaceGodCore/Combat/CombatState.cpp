// Port of Unity.SpaceGod Core/Combat/CombatState.cs (US-T10).

#include "Combat/CombatState.h"

#include "Combat/Boarding.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/ModularHull.h"
#include "Combat/ShieldEmitters.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	// ==================================================================== Subsystem

	Subsystem::Subsystem(const cs::Ref<SubsystemDef>& def, int index)
		: Def(def), Index(index)
	{
		Hp = def->MaxHp;
		Ammo = def->Weapon ? def->Weapon->Ammo : 0;
	}

	SubsystemKind Subsystem::Kind() const { return Def->Kind; }

	double Subsystem::Health() const { return cs::Math::Max(0.0, Hp / Def->MaxHp); }

	double Subsystem::ShieldFraction() const
	{
		return Kind() == SubsystemKind::ShieldGenerator && !Destroyed() && MaxShield > 0 && Shield > 0
			? cs::Math::Min(1.0, Shield / MaxShield) : 0;
	}

	// ==================================================================== ShipCombat

	void ShipCombat::SetFighters(int value)
	{
		if (value < Hangar->Count()) CarrierWings::Trim(shared_from_this(), value);
		else if (value > Hangar->Count()) CarrierWings::Fill(shared_from_this(), cs::Math::Min(Spec->HangarCraft(), value) - Hangar->Count());
	}

	void ShipCombat::SetShuttles(int value)
	{
		if (value < ShuttleBay->Count()) CarrierWings::TrimShuttles(shared_from_this(), value);
		else if (value > ShuttleBay->Count()) CarrierWings::FillShuttles(shared_from_this(), cs::Math::Min(Spec->Shuttles, value) - ShuttleBay->Count());
	}

	cs::Ref<ModularHull> ShipCombat::Structure()
	{
		if (!structure) structure = ModularHull::For(Spec);
		return structure;
	}

	ShipCombat::ShipCombat(const cs::Ref<CombatSpec>& spec, cs::Nullable<ShipClass> hull) : ShipCombat(spec, hull, nullptr) {}

	ShipCombat::ShipCombat(const cs::Ref<CombatSpec>& spec, cs::Nullable<ShipClass> hull, const cs::Ref<Core::Interior>& interior)
		: Spec(spec)
	{
		Hull = spec->Hull;
		for (int i = 0; i < spec->Layout.Count(); i++) Subsystems.Add(cs::New<Subsystem>(spec->Layout[i], i));
		double ratings = GeneratorRatings();
		if (ratings > 0)
			for (const auto& s : Subsystems)
				if (s->Kind() == SubsystemKind::ShieldGenerator) s->MaxShield = s->Shield = spec->Shield * s->Def->Rating / ratings;
		Interior = interior ? interior : (hull.has_value() ? Core::Interior::For(*hull, spec->Crew, spec->Marines) : Core::Interior::ForPlatform(spec->Crew, spec->Marines));
		// Nothing owns the hull yet, so shared_from_this() is not available: CarrierWings gets a non-owning alias of `this`,
		// which it uses only for the call (it keeps no reference to the hull).
		cs::Ref<ShipCombat> self(cs::Ref<ShipCombat>(), this);
		CarrierWings::Fill(self, spec->HangarCraft());
		CarrierWings::FillShuttles(self, spec->Shuttles);
	}

	cs::Ref<ShipCombat> ShipCombat::Refitted(const cs::Ref<CombatSpec>& spec, cs::Nullable<ShipClass> hull, int changed, const cs::Ref<Core::Interior>& interior)
	{
		auto c = cs::New<ShipCombat>(spec, hull, interior ? interior : Interior);
		c->Hull = spec->Hull * HullFraction();
		c->BoostFraction = BoostFraction;
		c->Readiness = Readiness;
		c->PhaserTuning = PhaserTuning;
		// The same craft stay aboard, as many as the refitted hangars hold (U.SPC-T526).
		c->Hangar = Hangar;
		// A refit changes what is mounted, not the hull's frame: its structure and any strain in it carry over (U.SPC-T596).
		if (structure != nullptr && spec->Layout.Count() == Spec->Layout.Count() && spec->LengthKm == Spec->LengthKm) c->structure = structure;
		CarrierWings::Trim(c, spec->HangarCraft());
		// And the same shuttles, as many as its shuttle berths hold (U.SPC-T590).
		c->ShuttleBay = ShuttleBay;
		CarrierWings::TrimShuttles(c, spec->Shuttles);
		for (int i = 0; i < c->Subsystems.Count() && i < Subsystems.Count(); i++)
		{
			if (i == changed) continue;
			auto was = Subsystems[i];
			auto now = c->Subsystems[i];
			if (was->Kind() != now->Kind()) continue;
			now->Hp = now->Def->MaxHp * (was->Hp <= 0 ? 0 : was->Health());
			now->Ammo = cs::Math::Min(now->Ammo, was->Ammo);
			now->Cooldown = was->Cooldown;
			now->LastHitSeconds = was->LastHitSeconds;
			if (now->Kind() == SubsystemKind::ShieldGenerator) now->Shield = was->MaxShield > 0 ? now->MaxShield * cs::Math::Min(1.0, was->Shield / was->MaxShield) : 0;
		}
		return c;
	}

	double ShipCombat::GeneratorRatings() const
	{
		double total = 0;
		for (const auto& s : Subsystems) if (s->Kind() == SubsystemKind::ShieldGenerator) total += s->Def->Rating;
		return total;
	}

	bool ShipCombat::Disabled() const { return Alive() && (EngineFactor() <= 0 || HullFraction() < 0.12 || (!BridgeIntact() && HullFraction() < 0.4)); }

	double ShipCombat::MarinesAvailable() const { return Interior->UnderBoarding() ? 0 : Interior->Marines(); }

	double ShipCombat::HullFraction() const { return cs::Math::Max(0.0, Hull / Spec->Hull); }

	bool ShipCombat::HasShieldGenerator() const
	{
		for (const auto& s : Subsystems)
			if (s->Kind() == SubsystemKind::ShieldGenerator && !s->Destroyed()) return true;
		return false;
	}

	double ShipCombat::Shield() const
	{
		double total = 0;
		for (const auto& s : Subsystems) if (s->Kind() == SubsystemKind::ShieldGenerator) total += s->Shield;
		return total;
	}

	cs::Ref<Subsystem> ShipCombat::PatchAt(DVec3 local) const
	{
		cs::Ref<Subsystem> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& s : Subsystems)
		{
			if (s->Kind() != SubsystemKind::ShieldGenerator) continue;
			double dx = s->Def->X - local.X, dy = s->Def->Y - local.Y, dz = s->Def->Z - local.Z;
			double d = dx * dx + dy * dy + dz * dz;
			if (d < bestD)
			{
				bestD = d;
				best = s;
			}
		}
		return best;
	}

	cs::Ref<Subsystem> ShipCombat::EmitterFor(DVec3 local) const
	{
		auto s = PatchAt(local);
		return s != nullptr && !s->Destroyed() ? s : nullptr;
	}

	void ShipCombat::ZeroAllShields()
	{
		for (const auto& s : Subsystems) if (s->Kind() == SubsystemKind::ShieldGenerator) s->Shield = 0;
	}

	void ShipCombat::ResetShieldTimers()
	{
		for (const auto& s : Subsystems) if (s->Kind() == SubsystemKind::ShieldGenerator) s->LastHitSeconds = -999;
	}

	void ShipCombat::RegenShields(double nowSeconds, double dt)
	{
		double ratings = GeneratorRatings();
		if (ratings <= 0) return;
		double delay = ShieldEmitters::RegenDelay()->Value, rate = Spec->ShieldRegen * ShieldEmitters::RegenRate()->Value;
		if (rate <= 0) return;
		for (const auto& s : Subsystems)
		{
			if (s->Kind() != SubsystemKind::ShieldGenerator || s->Destroyed()) continue;
			if (nowSeconds - s->LastHitSeconds > delay && s->Shield < s->MaxShield)
			{
				double add = rate * s->Def->Rating / ratings * dt;
				s->Shield = add >= s->MaxShield - s->Shield ? s->MaxShield : s->Shield + add;
			}
		}
	}

	bool ShipCombat::Armed() const
	{
		// Mines, sentry turrets and EMP rounds are a hull's means of getting away, not of fighting (U.SPC-T271).
		for (const auto& s : Subsystems)
			if (s->Def->IsWeapon() && !s->Destroyed() && !s->Def->Weapon->AntiSmall && (!s->Def->Weapon->Missile || s->Ammo > 0) && !s->Def->Weapon->Supporting()) return true;
		return false;
	}

	double ShipCombat::EngineFactor() const
	{
		int total = 0, alive = 0;
		for (const auto& s : Subsystems)
		{
			if (s->Kind() != SubsystemKind::Engine) continue;
			total++;
			if (!s->Destroyed()) alive++;
		}
		return total == 0 ? 1 : static_cast<double>(alive) / total;
	}

	bool ShipCombat::BridgeIntact() const
	{
		for (const auto& s : Subsystems)
			if (s->Kind() == SubsystemKind::Bridge) return !s->Destroyed();
		return true;
	}

	double ShipCombat::HullDamage() const { return cs::Math::Min(1.0, 1 - HullFraction()); }

	double ShipCombat::Damage() const
	{
		double missing = 1 - HullFraction();
		for (const auto& s : Subsystems) missing += (1 - s->Health()) * 0.1;
		return cs::Math::Min(1.0, missing / (1 + Subsystems.Count() * 0.1));
	}

	double ShipCombat::Strength() const
	{
		if (!Alive()) return 0;
		double dps = 0;
		for (const auto& s : Subsystems)
		{
			auto w = s->Def->Weapon;
			if (w == nullptr || s->Destroyed() || w->Supporting()) continue;
			dps += w->Damage * w->Salvo / w->Cooldown * (w->AntiSmall ? 0.3 : 1);
		}
		dps += Fighters() * 3;
		return dps * (0.3 + 0.7 * HullFraction()) * (Spec->Hull + Spec->Shield) / 1000;
	}

	void ShipCombat::Repair(double share)
	{
		Hull = cs::Math::Min(Spec->Hull, Hull + Spec->Hull * share);
		for (const auto& s : Subsystems)
		{
			s->Hp = cs::Math::Min(s->Def->MaxHp, cs::Math::Max(s->Hp, 0.0) + s->Def->MaxHp * share * 1.5);
			if (!s->Destroyed()) s->Restoring = 0;
			if (s->Kind() == SubsystemKind::ShieldGenerator) s->Shield = s->MaxShield;
		}
		// A full repair rebuilds the hull's structure whole, pieces lost included; a partial one mends what is still on it (U.SPC-T596).
		if (share >= 1) structure = nullptr;
		else if (structure) structure->Repair(share);
	}

	void ShipCombat::FreeResupply()
	{
		for (const auto& s : Subsystems)
			if (s->Def->Weapon != nullptr) s->Ammo = s->Def->Weapon->Ammo;
		Interior->Muster(Spec->Crew, Spec->Marines);
	}

	void ShipCombat::RestoreFighters(int count) { CarrierWings::Fill(shared_from_this(), count); }

	void ShipCombat::RestoreShuttles(int count) { CarrierWings::FillShuttles(shared_from_this(), count); }
}
