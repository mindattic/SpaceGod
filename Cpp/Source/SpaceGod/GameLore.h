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
}
