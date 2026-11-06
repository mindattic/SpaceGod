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
	/// the hulls. The Help screen opens it from its Ship catalog button.
	class ShipCatalogScreen final
	{
	public:
		ShipCatalogScreen() = default;

		/// Draws the page over <paramref name="area"/>.
		void Draw(Rect area);

		void DestroyOwnedTextures();

	private:
		static constexpr float Gutter = 28, HeadingH = 24, RowH = 18, ListRowH = 30, SectionGap = 14;

		bool stylesReady = false;
		cs::Ref<GUIStyle> title, heading, label, value, small, entry, entrySelected;
		cs::Ref<Texture2D> rowTexture, rowActiveTexture, ruleTexture;
		Vector2 scroll, listScroll;
		int selected = 0;

		void HandleKeys(int count);
		void Select(int index);
		void DrawList(Rect area, const cs::List<cs::Ref<Core::ShipCatalogEntry>>& entries);
		void DrawEntry(const cs::Ref<Core::ShipCatalogEntry>& e, Rect view);
		cs::List<std::pair<cs::String, cs::List<std::pair<cs::String, cs::String>>>> Sections(const cs::Ref<Core::ShipCatalogEntry>& e) const;
		void EnsureStyles();
	};
}
