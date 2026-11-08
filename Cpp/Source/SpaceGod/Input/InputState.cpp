// Port of Unity.SpaceGod Runtime/Input/InputState.cs (US-T29).

#include "Input/InputState.h"
#include "Input/InputDevice.h"
#include "Input/DriftKey.h"
#include "Input/Gamepad.h" // US-T250
#include "CaptureEvents.h"
#include "Embodiment/SelfDestruct.h"

namespace SpaceGod::Input
{
	void InputState::Poll(bool pointerOverUi, bool suspended)
	{
		// Keyboard.current and Mouse.current: the one device seam (Input/InputDevice.h).
		const InputDevice& kb = InputDevice::current();
		const InputDevice& mouse = InputDevice::current();
		Reset();
		if (suspended || !kb.present || !mouse.present) return;

		Move = Vector3(
			Axis(kb[Key::D], kb[Key::A]),
			Axis(kb[Key::E], kb[Key::Q]) + Axis(kb[Key::RightBracket], kb[Key::C]),
			Axis(kb[Key::W], kb[Key::S]));
		Move.y = Mathf::Clamp(Move.y, -1.0f, 1.0f);
		Boost = kb[Key::LeftShift].isPressed || kb[Key::RightShift].isPressed;
		Slow = kb[Key::LeftCtrl].isPressed || kb[Key::RightCtrl].isPressed;
		Cruise = kb[Key::J].isPressed;
		ToggleSystemCruise = kb[Key::J].wasPressedThisFrame && Boost; // US-T127: Shift+J, the cruise drive
		ThrottleStep = kb[Key::W].wasPressedThisFrame ? 1 : kb[Key::S].wasPressedThisFrame ? -1 : 0;
		Roll = Axis(kb[Key::X], kb[Key::Z]);
		// US-T87: Alt held is the manoeuvre key at the controls (a turn-and-burn's flip); with F9-F11 under capture, and
		// Shift+Alt+K, Alt keeps its other meanings, which only read it with another key.
		Manoeuvre = kb[Key::LeftAlt].isPressed || kb[Key::RightAlt].isPressed;
		// US-T165: at the controls, held (or pressed on, then off, set to toggle); R alone toggles auto-reorient in command.
		// US-T250: the pad's drift button is R too (one latch for both in toggle mode), but only while the pad is the game's.
		const ButtonControl& padDrift = Gamepad::MenuMode ? ButtonControl() : Gamepad::Action(PadAction::Drift);
		Drift = DriftKey::Read(kb[Key::R].isPressed || padDrift.isPressed, kb[Key::R].wasPressedThisFrame || padDrift.wasPressedThisFrame);
		// US-T94: the arrows move reactor power at the controls (Core::PowerKey): Left weapons, Up shields, Right engines a
		// pip at a time, Down an even share; Shift with one of the three, all the reactor can spare to it.
		PowerKey = kb[Key::LeftArrow].wasPressedThisFrame ? (Boost ? 3 : 0)
			: kb[Key::UpArrow].wasPressedThisFrame ? (Boost ? 4 : 1)
			: kb[Key::RightArrow].wasPressedThisFrame ? (Boost ? 5 : 2)
			: kb[Key::DownArrow].wasPressedThisFrame ? 6 : -1;

		MousePosition = mouse.position;
		MouseDelta = mouse.delta;
		Fire = mouse.leftButton.isPressed && !pointerOverUi;
		FireEmp = mouse.middleButton.isPressed && !pointerOverUi;
		DropMine = kb[Key::LeftCtrl].wasPressedThisFrame || kb[Key::RightCtrl].wasPressedThisFrame;
		LookHeld = mouse.rightButton.isPressed;
		LookDelta = LookHeld ? MouseDelta : Vector2::zero();
		if (mouse.rightButton.wasPressedThisFrame)
		{
			rightPressPosition = MousePosition;
			rightDragged = false;
		}
		if (LookHeld && (MousePosition - rightPressPosition).sqrMagnitude() > 16) rightDragged = true;
		if (mouse.rightButton.isPressed && mouse.delta.sqrMagnitude() > 4) rightDragged = true;
		RightClicked = mouse.rightButton.wasReleasedThisFrame && !rightDragged;

		if (!pointerOverUi)
		{
			float wheel = mouse.scrollY;
			if (Mathf::Abs(wheel) > 0.01f)
			{
				// Standard mice report multiples of 120 per notch; trackpads and high-resolution wheels
				// already report small per-notch values directly, so scale each convention on its own
				// rather than dividing both by 120 and snapping the underflowed result to a full notch.
				float normalized = Mathf::Abs(wheel) < 2.0f ? wheel : wheel / 120.0f;
				Scroll = Mathf::Clamp(normalized, -3.0f, 3.0f);
			}
			else
			{
				Scroll = 0.0f;
			}
		}

		if (mouse.leftButton.wasPressedThisFrame) pressPosition = MousePosition;
		if (mouse.leftButton.wasReleasedThisFrame && !pointerOverUi && (MousePosition - pressPosition).sqrMagnitude() < 36)
		{
			SelectClicked = true;
			float now = Time::unscaledTime;
			SelectDoubleClicked = now - lastClickTime < 0.35f && (MousePosition - lastClickPosition).sqrMagnitude() < 100;
			lastClickTime = SelectDoubleClicked ? -10.0f : now;
			lastClickPosition = MousePosition;
		}

		Focus = kb[Key::F].wasPressedThisFrame;
		ToggleFlightAssist = kb[Key::F].wasPressedThisFrame;
		Escape = kb[Key::Escape].wasPressedThisFrame;
		ToggleScale = kb[Key::G].wasPressedThisFrame || kb[Key::Backquote].wasPressedThisFrame;
		TimeFaster = kb[Key::Period].wasPressedThisFrame || kb[Key::Equals].wasPressedThisFrame || kb[Key::NumpadPlus].wasPressedThisFrame;
		TimeSlower = kb[Key::Comma].wasPressedThisFrame || kb[Key::Minus].wasPressedThisFrame || kb[Key::NumpadMinus].wasPressedThisFrame;
		TogglePause = kb[Key::Space].wasPressedThisFrame || kb[Key::Pause].wasPressedThisFrame;
		ResetTime = kb[Key::T].wasPressedThisFrame;
		LockTarget = kb[Key::T].wasPressedThisFrame && !Boost;
		ToggleTractor = kb[Key::T].wasPressedThisFrame && Boost; // US-T95
		ToggleOrbits = kb[Key::O].wasPressedThisFrame && !Boost;
		ToggleDiscoveries = kb[Key::O].wasPressedThisFrame && Boost; // US-T259
		ToggleLabels = kb[Key::L].wasPressedThisFrame && !Boost;
		ToggleLogs = kb[Key::L].wasPressedThisFrame && Boost; // US-T261
		// US-T126: Shift+F1 is the dev menu's, F1 alone (or H) the help screen's.
		ToggleHelp = kb[Key::H].wasPressedThisFrame || (kb[Key::F1].wasPressedThisFrame && !Boost);
		ToggleCheatMenu = kb[Key::F1].wasPressedThisFrame && Boost;
		ToggleHud = kb[Key::U].wasPressedThisFrame;
		// US-T266: Shift+M asks the port alongside for docking clearance (and opens or closes the port screen), M alone the market.
		ToggleMarket = kb[Key::M].wasPressedThisFrame && !Boost;
		DockPort = kb[Key::M].wasPressedThisFrame && Boost;
		ToggleCargo = kb[Key::I].wasPressedThisFrame;
		// US-T186: Shift+F2 is the spawn menu's (the dev menu on its Spawn tab), F2 alone outfitting's.
		ToggleOutfit = kb[Key::F2].wasPressedThisFrame && !Boost;
		ToggleSpawnMenu = kb[Key::F2].wasPressedThisFrame && Boost;
		// US-T93: Shift+F3 powers down (or up again), F3 alone is silent running; Tab at a helm is the active ping.
		ToggleSilent = kb[Key::F3].wasPressedThisFrame && !Boost;
		PowerToggle = kb[Key::F3].wasPressedThisFrame && Boost;
		SensorPing = kb[Key::Tab].wasPressedThisFrame;
		ToggleBounties = kb[Key::F4].wasPressedThisFrame;
		ToggleContracts = kb[Key::F7].wasPressedThisFrame;
		ToggleFleet = kb[Key::F10].wasPressedThisFrame;
		ToggleTrade = kb[Key::Backslash].wasPressedThisFrame;
		ToggleGalaxyMap = kb[Key::LeftBracket].wasPressedThisFrame && !Boost;
		ToggleSystemMap = kb[Key::LeftBracket].wasPressedThisFrame && Boost; // US-T258: Shift+[, the system map
		ClearScreen = kb[Key::F11].wasPressedThisFrame;
		HailPolity = kb[Key::F12].wasPressedThisFrame;
		ToggleNews = kb[Key::X].wasPressedThisFrame;
		ToggleSpawn = kb[Key::V].wasPressedThisFrame && !Boost;
		SpawnPractice = kb[Key::V].wasPressedThisFrame && Boost;
		WatchBattle = kb[Key::B].wasPressedThisFrame;
		ToggleLanes = kb[Key::N].wasPressedThisFrame;
		bool skyShift = kb[Key::LeftShift].isPressed || kb[Key::RightShift].isPressed;
		bool skyAlt = kb[Key::LeftAlt].isPressed || kb[Key::RightAlt].isPressed;
		ToggleSatellites = kb[Key::K].wasPressedThisFrame && !skyShift;
		CycleSky = (kb[Key::K].wasPressedThisFrame && skyShift && skyAlt) || kb[Key::NumpadDivide].wasPressedThisFrame ? -1
			: (kb[Key::K].wasPressedThisFrame && skyShift) || kb[Key::NumpadMultiply].wasPressedThisFrame ? 1 : 0;
		ToggleReorient = kb[Key::R].wasPressedThisFrame;
		CycleShips = kb[Key::Tab].wasPressedThisFrame;
		CycleBackward = CycleShips && Boost;
		// US-T97: Shift+Backspace is the self-destruct's, never home or up to the host.
		bool backspaceAlone = kb[Key::Backspace].wasPressedThisFrame && !Boost;
		Home = kb[Key::Home].wasPressedThisFrame || backspaceAlone;
		Pilot = kb[Key::P].wasPressedThisFrame && !Boost;
		EmbodyCrew = kb[Key::P].wasPressedThisFrame && Boost; // US-T135
		Eject = kb[Key::Y].wasPressedThisFrame && Boost; // US-T166
		CycleSeat = kb[Key::Quote].wasPressedThisFrame ? 1 : kb[Key::Semicolon].wasPressedThisFrame ? -1 : 0;
		ManGun = kb[Key::Slash].wasPressedThisFrame && !Boost;
		JumpToCraft = kb[Key::Slash].wasPressedThisFrame && Boost; // US-T106: Shift+/ into a launched craft
		MoveToHost = backspaceAlone;
		// US-T97, the self-destruct, two steps so it never goes off by accident: the press is a tap (it aborts a countdown
		// under way), and held a second (Core::SelfDestruct::ArmHoldS) it arms, once a press. Let go of either key and the
		// hold starts again.
		{
			bool held = Boost && kb[Key::Backspace].isPressed;
			float now = Time::unscaledTime;
			SelfDestructTap = Boost && kb[Key::Backspace].wasPressedThisFrame;
			if (SelfDestructTap || (held && selfDestructDownAt < 0))
			{
				selfDestructDownAt = now;
				selfDestructArmed = false;
			}
			if (!held) selfDestructDownAt = -1.0f;
			SelfDestructHoldS = selfDestructDownAt >= 0 ? now - selfDestructDownAt : 0.0f;
			if (selfDestructDownAt >= 0 && !selfDestructArmed && SelfDestructHoldS >= static_cast<float>(Core::SelfDestruct::ArmHoldS))
			{
				SelfDestructArm = true;
				selfDestructArmed = true;
			}
		}
		TogglePicker = kb[Key::F9].wasPressedThisFrame;
		// US-T72 (docs/CAPTURE.md section 5): while capture runs, Alt+F9 / Alt+F10 / Alt+F11 are its screenshot, toggle
		// recording and save replay instead of the picker, fleet and clear screen. Without capture Alt changes nothing.
		if (CaptureEvents::Running() && (kb[Key::LeftAlt].isPressed || kb[Key::RightAlt].isPressed))
		{
			captureScreenshot = TogglePicker;
			captureToggleRecording = ToggleFleet;
			captureSaveReplay = ClearScreen;
			TogglePicker = ToggleFleet = ClearScreen = false;
		}
		bool shifted = kb[Key::LeftShift].isPressed || kb[Key::RightShift].isPressed;
		TunePhasers = kb[Key::Insert].wasPressedThisFrame && !shifted;
		ToggleBoarding = kb[Key::B].wasPressedThisFrame;
		ToggleRunHot = kb[Key::Insert].wasPressedThisFrame && shifted;
		ActivateGate = kb[Key::Y].wasPressedThisFrame;
		EngageFtl = kb[Key::Enter].wasPressedThisFrame || kb[Key::NumpadEnter].wasPressedThisFrame;
		ToggleFtlAutopilot = kb[Key::End].wasPressedThisFrame;
		CycleFtlTarget = kb[Key::PageDown].wasPressedThisFrame ? 1 : kb[Key::PageUp].wasPressedThisFrame ? -1 : 0;
		ClearFtlCourse = kb[Key::Delete].wasPressedThisFrame && !shifted;
		ToggleRollPatches = kb[Key::Delete].wasPressedThisFrame && shifted;
		QuickSave = kb[Key::F5].wasPressedThisFrame;
		QuickLoad = kb[Key::F8].wasPressedThisFrame;
		ToggleSaves = kb[Key::F6].wasPressedThisFrame;

		const Key digits[] = { Key::Digit0, Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5, Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9 };
		for (int i = 0; i < 10; i++)
			if (kb[digits[i]].wasPressedThisFrame) NumberKey = i;

		PollPad(); // US-T250
	}

