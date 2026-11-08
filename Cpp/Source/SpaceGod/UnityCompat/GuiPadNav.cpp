// US-T250: the GUI by gamepad (UnityCompat/GuiPadNav.h).

#include "UnityCompat/GuiPadNav.h"
#include "UnityCompat/UnityRuntime.h"
#include "Input/Gamepad.h"

namespace SpaceGod::UnityCompat
{
	using ::SpaceGod::Input::Gamepad;
	using ::SpaceGod::Input::GamepadDevice;
	using ::SpaceGod::Input::PadAction;
	using ::SpaceGod::Input::PadButton;

	namespace
	{
		struct Stop
		{
			int id = 0;
			Rect rect;
			GuiPadNav::Kind kind = GuiPadNav::Kind::Button;
		};

		/// This frame's stops (Repaint fills it) and the last frame's (the focus moves over them).
		std::vector<Stop> current, last;
		/// BeginFrame ran with the pad the GUI's: Register collects, DrawFocus draws.
		bool armed = false;
		int focusId = 0;
		Rect focusRect;
		/// The control this frame's pad events are for.
		int pressId = 0, nudgeId = 0;
		/// A direction held (0 left, 1 up, 2 right, 3 down; -1 none) and when it next repeats.
		int heldDir = -1;
		float repeatAt = 0;
		constexpr float RepeatDelay = 0.4f, RepeatEvery = 0.12f;
		/// Scroll view lines (20 px each) a second at full right-stick deflection, and a d-pad step's past the last stop.
		constexpr float StickScrollLines = 45.0f, StepScrollLines = 3.0f;
		constexpr size_t MaxStops = 1024;

		Vector2 Centre(const Rect& r) { return Vector2(r.x + r.width * 0.5f, r.y + r.height * 0.5f); }

		/// GUI space: y grows downwards.
		Vector2 Direction(int dir)
		{
			switch (dir)
			{
				case 0: return Vector2(-1, 0);
				case 1: return Vector2(0, -1);
				case 2: return Vector2(1, 0);
				default: return Vector2(0, 1);
			}
		}

		const Stop* Find(int id)
		{
			for (const Stop& s : last)
				if (s.id == id) return &s;
			return nullptr;
		}

		const Stop* Nearest(Vector2 p)
		{
			const Stop* best = nullptr;
			float bestD = 0;
			for (const Stop& s : last)
			{
				float d = (Centre(s.rect) - p).sqrMagnitude();
				if (best == nullptr || d < bestD) { best = &s; bestD = d; }
			}
			return best;
		}

		/// The top-left stop: the first a screen reads.
		const Stop* First()
		{
			const Stop* best = nullptr;
			for (const Stop& s : last)
			{
				if (best == nullptr) { best = &s; continue; }
				bool higher = s.rect.y < best->rect.y - 4;
				bool level = Mathf::Abs(s.rect.y - best->rect.y) <= 4;
				if (higher || (level && s.rect.x < best->rect.x)) best = &s;
			}
			return best;
		}

		/// The nearest stop from `from` toward `dir`: ahead of it, scored by the distance along plus twice the distance across
		/// (a stop in line beats a nearer one off to the side), a stop overlapping it across the move preferred.
		const Stop* Toward(const Stop& from, int dir)
		{
			Vector2 c = Centre(from.rect), d = Direction(dir);
			bool horizontal = dir == 0 || dir == 2;
			const Stop* best = nullptr;
			float bestScore = 0;
			for (const Stop& s : last)
			{
				if (s.id == from.id) continue;
				Vector2 v = Centre(s.rect) - c;
				float along = v.x * d.x + v.y * d.y;
				if (along <= 1.0f) continue;
				float across = Mathf::Abs(horizontal ? v.y : v.x);
				bool overlaps = horizontal
					? s.rect.y < from.rect.yMax() && s.rect.yMax() > from.rect.y
					: s.rect.x < from.rect.xMax() && s.rect.xMax() > from.rect.x;
				float score = along + across * 2.0f + (overlaps ? 0.0f : 10000.0f);
				if (best == nullptr || score < bestScore) { best = &s; bestScore = score; }
			}
			return best;
		}

		/// This frame's move: a d-pad or left-stick push (the stick's stronger axis past half way), repeating while held.
		int PadDirection(const GamepadDevice& pad)
		{
			int dir = pad[PadButton::DPadLeft].isPressed ? 0 : pad[PadButton::DPadUp].isPressed ? 1
				: pad[PadButton::DPadRight].isPressed ? 2 : pad[PadButton::DPadDown].isPressed ? 3 : -1;
			if (dir < 0)
			{
				Vector2 s = pad.leftStick;
				if (s.magnitude() > 0.5f) dir = Mathf::Abs(s.x) > Mathf::Abs(s.y) ? (s.x < 0 ? 0 : 2) : (s.y > 0 ? 1 : 3);
			}
			float now = Time::unscaledTime;
			if (dir < 0)
			{
				heldDir = -1;
				return -1;
			}
			if (dir != heldDir)
			{
				heldDir = dir;
				repeatAt = now + RepeatDelay;
				return dir;
			}
			if (now >= repeatAt)
			{
				repeatAt = now + RepeatEvery;
				return dir;
			}
			return -1;
		}

