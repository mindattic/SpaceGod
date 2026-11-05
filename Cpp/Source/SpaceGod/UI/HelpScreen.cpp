#include "UI/HelpScreen.h"

#include "ControlsHelp.h"
#include "GameManual.h"
#include "UI/Hud.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	bool HelpScreen::Draw(const cs::String& closeHint)
	{
		EnsureStyles();
		const auto& chapters = GameManual::Chapters();
		chapter = Mathf::Clamp(chapter, 0, chapters.Count() - 1);
		HandleKeys(chapters.Count());

		auto screen = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		GUI::Box(screen, GUIContent::none(), panel);
		GUI::Label(Rect(Margin, 22, screen.width - 2 * Margin, 30), "MANUAL", title);
		GUI::Label(Rect(Margin, 54, screen.width - 2 * Margin, 18),
			cs::Cat("Everything in the game, by chapter  ·  Up / Down for the next chapter  ·  ", closeHint), small);

		float top = 86, bottom = screen.height - 64;
		float contentsW = Mathf::Clamp(screen.width * 0.24f, 180.0f, 300.0f);
		DrawContents(Rect(Margin, top, contentsW, bottom - top), chapters);

		float x = Margin + contentsW + Gutter;
		auto view = Rect(x, top, screen.width - Margin - x, bottom - top);
		DrawChapter(chapters[chapter], view);

		if (chapter > 0 && GUI::Button(Rect(x, screen.height - 50, 120, 32), "Previous", button)) Select(chapter - 1);
		if (chapter < chapters.Count() - 1 && GUI::Button(Rect(x + 132, screen.height - 50, 120, 32), "Next", button)) Select(chapter + 1);
		return GUI::Button(Rect(screen.width - Margin - 120, screen.height - 50, 120, 32), "Back", button);
	}

	void HelpScreen::HandleKeys(int count)
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown) return;
		if (e.keyCode == KeyCode::DownArrow && chapter < count - 1) { Select(chapter + 1); e.Use(); }
		else if (e.keyCode == KeyCode::UpArrow && chapter > 0) { Select(chapter - 1); e.Use(); }
	}

	void HelpScreen::Select(int index)
	{
		if (index == chapter) return;
		chapter = index;
		scroll = Vector2::zero();
	}

	void HelpScreen::DrawContents(Rect area, const cs::List<cs::Ref<ManualChapter>>& chapters)
	{
		GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), "CONTENTS", heading);
		GUI::DrawTexture(Rect(area.x, area.y + HeadingH - 5, area.width, 1), buttonActiveTexture);
		auto list = Rect(area.x, area.y + HeadingH, area.width, area.height - HeadingH);
		float contentH = chapters.Count() * ContentsRowH;
		contentsScroll = GUI::BeginScrollView(list, contentsScroll, Rect(0, 0, list.width - 16, contentH));
		for (int i = 0; i < chapters.Count(); i++)
		{
			auto row = Rect(0, i * ContentsRowH, list.width - 16, ContentsRowH - 4);
			if (GUI::Button(row, cs::Cat(i + 1, ".  ", chapters[i]->Title), i == chapter ? entrySelected : entry)) Select(i);
		}
		GUI::EndScrollView();
	}

	void HelpScreen::DrawChapter(const cs::Ref<ManualChapter>& c, Rect view)
	{
		float w = view.width - 16;
		float contentH = ChapterHeight(c, w);
		scroll = GUI::BeginScrollView(view, scroll, Rect(0, 0, w, contentH));
		float y = 0;
		GUI::Label(Rect(0, y, w, 28), cs::Strings::ToUpperInvariant(c->Title), chapterTitle);
		y += 28;
		float sh = small->CalcHeight(GUIContent(c->Summary), w);
		GUI::Label(Rect(0, y, w, sh), c->Summary, small);
		y += sh + TopicGap;

		for (const auto& t : c->Topics)
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), t.Heading, heading);
			y += HeadingH - 2;
			float th = text->CalcHeight(GUIContent(t.Text), w);
			GUI::Label(Rect(0, y, w, th), t.Text, text);
			y += th + TopicGap;
		}

		for (const auto& section : GameManual::ControlsOf(c))
			y = DrawSection(section, y, w);
		GUI::EndScrollView();
	}

	float HelpScreen::ChapterHeight(const cs::Ref<ManualChapter>& c, float w)
	{
		float h = 28 + small->CalcHeight(GUIContent(c->Summary), w) + TopicGap;
		for (const auto& t : c->Topics)
			h += HeadingH - 2 + text->CalcHeight(GUIContent(t.Text), w) + TopicGap;
		for (const auto& section : GameManual::ControlsOf(c))
		{
			h += HeadingH;
			for (const auto& line : section->Lines) h += LineHeight(line, w) + LineGap;
			h += TopicGap;
		}
		return h;
	}

	float HelpScreen::LineHeight(const HelpLine& line, float w)
	{
		return Mathf::Max(18.0f, text->CalcHeight(GUIContent(line.Text), w - KeyW));
	}

	float HelpScreen::DrawSection(const cs::Ref<HelpSection>& section, float y, float w)
	{
		GUI::Label(Rect(0, y, w, HeadingH - 4), cs::Cat("KEYS: ", section->Title), heading);
		GUI::DrawTexture(Rect(0, y + HeadingH - 5, w, 1), buttonActiveTexture);
		y += HeadingH;
		for (const auto& line : section->Lines)
		{
			float h = LineHeight(line, w);
			if (line.Keys.size() > 0) GUI::Label(Rect(0, y, KeyW - 8, h), line.Keys, keys);
			GUI::Label(Rect(KeyW, y, w - KeyW, h), line.Text, line.Keys.size() > 0 ? text : small);
			y += h + LineGap;
		}
		return y + TopicGap;
	}

	void HelpScreen::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The HUD's look, near opaque: the screen is the whole view.
		panelTexture = Hud::Solid(Color(0.02f, 0.035f, 0.06f, 0.96f), Color::clear());
		buttonTexture = Hud::Solid(Color(0.12f, 0.18f, 0.24f, 0.95f), Color(0.3f, 0.5f, 0.6f, 0.6f));
		buttonActiveTexture = Hud::Solid(Color(0.1f, 0.3f, 0.42f, 0.95f), Color(0.45f, 0.85f, 1.0f, 0.8f));
		panel = cs::New<GUIStyle>();
		panel->normal.background = panelTexture;
		title = cs::New<GUIStyle>();
		title->fontSize = 24;
		title->fontStyle = FontStyle::Bold;
		title->normal.textColor = Color::white();
		chapterTitle = cs::New<GUIStyle>();
		chapterTitle->fontSize = 19;
		chapterTitle->fontStyle = FontStyle::Bold;
		chapterTitle->normal.textColor = Color::white();
		heading = cs::New<GUIStyle>();
		heading->fontSize = 14;
		heading->fontStyle = FontStyle::Bold;
		heading->normal.textColor = Hud::Accent;
		keys = cs::New<GUIStyle>();
		keys->fontSize = 13;
		keys->fontStyle = FontStyle::Bold;
		keys->normal.textColor = Color::white();
		keys->wordWrap = true;
		text = cs::New<GUIStyle>();
		text->fontSize = 13;
		text->normal.textColor = Color(0.86f, 0.9f, 0.94f);
		text->wordWrap = true;
		small = cs::New<GUIStyle>(*text);
		small->normal.textColor = Hud::Dim;
		small->fontStyle = FontStyle::Italic;
		button = cs::New<GUIStyle>();
		button->fontSize = 14;
		button->alignment = TextAnchor::MiddleCenter;
		button->border = RectOffset(2, 2, 2, 2);
		button->normal.background = buttonTexture;
		button->normal.textColor = Color::white();
		button->hover.background = buttonActiveTexture;
		button->hover.textColor = Color::white();
		button->active.background = buttonActiveTexture;
		button->active.textColor = Hud::Accent;
		entry = cs::New<GUIStyle>(*button);
		entry->alignment = TextAnchor::MiddleLeft;
		entry->fontSize = 13;
		entry->padding = RectOffset(10, 6, 0, 0);
		entrySelected = cs::New<GUIStyle>(*entry);
		entrySelected->normal.background = buttonActiveTexture;
		entrySelected->normal.textColor = Hud::Accent;
		entrySelected->hover.background = buttonActiveTexture;
		entrySelected->hover.textColor = Hud::Accent;
	}

	void HelpScreen::DestroyOwnedTextures()
	{
		panelTexture = nullptr;
		buttonTexture = nullptr;
		buttonActiveTexture = nullptr;
		stylesReady = false;
	}
}