	void InputState::PollPad()
	{
		const GamepadDevice& pad = GamepadDevice::current();
		if (!pad.present) return;
		auto held = [](PadAction a) { return Gamepad::Action(a).isPressed; };
		auto pressed = [](PadAction a) { return Gamepad::Action(a).wasPressedThisFrame; };

		// A menu, panel or screen is up: the pad is the GUI's (UnityCompat/GuiPadNav moves the focus and presses), and the
		// game hears only Esc from B or the pause button, as the keyboard's Esc closes what is open.
		if (Gamepad::MenuMode)
		{
			if (pad[PadButton::B].wasPressedThisFrame || pressed(PadAction::Pause)) Escape = true;
			return;
		}

		// The left stick: thrust and strafe at the controls, the camera's swim in command (W/S and A/D).
		Vector2 move = Gamepad::Stick(pad.leftStick);
		Move.x = Mathf::Clamp(Move.x + move.x, -1.0f, 1.0f);
		Move.z = Mathf::Clamp(Move.z + move.y, -1.0f, 1.0f);
		// A capital's telegraph: a step ahead or astern each time the stick is pushed past half way (W and S once a press).
		int step = move.y > 0.6f ? 1 : move.y < -0.6f ? -1 : Mathf::Abs(move.y) < 0.3f ? 0 : padThrottleLatch;
		if (step != padThrottleLatch && step != 0 && ThrottleStep == 0) ThrottleStep = step;
		padThrottleLatch = step;

		// The right stick: the look (the string aim's at the controls, US-T168), as the mouse moves it; in command the
		// orbit, which Arbitrate hands the camera as a held right button.
		padLook = Gamepad::AimDelta(Time::unscaledDeltaTime);
		MouseDelta += padLook;

		Fire = Fire || held(PadAction::Fire);
		padSecondary = held(PadAction::Secondary);
		padBumpers = (held(PadAction::RollRight) ? 1.0f : 0.0f) - (held(PadAction::RollLeft) ? 1.0f : 0.0f);
		Roll = Mathf::Clamp(Roll + padBumpers, -1.0f, 1.0f);
		Boost = Boost || held(PadAction::Boost);
		Manoeuvre = Manoeuvre || held(PadAction::Flip);
		padBack = pressed(PadAction::Drift); // the drift itself was read with R, above
		if (pressed(PadAction::LockFocus))
		{
			// Y: lock at the controls, focus in command (Arbitrate keeps the owner's meaning, as T and F share theirs).
			LockTarget = true;
			Focus = true;
		}
		if (pressed(PadAction::FlightAssist)) ToggleFlightAssist = true;
		if (pressed(PadAction::Embody)) Pilot = true;
		if (pressed(PadAction::Pause)) Escape = true;
		if (pressed(PadAction::Help)) ToggleHelp = true;

		padDpad = pad[PadButton::DPadLeft].wasPressedThisFrame ? 0 : pad[PadButton::DPadUp].wasPressedThisFrame ? 1
			: pad[PadButton::DPadRight].wasPressedThisFrame ? 2 : pad[PadButton::DPadDown].wasPressedThisFrame ? 3 : -1;
	}

