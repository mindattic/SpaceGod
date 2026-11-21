#pragma once

// US-T437: the pure part of the input bindings (Input/KeyBindings.h): the inputs a binding can name (a key with the
// modifiers held with it, a mouse button or wheel step, a pad button, trigger or stick direction), how it is written in a
// profile and shown on the page, how its trigger mode (press, hold, double-tap, long press) turns raw edges into an
// action's held state and presses, and the three slots of an action read from and written to their profile text.
//
// Plain C++ and the standard library only: no engine, no Cs layer, so tools/build-core.ps1's standalone
// BindingCoreTests can check it (Cpp/Source/SpaceGodCore/Tests/Bindings).
//
// The text, one binding:
//   [Trigger:]input        Trigger: Press, Hold, DoubleTap, LongPress; none = the action's own (see Trigger::Auto)
//   input, a key:          [Ctrl+][Shift+][Alt+]KeyName       "T", "Shift+T", "NumpadMultiply", "LeftShift"
//   input, the mouse:      [mods]MouseLeft|MouseRight|MouseMiddle|Mouse4|Mouse5|WheelUp|WheelDown
//   input, the pad:        Pad:A|B|X|Y|LB|RB|View|Menu|LS|RS|DPadUp|DPadDown|DPadLeft|DPadRight     (buttons)
//                          Pad:LT|RT[>NN]                      (triggers; >NN: pulled past NN%, else the pad's own click)
//                          Pad:LeftStickUp|...|RightStickRight[>NN]  (a stick pushed that way past NN%, default 50%)
// An action's slots: the bindings joined by ", " ("Shift+K, NumpadMultiply"), "None" when all are empty. Before
// US-T437 a profile held keys only and the pad had its own map; such text (no "Pad:" in it) loads as it always did and
// gains the action's default pad bindings in its free slots, so the pad keeps working; once the player clears an
// action's pad binding, "Pad:None" is written to say so.

#include <cmath>
#include <cstdlib>
#include <string>

namespace SpaceGod::Input
{
	// ---------------------------------------------------------------- keys

	/// <summary>
	/// The keys the game reads, by the names of Unity Input System's <c>Key</c> members the original's bindings use
	/// (<c>kb.wKey</c> is <see cref="Key::W"/>, <c>kb.backquoteKey</c> is <see cref="Key::Backquote"/>...).
	/// <see cref="UnrealInputDevice"/> maps each onto its Unreal <c>FKey</c>. <c>Count</c> is not a key (the table size).
	/// US-T437 added Caps Lock and the numpad's digits and point at the end (bindable; nothing reads them by default but
	/// Caps Lock, the HUD mode's key).
	/// </summary>
	enum class Key : int
	{
		None,
		A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
		Space, Enter, NumpadEnter, Escape, Tab, Backspace, Backquote, Quote, Semicolon, Comma, Period, Slash, Backslash,
		LeftBracket, RightBracket, Minus, Equals, NumpadPlus, NumpadMinus, NumpadMultiply, NumpadDivide,
		LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
		Insert, Delete, Home, End, PageUp, PageDown, Pause,
		UpArrow, DownArrow, LeftArrow, RightArrow,
		CapsLock, Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9, NumpadPeriod,
		Count,
	};

