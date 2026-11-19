#include "UI/ShipCatalogScreen.h"

#include "Combat/Construction.h"
#include "Combat/FlightModels.h"
#include "Combat/Outfitting.h"
#include "Combat/PilotTuning.h"
#include "Combat/ShipCatalog.h"
#include "UI/UiStyle.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		using Line = std::pair<cs::String, cs::String>;

		cs::String Accel(double kmS2)
		{
			double ms2 = kmS2 * 1000;
			return cs::Cat(cs::Fmt(ms2, "0.##"), " m/s²  (", cs::Fmt(ms2 / 9.80665, "0.##"), " g)");
		}

		cs::String ControlName(PilotControl c)
		{
			switch (c)
			{
				case PilotControl::FreeAim: return "Free aim: the hull turns after the crosshair";
				case PilotControl::DirectNose: return "Direct nose: the mouse turns the hull";
				case PilotControl::Hauler: return "Hauler: nose-first, heavy and deliberate";
				case PilotControl::Helm: return "Helm: keys turn the hull, the turrets fight on their own";
			}
			return "";
		}

		cs::String StockName(const EquipmentSlot& s)
		{
			if (!s.Stock.has_value()) return "empty";
			const auto& m = ModuleSpec::Of(*s.Stock);
			return m != nullptr ? m->Name : cs::String(ToString(*s.Stock));
		}
	}

	void ShipCatalogScreen::Draw(Rect area)
	{
		EnsureStyles();
		const auto& entries = ShipCatalog::Entries();
		if (entries.Count() == 0) return;
		auto tabs = Tabs(entries);
		tab = Mathf::Clamp(tab, 0, tabs.Count() - 1);
		auto shown = Shown(entries, tabs[tab]);
		selected = Mathf::Clamp(selected, 0, shown.Count() - 1);
		HandleKeys(shown.Count(), tabs, entries, shown);
		// A key may have changed the tab: list what it picks now.
		shown = Shown(entries, tabs[tab]);
		selected = Mathf::Clamp(selected, 0, shown.Count() - 1);

		float tabsH = DrawTabs(Rect(area.x, area.y, area.width, area.height), tabs, entries, shown[selected]);
		// A click on a tab changes it while drawing the row.
		shown = Shown(entries, tabs[tab]);
		selected = Mathf::Clamp(selected, 0, shown.Count() - 1);
		float top = area.y + tabsH;
		float listW = Mathf::Clamp(area.width * 0.24f, 180.0f, 300.0f);
		DrawList(Rect(area.x, top, listW, area.height - tabsH), shown);
		float x = area.x + listW + Gutter;
		DrawEntry(shown[selected], Rect(x, top, area.x + area.width - x, area.height - tabsH));
	}

	cs::List<int> ShipCatalogScreen::Tabs(const cs::List<cs::Ref<ShipCatalogEntry>>& entries)
	{
		cs::List<int> tabs;
		tabs.Add(-1);
		int last = -1;
		for (const auto& e : entries) last = Mathf::Max(last, static_cast<int>(e->Category));
		for (int c = 0; c <= last; c++)
		{
			for (const auto& e : entries)
				if (static_cast<int>(e->Category) == c) { tabs.Add(c); break; }
		}
		return tabs;
	}

	cs::List<cs::Ref<ShipCatalogEntry>> ShipCatalogScreen::Shown(const cs::List<cs::Ref<ShipCatalogEntry>>& entries, int category) const
	{
		if (category < 0) return entries;
		cs::List<cs::Ref<ShipCatalogEntry>> shown;
		for (const auto& e : entries)
			if (static_cast<int>(e->Category) == category) shown.Add(e);
		return shown;
	}

	void ShipCatalogScreen::HandleKeys(int count, const cs::List<int>& tabs, const cs::List<cs::Ref<ShipCatalogEntry>>& entries,
		const cs::List<cs::Ref<ShipCatalogEntry>>& shown)
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown) return;
		auto keySelect = [&](int index) { Select(Mathf::Clamp(index, 0, Mathf::Max(0, count - 1))); follow = true; e.Use(); };
		if (e.keyCode == KeyCode::DownArrow && selected < count - 1) keySelect(selected + 1);
		else if (e.keyCode == KeyCode::UpArrow && selected > 0) keySelect(selected - 1);
		else if (e.keyCode == KeyCode::PageDown && count > 0) keySelect(selected + pageRows);
		else if (e.keyCode == KeyCode::PageUp && count > 0) keySelect(selected - pageRows);
		else if (e.keyCode == KeyCode::End && count > 0) keySelect(count - 1);
		else if (e.keyCode == KeyCode::Home && count > 0) keySelect(0);
		else if (e.keyCode == KeyCode::RightArrow && tab < tabs.Count() - 1) { PickTab(tab + 1, tabs, entries, shown[selected]); e.Use(); }
		else if (e.keyCode == KeyCode::LeftArrow && tab > 0) { PickTab(tab - 1, tabs, entries, shown[selected]); e.Use(); }
	}

	void ShipCatalogScreen::Select(int index)
	{
		if (index == selected) return;
		selected = index;
		scroll = Vector2::zero();
	}

	void ShipCatalogScreen::PickTab(int index, const cs::List<int>& tabs, const cs::List<cs::Ref<ShipCatalogEntry>>& entries,
		const cs::Ref<ShipCatalogEntry>& current)
	{
		if (index == tab) return;
		tab = index;
		int keep = Shown(entries, tabs[tab]).IndexOf(current);
		selected = -1;
		Select(Mathf::Max(0, keep));
		follow = true;
	}

	float ShipCatalogScreen::DrawTabs(Rect area, const cs::List<int>& tabs, const cs::List<cs::Ref<ShipCatalogEntry>>& entries,
		const cs::Ref<ShipCatalogEntry>& current)
	{
		float x = 0, y = 0;
		for (int i = 0; i < tabs.Count(); i++)
		{
			int category = tabs[i];
			cs::String name = category < 0 ? cs::String("All") : cs::String(ToString(static_cast<HullCategory>(category)));
			cs::String text = cs::Cat(name, " (", Shown(entries, category).Count(), ")");
			float w = entry->CalcSize(GUIContent(text)).x + 20;
			if (x > 0 && x + w > area.width) { x = 0; y += TabH + TabGap; }
			if (GUI::Button(Rect(area.x + x, area.y + y, w, TabH), text, i == tab ? entrySelected : entry)) PickTab(i, tabs, entries, current);
			x += w + TabGap;
		}
		return y + TabH + SectionGap;
	}

	void ShipCatalogScreen::DrawList(Rect area, const cs::List<cs::Ref<ShipCatalogEntry>>& entries)
	{
		GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), cs::Cat("HULLS (", entries.Count(), ")"), heading);
		GUI::DrawTexture(Rect(area.x, area.y + HeadingH - 5, area.width, 1), ruleTexture);
		auto list = Rect(area.x, area.y + HeadingH, area.width, area.height - HeadingH);
		pageRows = Mathf::Max(1, static_cast<int>(list.height / ListRowH) - 1);
		if (follow)
		{
			// A key moved the pick: scroll just enough to bring its row into view.
			follow = false;
			float rowTop = selected * ListRowH;
			if (rowTop < listScroll.y) listScroll.y = rowTop;
			else if (rowTop + ListRowH > listScroll.y + list.height) listScroll.y = rowTop + ListRowH - list.height;
		}
		bool all = tab == 0;
		listScroll = GUI::BeginScrollView(list, listScroll, Rect(0, 0, list.width - 16, entries.Count() * ListRowH));
		for (int i = 0; i < entries.Count(); i++)
		{
			auto row = Rect(0, i * ListRowH, list.width - 16, ListRowH - 4);
			if (GUI::Button(row, entries[i]->Name, i == selected ? entrySelected : entry)) Select(i);
			// Under All, each row says its category on the right (a tab lists one, so it would only repeat it).
			if (all) GUI::Label(Rect(row.x, row.y, row.width - 8, row.height), ToString(entries[i]->Category), rowCategory);
		}
		GUI::EndScrollView();
	}

	cs::List<std::pair<cs::String, cs::List<Line>>> ShipCatalogScreen::Sections(const cs::Ref<ShipCatalogEntry>& e) const
	{
		cs::List<std::pair<cs::String, cs::List<Line>>> out;
		const auto& h = e->Handling;
		const auto& lead = e->Lead();

		cs::List<Line> handling;
		handling.Add({ "Top speed", cs::Cat(cs::Fmt(h.TopSpeedKmS * 1000, "0"), " m/s") });
		handling.Add({ "Turn rate (pitch / yaw)", cs::Cat(cs::Fmt(h.TurnRateDegS, "0.#"), " deg/s") });
		handling.Add({ "Roll rate", cs::Cat(cs::Fmt(h.RollRateDegS, "0.#"), " deg/s") });
		handling.Add({ "Acceleration", Accel(lead.AccelKmS2) });
		handling.Add({ "Braking", Accel(lead.BrakeKmS2) });
		handling.Add({ "Strafe", Accel(lead.StrafeKmS2) });
		handling.Add({ "Boost", cs::Cat("x", cs::Fmt(h.BoostSpeedMultiplier, "0.##"), " for ", cs::Fmt(h.BoostDurationS, "0"), " s") });
		handling.Add({ "Cruise drive", cs::Cat("x", cs::Fmt(h.CruiseSpeedMultiplier, "0.#"), ", spools in ", cs::Fmt(h.CruiseRampS, "0.#"), " s") });
		handling.Add({ "Flown by", ControlName(e->Control) });
		handling.Add({ "Flight model", ToString(e->Family) });
		out.Add({ "HANDLING", handling });

		cs::List<Line> hull;
		hull.Add({ "Category", cs::Cat(ToString(e->Category), e->SmallCraft ? "  (small craft)" : "") });
		hull.Add({ "Length", cs::Cat(cs::Fmt(lead.LengthKm * 1000, "0"), " m") });
		hull.Add({ "Mass", cs::Cat(cs::Fmt(lead.MassKt * 1000, "N0"), " t") });
		hull.Add({ "Hull / shield / armour", cs::Cat(cs::Fmt(lead.Hull, "N0"), " / ", cs::Fmt(lead.Shield, "N0"), " / ", cs::Fmt(lead.Armor, "0.#")) });
		out.Add({ "HULL", hull });

		cs::List<Line> slots;
		const SlotCategory cats[] = { SlotCategory::Weapon, SlotCategory::Shield, SlotCategory::Engine };
		for (auto cat : cats)
		{
			for (const auto& s : e->Slots)
				if (s.Category == cat) slots.Add({ cs::Cat(ToString(cat), ": ", s.Name), StockName(s) });
		}
		int utility = e->SlotCount(SlotCategory::Utility);
		slots.Add({ "Utility", utility > 0 ? cs::Cat(utility, " section", utility == 1 ? "" : "s", " of free space (plate, racks, scanner, brig...)") : cs::String("none") });
		out.Add({ cs::Cat("EQUIPMENT SLOTS  ·  ", e->SlotCount(SlotCategory::Weapon), " weapon, ", e->SlotCount(SlotCategory::Shield), " shield, ",
			e->SlotCount(SlotCategory::Engine), " engine, ", utility, " utility"), slots });

		cs::List<Line> rooms;
		rooms.Add({ "Cargo", e->CargoTonnes > 0 ? cs::Cat(cs::Fmt(e->CargoTonnes, "N0"), " t") : cs::String("none") });
		rooms.Add({ "Hangar", e->HangarCraft > 0 ? cs::Cat(e->HangarCraft, " craft") : cs::String("none") });
		rooms.Add({ "Crew / marines", cs::Cat(cs::Fmt(e->Crew, "N0"), " / ", cs::Fmt(e->Marines, "N0")) });
		rooms.Add({ "Reactor reserve", cs::Cat(cs::Fmt(e->ReactorMw, "N0"), " MW for fittings") });
		rooms.Add({ "Internal space", cs::Cat(cs::Fmt(e->InternalSpace, "0.#"), " sections, ", cs::Fmt(e->SpareSpace, "0.#"), " free") });
		for (const auto& c : e->Compartments)
			rooms.Add({ cs::Cat(ToString(c.Role), ": ", c.Name), cs::Cat(cs::Fmt(c.Size, "0.#"), " sections") });
		out.Add({ "COMPARTMENTS", rooms });

		cs::List<Line> designs;
		for (const auto& d : e->Designs)
			designs.Add({ d.Name, cs::Cat(cs::Fmt(d.LengthKm * 1000, "0"), " m, accel ", cs::Fmt(d.AccelKmS2 * 1000, "0.#"), " / brake ",
				cs::Fmt(d.BrakeKmS2 * 1000, "0.#"), " m/s²") });
		out.Add({ cs::Cat("DESIGNS (", e->Designs.Count(), ")"), designs });
		return out;
	}

	void ShipCatalogScreen::DrawEntry(const cs::Ref<ShipCatalogEntry>& e, Rect view)
	{
		auto sections = Sections(e);
		float w = view.width - 16;
		float labelW = Mathf::Clamp(w * 0.42f, 160.0f, 340.0f);
		float h = 28 + RowH + SectionGap;
		for (const auto& s : sections) h += HeadingH + s.second.Count() * RowH + SectionGap;

		scroll = GUI::BeginScrollView(view, scroll, Rect(0, 0, w, h));
		float y = 0;
		GUI::Label(Rect(0, y, w, 28), cs::Strings::ToUpperInvariant(e->Name), title);
		y += 28;
		GUI::Label(Rect(0, y, w, RowH), e->Role, small);
		y += RowH + SectionGap;
		for (const auto& s : sections)
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), s.first, heading);
			GUI::DrawTexture(Rect(0, y + HeadingH - 5, w, 1), ruleTexture);
			y += HeadingH;
			for (const auto& line : s.second)
			{
				GUI::Label(Rect(0, y, labelW - 8, RowH), line.first, label);
				GUI::Label(Rect(labelW, y, w - labelW, RowH), line.second, value);
				y += RowH;
			}
			y += SectionGap;
		}
		GUI::EndScrollView();
	}

	void ShipCatalogScreen::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The Help screen's look (HelpScreen::EnsureStyles), so the catalog reads as one of its pages.
		// Its styles from the shared look (US-T199, UiStyle.h), the same factories the Help screen builds its own from.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		rowTexture = S::ControlTexture();
		rowActiveTexture = S::ControlOnTexture();
		ruleTexture = rowActiveTexture;
		title = S::TitleStyle(T::Chapter);
		heading = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
		label = S::TextStyle(T::Body, S::Dim);
		label->clipping = TextClipping::Clip;
		value = S::TextStyle(T::Body);
		value->clipping = TextClipping::Clip;
		small = S::TextStyle(T::Body, S::Dim, FontStyle::Italic);
		small->clipping = TextClipping::Clip;
		entry = S::EntryStyle(T::Body, rowTexture, rowActiveTexture);
		entrySelected = S::EntrySelectedStyle(entry, rowActiveTexture);
		rowCategory = S::TextStyle(T::Body, S::Dim, FontStyle::Italic);
		rowCategory->alignment = TextAnchor::MiddleRight;
		rowCategory->clipping = TextClipping::Clip;
	}

	void ShipCatalogScreen::DestroyOwnedTextures()
	{
		rowTexture = nullptr;
		rowActiveTexture = nullptr;
		ruleTexture = nullptr;
		stylesReady = false;
	}
}
