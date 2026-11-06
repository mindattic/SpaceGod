#pragma once
#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	enum class ShipClass : int;

	/// <summary>
	/// The way a hull is flown at the helm, one per kind of craft (U.SPC-T269): see <see cref="PilotAim"/>.
	/// </summary>
	enum class PilotControl : int
	{
		/// <summary>Light fighters, Star Conflict style: the mouse swings the camera and crosshair freely
		/// and the hull turns after them at its own rate.</summary>
		FreeAim,

		/// <summary>Heavy fighters, Everspace 2 style: the mouse turns the hull itself and the nose is the
		/// crosshair; the aim never leads it by more than the hull can brake its swing in.</summary>
		DirectNose,

		/// <summary>Freighters and miners: flown like <see cref="DirectNose"/>, with the hull's weight and
		/// a slow mouse making it a heavy, deliberate thing to manoeuvre.</summary>
		Hauler,

		/// <summary>Capital ships, naval style: the keys turn the hull, the mouse looks around it freely,
		/// and the turrets fight on their own.</summary>
		Helm,
	};
	SPACEGODCORE_API const char* ToString(PilotControl v);

	/// <summary>
	/// How a hull handles under a human's hands, independent of <see cref="CombatSpec"/>'s AI-battle
	/// balance numbers -- a Freighter tuned to feel fine as a slow AI hauler (three degrees a second)
	/// is not a Freighter that's fun to actually fly. Keyed by <see cref="ShipClass"/> rather than by
	/// design, since the arcade feel is a class-wide call, not a per-hull balance knob. The numbers are
	/// authored in one place, the ship catalog's table (US-T193, <see cref="ShipCatalog"/>).
	///
	/// Since U.SPC-T59 the hull's push and spin come from its own design (mass, thrust, torque and
	/// inertia on <see cref="CombatSpec"/>, see <see cref="HullDynamics"/>); what is left here is what
	/// the flight computer does with them: the speeds and turn rates flight assist holds the hull to,
	/// and the boost and cruise drive tiers.
	/// </summary>
	struct SPACEGODCORE_API PilotTuning
	{
		/// <summary>The speed flight assist flies the hull at with the throttle floored, km/s: its
		/// set point, not a hard limit -- with assist off nothing caps the speed a hull can burn up to.</summary>
		double MaxSpeed = 0;

		/// <summary>The fastest the attitude computer swings the nose (pitch/yaw), deg/s; how quickly
		/// it gets there is the hull's own torque over its inertia.</summary>
		double TurnRateDegS = 0;

		/// <summary>Boost lights an afterburner: main-drive thrust times this while the charge lasts
		/// (draining over <see cref="BoostDurationS"/>, recharging otherwise), and flight assist's set
		/// point rises by the same factor while it burns.</summary>
		double BoostSpeedMultiplier = 0, BoostDurationS = 0;

		/// <summary>The cruise drive (see <see cref="PilotCruiseState"/>) spools main-drive thrust up
		/// to this many times normal, and flight assist's set point with it -- deliberately far above
		/// anything boost reaches, since cruise exists to cover open-space distances that would
		/// otherwise take real-world days at tactical speeds, not to win a fight.</summary>
		double CruiseSpeedMultiplier = 0;

		/// <summary>Seconds of spooling up from rest to full cruise thrust once engaged.</summary>
		double CruiseRampS = 0;

		/// <summary>The hull's roll rate as a multiple of its turn rate (a long hull rolls far more readily than it
		/// turns, see <see cref="CombatSpec.RollInertia"/>): each hull's own, from its catalog row (US-T193,
		/// <see cref="ShipCatalog"/>), and carried along when a flight model narrows the turn, so the roll narrows with it.</summary>
		double RollFactor = DefaultRollFactor;

		/// <summary>The fastest flight assist lets the hull roll, deg/s.</summary>
		double RollRateDegS() const { return TurnRateDegS * RollFactor; }

		/// <summary>The roll factor of a tuning with none of its own.</summary>
		static constexpr double DefaultRollFactor = 1.5;

		constexpr PilotTuning() = default;

		constexpr PilotTuning(double maxSpeed, double turnRateDegS, double boostSpeedMultiplier, double boostDurationS, double cruiseSpeedMultiplier, double cruiseRampS,
			double rollFactor = DefaultRollFactor)
			: MaxSpeed(maxSpeed), TurnRateDegS(turnRateDegS), BoostSpeedMultiplier(boostSpeedMultiplier), BoostDurationS(boostDurationS),
			  CruiseSpeedMultiplier(cruiseSpeedMultiplier), CruiseRampS(cruiseRampS), RollFactor(rollFactor)
		{
		}

		/// <summary>The tuning of a hull of class <paramref name="c"/>: its row in the ship catalog (US-T193).</summary>
		static PilotTuning Of(ShipClass c);

		/// <summary>How a hull of class <paramref name="c"/> is flown at the helm (U.SPC-T269): its catalog row's.</summary>
		static PilotControl ControlOf(ShipClass c);

	private:
		/// <summary>The catalog's rows as tunings by class (US-T193: built from <see cref="ShipCatalog"/>, which owns the numbers;
		/// kept as the C# build's static, which the persistence walk names, Persistence/Registrations/StaticTables.cpp).</summary>
		static const cs::Dictionary<ShipClass, PilotTuning>& ByClass();
	};
}
