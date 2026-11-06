#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	struct PilotState;
	struct PilotInput;
	struct PilotTuning;
	struct HullDynamics;
	class ShipCombat;
	class IFlightModel;

	/// <summary>
	/// Drift (US-T165), as Chorus's: held, the dogfight model lets go of the hull. It barrels on along the velocity it had
	/// (only gravity changes it; no drive, no flight assist braking it) while its attitude thrusters swing it freely and
	/// fast onto the aim, so it can face backwards at a pursuer or rake a capital ship's length on a strafing pass. Let
	/// go, it snaps back to assisted flight in its new facing with a boost-out (<see cref="DriftControl"/>).
	///
	/// Any hull's stick can drift: the player's helm and fighter seat (R held), and a crew's (US-T164's AI strafing runs)
	/// by setting <see cref="PilotInput.Drift"/> through a <see cref="DriftControl"/> and flying with <see cref="Fly"/>.
	/// Nothing sets it for a crew today, so the Bench flies exactly as before.
	/// </summary>
	class SPACEGODCORE_API Drift
	{
	public:
		/// <summary>Seconds for the attitude thrusters to spin the nose up to its drift swing and to stop it: a floor on
		/// the hull's own push, so the hull answers the mouse crisply with the drive cold.</summary>
		static constexpr double SpinUpS = 0.12;

		/// <summary>The drift swing over the class's own top turn rate: with no drive fighting the turn and no track to
		/// carve, the attitude thrusters have the hull to themselves.</summary>
		static constexpr double TurnFactor = 1.3;

		/// <summary>Seconds of boost-out once drift is let go: the afterburner lit along the new facing, flight assist
		/// back on, the throttle at least full.</summary>
		static constexpr double BoostOutS = 0.6;

		/// <summary>
		/// One drifting frame: the attitude thrusters swing the hull onto <see cref="PilotInput.DesiredForward"/> (at
		/// <see cref="TurnFactor"/> of its top rate, spun up in <see cref="SpinUpS"/>, the roll held to the roll input's
		/// rate), the boost charge recharges and the cruise ramp drops, and the hull coasts: velocity changes only by
		/// <paramref name="gravityKmS2"/>.
		/// </summary>
		static void Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt);

		/// <summary>Flies one frame: <see cref="Step"/> while <see cref="PilotInput.Drift"/> is set, else the hull's own
		/// <paramref name="model"/>. The one call a stick that can drift flies through.</summary>
		static void Fly(const cs::Ref<IFlightModel>& model, PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt);

		/// <summary>The drift swing, deg/s, for a hull whose top turn rate is <paramref name="turnRateDegS"/>.</summary>
		static double TurnRateDegS(double turnRateDegS) { return turnRateDegS * TurnFactor; }
	};

	/// <summary>
	/// The hold and the release of drift for one stick (US-T165): held, it drifts; let go, <see cref="Drift.BoostOutS"/> of
	/// boost-out follows. Carried from frame to frame by whoever owns the stick (the helm's <see cref="HelmFlight"/>, a
	/// seat's <see cref="FighterControl"/>, or a crew's own state for US-T164). Not saved: a load starts out of drift.
	/// </summary>
	struct SPACEGODCORE_API DriftControl
	{
		/// <summary>Drifting this frame.</summary>
		bool Active = false;

		/// <summary>Seconds of boost-out left since drift was let go.</summary>
		double BoostOutLeft = 0;

		/// <summary>Advances one frame with drift <paramref name="held"/> (already gated on whether this hull may drift
		/// now). True on the frame drift is let go.</summary>
		bool Update(bool held, double dt);

		/// <summary>Writes this frame's drift onto <paramref name="input"/>: <see cref="PilotInput.Drift"/> while held;
		/// during the boost-out, the boost lit, flight assist on and the throttle at least full.</summary>
		void Apply(PilotInput& input) const;

		/// <summary>Drops out of drift at once, with no boost-out (the hull handed over, the drives knocked out).</summary>
		void Cancel()
		{
			Active = false;
			BoostOutLeft = 0;
		}
	};
}
