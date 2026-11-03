// Port of Assets/UnitySpace/Core/Combat/CombatSpecs.cs (see docs/MIGRATION.md).

#include "Combat/CombatSpecs.h"

#include "Ships.h"
#include "Combat/AlphaStrike.h"
#include "Combat/BulkFreighters.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/Facilities.h"
#include "Combat/FireCones.h"
#include "Combat/Outfitting.h"
#include "Combat/Radiators.h"
#include "Combat/SpinalMounts.h"
#include "Combat/TachyonLances.h"

namespace SpaceGod::Core
{
	const char* ToString(SubsystemKind v)
	{
		switch (v)
		{
			case SubsystemKind::LaserTurret: return "LaserTurret";
			case SubsystemKind::HeavyLaser: return "HeavyLaser";
			case SubsystemKind::MissileLauncher: return "MissileLauncher";
			case SubsystemKind::PointDefense: return "PointDefense";
			case SubsystemKind::FighterGuns: return "FighterGuns";
			case SubsystemKind::Torpedo: return "Torpedo";
			case SubsystemKind::Railgun: return "Railgun";
			case SubsystemKind::BoardingPods: return "BoardingPods";
			case SubsystemKind::Drill: return "Drill";
			case SubsystemKind::Engine: return "Engine";
			case SubsystemKind::ShieldGenerator: return "ShieldGenerator";
			case SubsystemKind::Bridge: return "Bridge";
			case SubsystemKind::Hangar: return "Hangar";
			case SubsystemKind::Reactor: return "Reactor";
			case SubsystemKind::Autocannon: return "Autocannon";
			case SubsystemKind::LightRailgun: return "LightRailgun";
			case SubsystemKind::EmpLauncher: return "EmpLauncher";
			case SubsystemKind::MineLayer: return "MineLayer";
			case SubsystemKind::AutoTurret: return "AutoTurret";
			case SubsystemKind::TorpedoTube: return "TorpedoTube";
			case SubsystemKind::Countermeasures: return "Countermeasures";
			case SubsystemKind::TachyonLance: return "TachyonLance";
			case SubsystemKind::PhaserArray: return "PhaserArray";
			case SubsystemKind::PlasmaCannon: return "PlasmaCannon";
			case SubsystemKind::AntimatterTorpedo: return "AntimatterTorpedo";
			case SubsystemKind::FlakCannon: return "FlakCannon";
			case SubsystemKind::InterceptorMissile: return "InterceptorMissile";
			case SubsystemKind::Coilgun: return "Coilgun";
			case SubsystemKind::Radiator: return "Radiator";
		}
		return "";
	}

	const char* ToString(DeployKind v)
	{
		switch (v)
		{
			case DeployKind::None: return "None";
			case DeployKind::Mine: return "Mine";
			case DeployKind::Decoy: return "Decoy";
		}
		return "";
	}

	// ---------------------------------------------------------------- WeaponSpec

	double WeaponSpec::DamageAt(double km) const
	{
		if (FalloffEdgeShare >= 1 || RangeKm <= 0) return Damage;
		double f = cs::Math::Max(0.0, cs::Math::Min(1.0, km / RangeKm));
		return Damage * (1 - (1 - cs::Math::Max(0.0, FalloffEdgeShare)) * f);
	}

