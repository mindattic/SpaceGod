#include "UI/ShipCatalogScreen.h"

#include <algorithm>
#include <cmath>

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
		// US-T340: the compare bar over the hull: mark it, unmark it, or say what the two shown are.
		const auto& current = shown[selected];
		float viewW = area.x + area.width - x;
		const float barH = 26;
		cs::String buttonText = marked == nullptr ? cs::String("Compare (C)") : marked == current ? cs::String("Unmark (C)") : cs::String("Stop comparing");
		float buttonW = entry->CalcSize(GUIContent(buttonText)).x + 24;
		if (GUI::Button(Rect(x, top, buttonW, barH), buttonText, marked != nullptr ? entrySelected : entry))
			ToggleMark(marked == nullptr ? current : marked);
		cs::String hint = marked == nullptr ? cs::String("mark this hull, then pick another to see the two side by side")
			: marked == current ? cs::String("marked: pick another hull to compare it with")
			: cs::Cat("comparing with the marked ", marked->Name, "; C on it unmarks");
		GUI::Label(Rect(x + buttonW + 10, top, Mathf::Max(0.0f, viewW - buttonW - 10), barH), hint, small);
		auto view = Rect(x, top + barH + 8, viewW, area.height - tabsH - barH - 8);
		if (marked != nullptr && marked != current) DrawCompare(marked, current, view);
		else DrawEntry(current, view);
	}

	void ShipCatalogScreen::ToggleMark(const cs::Ref<ShipCatalogEntry>& e)
	{
		marked = marked == e ? nullptr : e;
		scroll = Vector2::zero();
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
		// US-T340: C marks the hull picked to compare (on the marked one, unmarks it).
		else if (e.keyCode == KeyCode::C && !e.shift && !e.control && !e.alt && !e.command && count > 0) { ToggleMark(shown[selected]); e.Use(); }
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
			// US-T340: the marked hull's row says so instead.
			if (entries[i] == marked) GUI::Label(Rect(row.x, row.y, row.width - 8, row.height), "marked", rowCategory);
			else if (all) GUI::Label(Rect(row.x, row.y, row.width - 8, row.height), ToString(entries[i]->Category), rowCategory);
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

	void ShipCatalogScreen::DrawCompare(const cs::Ref<ShipCatalogEntry>& a, const cs::Ref<ShipCatalogEntry>& b, Rect view)
	{
		// One row: its name, the two hulls' values, how to show one, and which way is better (+1 more, -1 less, 0 neither).
		struct Row
		{
			cs::String Name;
			double A, B;
			cs::String (*Show)(double);
			int Better;
		};
		auto speed = [](double kmS) -> cs::String { return cs::Cat(cs::Fmt(kmS * 1000, "0"), " m/s"); };
		auto degS = [](double d) -> cs::String { return cs::Cat(cs::Fmt(d, "0.#"), " deg/s"); };
		auto accel = [](double kmS2) -> cs::String { return Accel(kmS2); };
		auto times = [](double m) -> cs::String { return cs::Cat("x", cs::Fmt(m, "0.##")); };
		auto secs = [](double s) -> cs::String { return cs::Cat(cs::Fmt(s, "0.#"), " s"); };
		auto metres = [](double km) -> cs::String { return cs::Cat(cs::Fmt(km * 1000, "0"), " m"); };
		auto tonnesKt = [](double kt) -> cs::String { return cs::Cat(cs::Fmt(kt * 1000, "N0"), " t"); };
		auto tonnes = [](double t) -> cs::String { return cs::Cat(cs::Fmt(t, "N0"), " t"); };
		auto whole = [](double n) -> cs::String { return cs::String(cs::Fmt(n, "N0")); };
		auto tenth = [](double n) -> cs::String { return cs::String(cs::Fmt(n, "0.#")); };
		auto mw = [](double n) -> cs::String { return cs::Cat(cs::Fmt(n, "N0"), " MW"); };
		auto sections = [](double n) -> cs::String { return cs::Cat(cs::Fmt(n, "0.#"), " sections"); };

		const auto& ha = a->Handling;
		const auto& hb = b->Handling;
		const auto& la = a->Lead();
		const auto& lb = b->Lead();
		auto slots = [](const cs::Ref<ShipCatalogEntry>& e, SlotCategory c) { return static_cast<double>(e->SlotCount(c)); };
		cs::List<std::pair<cs::String, cs::List<Row>>> groups;
		groups.Add({ cs::String("HANDLING"), cs::List<Row>{
			{ "Top speed", ha.TopSpeedKmS, hb.TopSpeedKmS, speed, 1 },
			{ "Turn rate (pitch / yaw)", ha.TurnRateDegS, hb.TurnRateDegS, degS, 1 },
			{ "Roll rate", ha.RollRateDegS, hb.RollRateDegS, degS, 1 },
			{ "Acceleration", la.AccelKmS2, lb.AccelKmS2, accel, 1 },
			{ "Braking", la.BrakeKmS2, lb.BrakeKmS2, accel, 1 },
			{ "Strafe", la.StrafeKmS2, lb.StrafeKmS2, accel, 1 },
			{ "Boost", ha.BoostSpeedMultiplier, hb.BoostSpeedMultiplier, times, 1 },
			{ "Boost lasts", ha.BoostDurationS, hb.BoostDurationS, secs, 1 },
			{ "Cruise drive", ha.CruiseSpeedMultiplier, hb.CruiseSpeedMultiplier, times, 1 },
			{ "Cruise spools in", ha.CruiseRampS, hb.CruiseRampS, secs, -1 } } });
		groups.Add({ cs::String("HULL"), cs::List<Row>{
			{ "Length", la.LengthKm, lb.LengthKm, metres, 0 },
			{ "Mass", la.MassKt, lb.MassKt, tonnesKt, 0 },
			{ "Hull", la.Hull, lb.Hull, whole, 1 },
			{ "Shield", la.Shield, lb.Shield, whole, 1 },
			{ "Armour", la.Armor, lb.Armor, tenth, 1 } } });
		groups.Add({ cs::String("EQUIPMENT SLOTS"), cs::List<Row>{
			{ "Weapon", slots(a, SlotCategory::Weapon), slots(b, SlotCategory::Weapon), whole, 1 },
			{ "Shield", slots(a, SlotCategory::Shield), slots(b, SlotCategory::Shield), whole, 1 },
			{ "Engine", slots(a, SlotCategory::Engine), slots(b, SlotCategory::Engine), whole, 1 },
			{ "Utility", slots(a, SlotCategory::Utility), slots(b, SlotCategory::Utility), whole, 1 } } });
		groups.Add({ cs::String("COMPARTMENTS"), cs::List<Row>{
			{ "Cargo", a->CargoTonnes, b->CargoTonnes, tonnes, 1 },
			{ "Hangar (craft)", static_cast<double>(a->HangarCraft), static_cast<double>(b->HangarCraft), whole, 1 },
			{ "Crew", a->Crew, b->Crew, whole, 0 },
			{ "Marines", a->Marines, b->Marines, whole, 1 },
			{ "Reactor reserve", a->ReactorMw, b->ReactorMw, mw, 1 },
			{ "Internal space", a->InternalSpace, b->InternalSpace, sections, 1 },
			{ "Free space", a->SpareSpace, b->SpareSpace, sections, 1 },
			{ "Designs", static_cast<double>(a->Designs.Count()), static_cast<double>(b->Designs.Count()), whole, 0 } } });

		float w = view.width - 16;
		float labelW = Mathf::Clamp(w * 0.34f, 150.0f, 300.0f);
		float colW = (w - labelW) / 2;
		float h = 28 + RowH + SectionGap;
		for (const auto& g : groups) h += HeadingH + g.second.Count() * RowH + SectionGap;
		h += RowH * 3;

		scroll = GUI::BeginScrollView(view, scroll, Rect(0, 0, w, h));
		float y = 0;
		GUI::Label(Rect(labelW, y, colW - 8, 28), cs::Strings::ToUpperInvariant(a->Name), compareHead);
		GUI::Label(Rect(labelW + colW, y, colW - 8, 28), cs::Strings::ToUpperInvariant(b->Name), compareHead);
		y += 28;
		GUI::Label(Rect(0, y, labelW - 8, RowH), "Category", label);
		GUI::Label(Rect(labelW, y, colW - 8, RowH), cs::Cat("marked  ·  ", ToString(a->Category)), small);
		GUI::Label(Rect(labelW + colW, y, colW - 8, RowH), cs::Cat("picked  ·  ", ToString(b->Category)), small);
		y += RowH + SectionGap;
		int aWins = 0, bWins = 0;
		for (const auto& g : groups)
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), g.first, heading);
			GUI::DrawTexture(Rect(0, y + HeadingH - 5, w, 1), ruleTexture);
			y += HeadingH;
			for (const auto& r : g.second)
			{
				// The better one only where the row has a better way and the two differ (beyond rounding).
				bool differ = std::abs(r.A - r.B) > 1e-6 * std::max(1.0, std::max(std::abs(r.A), std::abs(r.B)));
				bool aBetter = differ && r.Better != 0 && (r.Better > 0 ? r.A > r.B : r.A < r.B);
				bool bBetter = differ && r.Better != 0 && !aBetter;
				if (aBetter) aWins++;
				if (bBetter) bWins++;
				GUI::Label(Rect(0, y, labelW - 8, RowH), r.Name, label);
				GUI::Label(Rect(labelW, y, colW - 8, RowH), r.Show(r.A), aBetter ? better : value);
				GUI::Label(Rect(labelW + colW, y, colW - 8, RowH), r.Show(r.B), bBetter ? better : value);
				y += RowH;
			}
			y += SectionGap;
		}
		GUI::Label(Rect(0, y, w, RowH), cs::Cat("Better in ", aWins, " row", aWins == 1 ? "" : "s", " the ", a->Name, ", in ", bWins, " the ", b->Name,
			" (length, mass, crew and designs are neither)"), small);
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
		// US-T340: the better value of a compared row, and the two hulls' names over their columns.
		better = S::TextStyle(T::Body, S::Accent, FontStyle::Bold);
		better->clipping = TextClipping::Clip;
		compareHead = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
		compareHead->clipping = TextClipping::Clip;
	}

	void ShipCatalogScreen::DestroyOwnedTextures()
	{
		rowTexture = nullptr;
		rowActiveTexture = nullptr;
		ruleTexture = nullptr;
		stylesReady = false;
	}
}