		Event Make(EventType type, Vector2 mouse)
		{
			Event e;
			e.type = e.rawType = type;
			e.mousePosition = mouse;
			return e;
		}
	}

	bool GuiPadNav::Engaged() { return Gamepad::MenuMode && GamepadDevice::current().present; }

	void GuiPadNav::Register(int id, const Rect& screen, Kind kind)
	{
		if (!armed || current.size() >= MaxStops || screen.width <= 0 || screen.height <= 0) return;
		// A screen-sized catcher (a click anywhere closes) is no stop.
		if (screen.width * screen.height > 0.5f * static_cast<float>(Screen::width) * static_cast<float>(Screen::height)) return;
		for (const Stop& s : current)
			if (s.id == id) return; // one stop a control, whatever its passes
		Stop s;
		s.id = id;
		s.rect = screen;
		s.kind = kind;
		current.push_back(s);
	}

	void GuiPadNav::DrawFocus(int id, const Rect& local)
	{
		if (!armed || !Gamepad::LastUsed || id == 0 || id != focusId) return;
		const float t = 2, grow = 3;
		Rect r(local.x - grow, local.y - grow, local.width + 2 * grow, local.height + 2 * grow);
		Color keep = GUI::color;
		GUI::color = Color(1.0f, 0.82f, 0.35f, 0.95f);
		const auto& w = Texture2D::whiteTexture();
		GUI::DrawTexture(Rect(r.x, r.y, r.width, t), w);
		GUI::DrawTexture(Rect(r.x, r.yMax() - t, r.width, t), w);
		GUI::DrawTexture(Rect(r.x, r.y + t, t, r.height - 2 * t), w);
		GUI::DrawTexture(Rect(r.xMax() - t, r.y + t, t, r.height - 2 * t), w);
		GUI::color = keep;
	}

	bool GuiPadNav::Pressed(const Event& e, int id)
	{
		return e.type == EventType::KeyDown && e.keyCode == Accept && id != 0 && id == pressId;
	}

	int GuiPadNav::Nudge(const Event& e, int id)
	{
		if (e.type != EventType::KeyDown || id == 0 || id != nudgeId) return 0;
		return e.keyCode == Decrease ? -1 : e.keyCode == Increase ? 1 : 0;
	}

	void GuiPadNav::BeginFrame(std::vector<Event>& events, Vector2 mouse, bool& escape)
	{
		escape = false;
		last.swap(current);
		current.clear();
		pressId = nudgeId = 0;
		armed = Engaged();
		if (!armed)
		{
			focusId = 0;
			heldDir = -1;
			last.clear();
			return;
		}
		const GamepadDevice& pad = GamepadDevice::current();

		// The focus kept on its control, else on the stop nearest where it was, else (the pad in hand) on the first.
		const Stop* f = focusId != 0 ? Find(focusId) : nullptr;
		if (f == nullptr && focusId != 0) f = Nearest(Centre(focusRect));
		if (f == nullptr && Gamepad::LastUsed) f = First();

		int dir = PadDirection(pad);
		bool accept = pad[PadButton::A].wasPressedThisFrame;
		if (f == nullptr && (dir >= 0 || accept))
		{
			// The first touch only shows where the focus is.
			f = First();
			dir = -1;
			accept = false;
		}
		if (f != nullptr)
		{
			if ((dir == 0 || dir == 2) && f->kind == Kind::Slider)
			{
				nudgeId = f->id;
				Event e = Make(EventType::KeyDown, mouse);
				e.keyCode = dir == 0 ? Decrease : Increase;
				events.push_back(e);
			}
			else if (dir >= 0)
			{
				if (const Stop* next = Toward(*f, dir)) f = next;
				else if (dir == 1 || dir == 3)
				{
					// Nothing further that way: scroll what the focus is in (a long list carries on into view).
					Event e = Make(EventType::ScrollWheel, Centre(f->rect));
					e.delta = Vector2(0, dir == 3 ? StepScrollLines : -StepScrollLines);
					events.push_back(e);
				}
			}
			focusId = f->id;
			focusRect = f->rect;
			if (accept)
			{
				pressId = focusId;
				Event e = Make(EventType::KeyDown, mouse);
				e.keyCode = Accept;
				events.push_back(e);
			}
		}
		else
		{
			focusId = 0;
		}

		// The right stick scrolls what is under the focus (or the pointer).
		float ry = Gamepad::Stick(pad.rightStick).y;
		if (ry != 0)
		{
			Event e = Make(EventType::ScrollWheel, f != nullptr ? Centre(focusRect) : mouse);
			e.delta = Vector2(0, -ry * StickScrollLines * Mathf::Max(0.0f, Time::unscaledDeltaTime));
			events.push_back(e);
		}

		// B, or the pause button: Esc.
		escape = pad[PadButton::B].wasPressedThisFrame || Gamepad::Action(PadAction::Pause).wasPressedThisFrame;
		if (escape)
		{
			Event e = Make(EventType::KeyDown, mouse);
			e.keyCode = KeyCode::Escape;
			events.push_back(e);
		}
	}
}
