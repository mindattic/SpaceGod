// Hud: the Bio Mass's markers and contact warning (US-T125). Declarations in Hud.Panels.inl.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "Views/BioMassView.h"

#include "BodyCatalog.h"
#include "Combat/BioMass.h"
#include "SimClock.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void Hud::DrawBioMassMarkers()
	{
		auto world = app->World();
		auto bio = world ? world->BioMass : nullptr;
		if (bio == nullptr || bio->Globules.Count() == 0 || app->Catalog() == nullptr) return;
		double jd = app->Clock->Jd;
		auto player = app->PlayerHelioKm();
		float pulse = 0.65f + 0.35f * Mathf::Sin(Time::unscaledTime * 4.0f);
		for (const auto& g : bio->Globules)
		{
			if (!g->Alive() || g->Body < 0 || g->Body >= app->Catalog()->Count()) continue;
			double km = (bio->Helio(g, jd) - player).Length();
			Vector2 p;
			float depth = 0;
			if (!app->ToGui(Views::BioMassView::SceneOf(app, *g), p, depth)) continue;
			if (p.x < 0 || p.x > Screen::width || p.y < 0 || p.y > Screen::height) continue;
			// Red, and brighter while something is shooting it.
			double since = (jd - g->LastHitJd) * 86400.0;
			bool underFire = !cs::Double::IsNaN(g->LastHitJd) && since >= 0 && since < 3.0;
			Color c = Color(1.0f, underFire ? 0.45f : 0.2f, 0.2f, pulse);
			DrawBrackets(Rect(p.x - 12, p.y - 12, 24, 24), c, 1.5f);
			int hp = static_cast<int>(100 * g->Hp / cs::Math::Max(g->MaxHp(), 1.0));
			DrawLabel(cs::Cat("BIO MASS ", static_cast<int>(g->Mass / 1000), " kt  ", hp, "%  ", Km(km), underFire ? "  UNDER FIRE" : ""),
				p + Vector2(15, 6), c, true);
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
		auto warning = cs::Cat("BIO MASS CONTACT  ", Km(nearestKm), "  -  every gun fires on sight");
		auto size = label->CalcSize(GUIContent(warning));
		auto r = layout.Take(HudRegion::TopCentre, size.x, size.y);
		if (Event::current().type != EventType::Repaint) return;
		float pulse = 0.65f + 0.35f * Mathf::Sin(Time::unscaledTime * 4.0f);
		DrawLabel(warning, Vector2(r.x, r.y), Color(1.0f, 0.3f, 0.25f, pulse), false);
	}
}
