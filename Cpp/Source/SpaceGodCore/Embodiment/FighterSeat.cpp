// Port of Unity.SpaceGod Core/Embodiment/FighterSeat.cs (US-T21, see docs/MIGRATION.md).

#include "Embodiment/FighterSeat.h"
#include "Astro.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Combat/FlightDecks.h"

namespace SpaceGod::Core
{
	FighterSeat::FighterSeat(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& unit)
	{
		Battle = battle;
		Unit = unit;
	}

	cs::String FighterSeat::Craft() const
	{
		auto f = Unit->Fighter;
		if (f == nullptr) return "craft";
		return f->Bomber ? cs::String("bomber") : FighterCraft::Of(f->Role)->Name;
	}

	cs::String FighterSeat::Name() const
	{
		auto f = Unit->Fighter;
		if (f == nullptr) return Unit->Name();
		// A boarding shuttle flies in no squadron (U.SPC-T486).
		if (f->Squadron < 0) return f->Carrier != nullptr ? cs::Cat(f->Carrier->Name(), " ", Craft()) : Craft();
		return f->Carrier != nullptr ? cs::Cat(f->Carrier->Name(), " ", Craft(), " ", f->Squadron + 1, "-", f->Slot + 1) : Craft();
	}

	cs::Ref<Core::Faction> FighterSeat::Faction() const { return Unit->Faction; }

	bool FighterSeat::Embodied() const { return Battle->Flown.ContainsKey(Unit); }

	cs::String FighterSeat::Blocker() const
	{
		if (Unit->Kind != UnitKind::Fighter || Unit->Fighter == nullptr) return "not a launched craft";
		if (Battle->Over) return "the fight is over";
		if (Unit->Dead) return "shot down";
		// Down on a flight deck being turned round to launch again, it is still the player's (U.SPC-T518).
		if (Unit->Docked && !FlightDecks::TurningRound(Unit)) return "back aboard its carrier";
		if (!Unit->Present()) return "gone";
		if (Unit->Captured) return "taken as a prize";
		return cs::String();
	}

	EmbodimentPose FighterSeat::PoseAt(double /*jd*/)
	{
		cs::Ref<FighterControl> stick;
		auto up = Battle->Flown.TryGetValue(Unit, stick) && stick->Up.LengthSquared() > 1e-12 ? stick->Up : UpOf(Unit);
		return EmbodimentPose(Battle->Body, Unit->Pos + Battle->Anchor, Unit->Forward, up);
	}

	DVec3 FighterSeat::UpOf(const cs::Ref<BattleUnit>& u)
	{
		DVec3 unusedRight, up;
		u->Basis(unusedRight, up);
		return up;
	}

	void FighterSeat::TakeCharge()
	{
		double top = Unit->Combat->Spec->MaxSpeed;
		auto groups = WeaponGroups::Secondaries(Unit);
		auto control = cs::New<FighterControl>();
		control->Aim = Unit->Forward;
		control->Throttle = top > 0 ? cs::Math::Min(1.0, Unit->Vel.Length() / top) : 0;
		control->Up = UpOf(Unit);
		control->SecondaryGroup = groups.Count() > 0 ? groups[0] : WeaponGroup::Missiles;
		Battle->Flown[Unit] = control;
		Unit->SetPlayerControlled(true);
		Unit->PlayerFireHeld = false;
		Unit->PlayerSecondaryHeld = false;
		Unit->PlayerLockTarget = nullptr;
		Unit->PlayerSubTarget = nullptr;
		Unit->LockProgress = 0;
		Unit->PlayerAim = Unit->Forward;
		if (Unit->Power == nullptr) Unit->Power = cs::New<PowerDistribution>();
		// Its pilot's share of the reactor was for its pilot's fight; the player starts even.
		Unit->Power->Balance();
	}

	void FighterSeat::Control(const EmbodimentInput& input, double dt)
	{
		cs::Ref<FighterControl> stick;
		if (!Battle->Flown.TryGetValue(Unit, stick)) return;
		if (input.Aim.LengthSquared() > 1e-12) stick->Aim = input.Aim.Normalized();
		stick->Throttle = cs::Math::Max(0.0, cs::Math::Min(1.0, stick->Throttle + input.Throttle * ThrottlePerSecond * dt));
		stick->StrafeRight = input.StrafeRight;
		stick->StrafeUp = input.StrafeUp;
		stick->Roll = input.Roll;
		stick->Boost = input.Boost;
		stick->Secondary = input.Secondary;
		Unit->PlayerFireHeld = input.Primary;
		Unit->PlayerAim = stick->Aim;
	}

