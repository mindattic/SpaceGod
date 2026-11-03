// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Radiators.cs (see docs/MIGRATION.md).

#include "Combat/Radiators.h"

#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/Broadsides.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	void Radiators::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		RadiatorShed();
		SkinShed();
		WeaponHeat();
		EngineHeat();
		SinkSeconds();
		ThrottleAt();
		MinThrottle();
		CrewLoss();
		SystemBurn();
		AiFleesHot();
		Panels();
	}

	// ------------------------------------------------------------ settings

	const cs::Ref<RangeSetting>& Radiators::RadiatorShed()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("heat.radiators", SettingGroups::Broadsides,
			"Radiator capacity", 1.0, 0.25, 3, 0.05,
			"How much heat a big ship's radiators shed with every panel whole, as a share of what its reactor, guns and drive make together at full stretch (they make 1.05, the hull's skin sheds the rest).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "heat radiator capital cooling thermal");
		return value;
	}

	const cs::Ref<RangeSetting>& Radiators::SkinShed()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("heat.skin", SettingGroups::Broadsides,
			"Hull skin cooling", 0.3, 0, 1, 0.05,
			"How much heat a big ship sheds through its own hull with no radiators at all, on the same scale.",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "heat radiator capital cooling hull skin thermal");
		return value;
	}

	const cs::Ref<RangeSetting>& Radiators::WeaponHeat()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("heat.weapons", SettingGroups::Broadsides,
			"Heat from the guns", 0.6, 0, 2, 0.05,
			"How much heat a big ship's guns and launchers make with every one of them reloading at once.",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "heat weapons guns capital thermal");
		return value;
	}

	const cs::Ref<RangeSetting>& Radiators::EngineHeat()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("heat.engines", SettingGroups::Broadsides,
			"Heat from the drive", 0.35, 0, 2, 0.05,
			"How much heat a big ship's drive makes at its full battle speed.",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "heat engines drive capital thermal");
		return value;
	}

	const cs::Ref<RangeSetting>& Radiators::SinkSeconds()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("heat.sinks", SettingGroups::Broadsides,
			"Heat sinks", 90, 15, 300, 5,
			"How long a big ship's heat sinks hold heat it cannot shed: seconds of making a full share more than it sheds before it reaches their limit.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0"), " s"); }, "heat sinks capital thermal");
		return value;
	}

	const cs::Ref<RangeSetting>& Radiators::ThrottleAt()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("heat.throttleAt", SettingGroups::Broadsides,
			"Throttle from", 0.6, 0.2, 1, 0.05,
			"How full a big ship's heat sinks get before its crew starts throttling its guns and drive to hold the heat.",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "heat throttle capital thermal");
		return value;
	}

	const cs::Ref<RangeSetting>& Radiators::MinThrottle()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("heat.minThrottle", SettingGroups::Broadsides,
			"Throttled to", 0.15, 0, 1, 0.05,
			"How far the crew throttles the guns' reloading and the drive with the heat sinks at their limit (as designed, far enough to hold a hull with no radiators left short of it).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "heat throttle capital thermal");
		return value;
	}

	const cs::Ref<RangeSetting>& Radiators::CrewLoss()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("heat.crewLoss", SettingGroups::Broadsides,
			"Heat casualties", 0.004, 0, 0.03, 0.001,
			"The share of its crew a big ship loses each second past its heat sinks' limit (more the further past it goes).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0.0"), "%/s"); }, "heat crew casualties capital thermal");
		return value;
	}

	const cs::Ref<RangeSetting>& Radiators::SystemBurn()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("heat.systemBurn", SettingGroups::Broadsides,
			"Heat damage to systems", 0.01, 0, 0.05, 0.001,
			"The share of its toughness each gun, drive and shield generator loses each second a big ship runs past its heat sinks' limit (more the further past it goes).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0.0"), "%/s"); }, "heat damage systems capital thermal");
		return value;
	}

	const cs::Ref<ToggleSetting>& Radiators::AiFleesHot()
	{
		RunClassConstructor();
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("heat.aiFleesHot", SettingGroups::Broadsides,
			"AI runs hot to escape", true,
			"An AI captain running from a fight keeps its drive at full however hot the hull gets, and takes what the heat does to it.",
			"heat flee run hot capital thermal");
		return value;
	}

	// ------------------------------------------------------------ rules

	cs::Dictionary<ShipClass, cs::List<cs::Ref<SubsystemDef>>>& Radiators::Panels()
	{
		RunClassConstructor();
		static cs::Dictionary<ShipClass, cs::List<cs::Ref<SubsystemDef>>> value = []
		{
			cs::Dictionary<ShipClass, cs::List<cs::Ref<SubsystemDef>>> d;
			// Two pairs of wings off the flanks abaft the island, clear of the flight deck.
			d[ShipClass::Carrier] = Pairs(0.15, 0.0, { -0.12, -0.24 });
			// Two pairs off the flanks between the beam batteries and the drive.
			d[ShipClass::Battleship] = Pairs(0.12, 0.0, { -0.14, -0.33 });
			d[ShipClass::CommandCruiser] = Pairs(0.12, 0.0, { -0.1, -0.26 });
			// Two pairs off the flanks abaft the beam batteries, the plasma bottles' and the drive's heat (U.SPC-T478).
			d[ShipClass::GateBreaker] = Pairs(0.12, -0.02, { -0.28, -0.38 });
			// One pair amidships forward of the engine nacelles, one pair abaft them.
			d[ShipClass::Cruiser] = Pairs(0.14, 0.0, { 0.12, -0.36 });
			return d;
		}();
		return value;
	}

	cs::List<cs::Ref<SubsystemDef>> Radiators::Pairs(double x, double y, const cs::List<double>& zs)
	{
		cs::List<cs::Ref<SubsystemDef>> list;
		for (double z : zs)
		{
			list.Add(cs::New<SubsystemDef>(SubsystemKind::Radiator, x, y, z));
			list.Add(cs::New<SubsystemDef>(SubsystemKind::Radiator, -x, y, z));
		}
		return list.ToArray();
	}

	const cs::List<cs::Ref<SubsystemDef>>& Radiators::PanelsFor(ShipClass c)
	{
		static const cs::List<cs::Ref<SubsystemDef>> empty; // Array.Empty<SubsystemDef>()
		auto& panels = Panels();
		return panels.ContainsKey(c) ? panels.At(c) : empty;
	}

	void Radiators::Register(ShipClass c, cs::Nullable<cs::List<cs::Ref<SubsystemDef>>> panels)
	{
		if (!panels.has_value()) Panels().Remove(c);
		else Panels()[c] = *panels;
	}

	cs::List<cs::Ref<SubsystemDef>> Radiators::Fit(ShipClass c, const cs::List<cs::Ref<SubsystemDef>>& layout)
	{
		const auto& panels = PanelsFor(c);
		if (panels.Count() == 0) return layout;
		for (const auto& d : layout) if (d->Kind == SubsystemKind::Radiator) return layout;
		auto all = cs::NewArray<cs::Ref<SubsystemDef>>(layout.Count() + panels.Count());
		for (int i = 0; i < layout.Count(); i++) all[i] = layout[i]; // layout.CopyTo(all, 0)
		for (int i = 0; i < panels.Count(); i++) all[layout.Count() + i] = panels[i];
		return all;
	}

	bool Radiators::Carries(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return false;
		for (const auto& s : c->Subsystems) if (s->Kind() == SubsystemKind::Radiator) return true;
		return false;
	}

	double Radiators::Intact(const cs::Ref<ShipCombat>& c)
	{
		double total = 0, alive = 0;
		if (c != nullptr)
			for (const auto& s : c->Subsystems)
			{
				if (s->Kind() != SubsystemKind::Radiator) continue;
				total += s->Def->Rating;
				if (!s->Destroyed()) alive += s->Def->Rating;
			}
		return total <= 0 ? 1 : alive / total;
	}

	double Radiators::Shed(const cs::Ref<ShipCombat>& c) { return SkinShed()->Value + RadiatorShed()->Value * Intact(c); }

	double Radiators::Made(const cs::Ref<BattleUnit>& u, double reload)
	{
		auto c = u->Combat;
		double all = 0, busy = 0;
		for (const auto& s : c->Subsystems)
		{
			auto w = s->Def->Weapon;
			if (w == nullptr || w->Supporting() || w->Cooldown <= 0) continue;
			double dps = w->Damage * cs::Math::Max(1, w->Salvo) / w->Cooldown * (w->AntiSmall ? 0.3 : 1);
			all += dps;
			if (!s->Destroyed() && s->Cooldown > 0) busy += dps;
		}
		double guns = all > 0 ? WeaponHeat()->Value * busy / all * cs::Math::Max(0.0, reload) : 0;
		double top = c->Spec->MaxSpeed;
		double drive = top > 0 ? EngineHeat()->Value * cs::Math::Min(1.0, u->Vel.Length() / top) : 0;
		return ReactorHeat + guns + drive;
	}

	bool Radiators::RunsHot(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && ((u->Broadside != nullptr && u->Broadside->RunHot == true) || (!u->PlayerControlled() && u->Fleeing && AiFleesHot()->Value));
	}

	double Radiators::Throttle(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr || u->HullHeat <= 0 || RunsHot(u)) return 1;
		double from = ThrottleAt()->Value;
		if (u->HullHeat <= from) return 1;
		double f = cs::Math::Min(1.0, (u->HullHeat - from) / cs::Math::Max(1e-6, 1 - from));
		return 1 - f * (1 - MinThrottle()->Value);
	}

	void Radiators::Step(const cs::Ref<BattleUnit>& u, double dt, double reload, cs::List<cs::Ref<Subsystem>>* burnedOut)
	{
		auto c = u->Combat;
		if (dt <= 0 || !Carries(c)) return;
		double net = Made(u, reload * Throttle(u)) - Shed(c);
		u->HullHeat = cs::Math::Max(0.0, cs::Math::Min(MaxHeat, u->HullHeat + net * dt / cs::Math::Max(1.0, SinkSeconds()->Value)));
		double over = u->HullHeat - 1;
		if (over <= 0) return;
		// The further past the limit, the worse: twice as bad a quarter past it.
		double severity = 1 + over * 4;
		double lost = c->Interior->TakeCrew(c->Interior->Crew() * cs::Math::Min(1.0, CrewLoss()->Value * severity * dt));
		c->Interior->CrewLost += lost;
		double burn = SystemBurn()->Value * severity * dt;
		if (burn <= 0) return;
		for (const auto& s : c->Subsystems)
		{
			if (s->Destroyed() || !Cooks(s)) continue;
			s->Hp -= s->Def->MaxHp * burn;
			if (s->Hp > 0) continue;
			s->Hp = 0;
			if (s->Kind() == SubsystemKind::ShieldGenerator) s->Shield = 0;
			if (burnedOut) burnedOut->Add(s);
		}
	}

	bool Radiators::Cooks(const cs::Ref<Subsystem>& s)
	{
		return s->Kind() == SubsystemKind::Engine || s->Kind() == SubsystemKind::ShieldGenerator || (s->Def->Weapon != nullptr && !s->Def->Weapon->Supporting());
	}

	double Radiators::TargetWeight(const cs::Ref<BattleUnit>& target)
	{
		return 1 + HotTargetBonus * cs::Math::Min(1.0, cs::Math::Max(0.0, target ? target->HullHeat : 0.0));
	}
}
