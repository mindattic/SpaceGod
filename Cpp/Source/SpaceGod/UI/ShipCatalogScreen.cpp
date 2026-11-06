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
		selected = Mathf::Clamp(selected, 0, entries.Count() - 1);
		HandleKeys(entries.Count());

		float listW = Mathf::Clamp(area.width * 0.24f, 180.0f, 300.0f);
		DrawList(Rect(area.x, area.y, listW, area.height), entries);
		float x = area.x + listW + Gutter;
		DrawEntry(entries[selected], Rect(x, area.y, area.x + area.width - x, area.height));
	}

	void ShipCatalogScreen::HandleKeys(int count)
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown) return;
		if (e.keyCode == KeyCode::DownArrow && selected < count - 1) { Select(selected + 1); e.Use(); }
		else if (e.keyCode == KeyCode::UpArrow && selected > 0) { Select(selected - 1); e.Use(); }
	}

	void ShipCatalogScreen::Select(int index)
	{
		if (index == selected) return;
		selected = index;
		scroll = Vector2::zero();
	}

	void ShipCatalogScreen::DrawList(Rect area, const cs::List<cs::Ref<ShipCatalogEntry>>& entries)
	{
		GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), cs::Cat("HULLS (", entries.Count(), ")"), heading);
		GUI::DrawTexture(Rect(area.x, area.y + HeadingH - 5, area.width, 1), ruleTexture);
		auto list = Rect(area.x, area.y + HeadingH, area.width, area.height - HeadingH);
		listScroll = GUI::BeginScrollView(list, listScroll, Rect(0, 0, list.width - 16, entries.Count() * ListRowH));
		for (int i = 0; i < entries.Count(); i++)
		{
			auto row = Rect(0, i * ListRowH, list.width - 16, ListRowH - 4);
			if (GUI::Button(row, entries[i]->Name, i == selected ? entrySelected : entry)) Select(i);
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
	}

	void ShipCatalogScreen::DestroyOwnedTextures()
	{
		rowTexture = nullptr;
		rowActiveTexture = nullptr;
		ruleTexture = nullptr;
		stylesReady = false;
	}
}
