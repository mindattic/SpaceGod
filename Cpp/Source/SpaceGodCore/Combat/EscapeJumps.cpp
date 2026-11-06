// US-T98: stoppable escapes (see Combat/EscapeJumps.h).

#include "Combat/EscapeJumps.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/PilotFlight.h"
#include "Combat/SystemCruise.h"

namespace SpaceGod::Core
{
	double EscapeJumps::SpoolSecondsFor(const cs::Ref<BattleUnit>& u)
	{
		return SpoolFactor * SystemCruise::SpoolSecondsFor(HullDynamics::Of(u->Combat->Spec).MassKt);
	}

	bool EscapeJumps::Spooling(const cs::Ref<BattleUnit>& u) { return u != nullptr && u->JumpNeedS > 0; }

	double EscapeJumps::Fraction(const cs::Ref<BattleUnit>& u)
	{
		return Spooling(u) ? cs::Math::Max(0.0, cs::Math::Min(1.0, u->JumpChargeS / u->JumpNeedS)) : 0;
	}

	double EscapeJumps::DriveHp(const cs::Ref<ShipCombat>& c)
	{
		double hp = 0;
		for (const auto& s : c->Subsystems)
			if (s->Kind() == SubsystemKind::Engine) hp += cs::Math::Max(0.0, s->Hp);
		return hp;
	}

	double EscapeJumps::Disruption(const cs::Ref<BattleUnit>& u)
	{
		if (!Spooling(u)) return 0;
		auto c = u->Combat;
		double d = 0;
		if (c->Spec->Hull > 0) d += cs::Math::Max(0.0, u->JumpHullAt - c->Hull) / (c->Spec->Hull * HullShare);
		double driveMax = 0;
		for (const auto& s : c->Subsystems)
			if (s->Kind() == SubsystemKind::Engine) driveMax += s->Def->MaxHp;
		if (driveMax > 0) d += cs::Math::Max(0.0, u->JumpDriveAt - DriveHp(c)) / (driveMax * DriveShare);
		return d;
	}

	void EscapeJumps::Reset(const cs::Ref<BattleUnit>& u)
	{
		u->JumpChargeS = u->JumpNeedS = u->JumpHullAt = u->JumpDriveAt = 0;
		u->JumpRestartS = 0;
	}

	void EscapeJumps::Abort(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		if (Spooling(u)) b->Emit(FxKind::FtlAbort, u->Pos, u->Pos, u->Radius(), u->Side, u, -1);
		u->JumpChargeS = u->JumpNeedS = u->JumpHullAt = u->JumpDriveAt = 0;
		u->JumpRestartS = RestartS;
	}

	bool EscapeJumps::Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt)
	{
		if (u->Kind != UnitKind::Ship || !u->Fleeing || u->PlayerControlled())
		{
			Reset(u);
			return false;
		}
		// Drives knocked out by an interdiction pulse or an EMP: the charge is lost with them.
		if (u->Stunned > 0)
		{
			if (Spooling(u)) Abort(b, u);
			return false;
		}
		if (u->JumpRestartS > 0)
		{
			u->JumpRestartS = cs::Math::Max(0.0, u->JumpRestartS - dt);
			return false;
		}
		auto c = u->Combat;
		if (!Spooling(u))
		{
			u->JumpNeedS = cs::Math::Max(1.0, SpoolSecondsFor(u));
			u->JumpChargeS = 0;
			u->JumpHullAt = c->Hull;
			u->JumpDriveAt = DriveHp(c);
			b->Emit(FxKind::FtlSpool, u->Pos, u->Pos, 0, u->Side, u, -1);
			return false;
		}
		if (Disruption(u) >= 1)
		{
			Abort(b, u);
			return false;
		}
		// A drive with engines shot away charges the slower.
		int before = static_cast<int>(u->JumpChargeS / PulseS);
		u->JumpChargeS += dt * (0.5 + 0.5 * c->EngineFactor());
		if (u->JumpChargeS >= u->JumpNeedS)
		{
			b->Emit(FxKind::FtlJump, u->Pos, u->Pos + u->Forward * u->LengthKm * 4, u->Radius(), u->Side, u, -1);
			return true;
		}
		if (static_cast<int>(u->JumpChargeS / PulseS) > before)
			b->Emit(FxKind::FtlSpool, u->Pos, u->Pos, Fraction(u), u->Side, u, -1);
		return false;
	}
}
