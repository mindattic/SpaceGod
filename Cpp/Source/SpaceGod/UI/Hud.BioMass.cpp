// Hud: the Bio Mass's markers and contact warning (US-T125); its blooms and tumors named, and the alerts for growths on
// the player's own hull (US-T192). Declarations in Hud.Panels.inl.

#include "UI/Hud.h"

#include "UI/UiStyle.h"
#include "UnitySpaceApp.h"
#include "Views/BioMassView.h"

#include "BodyCatalog.h"
#include "Combat/BioMass.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

#include <map>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		/// What a growth is on, for its marker: a hull's name, a station's or asteroid's, or nothing when it is free.
		cs::String HostLabel(UnitySpaceApp* app, const BioGlobule& g)
		{
			if (g.Host == BioHost::Ship)
			{
				auto ship = Views::BioMassView::HostShip(app, g);
				return ship != nullptr ? ship->Name : cs::String();
			}
			if (g.Host == BioHost::Body && g.HostId >= 0 && g.HostId < app->Catalog()->Count()) return (*app->Catalog())[g.HostId]->Name;
			return cs::String();
		}

		Color WithAlpha(Color c, float a) { return Color(c.r, c.g, c.b, a); }
	}

	void Hud::DrawBioMassMarkers()
	{
		auto world = app->World();
		auto bio = world ? world->BioMass : nullptr;
		if (bio == nullptr || bio->Globules.Count() == 0 || app->Catalog() == nullptr) return;
		double jd = app->Clock->Jd;
		auto player = app->PlayerHelioKm();
		float pulse = 0.65f + 0.35f * Mathf::Sin(Time::unscaledTime * 4.0f);
		auto you = world->PilotedShip;

		// US-T192: blooms come in clusters: the ones latched on the same host share one label (with how many), so a hull
		// with five on it reads as one line, not five over each other.
		std::map<std::pair<int, int>, int> latched;
		for (const auto& g : bio->Globules)
			if (g->Alive() && g->Form == BioForm::Bloom && g->Attached()) latched[{ static_cast<int>(g->Host), g->HostId }]++;
		std::map<std::pair<int, int>, bool> labelled;

		for (const auto& g : bio->Globules)
		{
			if (!g->Alive() || g->Body < 0 || g->Body >= app->Catalog()->Count()) continue;
			double km = (bio->Helio(g, jd) - player).Length();
			Vector2 p;
			float depth = 0;
			if (!app->ToGui(Views::BioMassView::SceneOf(app, *g), p, depth)) continue;
			if (p.x < 0 || p.x > Screen::width || p.y < 0 || p.y > Screen::height) continue;
			// Red (a bloom amber), and brighter while something is shooting it.
			double since = (jd - g->LastHitJd) * 86400.0;
			bool underFire = !cs::Double::IsNaN(g->LastHitJd) && since >= 0 && since < 3.0;
			bool bloom = g->Form == BioForm::Bloom, tumor = g->Form == BioForm::Tumor;
			Color base = bloom ? UiStyle::Warning : UiStyle::Danger;
			Color c = WithAlpha(underFire ? Color(1.0f, 0.55f, 0.45f) : base, pulse);
			// The size of the bracket says the size of the thing: a bloom's small, a tumor's large.
			float half = bloom ? 7.0f : tumor ? 18.0f : 12.0f;
			DrawBrackets(Rect(p.x - half, p.y - half, half * 2, half * 2), c, tumor ? 2.0f : 1.5f);

			int hp = static_cast<int>(100 * g->Hp / cs::Math::Max(g->MaxHp(), 1.0));
			cs::String on = HostLabel(app, *g);
			bool onYou = you != nullptr && g->Host == BioHost::Ship && g->HostId == you->Id;
			cs::String where = !g->Attached() || on.empty() ? Km(km) : onYou ? cs::String("on YOUR HULL") : cs::Cat("on ", on);
			cs::String fire = underFire ? cs::String("  UNDER FIRE") : cs::String();
			cs::String markerText;
			if (bloom)
			{
				if (g->Attached())
				{
					auto key = std::make_pair(static_cast<int>(g->Host), g->HostId);
					if (labelled[key]) continue; // its cluster is named already
					labelled[key] = true;
					int n = latched[key];
					markerText = n > 1 ? cs::Cat("BLOOMS x", n, " latched ", where, fire) : cs::Cat("BLOOM ", hp, "% latched ", where, fire);
				}
				else markerText = cs::Cat("BLOOM ", hp, "%", UiStyle::Glyph::Separator, where, fire);
			}
			else if (tumor)
				markerText = cs::Cat("TUMOR ", static_cast<int>(g->Mass / 1000), " kt  ", hp, "%", UiStyle::Glyph::Separator,
					g->Attached() ? cs::Cat("rooted ", where) : cs::Cat("seed adrift  ", where), fire);
			else
				markerText = cs::Cat("BIO MASS ", static_cast<int>(g->Mass / 1000), " kt  ", hp, "%  ", where, fire);
			DrawLabel(markerText, p + Vector2(half + 3, 6), c, true);
		}
	}

	void Hud::DrawBioMassWarning()
	{
		// US-T154: a slot of its own in the TopCentre stack, under the news, never behind it. Taken on every event (not
		// only Repaint), so whatever stacks under it is placed alike in the Layout pass and the Repaint.
		auto world = app->World();
		auto bio = world ? world->BioMass : nullptr;
		if (bio == nullptr || bio->Globules.Count() == 0 || app->Catalog() == nullptr) return;
		double jd = app->Clock->Jd;
		auto player = app->PlayerHelioKm();
		double nearestKm = cs::Double::PositiveInfinity;
		for (const auto& g : bio->Globules)
		{
			if (!g->Alive() || g->Body < 0 || g->Body >= app->Catalog()->Count()) continue;
			nearestKm = cs::Math::Min(nearestKm, (bio->Helio(g, jd) - player).Length());
		}
		if (!(nearestKm < BioMassWarnKm)) return;
		float pulse = 0.65f + 0.35f * Mathf::Sin(Time::unscaledTime * 4.0f);
		auto line = [&](const cs::String& lineText, Color color, float alpha)
		{
			auto size = label->CalcSize(GUIContent(lineText));
			auto r = layout.Take(HudRegion::TopCentre, size.x, size.y);
			if (Event::current().type == EventType::Repaint) DrawLabel(lineText, Vector2(r.x, r.y), WithAlpha(color, alpha), false);
		};
		line(cs::Cat("BIO MASS CONTACT  ", Km(nearestKm), "  -  every gun fires on sight"), UiStyle::Danger, pulse);

		// US-T192: growths on the player's own hull, under the contact line: what is on it and what to do.
		auto you = world->PilotedShip;
		if (you == nullptr || you->Destroyed) return;
		int tumors = bio->OnShip(you->Id, BioForm::Tumor), blooms = bio->OnShip(you->Id, BioForm::Bloom);
		// A tumor's beat: the line flashes faster than the contact's.
		float beat = 0.55f + 0.45f * Mathf::Abs(Mathf::Sin(Time::unscaledTime * 6.0f));
		if (tumors > 0)
			line(cs::Cat("TUMOR ON YOUR HULL", tumors > 1 ? cs::Cat(" x", tumors) : cs::String(),
				"  -  it drains the hull as it grows: burn it off with your guns or an escort's"), UiStyle::Danger, beat);
		if (blooms > 0)
			line(cs::Cat("BLOOMS LATCHED ON  x", blooms, "  -  biting the hull: your guns and any escort's shoot them off"), UiStyle::Warning, pulse);
	}
}
