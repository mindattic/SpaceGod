#pragma once
// US-T237: the lookup keys of the Prose texts, made from the game's own things (docs/PROSE.md, "Keys" and "Ids").
//
// A Prose text is keyed by the universe entity it is about (planet-sol-earth), or by that id and a name for anything else
// about it (pilot-tomas-brennan/hail). The ids are the ones tools/universe-export writes, so they are made here, once,
// and the exporter and the game's panels both call these: a panel can never ask for a key the export spells differently.
//
// Two things the game cannot rebuild alone, and so never finds a text for (it keeps its placeholder): an entity the
// export had to suffix (-2, -3) because two things slug alike, and the bodies of a void station (exported under the
// station's own id, not a system's). Nothing here touches the simulation.

#include "Cs/Cs.h"
#include "BodyDefinition.h"

namespace SpaceGod::Core
{
	/// <summary>Prose-stable ids and text keys for the game's entities (the same ones the universe export writes).</summary>
	class SPACEGODCORE_API LoreKeys
	{
	public:
		/// <summary>
		/// A Prose-stable slug: lowercase ASCII letters and digits joined by single hyphens, nothing else ("unnamed" when
		/// nothing is left). Prose re-slugifies every id with exactly this rule, so an id in this form comes back unchanged.
		/// </summary>
		static cs::String Slug(const cs::String& s);

		/// <summary>A system's short slug: its name's slug without a trailing "-system" ("Sol System" -> "sol").</summary>
		static cs::String SystemSlug(const cs::String& systemName);

		/// <summary>The universe type of a body kind: star, planet, dwarf-planet, moon, barycentre, station, asteroid.</summary>
		static const char* BodyType(BodyKind kind);

		/// <summary>A star system: system-&lt;sys&gt;.</summary>
		static cs::String System(const cs::String& systemName);

		/// <summary>A body of a system: &lt;type&gt;-&lt;sys&gt;-&lt;name&gt; (planet-sol-earth, moon-sol-europa).</summary>
		static cs::String Body(BodyKind kind, const cs::String& systemName, const cs::String& bodyName);

		/// <summary>A settlement (a colony, station, outpost or pirate haven): settlement-&lt;sys&gt;-&lt;name&gt;.</summary>
		static cs::String Settlement(const cs::String& systemName, const cs::String& settlementName);

		/// <summary>A ship in a system: ship-&lt;sys&gt;-&lt;name&gt;.</summary>
		static cs::String Ship(const cs::String& systemName, const cs::String& shipName);

		/// <summary>A named pilot, by their plain name (no rank or callsign): pilot-&lt;name&gt;.</summary>
		static cs::String Pilot(const cs::String& pilotName);

		/// <summary>A text about an entity other than its description: &lt;entity&gt;/&lt;name&gt; (pilot-tomas-brennan/hail).</summary>
		static cs::String Named(const cs::String& entity, const cs::String& name);

		/// <summary>US-T239: a faction (a state, company, free traders or pirate clan): faction-&lt;name&gt;.</summary>
		static cs::String Faction(const cs::String& factionName);

		/// <summary>The name of the bark a speaker answers a hail with.</summary>
		static constexpr const char* Hail = "hail";

		/// <summary>US-T239: the key of a text about the universe as a whole, and the head of its named texts (spacegod/&lt;name&gt;).</summary>
		static constexpr const char* Universe = "spacegod";
	};
}
