#pragma once
// Port of Unity.SpaceGod Runtime/Input/InputState.cs (US-T29). Unity's Input System devices are read through the
// seam in Input/InputDevice.h (fed from Unreal by Input/UnrealInputDevice), with every binding as the original's.

#include "UnityCompat/UnityTypes.h"

namespace SpaceGod::Input
{
	struct ButtonControl;

	/// <summary>
	/// One frame of player input, polled from the Input System devices. Keeping all key reads in
	/// one place makes the bindings easy to find and keeps the camera and app logic device-free.
	/// </summary>
	class InputState final
	{
	public:
		/// <summary>x = strafe, y = rise, z = forward; each in [-1, 1].</summary>
		Vector3 Move;
		Vector2 LookDelta;
		bool LookHeld = false;

		/// <summary>Raw mouse delta, unconditional (unlike <see cref="LookDelta"/>, which only reports while
		/// the right button is held for camera orbit) -- piloting reads this for always-on heading control.</summary>
		Vector2 MouseDelta;
		float Scroll = 0;
		bool Boost = false;
		bool Slow = false;

		/// <summary>Held while piloting: engages the cruise drive (Elite-style) for fast open-space
		/// travel. See <see cref="Unity.SpaceGod.Core.PilotCruiseState"/>.</summary>
		bool Cruise = false;

		/// <summary>While piloting a capital ship (U.SPC-T499): W rings its engine telegraph one step ahead, S one step astern (0: neither).</summary>
		int ThrottleStep = 0;

		/// <summary>While piloting: roll, -1 (Z, anticlockwise) .. 1 (X, clockwise), seen from behind.</summary>
		float Roll = 0;

		/// <summary>While piloting: switches flight assist on or off (shares F with <see cref="Focus"/>,
		/// which is gated off while piloting, so the two never fire together).</summary>
		bool ToggleFlightAssist = false;

		/// <summary>Held, not click-edged: while piloting, fires the ship's forward-mount weapons.
		/// Left click still selects normally outside piloting, via the click-edged <see cref="SelectClicked"/>
		/// below (gated off while piloting, so holding the trigger down and releasing it never also selects).</summary>
		bool Fire = false;

		/// <summary>While piloting, in a fight (U.SPC-T271): the EMP trigger (middle mouse, held), and a mine dropped
		/// astern (Ctrl, on the press).</summary>
		bool FireEmp = false, DropMine = false;

		bool SelectClicked = false;
		bool SelectDoubleClicked = false;
		Vector2 MousePosition;

		bool Focus = false;
		bool Escape = false;
		bool ToggleScale = false;
		bool TimeFaster = false;
		bool TimeSlower = false;
		bool TogglePause = false;
		bool ResetTime = false;
		bool ToggleOrbits = false;
		bool ToggleLabels = false;
		bool ToggleHelp = false;
		/// <summary>U: the lighter toggle, the HUD's panels and readouts off while the names and markers on the view stay.</summary>
		bool ToggleHud = false;

		/// <summary>F11 (U.SPC-T496): clears the screen of all its UI but the reticle in use, and brings it all back.</summary>
		bool ClearScreen = false;
		bool ToggleMarket = false;
		bool ToggleCargo = false;
		bool ToggleOutfit = false;

		/// <summary>While piloting: switches silent running on or off (F3; see <see cref="Unity.SpaceGod.Core.Ship.SilentRunning"/>).</summary>
		bool ToggleSilent = false;

		/// <summary>The bounty board of the port in view (F4): who is wanted there, for how much, and where last seen.</summary>
		bool ToggleBounties = false;

		/// <summary>The contracts board of the port in view (F7): jobs to take, and the player's own (U.SPC-T101).</summary>
		bool ToggleContracts = false;

		/// <summary>The player's own fleet and property (F10; F9 is the model viewer's): owned ships, captains, standing orders, the books.</summary>
		bool ToggleFleet = false;

		/// <summary>The trade desk (backslash, U.SPC-T426): runs, brokers, trade news, charters, insurance, warehouses, the back door.</summary>
		bool ToggleTrade = false;

		/// <summary>[ (left bracket): the galaxy map (U.SPC-T103; on F11 until U.SPC-T496 gave that to the clear screen).</summary>
		bool ToggleGalaxyMap = false;

		/// <summary>F12 (U.SPC-T427): hails the polity of the ship locked, selected or hailing you, and opens the envoy screen to treat with it.</summary>
		bool HailPolity = false;

		/// <summary>The news feed: the full log of what has happened, filterable (X).</summary>
		bool ToggleNews = false;

		/// <summary>Dev/sandbox panel: spawn a ship of any class at the camera and pilot it.</summary>
		bool ToggleSpawn = false;

		/// <summary>U.SPC-T601: Shift+V puts the practice fighter at the camera and the player at its helm.</summary>
		bool SpawnPractice = false;
		bool WatchBattle = false;
		bool ToggleLanes = false;
		bool ToggleSatellites = false;

