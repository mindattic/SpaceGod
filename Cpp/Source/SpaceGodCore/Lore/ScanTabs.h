#pragma once
// US-T239: the scan panel's tabs and the Prose key of each tab's narrative (docs/PROSE.md, "Scan panels").
//
// The selection card is a scan: a row of subtabs picks what it is about (the body and each settlement on it; a ship and its
// named pilot once met), and a row of tabs picks the aspect (overview, industry, culture, history...). Each tab shows the
// simulation's own numbers for it (game-owned: gravity, population, cargo, crew...) and one Prose description, its
// narrative: the overview's is the entity's own description (its id as the key, as US-T237 already shows it), every other
// tab's is keyed <entity>/<tab>. A tab with no text shows a placeholder. Display only: nothing here touches the simulation.
//
// ScanFacts are the derived scanner readings the card and the universe export both show, so a writer reads in Prose the
// numbers a player reads on the card.

#include "Cs/Cs.h"
#include "BodyDefinition.h"

namespace SpaceGod::Core
{
	class BodyCatalog;

	/// <summary>What a scan is about; each has its own tabs (ScanTabs::Of).</summary>
	enum class ScanSubject : int
	{
		Star,
		/// <summary>A planet, dwarf planet, moon or asteroid.</summary>
		World,
		/// <summary>A station body (an orbital habitat in a system's catalogue).</summary>
		Station,
		/// <summary>A colony, station port, mining outpost or pirate haven on a body.</summary>
		Settlement,
		/// <summary>A civilian hull: merchant, courier, miner, independent.</summary>
		Ship,
		/// <summary>A warship or a pirate raider.</summary>
		Warship,
		/// <summary>A ship's named pilot (once met).</summary>
		Pilot,
	};

	/// <summary>One tab: its id (the key's last part, stable for writers) and its label on the card.</summary>
	struct SPACEGODCORE_API ScanTab
	{
		const char* Id;
		const char* Label;
	};

	class SPACEGODCORE_API ScanTabs final
	{
	public:
		/// <summary>The tab whose narrative is the entity's own description (keyed by the bare entity id).</summary>
		static constexpr const char* Overview = "overview";

		/// <summary>The tabs of a subject, in the card's order (always starting with the overview).</summary>
		static const cs::List<ScanTab>& Of(ScanSubject s);

		/// <summary>Whether <paramref name="s"/> has a tab <paramref name="tab"/>.</summary>
		static bool Has(ScanSubject s, const cs::String& tab);

		/// <summary>The subject of a body of the catalogue (a barycentre has none and is treated as a world).</summary>
		static ScanSubject OfBody(BodyKind kind);

		/// <summary>"star", "world", "station", "settlement", "ship", "warship", "pilot".</summary>
		static const char* Name(ScanSubject s);

		/// <summary>
		/// The Prose key of a tab's narrative for <paramref name="entity"/> (a LoreKeys id): the entity id itself for the
		/// overview (its description), else &lt;entity&gt;/&lt;tab&gt; (planet-sol-earth/history). "" for no entity.
		/// </summary>
		static cs::String Key(const cs::String& entity, const cs::String& tab);

		/// <summary>What a tab shows when Prose has no text for it ("" for the overview, which keeps its old fallback).</summary>
		static cs::String Placeholder(const cs::String& tab);
	};

	/// <summary>Derived scanner readings of a body, shared by the scan card and the universe export.</summary>
	class SPACEGODCORE_API ScanFacts final
	{
	public:
		/// <summary>Surface gravity in g (GM / r^2 against 9.80665 m/s^2); 0 for a body with no radius.</summary>
		static double SurfaceGravityG(const BodyDefinition& b);

		/// <summary>
		/// The black-body equilibrium temperature in kelvin (no albedo, heat spread round the globe): 278.6 K x L^(1/4) /
		/// sqrt(d in AU), with L the light of the star (or of every star of the barycentre) the body's chain orbits and d that
		/// chain's distance from it. NaN for a star, or when the chain reaches no star.
		/// </summary>
		static double EquilibriumK(const BodyCatalog& cat, int index);

		/// <summary>The atmosphere a scanner reads from the catalogue: "gas giant envelope", "thick", "moderate", "thin" or "none to speak of".</summary>
		static const char* Atmosphere(const BodyDefinition& b);
	};
}
