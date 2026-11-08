#pragma once
// Port of Unity.SpaceGod Runtime/Input/InputState.cs (US-T29). Unity's Input System devices are read through the
// seam in Input/InputDevice.h (fed from Unreal by Input/UnrealInputDevice), each key through the action map
// (US-T249, Input/KeyBindings.h: the original's keys unless the profile rebinds them). The gamepad (US-T250,
// Input/Gamepad.h) feeds the same fields after them.

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

		/// <summary>US-T127: Shift+J at a helm spools the in-system cruise drive (<see cref="Core.SystemCruise"/>), or drops out
		/// of it. An edge; J held alone stays the tactical cruise tier above.</summary>
		bool ToggleSystemCruise = false;

		/// <summary>While piloting a capital ship (U.SPC-T499): W rings its engine telegraph one step ahead, S one step astern (0: neither).</summary>
		int ThrottleStep = 0;

		/// <summary>While piloting: roll, -1 (Z, anticlockwise) .. 1 (X, clockwise), seen from behind.</summary>
		float Roll = 0;

		/// <summary>While piloting: switches flight assist on or off (shares F with <see cref="Focus"/>,
		/// which is gated off while piloting, so the two never fire together).</summary>
		bool ToggleFlightAssist = false;

		/// <summary>Held, while piloting or in a fighter seat (US-T87): Alt, the manoeuvre key. The hull swings its nose onto
		/// the retrograde for a turn-and-burn (see <see cref="Unity.SpaceGod.Core.PilotFlight.FlipAim"/>).</summary>
		bool Manoeuvre = false;

		/// <summary>Held, at a fighter's controls (US-T165): R, drift (<see cref="Core.Drift"/>). Shares R with
		/// <see cref="ToggleReorient"/>, which is the command view's alone (the chase camera is never re-levelled anyway).</summary>
		bool Drift = false;

		/// <summary>Edge, at the controls (US-T94): the arrow keys share the reactor (a <see cref="Core.PowerKey"/>): Left a pip
		/// to weapons, Up to shields, Right to engines, Down an even share; with Shift, all it can spare to that one. -1 none.</summary>
		int PowerKey = -1;

		/// <summary>At the controls (US-T97), Shift+Backspace, the self-destruct: <see cref="SelfDestructTap"/> the press (an
		/// edge: aborts a countdown under way), <see cref="SelfDestructArm"/> the moment it has been held for
		/// <see cref="Core.SelfDestruct.ArmHoldS"/> (an edge, once a press: arms it), and <see cref="SelfDestructHoldS"/> how
		/// long it has been held (the HUD's arming bar; 0 when it is not).</summary>
		bool SelfDestructTap = false, SelfDestructArm = false;
		float SelfDestructHoldS = 0;

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

		/// <summary>US-T261: Shift+L opens the logs screen (the ship logs and station records recovered, to read), or closes it.
		/// L alone stays the labels. Local to this machine, like the Help screen.</summary>
		bool ToggleLogs = false;

		/// <summary>US-T259: Shift+O opens the discoveries screen (everything found, by kind and day), or closes it. O alone stays
		/// the orbits. Local to this machine, like the Help screen.</summary>
		bool ToggleDiscoveries = false;

		/// <summary>US-T126: Shift+F1 pulls the dev menu down (spawn ships and enemies, travel, helm, time, scale), or puts
		/// it away. F1 alone stays the help screen. Local to this machine: the menu is never a co-op peer's.</summary>
		bool ToggleCheatMenu = false;

		/// <summary>US-T186: Shift+F2 opens the dev menu straight on its Spawn tab (every hull, station, pirate group and
		/// Bio Mass growth, each to spawn or to spawn and pilot), or puts it away. F2 alone stays outfitting.</summary>
		bool ToggleSpawnMenu = false;

		/// <summary>U: the lighter toggle, the HUD's panels and readouts off while the names and markers on the view stay.</summary>
		bool ToggleHud = false;

		/// <summary>F11 (U.SPC-T496): clears the screen of all its UI but the reticle in use, and brings it all back.</summary>
		bool ClearScreen = false;
		bool ToggleMarket = false;

		/// <summary>US-T266, Shift+M at a helm: docking clearance at the port alongside, then the port screen open or closed.</summary>
		bool DockPort = false;
		bool ToggleCargo = false;
		bool ToggleOutfit = false;

		/// <summary>While piloting: switches silent running on or off (F3; see <see cref="Unity.SpaceGod.Core.Ship.SilentRunning"/>).</summary>
		bool ToggleSilent = false;

		/// <summary>At a helm (US-T93): Tab, an active ping (in command Tab cycles the ships); Shift+F3, power down to go
		/// dark, or from dark power up again (see <see cref="Core.SensorPosture"/>).</summary>
		bool SensorPing = false, PowerToggle = false;

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

		/// <summary>US-T258: Shift+[ : the system map, the tactical map of the system the player is in.</summary>
		bool ToggleSystemMap = false;

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

		/// <summary>P, the embody key (US-T135): embodies what is selected (or under the mouse), whatever it is -- a ship's helm, a hull
		/// or station in a fight -- or, at any controls, lets go back to the free SpaceGod view.</summary>
		bool Pilot = false;

		/// <summary>Shift+P (US-T135): out on the hull held, flown or selected as its next EVA repair worker.</summary>
		bool EmbodyCrew = false;

		/// <summary>Shift+Y (US-T166): in a launched craft's cockpit, eject: the pilot out in a pressure suit (EVA), the craft
		/// left coasting on.</summary>
		bool Eject = false;

		/// <summary>Take charge (U.SPC-T422): ' takes the next seat in the fight in view (a ship's helm or a launched
		/// fighter's cockpit), ; the one before (0: neither).</summary>
		int CycleSeat = 0;

		/// <summary>Take charge (U.SPC-T422): / mans the next gun on the hull flown or selected, and past its last gun
		/// goes back up to the helm.</summary>
		bool ManGun = false;

		/// <summary>US-T106: Shift+/ jumps into the newest craft the carrier flown (or the one whose craft is held) launched,
		/// again for the next, and past the last back to the carrier's helm.</summary>
		bool JumpToCraft = false;

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

		/// <summary>US-T95: Shift+T at the controls, the tractor beam on the nearest cargo pod in reach (or off). T alone locks.</summary>
		bool ToggleTractor = false;

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
		/// US-T97: when Shift+Backspace went down (-1 while it is not held) and whether this press has armed already.
		float selfDestructDownAt = -1.0f;
		bool selfDestructArmed = false;
		/// US-T250: this frame's pad meanings that depend on who owns the frame, kept for Arbitrate: the right stick's look
		/// (the command view's orbit), the secondary trigger (a seat's right button), the bumpers (roll at the controls, rise
		/// and sink in command), the d-pad press (0 left, 1 up, 2 right, 3 down; -1 none: the reactor at the controls, time
		/// and ships in command) and the drift button's press (back out, in command). Cleared with every frame (Reset).
		Vector2 padLook;
		bool padSecondary = false, padBack = false;
		float padBumpers = 0;
		int padDpad = -1;
		/// US-T250: the left stick's last step past half way (1 ahead, -1 astern, 0 centred): a capital's telegraph rings
		/// once a push, as W and S ring it once a press. Kept across frames.
		int padThrottleLatch = 0;

	public:
		/// <summary>True on the frame the right button was released without dragging (a "right click").</summary>
		bool RightClicked = false;

		/// <summary>Reads this frame's input; with <paramref name="suspended"/> (another screen has the
		/// keyboard and mouse, such as the model viewer) everything reads as idle instead.</summary>
		void Poll(bool pointerOverUi, bool suspended = false);

		/// <summary>Drops this frame's right-button look (a full screen of its own has the mouse): no look, no right-click.</summary>
		void ReleaseLook();

		// ---------------------------------------------------------------- input ownership (US-T90)
		//
		// One owner per input per frame, by priority: the pause menu (it takes the frame before anything else reads it),
		// then the open panels (the mouse, through ClickPanelOpen), then whatever the player is embodied in (a helm, a
		// cockpit, a gun, a station), then the camera (free swim, focus orbit, travel). The owner is decided once, at the
		// start of the frame; a key that means one thing at the controls and another in command (F flight assist / focus,
		// T lock / reset time, Backspace up to the host / home, B boarding / watch a battle, X roll / news, Tab, the number
		// keys) reaches only the owner's meaning.

		/// <summary>Clears the meanings of the shared keys that do not belong to this frame's owner: the embodied craft's
		/// when <paramref name="atControls"/>, the command view's otherwise.</summary>
		void Arbitrate(bool atControls);

		/// <summary>The owner changed part-way through this frame: what was pressed has been spent by the old owner, so
		/// the new one gets an idle frame (every edge and every held key cleared).</summary>
		void Handover() { Reset(); }

		/// <summary>No stick, trigger or look held: what was held across an owner change has been let go.</summary>
		bool HandsIdle() const;

		/// <summary>Keys and buttons still held from before an owner change are not the new owner's until let go: the
		/// stick, the triggers and the look read idle, and a click released now selects nothing.</summary>
		void HoldHandsOff();

		/// <summary>What the camera reads while it follows an embodied craft: the craft has the stick, the triggers and the
		/// look, so only the wheel (the chase's zoom) is left to it.</summary>
		InputState ForChaseCamera() const;

	private:
		void Reset();

		/// US-T250: the pad's part of the frame (Input/Gamepad.h), after the keyboard's and mouse's.
		void PollPad();

		static float Axis(const ButtonControl& positive, const ButtonControl& negative);
	};
}
