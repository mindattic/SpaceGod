// The full arsenal (US-T101): see Arsenal.h.

#include "Combat/Arsenal.h"

#include "Ships.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Outfitting.h"

namespace SpaceGod::Core
{
	bool Arsenal::IsRotary(const cs::Ref<WeaponSpec>& w) { return w != nullptr && w->SpinUpS > 0; }

	bool Arsenal::IsRocket(const cs::Ref<WeaponSpec>& w) { return w != nullptr && w->Missile && w->Unguided; }

	bool Arsenal::IsHive(const cs::Ref<WeaponSpec>& w) { return w != nullptr && w->Missile && w->SplitInto > 0; }

	namespace
	{
		// The reload at a spin: SpinStartFactor times the full rate's cold, the full rate at full spin.
		double Interval(const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w)
		{
			double spin = cs::Math::Max(0.0, cs::Math::Min(1.0, s->Spin));
			return w->Cooldown * (Arsenal::SpinStartFactor + (1 - Arsenal::SpinStartFactor) * spin);
		}
	}

	double Arsenal::FireRotary(const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w, double scale)
	{
		if (!IsRotary(w)) return w->Cooldown * scale;
		double interval = Interval(s, w) * scale;
		s->Spin = cs::Math::Min(1.0, s->Spin + interval / w->SpinUpS);
		return interval;
	}

	void Arsenal::SpinDown(const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w, double dt)
	{
		if (!IsRotary(w) || s->Spin <= 0 || s->Cooldown > -SpinIdleS) return;
		s->Spin = cs::Math::Max(0.0, s->Spin - dt / (w->SpinUpS * SpinDownFactor));
	}

	double Arsenal::RoundsPerShot(const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w, double dt)
	{
		if (!IsRotary(w)) return 1;
		return cs::Math::Max(1.0, dt / cs::Math::Max(1e-6, Interval(s, w)));
	}

	int Arsenal::FitAll(const cs::Ref<Ship>& ship, ModuleKind module)
	{
		if (ship == nullptr || ship->Combat == nullptr) return 0;
		auto l = Outfitting::LoadoutOf(ship);
		if (l == nullptr) return 0;
		auto type = ModuleSpec::Of(module)->Mount;
		int fitted = 0;
		for (int i = 0; i < l->Slots.Count(); i++)
		{
			if (!l->Slots[i]->IsWeapon() || l->Slots[i]->Type != type || l->Fitted[i] == module || !Outfitting::CanFit(l, i, module)) continue;
			l->Fitted[i] = module;
			ship->Combat = ship->Combat->Refitted(l->Build(), l->Class, l->Slots[i]->LayoutIndex);
			fitted++;
		}
		return fitted;
	}
}
