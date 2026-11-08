// US-T237: LoreKeys, the Prose ids and text keys of the game's entities (Lore/LoreKeys.h, docs/PROSE.md).

#include "Lore/LoreKeys.h"

namespace SpaceGod::Core
{
	cs::String LoreKeys::Slug(const cs::String& s)
	{
		cs::String o;
		bool dash = false;
		for (unsigned char c : s)
		{
			if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
			if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			{
				if (dash && !o.empty()) o += '-';
				dash = false;
				o += static_cast<char>(c);
			}
			else dash = true;
		}
		return o.empty() ? cs::String("unnamed") : o;
	}

	cs::String LoreKeys::SystemSlug(const cs::String& systemName)
	{
		cs::String s = Slug(systemName);
		const cs::String suffix = "-system";
		if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) s.resize(s.size() - suffix.size());
		return s;
	}

	const char* LoreKeys::BodyType(BodyKind kind)
	{
		switch (kind)
		{
		case BodyKind::Star: return "star";
		case BodyKind::Planet: return "planet";
		case BodyKind::DwarfPlanet: return "dwarf-planet";
		case BodyKind::Moon: return "moon";
		case BodyKind::Barycentre: return "barycentre";
		case BodyKind::Station: return "station";
		case BodyKind::Asteroid: return "asteroid";
		}
		return "body";
	}

	cs::String LoreKeys::System(const cs::String& systemName) { return "system-" + SystemSlug(systemName); }

	cs::String LoreKeys::Body(BodyKind kind, const cs::String& systemName, const cs::String& bodyName)
	{
		return cs::String(BodyType(kind)) + "-" + SystemSlug(systemName) + "-" + Slug(bodyName);
	}

	cs::String LoreKeys::Settlement(const cs::String& systemName, const cs::String& settlementName)
	{
		return "settlement-" + SystemSlug(systemName) + "-" + Slug(settlementName);
	}

	cs::String LoreKeys::Ship(const cs::String& systemName, const cs::String& shipName)
	{
		return "ship-" + SystemSlug(systemName) + "-" + Slug(shipName);
	}

	cs::String LoreKeys::Pilot(const cs::String& pilotName) { return "pilot-" + Slug(pilotName); }

	cs::String LoreKeys::Named(const cs::String& entity, const cs::String& name) { return entity + "/" + name; }
}