	/// <summary>The key's name as a profile writes it ("Backquote", "NumpadMultiply").</summary>
	inline const char* ToString(Key key)
	{
		static const char* const names[] =
		{
			"None",
			"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
			"Digit0", "Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
			"Space", "Enter", "NumpadEnter", "Escape", "Tab", "Backspace", "Backquote", "Quote", "Semicolon", "Comma", "Period", "Slash", "Backslash",
			"LeftBracket", "RightBracket", "Minus", "Equals", "NumpadPlus", "NumpadMinus", "NumpadMultiply", "NumpadDivide",
			"LeftShift", "RightShift", "LeftCtrl", "RightCtrl", "LeftAlt", "RightAlt",
			"Insert", "Delete", "Home", "End", "PageUp", "PageDown", "Pause",
			"UpArrow", "DownArrow", "LeftArrow", "RightArrow",
			"CapsLock", "Numpad0", "Numpad1", "Numpad2", "Numpad3", "Numpad4", "Numpad5", "Numpad6", "Numpad7", "Numpad8", "Numpad9", "NumpadPeriod",
			"Count",
		};
		static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(Key::Count) + 1, "Key names out of step with the enum");
		int i = static_cast<int>(key);
		return i >= 0 && i <= static_cast<int>(Key::Count) ? names[i] : "";
	}

	/// <summary>The key a profile names (<see cref="Key::None"/> for anything else).</summary>
	inline Key KeyNamed(const std::string& name)
	{
		for (int i = static_cast<int>(Key::None) + 1; i < static_cast<int>(Key::Count); i++)
			if (name == ToString(static_cast<Key>(i))) return static_cast<Key>(i);
		return Key::None;
	}

	inline bool IsModifierKey(Key k)
	{
		return k == Key::LeftShift || k == Key::RightShift || k == Key::LeftCtrl || k == Key::RightCtrl || k == Key::LeftAlt || k == Key::RightAlt;
	}

	/// <summary>The key as a player reads it: the letter or symbol on the keycap.</summary>
	inline std::string KeyDisplay(Key k)
	{
		switch (k)
		{
			case Key::Digit0: return "0"; case Key::Digit1: return "1"; case Key::Digit2: return "2"; case Key::Digit3: return "3";
			case Key::Digit4: return "4"; case Key::Digit5: return "5"; case Key::Digit6: return "6"; case Key::Digit7: return "7";
			case Key::Digit8: return "8"; case Key::Digit9: return "9";
			case Key::NumpadEnter: return "Numpad Enter";
			case Key::Escape: return "Esc";
			case Key::Backquote: return "`";
			case Key::Quote: return "'";
			case Key::Semicolon: return ";";
			case Key::Comma: return ",";
			case Key::Period: return ".";
			case Key::Slash: return "/";
			case Key::Backslash: return "\\";
			case Key::LeftBracket: return "[";
			case Key::RightBracket: return "]";
			case Key::Minus: return "-";
			case Key::Equals: return "=";
			case Key::NumpadPlus: return "Numpad +";
			case Key::NumpadMinus: return "Numpad -";
			case Key::NumpadMultiply: return "Numpad *";
			case Key::NumpadDivide: return "Numpad /";
			case Key::LeftShift: return "Left Shift";
			case Key::RightShift: return "Right Shift";
			case Key::LeftCtrl: return "Left Ctrl";
			case Key::RightCtrl: return "Right Ctrl";
			case Key::LeftAlt: return "Left Alt";
			case Key::RightAlt: return "Right Alt";
			case Key::Delete: return "Del";
			case Key::PageUp: return "PgUp";
			case Key::PageDown: return "PgDn";
			case Key::UpArrow: return "Up";
			case Key::DownArrow: return "Down";
			case Key::LeftArrow: return "Left";
			case Key::RightArrow: return "Right";
			case Key::CapsLock: return "Caps Lock";
			case Key::NumpadPeriod: return "Numpad .";
			default: break;
		}
		if (k >= Key::Numpad0 && k <= Key::Numpad9) return std::string("Numpad ") + static_cast<char>('0' + (static_cast<int>(k) - static_cast<int>(Key::Numpad0)));
		return ToString(k);
	}

	// ---------------------------------------------------------------- the mouse and the pad

	/// <summary>The mouse's buttons: Back and Forward are the side buttons (Mouse 4 and Mouse 5).</summary>
	enum class MouseButton : int
	{
		Left,
		Right,
		Middle,
		Back,
		Forward,
		Count,
	};

	inline const char* ToString(MouseButton button)
	{
		switch (button)
		{
			case MouseButton::Left: return "Left";
			case MouseButton::Right: return "Right";
			case MouseButton::Middle: return "Middle";
			case MouseButton::Back: return "Back";
			case MouseButton::Forward: return "Forward";
			default: return "";
		}
	}

	/// <summary>An Xbox-layout pad's buttons (Unreal's Gamepad_FaceButton_Bottom is A ...). <c>Count</c> is not a button.</summary>
	enum class PadButton : int
	{
		None,
		A, B, X, Y,
		LeftBumper, RightBumper, LeftTrigger, RightTrigger,
		View, Menu, LeftStick, RightStick,
		DPadUp, DPadDown, DPadLeft, DPadRight,
		Count,
	};

	/// <summary>The button's glyph as the Help screen and the settings print it ("A", "LB", "RT", "View" ...).</summary>
	inline const char* ToString(PadButton button)
	{
		static const char* const names[] =
		{
			"None",
			"A", "B", "X", "Y",
			"LB", "RB", "LT", "RT",
			"View", "Menu", "LS", "RS",
			"D-pad up", "D-pad down", "D-pad left", "D-pad right",
			"Count",
		};
		static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(PadButton::Count) + 1, "PadButton names out of step with the enum");
		int i = static_cast<int>(button);
		return i >= 0 && i <= static_cast<int>(PadButton::Count) ? names[i] : "";
	}

	/// <summary>The button as a profile writes it after "Pad:" (no spaces: "DPadUp").</summary>
	inline const char* PadToken(PadButton button)
	{
		static const char* const names[] =
		{
			"None",
			"A", "B", "X", "Y",
			"LB", "RB", "LT", "RT",
			"View", "Menu", "LS", "RS",
			"DPadUp", "DPadDown", "DPadLeft", "DPadRight",
			"Count",
		};
		static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(PadButton::Count) + 1, "PadButton tokens out of step with the enum");
		int i = static_cast<int>(button);
		return i >= 0 && i <= static_cast<int>(PadButton::Count) ? names[i] : "";
	}

	/// <summary>A stick pushed one way (an <see cref="InputBinding"/>'s code for <see cref="BindSource::PadStick"/>).</summary>
	enum class StickDir : int
	{
		LeftUp, LeftDown, LeftLeft, LeftRight, RightUp, RightDown, RightLeft, RightRight,
		Count,
	};

	inline const char* StickToken(StickDir d)
	{
		static const char* const names[] =
		{
			"LeftStickUp", "LeftStickDown", "LeftStickLeft", "LeftStickRight", "RightStickUp", "RightStickDown", "RightStickLeft", "RightStickRight",
		};
		int i = static_cast<int>(d);
		return i >= 0 && i < static_cast<int>(StickDir::Count) ? names[i] : "";
	}

	inline const char* StickGlyph(StickDir d)
	{
		static const char* const names[] = { "LS up", "LS down", "LS left", "LS right", "RS up", "RS down", "RS left", "RS right" };
		int i = static_cast<int>(d);
		return i >= 0 && i < static_cast<int>(StickDir::Count) ? names[i] : "";
	}

	/// <summary>Modifier flags of a key, mouse button or wheel binding (a pad binding has none).</summary>
	namespace KeyMods
	{
		constexpr int None = 0, Shift = 1, Ctrl = 2, Alt = 4;
	}

	// ---------------------------------------------------------------- the binding

	/// <summary>What kind of input a binding names. A pad trigger is its own source (pulled past a threshold, or the pad's
	/// own click); a stick direction too. Codes: a <see cref="Key"/>, a <see cref="MouseButton"/>, the wheel (0 up, 1 down),
	/// a <see cref="PadButton"/>, a trigger (0 left, 1 right), a <see cref="StickDir"/>.</summary>
	enum class BindSource : int
	{
		None,
		Key,
		Mouse,
		Wheel,
		PadButton,
		PadTrigger,
		PadStick,
	};

	/// <summary>How a binding turns its input into the action. Auto: the action's own way (a held action is held while the
	/// input is; any other is pressed on its press), which is what every binding was before US-T437 and what a profile
	/// line without a prefix means.
	///   Press      a held action latches on a press and off on the next (tap to toggle); any other fires on the press.
	///   Hold       a held action is held while the input is; any other fires on the press and again on the release
	///              (a toggle is on only while held).
	///   DoubleTap  the second press within BindingTuning::DoubleTapS fires (and is held while that press is).
	///   LongPress  fires once the input has been held BindingTuning::LongPressS (and is held from then until let go).</summary>
	enum class Trigger : int
	{
		Auto,
		Press,
		Hold,
		DoubleTap,
		LongPress,
		Count,
	};

	/// <summary>As a profile writes the prefix ("DoubleTap").</summary>
	inline const char* TriggerToken(Trigger t)
	{
		switch (t)
		{
			case Trigger::Press: return "Press";
			case Trigger::Hold: return "Hold";
			case Trigger::DoubleTap: return "DoubleTap";
			case Trigger::LongPress: return "LongPress";
			default: return "";
		}
	}

	/// <summary>As the page's mode picker and the Help screen show it ("Double-tap").</summary>
	inline const char* TriggerLabel(Trigger t)
	{
		switch (t)
		{
			case Trigger::Press: return "Press";
			case Trigger::Hold: return "Hold";
			case Trigger::DoubleTap: return "Double-tap";
			case Trigger::LongPress: return "Long press";
			default: return "";
		}
	}

	/// <summary>The mode a binding works in for an action that is held (<paramref name="heldKind"/>) or pressed.</summary>
	inline Trigger Resolve(Trigger t, bool heldKind)
	{
		return t == Trigger::Auto || t == Trigger::Count ? (heldKind ? Trigger::Hold : Trigger::Press) : t;
	}

	/// <summary>Which bindings on one input would both answer the same touch: a press or a hold (0), a double-tap (1), a
	/// long press (2). Bindings of different classes on one input are told apart by how it is touched.</summary>
	inline int TriggerClass(Trigger t, bool heldKind)
	{
		Trigger r = Resolve(t, heldKind);
		return r == Trigger::DoubleTap ? 1 : r == Trigger::LongPress ? 2 : 0;
	}

	/// <summary>The tunables of the trigger modes and of listening for a new binding, in one place.</summary>
	struct BindingTuning
	{
		/// The second press of a double-tap comes within this long of the first (seconds).
		double DoubleTapS = 0.30;
		/// A long press fires once held this long (seconds).
		double LongPressS = 0.50;
		/// A stick direction with no threshold of its own counts once pushed this far (0..1).
		float StickThreshold = 0.50f;
		/// Listening: a stick pushed this far that way is taken (0..1), so a resting stick's drift is not.
		float CaptureStick = 0.60f;
		/// Listening: the slot gives up after this long with nothing pressed (a pad has no Esc to cancel with).
		double CaptureTimeoutS = 6.0;
	};

	/// <summary>One slot of an action: an input (and the modifiers held with it), with a trigger mode. Empty: unbound.</summary>
	struct InputBinding
	{
		BindSource source = BindSource::None;
		int code = 0;
		int mods = KeyMods::None;
		Trigger trigger = Trigger::Auto;
		/// A pad trigger's or stick's threshold in percent (1..100); 0: the default (a trigger's own click, a stick's
		/// BindingTuning::StickThreshold).
		int thresholdPct = 0;

		InputBinding() = default;
		/// A key (an empty slot for Key::None), so a table can write <c>{ Key::W }</c> or <c>InputBinding(Key::T, Shift)</c>.
		InputBinding(Key k, int m = KeyMods::None)
			: source(k == Key::None ? BindSource::None : BindSource::Key), code(static_cast<int>(k)), mods(k == Key::None ? 0 : m) {}

		static InputBinding OfMouse(MouseButton b, int m = KeyMods::None) { return Make(BindSource::Mouse, static_cast<int>(b), m); }
		static InputBinding OfWheel(bool up, int m = KeyMods::None) { return Make(BindSource::Wheel, up ? 0 : 1, m); }
		static InputBinding OfPad(PadButton b)
		{
			if (b == PadButton::LeftTrigger || b == PadButton::RightTrigger) return OfTrigger(b == PadButton::RightTrigger);
			return Make(BindSource::PadButton, static_cast<int>(b), 0);
		}
		static InputBinding OfTrigger(bool right, int pct = 0) { InputBinding r = Make(BindSource::PadTrigger, right ? 1 : 0, 0); r.thresholdPct = pct; return r; }
		static InputBinding OfStick(StickDir d, int pct = 0) { InputBinding r = Make(BindSource::PadStick, static_cast<int>(d), 0); r.thresholdPct = pct; return r; }

		InputBinding With(Trigger t) const { InputBinding r = *this; r.trigger = t; return r; }

		bool Empty() const { return source == BindSource::None; }
		bool IsPad() const { return source == BindSource::PadButton || source == BindSource::PadTrigger || source == BindSource::PadStick; }
		bool IsMouse() const { return source == BindSource::Mouse || source == BindSource::Wheel; }
		bool UsesMods() const { return source == BindSource::Key || source == BindSource::Mouse || source == BindSource::Wheel; }
		/// An analog reading against a threshold (its edges come from the last frame's reading, not the device's).
		bool Analog() const { return source == BindSource::PadStick || (source == BindSource::PadTrigger && thresholdPct > 0); }
		Key AsKey() const { return source == BindSource::Key ? static_cast<Key>(code) : Key::None; }

		/// The same physical input (and modifiers), whatever the trigger mode and threshold.
		bool SameInput(const InputBinding& o) const
		{
			if (Empty() || o.Empty()) return Empty() && o.Empty();
			return source == o.source && code == o.code && (!UsesMods() || mods == o.mods);
		}
		bool operator==(const InputBinding& o) const { return SameInput(o) && (Empty() || (trigger == o.trigger && thresholdPct == o.thresholdPct)); }
		bool operator!=(const InputBinding& o) const { return !(*this == o); }

		/// <summary>As a profile keeps it: "Shift+T", "Mouse4", "Pad:LeftStickUp", "DoubleTap:Z", "None".</summary>
		std::string Format() const
		{
			if (Empty()) return "None";
			std::string s = trigger != Trigger::Auto ? std::string(TriggerToken(trigger)) + ":" : std::string();
			if (IsPad())
			{
				s += "Pad:";
				s += source == BindSource::PadButton ? PadToken(static_cast<PadButton>(code))
					: source == BindSource::PadTrigger ? (code == 1 ? "RT" : "LT")
					: StickToken(static_cast<StickDir>(code));
				if (thresholdPct > 0) s += ">" + std::to_string(thresholdPct);
				return s;
			}
			s += ModsPrefix(mods);
			s += InputToken();
			return s;
		}

		/// <summary>As the page and Help print it: "Shift+T", "Mouse 4", "Wheel up", "Pad A", "Pad LS up >70%", with the mode
		/// when it is not the action's own ("Z (double-tap)").</summary>
		std::string Display() const
		{
			if (Empty()) return "";
			std::string s;
			if (IsPad()) s = std::string("Pad ") + PadGlyph();
			else if (source == BindSource::Mouse)
			{
				static const char* const names[] = { "Mouse left", "Mouse right", "Mouse middle", "Mouse 4", "Mouse 5" };
				s = ModsPrefix(mods) + (code >= 0 && code < 5 ? names[code] : "Mouse");
			}
			else if (source == BindSource::Wheel) s = ModsPrefix(mods) + (code == 0 ? "Wheel up" : "Wheel down");
			else s = ModsPrefix(mods) + KeyDisplay(static_cast<Key>(code));
			if (thresholdPct > 0) s += " >" + std::to_string(thresholdPct) + "%";
			if (trigger == Trigger::DoubleTap) s += " (double-tap)";
			else if (trigger == Trigger::LongPress) s += " (long press)";
			else if (trigger == Trigger::Hold) s += " (hold)";
			else if (trigger == Trigger::Press) s += " (press)";
			return s;
		}

		/// <summary>A pad binding's glyph ("A", "LT", "LS click", "RS up"); empty for anything else.</summary>
		std::string PadGlyph() const
		{
			if (source == BindSource::PadButton)
			{
				auto b = static_cast<PadButton>(code);
				std::string g = ToString(b);
				return b == PadButton::LeftStick || b == PadButton::RightStick ? g + " click" : g;
			}
			if (source == BindSource::PadTrigger) return code == 1 ? "RT" : "LT";
			if (source == BindSource::PadStick) return StickGlyph(static_cast<StickDir>(code));
			return "";
		}

		/// <summary>Takes back what <see cref="Format"/> wrote (and "None" or nothing: an empty slot); false for anything
		/// else, Esc among it (it is fixed).</summary>
		static bool TryParse(const std::string& text, InputBinding& out)
		{
			std::string t = Trim(text);
			if (t.empty() || t == "None")
			{
				out = InputBinding();
				return true;
			}
			Trigger trig = Trigger::Auto;
			for (Trigger c : { Trigger::Press, Trigger::Hold, Trigger::DoubleTap, Trigger::LongPress })
			{
				std::string p = std::string(TriggerToken(c)) + ":";
				if (t.compare(0, p.size(), p) == 0)
				{
					trig = c;
					t = t.substr(p.size());
					break;
				}
			}
			InputBinding b;
			if (t.compare(0, 4, "Pad:") == 0)
			{
				std::string name = t.substr(4);
				int pct = 0;
				size_t gt = name.find('>');
				if (gt != std::string::npos)
				{
					std::string n = name.substr(gt + 1);
					if (n.empty() || n.size() > 3) return false;
					for (char ch : n) if (ch < '0' || ch > '9') return false;
					pct = std::atoi(n.c_str());
					if (pct < 1 || pct > 100) return false;
					name = name.substr(0, gt);
				}
				if (name == "LT" || name == "RT") b = OfTrigger(name == "RT", pct);
				else
				{
					bool found = false;
					for (int i = 0; i < static_cast<int>(StickDir::Count) && !found; i++)
						if (name == StickToken(static_cast<StickDir>(i))) { b = OfStick(static_cast<StickDir>(i), pct); found = true; }
					for (int i = static_cast<int>(PadButton::None) + 1; i < static_cast<int>(PadButton::Count) && !found; i++)
					{
						auto pb = static_cast<PadButton>(i);
						if (pb == PadButton::LeftTrigger || pb == PadButton::RightTrigger) continue;
						if (name == PadToken(pb))
						{
							if (pct != 0) return false; // a button has no threshold
							b = OfPad(pb);
							found = true;
						}
					}
					if (!found) return false;
				}
			}
			else
			{
				int m = KeyMods::None;
				for (;;)
				{
					if (t.compare(0, 5, "Ctrl+") == 0) { m |= KeyMods::Ctrl; t = t.substr(5); }
					else if (t.compare(0, 6, "Shift+") == 0) { m |= KeyMods::Shift; t = t.substr(6); }
					else if (t.compare(0, 4, "Alt+") == 0) { m |= KeyMods::Alt; t = t.substr(4); }
					else break;
				}
				if (t == "MouseLeft") b = OfMouse(MouseButton::Left, m);
				else if (t == "MouseRight") b = OfMouse(MouseButton::Right, m);
				else if (t == "MouseMiddle") b = OfMouse(MouseButton::Middle, m);
				else if (t == "Mouse4") b = OfMouse(MouseButton::Back, m);
				else if (t == "Mouse5") b = OfMouse(MouseButton::Forward, m);
				else if (t == "WheelUp") b = OfWheel(true, m);
				else if (t == "WheelDown") b = OfWheel(false, m);
				else
				{
					Key k = KeyNamed(t);
					if (k == Key::None || k == Key::Escape) return false;
					b = InputBinding(k, m);
				}
			}
			b.trigger = trig;
			out = b;
			return true;
		}

		static std::string Trim(const std::string& s)
		{
			size_t a = 0, e = s.size();
			while (a < e && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
			while (e > a && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
			return s.substr(a, e - a);
		}

		static std::string ModsPrefix(int m)
		{
			std::string s;
			if (m & KeyMods::Ctrl) s += "Ctrl+";
			if (m & KeyMods::Shift) s += "Shift+";
			if (m & KeyMods::Alt) s += "Alt+";
			return s;
		}

	private:
		static InputBinding Make(BindSource s, int c, int m)
		{
			InputBinding r;
			r.source = s;
			r.code = c;
			r.mods = m;
			return r;
		}

		std::string InputToken() const
		{
			if (source == BindSource::Mouse)
			{
				static const char* const names[] = { "MouseLeft", "MouseRight", "MouseMiddle", "Mouse4", "Mouse5" };
				return code >= 0 && code < 5 ? names[code] : "None";
			}
			if (source == BindSource::Wheel) return code == 0 ? "WheelUp" : "WheelDown";
			return ToString(static_cast<Key>(code));
		}
	};

	/// <summary>The binding with its mode written as Auto when that is the action's own, so a binding picked as "Hold" on a
	/// held action is the same as one never given a mode (and its text has no prefix).</summary>
	inline InputBinding Normalized(InputBinding b, bool heldKind)
	{
		if (b.Empty()) return InputBinding();
		if (b.trigger != Trigger::Auto && Resolve(b.trigger, heldKind) == Resolve(Trigger::Auto, heldKind)) b.trigger = Trigger::Auto;
		if (b.trigger == Trigger::Count) b.trigger = Trigger::Auto;
		return b;
	}

	/// <summary>Two bindings (of actions held or not) that one touch of one input would both answer.</summary>
	inline bool Answers(const InputBinding& a, bool aHeld, const InputBinding& b, bool bHeld)
	{
		return !a.Empty() && a.SameInput(b) && TriggerClass(a.trigger, aHeld) == TriggerClass(b.trigger, bHeld);
	}

	// ---------------------------------------------------------------- trigger timing

	/// <summary>What a binding's mode remembers between frames.</summary>
	struct TriggerState
	{
		bool latch = false;      // Press on a held action: latched on
		bool active = false;     // DoubleTap: the second press is held
		bool longFired = false;  // LongPress: fired for this hold
		double downAt = -1;      // LongPress: when the input went down (-1: up)
		double lastTapAt = -1e9; // DoubleTap: the first press waiting for a second
	};

	/// <summary>A binding's answer this frame: the action held, and pressed (an edge).</summary>
	struct TriggerOut
	{
		bool held = false;
		bool pressed = false;
	};

	/// <summary>One frame of a binding: the input's raw state (<paramref name="down"/> now, went down, came up this
	/// frame; both edges in one frame for a tap shorter than a frame) at <paramref name="now"/> seconds, through the mode
	/// <paramref name="t"/> of an action that is held (<paramref name="heldKind"/>) or pressed.</summary>
	inline TriggerOut StepTrigger(TriggerState& s, Trigger t, bool heldKind, bool down, bool pressed, bool released, double now, const BindingTuning& tune)
	{
		TriggerOut o;
		switch (Resolve(t, heldKind))
		{
			case Trigger::Press:
				if (heldKind)
				{
					if (pressed) s.latch = !s.latch;
					o.held = s.latch;
					o.pressed = pressed && s.latch;
				}
				else
				{
					o.held = down;
					o.pressed = pressed;
				}
				break;
			case Trigger::Hold:
				o.held = down;
				o.pressed = heldKind ? pressed : (pressed || released);
				break;
			case Trigger::DoubleTap:
				if (pressed)
				{
					if (now - s.lastTapAt <= tune.DoubleTapS)
					{
						s.active = true;
						o.pressed = true;
						s.lastTapAt = -1e9;
					}
					else s.lastTapAt = now;
				}
				o.held = s.active && (down || o.pressed);
				if (!down) s.active = false;
				break;
			case Trigger::LongPress:
				if (pressed || (down && s.downAt < 0))
				{
					s.downAt = now;
					s.longFired = false;
				}
				if (!down)
				{
					s.downAt = -1;
					s.longFired = false;
				}
				else if (now - s.downAt >= tune.LongPressS)
				{
					o.held = true;
					if (!s.longFired)
					{
						s.longFired = true;
						o.pressed = true;
					}
				}
				break;
			default:
				break;
		}
		return o;
	}

	// ---------------------------------------------------------------- an action's three slots

	constexpr int BindingSlots = 3;

	/// <summary>The bound slots first, in order, the empty ones after.</summary>
	inline void CompactSlots(InputBinding (&v)[BindingSlots])
	{
		int n = 0;
		for (int i = 0; i < BindingSlots; i++)
			if (!v[i].Empty()) v[n++] = v[i];
		for (; n < BindingSlots; n++) v[n] = InputBinding();
	}

	/// <summary>The slots as a profile keeps them: "Shift+K, NumpadMultiply", "None" when all are empty, with
	/// ", Pad:None" when the action's defaults have a pad binding and the slots none (see the file's head).</summary>
	inline std::string FormatSlots(const InputBinding (&value)[BindingSlots], const InputBinding (&defaults)[BindingSlots])
	{
		std::string s;
		bool pad = false, defaultPad = false;
		for (const InputBinding& b : value)
		{
			if (b.Empty()) continue;
			if (!s.empty()) s += ", ";
			s += b.Format();
			pad = pad || b.IsPad();
		}
		for (const InputBinding& b : defaults) defaultPad = defaultPad || b.IsPad();
		if (!pad && defaultPad) s += s.empty() ? "Pad:None" : ", Pad:None";
		return s.empty() ? std::string("None") : s;
	}

	/// <summary>Takes back what <see cref="FormatSlots"/> wrote, or a profile's text from before US-T437 (keys only, no
	/// "Pad:"), which keeps its keys in their order and gains the defaults' pad bindings in its free slots. False (and
	/// <paramref name="out"/> untouched) for text that is not a binding list, or holds more than three.</summary>
	inline bool ParseSlots(const std::string& text, const InputBinding (&defaults)[BindingSlots], bool heldKind, InputBinding (&out)[BindingSlots])
	{
		InputBinding read[BindingSlots];
		int n = 0;
		bool sawPad = false;
		size_t start = 0;
		for (;;)
		{
			size_t comma = text.find(',', start);
			std::string part = InputBinding::Trim(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
			if (part == "Pad:None") sawPad = true;
			else
			{
				InputBinding b;
				if (!InputBinding::TryParse(part, b)) return false;
				if (!b.Empty())
				{
					sawPad = sawPad || b.IsPad();
					if (n >= BindingSlots) return false;
					read[n++] = Normalized(b, heldKind);
				}
			}
			if (comma == std::string::npos) break;
			start = comma + 1;
		}
		if (!sawPad)
			for (const InputBinding& d : defaults)
			{
				if (d.Empty() || !d.IsPad() || n >= BindingSlots) continue;
				bool have = false;
				for (int i = 0; i < n; i++) have = have || read[i] == d;
				if (!have) read[n++] = Normalized(d, heldKind);
			}
		for (int i = 0; i < BindingSlots; i++) out[i] = read[i];
		CompactSlots(out);
		return true;
	}
}
