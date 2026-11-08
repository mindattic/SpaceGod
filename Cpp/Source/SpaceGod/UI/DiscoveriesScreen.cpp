#include "UI/DiscoveriesScreen.h"

#include "UnitySpaceApp.h"
#include "Astro.h"
#include "GameLore.h"
#include "SimClock.h"
#include "Systems/DiscoveryLog.h"
#include "Systems/Galaxy.h"
#include "UI/UiStyle.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	DiscoveriesScreen::DiscoveriesScreen(UnitySpaceApp* app) : app(app) {}

	DiscoveriesScreen::~DiscoveriesScreen() = default;

	cs::String DiscoveriesScreen::Date(double jd)
	{
		return jd <= 0 ? cs::String("date unknown") : Astro::ToDateTime(jd).ToString("yyyy-MM-dd");
	}

	const char* DiscoveriesScreen::HowFound(int k)
	{
		switch (static_cast<DiscoveryKind>(k))
		{
			case DiscoveryKind::Star: return "A star is charted when a lookout (you, or a group crossing open space) comes near enough to find it, and logged too when you visit it.";
			case DiscoveryKind::Body: return "A body is surveyed when you open it on the scan card (select it) or bring a helm in close to it.";
			case DiscoveryKind::Settlement: return "The colonies, station ports and outposts of each system you go to are logged as you arrive.";
			case DiscoveryKind::Haven: return "A pirate haven is hidden until somebody finds it, a navy's scanners or yours by coming close.";
			case DiscoveryKind::Graveyard: return "Some systems and void stations keep a graveyard of old hulks: go there to find it.";
			case DiscoveryKind::Derelict: return "Derelict stations lie dark in graveyards and on their own: go there to find one.";
			case DiscoveryKind::VoidSight: return "Void stations keep a sight or two worth the detour while the drive cools: stop at one.";
			case DiscoveryKind::Sighting: return "Bio Mass blooms in the systems you are in, and Void Raider strikes and signs of the Watcher out between the stars.";
			case DiscoveryKind::Pilot: return "A pilot is met by hailing them, flying their hull, fighting them or pulling them out of a wreck.";
		}
		return "";
	}

	void DiscoveriesScreen::Refresh(const DiscoveryLog& log)
	{
		if (shownLog == &log && shownVersion == log.Version && shownKind == kind) return;
		const DiscoveryEntry* was = selected >= 0 && selected < shown.Count() && shownKind == kind ? shown[selected].get() : nullptr;
		shown = log.Of(static_cast<DiscoveryKind>(kind));
		shownLog = &log;
		shownVersion = log.Version;
		shownKind = kind;
		// A new find (listed first) does not move the card off the find being read.
		if (was != nullptr)
			for (int i = 0; i < shown.Count(); i++)
				if (shown[i].get() == was) { selected = i; break; }
		selected = shown.Count() == 0 ? 0 : Mathf::Clamp(selected, 0, shown.Count() - 1);
	}

	bool DiscoveriesScreen::Draw(const cs::String& closeHint)
	{
		EnsureStyles();
		const auto& log = app->Discoveries();
		Refresh(log);
		HandleKeys(shown.Count());
		Refresh(log);

		auto screen = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		GUI::Box(screen, GUIContent::none(), panel);
		GUI::Label(Rect(Margin, 22, screen.width - 2 * Margin, 30), "DISCOVERIES", title);
		GUI::Label(Rect(Margin, 54, screen.width - 2 * Margin, 18),
			cs::Cat("What you have found, by kind and the day it was found  \xC2\xB7  Up / Down for the next find, Left / Right for the next kind  \xC2\xB7  ", closeHint), small);

		float top = 86, bottom = screen.height - 64;
		float kindsW = Mathf::Clamp(screen.width * 0.16f, 170.0f, 230.0f);
		DrawKinds(Rect(Margin, top, kindsW, bottom - top), log);
		float listX = Margin + kindsW + Gutter;
		float listW = Mathf::Clamp(screen.width * 0.24f, 220.0f, 330.0f);
		DrawList(Rect(listX, top, listW, bottom - top));
		Refresh(log); // a click on the kinds or the list may have picked another this event

		float x = listX + listW + Gutter;
		auto card = Rect(x, top, screen.width - Margin - x, bottom - top);
		bool went = false;
		if (shown.Count() == 0)
		{
			GUI::Label(Rect(card.x, card.y, card.width, HeadingH - 4), "NOTHING YET", heading);
			GUI::Label(Rect(card.x, card.y + HeadingH, card.width, 80), HowFound(kind), text);
		}
		else went = DrawEntry(*shown[selected], card, screen.height - 50);
		if (went) return true;
		return GUI::Button(Rect(screen.width - Margin - 120, screen.height - 50, 120, 32), "Back", button);
	}

	void DiscoveriesScreen::HandleKeys(int count)
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown) return;
		if (e.keyCode == KeyCode::DownArrow && selected < count - 1) { Pick(selected + 1); e.Use(); }
		else if (e.keyCode == KeyCode::UpArrow && selected > 0) { Pick(selected - 1); e.Use(); }
		else if (e.keyCode == KeyCode::RightArrow && kind < DiscoveryLog::KindCount - 1) { PickKind(kind + 1); e.Use(); }
		else if (e.keyCode == KeyCode::LeftArrow && kind > 0) { PickKind(kind - 1); e.Use(); }
	}

	void DiscoveriesScreen::Pick(int index)
	{
		if (index == selected) return;
		selected = index;
		scroll = Vector2::zero();
	}

	void DiscoveriesScreen::PickKind(int k)
	{
		if (k == kind) return;
		kind = k;
		selected = 0;
		scroll = Vector2::zero();
		listScroll = Vector2::zero();
	}

	void DiscoveriesScreen::DrawKinds(Rect area, const DiscoveryLog& log)
	{
		GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), cs::Cat("FOUND  (", log.Entries.Count(), ")"), heading);
		GUI::DrawTexture(Rect(area.x, area.y + HeadingH - 5, area.width, 1), buttonActiveTexture);
		for (int k = 0; k < DiscoveryLog::KindCount; k++)
		{
			auto row = Rect(area.x, area.y + HeadingH + k * KindRowH, area.width, KindRowH - 4);
			cs::String label = cs::Cat(DiscoveryLog::Heading(static_cast<DiscoveryKind>(k)), "  (", log.CountOf(static_cast<DiscoveryKind>(k)), ")");
			if (GUI::Button(row, label, k == kind ? entrySelected : entry)) PickKind(k);
		}
	}

	void DiscoveriesScreen::DrawList(Rect area)
	{
		GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4),
			cs::Strings::ToUpperInvariant(DiscoveryLog::Heading(static_cast<DiscoveryKind>(kind))), heading);
		GUI::DrawTexture(Rect(area.x, area.y + HeadingH - 5, area.width, 1), buttonActiveTexture);
		auto list = Rect(area.x, area.y + HeadingH, area.width, area.height - HeadingH);
		float contentH = shown.Count() * RowH;
		listScroll = GUI::BeginScrollView(list, listScroll, Rect(0, 0, list.width - 16, contentH));
		// Only the rows in view are drawn: a long career meets hundreds of pilots.
		int first = Mathf::Max(0, static_cast<int>(listScroll.y / RowH) - 1);
		int last = Mathf::Min(shown.Count() - 1, static_cast<int>((listScroll.y + list.height) / RowH) + 1);
		for (int i = first; i <= last; i++)
		{
			const auto& e = shown[i];
			auto row = Rect(0, i * RowH, list.width - 16, RowH - 4);
			cs::String where = e->System.empty() || e->System == e->Name ? cs::String() : cs::Cat("  \xC2\xB7  ", e->System);
			if (GUI::Button(row, cs::Cat(e->Name, "\n", Date(e->Jd), where), i == selected ? entrySelected : entry)) Pick(i);
		}
		GUI::EndScrollView();
	}

	bool DiscoveriesScreen::DrawEntry(const DiscoveryEntry& e, Rect view, float buttonsY)
	{
		double jd = app->Clock != nullptr ? app->Clock->Jd : e.Jd;
		auto readings = DiscoveryLog::Readings(*app->Galaxy, e, jd);
		cs::String record = GameLore().Long(e.LoreKey, "No record on file.");
		cs::String found = cs::Cat(DiscoveryLog::Heading(e.Kind), e.System.empty() || e.System == e.Name ? cs::String() : cs::Cat("  \xC2\xB7  ", e.System),
			"  \xC2\xB7  found ", Date(e.Jd), e.By.empty() ? cs::String() : cs::Cat(" by ", e.By));

		float w = view.width - 16;
		float noteH = e.Note.empty() ? 0.0f : text->CalcHeight(GUIContent(e.Note), w) + 6;
		float readingsH = 0;
		for (const auto& line : readings) readingsH += text->CalcHeight(GUIContent(line), w) + 2;
		float recordH = text->CalcHeight(GUIContent(record), w);
		float cardH = view.height - 44; // the buttons' row below
		float contentH = 28 + 22 + 10 + noteH + HeadingH + readingsH + 12 + HeadingH + recordH;
		scroll = GUI::BeginScrollView(Rect(view.x, view.y, view.width, cardH), scroll, Rect(0, 0, w, contentH));
		float y = 0;
		GUI::Label(Rect(0, y, w, 28), cs::Strings::ToUpperInvariant(e.Name), entryTitle);
		y += 28;
		GUI::Label(Rect(0, y, w, 20), found, small);
		y += 22;
		GUI::DrawTexture(Rect(0, y + 4, w, 1), buttonActiveTexture);
		y += 10;
		if (!e.Note.empty())
		{
			GUI::Label(Rect(0, y, w, noteH), e.Note, text);
			y += noteH;
		}
		GUI::Label(Rect(0, y, w, HeadingH - 4), "SCAN READINGS", heading);
		y += HeadingH;
		if (readings.Count() == 0)
		{
			GUI::Label(Rect(0, y, w, 20), "Nothing more to read from here.", small);
			y += 22;
		}
		for (const auto& line : readings)
		{
			float h = text->CalcHeight(GUIContent(line), w);
			GUI::Label(Rect(0, y, w, h), line, text);
			y += h + 2;
		}
		y += 12;
		GUI::Label(Rect(0, y, w, HeadingH - 4), "RECORD", heading);
		y += HeadingH;
		GUI::Label(Rect(0, y, w, recordH), record, text);
		GUI::EndScrollView();

		// Go to it here, or plot a course to where it is.
		float bx = view.x;
		Target target = Target::None();
		if (app->DiscoveryTarget(e, target))
		{
			if (GUI::Button(Rect(bx, buttonsY, 120, 32), "Select", button) && app->GoToDiscovery(e, false)) return true;
			bx += 132;
			if (!app->AtControls())
			{
				if (GUI::Button(Rect(bx, buttonsY, 120, 32), "Focus", button) && app->GoToDiscovery(e, true)) return true;
				bx += 132;
			}
		}
		cs::String refusal = app->DiscoveryCourseRefusal(e);
		if (refusal.empty())
		{
			if (GUI::Button(Rect(bx, buttonsY, 160, 32), "Plot course", button) && app->PlotDiscoveryCourse(e)) return true;
		}
		else if (refusal != "Here, in this system." || bx == view.x)
			GUI::Label(Rect(bx, buttonsY + 6, Mathf::Max(120.0f, view.x + view.width - 140 - bx), 20), refusal, small);
		return false;
	}

	void DiscoveriesScreen::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The Help screen's look (HelpScreen::EnsureStyles, UiStyle.h) at full-screen depth.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Screen);
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();
		panel = S::PanelStyle(panelTexture);
		title = S::TitleStyle(T::Screen);
		entryTitle = S::TitleStyle(T::Chapter);
		heading = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
		text = S::TextStyle(T::Body);
		text->wordWrap = true;
		small = S::CaptionStyle(T::Body);
		small->fontStyle = FontStyle::Italic;
		button = S::ButtonStyle(T::Heading, buttonTexture, buttonActiveTexture);
		entry = S::EntryStyle(T::Body, buttonTexture, buttonActiveTexture);
		entry->wordWrap = true;
		entrySelected = S::EntrySelectedStyle(entry, buttonActiveTexture);
	}

	void DiscoveriesScreen::DestroyOwnedTextures()
	{
		panelTexture = nullptr;
		buttonTexture = nullptr;
		buttonActiveTexture = nullptr;
		stylesReady = false;
	}
}