	void InputState::ReleaseLook()
	{
		LookHeld = false;
		LookDelta = Vector2::zero();
		RightClicked = false;
	}

	void InputState::Arbitrate(bool atControls)
	{
		if (atControls)
		{
			// The craft has F (flight assist), T (lock), Backspace (up to the host), B (boarding), X (roll), the number
			// keys; the command view's meanings of them stand aside.
			Focus = ResetTime = Home = WatchBattle = ToggleNews = CycleShips = CycleBackward = false;
			ToggleReorient = false; // US-T165: R held is the craft's drift
			// US-T250: the pad at the controls: the left trigger is the seat's second trigger (the right button's), the d-pad
			// shares the reactor as the arrows do (left weapons, up shields, right engines, down an even share).
			if (padSecondary) LookHeld = true;
			if (PowerKey < 0 && padDpad >= 0) PowerKey = padDpad == 3 ? 6 : padDpad;
			return;
		}
		// US-T250: the pad in command: the right stick orbits and looks about (the right button held), the bumpers sink and
		// rise (C and E), the d-pad steps time (up faster, down slower) and the ships (right the next, left the one before),
		// and the drift button backs out (Esc).
		if (padLook.sqrMagnitude() > 0)
		{
			LookHeld = true;
			LookDelta = LookDelta + padLook;
		}
		Move.y = Mathf::Clamp(Move.y + padBumpers, -1.0f, 1.0f);
		if (padDpad == 1) TimeFaster = true;
		else if (padDpad == 3) TimeSlower = true;
		else if (padDpad == 2) CycleShips = true;
		else if (padDpad == 0) CycleShips = CycleBackward = true;
		if (padBack) Escape = true;
		// In command nothing is flown: F focuses, T resets time, Backspace goes home, B watches a battle.
		ToggleFlightAssist = LockTarget = MoveToHost = ToggleBoarding = Manoeuvre = JumpToCraft = false; // US-T106: no craft to jump from
		Drift = false; // US-T165: R toggles auto-reorient
		DriftKey::Drop(); // US-T165: a toggled drift ends with the controls
		PowerKey = -1; // US-T94: nothing to share a reactor of in command
		SelfDestructTap = SelfDestructArm = false; // US-T97: nothing to set off in command
		SelfDestructHoldS = 0;
		SensorPing = PowerToggle = false; // US-T93: nothing to ping from or power down in command
		ToggleSystemCruise = false; // US-T127: no drive to spool in command
		ToggleTractor = false; // US-T95: no beam in command
		Eject = false; // US-T166: no cockpit to eject from in command
		ThrottleStep = 0;
		Roll = 0;
	}

