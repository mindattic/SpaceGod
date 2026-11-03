#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class ShipCombat;
	class RangeSetting;
	class ChoiceSetting;
	struct PilotState;
	struct PilotInput;
	struct PilotTuning;
	struct HullDynamics;
	enum class ShipClass : int;

	/// <summary>
	/// The families of craft that fly by rules of their own at the helm (U.SPC-T17, see
	/// docs/FLIGHT_MODELS.md). A family is a way of flying, not a hull: every <see cref="ShipClass"/>
	/// belongs to exactly one (<see cref="FlightModelRegistry.FamilyOf"/>), and each family has one
	/// <see cref="IFlightModel"/>. This is where the fighter lineup splits into light and heavy.
	/// </summary>
	enum class FlightFamily : int
	{
		/// <summary>Star Conflict-style dogfighters (the Fighter, the space superiority workhorse): the track carves
		/// after the nose, strafing is strong, and a turn is tightest at the corner speed.</summary>
		LightFighter,

		/// <summary>Everspace 2-style heavy fighters (U.SPC-T441, <see cref="HeavyFighterFlightModel"/>): the Gunship (U.SPC-T467).</summary>
		HeavyFighter,

		/// <summary>Freighters and miners: heavy, deliberate haulers (U.SPC-T443, <see cref="HaulerFlightModel"/>).</summary>
		Hauler,

		/// <summary>Capital ships (Carrier, Battleship, Cruiser, Destroyer): naval broadside handling.</summary>
		Capital,

		/// <summary>Interceptors (U.SPC-T484): the light fighter's rules pushed further, with more grip, harder strafing
		/// and a turn that holds up at higher speed, so it can run a fighter down and still turn with it.</summary>
		Interceptor,

		/// <summary>Bombers (U.SPC-T484): a steady, heavy torpedo platform, with a wide carve, a weak strafe and a
		/// throttle with heft.</summary>
		Bomber,

		/// <summary>Runners (U.SPC-T444): the Courier, a blockade runner rather than a dogfighter. The light fighter's
		/// rules with little grip and an early corner speed, so it turns on a wide circle, on the highest top speed,
		/// the longest boost and the fastest cruise spool in the game (<see cref="LightFighterHandling.Runner"/>).
		/// Last in the list so the other families keep their numbers.</summary>
		Runner,
	};
	SPACEGODCORE_API const char* ToString(FlightFamily v);

	/// <summary>
	/// How one family of craft turns the pilot's input into motion each frame (U.SPC-T17). Every model
	/// flies the same <see cref="PilotState"/> on the same hull (<see cref="HullDynamics"/>) and class
	/// tuning (<see cref="PilotTuning"/>), so a hull handed from one model to another carries on where it
	/// was; what differs is the rules in between. A model keeps no state of its own between frames
	/// (everything that carries is on <see cref="PilotState"/> or <see cref="ShipCombat"/>), so one
	/// instance serves every hull of its family. A new way of flying is a new implementation registered
	/// for its family, never a branch in <see cref="PilotFlight"/>.
	/// </summary>
	class SPACEGODCORE_API IFlightModel
	{
	public:
		virtual ~IFlightModel() = default;

		/// <summary>A short name for the HUD and logs.</summary>
		virtual cs::String Name() const = 0;

		/// <summary>Advances the hull by <paramref name="dt"/> seconds; see <see cref="PilotFlight.Step"/>
		/// for what each argument means.</summary>
		virtual void Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt) = 0;
	};

	/// <summary>
	/// The shared Newtonian model every hull flew before U.SPC-T17 (U.SPC-T59, <see cref="PilotFlight.Step"/>),
	/// unchanged: the families with no model of their own yet fly it.
	/// </summary>
	class SPACEGODCORE_API NewtonianFlightModel final : public IFlightModel
	{
	public:
		static const cs::Ref<NewtonianFlightModel>& Instance();

		cs::String Name() const override { return "Newtonian"; }

		void Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt) override;
	};

	/// <summary>What stiffens a light fighter's turn (U.SPC-T440, <see cref="LightFighterHandling.TurnPenalty"/>).</summary>
	enum class TurnPenaltyRule : int
	{
		/// <summary>Speed alone: the turn tapers from the corner speed down to <see cref="LightFighterHandling.OverspeedTurnFactor"/>
		/// at full boost speed, boost held or not, so a hull still carrying a boost's speed turns wide until it sheds it.</summary>
		Speed,

		/// <summary>Boost held, as a Star Conflict afterburner: the nose swings at <see cref="LightFighterHandling.OverspeedTurnFactor"/>
		/// of its rate while the boost fires and at the full rate at any speed once it is let go.</summary>
		BoostHeld,
	};
	SPACEGODCORE_API const char* ToString(TurnPenaltyRule v);

	/// <summary>
	/// The light fighter's handling numbers (U.SPC-T17): data, not code, so a tuning pass or the settings
	/// registry (U.SPC-T431) changes them without touching <see cref="LightFighterFlightModel"/>. The game's
	/// light fighters, interceptors and runners read theirs from Settings > Dogfighting (<see cref="LightFighterSettings"/>, U.SPC-T440).
	/// </summary>
	struct SPACEGODCORE_API LightFighterHandling
	{
		/// <summary>With flight assist on, how hard the flight computer may push across the nose to pull
		/// the track round after it (the "grip" that makes a turn a carve, not a slide), as a fraction of
		/// the push that holds the track to the nose through the tightest turn at the corner speed
		/// (corner speed times full turn rate): at 1 the track sticks to the nose at or below the corner
		/// speed and slides wide above it. The Newtonian model is held to the manoeuvre thrusters, a few
		/// percent of that.</summary>
		double Grip = 0;

		/// <summary>Flight assist's strafe set point as a fraction of the forward one (the Newtonian
		/// model's is <see cref="PilotFlight.StrafeSpeedFraction"/>): a light fighter dodges sideways hard.</summary>
		double StrafeSpeedFraction = 0;

		/// <summary>The corner speed, as a fraction of <see cref="PilotTuning.MaxSpeed"/>: at or below it
		/// the nose swings at the full <see cref="PilotTuning.TurnRateDegS"/>.</summary>
		double CornerSpeedFraction = 0;

		/// <summary>The turn rate left at full boost speed (<see cref="PilotTuning.MaxSpeed"/> times
		/// <see cref="PilotTuning.BoostSpeedMultiplier"/>) and beyond, as a fraction of the full rate;
		/// between the corner speed and there it tapers linearly. Boost to get somewhere, ease off to turn.</summary>
		double OverspeedTurnFactor = 0;

		/// <summary>Whether the turn penalty follows speed or the boost held (U.SPC-T440, see <see cref="TurnPenaltyRule"/>).</summary>
		TurnPenaltyRule TurnPenalty = TurnPenaltyRule::Speed;

		constexpr LightFighterHandling() = default;

		constexpr LightFighterHandling(double grip, double strafeSpeedFraction, double cornerSpeedFraction, double overspeedTurnFactor,
			TurnPenaltyRule turnPenalty = TurnPenaltyRule::Speed)
			: Grip(grip), StrafeSpeedFraction(strafeSpeedFraction), CornerSpeedFraction(cornerSpeedFraction), OverspeedTurnFactor(overspeedTurnFactor),
			  TurnPenalty(turnPenalty)
		{
		}

		/// <summary>These numbers with the turn penalty following <paramref name="rule"/>.</summary>
		LightFighterHandling WithTurnPenalty(TurnPenaltyRule rule) const
		{
			return LightFighterHandling(Grip, StrafeSpeedFraction, CornerSpeedFraction, OverspeedTurnFactor, rule);
		}

		/// <summary>The first-pass numbers, for the owner's review (docs/FLIGHT_MODELS.md).</summary>
		static const LightFighterHandling Default;

		/// <summary>The interceptor's numbers (U.SPC-T484, docs/FLIGHT_MODELS.md): more grip, the hardest strafe in the
		/// game, a higher corner speed and less of the turn lost past it, so it turns with what it runs down.</summary>
		static const LightFighterHandling Interceptor;

		/// <summary>The runner's numbers (U.SPC-T444, docs/FLIGHT_MODELS.md, Couriers): little grip, a modest strafe, an
		/// early corner speed and most of the turn lost past it, so at speed it swings wide on a big circle. It outruns a
		/// fight; it does not turn in one.</summary>
		static const LightFighterHandling Runner;
	};
	inline constexpr LightFighterHandling LightFighterHandling::Default = LightFighterHandling(
		/*grip:*/ 1.0, /*strafeSpeedFraction:*/ 0.5, /*cornerSpeedFraction:*/ 0.6, /*overspeedTurnFactor:*/ 0.6);
	inline constexpr LightFighterHandling LightFighterHandling::Interceptor = LightFighterHandling(
		/*grip:*/ 1.25, /*strafeSpeedFraction:*/ 0.6, /*cornerSpeedFraction:*/ 0.75, /*overspeedTurnFactor:*/ 0.75);
	inline constexpr LightFighterHandling LightFighterHandling::Runner = LightFighterHandling(
		/*grip:*/ 0.6, /*strafeSpeedFraction:*/ 0.35, /*cornerSpeedFraction:*/ 0.4, /*overspeedTurnFactor:*/ 0.45);

	/// <summary>
	/// Light fighters, Star Conflict style (U.SPC-T17): arcade-coupled dogfighting. With flight assist on,
	/// (1) the track carves after the nose -- the computer may push across the nose as hard as
	/// <see cref="LightFighterHandling.Grip"/> allows, so a hard turn at the corner speed leaves no
	/// slide and a faster one only a little; (2) strafing is strong (<see cref="LightFighterHandling.StrafeSpeedFraction"/>); and (3) the
	/// nose swings fastest at or below the corner speed and stiffens above it, down to
	/// <see cref="LightFighterHandling.OverspeedTurnFactor"/> of the full rate at full boost, so speed is
	/// something to manage in a fight (or, on <see cref="TurnPenaltyRule.BoostHeld"/>, only while the boost fires).
	/// With flight assist off the hull is the Newtonian one, unchanged: the drift and the turn-and-burn stay a
	/// pilot's tool. The player and the crews in a battle's dogfight (<see cref="Battle"/>, U.SPC-T274, U.SPC-T440)
	/// fly the same model.
	/// </summary>
	class SPACEGODCORE_API LightFighterFlightModel final : public IFlightModel
	{
		cs::Func<LightFighterHandling> handling;

	public:
		/// <summary>The numbers it flies on now: fixed, or read from the settings each frame (<see cref="LightFighterSettings"/>).</summary>
		LightFighterHandling Handling() const { return handling(); }

		LightFighterFlightModel();

		LightFighterFlightModel(LightFighterHandling handling, cs::String name = "Light fighter");

		/// <summary>A model that reads its numbers from <paramref name="handling"/> each frame, so a setting changed in the
		/// menu takes hold at once (U.SPC-T440).</summary>
		LightFighterFlightModel(cs::Func<LightFighterHandling> handling, cs::String name = "Light fighter");

		// C#'s get-only auto-property Name implements IFlightModel.Name: an interface member, so a method here.
		cs::String Name() const override { return name; }

	private:
		cs::String name;

	public:
		/// <summary>The fastest the nose may swing at <paramref name="speedKmS"/> with the boost not firing, deg/s (see the class remarks).</summary>
		double TurnRateAt(double speedKmS, const PilotTuning& tuning) const;

		/// <summary>The fastest the nose may swing at <paramref name="speedKmS"/>, deg/s, the boost firing or not
		/// (<paramref name="boosting"/>; it counts only on <see cref="TurnPenaltyRule.BoostHeld"/>).</summary>
		double TurnRateAt(double speedKmS, const PilotTuning& tuning, bool boosting) const;

	private:
		static double TurnRateAt(const LightFighterHandling& h, double speedKmS, const PilotTuning& tuning, bool boosting);

	public:
		/// <summary>The most the flight computer pushes across the nose with assist on, km/s² (see
		/// <see cref="LightFighterHandling.Grip"/>).</summary>
		double GripAccel(const PilotTuning& tuning) const;

	private:
		static double GripAccel(const LightFighterHandling& h, const PilotTuning& tuning);

	public:
		void Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt) override;
	};

	/// <summary>
	/// The light fighter's handling in Settings > Dogfighting (U.SPC-T440, the settings registry of U.SPC-T431): grip, strafe,
	/// corner speed and the turn left at full boost for each family flying <see cref="LightFighterFlightModel"/> (the light
	/// fighter, the interceptor and the runner), defaulting to their own <see cref="LightFighterHandling"/>, and one choice of
	/// what stiffens the turn, shared by all three. The game's registry reads them every frame, so a change takes hold in
	/// the player's hands and the crews' at once.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API LightFighterSettings
	{
		/// <summary>The <see cref="TurnPenaltyRule"/> options, in its order.</summary>
		static const cs::List<cs::String>& TurnPenaltyOptions();

	public:
		static const cs::Ref<ChoiceSetting>& TurnPenalty();

		/// <summary>The rule <see cref="TurnPenalty"/> names.</summary>
		static TurnPenaltyRule Rule();

		/// <summary>One family's four numbers.</summary>
		class SPACEGODCORE_API Family final
		{
		public:
			cs::Ref<RangeSetting> Grip, Strafe, CornerSpeed, OverspeedTurn;

			Family(cs::String key, cs::String label, cs::String keywords, LightFighterHandling defaults);

			/// <summary>The numbers as set now, with the shared turn penalty rule.</summary>
			LightFighterHandling Handling() const;
		};

		/// <summary>The Fighter's (<see cref="FlightFamily.LightFighter"/>).</summary>
		static const cs::Ref<Family>& LightFighter();

		/// <summary>The interceptor's and the recon fighter's (<see cref="FlightFamily.Interceptor"/>).</summary>
		static const cs::Ref<Family>& Interceptor();

		/// <summary>The Courier's (<see cref="FlightFamily.Runner"/>).</summary>
		static const cs::Ref<Family>& Runner();

	private:
		static cs::String Times(double v);

		static cs::String Percent(double v);
	};

	/// <summary>
	/// The bomber's handling numbers (U.SPC-T484): data, not code, like <see cref="LightFighterHandling"/>.
	/// </summary>
	struct SPACEGODCORE_API BomberHandling
	{
		/// <summary>With flight assist on, how hard the flight computer may push across the nose, as a fraction of the
		/// push that holds the track to the nose through its tightest turn at top speed (top speed times turn rate):
		/// below 1 the track swings wide of the nose in a hard turn, a carve with a slide in it.</summary>
		double Grip = 0;

		/// <summary>Flight assist's strafe set point as a fraction of the forward one: a bomber barely sidesteps.</summary>
		double StrafeSpeedFraction = 0;

		/// <summary>The share of its drive (and retros) flight assist may use to change speed along the nose: the heft
		/// on the throttle, so a bomber gathers and sheds way slowly.</summary>
		double ThrottleAuthority = 0;

		constexpr BomberHandling() = default;

		constexpr BomberHandling(double grip, double strafeSpeedFraction, double throttleAuthority)
			: Grip(grip), StrafeSpeedFraction(strafeSpeedFraction), ThrottleAuthority(throttleAuthority)
		{
		}

		/// <summary>The first-pass numbers, for the owner's review (docs/FLIGHT_MODELS.md).</summary>
		static const BomberHandling Default;
	};
	inline constexpr BomberHandling BomberHandling::Default = BomberHandling(/*grip:*/ 0.6, /*strafeSpeedFraction:*/ 0.25, /*throttleAuthority:*/ 0.6);

	/// <summary>
	/// Bombers (U.SPC-T484): a steady torpedo platform, not a dogfighter. With flight assist on, (1) the nose turns at
	/// the class's own rate at any speed (no corner speed, so a run in is flown at one steady rate), (2) the track
	/// carves after it only as hard as <see cref="BomberHandling.Grip"/> allows, so a hard turn swings wide, (3) the
	/// strafe is weak (<see cref="BomberHandling.StrafeSpeedFraction"/>) and (4) assist changes speed with only
	/// <see cref="BomberHandling.ThrottleAuthority"/> of the drive, so a bomber is slow to gather way and slow to shed
	/// it. With flight assist off the hull is the Newtonian one, unchanged.
	/// </summary>
	class SPACEGODCORE_API BomberFlightModel final : public IFlightModel
	{
	public:
		BomberHandling Handling;

		BomberFlightModel() : BomberFlightModel(BomberHandling::Default) {}

		BomberFlightModel(BomberHandling handling) : Handling(handling) {}

		cs::String Name() const override { return "Bomber"; }

		/// <summary>The most the flight computer pushes across the nose with assist on, km/s² (see <see cref="BomberHandling.Grip"/>).</summary>
		double GripAccel(const PilotTuning& tuning) const;

		void Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt) override;
	};

	/// <summary>
	/// The heavy fighter's handling numbers (U.SPC-T441, docs/FLIGHT_MODELS.md, Heavy fighters): data, not code, like
	/// <see cref="LightFighterHandling"/>, each one a setting for the settings registry (U.SPC-T431) to declare.
	/// </summary>
	struct SPACEGODCORE_API HeavyFighterHandling
	{
		/// <summary>The cap on the nose's swing, as a fraction of the light fighter's turn rate (the space superiority
		/// fighter's, <see cref="LightFighterTurnRateDegS"/>): stiff, below any light fighter. A class tuned slower still keeps
		/// its own rate.</summary>
		double TurnRateCapFraction = 0;

		/// <summary>Seconds for the nose to spin up from still to its top swing (and to stop from it): short, so the nose
		/// starts and stops crisply. A hull whose own attitude thrusters spin it faster keeps its own.</summary>
		double SpinUpS = 0;

		/// <summary>With flight assist on, seconds for the hull to settle (to within 5%) on a new set point, forward,
		/// sideways or vertically: a short ease, whatever its thrusters, so a change of speed reads as weight without being slow.</summary>
		double SettleS = 0;

		/// <summary>Flight assist's strafe and vertical set points as a fraction of the forward one.</summary>
		double StrafeSpeedFraction = 0;

		/// <summary>Seconds over which, once boost is let go, assist bleeds the boost's extra speed off (the whole of
		/// it, from full boost speed down to top speed), instead of braking hard: a boosted run carries through the target.</summary>
		double BoostBleedS = 0;

		constexpr HeavyFighterHandling() = default;

		constexpr HeavyFighterHandling(double turnRateCapFraction, double spinUpS, double settleS, double strafeSpeedFraction, double boostBleedS)
			: TurnRateCapFraction(turnRateCapFraction), SpinUpS(spinUpS), SettleS(settleS), StrafeSpeedFraction(strafeSpeedFraction), BoostBleedS(boostBleedS)
		{
		}

		/// <summary>The light fighter's turn rate the cap is a share of, deg/s: the space superiority fighter's.</summary>
		static double LightFighterTurnRateDegS();

		/// <summary>The first-pass numbers, for the owner's review (docs/FLIGHT_MODELS.md).</summary>
		static const HeavyFighterHandling Default;
	};
	inline constexpr HeavyFighterHandling HeavyFighterHandling::Default = HeavyFighterHandling(
		/*turnRateCapFraction:*/ 0.7, /*spinUpS:*/ 0.1, /*settleS:*/ 0.5, /*strafeSpeedFraction:*/ 0.45, /*boostBleedS:*/ 2);

	/// <summary>
	/// Heavy fighters, Everspace 2 style (U.SPC-T441): always coupled and very responsive, heavy without being sluggish.
	/// With flight assist on, (1) the nose turns at one rate at any speed (no corner speed), capped below a light
	/// fighter's (<see cref="HeavyFighterHandling.TurnRateCapFraction"/>), and spins up and stops hard
	/// (<see cref="HeavyFighterHandling.SpinUpS"/>), so the aim is "on rails"; (2) assist eases onto its set point over
	/// <see cref="HeavyFighterHandling.SettleS"/>, forward, sideways and vertically alike, so the hull goes where it
	/// points and a change of speed has heft; (3) the strafe and vertical set points are
	/// <see cref="HeavyFighterHandling.StrafeSpeedFraction"/> of the forward one, reached as fast as forward speed, for
	/// circle-strafing; and (4) speed above the set point (a boost let go) bleeds off over
	/// <see cref="HeavyFighterHandling.BoostBleedS"/> rather than being braked away, so a boosted run carries through.
	/// With flight assist off the hull is the Newtonian one, unchanged.
	/// </summary>
	class SPACEGODCORE_API HeavyFighterFlightModel final : public IFlightModel
	{
	public:
		HeavyFighterHandling Handling;

		HeavyFighterFlightModel() : HeavyFighterFlightModel(HeavyFighterHandling::Default) {}

		HeavyFighterFlightModel(HeavyFighterHandling handling) : Handling(handling) {}

		cs::String Name() const override { return "Heavy fighter"; }

		/// <summary>The nose's top swing, deg/s, at any speed: the class's own, capped below a light fighter's.</summary>
		double TurnRateDegS(const PilotTuning& tuning) const;

		/// <summary>The angular acceleration the nose swings with, rad/s²: enough to reach the top swing in
		/// <see cref="HeavyFighterHandling.SpinUpS"/>, or the hull's own if that is more.</summary>
		double TurnAlpha(const HullDynamics& hull, const PilotTuning& tuning) const;

		/// <summary>The time constant of assist's ease onto its set point, s: a first-order ease is within 5% after three.</summary>
		double EaseTimeConstantS() const;

		/// <summary>The most assist slows a hull running above its set point, km/s² (the boost bleed): the boost's extra
		/// speed over <see cref="HeavyFighterHandling.BoostBleedS"/>, and never less than the retros give.</summary>
		double BleedAccel(const HullDynamics& hull, const PilotTuning& tuning) const;

		/// <summary>How far the stick's aim may run ahead of the nose, radians (as <see cref="PilotFlight.AimLeadRad"/>, for
		/// this model's own swing and spin-up).</summary>
		double AimLeadRad(const HullDynamics& hull, const PilotTuning& tuning) const;

		void Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt) override;

	private:
		static double Clamp(double x);
	};

	/// <summary>
	/// A capital ship's engine telegraph (U.SPC-T499): the stepped speeds its engines are rung to, which hold
	/// until rung again. Flank is the boost, and drains the boost charge.
	/// </summary>
	enum class EngineTelegraph : int
	{
		FullAstern,
		Stop,
		Slow,
		Half,
		Full,
		Flank,
	};
	SPACEGODCORE_API const char* ToString(EngineTelegraph v);

	/// <summary>
	/// A capital ship's handling numbers (U.SPC-T499, docs/FLIGHT_MODELS.md, Capital ships): data, not code, with a
	/// <see cref="Default"/> and per-class overrides (<see cref="Of"/>). The turn itself is not a number here: it is fitted
	/// to <see cref="ComeAboutS"/> (<see cref="CapitalFlightModel.PeakTurnRateDegS"/>), so the come-about is what the
	/// numbers promise.
	/// </summary>
	struct SPACEGODCORE_API CapitalHandling
	{
		/// <summary>The telegraph's speeds as fractions of <see cref="PilotTuning.MaxSpeed"/> (Full is 1, Flank the boost
		/// on top of it, Stop 0): Full Astern (astern, so the hull backs at this share), Slow and Half.</summary>
		double FullAsternFraction = 0, SlowFraction = 0, HalfFraction = 0;

		/// <summary>How the engines answer a new step: the time constant, s, with which the hull eases toward the step's
		/// speed, as fast as its drive and retros allow.</summary>
		double SpoolS = 0;

		/// <summary>Steerage, as fractions of the peak turn rate (which is at Half): a slow thruster pivot at Stop, what is
		/// left at Full, and at Flank speed and beyond. Between them it runs linearly with the hull's speed.</summary>
		double PivotFactor = 0, FullFactor = 0, FlankFactor = 0;

		/// <summary>The keel: with flight assist on, the most the hull pushes across its nose to kill slip, as a fraction
		/// of top speed times the peak turn rate. Enough to trace a turning circle at Half and Full; a little slip shows at
		/// Flank.</summary>
		double Keel = 0;

		/// <summary>The pitch rate as a fraction of the yaw rate (a turn between them blends the two); 1 swings the nose
		/// equally readily every way, as nothing holds a capital to a plane.</summary>
		double PitchFactor = 0;

		/// <summary>Flight assist's roll rate as a fraction of the peak turn rate.</summary>
		double RollFactor = 0;

		/// <summary>The come-about: seconds for a 180 deg turn at Half, from a steady heading to a steady heading.</summary>
		double ComeAboutS = 0;

		constexpr CapitalHandling() = default;

		constexpr CapitalHandling(double fullAsternFraction, double slowFraction, double halfFraction, double spoolS, double pivotFactor,
			double fullFactor, double flankFactor, double keel, double pitchFactor, double rollFactor, double comeAboutS)
			: FullAsternFraction(fullAsternFraction), SlowFraction(slowFraction), HalfFraction(halfFraction), SpoolS(spoolS), PivotFactor(pivotFactor),
			  FullFactor(fullFactor), FlankFactor(flankFactor), Keel(keel), PitchFactor(pitchFactor), RollFactor(rollFactor), ComeAboutS(comeAboutS)
		{
		}

		/// <summary>These numbers with the ones given changed.</summary>
		CapitalHandling With(cs::Nullable<double> halfFraction = std::nullopt, cs::Nullable<double> spoolS = std::nullopt, cs::Nullable<double> pivotFactor = std::nullopt,
			cs::Nullable<double> fullFactor = std::nullopt, cs::Nullable<double> flankFactor = std::nullopt, cs::Nullable<double> comeAboutS = std::nullopt) const
		{
			return CapitalHandling(FullAsternFraction, SlowFraction, halfFraction.value_or(HalfFraction), spoolS.value_or(SpoolS), pivotFactor.value_or(PivotFactor),
				fullFactor.value_or(FullFactor), flankFactor.value_or(FlankFactor), Keel, PitchFactor, RollFactor, comeAboutS.value_or(ComeAboutS));
		}

		/// <summary>The first-pass numbers, for the owner's review (docs/FLIGHT_MODELS.md), with a cruiser's come-about.</summary>
		static const CapitalHandling Default;

	private:
		static const cs::Dictionary<ShipClass, CapitalHandling>& ByClass();

	public:
		/// <summary>The handling of a capital of class <paramref name="c"/>; <see cref="Default"/> for one with none of its own.</summary>
		static CapitalHandling Of(ShipClass c);

		/// <summary>The classes with handling of their own.</summary>
		static cs::List<ShipClass> Classes();
	};
	inline constexpr CapitalHandling CapitalHandling::Default = CapitalHandling(
		/*fullAsternFraction:*/ 0.4, /*slowFraction:*/ 0.25, /*halfFraction:*/ 0.5, /*spoolS:*/ 4, /*pivotFactor:*/ 0.25,
		/*fullFactor:*/ 0.7, /*flankFactor:*/ 0.6, /*keel:*/ 0.75, /*pitchFactor:*/ 1, /*rollFactor:*/ 1, /*comeAboutS:*/ 10);

	/// <summary>
	/// Capital ships (U.SPC-T499, docs/FLIGHT_MODELS.md): Black Flag's handling, in full 3D. With flight assist on,
	/// (1) the engine telegraph sets the speed: it is rung a step at a time (<see cref="PilotInput.TelegraphStep"/>) and
	/// holds, and the hull eases toward each step's speed (<see cref="CapitalHandling.SpoolS"/>); Flank is the boost, and
	/// rings itself down to Full once the charge is spent; (2) steerage: the nose swings fastest at Half, less at Full and
	/// Flank, and only pivots slowly at Stop; (3) the keel kills slip across the hull, so a turn traces a turning circle;
	/// and (4) nothing holds the hull to a plane or levels it: pitch, yaw and roll are all free, slow and heavy, so a
	/// capital climbs, dives and rolls to bring a broadside to bear above or below. The turn is fitted to the class's
	/// come-about (<see cref="CapitalHandling.ComeAboutS"/>). With flight assist off the hull is the Newtonian one, unchanged.
	/// </summary>
	class SPACEGODCORE_API CapitalFlightModel final : public IFlightModel
	{
	public:
		CapitalHandling Handling;

		CapitalFlightModel() : CapitalFlightModel(CapitalHandling::Default) {}

		CapitalFlightModel(CapitalHandling handling) : Handling(handling) {}

		cs::String Name() const override { return "Capital"; }

		/// <summary>The telegraph rung <paramref name="steps"/> steps ahead (negative: astern), held between Full Astern and Flank.</summary>
		static EngineTelegraph Ring(EngineTelegraph t, int steps);

		/// <summary>The speed a telegraph step asks for along the nose, km/s (negative astern); Flank's is Full's times the
		/// boost, while the charge lasts.</summary>
		double SpeedOf(EngineTelegraph t, const PilotTuning& tuning) const;

		/// <summary>The step (Full Astern to Full) nearest a hull making <paramref name="forwardSpeedKmS"/> along its nose:
		/// what the telegraph stands at when a hull already under way is first taken in hand.</summary>
		EngineTelegraph NearestStep(double forwardSpeedKmS, const PilotTuning& tuning) const;

		/// <summary>The angular acceleration the nose swings with, rad/s²: the hull's own, or where its attitude thrusters
		/// could not make the come-about at all, the least the helm needs to (with a quarter to spare).</summary>
		double TurnAlpha(const HullDynamics& hull) const;

		/// <summary>The nose's top swing at Half, deg/s: fitted so a 180 deg turn from a steady heading to a steady heading
		/// (spin up at <see cref="TurnAlpha"/>, swing, brake) takes the come-about. With swing rate r and spin-up α the turn
		/// takes π/r + r/α, so r is the smaller root of r² - αTr + απ = 0.</summary>
		double PeakTurnRateDegS(const HullDynamics& hull) const;

		/// <summary>Steerage at <paramref name="speedKmS"/> (either way), as a fraction of the peak turn rate: from the
		/// pivot at Stop up to 1 at Half, down to <see cref="CapitalHandling.FullFactor"/> at Full and
		/// <see cref="CapitalHandling.FlankFactor"/> at Flank speed and beyond.</summary>
		double SteerageFactor(double speedKmS, const PilotTuning& tuning) const;

		/// <summary>The fastest the nose may swing (yawing) at <paramref name="speedKmS"/>, deg/s.</summary>
		double TurnRateAt(double speedKmS, const HullDynamics& hull, const PilotTuning& tuning) const;

		/// <summary>How long a 180 deg turn takes holding <paramref name="speedKmS"/>, s: spin up, swing at the steerage's
		/// rate, brake. At Half it is the come-about (<see cref="CapitalHandling.ComeAboutS"/>).</summary>
		double ComeAboutSecondsAt(double speedKmS, const HullDynamics& hull, const PilotTuning& tuning) const;

		/// <summary>The most the keel pushes across the nose with assist on, km/s² (see <see cref="CapitalHandling.Keel"/>).</summary>
		double KeelAccel(const HullDynamics& hull, const PilotTuning& tuning) const;

		/// <summary>How far the helm's heading may run ahead of the nose, radians (as <see cref="PilotFlight.AimLeadRad"/>,
		/// for this model's own swing): enough for the nose to reach its peak rate while a turn key is held.</summary>
		double AimLeadRad(const HullDynamics& hull) const;

		void Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt) override;

	private:
		static double Limit(double x, double max);
	};

	/// <summary>
	/// Which family each <see cref="ShipClass"/> flies as, and which model flies each family (U.SPC-T17):
	/// the one extension point for per-class handling. Both tables are data: a new class is assigned a
	/// family, and a new way of flying is a model registered for its family, without touching the code
	/// that flies the others. A class may also fly a model of its own within its family
	/// (<see cref="Register(ShipClass, IFlightModel)"/>): the same rules on its own numbers, as each capital class
	/// flies the capital model on its own <see cref="CapitalHandling"/> (U.SPC-T499). <see cref="Default"/> is the
	/// game's; tests build their own.
	/// </summary>
	class SPACEGODCORE_API FlightModelRegistry final
	{
		cs::Dictionary<ShipClass, FlightFamily> families;
		cs::Dictionary<FlightFamily, cs::Ref<IFlightModel>> models;
		cs::Dictionary<ShipClass, cs::Ref<IFlightModel>> classModels;

	public:
		/// <summary>The model a family with none registered flies: the shared Newtonian one.</summary>
		cs::Ref<IFlightModel> Fallback;

		FlightModelRegistry();

		/// <summary>The game's registry: every class in its family, and the light fighters, interceptors,
		/// heavy fighters, bombers, runners and capital ships on models of their own; the other families fly the Newtonian model until theirs land (docs/FLIGHT_MODELS.md).</summary>
		static const cs::Ref<FlightModelRegistry>& Default();

		static cs::Ref<FlightModelRegistry> CreateDefault();

		/// <summary>Puts <paramref name="c"/> in <paramref name="family"/>, flying that family's model (any model of its own
		/// it flew in another family is dropped).</summary>
		void Assign(ShipClass c, FlightFamily family);

		/// <summary>Flies every hull of <paramref name="family"/> with <paramref name="model"/>, but for a class flying a
		/// model of its own.</summary>
		void Register(FlightFamily family, cs::Ref<IFlightModel> model);

		/// <summary>Flies every hull of class <paramref name="c"/> with <paramref name="model"/>, over its family's.</summary>
		void Register(ShipClass c, cs::Ref<IFlightModel> model);

		/// <summary>The family <paramref name="c"/> flies as; a class never assigned flies as a capital ship.</summary>
		FlightFamily FamilyOf(ShipClass c) const;

		/// <summary>The model flying <paramref name="family"/>.</summary>
		cs::Ref<IFlightModel> ModelOf(FlightFamily family) const;

		/// <summary>The model flying a hull of class <paramref name="c"/>.</summary>
		cs::Ref<IFlightModel> For(ShipClass c) const;
	};
}
