// US-T135: the EVA repair worker (see RepairWorker.h).

#include "Embodiment/RepairWorker.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/Collisions.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/ShieldEmitters.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	// ------------------------------------------------------------ the weld's timing

	double WeldTiming::ZoneCentre() const
	{
		// The golden-ratio walk: every hit moves the zone somewhere new, the same on every peer.
		double f = 0.5 + Hits * 0.6180339887498949;
		return 0.2 + 0.6 * (f - cs::Math::Floor(f));
	}

	double WeldTiming::ZoneHalf() const { return cs::Math::Max(0.06, 0.13 - 0.01 * cs::Math::Min(Streak, 7)); }

	void WeldTiming::Step(double dt, bool primary)
	{
		bool press = primary && !PrimaryWas;
		PrimaryWas = primary;
		dt = cs::Math::Max(0.0, dt);
		Phase += dt * SweepHz * 2;
		Phase -= 2 * cs::Math::Floor(Phase / 2);
		Stall = cs::Math::Max(0.0, Stall - dt);
		VerdictS = cs::Math::Max(0.0, VerdictS - dt);
		Quality = cs::Math::Max(0.0, Quality - QualityDecayS * dt);
		if (!press) return;
		VerdictS = 0.8;
		if (Stall <= 0 && cs::Math::Abs(Marker() - ZoneCentre()) <= ZoneHalf())
		{
			Hits++;
			Streak++;
			Quality = cs::Math::Min(1.0, Quality + QualityPerHit);
			VerdictGood = true;
			return;
		}
		Misses++;
		Streak = 0;
		Quality *= 0.5;
		Stall = StallS;
		VerdictGood = false;
	}

	double WeldTiming::Multiplier() const { return Stall > 0 ? 1.0 : 1.0 + FlatBonus + SkillBonus * Quality; }

	// ------------------------------------------------------------ the worker

	RepairWorker::RepairWorker(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& unit, int team, const cs::Ref<IEmbodiable>& host)
	{
		Battle = battle;
		Unit = unit;
		Team = team;
		this->host = host;
	}

	int RepairWorker::TeamsOn(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr || u->Combat == nullptr) return 0;
		bool big = Broadsides::Commands(u) || u->Kind == UnitKind::Platform;
		if (!big) return 0;
		return cs::Math::Max(1, Broadsides::Teams(u));
	}

	bool RepairWorker::Repairable(const cs::Ref<Subsystem>& s)
	{
		if (s == nullptr) return false;
		if (ShieldEmitters::Restorable(s)) return true;
		return !s->Destroyed() && s->Hp < s->Def->MaxHp * Broadsides::FieldRepairCap - 1e-9;
	}

	double RepairWorker::TeamHpS(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr || u->Combat == nullptr) return 0;
		return Broadsides::RepairHpPerTeamS * Broadsides::DamageControlRate()->Value * u->Combat->Readiness;
	}

	DVec3 RepairWorker::SurfaceOf(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s)
	{
		auto at = DVec3(s->Def->X, s->Def->Y, s->Def->Z);
		if (u->Combat == nullptr || u->Combat->Spec == nullptr) return at;
		// A hardpoint on the plating is reached where it is; one buried inside, at the plating over it.
		return ShieldEmitters::StrikePoint(u->Combat->Spec, at.LengthSquared() < 1e-6 ? DVec3(0, 1, 0) * 0.5 : at);
	}

	cs::Ref<Subsystem> RepairWorker::NearestJob(const cs::Ref<BattleUnit>& u, DVec3 local, double& lengths)
	{
		lengths = 0;
		if (u == nullptr || u->Combat == nullptr) return nullptr;
		cs::Ref<Subsystem> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& s : u->Combat->Subsystems)
		{
			if (!Repairable(s)) continue;
			double d = (SurfaceOf(u, s) - local).Length();
			if (d < bestD)
			{
				bestD = d;
				best = s;
			}
		}
		if (best != nullptr) lengths = bestD;
		return best;
	}

	double RepairWorker::JobProgress(const cs::Ref<Subsystem>& s)
	{
		if (s == nullptr) return 0;
		double max = s->Def->MaxHp;
		if (max <= 0) return 1;
		if (s->Destroyed()) return cs::Math::Clamp(s->Restoring / cs::Math::Max(1e-9, ShieldEmitters::EmergencyRepairWork()->Value * max), 0.0, 1.0);
		return cs::Math::Clamp(s->Hp / (max * Broadsides::FieldRepairCap), 0.0, 1.0);
	}

	cs::String RepairWorker::Name() const { return cs::Cat(Unit->Name(), " EVA worker ", Team + 1); }

	cs::Ref<Core::Faction> RepairWorker::Faction() const { return Unit->Faction; }

	cs::String RepairWorker::Blocker() const
	{
		if (Battle == nullptr || Battle->Over) return "the fight is over";
		if (Unit == nullptr || !Unit->Present()) return "gone";
		if (Unit->Captured) return "taken as a prize";
		if (Team < 0 || Team >= TeamsOn(Unit)) return "no damage control team to send out";
		return cs::String();
	}

	bool RepairWorker::Embodied() const { return Unit != nullptr && Team >= 0 && Team < 31 && ((Unit->EvaHeldTeams >> Team) & 1) != 0; }

	EmbodimentPose RepairWorker::PoseAt(double /*jd*/)
	{
		auto n = Local.LengthSquared() > 1e-12 ? Local.Normalized() : DVec3(0, 1, 0);
		return EmbodimentPose(Battle->Body, Unit->PointLocal(Local) + Battle->Anchor, Unit->LocalDirToWorld(Facing).Normalized(), Unit->LocalDirToWorld(n).Normalized());
	}

	void RepairWorker::Place()
	{
		int teams = cs::Math::Max(1, TeamsOn(Unit));
		double along = teams > 1 ? 0.3 * (Team / static_cast<double>(teams - 1) - 0.5) : 0;
		auto spec = Unit->Combat != nullptr ? Unit->Combat->Spec : nullptr;
		auto dir = DVec3(0, 1, along);
		Local = spec != nullptr ? Collisions::SurfacePoint(spec, dir) : dir * 0.5;
		double d = 0;
		Nearest = NearestJob(Unit, Local, d);
		NearestLengths = d;
		// Facing the nearest work, so the first look is at it.
		if (Nearest != nullptr)
		{
			auto n = Local.Normalized();
			auto to = SurfaceOf(Unit, Nearest) - Local;
			to = to - n * DVec3::Dot(to, n);
			if (to.LengthSquared() > 1e-12) Facing = to.Normalized();
		}
		placed = true;
	}

	void RepairWorker::TakeCharge()
	{
		if (Team >= 0 && Team < 31) Unit->EvaHeldTeams |= (1 << Team);
		if (!placed) Place();
		Weld.Reset();
	}

	void RepairWorker::Control(const EmbodimentInput& input, double dt)
	{
		if (Unit == nullptr || Unit->Combat == nullptr) return;
		dt = cs::Math::Max(0.0, dt);
		auto spec = Unit->Combat->Spec;
		// Crawl: the stick moves it over the plating, forward where the player looks, kept on the hull.
		auto n = Local.LengthSquared() > 1e-12 ? Local.Normalized() : DVec3(0, 1, 0);
		if (input.Aim.LengthSquared() > 1e-12)
		{
			auto look = Unit->DirToLocal(input.Aim.Normalized());
			auto along = look - n * DVec3::Dot(look, n);
			if (along.LengthSquared() > 1e-9) Facing = along.Normalized();
		}
		auto right = DVec3::Cross(Facing, n); // the battle's own handedness (BattleUnit::Basis: right = forward x roof)
		auto stick = Facing * input.Throttle + right * input.StrafeRight;
		if (stick.LengthSquared() > 1) stick = stick.Normalized();
		if (stick.LengthSquared() > 1e-12 && dt > 0)
		{
			double speed = CrawlLengthsS * (input.Boost ? JetFactor : 1.0);
			auto next = Local + stick * (speed * dt);
			Local = spec != nullptr ? Collisions::SurfacePoint(spec, next) : next;
		}
		double d = 0;
		Nearest = NearestJob(Unit, Local, d);
		NearestLengths = d;
		Job = Nearest != nullptr && d <= ReachLengths ? Nearest : nullptr;
		// The torch: its timing is judged only with work under it; the repair runs at the team's rate times the torch's.
		Weld.Step(dt, input.Primary && Job != nullptr);
		if (Job != nullptr && dt > 0) Put(TeamHpS(Unit) * Weld.Multiplier() * dt);
	}

	void RepairWorker::Put(double work)
	{
		if (work <= 0 || Job == nullptr) return;
		auto s = Job;
		auto cmd = Unit->Broadside;
		if (s->Destroyed())
		{
			if (!ShieldEmitters::Restorable(s)) return;
			double rate = ShieldEmitters::EmergencyRepairRate()->Value;
			if (rate <= 0) return;
			double need = ShieldEmitters::EmergencyRepairWork()->Value * s->Def->MaxHp;
			double put = cs::Math::Min(work * rate, cs::Math::Max(0.0, need - s->Restoring));
			s->Restoring += put;
			Welded += put;
			if (s->Restoring < need - 1e-9) return;
			s->Hp = s->Def->MaxHp * ShieldEmitters::RestoredHealth()->Value;
			s->Restoring = 0;
			s->Shield = 0;
			Restored++;
			if (cmd != nullptr) cmd->Restored++;
			return;
		}
		double fix = cs::Math::Min(work, cs::Math::Max(0.0, s->Def->MaxHp * Broadsides::FieldRepairCap - s->Hp));
		s->Hp += fix;
		Welded += fix;
		if (cmd != nullptr) cmd->Repaired += fix;
	}

	void RepairWorker::HandBack()
	{
		if (Unit != nullptr && Team >= 0 && Team < 31) Unit->EvaHeldTeams &= ~(1 << Team);
		Job = nullptr;
		Weld.Reset();
	}

	EmbodimentCameraRig RepairWorker::CameraRig() const { return EmbodimentCameraRig(EmbodimentCameraKind::Shoulder, 2.5, 0.05, 0.07); }

	EmbodimentRolePanel RepairWorker::RolePanel()
	{
		EmbodimentRolePanel p;
		p.Title = "EVA REPAIR";
		p.Verb = "Working";
		auto partName = [](const cs::Ref<Subsystem>& s)
		{
			return cs::Cat(cs::Strings::ToLowerInvariant(SubsystemDef::Name(s->Kind())), " ", s->Index + 1);
		};
		if (Job != nullptr)
			p.Status = cs::Cat("Welding the ", partName(Job), Job->Destroyed() ? " (shot away)" : "", "  ", cs::Fmt(JobProgress(Job) * 100, "0"), "%  \xC2\xB7  x",
				cs::Fmt(Weld.Multiplier(), "0.00"), " the team's rate");
		else if (Nearest != nullptr)
			p.Status = cs::Cat("Nearest work: the ", partName(Nearest), " (", Nearest->Destroyed() ? cs::String("shot away") : cs::Cat(cs::Fmt(Nearest->Health() * 100, "0"), "%"),
				"), ", cs::Fmt(NearestLengths * Unit->LengthKm * 1000, "N0"), " m over the plating: crawl to it");
		else p.Status = "Nothing damaged on the hull: the plating holds";
		p.Abilities.Add(EmbodimentAbility("W/S A/D", "crawl the plating where you look"));
		p.Abilities.Add(EmbodimentAbility("Shift", "suit jets (faster)"));
		p.Abilities.Add(EmbodimentAbility("Left mouse", "weld: tap as the marker crosses the bright zone", Job != nullptr));
		p.Abilities.Add(EmbodimentAbility("Shift+P", "next worker on this hull"));
		p.Abilities.Add(EmbodimentAbility("Backspace", host != nullptr ? cs::Cat("back inside: ", host->Name()) : cs::String("back inside"), host != nullptr));
		p.Abilities.Add(EmbodimentAbility("' ;", "next seat in the fight"));
		p.Abilities.Add(EmbodimentAbility("P", "back to the free SpaceGod view"));
		if (Job != nullptr)
		{
			p.Meter.Show = true;
			p.Meter.Label = "WELD";
			p.Meter.Marker = Weld.Marker();
			p.Meter.ZoneLo = Weld.ZoneCentre() - Weld.ZoneHalf();
			p.Meter.ZoneHi = Weld.ZoneCentre() + Weld.ZoneHalf();
			p.Meter.Progress = JobProgress(Job);
			p.Meter.Quality = Weld.Quality;
			if (Weld.VerdictS > 0) p.Meter.Verdict = Weld.VerdictGood ? cs::Cat("GOOD WELD x", Weld.Streak) : cs::String("SPUTTER");
			p.Meter.VerdictGood = Weld.VerdictGood;
		}
		return p;
	}

	EmbodimentSubject RepairWorker::Subject() const
	{
		EmbodimentSubject s;
		s.Battle = Battle;
		s.Unit = Unit;
		s.Ship = Unit != nullptr ? Unit->Ship : nullptr;
		s.Part = Job != nullptr ? Job : Nearest;
		s.HasPoint = Unit != nullptr;
		if (Unit != nullptr) s.PointKm = Unit->PointLocal(Local);
		return s;
	}

	bool RepairWorker::Equals(const cs::Ref<IEmbodiable>& obj) const
	{
		auto w = cs::As<RepairWorker>(obj);
		return w && w->Unit == Unit && w->Team == Team && w->Battle == Battle;
	}

	size_t RepairWorker::GetHashCode() const { return cs::Hash<cs::Ref<BattleUnit>>{}(Unit) * 31 + static_cast<size_t>(Team); }
}
