#pragma once

// ShipCatalogScreen (US-T193): the in-game browser of the full ship catalog (Core::ShipCatalog).

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::Core
{
	class ShipCatalogEntry;
}

namespace SpaceGod::UI
{
	/// The ship catalog page (US-T193): every hull in the game down the left, and the one picked on the right -- what it
	/// is for, how it handles (top speed, turn, roll, acceleration, braking, boost and cruise), how it is flown, its
	/// equipment slots (weapons, shields, engines, utility) and its compartments (cargo, hangar, crew, reactor), and each
	/// of its designs' drives. Read straight from Core::ShipCatalog, the table flight itself reads. Up / Down step through
	/// the hulls (Page Up / Page Down / Home / End jump, the list following the pick); tabs along the top narrow the list to
	/// one category, each with its count, Left / Right stepping through them (US-T302). The Help screen opens it from its
	/// Ship catalog button.
	class ShipCatalogScreen final
	{
	public:
		ShipCatalogScreen() = default;

		/// Draws the page over <paramref name="area"/>.
		void Draw(Rect area);

		void DestroyOwnedTextures();

	private:
		static constexpr float Gutter = 28, HeadingH = 24, RowH = 18, ListRowH = 30, SectionGap = 14, TabH = 28, TabGap = 6;

		bool stylesReady = false;
		cs::Ref<GUIStyle> title, heading, label, value, small, entry, entrySelected, rowCategory;
		cs::Ref<Texture2D> rowTexture, rowActiveTexture, ruleTexture;
		Vector2 scroll, listScroll;
		int selected = 0;
		/// The category tab picked: an index into the tabs, 0 being All (US-T302).
		int tab = 0;
		/// The list scrolls to the pick on the next draw (a key moved it, not a click).
		bool follow = false;
		/// Rows the list showed last draw: how far Page Up / Page Down go.
		int pageRows = 8;

		/// The tabs: All, then each hull category the catalog has a hull of, in the category's order (-1 for All).
		static cs::List<int> Tabs(const cs::List<cs::Ref<Core::ShipCatalogEntry>>& entries);
		/// The hulls the tab picked lists, in the catalog's order.
		cs::List<cs::Ref<Core::ShipCatalogEntry>> Shown(const cs::List<cs::Ref<Core::ShipCatalogEntry>>& entries, int category) const;
		void HandleKeys(int count, const cs::List<int>& tabs, const cs::List<cs::Ref<Core::ShipCatalogEntry>>& entries,
			const cs::List<cs::Ref<Core::ShipCatalogEntry>>& shown);
		void Select(int index);
		/// Picks tab <paramref name="index"/>, keeping the hull picked if the new tab lists it.
		void PickTab(int index, const cs::List<int>& tabs, const cs::List<cs::Ref<Core::ShipCatalogEntry>>& entries,
			const cs::Ref<Core::ShipCatalogEntry>& current);
		/// Draws the tab row over <paramref name="area"/>; returns the height it took (it wraps on a narrow screen).
		float DrawTabs(Rect area, const cs::List<int>& tabs, const cs::List<cs::Ref<Core::ShipCatalogEntry>>& entries,
			const cs::Ref<Core::ShipCatalogEntry>& current);
		void DrawList(Rect area, const cs::List<cs::Ref<Core::ShipCatalogEntry>>& entries);
		void DrawEntry(const cs::Ref<Core::ShipCatalogEntry>& e, Rect view);
		cs::List<std::pair<cs::String, cs::List<std::pair<cs::String, cs::String>>>> Sections(const cs::Ref<Core::ShipCatalogEntry>& e) const;
		void EnsureStyles();
	};
}
