#pragma once
// Port of Unity.SpaceGod Core/Embodiment/HelmFlight.cs (US-T21, see docs/MIGRATION.md). No members with bodies, so no .cpp
// beyond the translation-unit stub.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/PilotFlight.h" // PilotState, HullDynamics, PilotInput (by value)

namespace SpaceGod::Core
{
	class IFlightModel;

	/// <summary>
	/// The flight a <see cref="ShipHelm"/> flies (U.SPC-T449): the hull's motion that carries from frame to frame
	/// (<see cref="State"/>), what the world around it does to it this frame (set by whoever hosts the helm before each
	/// <see cref="ShipHelm.Control"/>), and what the frame just flown did, for the HUD, the camera and the sensors.
	/// </summary>
	class HelmFlight final
	{
	public:
		/// <summary>Where the hull is, how it is moving and turning, its cruise ramp and engine telegraph: in its frame
		/// body's frame, km and km/s, ecliptic axes.</summary>
		PilotState State;

		/// <summary>The body whose frame <see cref="State"/> is in (set by the host with the rest of the world's part), or -1
		/// when unknown: where a self-destruct out of any battle goes off (US-T97).</summary>
		int FrameBody = -1;

		/// <summary>The pull of every body near the hull this frame, km/s^2 (see <see cref="PilotFlight.Gravity"/>).</summary>
		DVec3 Gravity;

		/// <summary>Flight assist switched off at the helm: the hull drifts on its momentum.</summary>
		bool AssistOff = false;

		/// <summary>The drives are knocked out (an interdiction pulse, a hunter's EMP): no thrust, no turning, no boost or
		/// cruise; the hull drifts on what way it had.</summary>
		bool DrivesDown = false;

		/// <summary>The hull is inside a port's approach, where a hauler's Ctrl is docking mode (U.SPC-T443).</summary>
		bool DockingApproach = false;

		/// <summary>The model the last frame was flown by, the hull it was flown on, and the input it was given.</summary>
		cs::Ref<IFlightModel> Model;
		HullDynamics Hull;
		PilotInput Input;

		/// <summary>Where the hull was and how it was moving before the last frame was flown.</summary>
		DVec3 PosBefore;
		DVec3 VelBefore;

		/// <summary>The afterburner (or a capital rung to Flank) lit the last frame.</summary>
		bool Boosting = false;

		/// <summary>The thrusters changed the hull's velocity beyond what gravity did the last frame, so its drive shows
		/// on others' scanners (U.SPC-T66).</summary>
		bool Burning = false;

		/// <summary>The helm's own HUD line for the last frame (US-T87): with flight assist off, what stopping costs
		/// (<see cref="BrakePlan"/>); empty when there is nothing to say.</summary>
		cs::String Note;

		/// <summary>Thrust, km/s^2, above which the drive shows as a burn (about 0.01 g).</summary>
		static constexpr double BurnKmS2 = 1e-4;
	};
}