	bool InputState::HandsIdle() const
	{
		return Move.sqrMagnitude() < 0.0001f && Roll == 0 && !Fire && !FireEmp && !LookHeld && !Boost && !Manoeuvre && !Cruise && !Drift;
	}

	void InputState::HoldHandsOff()
	{
		Move = Vector3::zero();
		Roll = 0;
		ThrottleStep = 0;
		Fire = FireEmp = Boost = Slow = Manoeuvre = Cruise = Drift = false;
		LookHeld = false;
		LookDelta = Vector2::zero();
		SelectClicked = SelectDoubleClicked = RightClicked = false;
	}

	InputState InputState::ForChaseCamera() const
	{
		InputState chase = *this;
		chase.Move = Vector3::zero();
		chase.LookHeld = false;
		chase.LookDelta = Vector2::zero();
		chase.Boost = chase.Slow = false;
		return chase;
	}

	void InputState::Reset()
	{
		Move = Vector3::zero();
		LookDelta = Vector2::zero();
		LookHeld = false;
		MouseDelta = Vector2::zero();
		Scroll = 0;
		Roll = 0;
		ToggleFlightAssist = false;
		Manoeuvre = false;
		Drift = false; // US-T165
		PowerKey = -1;
		SelfDestructTap = SelfDestructArm = false;
		SelfDestructHoldS = 0;
		SensorPing = PowerToggle = false; // US-T93
		Boost = Slow = Fire = Cruise = FireEmp = DropMine = false;
		SelectClicked = SelectDoubleClicked = RightClicked = false;
		Focus = Escape = ToggleScale = TimeFaster = TimeSlower = TogglePause = ResetTime = false;
		ToggleOrbits = ToggleLabels = ToggleHelp = ToggleHud = ClearScreen = ToggleMarket = DockPort = ToggleCargo = ToggleOutfit = ToggleSilent = ToggleBounties = ToggleContracts = ToggleFleet = ToggleTrade = ToggleGalaxyMap = ToggleNews = ToggleSpawn = WatchBattle = ToggleLanes = ToggleSatellites = ToggleReorient = CycleShips = CycleBackward = Home = Pilot = LockTarget = ActivateGate = EngageFtl = false;
		QuickSave = QuickLoad = ToggleSaves = false;
		ToggleSystemMap = false; // US-T258
		SpawnPractice = false;
		ToggleCheatMenu = false; // US-T126
		ToggleLogs = false; // US-T261
		ToggleDiscoveries = false; // US-T259
		ToggleSpawnMenu = false; // US-T186
		ToggleSystemCruise = false; // US-T127
		ToggleTractor = false; // US-T95
		ToggleFtlAutopilot = ClearFtlCourse = false;
		CycleFtlTarget = 0;
		CycleSeat = 0;
		CycleSky = 0;
		ThrottleStep = 0;
		ManGun = false;
		EmbodyCrew = false; // US-T135
		Eject = false; // US-T166
		JumpToCraft = false;
		MoveToHost = false;
		TogglePicker = false;
		TunePhasers = false;
		ToggleBoarding = false;
		ToggleRunHot = false;
		ToggleRollPatches = false;
		NumberKey = -1;
		HailPolity = false; // US-T70: as Unity's Reset; without it a suspended frame kept F12's last edge and hailed again
		captureScreenshot = false; // US-T72
		captureToggleRecording = false;
		captureSaveReplay = false;
		padLook = Vector2::zero(); // US-T250
		padSecondary = padBack = false;
		padBumpers = 0;
		padDpad = -1;
	}

	float InputState::Axis(const ButtonControl& positive, const ButtonControl& negative)
	{
		return (positive.isPressed ? 1.0f : 0.0f) - (negative.isPressed ? 1.0f : 0.0f);
	}
}