	void FighterSeat::HandBack()
	{
		Battle->Flown.Remove(Unit);
		Unit->SetPlayerControlled(false);
		Unit->PlayerFireHeld = false;
		Unit->PlayerSecondaryHeld = false;
		Unit->PlayerDecoyRequested = false;
		Unit->PlayerLockTarget = nullptr;
		Unit->PlayerSubTarget = nullptr;
		Unit->LockProgress = 0;
		auto f = Unit->Fighter;
		if (f == nullptr) return;
		// Nothing snaps back to a stale run: a fresh approach from where it is, on a target it picks now.
		if (f->Phase != FighterPhase::Return)
		{
			f->Phase = FighterPhase::Approach;
			f->PhaseTime = 0;
		}
		f->RunEntry = DVec3::Zero;
		Unit->Target = nullptr;
	}

	cs::Ref<FighterControl> FighterSeat::Stick() const
	{
		cs::Ref<FighterControl> s;
		return Battle->Flown.TryGetValue(Unit, s) ? s : nullptr;
	}

	// ------------------------------------------------------------ the dogfight game (U.SPC-T424)

	cs::Nullable<WeaponGroup> FighterSeat::SecondaryGroup() const
	{
		auto stick = Stick();
		return stick ? cs::Nullable<WeaponGroup>(stick->SecondaryGroup) : std::nullopt;
	}

	cs::Nullable<WeaponGroup> FighterSeat::CycleSecondaryGroup()
	{
		auto stick = Stick();
		auto groups = WeaponGroups::Secondaries(Unit);
		if (stick == nullptr || groups.Count() == 0) return std::nullopt;
		int i = groups.IndexOf(stick->SecondaryGroup);
		stick->SecondaryGroup = groups[(i + 1) % groups.Count()];
		return stick->SecondaryGroup;
	}

	namespace
	{
		// C#: the tuple (BattleUnit unit, double angle).
		struct LockCandidate
		{
			cs::Ref<BattleUnit> unit;
			double angle = 0;
		};
	}

