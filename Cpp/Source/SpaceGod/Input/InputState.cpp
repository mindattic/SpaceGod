// Port of Unity.SpaceGod Runtime/Input/InputState.cs (US-T29).

#include "Input/InputState.h"
#include "Input/InputDevice.h"
#include "Input/DriftKey.h"
#include "Input/Gamepad.h" // US-T250
#include "Input/KeyBindings.h" // US-T249
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

		// US-T249: every key through the action map (Input/KeyBindings.h): the profile's bindings, the original's keys by default.
		using A = KeyAction;
		auto down = [&](KeyAction a) { return KeyBindings::Held(a, kb); };
		auto hit = [&](KeyAction a) { return KeyBindings::Pressed(a, kb); };
		auto axis = [&](KeyAction positive, KeyAction negative) { return (down(positive) ? 1.0f : 0.0f) - (down(negative) ? 1.0f : 0.0f); };

		Move = Vector3(axis(A::StrafeRight, A::StrafeLeft), axis(A::Rise, A::Sink), axis(A::MoveForward, A::MoveBack));
		Move.y = Mathf::Clamp(Move.y, -1.0f, 1.0f);
		Boost = down(A::Boost);
		Slow = down(A::Slow);
		Cruise = down(A::Cruise);
		ToggleSystemCruise = hit(A::SystemCruise); // US-T127: Shift+J, the cruise drive
		ThrottleStep = hit(A::MoveForward) ? 1 : hit(A::MoveBack) ? -1 : 0;
		Roll = axis(A::RollRight, A::RollLeft);
		// US-T87: Alt held is the manoeuvre key at the controls (a turn-and-burn's flip); with F9-F11 under capture, and
		// Shift+Alt+K, Alt keeps its other meanings, which only read it with another key.
		Manoeuvre = down(A::Manoeuvre);
		// US-T165: at the controls, held (or pressed on, then off, set to toggle); R alone toggles auto-reorient in command.
		// US-T250: the pad's drift button is R too (one latch for both in toggle mode), but only while the pad is the game's.
		const ButtonControl& padDrift = Gamepad::MenuMode ? ButtonControl() : Gamepad::Action(PadAction::Drift);
		Drift = DriftKey::Read(down(A::Drift) || padDrift.isPressed, hit(A::Drift) || padDrift.wasPressedThisFrame);
		// US-T94: the arrows move reactor power at the controls (Core::PowerKey): Left weapons, Up shields, Right engines a
		// pip at a time, Down an even share; Shift with one of the three, all the reactor can spare to it.
		PowerKey = hit(A::PowerAllWeapons) ? 3 : hit(A::PowerWeapons) ? 0
			: hit(A::PowerAllShields) ? 4 : hit(A::PowerShields) ? 1
			: hit(A::PowerAllEngines) ? 5 : hit(A::PowerEngines) ? 2
			: hit(A::PowerBalance) ? 6 : -1;

		MousePosition = mouse.position;
		MouseDelta = mouse.delta;
		Fire = mouse.leftButton.isPressed && !pointerOverUi;
		FireEmp = mouse.middleButton.isPressed && !pointerOverUi;
		DropMine = hit(A::DropMine);
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

		Focus = hit(A::Focus);
		ToggleFlightAssist = hit(A::FlightAssist);
		Escape = kb[Key::Escape].wasPressedThisFrame; // fixed: Esc backs out of everything, menus included
		ToggleScale = hit(A::ToggleScale);
		TimeFaster = hit(A::TimeFaster);
		TimeSlower = hit(A::TimeSlower);
		TogglePause = hit(A::TogglePause);
		ResetTime = hit(A::ResetTime);
		LockTarget = hit(A::LockTarget);
		ToggleTractor = hit(A::Tractor); // US-T95
		ToggleOrbits = hit(A::Orbits);
		ToggleDiscoveries = hit(A::Discoveries); // US-T259
		ToggleLabels = hit(A::Labels);
		ToggleLogs = hit(A::Logs); // US-T261
		// US-T126: Shift+F1 is the dev menu's, F1 alone (or H) the help screen's.
		ToggleHelp = hit(A::Help);
		ToggleCheatMenu = hit(A::CheatMenu);
		ToggleHud = hit(A::ToggleHud);
		// US-T266: Shift+M asks the port alongside for docking clearance (and opens or closes the port screen), M alone the market.
		ToggleMarket = hit(A::Market);
		DockPort = hit(A::DockPort);
		ToggleCargo = hit(A::Cargo);
		// US-T186: Shift+F2 is the spawn menu's (the dev menu on its Spawn tab), F2 alone outfitting's.
		ToggleOutfit = hit(A::Outfit);
		ToggleSpawnMenu = hit(A::SpawnMenu);
		// US-T93: Shift+F3 powers down (or up again), F3 alone is silent running; Tab at a helm is the active ping.
		ToggleSilent = hit(A::SilentRunning);
		PowerToggle = hit(A::PowerToggle);
		SensorPing = hit(A::SensorPing);
		ToggleBounties = hit(A::Bounties);
		ToggleContracts = hit(A::Contracts);
		ToggleFleet = hit(A::Fleet);
		ToggleTrade = hit(A::Trade);
		ToggleGalaxyMap = hit(A::GalaxyMap);
		ToggleSystemMap = hit(A::SystemMap); // US-T258: Shift+[, the system map
		ClearScreen = hit(A::ClearScreen);
		HailPolity = hit(A::Hail);
		ToggleNews = hit(A::News);
		ToggleSpawn = hit(A::SpawnPilot);
		SpawnPractice = hit(A::SpawnPractice);
		WatchBattle = hit(A::WatchBattle);
		ToggleLanes = hit(A::Lanes);
		ToggleSatellites = hit(A::Satellites);
		CycleSky = hit(A::SkyPrev) ? -1 : hit(A::SkyNext) ? 1 : 0;
		ToggleReorient = hit(A::Reorient);
		CycleBackward = hit(A::CycleBackward);
		CycleShips = hit(A::CycleShips) || CycleBackward;
		// US-T97: Shift+Backspace is the self-destruct's, never home or up to the host (the map gives the press to the
		// more specific chord).
		Home = hit(A::Home);
		Pilot = hit(A::Pilot);
		EmbodyCrew = hit(A::EmbodyCrew); // US-T135
		Eject = hit(A::Eject); // US-T166
		CycleSeat = hit(A::NextSeat) ? 1 : hit(A::PrevSeat) ? -1 : 0;
		ManGun = hit(A::ManGun);
		JumpToCraft = hit(A::JumpToCraft); // US-T106: Shift+/ into a launched craft
		MoveToHost = hit(A::MoveToHost);
		// US-T97, the self-destruct, two steps so it never goes off by accident: the press is a tap (it aborts a countdown
		// under way), and held a second (Core::SelfDestruct::ArmHoldS) it arms, once a press. Let go of either key and the
		// hold starts again.
		{
			bool held = down(A::SelfDestruct);
			float now = Time::unscaledTime;
			SelfDestructTap = hit(A::SelfDestruct);
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
		TogglePicker = hit(A::Picker);
		// US-T72 (docs/CAPTURE.md section 5): while capture runs, Alt+F9 / Alt+F10 / Alt+F11 are its screenshot, toggle
		// recording and save replay instead of the picker, fleet and clear screen. Without capture Alt changes nothing.
		if (CaptureEvents::Running() && (kb[Key::LeftAlt].isPressed || kb[Key::RightAlt].isPressed))
		{
			captureScreenshot = TogglePicker;
			captureToggleRecording = ToggleFleet;
			captureSaveReplay = ClearScreen;
			TogglePicker = ToggleFleet = ClearScreen = false;
		}
		TunePhasers = hit(A::TunePhasers);
		ToggleBoarding = hit(A::Boarding);
		ToggleRunHot = hit(A::RunHot);
		ActivateGate = hit(A::ActivateGate);
		EngageFtl = hit(A::EngageFtl);
		ToggleFtlAutopilot = hit(A::FtlAutopilot);
		CycleFtlTarget = hit(A::NextFtlTarget) ? 1 : hit(A::PrevFtlTarget) ? -1 : 0;
		ClearFtlCourse = hit(A::ClearFtlCourse);
		ToggleRollPatches = hit(A::RollPatches);
		QuickSave = hit(A::QuickSave);
		QuickLoad = hit(A::QuickLoad);
		ToggleSaves = hit(A::Saves);

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
