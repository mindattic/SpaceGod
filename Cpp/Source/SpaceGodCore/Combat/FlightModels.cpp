#include "Combat/FlightModels.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/HaulerFlight.h"
#include "Combat/CombatState.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	using cs::Math::Max;
	using cs::Math::Min;

	const char* ToString(FlightFamily v)
	{
		switch (v)
		{
			case FlightFamily::LightFighter: return "LightFighter";
			case FlightFamily::HeavyFighter: return "HeavyFighter";
			case FlightFamily::Hauler: return "Hauler";
			case FlightFamily::Capital: return "Capital";
			case FlightFamily::Interceptor: return "Interceptor";
			case FlightFamily::Bomber: return "Bomber";
			case FlightFamily::Runner: return "Runner";
		}
		return "";
	}

	const char* ToString(TurnPenaltyRule v)
	{
		switch (v)
		{
			case TurnPenaltyRule::Speed: return "Speed";
			case TurnPenaltyRule::BoostHeld: return "BoostHeld";
		}
		return "";
	}

	const char* ToString(EngineTelegraph v)
	{
		switch (v)
		{
			case EngineTelegraph::FullAstern: return "FullAstern";
			case EngineTelegraph::Stop: return "Stop";
			case EngineTelegraph::Slow: return "Slow";
			case EngineTelegraph::Half: return "Half";
			case EngineTelegraph::Full: return "Full";
			case EngineTelegraph::Flank: return "Flank";
		}
		return "";
	}

	// ---------------------------------------------------------------- NewtonianFlightModel

	const cs::Ref<NewtonianFlightModel>& NewtonianFlightModel::Instance()
	{
		static const cs::Ref<NewtonianFlightModel> instance = cs::New<NewtonianFlightModel>();
		return instance;
	}

	void NewtonianFlightModel::Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		PilotFlight::Step(s, combat, hull, tuning, input, gravityKmS2, dt);
	}

	// ---------------------------------------------------------------- LightFighterFlightModel

	LightFighterFlightModel::LightFighterFlightModel() : LightFighterFlightModel(LightFighterHandling::Default) {}

	LightFighterFlightModel::LightFighterFlightModel(LightFighterHandling handling, cs::String name)
		: LightFighterFlightModel(cs::Func<LightFighterHandling>([handling] { return handling; }), std::move(name))
	{
	}

	LightFighterFlightModel::LightFighterFlightModel(cs::Func<LightFighterHandling> handling, cs::String name)
	{
		if (!handling) throw cs::ArgumentNullException("handling");
		this->handling = std::move(handling);
		this->name = std::move(name);
	}

	double LightFighterFlightModel::TurnRateAt(double speedKmS, const PilotTuning& tuning) const { return TurnRateAt(speedKmS, tuning, false); }

	double LightFighterFlightModel::TurnRateAt(double speedKmS, const PilotTuning& tuning, bool boosting) const { return TurnRateAt(Handling(), speedKmS, tuning, boosting); }

	double LightFighterFlightModel::TurnRateAt(const LightFighterHandling& h, double speedKmS, const PilotTuning& tuning, bool boosting)
	{
		if (h.TurnPenalty == TurnPenaltyRule::BoostHeld)
			return boosting ? tuning.TurnRateDegS * h.OverspeedTurnFactor : tuning.TurnRateDegS;
		double corner = tuning.MaxSpeed * h.CornerSpeedFraction;
		double top = tuning.MaxSpeed * Max(1.0, tuning.BoostSpeedMultiplier);
		if (speedKmS <= corner || top <= corner) return tuning.TurnRateDegS;
		double t = Min(1.0, (speedKmS - corner) / (top - corner));
		return tuning.TurnRateDegS * (1 - t * (1 - h.OverspeedTurnFactor));
	}

	double LightFighterFlightModel::GripAccel(const PilotTuning& tuning) const { return GripAccel(Handling(), tuning); }

	double LightFighterFlightModel::GripAccel(const LightFighterHandling& h, const PilotTuning& tuning)
	{
		return h.Grip * tuning.MaxSpeed * h.CornerSpeedFraction * tuning.TurnRateDegS * Astro::Deg2Rad;
	}

	void LightFighterFlightModel::Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		if (input.FlightAssistOff)
		{
			PilotFlight::Step(s, combat, hull, tuning, input, gravityKmS2, dt);
			return;
		}
		if (dt <= 0) return;

		auto h = Handling();
		// Firing this frame: held, with charge left (as PilotFlight.DriveMultiplier has it).
		bool boosting = input.Boost && tuning.BoostDurationS > 0 && combat != nullptr && combat->BoostFraction > 0;
		auto turning = PilotTuning(tuning.MaxSpeed, TurnRateAt(h, s.Vel.Length(), tuning, boosting), tuning.BoostSpeedMultiplier,
			tuning.BoostDurationS, tuning.CruiseSpeedMultiplier, tuning.CruiseRampS);
		auto forwardBeforeTurn = s.Forward;
		bool snap = input.Piloted && Feel.SpinUpS > 0;
		if (snap)
		{
			// The player's snap (US-T79): the attitude thrusters reach the top swing and roll in SpinUpS, or faster if
			// the hull's own can; Rotate's v² = 2αθ then brakes the nose onto the aim just as crisply.
			double spin = Max(hull.PitchYawAngularAccel, turning.TurnRateDegS * Astro::Deg2Rad / Feel.SpinUpS);
			double roll = Max(hull.RollAngularAccel, turning.RollRateDegS() * Astro::Deg2Rad / Feel.SpinUpS);
			PilotFlight::Rotate(s, HullDynamics(hull.MassKt, hull.MainAccel, hull.RetroAccel, hull.LateralAccel, hull.VerticalAccel, spin, roll),
				turning, input, dt);
		}
		else PilotFlight::Rotate(s, hull, turning, input, dt);
		bool dashing = Dashing(input, combat, tuning);
		double driveMult = PilotFlight::DriveMultiplier(s, combat, tuning, input, forwardBeforeTurn, dt);

		double grip = GripAccel(h, tuning);
		double strafeFraction = h.StrafeSpeedFraction, lateralCap = Max(hull.LateralAccel, grip), verticalCap = Max(hull.VerticalAccel, grip);
		if (dashing)
		{
			// The lateral boost (US-T79): the strafe set point rises to the dash's share of the boosted speed, and the
			// push across the nose to a share of the boosted main drive, so it kicks sideways and is there in a moment.
			strafeFraction = Max(strafeFraction, Feel.DashSpeedFraction);
			double kick = hull.MainAccel * driveMult * Feel.DashThrustFraction;
			lateralCap = Max(lateralCap, kick);
			verticalCap = Max(verticalCap, kick);
		}
		auto thrust = PilotFlight::AssistThrust(s, hull, tuning, input, gravityKmS2, driveMult, dt, strafeFraction, lateralCap, verticalCap);
		PilotFlight::Integrate(s, thrust, gravityKmS2, dt);
	}

	bool LightFighterFlightModel::Dashing(const PilotInput& input, const cs::Ref<ShipCombat>& combat, const PilotTuning& tuning) const
	{
		if (!input.Piloted || input.FlightAssistOff || Feel.DashSpeedFraction <= 0) return false;
		bool boosting = input.Boost && tuning.BoostDurationS > 0 && combat != nullptr && combat->BoostFraction > 0;
		bool strafing = cs::Math::Abs(input.StrafeRight) > 0.1 || cs::Math::Abs(input.StrafeUp) > 0.1;
		return boosting && strafing;
	}

	// ---------------------------------------------------------------- LightFighterSettings

	const cs::List<cs::String>& LightFighterSettings::TurnPenaltyOptions()
	{
		static const cs::List<cs::String> options = { "By speed", "While boosting" };
		return options;
	}

	const cs::Ref<ChoiceSetting>& LightFighterSettings::TurnPenalty()
	{
		static const cs::Ref<ChoiceSetting> setting = cs::New<ChoiceSetting>("flight.lightFighter.turnPenalty", SettingGroups::Dogfighting,
			"Light fighters: what stiffens the turn", TurnPenaltyOptions(), static_cast<int>(TurnPenaltyRule::Speed),
			"By speed: past the corner speed the turn tapers off toward full boost speed, boost held or not. While boosting: the turn stiffens only while the boost fires (a Star Conflict afterburner) and is full at any speed once it is let go. Fighters, interceptors and couriers alike.",
			"flight model handling light fighter interceptor courier runner boost afterburner turn corner");
		return setting;
	}

	TurnPenaltyRule LightFighterSettings::Rule() { return static_cast<TurnPenaltyRule>(TurnPenalty()->Index); }

	LightFighterSettings::Family::Family(cs::String key, cs::String label, cs::String keywords, LightFighterHandling defaults)
	{
		cs::String k = cs::Cat("flight.", key, ".");
		cs::String words = cs::Cat("flight model handling ", keywords);
		Grip = cs::New<RangeSetting>(cs::Cat(k, "grip"), SettingGroups::Dogfighting, cs::Cat(label, ": grip"), defaults.Grip, 0.25, 2, 0.05,
			"How hard flight assist pulls the track round after the nose. At 1 the track holds to the nose through the tightest turn at the corner speed; less slides wide, more carves tighter.",
			cs::Func<cs::String, double>(Times), cs::Cat(words, " grip carve slide"));
		Strafe = cs::New<RangeSetting>(cs::Cat(k, "strafe"), SettingGroups::Dogfighting, cs::Cat(label, ": strafe"), defaults.StrafeSpeedFraction, 0.1, 1, 0.05,
			"How fast it strafes, as a share of the forward speed it is set to.",
			cs::Func<cs::String, double>(Percent), cs::Cat(words, " strafe dodge sideways"));
		CornerSpeed = cs::New<RangeSetting>(cs::Cat(k, "cornerSpeed"), SettingGroups::Dogfighting, cs::Cat(label, ": corner speed"), defaults.CornerSpeedFraction, 0.2, 1, 0.05,
			"The speed, as a share of top speed, at or below which the nose swings at its full rate; above it the turn stiffens (when the turn stiffens by speed).",
			cs::Func<cs::String, double>(Percent), cs::Cat(words, " corner speed turn"));
		OverspeedTurn = cs::New<RangeSetting>(cs::Cat(k, "overspeedTurn"), SettingGroups::Dogfighting, cs::Cat(label, ": turn at full boost"), defaults.OverspeedTurnFactor, 0.2, 1, 0.05,
			"The share of the turn rate left at full boost speed (or while boosting, when the turn stiffens only then). 1 takes the penalty away.",
			cs::Func<cs::String, double>(Percent), cs::Cat(words, " boost afterburner turn penalty"));
	}

	LightFighterHandling LightFighterSettings::Family::Handling() const
	{
		return LightFighterHandling(Grip->Value, Strafe->Value, CornerSpeed->Value, OverspeedTurn->Value, Rule());
	}

	const cs::Ref<LightFighterSettings::Family>& LightFighterSettings::LightFighter()
	{
		static const cs::Ref<Family> family = cs::New<Family>("lightFighter", "Light fighters", "light fighter space superiority", LightFighterHandling::Default);
		return family;
	}

	const cs::Ref<LightFighterSettings::Family>& LightFighterSettings::Interceptor()
	{
		static const cs::Ref<Family> family = cs::New<Family>("interceptor", "Interceptors", "interceptor recon", LightFighterHandling::Interceptor);
		return family;
	}

	const cs::Ref<LightFighterSettings::Family>& LightFighterSettings::Runner()
	{
		static const cs::Ref<Family> family = cs::New<Family>("runner", "Couriers", "courier runner blockade", LightFighterHandling::Runner);
		return family;
	}

	cs::String LightFighterSettings::Times(double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }

	cs::String LightFighterSettings::Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	// ---------------------------------------------------------------- BomberFlightModel

	double BomberFlightModel::GripAccel(const PilotTuning& tuning) const
	{
		return Handling.Grip * tuning.MaxSpeed * tuning.TurnRateDegS * Astro::Deg2Rad;
	}

	void BomberFlightModel::Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		if (input.FlightAssistOff)
		{
			PilotFlight::Step(s, combat, hull, tuning, input, gravityKmS2, dt);
			return;
		}
		if (dt <= 0) return;

		auto forwardBeforeTurn = s.Forward;
		PilotFlight::Rotate(s, hull, tuning, input, dt);
		double driveMult = PilotFlight::DriveMultiplier(s, combat, tuning, input, forwardBeforeTurn, dt);

		double grip = GripAccel(tuning);
		double authority = Max(0.0, Min(1.0, Handling.ThrottleAuthority));
		auto heavy = HullDynamics(hull.MassKt, hull.MainAccel * authority, hull.RetroAccel * authority, hull.LateralAccel, hull.VerticalAccel,
			hull.PitchYawAngularAccel, hull.RollAngularAccel);
		auto thrust = PilotFlight::AssistThrust(s, heavy, tuning, input, gravityKmS2, driveMult, dt, Handling.StrafeSpeedFraction,
			Max(hull.LateralAccel, grip), Max(hull.VerticalAccel, grip));
		PilotFlight::Integrate(s, thrust, gravityKmS2, dt);
	}

	// ---------------------------------------------------------------- HeavyFighterFlightModel

	double HeavyFighterHandling::LightFighterTurnRateDegS() { return PilotTuning::Of(ShipClass::Fighter).TurnRateDegS; }

	double HeavyFighterFlightModel::TurnRateDegS(const PilotTuning& tuning) const
	{
		return Min(tuning.TurnRateDegS, Handling.TurnRateCapFraction * HeavyFighterHandling::LightFighterTurnRateDegS());
	}

	double HeavyFighterFlightModel::TurnAlpha(const HullDynamics& hull, const PilotTuning& tuning) const
	{
		return Max(hull.PitchYawAngularAccel, TurnRateDegS(tuning) * Astro::Deg2Rad / Max(1e-3, Handling.SpinUpS));
	}

	double HeavyFighterFlightModel::EaseTimeConstantS() const { return Max(1e-3, Handling.SettleS) / 3; }

	double HeavyFighterFlightModel::BleedAccel(const HullDynamics& hull, const PilotTuning& tuning) const
	{
		double extra = tuning.MaxSpeed * (Max(1.0, tuning.BoostSpeedMultiplier) - 1);
		double bleed = Handling.BoostBleedS > 0 ? extra / Handling.BoostBleedS : cs::Double::PositiveInfinity;
		return Max(hull.RetroAccel, bleed);
	}

	double HeavyFighterFlightModel::AimLeadRad(const HullDynamics& hull, const PilotTuning& tuning) const
	{
		double rate = TurnRateDegS(tuning) * Astro::Deg2Rad;
		double brake = rate * rate / (2 * TurnAlpha(hull, tuning));
		return Min(cs::Math::PI, Max(PilotFlight::MinAimLeadDeg * Astro::Deg2Rad, brake));
	}

	void HeavyFighterFlightModel::Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		if (input.FlightAssistOff)
		{
			PilotFlight::Step(s, combat, hull, tuning, input, gravityKmS2, dt);
			return;
		}
		if (dt <= 0) return;
		if (input.Piloted)
		{
			// The player's hands (US-T80): the same rules on heavier handling and a surged boost, kicking in while it fires.
			bool boosting = input.Boost && tuning.BoostDurationS > 0 && combat != nullptr && combat->BoostFraction > 0;
			HeavyFighterFlightModel felt(PilotedHandling());
			felt.Fly(s, combat, hull, Surged(tuning), input, gravityKmS2, dt, boosting ? Feel.BoostKickS : 0);
			return;
		}
		Fly(s, combat, hull, tuning, input, gravityKmS2, dt, 0);
	}

	HeavyFighterHandling HeavyFighterFlightModel::PilotedHandling() const
	{
		return HeavyFighterHandling(Handling.TurnRateCapFraction, Max(Handling.SpinUpS, Feel.SpinUpS), Max(Handling.SettleS, Feel.SettleS),
			Handling.StrafeSpeedFraction, Max(Handling.BoostBleedS, Feel.BoostBleedS));
	}

	PilotTuning HeavyFighterFlightModel::Surged(const PilotTuning& tuning) const
	{
		double boost = 1 + (Max(1.0, tuning.BoostSpeedMultiplier) - 1) * Max(1.0, Feel.BoostSurge);
		return PilotTuning(tuning.MaxSpeed, tuning.TurnRateDegS, boost, tuning.BoostDurationS, tuning.CruiseSpeedMultiplier, tuning.CruiseRampS);
	}

	void HeavyFighterFlightModel::Fly(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt, double kickS) const
	{
		auto stiff = HullDynamics(hull.MassKt, hull.MainAccel, hull.RetroAccel, hull.LateralAccel, hull.VerticalAccel,
			TurnAlpha(hull, tuning), hull.RollAngularAccel);
		auto forwardBeforeTurn = s.Forward;
		PilotFlight::Rotate(s, stiff, TurnRateDegS(tuning), tuning.RollRateDegS(), input, dt);
		double driveMult = PilotFlight::DriveMultiplier(s, combat, tuning, input, forwardBeforeTurn, dt);

		auto f = s.Forward;
		auto r = s.Right();
		auto u = s.Up;
		double setPoint = tuning.MaxSpeed * driveMult;
		double strafe = setPoint * Handling.StrafeSpeedFraction;
		auto commanded = f * (Clamp(input.Throttle) * setPoint) + r * (Clamp(input.StrafeRight) * strafe) + u * (Clamp(input.StrafeUp) * strafe);

		// The ease: every axis closes on its set point with one time constant (gravity cancelled as it goes).
		auto ease = (commanded - s.Vel) / Max(dt, EaseTimeConstantS()) - gravityKmS2;
		double along = DVec3::Dot(s.Vel, f);
		double fwd = DVec3::Dot(ease, f);
		// The boost's kick (US-T80, the player's only): while it fires, forward speed is gathered on the short kick rather
		// than the settle, so the surge lands in the back at once; it only ever adds push, never brakes.
		if (kickS > 0)
		{
			double kick = DVec3::Dot(commanded - s.Vel, f) / Max(dt, kickS) - DVec3::Dot(gravityKmS2, f);
			fwd = Max(fwd, kick);
		}
		// The boost bleed: speed above the set point is shed no faster than the bleed, down to the set point, and the
		// ease takes over from there.
		if (along > setPoint)
		{
			double shed = Max(-(along - setPoint) / dt, -BleedAccel(hull, tuning)) - DVec3::Dot(gravityKmS2, f);
			fwd = Max(fwd, shed);
		}
		auto thrust = f * fwd + r * DVec3::Dot(ease, r) + u * DVec3::Dot(ease, u);
		PilotFlight::Integrate(s, thrust, gravityKmS2, dt);
	}

	double HeavyFighterFlightModel::Clamp(double x) { return Max(-1.0, Min(1.0, x)); }

	// ---------------------------------------------------------------- CapitalHandling

	const cs::Dictionary<ShipClass, CapitalHandling>& CapitalHandling::ByClass()
	{
		static const cs::Dictionary<ShipClass, CapitalHandling> byClass = []
		{
			cs::Dictionary<ShipClass, CapitalHandling> d;
			// With(halfFraction, spoolS, pivotFactor, fullFactor, flankFactor, comeAboutS)
			d[ShipClass::Carrier] = Default.With(/*halfFraction*/ std::nullopt, /*spoolS:*/ 6, std::nullopt, std::nullopt, std::nullopt, /*comeAboutS:*/ 18);
			d[ShipClass::Battleship] = Default.With(std::nullopt, /*spoolS:*/ 5, std::nullopt, std::nullopt, std::nullopt, /*comeAboutS:*/ 14);
			d[ShipClass::Cruiser] = Default.With(std::nullopt, /*spoolS:*/ 4, std::nullopt, std::nullopt, std::nullopt, /*comeAboutS:*/ 10);
			// The skirmisher (as in Battle.BroadsideFacing): fights slower for its pace, so its circle is the tightest of the
			// line, and keeps more of its turn at Full and Flank to cut across a battleship's bow.
			d[ShipClass::Destroyer] = Default.With(/*halfFraction:*/ 0.35, /*spoolS:*/ 2.5, /*pivotFactor:*/ 0.35, /*fullFactor:*/ 0.85, /*flankFactor:*/ 0.7, /*comeAboutS:*/ 6);
			// The battlecruiser, between the cruiser and the battleship.
			d[ShipClass::CommandCruiser] = Default.With(std::nullopt, /*spoolS:*/ 4.5, std::nullopt, std::nullopt, std::nullopt, /*comeAboutS:*/ 12);
			// The frigate: the fastest warship hull, quick to flip end for end.
			d[ShipClass::Frigate] = Default.With(std::nullopt, /*spoolS:*/ 2.5, std::nullopt, /*fullFactor:*/ 0.8, std::nullopt, /*comeAboutS:*/ 5);
			// The gate breaker (U.SPC-T478): heavier than a battleship for its length, the slowest to bring about and spool.
			d[ShipClass::GateBreaker] = Default.With(std::nullopt, /*spoolS:*/ 5.5, std::nullopt, std::nullopt, std::nullopt, /*comeAboutS:*/ 16);
			return d;
		}();
		return byClass;
	}

	CapitalHandling CapitalHandling::Of(ShipClass c)
	{
		CapitalHandling h;
		return ByClass().TryGetValue(c, h) ? h : Default;
	}

	cs::List<ShipClass> CapitalHandling::Classes() { return ByClass().Keys(); }

	// ---------------------------------------------------------------- CapitalFlightModel

	EngineTelegraph CapitalFlightModel::Ring(EngineTelegraph t, int steps)
	{
		return static_cast<EngineTelegraph>(Max(static_cast<int>(EngineTelegraph::FullAstern), Min(static_cast<int>(EngineTelegraph::Flank), static_cast<int>(t) + steps)));
	}

	double CapitalFlightModel::SpeedOf(EngineTelegraph t, const PilotTuning& tuning) const
	{
		switch (t)
		{
			case EngineTelegraph::FullAstern: return -Handling.FullAsternFraction * tuning.MaxSpeed;
			case EngineTelegraph::Stop: return 0;
			case EngineTelegraph::Slow: return Handling.SlowFraction * tuning.MaxSpeed;
			case EngineTelegraph::Half: return Handling.HalfFraction * tuning.MaxSpeed;
			case EngineTelegraph::Full: return tuning.MaxSpeed;
			default: return tuning.MaxSpeed * Max(1.0, tuning.BoostSpeedMultiplier);
		}
	}

	EngineTelegraph CapitalFlightModel::NearestStep(double forwardSpeedKmS, const PilotTuning& tuning) const
	{
		auto best = EngineTelegraph::Stop;
		double bestOff = cs::Double::MaxValue;
		for (auto t = EngineTelegraph::FullAstern; t <= EngineTelegraph::Full; t = static_cast<EngineTelegraph>(static_cast<int>(t) + 1))
		{
			double off = cs::Math::Abs(SpeedOf(t, tuning) - forwardSpeedKmS);
			if (off < bestOff)
			{
				bestOff = off;
				best = t;
			}
		}
		return best;
	}

	double CapitalFlightModel::TurnAlpha(const HullDynamics& hull) const
	{
		double t = Max(1e-3, Handling.ComeAboutS);
		return Max(hull.PitchYawAngularAccel, 1.25 * 4 * cs::Math::PI / (t * t));
	}

	double CapitalFlightModel::PeakTurnRateDegS(const HullDynamics& hull) const
	{
		double a = TurnAlpha(hull), t = Max(1e-3, Handling.ComeAboutS);
		double disc = a * a * t * t - 4 * a * cs::Math::PI;
		double r = (a * t - cs::Math::Sqrt(Max(0.0, disc))) / 2;
		return r * Astro::Rad2Deg;
	}

	double CapitalFlightModel::SteerageFactor(double speedKmS, const PilotTuning& tuning) const
	{
		auto h = Handling;
		double v = cs::Math::Abs(speedKmS);
		double half = SpeedOf(EngineTelegraph::Half, tuning), full = SpeedOf(EngineTelegraph::Full, tuning), flank = SpeedOf(EngineTelegraph::Flank, tuning);
		if (v <= half) return half > 0 ? h.PivotFactor + (1 - h.PivotFactor) * v / half : 1;
		if (v <= full) return full > half ? 1 + (h.FullFactor - 1) * (v - half) / (full - half) : h.FullFactor;
		if (v < flank) return h.FullFactor + (h.FlankFactor - h.FullFactor) * (v - full) / (flank - full);
		return h.FlankFactor;
	}

	double CapitalFlightModel::TurnRateAt(double speedKmS, const HullDynamics& hull, const PilotTuning& tuning) const
	{
		return PeakTurnRateDegS(hull) * SteerageFactor(speedKmS, tuning);
	}

	double CapitalFlightModel::ComeAboutSecondsAt(double speedKmS, const HullDynamics& hull, const PilotTuning& tuning) const
	{
		double r = TurnRateAt(speedKmS, hull, tuning) * Astro::Deg2Rad;
		return r <= 0 ? cs::Double::PositiveInfinity : cs::Math::PI / r + r / TurnAlpha(hull);
	}

	double CapitalFlightModel::KeelAccel(const HullDynamics& hull, const PilotTuning& tuning) const
	{
		return Handling.Keel * tuning.MaxSpeed * PeakTurnRateDegS(hull) * Astro::Deg2Rad;
	}

	double CapitalFlightModel::AimLeadRad(const HullDynamics& hull) const
	{
		double rate = PeakTurnRateDegS(hull) * Astro::Deg2Rad;
		double brake = rate * rate / (2 * TurnAlpha(hull));
		return Min(cs::Math::PI, Max(PilotFlight::MinAimLeadDeg * Astro::Deg2Rad, brake));
	}

	void CapitalFlightModel::Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		auto telegraph = Ring(s.Telegraph.has_value() ? *s.Telegraph : NearestStep(DVec3::Dot(s.Vel, s.Forward), tuning), input.TelegraphStep);
		s.Telegraph = telegraph;
		if (input.FlightAssistOff)
		{
			PilotFlight::Step(s, combat, hull, tuning, input, gravityKmS2, dt);
			return;
		}
		if (dt <= 0) return;

		// Steerage, and a pitch that may be slower than the yaw: a turn between them blends the two.
		double rate = TurnRateAt(s.Vel.Length(), hull, tuning);
		if (input.DesiredForward.LengthSquared() > 1e-12 && Handling.PitchFactor > 0 && Handling.PitchFactor != 1)
		{
			auto axis = DVec3::Cross(s.Forward, input.DesiredForward.Normalized());
			if (axis.LengthSquared() > 1e-18)
			{
				axis = axis.Normalized();
				double yaw = DVec3::Dot(axis, s.Up), pitch = DVec3::Dot(axis, s.Right()) / Handling.PitchFactor;
				rate /= Max(1.0, cs::Math::Sqrt(yaw * yaw + pitch * pitch));
			}
		}
		auto turning = HullDynamics(hull.MassKt, hull.MainAccel, hull.RetroAccel, hull.LateralAccel, hull.VerticalAccel,
			TurnAlpha(hull), hull.RollAngularAccel);
		auto forwardBeforeTurn = s.Forward;
		PilotFlight::Rotate(s, turning, rate, PeakTurnRateDegS(hull) * Handling.RollFactor, input, dt);

		// The engines: Flank (or the boost held) is the boost, and Full lets the cruise drive spool.
		bool flank = telegraph == EngineTelegraph::Flank || input.Boost;
		auto drive = input;
		drive.Boost = flank;
		drive.Throttle = flank || telegraph == EngineTelegraph::Full ? 1 : 0;
		double driveMult = PilotFlight::DriveMultiplier(s, combat, tuning, drive, forwardBeforeTurn, dt);
		if (telegraph == EngineTelegraph::Flank && combat->BoostFraction <= 0) s.Telegraph = EngineTelegraph::Full;
		double target = (flank ? tuning.MaxSpeed : SpeedOf(telegraph, tuning)) * driveMult;

		auto f = s.Forward;
		auto r = s.Right();
		auto u = s.Up;
		double along = DVec3::Dot(s.Vel, f);
		double ease = (target - along) / Max(dt, Handling.SpoolS) - DVec3::Dot(gravityKmS2, f);
		double fwd = Max(-hull.RetroAccel, Min(hull.MainAccel * driveMult, ease));

		// The keel: slip across the hull (and gravity across it) is killed as hard as it can push.
		auto across = (-(s.Vel - f * along)) / dt - (gravityKmS2 - f * DVec3::Dot(gravityKmS2, f));		double keel = KeelAccel(hull, tuning);
		auto thrust = f * fwd
			+ r * Limit(DVec3::Dot(across, r), Max(hull.LateralAccel, keel))
			+ u * Limit(DVec3::Dot(across, u), Max(hull.VerticalAccel, keel));
		PilotFlight::Integrate(s, thrust, gravityKmS2, dt);
	}

	double CapitalFlightModel::Limit(double x, double max) { return Max(-max, Min(max, x)); }

	// ---------------------------------------------------------------- FlightModelRegistry

	FlightModelRegistry::FlightModelRegistry() : Fallback(NewtonianFlightModel::Instance()) {}

	const cs::Ref<FlightModelRegistry>& FlightModelRegistry::Default()
	{
		static const cs::Ref<FlightModelRegistry> registry = CreateDefault();
		return registry;
	}

	cs::Ref<FlightModelRegistry> FlightModelRegistry::CreateDefault()
	{
		auto r = cs::New<FlightModelRegistry>();
		r->Assign(ShipClass::Fighter, FlightFamily::LightFighter);
		r->Assign(ShipClass::Interceptor, FlightFamily::Interceptor);
		r->Assign(ShipClass::Bomber, FlightFamily::Bomber);
		// The recon fighter (U.SPC-T485) is an interceptor's airframe and flies as one.
		r->Assign(ShipClass::Recon, FlightFamily::Interceptor);
		// A boarding shuttle (U.SPC-T486) flies like a bomber: a heavy throttle and a weak strafe.
		r->Assign(ShipClass::AssaultShuttle, FlightFamily::Bomber);
		// The Courier (U.SPC-T444) is a blockade runner, flying a family of its own.
		r->Assign(ShipClass::Courier, FlightFamily::Runner);
		r->Assign(ShipClass::Gunship, FlightFamily::HeavyFighter);
		r->Assign(ShipClass::Freighter, FlightFamily::Hauler);
		r->Assign(ShipClass::Miner, FlightFamily::Hauler);
		r->Assign(ShipClass::Explorer, FlightFamily::Hauler);
		r->Assign(ShipClass::OreTug, FlightFamily::Hauler);
		r->Assign(ShipClass::CaptureTug, FlightFamily::Hauler);
		r->Assign(ShipClass::LaneTender, FlightFamily::Hauler);
		r->Assign(ShipClass::GatePicket, FlightFamily::HeavyFighter);
		r->Assign(ShipClass::Carrier, FlightFamily::Capital);
		r->Assign(ShipClass::Battleship, FlightFamily::Capital);
		r->Assign(ShipClass::Cruiser, FlightFamily::Capital);
		r->Assign(ShipClass::Destroyer, FlightFamily::Capital);
		r->Assign(ShipClass::CommandCruiser, FlightFamily::Capital);
		r->Assign(ShipClass::Frigate, FlightFamily::Capital);
		r->Assign(ShipClass::Corvette, FlightFamily::Capital);
		r->Assign(ShipClass::GateBreaker, FlightFamily::Capital);
		// The light fighter's rules on the numbers in Settings > Dogfighting (U.SPC-T440).
		r->Register(FlightFamily::LightFighter, cs::New<LightFighterFlightModel>(cs::Func<LightFighterHandling>([] { return LightFighterSettings::LightFighter()->Handling(); })));
		r->Register(FlightFamily::Interceptor, cs::New<LightFighterFlightModel>(cs::Func<LightFighterHandling>([] { return LightFighterSettings::Interceptor()->Handling(); }), "Interceptor"));
		r->Register(FlightFamily::Bomber, cs::New<BomberFlightModel>());
		// Heavy fighters (U.SPC-T441): the Gunship, and the gate picket flown nose-first like one.
		r->Register(FlightFamily::HeavyFighter, cs::New<HeavyFighterFlightModel>());
		auto runner = cs::New<LightFighterFlightModel>(cs::Func<LightFighterHandling>([] { return LightFighterSettings::Runner()->Handling(); }), "Runner");
		runner->Feel = LightFighterFeel::Runner; // US-T79: a courier gets no lateral boost
		r->Register(FlightFamily::Runner, runner);
		// Haulers (U.SPC-T443): freighters, miners and the other work hulls fly heavy on what they carry.
		r->Register(FlightFamily::Hauler, cs::New<HaulerFlightModel>());
		// Capital ships (U.SPC-T499): one model, each class on its own handling.
		r->Register(FlightFamily::Capital, cs::New<CapitalFlightModel>());
		for (auto c : CapitalHandling::Classes()) r->Register(c, cs::New<CapitalFlightModel>(CapitalHandling::Of(c)));
		return r;
	}

	void FlightModelRegistry::Assign(ShipClass c, FlightFamily family)
	{
		FlightFamily was{};
		if (families.TryGetValue(c, was) && was != family) classModels.Remove(c);
		families[c] = family;
	}

	void FlightModelRegistry::Register(FlightFamily family, cs::Ref<IFlightModel> model)
	{
		if (!model) throw cs::ArgumentNullException("model");
		models[family] = model;
	}

	void FlightModelRegistry::Register(ShipClass c, cs::Ref<IFlightModel> model)
	{
		if (!model) throw cs::ArgumentNullException("model");
		classModels[c] = model;
	}

	FlightFamily FlightModelRegistry::FamilyOf(ShipClass c) const
	{
		FlightFamily f{};
		return families.TryGetValue(c, f) ? f : FlightFamily::Capital;
	}

	cs::Ref<IFlightModel> FlightModelRegistry::ModelOf(FlightFamily family) const
	{
		cs::Ref<IFlightModel> m;
		return models.TryGetValue(family, m) ? m : Fallback;
	}

	cs::Ref<IFlightModel> FlightModelRegistry::For(ShipClass c) const
	{
		cs::Ref<IFlightModel> m;
		return classModels.TryGetValue(c, m) ? m : ModelOf(FamilyOf(c));
	}
}