	// The static readonly instances, each built as its C# object initialiser builds it.
	const cs::Ref<WeaponSpec>& WeaponSpec::TachyonLance()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 60; w->Damage = 650; w->Cooldown = 60; w->Accuracy = 0.95; w->Lance = true; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Laser()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 16; w->Damage = 45; w->Cooldown = 3.0; w->Accuracy = 0.8; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Heavy()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 24; w->Damage = 190; w->Cooldown = 9.0; w->Accuracy = 0.72; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Missiles()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 40; w->Damage = 260; w->Cooldown = 24; w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = 1.4; w->Salvo = 2; w->Ammo = 16; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::PointDefense()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 3.5; w->Damage = 10; w->Cooldown = 0.35; w->Accuracy = 0.5; w->AntiSmall = true; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::FighterGuns()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 1.6; w->Damage = 14; w->Cooldown = 0.3; w->Accuracy = 0.55; w->MuzzleSpeedKmS = 3.5; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Torpedo()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 4; w->Damage = 420; w->Cooldown = 40; w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = 0.9; w->Salvo = 1; w->Ammo = 2; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Railgun()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 30; w->Damage = 260; w->Cooldown = 12; w->Accuracy = 0.85; w->MuzzleSpeedKmS = 14; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Autocannon()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 1.6; w->Damage = 14; w->Cooldown = 0.3; w->Accuracy = 0.55; w->MuzzleSpeedKmS = 3.5; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::LightRailgun()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 6; w->Damage = 80; w->Cooldown = 2.0; w->Accuracy = 0.85; w->MuzzleSpeedKmS = 9; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Emp()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 5; w->Damage = 0; w->Cooldown = 18; w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = 1.6; w->Salvo = 1; w->Ammo = 4; w->EmpSeconds = 8; w->EmpRadiusKm = 0.5; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Mines()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 0.6; w->Damage = 300; w->Cooldown = 3; w->Accuracy = 1; w->Ammo = 6; w->Deploy = DeployKind::Mine; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::AutoTurret()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 1.8; w->Damage = 9; w->Cooldown = 0.25; w->Accuracy = 0.6; w->MuzzleSpeedKmS = 3.5; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::HeavyTorpedo()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 32; w->Damage = 900; w->Cooldown = 45; w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = 1.0; w->Salvo = 1; w->Ammo = 6; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::DogfightMissile()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 3.5; w->Damage = 70; w->Cooldown = 3; w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = 1.1; w->Salvo = 1; w->Ammo = 2; w->SeeksSmallCraft = true; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Flares()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 0; w->Damage = 0; w->Cooldown = 2; w->Accuracy = 1; w->Ammo = 5; w->Deploy = DeployKind::Decoy; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::AntimatterTorpedo()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 20; w->Damage = 1600; w->Cooldown = 35; w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = 1.3; w->Salvo = 1; w->Ammo = 4; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Flak()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 5; w->Damage = 30; w->Cooldown = 0.8; w->Accuracy = 0.6; w->AntiSmall = true; w->BurstKm = 0.4; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Interceptor()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 14; w->Damage = 80; w->Cooldown = 3; w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = 3.2; w->Salvo = 1; w->Ammo = 20; w->AntiSmall = true; w->SeeksSmallCraft = true; return w; }();
		return s;
	}

	const cs::Ref<WeaponSpec>& WeaponSpec::Pods()
	{
		static const cs::Ref<WeaponSpec> s = [] { auto w = cs::New<WeaponSpec>(); w->RangeKm = 10; w->Damage = 60; w->Cooldown = 50; w->Accuracy = 1; w->Missile = true; w->MissileSpeedKmS = 0.8; w->Salvo = 2; w->Ammo = 6; return w; }();
		return s;
	}
	// ---------------------------------------------------------------- SubsystemDef

	SubsystemDef::SubsystemDef(SubsystemKind kind, double x, double y, double z, bool facesAft, cs::Nullable<DVec3> coneAxis,
		cs::Nullable<double> coneHalfDeg)
	{
		Kind = kind;
		X = x;
		Y = y;
		Z = z;
		FacesAft = facesAft;
		ConeAxis = coneAxis;
		ConeHalfDeg = coneHalfDeg;
		MaxHp = DefaultHp(kind);
		Weapon = DefaultWeapon(kind);
	}

	double SubsystemDef::HalfDeg() const { return FireCones::HalfDeg(Self()); }

	DVec3 SubsystemDef::AxisOn(const cs::Ref<CombatSpec>& spec) const { return FireCones::Axis(Self(), spec); }

	bool SubsystemDef::Covers(const cs::Ref<CombatSpec>& spec, DVec3 dirLocal) const { return FireCones::Covers(Self(), spec, dirLocal); }

	double SubsystemDef::DefaultHp(SubsystemKind k)
	{
		return BaseHp(k) * (k == SubsystemKind::FighterGuns || k == SubsystemKind::Torpedo || k == SubsystemKind::Autocannon
		                    || k == SubsystemKind::Radiator ? 1 : 3);
	}

	double SubsystemDef::BaseHp(SubsystemKind k)
	{
		switch (k)
		{
			case SubsystemKind::LaserTurret: return 220;
			case SubsystemKind::HeavyLaser: return 380;
			case SubsystemKind::Railgun: return 420;
			case SubsystemKind::MissileLauncher: return 260;
			case SubsystemKind::PointDefense: return 90;
			case SubsystemKind::Engine: return 300;
			case SubsystemKind::ShieldGenerator: return 260;
			case SubsystemKind::Bridge: return 320;
			case SubsystemKind::Hangar: return 520;
			case SubsystemKind::Reactor: return 650;
			case SubsystemKind::BoardingPods: return 200;
			case SubsystemKind::Drill: return 600;
			case SubsystemKind::FighterGuns: return 30;
			case SubsystemKind::Torpedo: return 30;
			case SubsystemKind::Autocannon: return 60;
			case SubsystemKind::LightRailgun: return 40;
			case SubsystemKind::EmpLauncher:
			case SubsystemKind::MineLayer: return 30;
			case SubsystemKind::AutoTurret: return 50;
			case SubsystemKind::TorpedoTube: return 280;
			case SubsystemKind::Countermeasures: return 30;
			case SubsystemKind::TachyonLance: return 450;
			case SubsystemKind::PhaserArray: return 260;
			case SubsystemKind::PlasmaCannon: return 400;
			case SubsystemKind::AntimatterTorpedo: return 300;
			case SubsystemKind::FlakCannon: return 110;
			case SubsystemKind::InterceptorMissile: return 120;
			case SubsystemKind::Coilgun: return 450;
			case SubsystemKind::Radiator: return Radiators::PanelHp;
			default: return 200;
		}
	}

	cs::Ref<WeaponSpec> SubsystemDef::DefaultWeapon(SubsystemKind k)
	{
		switch (k)
		{
			case SubsystemKind::LaserTurret: return WeaponSpec::Laser();
			case SubsystemKind::HeavyLaser: return WeaponSpec::Heavy();
			case SubsystemKind::Railgun: return WeaponSpec::Railgun();
			case SubsystemKind::MissileLauncher: return WeaponSpec::Missiles();
			case SubsystemKind::PointDefense: return WeaponSpec::PointDefense();
			case SubsystemKind::FighterGuns: return WeaponSpec::FighterGuns();
			case SubsystemKind::Torpedo: return WeaponSpec::Torpedo();
			case SubsystemKind::BoardingPods: return WeaponSpec::Pods();
			case SubsystemKind::Autocannon: return WeaponSpec::Autocannon();
			case SubsystemKind::LightRailgun: return WeaponSpec::LightRailgun();
			case SubsystemKind::EmpLauncher: return WeaponSpec::Emp();
			case SubsystemKind::MineLayer: return WeaponSpec::Mines();
			case SubsystemKind::AutoTurret: return WeaponSpec::AutoTurret();
			case SubsystemKind::TorpedoTube: return WeaponSpec::HeavyTorpedo();
			case SubsystemKind::Countermeasures: return WeaponSpec::Flares();
			case SubsystemKind::TachyonLance: return TachyonLances::Spec();
			case SubsystemKind::PhaserArray: return EnergyWeapons::PhaserLance();
			case SubsystemKind::PlasmaCannon: return EnergyWeapons::PlasmaCannon();
			case SubsystemKind::AntimatterTorpedo: return WeaponSpec::AntimatterTorpedo();
			case SubsystemKind::FlakCannon: return WeaponSpec::Flak();
			case SubsystemKind::InterceptorMissile: return WeaponSpec::Interceptor();
			case SubsystemKind::Coilgun: return SpinalMounts::Coilgun();
			default: return nullptr;
		}
	}

	cs::String SubsystemDef::Name(SubsystemKind k)
	{
		switch (k)
		{
			case SubsystemKind::LaserTurret: return "Laser turret";
			case SubsystemKind::HeavyLaser: return "Heavy laser";
			case SubsystemKind::Railgun: return "Railgun";
			case SubsystemKind::MissileLauncher: return "Missile launcher";
			case SubsystemKind::PointDefense: return "Point defence";
			case SubsystemKind::FighterGuns: return "Guns";
			case SubsystemKind::Torpedo: return "Torpedo";
			case SubsystemKind::Engine: return "Engine";
			case SubsystemKind::ShieldGenerator: return "Shield generator";
			case SubsystemKind::Bridge: return "Bridge";
			case SubsystemKind::Hangar: return "Hangar";
			case SubsystemKind::BoardingPods: return "Boarding pods";
			case SubsystemKind::Drill: return "Drill prow";
			case SubsystemKind::Autocannon: return "Autocannon";
			case SubsystemKind::LightRailgun: return "Light railgun";
			case SubsystemKind::EmpLauncher: return "EMP launcher";
			case SubsystemKind::MineLayer: return "Mine layer";
			case SubsystemKind::AutoTurret: return "Auto turret";
			case SubsystemKind::TorpedoTube: return "Torpedo tube";
			case SubsystemKind::Countermeasures: return "Countermeasures";
			case SubsystemKind::TachyonLance: return "Tachyon lance";
			case SubsystemKind::PhaserArray: return "Phaser array";
			case SubsystemKind::PlasmaCannon: return "Plasma cannon";
			case SubsystemKind::AntimatterTorpedo: return "Antimatter torpedo";
			case SubsystemKind::FlakCannon: return "Flak cannon";
			case SubsystemKind::InterceptorMissile: return "Interceptor missiles";
			case SubsystemKind::Coilgun: return "Coilgun";
			case SubsystemKind::Radiator: return "Radiator";
			default: return "Reactor";
		}
	}

	// ---------------------------------------------------------------- CombatSpec

	// The C# static field initialiser of Specs: every class's designs as drawn.
	cs::Dictionary<ShipClass, cs::List<cs::Ref<CombatSpec>>>& CombatSpec::SpecsField()
	{
		static cs::Dictionary<ShipClass, cs::List<cs::Ref<CombatSpec>>> field = []
		{
			cs::Dictionary<ShipClass, cs::List<cs::Ref<CombatSpec>>> specs;
			{
				auto s = cs::New<CombatSpec>();
				s->DesignName = "Carrier";
				s->LengthKm = 3.2;
				s->MassKt = 40000;
				s->MainThrustMN = 100000;
				s->RetroThrustMN = 75000;
				s->LateralThrustMN = 35000;
				s->VerticalThrustMN = 35000;
				s->PitchYawTorqueMNKm = 4770000;
				s->RollTorqueMNKm = 1190000;
				s->Hull = 26000;
				s->Shield = 11000;
				s->ShieldRegen = 75;
				s->Armor = 6;
				s->MaxSpeed = 0.07;
				s->Accel = 0.0025;
				s->Decel = 0.0025;
				s->BoostSpeedMultiplier = 1.3;
				s->BoostDurationS = 30;
				s->TurnRateDegS = 4;
				s->Squadrons = 3;
				s->ReconCraft = 2;
				s->Shuttles = 2;
				s->Crew = 3800;
				s->DryMassTonnes = 140000;
				s->PropellantTonnes = 70000;
				s->ExhaustVelocityKmS = 4000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::BoardingPods, 0, -0.035, 0.25),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0, 0); d->Buried = true; return d; }(),
					cs::New<SubsystemDef>(SubsystemKind::Hangar, 0, 0.012, 0.44),
					cs::New<SubsystemDef>(SubsystemKind::Hangar, 0.125, -0.01, 0.12),
					cs::New<SubsystemDef>(SubsystemKind::Hangar, -0.125, -0.01, 0.12),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0.09, 0.055, 0.3, false, FireCones::Starboard, 75),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, -0.09, 0.055, 0.3, false, FireCones::Port, 75),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0.12, 0.035, -0.06, false, FireCones::Starboard, 75),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, -0.12, 0.035, -0.06, false, FireCones::Port, 75),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.1, 0.055, 0.42),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.1, 0.055, 0.42),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.12, 0.035, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.12, 0.035, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.05, 0.4),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.08, 0.05, 0.25),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.1, 0.05, 0),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.08, 0.05, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.05, -0.45),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.08, 0.05, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.1, 0.05, 0),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.08, 0.05, 0.25),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.075, 0, -0.49),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.027, 0, -0.49),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.027, 0, -0.49),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.075, 0, -0.49),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, -0.01, -0.3),
					// A dorsal antimatter torpedo launcher on the bow (U.SPC-T489): a capital's heavy guided warhead.
					cs::New<SubsystemDef>(SubsystemKind::AntimatterTorpedo, 0, 0.06, 0.36, false, FireCones::Bow, 45),
				};
				specs[ShipClass::Carrier] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				s->DesignName = "Battleship";
				s->LengthKm = 2.4;
				s->MassKt = 30000;
				s->MainThrustMN = 90000;
				s->RetroThrustMN = 67500;
				s->LateralThrustMN = 31500;
				s->VerticalThrustMN = 31500;
				s->PitchYawTorqueMNKm = 3140000;
				s->RollTorqueMNKm = 785000;
				s->Hull = 30000;
				s->Shield = 13000;
				s->ShieldRegen = 90;
				s->Armor = 10;
				s->MaxSpeed = 0.09;
				s->Accel = 0.003;
				s->Decel = 0.003;
				s->BoostSpeedMultiplier = 1.3;
				s->BoostDurationS = 32;
				s->TurnRateDegS = 5;
				s->Crew = 2400;
				s->DryMassTonnes = 120000;
				s->PropellantTonnes = 60000;
				s->ExhaustVelocityKmS = 4000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::BoardingPods, 0, -0.045, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::HeavyLaser, 0, 0.045, 0.20, false, FireCones::Bow, 60),
					cs::New<SubsystemDef>(SubsystemKind::HeavyLaser, 0, 0.06, 0.07, false, FireCones::Bow, 60),
					cs::New<SubsystemDef>(SubsystemKind::HeavyLaser, 0, 0.045, -0.28, true, FireCones::Stern, 60),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0.06, 0.05, -0.02, false, FireCones::Starboard, 75),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, -0.06, 0.05, -0.02, false, FireCones::Port, 75),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0.07, 0.0, 0.14, false, FireCones::Starboard, 75),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, -0.07, 0.0, 0.14, false, FireCones::Port, 75),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.075, 0.03, 0.27),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.075, 0.03, 0.27),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.075, 0.03, -0.36),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.075, 0.03, -0.36),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0, 0); d->Buried = true; return d; }(),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.06, 0.35),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.07, 0.06, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.09, 0.06, -0.05),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.07, 0.06, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.06, -0.44),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.07, 0.06, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.09, 0.06, -0.05),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.07, 0.06, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.042, 0, -0.48),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.48),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.042, 0, -0.48),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.22),
					// A pair of bow torpedo tubes under the forward heavy battery (U.SPC-T377).
					cs::New<SubsystemDef>(SubsystemKind::TorpedoTube, 0.03, -0.025, 0.46, false, FireCones::Bow, 30),
					cs::New<SubsystemDef>(SubsystemKind::TorpedoTube, -0.03, -0.025, 0.46, false, FireCones::Bow, 30),
					// A dorsal antimatter torpedo launcher ahead of the forward battery (U.SPC-T489).
					cs::New<SubsystemDef>(SubsystemKind::AntimatterTorpedo, 0, 0.05, 0.33, false, FireCones::Bow, 45),
					// The two ship-length spinal mounts it is built around (U.SPC-T490, see SpinalMounts): a railgun down the
					// dorsal keel and a coilgun down the ventral, their muzzles at the bow, each fed by a spinal power plant.
					SpinalMounts::Mount(ModuleKind::Railgun, 0, 0.025, 0.5),
					SpinalMounts::Mount(ModuleKind::Coilgun, 0, -0.04, 0.49),
				};
				specs[ShipClass::Battleship] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				s->DesignName = "Cruiser";
				s->LengthKm = 1.2;
				s->MassKt = 6000;
				s->MainThrustMN = 30000;
				s->RetroThrustMN = 22500;
				s->LateralThrustMN = 12000;
				s->VerticalThrustMN = 12000;
				s->PitchYawTorqueMNKm = 335000;
				s->RollTorqueMNKm = 83800;
				s->Hull = 13000;
				s->Shield = 6000;
				s->ShieldRegen = 54;
				s->Armor = 6;
				s->MaxSpeed = 0.14;
				s->Accel = 0.005;
				s->Decel = 0.006;
				s->BoostSpeedMultiplier = 1.4;
				s->BoostDurationS = 28;
				s->TurnRateDegS = 8;
				s->Crew = 900;
				s->DryMassTonnes = 45000;
				s->PropellantTonnes = 22000;
				s->ExhaustVelocityKmS = 4000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::BoardingPods, 0, -0.03, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0, 0.03, 0.15, false, FireCones::Bow, 60),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0, 0.03, 0.0, false, FireCones::Bow, 60),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0.1, 0.014, -0.05, false, FireCones::Starboard, 75),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, -0.1, 0.014, -0.05, false, FireCones::Port, 75),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.045, 0.025, 0.3),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.045, 0.025, 0.3),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0, 0); d->Buried = true; return d; }(),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.04, 0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.08, 0.04, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.08, 0.04, -0.25),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.04, -0.4),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.08, 0.04, -0.25),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.08, 0.04, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.025, 0, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.025, 0, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.1, 0, -0.225),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.1, 0, -0.225),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.3),
					// One bow torpedo tube in the keel (U.SPC-T377).
					cs::New<SubsystemDef>(SubsystemKind::TorpedoTube, 0, -0.02, 0.45, false, FireCones::Bow, 30),
				};
				specs[ShipClass::Cruiser] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				s->DesignName = "Destroyer";
				s->LengthKm = 0.45;
				s->MassKt = 700;
				s->MainThrustMN = 6300;
				s->RetroThrustMN = 4720;
				s->LateralThrustMN = 2520;
				s->VerticalThrustMN = 2520;
				s->PitchYawTorqueMNKm = 14400;
				s->RollTorqueMNKm = 3610;
				s->Hull = 5500;
				s->Shield = 2500;
				s->ShieldRegen = 30;
				s->Armor = 3;
				s->MaxSpeed = 0.24;
				s->Accel = 0.009;
				s->Decel = 0.011;
				s->BoostSpeedMultiplier = 1.5;
				s->BoostDurationS = 25;
				s->TurnRateDegS = 14;
				s->Crew = 280;
				s->DryMassTonnes = 9000;
				s->PropellantTonnes = 5000;
				s->ExhaustVelocityKmS = 4000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::BoardingPods, 0, -0.035, 0.0),
					// A fixed spinal railgun rather than a turret: the Destroyer's whole identity is
					// closing fast to bring its nose to bear, then landing one hard shot.
					cs::New<SubsystemDef>(SubsystemKind::Railgun, 0, 0.03, 0.3, false, FireCones::Bow, 60),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0, 0.07, -0.25, false, FireCones::Bow, 75),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.05, 0.02, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.05, 0.02, 0.2),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0, 0); d->Buried = true; return d; }(),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, 0.25),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.07, 0.03, -0.1),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, -0.4),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.07, 0.03, -0.1),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.035, 0, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.035, 0, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.25),
					// The anti-fighter screen beside its point defence (U.SPC-T489): a rotary flak cannon on each beam and an
					// interceptor launcher amidships, the umbrella that stops missile swarms and fighters short of the capitals.
					cs::New<SubsystemDef>(SubsystemKind::FlakCannon, 0.06, 0.03, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::FlakCannon, -0.06, 0.03, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::InterceptorMissile, 0, 0.05, -0.15),
				};
				specs[ShipClass::Destroyer] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			// A truss freighter (U.SPC-T482, see BulkFreighters): a bare spine with pods and fuel spheres clamped along it and one
			// big engine astern. No armour and no weapons, since every kilogram goes to cargo (its escorts carry the guns), and
			// a weak drive for its mass, so off the lanes it is slow to speed up and slow down. Its shields only turn debris.
			{
				auto s = cs::New<CombatSpec>();
				s->DesignName = "Freighter";
				s->LengthKm = 0.6;
				s->MassKt = 900;
				s->MainThrustMN = 13500;
				s->RetroThrustMN = 9000;
				s->LateralThrustMN = 7200;
				s->VerticalThrustMN = 7200;
				s->PitchYawTorqueMNKm = 101000;
				s->RollTorqueMNKm = 25200;
				s->Hull = 4500;
				s->Shield = 750;
				s->ShieldRegen = 9;
				s->Armor = 0;
				s->MaxSpeed = 0.05;
				s->Accel = 0.0008;
				s->Decel = 0.0008;
				s->BoostSpeedMultiplier = 1.25;
				s->BoostDurationS = 36;
				s->TurnRateDegS = 3;
				s->Crew = 40;
				s->DryMassTonnes = 9000;
				s->PropellantTonnes = BulkFreighters::StockSpheres * BulkFreighters::SphereTonnes;
				s->ExhaustVelocityKmS = 2500;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.03, 0.43),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.05, 0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.1, 0.05, -0.1),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.05, -0.4),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.1, 0.05, -0.1),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.38),
				};
				specs[ShipClass::Freighter] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				s->DesignName = "Ore hauler";
				s->LengthKm = 0.3;
				s->MassKt = 500;
				s->MainThrustMN = 14000;
				s->RetroThrustMN = 10500;
				s->LateralThrustMN = 5600;
				s->VerticalThrustMN = 5600;
				s->PitchYawTorqueMNKm = 11500;
				s->RollTorqueMNKm = 2860;
				s->Hull = 6500;
				s->Shield = 500;
				s->ShieldRegen = 6;
				s->Armor = 4;
				s->MaxSpeed = 0.04;
				s->Accel = 0.0012;
				s->Decel = 0.0014;
				s->BoostSpeedMultiplier = 1.2;
				s->BoostDurationS = 38;
				s->TurnRateDegS = 3;
				s->Crew = 30;
				s->DryMassTonnes = 8000;
				s->PropellantTonnes = 7000;
				s->ExhaustVelocityKmS = 2200;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.09, 0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.08, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.08, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.05, 0, -0.45),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.05, 0, -0.45),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.3),
				};
				specs[ShipClass::Miner] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				s->DesignName = "Courier";
				s->LengthKm = 0.08;
				s->MassKt = 3;
				s->MainThrustMN = 150;
				s->RetroThrustMN = 113;
				s->LateralThrustMN = 75;
				s->VerticalThrustMN = 75;
				s->PitchYawTorqueMNKm = 34.9;
				s->RollTorqueMNKm = 8.73;
				s->Hull = 1300;
				s->Shield = 600;
				s->ShieldRegen = 12;
				s->Armor = 0;
				s->MaxSpeed = 0.35;
				s->Accel = 0.02;
				s->Decel = 0.024;
				s->BoostSpeedMultiplier = 1.8;
				s->BoostDurationS = 14;
				s->TurnRateDegS = 30;
				s->Crew = 6;
				s->DryMassTonnes = 220;
				s->PropellantTonnes = 180;
				s->ExhaustVelocityKmS = 4500;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					// The "light fighter": the fastest, most agile hull in the game carries the
					// fastest-firing forward gun to match -- rapid, low-damage autocannon fire, not a
					// single hard-hitting shot.
					cs::New<SubsystemDef>(SubsystemKind::Autocannon, 0, -0.01, 0.1, false, FireCones::Bow, 20),
					// Its utility mount: an EMP launcher to stop what it cannot outgun (U.SPC-T271).
					cs::New<SubsystemDef>(SubsystemKind::EmpLauncher, 0, -0.03, 0),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.035, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, 0.03),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, -0.2),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.44),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.2),
				};
				specs[ShipClass::Courier] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				s->DesignName = "Fighter";
				s->LengthKm = 0.1;
				s->MassKt = 4;
				s->MainThrustMN = 220;
				s->RetroThrustMN = 165;
				s->LateralThrustMN = 110;
				s->VerticalThrustMN = 110;
				s->PitchYawTorqueMNKm = 120;
				s->RollTorqueMNKm = 30;
				s->Hull = 1800;
				s->Shield = 700;
				s->ShieldRegen = 12;
				s->Armor = 1;
				s->MaxSpeed = 0.32;
				s->Accel = 0.018;
				s->Decel = 0.02;
				s->BoostSpeedMultiplier = 1.7;
				s->BoostDurationS = 16;
				s->TurnRateDegS = 26;
				s->Crew = 2;
				s->DryMassTonnes = 60;
				s->PropellantTonnes = 30;
				s->ExhaustVelocityKmS = 3000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					// A dedicated dogfighter: twin forward guns rather than the Courier's single gun,
					// traded against a lighter hull than either existing "fighter" archetype.
					cs::New<SubsystemDef>(SubsystemKind::Autocannon, -0.09, 0, 0.1, false, FireCones::Bow, 20),
					cs::New<SubsystemDef>(SubsystemKind::Autocannon, 0.09, 0, 0.1, false, FireCones::Bow, 20),
					// Its utility mount: a missile rack (U.SPC-T271).
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0, -0.03, 0),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.035, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, 0.03),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, -0.2),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.44),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.2),
				};
				specs[ShipClass::Fighter] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The deep-space explorer (U.SPC-T419): long-legged rather than fast or tough, a big reactor and tanks
				// for its FTL drive and months out between the stars, a light gun and heavy shields for what it meets there.
				s->DesignName = "Explorer";
				s->LengthKm = 0.22;
				s->MassKt = 60;
				s->MainThrustMN = 1800;
				s->RetroThrustMN = 1350;
				s->LateralThrustMN = 900;
				s->VerticalThrustMN = 900;
				s->PitchYawTorqueMNKm = 900;
				s->RollTorqueMNKm = 225;
				s->Hull = 2600;
				s->Shield = 1600;
				s->ShieldRegen = 20;
				s->Armor = 2;
				s->MaxSpeed = 0.3;
				s->Accel = 0.012;
				s->Decel = 0.014;
				s->BoostSpeedMultiplier = 1.5;
				s->BoostDurationS = 18;
				s->TurnRateDegS = 20;
				s->Crew = 40;
				s->DryMassTonnes = 2400;
				s->PropellantTonnes = 2600;
				s->ExhaustVelocityKmS = 4500;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::Autocannon, 0, -0.02, 0.35, false, FireCones::Bow, 30),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0, 0.05, 0.1),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.06, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.04, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.04, -0.25),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.04, 0, -0.46),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.04, 0, -0.46),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.2),
				};
				specs[ShipClass::Explorer] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The heavy fighter (U.SPC-T467): an armoured multi-crew brawler, slower and stiffer than a light fighter
				// but plated like a small warship, with a spinal light railgun for corvette armour and two crewed laser
				// turrets (dorsal and ventral, all round) that keep firing whichever way the pilot points the nose.
				s->DesignName = "Gunship";
				s->LengthKm = 0.14;
				s->MassKt = 80;
				s->MainThrustMN = 4000;
				s->RetroThrustMN = 3000;
				s->LateralThrustMN = 2000;
				s->VerticalThrustMN = 2000;
				s->PitchYawTorqueMNKm = 1900;
				s->RollTorqueMNKm = 475;
				s->Hull = 2600;
				s->Shield = 1100;
				s->ShieldRegen = 16;
				s->Armor = 10;
				s->MaxSpeed = 0.3;
				s->Accel = 0.013;
				s->Decel = 0.015;
				s->BoostSpeedMultiplier = 1.6;
				s->BoostDurationS = 20;
				s->TurnRateDegS = 20;
				s->Crew = 8;
				s->DryMassTonnes = 1400;
				s->PropellantTonnes = 700;
				s->ExhaustVelocityKmS = 3500;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::LightRailgun, 0, -0.02, 0.4, false, FireCones::Bow, 20),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0, 0.06, 0.1),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0, -0.06, -0.05),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0, 0.06, -0.2),
					// Its utility mount: a missile rack (a small craft's, see Loadout.IsSmallCraft).
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0, -0.04, 0.15),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.05, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.04, 0.15),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.04, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.06, 0, -0.46),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.06, 0, -0.46),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.25),
				};
				specs[ShipClass::Gunship] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The ore tug (U.SPC-T483): a big engine with claws and almost nothing else. Unladen it is quick for its size
				// (three main drives on a short frame); latched onto a rock of millions of tonnes it barely creeps (see
				// BeltMining.TugAccel). No hold, no guns: a skeleton crew, plate over the reactor, and shields for the grit.
				s->DesignName = "Ore tug";
				s->LengthKm = 0.2;
				s->MassKt = 200;
				s->MainThrustMN = 12000;
				s->RetroThrustMN = 6000;
				s->LateralThrustMN = 2400;
				s->VerticalThrustMN = 2400;
				s->PitchYawTorqueMNKm = 2000;
				s->RollTorqueMNKm = 500;
				s->Hull = 5000;
				s->Shield = 400;
				s->ShieldRegen = 5;
				s->Armor = 6;
				s->MaxSpeed = 0.05;
				s->Accel = 0.0016;
				s->Decel = 0.0012;
				s->BoostSpeedMultiplier = 1.2;
				s->BoostDurationS = 30;
				s->TurnRateDegS = 4;
				s->Crew = 12;
				s->DryMassTonnes = 3000;
				s->PropellantTonnes = 2500;
				s->ExhaustVelocityKmS = 3000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.08, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.07, 0.25),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.07, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.08, 0, -0.45),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.08, 0, -0.45),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.25),
				};
				specs[ShipClass::OreTug] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The interceptor (U.SPC-T484): smaller, faster and quicker to turn than the Fighter, one pilot, light twin
				// guns and a missile rack, and thin plating: it hunts craft and screens bombers, it does not trade blows.
				s->DesignName = "Interceptor";
				s->LengthKm = 0.07;
				s->MassKt = 2.5;
				s->MainThrustMN = 175;
				s->RetroThrustMN = 130;
				s->LateralThrustMN = 95;
				s->VerticalThrustMN = 95;
				s->PitchYawTorqueMNKm = 60;
				s->RollTorqueMNKm = 15;
				s->Hull = 1100;
				s->Shield = 450;
				s->ShieldRegen = 10;
				s->Armor = 0;
				s->MaxSpeed = 0.36;
				s->Accel = 0.024;
				s->Decel = 0.026;
				s->BoostSpeedMultiplier = 1.8;
				s->BoostDurationS = 12;
				s->TurnRateDegS = 32;
				s->Crew = 1;
				s->DryMassTonnes = 35;
				s->PropellantTonnes = 25;
				s->ExhaustVelocityKmS = 3200;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::Autocannon, -0.07, 0, 0.12, false, FireCones::Bow, 20),
					cs::New<SubsystemDef>(SubsystemKind::Autocannon, 0.07, 0, 0.12, false, FireCones::Bow, 20),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0, -0.03, 0),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.03, 0.08),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.025, 0.03),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.025, -0.2),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.44),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.2),
				};
				specs[ShipClass::Interceptor] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The bomber (U.SPC-T484): slow, heavily plated and shielded, a pilot, a bombardier and a tail gunner, with
				// a pair of torpedo tubes for capital ships (they hit hardest once a capital's shields are down, see
				// StrikeTorpedoes), a defensive gun and point defence astern. It needs an escort.
				s->DesignName = "Bomber";
				s->LengthKm = 0.14;
				s->MassKt = 9;
				s->MainThrustMN = 300;
				s->RetroThrustMN = 225;
				s->LateralThrustMN = 110;
				s->VerticalThrustMN = 110;
				s->PitchYawTorqueMNKm = 380;
				s->RollTorqueMNKm = 95;
				s->Hull = 3200;
				s->Shield = 1100;
				s->ShieldRegen = 14;
				s->Armor = 3;
				s->MaxSpeed = 0.26;
				s->Accel = 0.012;
				s->Decel = 0.013;
				s->BoostSpeedMultiplier = 1.5;
				s->BoostDurationS = 12;
				s->TurnRateDegS = 18;
				s->Crew = 3;
				s->DryMassTonnes = 160;
				s->PropellantTonnes = 70;
				s->ExhaustVelocityKmS = 3000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::TorpedoTube, -0.08, -0.03, 0.1, false, FireCones::Bow, 20),
					cs::New<SubsystemDef>(SubsystemKind::TorpedoTube, 0.08, -0.03, 0.1, false, FireCones::Bow, 20),
					cs::New<SubsystemDef>(SubsystemKind::Autocannon, 0, -0.02, 0.3, false, FireCones::Bow, 20),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0, 0.04, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.04, 0.15),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.035, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.035, -0.2),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.05, 0, -0.45),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.05, 0, -0.45),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.25),
				};
				specs[ShipClass::Bomber] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The recon / EW fighter (U.SPC-T485): an interceptor's airframe stretched round a sensor and jamming suite,
				// a pilot and an electronic warfare officer, and one light gun. It scouts, jams and paints targets for the
				// bombers (ElectronicWarfare); it does not fight.
				s->DesignName = "Recon";
				s->LengthKm = 0.08;
				s->MassKt = 2.8;
				s->MainThrustMN = 190;
				s->RetroThrustMN = 140;
				s->LateralThrustMN = 95;
				s->VerticalThrustMN = 95;
				s->PitchYawTorqueMNKm = 70;
				s->RollTorqueMNKm = 18;
				s->Hull = 900;
				s->Shield = 500;
				s->ShieldRegen = 12;
				s->Armor = 0;
				s->MaxSpeed = 0.36;
				s->Accel = 0.024;
				s->Decel = 0.026;
				s->BoostSpeedMultiplier = 1.8;
				s->BoostDurationS = 14;
				s->TurnRateDegS = 30;
				s->Crew = 2;
				s->DryMassTonnes = 40;
				s->PropellantTonnes = 30;
				s->ExhaustVelocityKmS = 3300;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::Autocannon, 0, -0.02, 0.15, false, FireCones::Bow, 20),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.03, 0.1),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.025, 0.03),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.025, -0.2),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.44),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.2),
				};
				specs[ShipClass::Recon] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The boarding shuttle (U.SPC-T486): a pilot and a loadmaster up front and a troop bay behind them for one
				// marine team, a docking collar on its flank. Armoured and shielded to get the team across, slower than a
				// bomber, and unarmed but for point defence: it is escorted in, not fought with.
				s->DesignName = "Assault shuttle";
				s->LengthKm = 0.12;
				s->MassKt = 12;
				s->MainThrustMN = 300;
				s->RetroThrustMN = 225;
				s->LateralThrustMN = 120;
				s->VerticalThrustMN = 120;
				s->PitchYawTorqueMNKm = 420;
				s->RollTorqueMNKm = 105;
				s->Hull = 3000;
				s->Shield = 900;
				s->ShieldRegen = 10;
				s->Armor = 4;
				s->MaxSpeed = 0.22;
				s->Accel = 0.01;
				s->Decel = 0.012;
				s->BoostSpeedMultiplier = 1.4;
				s->BoostDurationS = 10;
				s->TurnRateDegS = 14;
				s->Crew = 2;
				s->Marines = 24;
				s->DryMassTonnes = 200;
				s->PropellantTonnes = 80;
				s->ExhaustVelocityKmS = 3000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0, 0.045, -0.1),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.04, 0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.035, 0.1),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.035, -0.2),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.05, 0, -0.45),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.05, 0, -0.45),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.28),
				};
				specs[ShipClass::AssaultShuttle] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The gate picket (U.SPC-T480): a sensor mast with a drive, not a warship. A light gun and point defence for
				// what strays near it, good shields to last until word is out, and a long-range search suite (see
				// SensorSuite.Picket) that is the whole reason it is there.
				s->DesignName = "Gate picket";
				s->LengthKm = 0.12;
				s->MassKt = 20;
				s->MainThrustMN = 700;
				s->RetroThrustMN = 520;
				s->LateralThrustMN = 350;
				s->VerticalThrustMN = 350;
				s->PitchYawTorqueMNKm = 260;
				s->RollTorqueMNKm = 65;
				s->Hull = 1500;
				s->Shield = 1100;
				s->ShieldRegen = 16;
				s->Armor = 1;
				s->MaxSpeed = 0.3;
				s->Accel = 0.014;
				s->Decel = 0.016;
				s->BoostSpeedMultiplier = 1.5;
				s->BoostDurationS = 16;
				s->TurnRateDegS = 22;
				s->Crew = 14;
				s->DryMassTonnes = 700;
				s->PropellantTonnes = 500;
				s->ExhaustVelocityKmS = 3500;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::Autocannon, 0, -0.02, 0.3, false, FireCones::Bow, 30),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0, 0.05, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.04, 0.15),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.04, 0.15),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.04, -0.25),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.46),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.2),
				};
				specs[ShipClass::GatePicket] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The lane tender (U.SPC-T481): a maintenance hull that keeps a hyperlane's rings powered and aligned. No
				// guns at all: a big reactor to feed the rings it works on, heavy shields and plate to ride out what comes for
				// it, and a crew of engineers rather than gunners.
				s->DesignName = "Lane tender";
				s->LengthKm = 0.26;
				s->MassKt = 120;
				s->MainThrustMN = 3600;
				s->RetroThrustMN = 2700;
				s->LateralThrustMN = 1800;
				s->VerticalThrustMN = 1800;
				s->PitchYawTorqueMNKm = 2200;
				s->RollTorqueMNKm = 550;
				s->Hull = 3400;
				s->Shield = 1500;
				s->ShieldRegen = 16;
				s->Armor = 3;
				s->MaxSpeed = 0.2;
				s->Accel = 0.008;
				s->Decel = 0.01;
				s->BoostSpeedMultiplier = 1.3;
				s->BoostDurationS = 24;
				s->TurnRateDegS = 12;
				s->Crew = 30;
				s->DryMassTonnes = 3600;
				s->PropellantTonnes = 3000;
				s->ExhaustVelocityKmS = 3500;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.06, 0.32),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.05, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.05, -0.25),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.05, 0, -0.46),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.05, 0, -0.46),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.1),
				};
				specs[ShipClass::LaneTender] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The command cruiser (U.SPC-T477): a battlecruiser's hull between a cruiser and a battleship, whose space and
				// power go to the sensor array, the communications suite and the targeting computers inside it (see
				// FacilityLayouts and FleetCoordination) rather than guns: a cruiser's battery, no torpedoes and no boarding
				// pods, but heavy shields, two reactors and a big crew with the fleet's staff aboard.
				s->DesignName = "Command cruiser";
				s->LengthKm = 1.6;
				s->MassKt = 12000;
				s->MainThrustMN = 48000;
				s->RetroThrustMN = 36000;
				s->LateralThrustMN = 18000;
				s->VerticalThrustMN = 18000;
				s->PitchYawTorqueMNKm = 900000;
				s->RollTorqueMNKm = 225000;
				s->Hull = 17000;
				s->Shield = 9000;
				s->ShieldRegen = 70;
				s->Armor = 7;
				s->MaxSpeed = 0.12;
				s->Accel = 0.0045;
				s->Decel = 0.005;
				s->BoostSpeedMultiplier = 1.35;
				s->BoostDurationS = 30;
				s->TurnRateDegS = 7;
				s->Crew = 1400;
				s->DryMassTonnes = 70000;
				s->PropellantTonnes = 35000;
				s->ExhaustVelocityKmS = 4000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0, 0.04, 0.22, false, FireCones::Bow, 60),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0, 0.04, -0.3, true, FireCones::Stern, 60),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0.09, 0.02, 0.0, false, FireCones::Starboard, 75),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, -0.09, 0.02, 0.0, false, FireCones::Port, 75),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.05, 0.03, 0.3),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.05, 0.03, 0.3),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.06, 0.03, -0.32),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.06, 0.03, -0.32),
					// The flag bridge, buried at the centre of the hull under the array's mast (U.SPC-T492).
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0, 0); d->Buried = true; return d; }(),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.05, 0.34),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.08, 0.05, 0.12),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.08, 0.05, -0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.05, -0.42),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.08, 0.05, -0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.08, 0.05, 0.12),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.04, 0, -0.48),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.48),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.04, 0, -0.48),
					// Two reactors: the array and the suite draw as much as a battery would.
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.1),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.3),
				};
				specs[ShipClass::CommandCruiser] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The frigate (U.SPC-T475, see AlphaStrike): smaller and lighter than a destroyer, with a quarter again its
				// drive on half its mass, so it is the fastest warship there is; it pays for it in plate (thin hull, light
				// shields, almost no armour) and in guns (none but point defence). Its whole battery is forward-firing
				// launchers fed from deep magazines: four long-range anti-ship missile racks and two antimatter (photon)
				// torpedo launchers, loosed together in one alpha strike.
				s->DesignName = "Frigate";
				s->LengthKm = 0.32;
				s->MassKt = 350;
				s->MainThrustMN = 7000;
				s->RetroThrustMN = 5250;
				s->LateralThrustMN = 2800;
				s->VerticalThrustMN = 2800;
				s->PitchYawTorqueMNKm = 5400;
				s->RollTorqueMNKm = 1350;
				s->Hull = 2600;
				s->Shield = 1400;
				s->ShieldRegen = 20;
				s->Armor = 1;
				s->MaxSpeed = 0.38;
				s->Accel = 0.02;
				s->Decel = 0.022;
				s->BoostSpeedMultiplier = 1.6;
				s->BoostDurationS = 30;
				s->TurnRateDegS = 20;
				s->Crew = 120;
				s->DryMassTonnes = 4500;
				s->PropellantTonnes = 4000;
				s->ExhaustVelocityKmS = 4000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					// The anti-ship missile racks, either side of the spine, bearing forward.
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0.06, 0.035, 0.15, false, FireCones::Bow, 75); d->Weapon = AlphaStrike::AntiShipMissile(); return d; }(),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, -0.06, 0.035, 0.15, false, FireCones::Bow, 75); d->Weapon = AlphaStrike::AntiShipMissile(); return d; }(),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0.06, -0.035, 0.15, false, FireCones::Bow, 75); d->Weapon = AlphaStrike::AntiShipMissile(); return d; }(),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, -0.06, -0.035, 0.15, false, FireCones::Bow, 75); d->Weapon = AlphaStrike::AntiShipMissile(); return d; }(),
					// Two antimatter (photon) torpedo launchers in the bow.
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::AntimatterTorpedo, 0.03, 0, 0.42, false, FireCones::Bow, 45); d->Weapon = AlphaStrike::MagazineTorpedo(); return d; }(),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::AntimatterTorpedo, -0.03, 0, 0.42, false, FireCones::Bow, 45); d->Weapon = AlphaStrike::MagazineTorpedo(); return d; }(),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.04, 0.03, 0.28),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.04, 0.03, -0.2),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0, 0); d->Buried = true; return d; }(),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.03, -0.3),
					// Engine-heavy: four big bells astern, a third of the hull's length in engine rooms.
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.045, 0.025, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.045, 0.025, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.045, -0.025, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.045, -0.025, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.32),
				};
				specs[ShipClass::Frigate] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The corvette (U.SPC-T476, see Corvettes): system defence with no FTL drive, the weight a drive room would
				// take going to patrol instead. Smaller than a frigate, it carries tanks as deep as its dry mass for weeks
				// on its beat, a patrol search suite (Sensors), and a brawler's battery for the smugglers and raiders it
				// stops: a spinal light railgun, two laser turrets that bear all round, a missile rack and point defence,
				// behind a destroyer's plate.
				s->DesignName = "Corvette";
				s->LengthKm = 0.22;
				s->MassKt = 160;
				s->MainThrustMN = 3300;
				s->RetroThrustMN = 2480;
				s->LateralThrustMN = 1400;
				s->VerticalThrustMN = 1400;
				s->PitchYawTorqueMNKm = 3200;
				s->RollTorqueMNKm = 800;
				s->Hull = 3200;
				s->Shield = 1500;
				s->ShieldRegen = 20;
				s->Armor = 3;
				s->MaxSpeed = 0.3;
				s->Accel = 0.014;
				s->Decel = 0.016;
				s->BoostSpeedMultiplier = 1.5;
				s->BoostDurationS = 24;
				s->TurnRateDegS = 18;
				s->Crew = 70;
				s->DryMassTonnes = 2600;
				s->PropellantTonnes = 2600;
				s->ExhaustVelocityKmS = 4000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					// The spinal light railgun for a fleeing hull's drive, and the laser turrets dorsal and ventral.
					cs::New<SubsystemDef>(SubsystemKind::LightRailgun, 0, -0.02, 0.4, false, FireCones::Bow, 20),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0, 0.05, 0.15),
					cs::New<SubsystemDef>(SubsystemKind::LaserTurret, 0, -0.05, -0.1),
					cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0, 0.05, -0.2, false, FireCones::Bow, 75),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.04, 0.03, 0.25),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.04, 0.03, -0.25),
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.05, 0.05),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.035, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.035, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.04, 0, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.04, 0, -0.47),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.28),
				};
				specs[ShipClass::Corvette] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The gate breaker (U.SPC-T478, see GateBreakers): a siege capital shorter than a battleship and heavier, all
				// of the difference plate, so it takes a gate's forts' fire as it closes and is the slowest warship hull. Its
				// siege guns are brawling batteries of plasma cannons, a pair in the bow and three down either beam and
				// nothing longer-ranged, so it fights inside their reach; its shuttle deck keeps two boarding shuttles to
				// put its boarding teams aboard a silenced fort or gate.
				s->DesignName = "Gate breaker";
				s->LengthKm = 2.0;
				s->MassKt = 34000;
				s->MainThrustMN = 85000;
				s->RetroThrustMN = 63750;
				s->LateralThrustMN = 29750;
				s->VerticalThrustMN = 29750;
				s->PitchYawTorqueMNKm = 2300000;
				s->RollTorqueMNKm = 575000;
				s->Hull = 34000;
				s->Shield = 12000;
				s->ShieldRegen = 80;
				s->Armor = 14;
				s->MaxSpeed = 0.075;
				s->Accel = 0.0025;
				s->Decel = 0.0025;
				s->BoostSpeedMultiplier = 1.25;
				s->BoostDurationS = 30;
				s->TurnRateDegS = 4.5;
				s->Shuttles = 2;
				s->Crew = 2200;
				s->DryMassTonnes = 135000;
				s->PropellantTonnes = 60000;
				s->ExhaustVelocityKmS = 4000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::BoardingPods, 0, -0.05, 0.3),
					// The shuttle deck, ventral amidships: two boarding shuttles and their marine teams.
					cs::New<SubsystemDef>(SubsystemKind::Hangar, 0, -0.045, -0.05),
					// The siege batteries: plasma cannons, a pair bearing ahead and three down either beam.
					cs::New<SubsystemDef>(SubsystemKind::PlasmaCannon, 0.04, 0.05, 0.3, false, FireCones::Bow, 60),
					cs::New<SubsystemDef>(SubsystemKind::PlasmaCannon, -0.04, 0.05, 0.3, false, FireCones::Bow, 60),
					cs::New<SubsystemDef>(SubsystemKind::PlasmaCannon, 0.085, 0.03, 0.15, false, FireCones::Starboard, 75),
					cs::New<SubsystemDef>(SubsystemKind::PlasmaCannon, 0.085, 0.03, -0.02, false, FireCones::Starboard, 75),
					cs::New<SubsystemDef>(SubsystemKind::PlasmaCannon, 0.085, 0.03, -0.2, false, FireCones::Starboard, 75),
					cs::New<SubsystemDef>(SubsystemKind::PlasmaCannon, -0.085, 0.03, 0.15, false, FireCones::Port, 75),
					cs::New<SubsystemDef>(SubsystemKind::PlasmaCannon, -0.085, 0.03, -0.02, false, FireCones::Port, 75),
					cs::New<SubsystemDef>(SubsystemKind::PlasmaCannon, -0.085, 0.03, -0.2, false, FireCones::Port, 75),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.07, 0.04, 0.36),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.07, 0.04, 0.36),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.07, 0.04, -0.34),
					cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.07, 0.04, -0.34),
					[] { auto d = cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0, 0); d->Buried = true; return d; }(),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.065, 0.35),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.07, 0.065, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.09, 0.065, -0.05),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.07, 0.065, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.065, -0.44),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.07, 0.065, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.09, 0.065, -0.05),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.07, 0.065, 0.2),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.045, 0, -0.48),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0, 0, -0.48),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.045, 0, -0.48),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.25),
				};
				specs[ShipClass::GateBreaker] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			{
				auto s = cs::New<CombatSpec>();
				// The capture tug (U.SPC-T522): an ore tug's claws on a frame half its length, two drives instead of three. It
				// chases down and matches a tumbling belt rock of 40-140 m and pushes it in to its refinery; latched onto a
				// few million tonnes it creeps too (see Ship.AccelG). No hold, no guns: a small crew and shields for the grit.
				s->DesignName = "Capture tug";
				s->LengthKm = 0.12;
				s->MassKt = 80;
				s->MainThrustMN = 5600;
				s->RetroThrustMN = 2800;
				s->LateralThrustMN = 1400;
				s->VerticalThrustMN = 1400;
				s->PitchYawTorqueMNKm = 900;
				s->RollTorqueMNKm = 240;
				s->Hull = 2600;
				s->Shield = 220;
				s->ShieldRegen = 4;
				s->Armor = 4;
				s->MaxSpeed = 0.07;
				s->Accel = 0.0024;
				s->Decel = 0.0018;
				s->BoostSpeedMultiplier = 1.2;
				s->BoostDurationS = 30;
				s->TurnRateDegS = 6;
				s->Crew = 8;
				s->DryMassTonnes = 1200;
				s->PropellantTonnes = 1000;
				s->ExhaustVelocityKmS = 3000;
				s->Layout = cs::List<cs::Ref<SubsystemDef>>{
					cs::New<SubsystemDef>(SubsystemKind::Bridge, 0, 0.07, 0.18),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.06, 0.22),
					cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.06, -0.3),
					cs::New<SubsystemDef>(SubsystemKind::Engine, -0.06, 0, -0.46),
					cs::New<SubsystemDef>(SubsystemKind::Engine, 0.06, 0, -0.46),
					cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, -0.25),
				};
				specs[ShipClass::CaptureTug] = cs::List<cs::Ref<CombatSpec>>{ s };
			}
			return specs;
		}();
		return field;
	}

	// C# runs the static constructor once, after the field initialiser and before any static member is used; code it
	// reaches that reads Specs again (on the same thread) sees the field already set, as here.
	const cs::Dictionary<ShipClass, cs::List<cs::Ref<CombatSpec>>>& CombatSpec::Specs()
	{
		auto& specs = SpecsField();
		static bool constructed = false;
		if (!constructed)
		{
			constructed = true;
			StaticConstructor(specs);
		}
		return specs;
	}

	void CombatSpec::StaticConstructor(cs::Dictionary<ShipClass, cs::List<cs::Ref<CombatSpec>>>& specs)
	{
		for (auto [key, value] : specs)
			for (const auto& design : value)
			{
				design->Marines = FacilityLayouts::Complement(key, design);
				// The spinal power plants it is designed with (U.SPC-T490).
				design->SpinalPlants = FacilityLayouts::CountOf(FacilityLayouts::For(key), FacilityKinds::SpinalPlant);
				// A big ship's radiator panels (U.SPC-T491) go on after its own hardpoints, so theirs keep their places.
				design->Layout = Radiators::Fit(key, design->Layout);
			}
	}

	cs::Ref<CombatSpec> CombatSpec::Clone() const
	{
		// MemberwiseClone, then the hardpoint list its own array: copying the cs::List copies the array of Refs.
		auto c = cs::New<CombatSpec>(*this);
		c->Layout = Layout;
		return c;
	}

	const cs::Ref<CombatSpec>& CombatSpec::Of(ShipClass c)
	{
		const auto& specs = Specs();
		return specs.ContainsKey(c) ? specs[c][0] : specs[ShipClass::Destroyer][0];
	}

	const cs::List<cs::Ref<CombatSpec>>& CombatSpec::DesignsOf(ShipClass c)
	{
		static const cs::List<cs::Ref<CombatSpec>> empty; // Array.Empty<CombatSpec>()
		const auto& specs = Specs();
		return specs.ContainsKey(c) ? specs[c] : empty;
	}

	cs::Ref<CombatSpec> CombatSpec::RandomOf(ShipClass c, cs::Random& rng)
	{
		const auto& specs = Specs();
		const auto& designs = specs.ContainsKey(c) ? specs[c] : specs[ShipClass::Destroyer];
		return designs[rng.Next(designs.Count())];
	}

	const cs::Ref<CombatSpec>& CombatSpec::Platform()
	{
		static const cs::Ref<CombatSpec> field = []
		{
			auto s = cs::New<CombatSpec>();
			s->DesignName = "Defence platform";
			s->LengthKm = PlatformLengthKm;
			s->Hull = 21000;
			s->Shield = 11000;
			s->ShieldRegen = 66;
			s->Armor = 8;
			s->MaxSpeed = 0;
			s->Accel = 0;
			s->Decel = 0;
			s->TurnRateDegS = 0;
			// Its garrison is what its barracks quarter (U.SPC-T465).
			s->Crew = 220;
			s->Marines = FacilityLayouts::Complement(FacilityLayouts::Platform(), 1);
			s->Layout = cs::List<cs::Ref<SubsystemDef>>{
				cs::New<SubsystemDef>(SubsystemKind::HeavyLaser, 0, 0.18, 0.12),
				cs::New<SubsystemDef>(SubsystemKind::HeavyLaser, 0, -0.18, -0.12),
				cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, 0.3, 0, 0),
				cs::New<SubsystemDef>(SubsystemKind::MissileLauncher, -0.3, 0, 0),
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.2, 0.1, 0.25),
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.2, 0.1, -0.25),
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, 0.2, -0.1, -0.25),
				cs::New<SubsystemDef>(SubsystemKind::PointDefense, -0.2, -0.1, 0.25),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.25, 0.2),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0.25, 0.25, 0),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, 0, 0.25, -0.2),
				cs::New<SubsystemDef>(SubsystemKind::ShieldGenerator, -0.25, 0.25, 0),
				cs::New<SubsystemDef>(SubsystemKind::Reactor, 0, 0, 0),
			};
			return s;
		}();
		return field;
	}
}
