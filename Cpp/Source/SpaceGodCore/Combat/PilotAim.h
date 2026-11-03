#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// Turns the stick into where the hull should point, one way per <see cref="PilotControl"/>
	/// (U.SPC-T269). The result is <see cref="PilotInput.DesiredForward"/>; the hull then swings toward
	/// it at its own rate (<see cref="PilotFlight.Step"/>).
	/// </summary>
	class PilotAim
	{
	public:
		/// <summary>How far a free-aim crosshair may run ahead of the nose: anywhere in front of the hull,
		/// never behind it, so the camera always looks over the hull's shoulder rather than back at it.</summary>
		static constexpr double FreeAimMaxLeadDeg = 90;

	private:
		/// <summary>How fast the free-aim view's roof follows the hull's own as it rolls, 1/s.</summary>
		static constexpr double ViewRollFollowHz = 6;

	public:
		/// <summary>
		/// Free aim (<see cref="PilotControl.FreeAim"/>): swings the crosshair <paramref name="aim"/>
		/// right by <paramref name="rightRad"/> and up by <paramref name="upRad"/> in the view's own frame
		/// (its roof <paramref name="viewUp"/>, which turns with it), so the mouse is screen-relative
		/// whatever the hull is doing. The roof then eases toward the hull's own
		/// (<paramref name="hullUp"/>) about the line of sight, so rolling the hull rolls the view, and
		/// the crosshair is held within <see cref="FreeAimMaxLeadDeg"/> of the nose.
		/// </summary>
		static void Look(DVec3& aim, DVec3& viewUp, double rightRad, double upRad, DVec3 hullForward, DVec3 hullUp, double dt);

		/// <summary>
		/// Nose steering (<see cref="PilotControl.DirectNose"/>, <see cref="PilotControl.Hauler"/>): the
		/// mouse moves the aim about the hull's own roof and starboard axes, and the aim never leads the
		/// nose by more than the hull can brake its swing in (<see cref="PilotFlight.AimLeadRad"/>), so the
		/// nose stops soon after the mouse does.
		/// </summary>
		static DVec3 Nose(DVec3 aim, DVec3 hullForward, DVec3 hullUp, double rightRad, double upRad, double leadRad);

		/// <summary>
		/// The capital-ship helm (<see cref="PilotControl.Helm"/>): <paramref name="yaw"/> and
		/// <paramref name="pitch"/> (each -1..1, positive to starboard and up) ask the hull to turn at full
		/// rate while held, by setting the heading the brake angle (<paramref name="leadRad"/>) ahead of the
		/// nose; let go and the heading is the nose itself, so the swing stops as fast as the hull allows.
		/// </summary>
		static DVec3 Helm(DVec3 hullForward, DVec3 hullUp, double yaw, double pitch, double leadRad);

	private:
		/// <summary><paramref name="up"/> made square to <paramref name="forward"/> (unit), falling back to
		/// <paramref name="fallback"/>'s and then the level roof when it lies along it.</summary>
		static DVec3 Square(DVec3 up, DVec3 forward, DVec3 fallback);
	};
}
