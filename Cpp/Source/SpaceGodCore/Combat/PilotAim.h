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
	class SPACEGODCORE_API PilotAim
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

		// ------------------------------------------------------------ the dogfight stick (US-T129)

		/// <summary>Mouse travel, degrees of look, that pushes the dogfight stick (<see cref="Stick"/>) from centre to full.</summary>
		static constexpr double StickFullDeg = 12;

		/// <summary>How fast the dogfight stick drifts back to centre on its own, 1/s (Unreal.Soulslike's fighter: 1.6).</summary>
		static constexpr double StickReturnHz = 1.6;

		/// <summary>How far a medium hull banks into a full-stick turn, deg, when nothing else holds the roll.</summary>
		static constexpr double BankDeg = 35;

		/// <summary>The roll error, deg, at which the auto-level asks for full roll rate.</summary>
		static constexpr double LevelFullDeg = 30;

		/// <summary>
		/// The medium hulls' dogfight stick (US-T129; <see cref="PilotControl.DirectNose"/>, <see cref="PilotControl.Hauler"/>), as
		/// the spaceship in Unreal.Soulslike flies: the mouse pushes a virtual stick (<paramref name="stick"/>, x to starboard,
		/// y up, each -1..1 and held inside the unit circle) by <paramref name="rightDeg"/> and <paramref name="upDeg"/> of look
		/// (<see cref="StickFullDeg"/> to full), and the stick drifts back to centre at <see cref="StickReturnHz"/>. Its
		/// deflection is a turn rate, not a heading: the result is the heading <see cref="Helm"/> sets for that much stick, so a
		/// held push keeps the nose swinging and a centred stick lets it settle. The camera rides behind the hull and the nose is
		/// the crosshair.
		/// </summary>
		static DVec3 Stick(double& stickX, double& stickY, double rightDeg, double upDeg, DVec3 hullForward, DVec3 hullUp, double leadRad, double dt);

		/// <summary>
		/// The dogfight stick's roll (US-T129), as Unreal.Soulslike's fighter: with no roll asked for and the guns quiet, the
		/// hull rolls itself back toward level with the ecliptic, banked <see cref="BankDeg"/> into the turn the stick asks
		/// (<paramref name="stickX"/>, + to starboard: the starboard wing dips). A roll input, -1..1 (+ clockwise from behind);
		/// it fades out as the nose nears the ecliptic's poles, where there is no level to find.
		/// </summary>
		static double LevelRoll(DVec3 hullForward, DVec3 hullUp, double stickX);

		/// <summary>The hull's roll off level, deg, -180..180: + with the starboard wing down (clockwise from behind).</summary>
		static double RollOffLevelDeg(DVec3 hullForward, DVec3 hullUp);

	private:
		/// <summary><paramref name="up"/> made square to <paramref name="forward"/> (unit), falling back to
		/// <paramref name="fallback"/>'s and then the level roof when it lies along it.</summary>
		static DVec3 Square(DVec3 up, DVec3 forward, DVec3 fallback);
	};
}
