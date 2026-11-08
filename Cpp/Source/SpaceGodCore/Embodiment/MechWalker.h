#pragma once
// US-T83: the walking mech of Unreal.Soulslike (UNR-T68, ASoulsMech), ported as an embodiable of its own: a two-legged war
// machine that walks a hull's plating on mag boots, twists its torso on its own, fires its arm autocannons and shoulder
// missile pods, and lights its jump jets (Space held) to fly free in open space and set down on any hull it reaches.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;

	/// <summary>
	/// A mech out of a hull's bay (US-T83), as an <see cref="IEmbodiable"/> of <see cref="EmbodimentKind.Mech"/> on
	/// <see cref="EmbodimentInputProfile.Walker"/>. <see cref="Deploy"/> sends it out of a capital ship's or a station's bay
	/// in a fight onto its dorsal plating. On a hull (<see cref="Hull"/>) it walks as the Soulslike mech walked its island:
	/// W/S throttle the legs with weight and inertia (Shift runs), A/D turn them, slower at speed, and the mouse twists the
	/// torso up to <see cref="TorsoTwistLimitDeg"/> off the legs and pitches the gun arms; mag boots hold it to the plating
	/// wherever the hull goes. Space held lights the jump jets (<see cref="EmbodimentInput.JumpJets"/>): off the plating
	/// along its roof, then in open space they push it along the look (W/S A/D Q/E steer the push, Shift the jets at full)
	/// on a tank of <see cref="JetFuelS"/> seconds of burn that refills on a hull. Let go, it coasts; coasting onto any hull
	/// big enough to stand on, its boots catch and it sets down, a hard landing (faster than <see cref="LandSafeKmS"/>)
	/// denting its armour, a crash (<see cref="CrashKmS"/>) wrecking it. Left mouse (held) fires the twin arm autocannons in
	/// turn, real rounds in the fight (<see cref="Battle.Rounds"/>) that hit the first hostile hull they cross; right mouse
	/// ripples a four-missile salvo from the shoulder pods, homing on the hostile nearest the aim (straight rockets when none
	/// is near it). Both build heat on one scale (<see cref="Heat"/>, overheated at 100 they lock out until it falls to
	/// <see cref="CoolResumeHeat"/>). Battle km and seconds throughout; nothing drawn at random, so co-op and replays step it
	/// alike. Only ever stepped while held, as any seat: crews, the AI and the Bench never see it. Not saved, as an EVA pilot
	/// is not: a load drops the player back in command.
	/// </summary>
	class SPACEGODCORE_API MechWalker final : public IEmbodiable
	{
	public:
		cs::Ref<Core::Battle> Battle;

		/// <summary>The hull whose bay it came out of, and who it fights for.</summary>
		cs::Ref<BattleUnit> Carrier;
		int Side = 0;
		cs::String Callsign;

		/// <summary>The hull its mag boots hold it to, or null in flight; where on it (hull frame, hull lengths, on the plating)
		/// and which way its legs face along the plating (hull frame, unit).</summary>
		cs::Ref<BattleUnit> Hull;
		DVec3 Local, Legs = DVec3(0, 0, 1);

		/// <summary>Where it is and how fast (battle frame, km and km/s), its legs' facing and its roof (world, unit); kept up on
		/// a hull too, from the plating.</summary>
		DVec3 Pos, Vel, Facing = DVec3(0, 0, 1), Up = DVec3(0, 1, 0);

		/// <summary>Where the torso and gun arms point (world, unit).</summary>
		DVec3 AimDir = DVec3(0, 0, 1);

		/// <summary>The legs' speed along their facing (km/s, negative backing), the torso's twist off the legs and the arms'
		/// pitch off the plating (degrees, for the view and the HUD).</summary>
		double Speed = 0, TorsoYawDeg = 0, AimPitchDeg = 0;

		/// <summary>The walk cycle, in strides (each whole number one left and one right footfall), and footfalls so far.</summary>
		double GaitPhase = 0;
		int Footfalls = 0;

		/// <summary>The jump jets: lit this step, and seconds of burn left in the tank.</summary>
		bool JetsLit = false;
		double JetFuel = 0;

		/// <summary>Armour left (of <see cref="MaxArmor"/>), the weapons' shared heat (0..100) and whether it has locked them out.</summary>
		double Armor = 0, Heat = 0;
		bool Overheated = false;

		int CannonAmmo = 0, Missiles = 0;
		int ShellsFired = 0, MissilesFired = 0, Landings = 0;

		/// <summary>Seconds each arm's muzzle flash has left (0 the left arm, 1 the right), and since it last set down.</summary>
		double MuzzleFlash[2] = { 0, 0 };
		double SinceLanding = 10;

		/// <summary>The last thing that happened, for the HUD (and how long it shows); why it was lost, once it is.</summary>
		cs::String Event;
		double EventS = 0;
		cs::String Lost;

		// ---- tuning: the Soulslike mech's own numbers (cm and cm/s there), in km here; its jets are new (US-T83) ----

		/// <summary>Its height, antenna tips to soles (7.2 m).</summary>
		static constexpr double HeightKm = 0.0072;
		static constexpr double MaxForwardKmS = 0.0052;
		static constexpr double MaxReverseKmS = 0.0026;
		static constexpr double RunFactor = 1.6;
		static constexpr double AccelKmS2 = 0.003;
		static constexpr double DecelKmS2 = 0.0052;
		/// <summary>Leg turn rate, degrees a second standing and at full speed.</summary>
		static constexpr double TurnRateStillDeg = 62;
		static constexpr double TurnRateFullDeg = 34;
		static constexpr double TorsoTwistLimitDeg = 100;
		static constexpr double TorsoTwistRateDeg = 130;
		/// <summary>How far the arms pitch off the plating (down, up): more upward than on the island, for hulls overhead.</summary>
		static constexpr double AimPitchMinDeg = -25;
		static constexpr double AimPitchMaxDeg = 80;
		static constexpr double AimPitchRateDeg = 110;
		/// <summary>One stride (a left and a right step), km.</summary>
		static constexpr double StrideKm = 0.0052;

		static constexpr double MaxArmor = 1400;
		static constexpr double CannonDamage = 14;
		static constexpr double CannonRate = 7;
		static constexpr double CannonHeat = 3;
		static constexpr double CannonRangeKm = 2.0;
		static constexpr double CannonMuzzleKmS = 3.5;
		static constexpr int MaxCannonAmmo = 600;
		static constexpr double MissileDamage = 70;
		static constexpr double MissileSpeedKmS = 1.4;
		static constexpr double MissileRangeKm = 8;
		static constexpr double MissileSalvoHeat = 24;
		static constexpr double MissileConeDeg = 20;
		static constexpr double SalvoIntervalS = 0.12;
		static constexpr double MissileCooldownS = 2.5;
		static constexpr int MaxMissiles = 32;
		static constexpr int SalvoSize = 4;
		static constexpr double HeatDissipation = 16;
		static constexpr double CoolResumeHeat = 40;

		/// <summary>The jump jets: their push (km/s^2; Shift doubles it and the burn), the tank (seconds), its refill on a hull
		/// (seconds a second), and the kick off the plating on lifting off (km/s).</summary>
		static constexpr double JetAccelKmS2 = 0.025;
		static constexpr double JetFuelS = 6;
		static constexpr double JetRefillPerS = 0.75;
		static constexpr double LiftKickKmS = 0.004;
		/// <summary>Setting down: no armour lost below <see cref="LandSafeKmS"/> against the hull, wrecked above
		/// <see cref="CrashKmS"/>, armour lost per km/s over the safe speed between; and how long after lifting off before the
		/// boots catch again (s).</summary>
		static constexpr double LandSafeKmS = 0.03;
		static constexpr double CrashKmS = 0.25;
		static constexpr double HardLandingArmorPerKmS = 6000;
		static constexpr double LiftGraceS = 0.6;
		/// <summary>The smallest hull it can stand on (km long): nothing a fighter's size.</summary>
		static constexpr double MinHullKm = 0.05;

		MechWalker(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& carrier);

		/// <summary>Whether <paramref name="u"/> carries a mech in its bay: a capital ship or a station (whatever sends out EVA
		/// crews), present and not taken as a prize.</summary>
		static bool CarriesMech(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// Sends <paramref name="carrier"/>'s mech out onto its dorsal plating, or returns the one already out and still
		/// standing. Null, with <paramref name="why"/>, when it cannot: no fight, the fight over, or no mech bay on that hull.
		/// </summary>
		static cs::Ref<MechWalker> Deploy(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& carrier, cs::String& why);

		/// <summary>Every mech out in <paramref name="battle"/> and still standing (<see cref="Embodiables.WalkersIn"/>).</summary>
		static cs::List<cs::Ref<MechWalker>> DeployedIn(const cs::Ref<Core::Battle>& battle);

		/// <summary>Whether <paramref name="u"/> is a hull its boots can stand on.</summary>
		static bool Standable(const cs::Ref<BattleUnit>& u);

		bool OnHull() const { return Hull != nullptr; }
		double ArmorFraction() const { return cs::Math::Max(0.0, Armor) / MaxArmor; }

		cs::String Name() const override { return Callsign; }
		EmbodimentKind Kind() const override { return EmbodimentKind::Mech; }
		cs::Ref<Core::Faction> Faction() const override;
		cs::Ref<IEmbodiable> Host() const override { return nullptr; }
		bool CanEmbody() const override { return Blocker().empty(); }
		cs::String Blocker() const override;
		bool Embodied() const override { return held; }
		EmbodimentPose PoseAt(double jd) override;
		void TakeCharge() override;
		void Control(const EmbodimentInput& input, double dt) override;
		void HandBack() override;

		EmbodimentInputProfile InputProfile() const override { return EmbodimentInputProfile::Walker; }
		EmbodimentCameraRig CameraRig() const override;
		EmbodimentRolePanel RolePanel() override;
		EmbodimentSubject Subject() const override;

	private:
		bool held = false;
		bool secondaryWas = false;
		double cannonCooldown = 0, missileCooldown = 0, salvoTimer = 0, liftGrace = 0;
		int nextArm = 0, salvoLeft = 0;
		cs::Ref<BattleUnit> salvoTarget;

		void Note(cs::String what);
		void PlaceOnCarrier();
		void Walk(const EmbodimentInput& input, double dt);
		void Fly(const EmbodimentInput& input, double dt);
		void LiftOff();
		void SetDown(const cs::Ref<BattleUnit>& u, DVec3 local);
		void Weapons(const EmbodimentInput& input, double dt);
		void FireCannon();
		void LaunchMissile();
		cs::Ref<BattleUnit> TargetNearAim() const;
		/// <summary>A muzzle (0 left, 1 right) or a launch tube, battle frame.</summary>
		DVec3 MuzzleAt(int arm) const;
		/// <summary>A friendly unit still in the fight to stand for it in the world: the hull it is on, its carrier, or the nearest.</summary>
		cs::Ref<BattleUnit> Anchor() const;
	};
}