	cs::Ref<BattleUnit> FighterSeat::CycleLock(DVec3 aim)
	{
		if (Stick() == nullptr) return nullptr;
		auto look = aim.LengthSquared() > 1e-12 ? aim.Normalized() : Unit->Forward;
		cs::List<LockCandidate> candidates;
		for (const auto& o : Battle->Units)
		{
			if (!Battle->Hostile(Unit, o) || o->Surrendering || o->Captured) continue;
			auto to = o->Pos - Unit->Pos;
			double d = to.Length();
			if (d < 1e-9 || d > LockPickKm) continue;
			double angle = cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(look, to / d)))) * Astro::Rad2Deg;
			if (angle <= LockPickDeg) candidates.Add(LockCandidate{ o, angle });
		}
		candidates.Sort([](const LockCandidate& a, const LockCandidate& b) { return cs::Compare(a.angle, b.angle); });
		cs::Ref<BattleUnit> pick = nullptr;
		if (candidates.Count() > 0)
		{
			auto lockTarget = Unit->PlayerLockTarget;
			int i = candidates.FindIndex([&](const LockCandidate& c) { return c.unit == lockTarget; });
			pick = candidates[i < 0 ? 0 : (i + 1) % candidates.Count()].unit;
		}
		Lock(pick);
		return pick;
	}

	void FighterSeat::Lock(const cs::Ref<BattleUnit>& target)
	{
		if (Unit->PlayerLockTarget != target) Unit->LockProgress = 0;
		Unit->PlayerLockTarget = target;
		Unit->PlayerSubTarget = target != nullptr ? WeakPoints::Preferred(target) : nullptr;
	}

	cs::Ref<Subsystem> FighterSeat::CycleWeakPoint(int step)
	{
		auto t = Unit->PlayerLockTarget;
		if (t == nullptr || !t->Present()) return nullptr;
		Unit->PlayerSubTarget = WeakPoints::Cycle(t, Unit->PlayerSubTarget, step);
		return Unit->PlayerSubTarget;
	}

	void FighterSeat::DropCountermeasures() { Unit->PlayerDecoyRequested = true; }

	int FighterSeat::Countermeasures() const
	{
		int n = 0;
		for (const auto& s : Unit->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::Countermeasures && !s->Destroyed()) n += s->Ammo;
		return n;
	}

	bool FighterSeat::ToggleFlightAssist()
	{
		auto stick = Stick();
		if (stick == nullptr) return false;
		stick->FlightAssistOff = !stick->FlightAssistOff;
		return stick->FlightAssistOff;
	}

	bool FighterSeat::FlightAssistOff() const
	{
		auto stick = Stick();
		return stick ? stick->FlightAssistOff : false;
	}

	bool FighterSeat::ShiftPower(PowerSystem s) { return Unit->Power != nullptr && Unit->Power->Shift(s); }

	void FighterSeat::BalancePower()
	{
		if (Unit->Power) Unit->Power->Balance();
	}

	int FighterSeat::OrderWing(WingOrder order) { return Wing::Command(Battle, Unit, order); }

	int FighterSeat::Wingmen() const { return Wing::Members(Battle, Unit).Count(); }

	bool FighterSeat::MissileWarning() const
	{
		// C#: global::Unity.SpaceGod.Core.Countermeasures.ReachKm (the class, not this seat's Countermeasures).
		return Battle->MissileInbound(Unit, Core::Countermeasures::ReachKm * 1.5) != nullptr;
	}

	cs::String FighterSeat::Readout()
	{
		auto u = Unit;
		auto c = u->Combat;
		auto spec = c->Spec;
		auto stick = Stick();
		cs::StringBuilder sb;
		sb.Append(cs::Strings::ToUpperInvariant(Craft())).Append("  ·  ").Append(Name());
		sb.Append("  ·  hull ").Append(Pct(c->Hull / cs::Math::Max(1.0, spec->Hull)));
		sb.Append("  shields ").Append(Pct(spec->Shield > 0 ? c->Shield() / spec->Shield : 0));
		if (stick != nullptr) sb.Append("  ·  throttle ").Append(Pct(stick->Throttle));
		sb.Append("  boost ").Append(Pct(c->BoostFraction));
		if (stick != nullptr && stick->FlightAssistOff) sb.Append("  ASSIST OFF");
		if (u->Power != nullptr) sb.Append("  ·  power ").Append(u->Power);
		if (stick != nullptr && WeaponGroups::Secondaries(u).Count() > 0)
			sb.Append("  ·  ").Append(WeaponGroups::Label(stick->SecondaryGroup)).Append(' ').Append(WeaponGroups::Ammo(u, stick->SecondaryGroup));
		sb.Append("  flares ").Append(Countermeasures());
		auto t = u->PlayerLockTarget;
		if (t != nullptr && t->Present())
		{
			sb.Append("  ·  lock ").Append(t->Name()).Append(' ').Append(cs::Fmt((t->Pos - u->Pos).Length(), "0.0")).Append(" km ");
			sb.Append(u->LockProgress >= 1 ? cs::String("LOCKED") : Pct(u->LockProgress));
			auto weak = u->PlayerSubTarget;
			if (weak != nullptr && !weak->Destroyed())
				sb.Append(" @ ").Append(WeakPoints::Label(weak)).Append(WeakPoints::ExposedFrom(t, weak, u->Pos) ? " (OPEN)" : " (covered)");
		}
		if (Wingmen() > 0) sb.Append("  ·  wing ").Append(Wingmen()).Append(": ").Append(Wing::Label(u->Fighter->Order));
		if (u->Overheated) sb.Append("  ·  GUNS OVERHEATED");
		if (MissileWarning()) sb.Append("  ·  MISSILE");
		// Landing guidance for a friendly flight deck near (U.SPC-T518).
		auto deck = FlightDecks::Guidance(Battle, u);
		if (!deck.empty()) sb.Append("  ·  ").Append(deck);
		return sb.ToString();
	}

	cs::String FighterSeat::Pct(double f)
	{
		return cs::Cat(cs::Fmt(cs::Math::Round(100 * cs::Math::Max(0.0, cs::Math::Min(1.0, f))), "0"), "%");
	}

	bool FighterSeat::Equals(const cs::Ref<IEmbodiable>& obj) const
	{
		auto s = cs::As<FighterSeat>(obj);
		return s && s->Unit == Unit && s->Battle == Battle;
	}

	size_t FighterSeat::GetHashCode() const { return cs::Hash<cs::Ref<BattleUnit>>{}(Unit); }
}
