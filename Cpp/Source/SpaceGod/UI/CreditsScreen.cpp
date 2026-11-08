#include "UI/CreditsScreen.h"
#include "UI/UiMotion.h" // US-T255

#include "Credits.h"
#include "UI/UiStyle.h"
#include "UnityCompat/UnityRuntime.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void CreditsScreen::Restart()
	{
		roll = 0;
		lastTime = -1;
		waitUntil = 0;
		paused = false;
	}

	bool CreditsScreen::Draw(const cs::String& closeHint)
	{
		EnsureStyles();
		// US-T255: the whole screen fades in (no slide: it covers the screen).
		UiMotion::Scope motion("credits", 0);

		auto screen = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		GUI::Box(screen, GUIContent::none(), panel);
		GUI::Label(Rect(Margin, 22, screen.width - 2 * Margin, 30), "CREDITS", title);
		GUI::Label(Rect(Margin, 54, screen.width - 2 * Margin, 18),
			cs::Cat("Wheel, drag or Up / Down to read at your own pace  ·  Space to ", paused ? "roll on" : "pause", "  ·  ", closeHint), small);

		float top = 86, bottom = screen.height - 64;
		auto view = Rect(Margin, top, screen.width - 2 * Margin, bottom - top);
		float w = Mathf::Min(ColumnW, view.width - 16);
		Measure(w);
		// The column starts below the view and rolls out past its top: a view's height of space before it and after it.
		float total = view.height + contentH + view.height;
		float maxRoll = Mathf::Max(0.0f, total - view.height);

		HandleKeys(view.height, maxRoll);

		// The roll moves once a frame (on the repaint), on real time: the pause menu stops scaled time.
		Event& e = Event::current();
		if (e.type == EventType::Repaint)
		{
			float now = Time::unscaledTime;
			float dt = lastTime < 0 ? 0 : Mathf::Clamp(now - lastTime, 0.0f, 0.25f);
			lastTime = now;
			if (!paused && now >= waitUntil)
			{
				roll += RollSpeed * dt;
				if (roll >= maxRoll) roll = 0; // round again, from below the screen
			}
		}
		// A drag anywhere on the column scrolls it by hand.
		if (e.type == EventType::MouseDrag && view.Contains(e.mousePosition))
		{
			roll = Mathf::Clamp(roll - e.delta.y, 0.0f, maxRoll);
			waitUntil = Time::unscaledTime + ResumeAfter;
			e.Use();
		}

		float asked = roll;
		Vector2 scrolled = GUI::BeginScrollView(view, Vector2(0, roll), Rect(0, 0, view.width - 16, total));
		GUI::BeginGroup(Rect((view.width - 16 - w) * 0.5f, view.height, w, contentH));
		DrawColumn(w, true);
		GUI::EndGroup();
		GUI::EndScrollView();
		// The wheel or the scroll bar moved it: the player is reading; the roll waits, then carries on from there.
		if (Mathf::Abs(scrolled.y - asked) > 0.5f)
		{
			roll = Mathf::Clamp(scrolled.y, 0.0f, maxRoll);
			waitUntil = Time::unscaledTime + ResumeAfter;
		}

		if (GUI::Button(Rect(Margin, screen.height - 50, 150, 32), paused ? "Roll on" : "Pause", button)) paused = !paused;
		if (GUI::Button(Rect(Margin + 162, screen.height - 50, 150, 32), "From the top", button))
		{
			roll = 0;
			waitUntil = 0;
		}
		return GUI::Button(Rect(screen.width - Margin - 120, screen.height - 50, 120, 32), "Back", button);
	}

	void CreditsScreen::HandleKeys(float viewH, float maxRoll)
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown) return;
		float to = roll;
		switch (e.keyCode)
		{
			case KeyCode::DownArrow: to += 60; break;
			case KeyCode::UpArrow: to -= 60; break;
			case KeyCode::PageDown: to += viewH * 0.85f; break;
			case KeyCode::PageUp: to -= viewH * 0.85f; break;
			case KeyCode::Home: to = 0; break;
			case KeyCode::End: to = maxRoll; break;
			case KeyCode::Space:
				paused = !paused;
				waitUntil = 0;
				e.Use();
				return;
			default: return;
		}
		roll = Mathf::Clamp(to, 0.0f, maxRoll);
		waitUntil = Time::unscaledTime + ResumeAfter;
		e.Use();
	}

	float CreditsScreen::Measure(float w)
	{
		if (w != measuredW)
		{
			measuredW = w;
			contentH = DrawColumn(w, false);
		}
		return contentH;
	}

	float CreditsScreen::DrawColumn(float w, bool draw)
	{
		float y = 0;
		auto line = [&](const cs::String& s, const cs::Ref<GUIStyle>& style, float gap)
		{
			if (s.empty()) return;
			float h = style->CalcHeight(GUIContent(s), w);
			if (draw) GUI::Label(Rect(0, y, w, h), s, style);
			y += h + gap;
		};

		line(Credits::Title, gameTitle, 4);
		line(Credits::Tagline, tagline, SectionGap);
		for (const auto& s : Credits::Sections())
		{
			line(s->Title, section, 12);
			for (const auto& entry : s->Entries)
			{
				line(entry.Name, name, 2);
				line(entry.Detail, detail, entry.Licence.empty() ? 0 : 2);
				line(entry.Licence, licence, 0);
				y += EntryGap;
			}
			y += SectionGap - EntryGap;
		}
		line(Credits::Closing, tagline, 0);
		return y;
	}

	void CreditsScreen::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The shared look (US-T199, UiStyle.h) at full-screen depth, as Help's.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Screen);
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();
		panel = S::PanelStyle(panelTexture);
		title = S::TitleStyle(T::Screen);
		small = S::CaptionStyle(T::Body);
		small->fontStyle = FontStyle::Italic;
		button = S::ButtonStyle(T::Heading, buttonTexture, buttonActiveTexture);

		auto centred = [](cs::Ref<GUIStyle> s)
		{
			s->alignment = TextAnchor::UpperCenter;
			s->wordWrap = true;
			return s;
		};
		gameTitle = centred(S::TextStyle(T::Display, S::Heading, FontStyle::Bold));
		tagline = centred(S::CaptionStyle(T::Heading));
		tagline->fontStyle = FontStyle::Italic;
		section = centred(S::TextStyle(T::Chapter, S::Accent, FontStyle::Bold));
		name = centred(S::TextStyle(T::Heading, S::Heading, FontStyle::Bold));
		detail = centred(S::TextStyle(T::Body));
		licence = centred(S::CaptionStyle(T::Body));
		licence->fontStyle = FontStyle::Italic;
	}

	void CreditsScreen::DestroyOwnedTextures()
	{
		panelTexture = nullptr;
		buttonTexture = nullptr;
		buttonActiveTexture = nullptr;
		stylesReady = false;
		measuredW = -1; // the styles are made again, so the column is measured again
	}
}
