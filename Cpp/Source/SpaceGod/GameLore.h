#pragma once
// US-T236: the game's one copy of the Prose texts (Core's LoreLibrary over Data/Lore; docs/PROSE.md).

#include "Lore/LoreLibrary.h"

namespace SpaceGod
{
	/// <summary>
	/// The Prose texts the game ships, loaded once on first use. The folder is SPACEGOD_LORE_DIR when set, else the
	/// package's Lore/ folder (tools/package.ps1 copies Data/Lore there), else the repo's Data/Lore beside Cpp/ (an editor
	/// or uncooked run). A missing folder is an empty library: every lookup answers its placeholder, and play goes on.
	/// </summary>
	const Core::LoreLibrary& GameLore();

	// US-T237: the one way a panel asks for story. Each surface makes its key with Core::LoreKeys (the export's own ids)
	// and asks once; with no approved text it gets back exactly what it showed before (its generated line, or nothing).
	// Display only: no simulation code may call these, so play never depends on what Prose has written.

	/// <summary>A description's short line, for a tight space (a list row, a channel header); else <paramref name="placeholder"/>.</summary>
	cs::String LoreShort(const cs::String& key, const cs::String& placeholder = cs::String());

	/// <summary>A description's long text, for a detail view (the selection card); else <paramref name="placeholder"/>.</summary>
	cs::String LoreLong(const cs::String& key, const cs::String& placeholder = cs::String());

	/// <summary>A line of the bark <paramref name="name"/> said by <paramref name="speaker"/> (an entity id), else <paramref name="placeholder"/>.</summary>
	cs::String LoreBark(const cs::String& speaker, const cs::String& name, uint32_t pick, const cs::String& placeholder);
}
