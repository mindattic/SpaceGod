#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	enum class PilotControl : int;

	/// <summary>
	/// How far and how fast a "pulled by a string" aim (US-T168) may run ahead of the hull: the camera looks where the
	/// mouse puts it, the hull chases it. A light fighter's string is long and slack (the view swings a full 180 at once
	/// and the hull whips round after it); a heavy hull's is short and taut (the view turns not much faster than the hull
	/// can, so the gap stays subtle).
	/// </summary>
	struct SPACEGODCORE_API StringAim
	{
		/// <summary>The furthest the aim may lead the nose, degrees; 180 or more: anywhere, even dead astern.</summary>
		double MaxLeadDeg = 180;

		/// <summary>The fastest the aim may turn, deg/s; 0: at once, as fast as the mouse moves. Mouse travel beyond it is
		/// kept and paid out at this rate, never more than <see cref="MaxLeadDeg"/> of it (so a flick is not lost, and the
		/// view does not go on turning long after the hand has stopped).</summary>
		double LookRateDegS = 0;
	};

	/// <summary>
	/// Turns the stick into where the hull should point, one way per <see cref="PilotControl"/>
	/// (U.SPC-T269). The result is <see cref="PilotInput.DesiredForward"/>; the hull then swings toward
	/// it at its own rate (<see cref="PilotFlight.Step"/>).
	/// </summary>
	class SPACEGODCORE_API PilotAim
	{
	public:
		/// <summary>A heavy hull's string (US-T168: heavy fighters, bombers, pickets, haulers): the aim leads the nose by at
		/// most this, degrees...</summary>
		static constexpr double HeavyMaxLeadDeg = 25;

		/// <summary>...and turns at most this many times the hull's own top swing, so the camera's look stays close to the
		/// hull's turn and the gap between them is subtle.</summary>
		static constexpr double HeavyLookRateFactor = 1.3;

	private:
		/// <summary>How fast the free-aim view's roof follows the hull's own as it rolls, 1/s.</summary>
		static constexpr double ViewRollFollowHz = 6;

	public:
		/// <summary>
		/// The string each kind of hull flies on (US-T168): a light fighter's (<see cref="PilotControl.FreeAim"/>) is slack,
		/// the view anywhere at once; a heavy hull's (<see cref="PilotControl.DirectNose"/>, <see cref="PilotControl.Hauler"/>)
		/// taut, <see cref="HeavyMaxLeadDeg"/> and <see cref="HeavyLookRateFactor"/> of <paramref name="hullTurnRateDegS"/>.
		/// <paramref name="drifting"/> (US-T165) slackens any hull's: the hull turns free of its track.
		/// </summary>
		static StringAim StringFor(PilotControl control, double hullTurnRateDegS, bool drifting);

		/// <summary>
		/// The string aim (US-T168): swings the crosshair <paramref name="aim"/> right by <paramref name="rightRad"/> and up
		/// by <paramref name="upRad"/> in the view's own frame (its roof <paramref name="viewUp"/>, which turns with it), so
		/// the mouse is screen-relative whatever the hull is doing, no faster than <paramref name="string"/>'s look rate
		/// (travel beyond it waits in <paramref name="pendingRight"/> / <paramref name="pendingUp"/>, radians) and no further
		/// than its lead from the nose. The roof then eases toward the hull's own (<paramref name="hullUp"/>) about the line
		/// of sight, so rolling the hull rolls the view.
		/// </summary>
		static void Look(DVec3& aim, DVec3& viewUp, double& pendingRight, double& pendingUp, double rightRad, double upRad,
			DVec3 hullForward, DVec3 hullUp, double dt, const StringAim& string);

		/// <summary>
		/// Nose steering: the mouse moves the aim about the hull's own roof and starboard axes, and the aim never leads the
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
