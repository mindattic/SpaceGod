// BindingCoreTests: unit checks of the input bindings' pure part (US-T437, SpaceGod/Input/BindingCore.h): every kind of
// binding writes and reads back the same, a profile's text from before US-T437 ("Shift+T, NumpadMultiply", "None") loads
// exactly as it did (and gains the action's default pad binding, as the pad map of before still worked), "Pad:None" keeps
// a cleared pad binding cleared, nonsense is refused, and the trigger modes time press, hold, double-tap and long press.
//
//   BindingCoreTests.exe     exit code 0 = every check passed, 1 = failures (listed)
//
// Built only by tools/build-core.ps1 (SPACEGOD_STANDALONE); the UE module compiles it to nothing.

#ifdef SPACEGOD_STANDALONE

#include <cstdio>
#include <string>
#include <vector>

#include "../../../SpaceGod/Input/BindingCore.h"

using namespace SpaceGod::Input;

namespace
{
	int failures = 0, checks = 0;

	void Check(bool ok, const std::string& what)
	{
		checks++;
		if (ok) return;
		failures++;
		std::printf("FAIL  %s\n", what.c_str());
	}

	/// Format, parse back, format again: the same binding and the same text.
	void RoundTrip(const InputBinding& b, const char* text)
	{
		Check(b.Format() == text, std::string("format ") + text + " (got " + b.Format() + ")");
		InputBinding back;
		Check(InputBinding::TryParse(text, back), std::string("parse ") + text);
		Check(back == b, std::string("round trip ") + text);
		Check(back.Format() == text, std::string("format again ") + text);
	}

	struct Frame
	{
		bool down, pressed, released;
	};

	/// Steps a mode over frames 1/60 s apart; returns the presses and the frames held.
	void Run(Trigger t, bool heldKind, const std::vector<Frame>& frames, int& presses, int& held, double dt = 1.0 / 60, double start = 100)
	{
		BindingTuning tune;
		TriggerState s;
		presses = held = 0;
		for (size_t i = 0; i < frames.size(); i++)
		{
			TriggerOut o = StepTrigger(s, t, heldKind, frames[i].down, frames[i].pressed, frames[i].released, start + i * dt, tune);
			presses += o.pressed ? 1 : 0;
			held += o.held ? 1 : 0;
		}
	}

	/// n frames held after a press, then a release.
	std::vector<Frame> Tap(int heldFrames, int gapFrames = 0)
	{
		std::vector<Frame> f;
		f.push_back({ true, true, false });
		for (int i = 1; i < heldFrames; i++) f.push_back({ true, false, false });
		f.push_back({ false, false, true });
		for (int i = 0; i < gapFrames; i++) f.push_back({ false, false, false });
		return f;
	}

	std::vector<Frame> operator+(std::vector<Frame> a, const std::vector<Frame>& b)
	{
		a.insert(a.end(), b.begin(), b.end());
		return a;
	}
}

