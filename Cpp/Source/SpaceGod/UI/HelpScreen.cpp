#include "UI/HelpScreen.h"
#include "UI/UiMotion.h" // US-T255

#include "ControlsHelp.h"
#include "GameManual.h"
#include "UI/Hud.h"
#include "UI/ShipCatalogScreen.h"
#include "UI/UiStyle.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	HelpScreen::HelpScreen() = default;

	HelpScreen::~HelpScreen() = default;

	bool HelpScreen::Draw(const cs::String& closeHint)
	{
		EnsureStyles();
		// US-T255: the whole screen fades in (no slide: it covers the screen); the catalog is a page of its own.
		UiMotion::Scope motion(showCatalog ? "help.catalog" : "help", 0);

		// The ship catalog (US-T193): every hull's handling, slots and compartments, in place of the manual.
		if (showCatalog)
		{
			if (!catalog) catalog = cs::New<ShipCatalogScreen>();
			auto whole = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
			GUI::Box(whole, GUIContent::none(), panel);
			GUI::Label(Rect(Margin, 22, whole.width - 2 * Margin, 30), "SHIP CATALOG", title);
			GUI::Label(Rect(Margin, 54, whole.width - 2 * Margin, 18),
				cs::Cat("Every hull in the game: handling, equipment slots and compartments  ·  Up / Down for the next hull  ·  ", closeHint), small);
			catalog->Draw(Rect(Margin, 86, whole.width - 2 * Margin, whole.height - 64 - 86));
			if (GUI::Button(Rect(Margin, whole.height - 50, 150, 32), "Manual", button)) showCatalog = false;
			return GUI::Button(Rect(whole.width - Margin - 120, whole.height - 50, 120, 32), "Back", button);
		}

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
		if (GUI::Button(Rect(screen.width - Margin - 120 - 12 - 150, screen.height - 50, 150, 32), "Ship catalog", button)) showCatalog = true;
		// US-T254: the guided tutorial, replayed from here (the owner says what starting one means: a new game from the menu,
		// the tutorial over the game in play).
		if (OnTutorial && GUI::Button(Rect(screen.width - Margin - 120 - 12 - 150 - 12 - 120, screen.height - 50, 120, 32), "Tutorial", button)) OnTutorial();
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
		// The shared look (US-T199, UiStyle.h) at full-screen depth: the screen is the whole view, framed at its corners.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Screen);
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();
		panel = S::PanelStyle(panelTexture);
		title = S::TitleStyle(T::Screen);
		chapterTitle = S::TitleStyle(T::Chapter);
		heading = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
		keys = S::TextStyle(T::Body, S::Heading, FontStyle::Bold);
		keys->wordWrap = true;
		text = S::TextStyle(T::Body);
		text->wordWrap = true;
		small = S::CaptionStyle(T::Body);
		small->fontStyle = FontStyle::Italic;
		button = S::ButtonStyle(T::Heading, buttonTexture, buttonActiveTexture);
		entry = S::EntryStyle(T::Body, buttonTexture, buttonActiveTexture);
		entrySelected = S::EntrySelectedStyle(entry, buttonActiveTexture);
	}

	void HelpScreen::DestroyOwnedTextures()
	{
		panelTexture = nullptr;
		buttonTexture = nullptr;
		buttonActiveTexture = nullptr;
		stylesReady = false;
		if (catalog) catalog->DestroyOwnedTextures();
	}
}
