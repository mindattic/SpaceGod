// US-T239: the scan panel's tabs and keys, and the derived scanner readings (Lore/ScanTabs.h, docs/PROSE.md).

#include "Lore/ScanTabs.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "Lore/LoreKeys.h"
#include "OrbitalElements.h"

#include <cmath>

namespace SpaceGod::Core
{
	const cs::List<ScanTab>& ScanTabs::Of(ScanSubject s)
	{
		static const cs::List<ScanTab> star{ { Overview, "Overview" }, { "history", "History" }, { "notable", "Notable" } };
		static const cs::List<ScanTab> place{ { Overview, "Overview" }, { "industry", "Industry" }, { "culture", "Culture" }, { "history", "History" },
			{ "notable", "Notable" } };
		static const cs::List<ScanTab> ship{ { Overview, "Overview" }, { "cargo", "Cargo" }, { "crew", "Crew" }, { "history", "History" },
			{ "reputation", "Reputation" } };
		static const cs::List<ScanTab> warship{ { Overview, "Overview" }, { "orders", "Orders" }, { "crew", "Crew" }, { "history", "History" },
			{ "reputation", "Reputation" } };
		static const cs::List<ScanTab> pilot{ { Overview, "Overview" }, { "history", "History" }, { "reputation", "Reputation" } };
		switch (s)
		{
		case ScanSubject::Star: return star;
		case ScanSubject::World:
		case ScanSubject::Station:
		case ScanSubject::Settlement: return place;
		case ScanSubject::Ship: return ship;
		case ScanSubject::Warship: return warship;
		case ScanSubject::Pilot: return pilot;
		}
		return star;
	}

	bool ScanTabs::Has(ScanSubject s, const cs::String& tab)
	{
		for (const auto& t : Of(s))
			if (tab == t.Id) return true;
		return false;
	}

	ScanSubject ScanTabs::OfBody(BodyKind kind)
	{
		if (kind == BodyKind::Star) return ScanSubject::Star;
		if (kind == BodyKind::Station) return ScanSubject::Station;
		return ScanSubject::World;
	}

	const char* ScanTabs::Name(ScanSubject s)
	{
		switch (s)
		{
		case ScanSubject::Star: return "star";
		case ScanSubject::World: return "world";
		case ScanSubject::Station: return "station";
		case ScanSubject::Settlement: return "settlement";
		case ScanSubject::Ship: return "ship";
		case ScanSubject::Warship: return "warship";
		case ScanSubject::Pilot: return "pilot";
		}
		return "world";
	}

	cs::String ScanTabs::Key(const cs::String& entity, const cs::String& tab)
	{
		if (entity.empty()) return cs::String();
		if (tab.empty() || tab == Overview) return entity;
		return LoreKeys::Named(entity, tab);
	}

	cs::String ScanTabs::Placeholder(const cs::String& tab)
	{
		if (tab.empty() || tab == Overview) return cs::String();
		return "No record on file.";
	}

	double ScanFacts::SurfaceGravityG(const BodyDefinition& b)
	{
		if (b.RadiusKm <= 0) return 0;
		return b.Gm / (b.RadiusKm * b.RadiusKm) * 1000 / 9.80665;
	}

	double ScanFacts::EquilibriumK(const BodyCatalog& cat, int index)
	{
		if (index < 0 || index >= cat.Count()) return cs::Double::NaN;
		auto b = cat[index];
		if (b->IsStar()) return cs::Double::NaN;
		// Up the chain to the body that orbits a star (or a barycentre of stars) directly: its distance is the chain's.
		int at = index;
		for (int guard = 0; guard < 16; guard++)
		{
			auto body = cat[at];
			if (body->ParentIndex < 0) return cs::Double::NaN;
			auto parent = cat[body->ParentIndex];
			double light = 0;
			if (parent->IsStar()) light = parent->LuminositySolar;
			else if (parent->Kind == BodyKind::Barycentre)
				for (int child : cat.ChildrenOf(parent->Index))
					if (cat[child]->IsStar()) light += cat[child]->LuminositySolar;
			if (light > 0)
			{
				if (body->Elements == nullptr || body->Elements->AKm <= 0) return cs::Double::NaN;
				double au = body->Elements->AKm / Astro::AuKm;
				return 278.6 * std::pow(light, 0.25) / std::sqrt(au);
			}
			at = parent->Index;
		}
		return cs::Double::NaN;
	}

	const char* ScanFacts::Atmosphere(const BodyDefinition& b)
	{
		if (b.GasGiant) return "gas giant envelope";
		if (b.AtmosphereStrength >= 0.6f) return "thick";
		if (b.AtmosphereStrength >= 0.25f) return "moderate";
		if (b.AtmosphereStrength > 0) return "thin";
		return "none to speak of";
	}
}