int main()
{
	// Every kind of binding round-trips.
	RoundTrip(InputBinding(Key::T), "T");
	RoundTrip(InputBinding(Key::T, KeyMods::Shift), "Shift+T");
	RoundTrip(InputBinding(Key::K, KeyMods::Shift | KeyMods::Alt), "Shift+Alt+K");
	RoundTrip(InputBinding(Key::F, KeyMods::Ctrl | KeyMods::Shift | KeyMods::Alt), "Ctrl+Shift+Alt+F");
	RoundTrip(InputBinding(Key::NumpadMultiply), "NumpadMultiply");
	RoundTrip(InputBinding(Key::LeftShift), "LeftShift");
	RoundTrip(InputBinding(Key::CapsLock), "CapsLock");
	RoundTrip(InputBinding(Key::Numpad7), "Numpad7");
	RoundTrip(InputBinding::OfMouse(MouseButton::Back), "Mouse4");
	RoundTrip(InputBinding::OfMouse(MouseButton::Forward), "Mouse5");
	RoundTrip(InputBinding::OfMouse(MouseButton::Middle, KeyMods::Ctrl), "Ctrl+MouseMiddle");
	RoundTrip(InputBinding::OfMouse(MouseButton::Left, KeyMods::Shift), "Shift+MouseLeft");
	RoundTrip(InputBinding::OfWheel(true), "WheelUp");
	RoundTrip(InputBinding::OfWheel(false, KeyMods::Alt), "Alt+WheelDown");
	RoundTrip(InputBinding::OfPad(PadButton::A), "Pad:A");
	RoundTrip(InputBinding::OfPad(PadButton::LeftStick), "Pad:LS");
	RoundTrip(InputBinding::OfPad(PadButton::DPadLeft), "Pad:DPadLeft");
	RoundTrip(InputBinding::OfPad(PadButton::RightTrigger), "Pad:RT");
	RoundTrip(InputBinding::OfTrigger(false, 75), "Pad:LT>75");
	RoundTrip(InputBinding::OfStick(StickDir::LeftUp), "Pad:LeftStickUp");
	RoundTrip(InputBinding::OfStick(StickDir::RightRight, 30), "Pad:RightStickRight>30");
	RoundTrip(InputBinding(Key::Z).With(Trigger::DoubleTap), "DoubleTap:Z");
	RoundTrip(InputBinding(Key::R, KeyMods::Shift).With(Trigger::LongPress), "LongPress:Shift+R");
	RoundTrip(InputBinding::OfPad(PadButton::Y).With(Trigger::Hold), "Hold:Pad:Y");
	RoundTrip(InputBinding::OfMouse(MouseButton::Back).With(Trigger::Press), "Press:Mouse4");
	RoundTrip(InputBinding::OfStick(StickDir::LeftDown, 60).With(Trigger::DoubleTap), "DoubleTap:Pad:LeftStickDown>60");
	RoundTrip(InputBinding(), "None");
	// Every key's name round-trips but Esc (fixed) and None.
	for (int i = static_cast<int>(Key::None) + 1; i < static_cast<int>(Key::Count); i++)
	{
		auto k = static_cast<Key>(i);
		if (k == Key::Escape) continue;
		RoundTrip(InputBinding(k), ToString(k));
	}
	for (int i = static_cast<int>(PadButton::None) + 1; i < static_cast<int>(PadButton::Count); i++)
	{
		InputBinding b = InputBinding::OfPad(static_cast<PadButton>(i));
		InputBinding back;
		Check(InputBinding::TryParse(b.Format(), back) && back == b, std::string("pad round trip ") + b.Format());
	}
	// Modifiers in any order read the same; whitespace is trimmed.
	{
		InputBinding a, b;
		Check(InputBinding::TryParse("Alt+Shift+K", a) && InputBinding::TryParse("  Shift+Alt+K ", b) && a == b, "modifier order and spaces");
	}
	// Refused: Esc, nonsense, a threshold on a button, a bad threshold, a mode without an input.
	for (const char* bad : { "Escape", "Shift+Escape", "Banana", "Pad:Banana", "Pad:A>50", "Pad:LT>0", "Pad:LT>101", "Pad:LT>x", "DoubleTap:", "Mouse6", "Shift+" })
	{
		InputBinding b;
		Check(!InputBinding::TryParse(bad, b), std::string("refuses ") + bad);
	}
	// Displays.
	Check(InputBinding(Key::NumpadMultiply).Display() == "Numpad *", "display numpad *");
	Check(InputBinding::OfMouse(MouseButton::Back).Display() == "Mouse 4", "display mouse 4");
	Check(InputBinding::OfPad(PadButton::Y).Display() == "Pad Y", "display pad Y");
	Check(InputBinding::OfStick(StickDir::LeftUp).With(Trigger::DoubleTap).Display() == "Pad LS up (double-tap)", "display stick double-tap");
	Check(InputBinding::OfPad(PadButton::RightStick).PadGlyph() == "RS click", "glyph RS click");

	// An action's slots: the profile text of before US-T437 loads exactly as it did.
	{
		const InputBinding skyNext[BindingSlots] = { InputBinding(Key::K, KeyMods::Shift), InputBinding(Key::NumpadMultiply) };
		InputBinding out[BindingSlots];
		Check(ParseSlots("Shift+K, NumpadMultiply", skyNext, false, out), "legacy two keys parse");
		Check(out[0] == InputBinding(Key::K, KeyMods::Shift) && out[1] == InputBinding(Key::NumpadMultiply) && out[2].Empty(), "legacy two keys in order");
		Check(FormatSlots(out, skyNext) == "Shift+K, NumpadMultiply", "legacy two keys format back");
		Check(ParseSlots("Shift+T", skyNext, false, out) && out[0] == InputBinding(Key::T, KeyMods::Shift) && out[1].Empty(), "legacy one key");
		Check(ParseSlots("None", skyNext, false, out) && out[0].Empty() && out[1].Empty() && out[2].Empty(), "legacy None");
		Check(FormatSlots(out, skyNext) == "None", "None formats back");
		Check(ParseSlots("", skyNext, false, out) && out[0].Empty(), "empty text is None");
		InputBinding keep[BindingSlots] = { InputBinding(Key::A) };
		Check(!ParseSlots("A, B, C, D", skyNext, false, keep) && keep[0] == InputBinding(Key::A), "four refused, slots untouched");
		Check(!ParseSlots("A, Banana", skyNext, false, keep), "a bad part refuses the line");
		Check(ParseSlots("Shift+K, , NumpadMultiply", skyNext, false, out) && out[1] == InputBinding(Key::NumpadMultiply), "an empty part is skipped");
	}
	// An action that has a pad default (the pad map of before): a legacy line keeps its keys and gains the pad's, so the pad
	// keeps working as it did; a new line keeps the pad binding it names, or none with "Pad:None".
	{
		const InputBinding help[BindingSlots] = { InputBinding(Key::H), InputBinding(Key::F1), InputBinding::OfPad(PadButton::View) };
		InputBinding out[BindingSlots];
		Check(ParseSlots("H", help, false, out) && out[0] == InputBinding(Key::H) && out[1] == InputBinding::OfPad(PadButton::View) && out[2].Empty(), "legacy gains the pad default");
		Check(FormatSlots(out, help) == "H, Pad:View", "and writes it");
		Check(ParseSlots("None", help, false, out) && out[0] == InputBinding::OfPad(PadButton::View) && out[1].Empty(), "legacy None keeps the pad");
		Check(ParseSlots("H, F1, G", help, false, out) && out[2] == InputBinding(Key::G), "legacy full: no room for the pad");
		InputBinding cleared[BindingSlots] = { InputBinding(Key::H) };
		Check(FormatSlots(cleared, help) == "H, Pad:None", "a cleared pad is written as Pad:None");
		Check(ParseSlots("H, Pad:None", help, false, out) && out[0] == InputBinding(Key::H) && out[1].Empty(), "Pad:None stays cleared");
		InputBinding none[BindingSlots];
		Check(FormatSlots(none, help) == "Pad:None", "all cleared");
		Check(ParseSlots("Pad:None", help, false, out) && out[0].Empty(), "all cleared reads back");
		Check(ParseSlots("Mouse4, Pad:Y", help, false, out) && out[0] == InputBinding::OfMouse(MouseButton::Back) && out[1] == InputBinding::OfPad(PadButton::Y) && out[2].Empty(),
			"a new line keeps its own pad binding");
		InputBinding mixed[BindingSlots] = { InputBinding(Key::Z).With(Trigger::DoubleTap), InputBinding::OfMouse(MouseButton::Back), InputBinding::OfStick(StickDir::LeftUp, 70) };
		std::string text = FormatSlots(mixed, help);
		Check(text == "DoubleTap:Z, Mouse4, Pad:LeftStickUp>70", "mixed slots format (" + text + ")");
		Check(ParseSlots(text, help, false, out) && out[0] == mixed[0] && out[1] == mixed[1] && out[2] == mixed[2], "mixed slots round trip");
	}
	// A mode written as the action's own is the same as none (no prefix).
	{
		const InputBinding none[BindingSlots] = {};
		InputBinding out[BindingSlots];
		Check(ParseSlots("Hold:W", none, true, out) && out[0].trigger == Trigger::Auto && FormatSlots(out, none) == "W", "Hold on a held action is its own");
		Check(ParseSlots("Press:T", none, false, out) && out[0].trigger == Trigger::Auto, "Press on a pressed action is its own");
		Check(ParseSlots("Hold:T", none, false, out) && out[0].trigger == Trigger::Hold && FormatSlots(out, none) == "Hold:T", "Hold on a pressed action is kept");
		Check(ParseSlots("Press:W", none, true, out) && out[0].trigger == Trigger::Press, "Press on a held action is kept");
	}
	// Answers: the same input with the same touch clashes; a double-tap or long press on it does not.
	{
		Check(Answers(InputBinding(Key::Z), true, InputBinding(Key::Z), false), "Z held and Z pressed clash");
		Check(!Answers(InputBinding(Key::Z), true, InputBinding(Key::Z).With(Trigger::DoubleTap), false), "Z and double-tap Z do not");
		Check(!Answers(InputBinding(Key::Z), true, InputBinding(Key::Z, KeyMods::Shift), false), "Z and Shift+Z do not");
		Check(Answers(InputBinding::OfPad(PadButton::Y), false, InputBinding::OfPad(PadButton::Y), false), "pad Y and pad Y clash");
		Check(Answers(InputBinding::OfStick(StickDir::LeftUp, 30), false, InputBinding::OfStick(StickDir::LeftUp, 80), false), "a stick way clashes whatever the threshold");
	}

	// Trigger timing.
	int presses = 0, held = 0;
	// Auto on a pressed action: one press per tap, held while down (as before US-T437).
	Run(Trigger::Auto, false, Tap(5, 3), presses, held);
	Check(presses == 1 && held == 5, "auto press: one press, held 5");
	// Auto on a held action: held while down, the press edge too.
	Run(Trigger::Auto, true, Tap(10), presses, held);
	Check(presses == 1 && held == 10, "auto hold: held 10");
	// Hold on a pressed action: a press at each end (on while held).
	Run(Trigger::Hold, false, Tap(10, 2), presses, held);
	Check(presses == 2 && held == 10, "hold on a pressed action: press and release");
	// Press on a held action: a tap latches on, the next off.
	{
		BindingTuning tune;
		TriggerState s;
		auto step = [&](Frame f, double t) { return StepTrigger(s, Trigger::Press, true, f.down, f.pressed, f.released, t, tune); };
		TriggerOut o = step({ true, true, false }, 0);
		Check(o.held && o.pressed, "latch: first tap on");
		o = step({ false, false, true }, 0.1);
		Check(o.held && !o.pressed, "latch: stays on after the release");
		o = step({ false, false, false }, 2.0);
		Check(o.held, "latch: still on later");
		o = step({ true, true, false }, 2.1);
		Check(!o.held && !o.pressed, "latch: second tap off");
	}
	// Double-tap: two taps within the window fire once; held while the second is.
	Run(Trigger::DoubleTap, false, Tap(3, 6) + Tap(8, 2), presses, held); // second press 10 frames (0.17 s) after the first
	Check(presses == 1 && held == 8, "double-tap within the window");
	Run(Trigger::DoubleTap, false, Tap(3, 30) + Tap(3, 2), presses, held); // 34 frames (0.57 s) apart
	Check(presses == 0 && held == 0, "double-tap too slow");
	Run(Trigger::DoubleTap, false, Tap(3, 30) + Tap(3, 4) + Tap(3, 2), presses, held); // the late one starts a new pair
	Check(presses == 1, "a slow tap then a quick pair");
	Run(Trigger::DoubleTap, false, Tap(2, 3) + Tap(2, 3) + Tap(2, 3), presses, held); // three quick taps: one pair, the third starts over
	Check(presses == 1, "three quick taps fire once");
	{
		// Exactly at the window's edge counts; just past it does not.
		BindingTuning tune;
		TriggerState s;
		StepTrigger(s, Trigger::DoubleTap, false, true, true, false, 10.0, tune);
		StepTrigger(s, Trigger::DoubleTap, false, false, false, true, 10.05, tune);
		Check(StepTrigger(s, Trigger::DoubleTap, false, true, true, false, 10.0 + tune.DoubleTapS - 1e-6, tune).pressed, "double-tap at the window's edge");
		TriggerState s2;
		StepTrigger(s2, Trigger::DoubleTap, false, true, true, false, 10.0, tune);
		Check(!StepTrigger(s2, Trigger::DoubleTap, false, true, true, false, 10.0 + tune.DoubleTapS + 0.01, tune).pressed, "double-tap past the window");
	}
	// Long press: fires once after the time, held from then until let go; a short tap never fires.
	Run(Trigger::LongPress, false, Tap(60, 2), presses, held); // 1 s held
	Check(presses == 1, "long press fires once");
	Check(held >= 28 && held <= 31, "long press held from 0.5 s (" + std::to_string(held) + " frames)");
	Run(Trigger::LongPress, false, Tap(20, 2), presses, held); // 0.33 s
	Check(presses == 0 && held == 0, "a short tap is no long press");
	Run(Trigger::LongPress, false, Tap(40, 2) + Tap(40, 2), presses, held); // two holds of 0.67 s
	Check(presses == 2, "each long hold fires");
	{
		// A held action on long press is held only after the time.
		BindingTuning tune;
		TriggerState s;
		Check(!StepTrigger(s, Trigger::LongPress, true, true, true, false, 0, tune).held, "long press not held at once");
		Check(!StepTrigger(s, Trigger::LongPress, true, true, false, false, tune.LongPressS - 0.01, tune).held, "not held before the time");
		Check(StepTrigger(s, Trigger::LongPress, true, true, false, false, tune.LongPressS + 0.01, tune).held, "held after the time");
		Check(!StepTrigger(s, Trigger::LongPress, true, false, false, true, tune.LongPressS + 0.1, tune).held, "let go");
	}
	// A tap shorter than a frame (both edges at once) is still a press.
	Run(Trigger::Auto, false, { { false, true, true } }, presses, held);
	Check(presses == 1, "sub-frame tap");

	std::printf("BindingCoreTests: %d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

#endif
