// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/AlphaStrike.cs (see docs/MIGRATION.md).

#include "Combat/AlphaStrike.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(StrikePhase v)
	{
		switch (v)
		{
			case StrikePhase::RunIn: return "RunIn";
			case StrikePhase::Egress: return "Egress";
		}
		return "";
	}

	cs::String AlphaStrike::Km(double v) { return cs::Cat(cs::Fmt(v, "0"), " km"); }

	cs::String AlphaStrike::Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	cs::String AlphaStrike::Times(double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }

	void AlphaStrike::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		VolleyShare();
		EgressKm();
		Burn();
		CapitalBias();
		AntiShipMissile();
		MagazineTorpedo();
		classes();
	}

	const cs::Ref<RangeSetting>& AlphaStrike::VolleyShare()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("frigate.volleyRange", SettingGroups::Broadsides,
			"Alpha strike range", 0.8, 0.3, 1, 0.05,
			cs::Cat("How far into the reach of its shortest-ranged launcher a frigate burns before loosing its volley: every launcher ",
				"then reaches its mark, and lower holds it until it is surer of it."), &Percent,
			"frigate alpha strike volley range torpedo missile run");
		return value;
	}

	const cs::Ref<RangeSetting>& AlphaStrike::EgressKm()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("frigate.egressKm", SettingGroups::Broadsides,
			"Alpha strike egress", 32, 10, 44, 1,
			cs::Cat("How far (km) from its mark a frigate burns back out after its volley before it comes round for another run, once ",
				"its launchers have reloaded."), &Km, "frigate alpha strike egress withdraw distance burn out flip");
		return value;
	}

	const cs::Ref<RangeSetting>& AlphaStrike::Burn()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("frigate.burn", SettingGroups::Broadsides,
			"Alpha strike burn", 1, 0, 1, 0.05,
			"How much of its boost a frigate burns on its runs in and out (0 flies them at its cruising speed).", &Percent,
			"frigate alpha strike speed boost burn run");
		return value;
	}

	const cs::Ref<RangeSetting>& AlphaStrike::CapitalBias()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("frigate.capitalBias", SettingGroups::Broadsides,
			"Frigate capital preference", 12, 0, 40, 1,
			cs::Cat("How strongly a frigate picks a capital ship over a smaller hull to strike (as if it were that many km nearer): its ",
				"torpedoes are built to sink the big ones."), &Km, "frigate alpha strike target capital battleship carrier preference");
		return value;
	}

	// ---------------------------------------------------------------- the magazines

	const cs::Ref<WeaponSpec>& AlphaStrike::AntiShipMissile()
	{
		RunClassConstructor();
		static const cs::Ref<WeaponSpec> value = []
		{
			auto w = cs::New<WeaponSpec>();
			w->RangeKm = 50; w->Damage = 300; w->Cooldown = 30; w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = 1.6; w->Salvo = 2; w->Ammo = 24;
			return w;
		}();
		return value;
	}

	const cs::Ref<WeaponSpec>& AlphaStrike::MagazineTorpedo()
	{
		RunClassConstructor();
		static const cs::Ref<WeaponSpec> value = []
		{
			auto w = cs::New<WeaponSpec>();
			w->RangeKm = WeaponSpec::AntimatterTorpedo()->RangeKm; w->Damage = WeaponSpec::AntimatterTorpedo()->Damage; w->Cooldown = WeaponSpec::AntimatterTorpedo()->Cooldown;
			w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = WeaponSpec::AntimatterTorpedo()->MissileSpeedKmS; w->Salvo = 1; w->Ammo = 2 * WeaponSpec::AntimatterTorpedo()->Ammo;
			return w;
		}();
		return value;
	}

	// ---------------------------------------------------------------- who flies it

	cs::HashSet<ShipClass>& AlphaStrike::classes()
	{
		RunClassConstructor();
		static cs::HashSet<ShipClass> value { ShipClass::Frigate };
		return value;
	}

	void AlphaStrike::Register(ShipClass cls, bool flies)
	{
		if (flies) classes().Add(cls);
		else classes().Remove(cls);
	}

	bool AlphaStrike::Doctrine(ShipClass cls) { return classes().Contains(cls); }

	bool AlphaStrike::Flies(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Kind == UnitKind::Ship && (u->Ship ? u->Ship->Spec : nullptr) != nullptr && !u->PlayerControlled() && Doctrine(u->Ship->Spec->Class)
			&& u->Combat != nullptr && StrikeMounts(u->Combat) > 0;
	}

	bool AlphaStrike::IsStrikeMount(const cs::Ref<Subsystem>& s)
	{
		auto w = s ? s->Def->Weapon : cs::Ref<WeaponSpec>();
		return w != nullptr && w->Missile && !w->AntiSmall && !w->Supporting() && s->Kind() != SubsystemKind::BoardingPods;
	}

	int AlphaStrike::StrikeMounts(const cs::Ref<ShipCombat>& c)
	{
		int n = 0;
		for (const auto& s : c->Subsystems)
			if (IsStrikeMount(s) && !s->Destroyed() && s->Ammo > 0) n++;
		return n;
	}

	double AlphaStrike::VolleyRangeKm(const cs::Ref<BattleUnit>& u)
	{
		double reach = cs::Double::MaxValue;
		for (const auto& s : u->Combat->Subsystems)
			if (IsStrikeMount(s) && !s->Destroyed() && s->Ammo > 0) reach = cs::Math::Min(reach, s->Def->Weapon->RangeKm);
		return reach == cs::Double::MaxValue ? 0 : reach * VolleyShare()->Value;
	}

	bool AlphaStrike::Loaded(const cs::Ref<BattleUnit>& u)
	{
		for (const auto& s : u->Combat->Subsystems)
			if (IsStrikeMount(s) && !s->Destroyed() && s->Ammo > 0 && s->Cooldown > 0) return false;
		return true;
	}

	double AlphaStrike::Gap(const cs::Ref<BattleUnit>& u) { return cs::Math::Max(0.0, (u->Target->Pos - u->Pos).Length() - u->Target->Radius()); }

	bool AlphaStrike::Releases(const cs::Ref<BattleUnit>& u)
	{
		return u->Strike == StrikePhase::RunIn && u->Target != nullptr && u->Target->Present() && Loaded(u) && Gap(u) <= VolleyRangeKm(u);
	}

	bool AlphaStrike::Holds(const cs::Ref<Subsystem>& s, bool release) { return IsStrikeMount(s) && !release; }

	void AlphaStrike::Loosed(const cs::Ref<BattleUnit>& u) { u->Strike = StrikePhase::Egress; }

	double AlphaStrike::Steer(const cs::Ref<BattleUnit>& u, double speed, DVec3& desired, DVec3& face)
	{
		auto t = u->Target;
		auto to = t->Pos - u->Pos;
		double dist = cs::Math::Max(to.Length(), 1e-6);
		auto dir = to / dist;
		double boost = 1 + (cs::Math::Max(1.0, u->Combat->Spec->BoostSpeedMultiplier) - 1) * Burn()->Value;
		double top = speed * boost;
		// Out far enough (or at the edge of the fight): clear of the enemy's reach.
		bool clear = Gap(u) >= EgressKm()->Value || u->Pos.Length() > Battle::EscapeKm * 0.7;
		// Reloaded out there: come round for the next run.
		if (u->Strike == StrikePhase::Egress && clear && Loaded(u)) u->Strike = StrikePhase::RunIn;
		if (u->Strike == StrikePhase::RunIn)
		{
			desired = dir * top;
			face = dir;
			return boost;
		}
		// Burning out: away from the mark, bending back in rather than leaving the fight altogether.
		auto away = -dir;
		if (u->Pos.Length() > Battle::EscapeKm * 0.6) away = (away + (-u->Pos).Normalized() * 1.2).Normalized();
		if (clear)
		{
			// Waiting on its reload out of reach: hold off, circling.
			auto around = DVec3::Cross(DVec3::UnitZ, dir);
			if (around.LengthSquared() < 1e-6) around = DVec3::UnitY;
			desired = around.Normalized() * speed * 0.5;
			face = desired;
			return boost;
		}
		desired = away * top;
		face = away;
		return boost;
	}

	double AlphaStrike::TargetBias(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o)
	{
		if ((u && u->Ship ? u->Ship->Spec : nullptr) == nullptr || !Doctrine(u->Ship->Spec->Class) || o == nullptr || o->Kind == UnitKind::Fighter) return 0;
		return o->LengthKm >= 1 ? CapitalBias()->Value : 0;
	}
}