		/// <summary>U.SPC-T595: Shift+K or numpad * moves the system in view on to its next skybox (+1); Shift+Alt+K or
		/// numpad / back to the one before (-1).</summary>
		int CycleSky = 0;
		bool ToggleReorient = false;
		bool CycleShips = false;
		bool CycleBackward = false;
		bool Home = false;
		int NumberKey = -1;

		/// <summary>Takes or releases direct control of the selected/focused ship.</summary>
		bool Pilot = false;

		/// <summary>Take charge (U.SPC-T422): ' takes the next seat in the fight in view (a ship's helm or a launched
		/// fighter's cockpit), ; the one before (0: neither).</summary>
		int CycleSeat = 0;

		/// <summary>Take charge (U.SPC-T422): / mans the next gun on the hull flown or selected, and past its last gun
		/// goes back up to the helm.</summary>
		bool ManGun = false;

		/// <summary>At a gun mount (U.SPC-T445): Backspace moves up from the gun to the helm of the hull it is mounted on
		/// (in the strategy view Backspace is Home).</summary>
		bool MoveToHost = false;

		/// <summary>F9 (U.SPC-T446): the take-charge picker, every seat on the thing in hand and around the camera to click into.</summary>
		bool TogglePicker = false;

		/// <summary>While piloting in a fight (U.SPC-T488): Insert retunes the hull's phaser arrays, a narrow lance for hulls or a wide sweep for fighters and missiles.</summary>
		bool TunePhasers = false;

		/// <summary>B at the controls (in the strategy view B watches a battle): the boarding command panel, for a fight aboard the piloted hull or one its marines are boarding (U.SPC-T468).</summary>
		bool ToggleBoarding = false;
		/// <summary>At a big ship's helm (U.SPC-T491): Shift+Insert orders the crew to run hot, keeping the guns and drive at full however
		/// hot the hull gets, or to throttle down again.</summary>
		bool ToggleRunHot = false;

		/// <summary>While piloting: locks (or clears) the camera's tracking onto the hovered/selected
		/// ship. Shares its key with <see cref="ResetTime"/>, which is gated off while piloting so the
		/// two never fire together.</summary>
		bool LockTarget = false;

		/// <summary>While piloting, in range of one of this system's jump gates: activates it.</summary>
		bool ActivateGate = false;

		/// <summary>While piloting a hull with its own FTL drive (U.SPC-T417): spools the drive for what the nose is on (Enter), or calls a charge off.</summary>
		bool EngageFtl = false;

		/// <summary>While piloting a hull with its own FTL drive (U.SPC-T420): End engages the FTL autopilot for the plotted
		/// course (plotting one for the star the nose is on if there is none), or lets go of it.</summary>
		bool ToggleFtlAutopilot = false;

		/// <summary>While piloting a hull with its own FTL drive (U.SPC-T420): Page Down plots a course to the next star or
		/// beacon out, nearest first, Page Up the one before (0: neither).</summary>
		int CycleFtlTarget = 0;

		/// <summary>While piloting (U.SPC-T420): Delete clears the plotted FTL course.</summary>
		bool ClearFtlCourse = false;

		/// <summary>At a capital ship's helm (U.SPC-T502): Shift+Delete orders the helm assist to roll the hull's weak shield
		/// patches away from the heaviest fire, or lets the roll go back to the helmsman.</summary>
		bool ToggleRollPatches = false;

		/// <summary>F5 saves to the quicksave slot, F8 loads it, F6 opens the save slots.</summary>
		bool QuickSave = false, QuickLoad = false, ToggleSaves = false;

		/// <summary>While capture runs (US-T72, docs/CAPTURE.md section 5): Alt+F9 a screenshot, Alt+F10 toggles an
		/// on-demand recording, Alt+F11 saves the replay. Local to this machine: never sent to co-op peers (they are
		/// read-only properties, which PilotFrame's field list leaves out, as Unity's PilotFrame skips properties).</summary>
		bool CaptureScreenshot() const { return captureScreenshot; }
		bool CaptureToggleRecording() const { return captureToggleRecording; }
		bool CaptureSaveReplay() const { return captureSaveReplay; }

	private:
		bool captureScreenshot = false, captureToggleRecording = false, captureSaveReplay = false;
		Vector2 pressPosition;
		float lastClickTime = -10.0f;
		Vector2 lastClickPosition;
		bool rightDragged = false;
		Vector2 rightPressPosition;

	public:
		/// <summary>True on the frame the right button was released without dragging (a "right click").</summary>
		bool RightClicked = false;

		/// <summary>Reads this frame's input; with <paramref name="suspended"/> (another screen has the
		/// keyboard and mouse, such as the model viewer) everything reads as idle instead.</summary>
		void Poll(bool pointerOverUi, bool suspended = false);

		/// <summary>Drops this frame's right-button look (a full screen of its own has the mouse): no look, no right-click.</summary>
		void ReleaseLook();

	private:
		void Reset();

		static float Axis(const ButtonControl& positive, const ButtonControl& negative);
	};
}
