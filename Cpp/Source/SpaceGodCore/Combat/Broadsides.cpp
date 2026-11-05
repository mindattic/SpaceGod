// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Broadsides.cs (see docs/MIGRATION.md).

#include "Combat/Broadsides.h"

#include "Astro.h"
#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/FireCones.h"
#include "Combat/ShieldEmitters.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(BatteryOrder v)
	{
		switch (v)
		{
			case BatteryOrder::FireAtWill: return "FireAtWill";
			case BatteryOrder::HoldFire: return "HoldFire";
			case BatteryOrder::Volley: return "Volley";
		}
		return "";
	}

	const char* ToString(DamageControlFocus v)
	{
		switch (v)
		{
			case DamageControlFocus::Balanced: return "Balanced";
			case DamageControlFocus::Weapons: return "Weapons";
			case DamageControlFocus::Shields: return "Shields";
			case DamageControlFocus::Engines: return "Engines";
		}
		return "";
	}

	const char* ToString(FleetFormation v)
	{
		switch (v)
		{
			case FleetFormation::None: return "None";
			case FleetFormation::LineAhead: return "LineAhead";
			case FleetFormation::LineAbreast: return "LineAbreast";
			case FleetFormation::Echelon: return "Echelon";
			case FleetFormation::Screen: return "Screen";
			case FleetFormation::EngageAtWill: return "EngageAtWill";
		}
		return "";
	}

	// ---------------------------------------------------------------- BroadsideCommand

	BatteryOrder BroadsideCommand::operator[](int index) const
	{
		return index >= 0 && index < orders.Count() ? orders[index] : BatteryOrder::FireAtWill;
	}

	void BroadsideCommand::Set(int index, BatteryOrder value)
	{
		if (index < 0) return;
		if (index >= orders.Count()) orders.Raw().resize(static_cast<size_t>(index) + 1, BatteryOrder::FireAtWill); // Array.Resize(ref orders, index + 1)
		orders[index] = value;
	}

	BatteryOrder BroadsideCommand::OrderOf(const cs::Ref<Subsystem>& s) const { return (*this)[s->Index]; }

	// ---------------------------------------------------------------- Broadsides

	void Broadsides::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		BraceReduction();
		DamageControlRate();
		AiVolleys();
		AiFocusFire();
		AiBraces();
	}

	// ------------------------------------------------------------ settings

	const cs::Ref<RangeSetting>& Broadsides::BraceReduction()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("broadside.brace", SettingGroups::Broadsides,
			"Brace", 0.5, 0, 0.9, 0.05,
			"The share of every hit a braced crew shrugs off while the brace lasts (its guns hold their fire while it is braced).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "brace broadside capital volley");
		return value;
	}

	const cs::Ref<RangeSetting>& Broadsides::DamageControlRate()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("broadside.damageControl", SettingGroups::Broadsides,
			"Damage control rate", 1, 0, 3, 0.25,
			"How fast a capital ship's damage control teams patch its mounts, shields and engines in battle: 0 turns damage control off, 1 is as designed.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "repair crew damage control capital");
		return value;
	}

	const cs::Ref<ToggleSetting>& Broadsides::AiVolleys()
	{
		RunClassConstructor();
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("broadside.aiVolleys", SettingGroups::Broadsides,
			"AI captains fire broadsides", true,
			"AI capital ships hold the guns whose fire cones bear together for a volley and fire them together, telegraphing it, as the player can; a gun that bears alone fires at will.",
			"volley broadside ai doctrine");
		return value;
	}

	const cs::Ref<ToggleSetting>& Broadsides::AiFocusFire()
	{
		RunClassConstructor();
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("broadside.aiFocusFire", SettingGroups::Broadsides,
			"AI gunners focus a weakened patch", true,
			"AI gun crews go for the hardpoints in a patch of the enemy's hull whose shield field is weakened or holed (how strongly is the shield focus setting).",
			"shield emitter patch focus fire ai doctrine");
		return value;
	}

	const cs::Ref<ToggleSetting>& Broadsides::AiBraces()
	{
		RunClassConstructor();
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("broadside.aiBrace", SettingGroups::Broadsides,
			"AI captains brace", true,
			"AI capital ships brace when a broadside they can see building is about to come in on them.",
			"brace ai doctrine");
		return value;
	}

	// ------------------------------------------------------------ constants

	double Broadsides::MinCapitalKm() { return WeakPoints::MinCapitalKm; }

	// ------------------------------------------------------------ who commands

	bool Broadsides::Commands(const cs::Ref<BattleUnit>& u) { return u != nullptr && u->Kind == UnitKind::Ship && u->LengthKm >= MinCapitalKm(); }

	cs::Ref<BroadsideCommand> Broadsides::Of(const cs::Ref<BattleUnit>& u)
	{
		if (!Commands(u)) return nullptr;
		if (!u->Broadside) u->Broadside = cs::New<BroadsideCommand>();
		return u->Broadside;
	}

	// ------------------------------------------------------------ batteries

	bool Broadsides::IsBatteryGun(const cs::Ref<Subsystem>& s)
	{
		auto w = s->Def->Weapon;
		// A spinal mount is no battery: the hull lays it, not its gun crews (U.SPC-T490).
		return w != nullptr && !s->Destroyed() && !w->Missile && !w->AntiSmall && !w->Supporting() && s->Kind() != SubsystemKind::BoardingPods && !s->Def->Spinal;
	}

	bool Broadsides::IsOrdered(const cs::Ref<Subsystem>& s)
	{
		auto w = s->Def->Weapon;
		return w != nullptr && !s->Destroyed() && !w->AntiSmall && !w->Supporting() && s->Kind() != SubsystemKind::BoardingPods;
	}

	std::pair<int, int> Broadsides::Battery(const cs::Ref<BattleUnit>& u, DVec3 dir)
	{
		int loaded = 0, guns = 0;
		for (const auto& s : u->Combat->Subsystems)
		{
			if (!IsBatteryGun(s) || !u->BearsAlong(s->Def, dir)) continue;
			guns++;
			if (s->Cooldown <= 0) loaded++;
		}
		return { loaded, guns };
	}

	std::pair<int, int> Broadsides::VolleyOn(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		int loaded = 0, guns = 0;
		auto cmd = u->Broadside;
		if (cmd == nullptr) return { 0, 0 };
		for (const auto& s : u->Combat->Subsystems)
		{
			if (!IsBatteryGun(s) || cmd->OrderOf(s) != BatteryOrder::Volley || !u->Bears(s->Def, target->Pos)) continue;
			guns++;
			if (s->Cooldown <= 0) loaded++;
		}
		return { loaded, guns };
	}

	std::pair<cs::Nullable<BatteryOrder>, int> Broadsides::OrderAlong(const cs::Ref<BattleUnit>& u, DVec3 dir)
	{
		cs::Nullable<BatteryOrder> order = std::nullopt;
		int n = 0;
		auto cmd = u->Broadside;
		for (const auto& s : u->Combat->Subsystems)
		{
			if (!IsOrdered(s) || !u->BearsAlong(s->Def, dir)) continue;
			auto o = cmd ? cmd->OrderOf(s) : BatteryOrder::FireAtWill;
			if (n == 0) order = o;
			else if (order != o) order = std::nullopt;
			n++;
		}
		return { n == 0 ? cs::Nullable<BatteryOrder>(BatteryOrder::FireAtWill) : order, n };
	}

	int Broadsides::OrderAlong(const cs::Ref<BattleUnit>& u, DVec3 dir, BatteryOrder order)
	{
		auto cmd = Of(u);
		if (cmd == nullptr) return 0;
		int n = 0;
		for (const auto& s : u->Combat->Subsystems)
		{
			if (!IsOrdered(s) || !u->BearsAlong(s->Def, dir)) continue;
			cmd->Set(s->Index, order);
			n++;
		}
		return n;
	}

	std::pair<BatteryOrder, int> Broadsides::CycleOrder(const cs::Ref<BattleUnit>& u, DVec3 dir)
	{
		if (Of(u) == nullptr) return { BatteryOrder::FireAtWill, 0 };
		auto [now, n] = OrderAlong(u, dir);
		if (n == 0) return { BatteryOrder::FireAtWill, 0 };
		auto next = static_cast<BatteryOrder>((static_cast<int>(now.has_value() ? *now : BatteryOrder::Volley) + 1) % 3);
		OrderAlong(u, dir, next);
		return { next, n };
	}

	void Broadsides::SetAll(const cs::Ref<BattleUnit>& u, BatteryOrder order)
	{
		auto cmd = Of(u);
		if (cmd == nullptr) return;
		for (const auto& s : u->Combat->Subsystems) cmd->Set(s->Index, order);
	}

	void Broadsides::BeginVolleys(const cs::Ref<BattleUnit>& u)
	{
		auto cmd = u->Broadside;
		if (cmd == nullptr) return;
		cmd->ReadyingOn = nullptr;
		cmd->ReadyingLoaded = 0;
		cmd->Releasing.Clear();
	}

	bool Broadsides::Releases(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		auto cmd = u->Broadside;
		if (cmd == nullptr || target == nullptr) return true;
		bool go = false;
		if (cmd->Releasing.TryGetValue(target, go)) return go;
		auto [loaded, guns] = VolleyOn(u, target);
		go = guns == 0 || loaded == guns;
		cmd->Releasing[target] = go;
		return go;
	}

	void Broadsides::Holding(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		auto cmd = u->Broadside;
		if (cmd == nullptr || cmd->Readying()) return;
		auto [loaded, guns] = VolleyOn(u, target);
		cmd->ReadyingOn = target;
		cmd->ReadyingLoaded = guns == 0 ? 1 : static_cast<double>(loaded) / guns;
	}

	// ------------------------------------------------------------ focus fire

	bool Broadsides::ToggleSpreadFire(const cs::Ref<BattleUnit>& u)
	{
		auto cmd = Of(u);
		if (cmd == nullptr) return false;
		cmd->SpreadFire = !cmd->SpreadFire;
		return cmd->SpreadFire;
	}

	bool Broadsides::FocusesFire(const cs::Ref<BattleUnit>& shooter)
	{
		if (shooter == nullptr) return false;
		if (shooter->Broadside != nullptr && shooter->Broadside->SpreadFire) return false;
		return shooter->PlayerControlled() || AiFocusFire()->Value;
	}

	// ------------------------------------------------------------ the brace

	bool Broadsides::Brace(const cs::Ref<BattleUnit>& u)
	{
		auto cmd = Of(u);
		if (cmd == nullptr || cmd->Braced() || cmd->BraceCooldown > 0) return false;
		cmd->BraceLeft = BraceSeconds;
		cmd->BraceCooldown = BraceCooldownSeconds;
		return true;
	}

	double Broadsides::BraceFactor(const cs::Ref<BattleUnit>& u)
	{
		return u->Broadside != nullptr && u->Broadside->Braced() ? 1 - BraceReduction()->Value : 1;
	}

	bool Broadsides::Fires(const cs::Ref<BattleUnit>& u) { return u->Broadside == nullptr || !u->Broadside->Braced(); }

	cs::Ref<BattleUnit> Broadsides::IncomingBroadside(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		double cone = cs::Math::Cos(Battle::VolleyConeDeg * Astro::Deg2Rad);
		for (const auto& o : b->Units)
		{
			if (!b->Hostile(u, o) || o->Stunned > 0 || o->Surrendering || o->Captured) continue;
			auto cmd = o->Broadside;
			if (cmd != nullptr && cmd->Readying() && cmd->ReadyingOn == u && cmd->ReadyingLoaded >= BraceAtLoaded) return o;
			if (o->PlayerControlled() && o->PlayerHelm && o->PlayerFireHeld && o->VolleyCharge >= Battle::VolleyBuildSeconds * 0.5 && o->PlayerVolleyAim.LengthSquared() > 1e-12)
			{
				auto to = u->Pos - o->Pos;
				if (to.LengthSquared() > 1e-12 && DVec3::Dot(o->PlayerVolleyAim.Normalized(), to.Normalized()) > cone) return o;
			}
		}
		return nullptr;
	}

	// ------------------------------------------------------------ damage control

	int Broadsides::Teams(const cs::Ref<BattleUnit>& u)
	{
		auto interior = u->Combat->Interior;
		if (interior == nullptr) return 0;
		double crew = interior->Crew() * (interior->UnderBoarding() ? 0.5 : 1);
		return cs::ToInt(cs::Math::Min(static_cast<double>(MaxTeams), cs::Math::Floor(crew / CrewPerTeam)));
	}

	DamageControlFocus Broadsides::CycleFocus(const cs::Ref<BattleUnit>& u)
	{
		auto cmd = Of(u);
		if (cmd == nullptr) return DamageControlFocus::Balanced;
		cmd->Focus = static_cast<DamageControlFocus>((static_cast<int>(cmd->Focus) + 1) % 4);
		return cmd->Focus;
	}

	bool Broadsides::InFocus(const cs::Ref<Subsystem>& s, DamageControlFocus focus)
	{
		switch (focus)
		{
			case DamageControlFocus::Weapons: return s->Def->IsWeapon();
			case DamageControlFocus::Shields: return s->Kind() == SubsystemKind::ShieldGenerator;
			case DamageControlFocus::Engines: return s->Kind() == SubsystemKind::Engine;
			default: return false;
		}
	}

	void Broadsides::StepDamageControl(const cs::Ref<BattleUnit>& u, double dt)
	{
		auto cmd = u->Broadside;
		// Nor with the hull's systems knocked out: the teams are working in the dark.
		if (cmd == nullptr || u->Captured || u->Surrendering || u->Dead || u->Stunned > 0) return;
		double rate = DamageControlRate()->Value;
		int teams = Teams(u);
		// US-T135: a team the player has out on the plating as an EVA worker welds there instead (RepairWorker).
		for (int bits = u->EvaHeldTeams; bits != 0; bits &= bits - 1) teams--;
		if (rate <= 0 || teams <= 0) return;
		auto c = u->Combat;
		double budget = teams * RepairHpPerTeamS * rate * c->Readiness * dt;
		for (int pass = 0; pass < 8 && budget > 1e-9; pass++)
		{
			cs::Ref<Subsystem> worst = nullptr;
			double worstScore = cs::Double::MaxValue;
			for (const auto& s : c->Subsystems)
			{
				if (s->Destroyed() || s->Hp >= s->Def->MaxHp * FieldRepairCap) continue;
				double score = s->Health() - (InFocus(s, cmd->Focus) ? 2 : 0);
				if (score < worstScore)
				{
					worstScore = score;
					worst = s;
				}
			}
			if (worst == nullptr) break;
			double fix = cs::Math::Min(budget, worst->Def->MaxHp * FieldRepairCap - worst->Hp);
			worst->Hp += fix;
			cmd->Repaired += fix;
			budget -= fix;
		}
		StepEmergencyRepair(c, cmd, teams, dt);
		if (cmd->Focus == DamageControlFocus::Shields)
		{
			double ratings = 0, room = 0;
			for (const auto& s : c->Subsystems)
				if (s->Kind() == SubsystemKind::ShieldGenerator && !s->Destroyed())
				{
					ratings += s->Def->Rating;
					room += s->MaxShield - s->Shield;
				}
			if (ratings <= 0 || u->Stunned > 0) return;
			for (const auto& s : c->Subsystems)
			{
				if (room <= 0) return;
				if (s->Kind() != SubsystemKind::ShieldGenerator || s->Destroyed() || s->Shield >= s->MaxShield) continue;
				// RegenShields already regrows a patch that has not been hit lately: this is for the ones under fire.
				double add = cs::Math::Min(room, cs::Math::Min(s->MaxShield - s->Shield, c->Spec->ShieldRegen * s->Def->Rating / ratings * 0.5 * rate * dt));
				s->Shield += add;
				room -= add;
			}
		}
	}

	void Broadsides::StepEmergencyRepair(const cs::Ref<ShipCombat>& c, const cs::Ref<BroadsideCommand>& cmd, int teams, double dt)
	{
		double rate = ShieldEmitters::EmergencyRepairRate()->Value;
		if (rate <= 0) return;
		double work = teams * RepairHpPerTeamS * rate * c->Readiness * dt;
		double share = ShieldEmitters::EmergencyRepairWork()->Value;
		for (int pass = 0; pass < 8 && work > 1e-9; pass++)
		{
			cs::Ref<Subsystem> next = nullptr;
			double nextScore = cs::Double::MaxValue;
			for (const auto& s : c->Subsystems)
			{
				if (!ShieldEmitters::Restorable(s)) continue;
				double score = -s->Restoring / s->Def->MaxHp - (InFocus(s, cmd->Focus) ? 10 : 0);
				if (score < nextScore)
				{
					nextScore = score;
					next = s;
				}
			}
			if (next == nullptr) return;
			double need = share * next->Def->MaxHp - next->Restoring;
			double put = cs::Math::Min(work, cs::Math::Max(0.0, need));
			next->Restoring += put;
			work -= put;
			if (next->Restoring < share * next->Def->MaxHp - 1e-9) continue;
			next->Hp = next->Def->MaxHp * ShieldEmitters::RestoredHealth()->Value;
			next->Restoring = 0;
			next->Shield = 0;
			cmd->Restored++;
		}
	}

	// ------------------------------------------------------------ the step

	void Broadsides::Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt)
	{
		auto cmd = Of(u);
		if (cmd == nullptr) return;
		cmd->BraceLeft = cs::Math::Max(0.0, cmd->BraceLeft - dt);
		cmd->BraceCooldown = cs::Math::Max(0.0, cmd->BraceCooldown - dt);
		if (!u->PlayerControlled() && !u->Captured && !u->Surrendering && !u->Combat->Disabled()) Doctrine(b, u, cmd);
		StepDamageControl(u, dt);
	}

	void Broadsides::Doctrine(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BroadsideCommand>& cmd)
	{
		const auto& subs = u->Combat->Subsystems;
		auto spec = u->Combat->Spec;
		for (const auto& s : subs)
		{
			if (!IsOrdered(s)) continue;
			bool together = false;
			if (AiVolleys()->Value && IsBatteryGun(s))
				for (const auto& o : subs)
					if (o != s && IsBatteryGun(o) && FireCones::Overlap(s->Def, o->Def, spec))
					{
						together = true;
						break;
					}
			cmd->Set(s->Index, together ? BatteryOrder::Volley : BatteryOrder::FireAtWill);
		}
		if (AiBraces()->Value && !cmd->Braced() && cmd->BraceCooldown <= 0 && IncomingBroadside(b, u) != nullptr) Brace(u);
	}

	// ------------------------------------------------------------ formations

	bool Broadsides::IsHeld(FleetFormation f) { return f != FleetFormation::None && f != FleetFormation::EngageAtWill; }

	bool Broadsides::HoldsFormation(const cs::Ref<BattleUnit>& u)
	{
		return u->Leader != nullptr && u->Leader->Present() && !u->Leader->Fleeing && u->Leader->Broadside != nullptr && IsHeld(u->Leader->Broadside->Formation);
	}

	cs::List<cs::Ref<BattleUnit>> Broadsides::FormationMembers(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& flag)
	{
		cs::List<cs::Ref<BattleUnit>> list;
		for (const auto& o : b->Units)
		{
			if (o == flag || !o->Present() || o->Side != flag->Side || o->Faction != flag->Faction || !Commands(o)) continue;
			if (o->PlayerControlled() || o->Captured || o->Surrendering || o->Fleeing || o->Combat->Disabled() || o->Latched != nullptr || o->BoardTarget != nullptr) continue;
			if (o->Ship == nullptr || !o->Ship->Spec->IsMilitary) continue;
			list.Add(o);
		}
		list.Sort([](const cs::Ref<BattleUnit>& a, const cs::Ref<BattleUnit>& c)
		{
			int h = cs::CompareTo(c->Combat->Spec->Hull, a->Combat->Spec->Hull);
			return h != 0 ? h : cs::CompareTo(a->Id, c->Id);
		});
		return list;
	}

	DVec3 Broadsides::SlotFor(FleetFormation f, int i, int n, double spacing)
	{
		int k = i + 1;
		switch (f)
		{
			case FleetFormation::LineAhead: return DVec3(0, 0, -k * spacing);
			case FleetFormation::LineAbreast:
			{
				int rank = (i / 2) + 1;
				return DVec3((i % 2 == 0 ? 1 : -1) * rank * spacing, 0, 0);
			}
			case FleetFormation::Echelon: return DVec3(k * spacing * 0.7, 0, -k * spacing * 0.7);
			case FleetFormation::Screen:
			{
				double a = 2 * cs::Math::PI * i / cs::Math::Max(1, n);
				double r = spacing * 1.5;
				return DVec3(cs::Math::Sin(a) * r, 0, cs::Math::Cos(a) * r);
			}
			default: return DVec3::Zero;
		}
	}

	double Broadsides::Spacing(const cs::Ref<BattleUnit>& flag, const cs::List<cs::Ref<BattleUnit>>& members)
	{
		double longest = flag->LengthKm;
		for (const auto& m : members) longest = cs::Math::Max(longest, m->LengthKm);
		return longest * 1.5 + 1;
	}

	int Broadsides::OrderFormation(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& flag, FleetFormation f)
	{
		auto cmd = Of(flag);
		if (cmd == nullptr) return 0;
		auto members = FormationMembers(b, flag);
		cmd->Formation = f;
		double spacing = Spacing(flag, members);
		for (int i = 0; i < members.Count(); i++)
		{
			auto m = members[i];
			if (IsHeld(f))
			{
				m->Leader = flag;
				m->Slot = SlotFor(f, i, members.Count(), spacing);
			}
			else if (m->Leader == flag)
			{
				m->Leader = nullptr;
				m->Slot = DVec3::Zero;
			}
		}
		return members.Count();
	}

	cs::String Broadsides::Label(FleetFormation f)
	{
		switch (f)
		{
			case FleetFormation::LineAhead: return "line ahead";
			case FleetFormation::LineAbreast: return "line abreast";
			case FleetFormation::Echelon: return "echelon";
			case FleetFormation::Screen: return "screen";
			case FleetFormation::EngageAtWill: return "engage at will";
			default: return "no formation";
		}
	}

	cs::String Broadsides::Label(BatteryOrder o) { return o == BatteryOrder::HoldFire ? "hold fire" : o == BatteryOrder::Volley ? "volley" : "fire at will"; }

	cs::String Broadsides::Label(DamageControlFocus f)
	{
		return f == DamageControlFocus::Weapons ? "weapons" : f == DamageControlFocus::Shields ? "shields" : f == DamageControlFocus::Engines ? "engines" : "balanced";
	}

	// ------------------------------------------------------------ turning circles

	double Broadsides::TurningRadiusKm(double speedKmS, double turnDegS)
	{
		return turnDegS <= 0 ? cs::Double::PositiveInfinity : cs::Math::Abs(speedKmS) / (turnDegS * Astro::Deg2Rad);
	}

	double Broadsides::ComeAboutSeconds(double turnDegS) { return turnDegS <= 0 ? cs::Double::PositiveInfinity : 180 / turnDegS; }

	double Broadsides::SecondsToBear(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& gun, double turnDegS)
	{
		if (u->Bears(gun->Def, target->Pos)) return 0;
		if (turnDegS <= 0) return cs::Double::PositiveInfinity;
		return FireCones::OutsideDeg(gun->Def, u->Combat->Spec, u->DirToLocal(target->Pos - u->Pos)) / turnDegS;
	}
}
