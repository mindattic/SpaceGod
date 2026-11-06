// Hud: the gas works' markers, the keys at a giant and the protection contract under way (US-T146). Declarations in Hud.Panels.inl.

#include "UI/Hud.h"

#include "HarvestPlay.h"
#include "UnitySpaceApp.h"
#include "Views/HarvesterView.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Economy/GasHarvest.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void Hud::DrawHarvest()
	{
		auto world = app->World();
		auto works = world != nullptr ? world->Harvest : nullptr;
		if (works == nullptr || works->Operations.Count() == 0 || app->Catalog() == nullptr) return;
		double jd = app->Clock->Jd;
		auto player = app->PlayerHelioKm();
		const Color amber(1.0f, 0.72f, 0.3f, 0.9f), dim(1.0f, 0.72f, 0.3f, 0.45f);

		// The works of the giant nearest the player: each skimmer bracketed, its tanks, DOWN when a raid has it out.
		cs::Ref<HarvestOperation> here;
		double hereKm = cs::Double::PositiveInfinity;
		for (const auto& op : works->Operations)
		{
			double km = (player - app->Ephemeris()->Helio(op->Body)).Length();
			if (km < hereKm) { hereKm = km; here = op; }
		}
		if (here == nullptr || hereKm > works->GuardKm(here->Body) * 3) here = nullptr;
		auto ship = app->IsPiloting() ? app->Piloted.Ship : nullptr;
		auto active = ship != nullptr ? works->ActiveFor(ship) : nullptr;
		if (here != nullptr)
			for (int i = 0; i < here->Platforms.Count(); i++)
			{
				auto p = here->Platforms[i];
				Vector2 g;
				float depth = 0;
				if (!app->ToGui(Views::HarvesterView::PlatformScene(app, here, i), g, depth)) continue;
				if (g.x < 0 || g.x > Screen::width || g.y < 0 || g.y > Screen::height) continue;
				bool guarded = active != nullptr && active->Operation == here && active->Platform == i;
				Color c = p->Down(jd) ? Color(1.0f, 0.35f, 0.25f, 0.85f) : guarded ? Color(0.45f, 1.0f, 0.55f, 0.95f) : dim;
				DrawBrackets(Rect(g.x - 9, g.y - 9, 18, 18), c, 1.0f);
				double km = (works->PlatformHelio(here, i, jd) - player).Length();
				DrawLabel(cs::Cat(cs::Strings::ToUpperInvariant(p->Name), "  ", Km(km), p->Down(jd) ? "  DOWN (raided)" : cs::Cat("  tanks ", cs::Fmt(p->TankTonnes(), "N0"), " t"),
					guarded ? "  GUARDING" : ""), g + Vector2(12, 4), c, true);
			}

		// The contract under way.
		if (active != nullptr)
		{
			auto op = active->Operation;
			auto p = op->Platforms[active->Platform];
			auto giant = (*app->Catalog())[op->Body]->Name;
			bool onStation = active->GuardKmFromGiant <= works->GuardKm(op->Body);
			cs::String head = cs::Cat("PROTECTION CONTRACT  ·  ", p->Name, " for ", active->Patron != nullptr ? active->Patron->Name : cs::String("the works"), "  ·  ",
				cs::Fmt(active->Pay, "N0"), " cr");
			cs::String line1 = cs::Cat("orbit ", cs::Math::Min(active->Orbits, active->OrbitsDone(jd) + 1), " of ", active->Orbits, "  ·  ",
				cs::Fmt(cs::Math::Max(0.0, active->EndJd - jd) * 24, "N1"), " h left  ·  platform ", static_cast<int>(cs::Math::Max(0.0, active->PlatformHp) * 100), "%");
			cs::String line2;
			bool alarm = false;
			if (auto live = active->LiveWave())
			{
				alarm = true;
				line2 = live->Kind == HarvestThreat::BioMass ? cs::String("BIO MASS on the platform: burn it away")
					: cs::Cat(live->Kind == HarvestThreat::VoidRaiders ? "GHOST SHIPS" : live->Kind == HarvestThreat::Rival ? "ENEMY WARSHIPS" : "RAIDERS", " on the platform: beat them off");
			}
			else if (auto next = active->NextWave())
				line2 = cs::Cat("quiet  ·  something will come", next->Jd - jd < 1.0 / 24 ? " soon" : "");
			else line2 = "quiet  ·  no more trouble expected";
			cs::String line3 = onStation ? cs::Cat("ON STATION at ", giant) : cs::Cat("OFF STATION: get back to ", giant, " before they come, or the contract fails");
			auto a = GUIContent(head);
			auto b = GUIContent(line1);
			auto m = GUIContent(line2);
			auto d = GUIContent(line3);
			float w = Mathf::Max(Mathf::Max(text->CalcSize(a).x, small->CalcSize(b).x), Mathf::Max(small->CalcSize(m).x, small->CalcSize(d).x)) + 28;
			auto r = Panel(Rect(Screen::width - w - 16, 150, w, 86));
			GUI::contentColor = amber;
			GUI::Label(Rect(r.x + 14, r.y + 5, w, 20), a, text);
			GUI::contentColor = Color::white();
			GUI::Label(Rect(r.x + 14, r.y + 27, w, 18), b, small);
			GUI::contentColor = alarm ? UiStyle::Danger : Dim;
			GUI::Label(Rect(r.x + 14, r.y + 45, w, 18), m, small);
			GUI::contentColor = onStation ? UiStyle::Good : UiStyle::Danger;
			GUI::Label(Rect(r.x + 14, r.y + 63, w, 18), d, small);
			GUI::contentColor = Color::white();
			return;
		}

		// At a helm by a giant: what it offers, and the keys.
		if (ship == nullptr || here == nullptr || hereKm > works->GuardKm(here->Body)) return;
		auto offer = works->OpenAt(here);
		cs::Ref<HarvestOperation> nearOp;
		int nearIndex = -1;
		double nearKm = 0;
		bool raidable = works->Nearest(player, jd, nearOp, nearIndex, nearKm) && nearKm <= HarvestPlay::RaidKm;
		cs::String head = cs::Cat("GAS WORKS  ·  ", (*app->Catalog())[here->Body]->Name, "  ·  ", here->Owner != nullptr ? here->Owner->Name : cs::String("unowned"), "  ·  ",
			here->Working(jd), " of ", here->Platforms.Count(), " skimmers working");
		cs::String line1 = offer != nullptr
			? cs::Cat("Home  take the contract: ", offer->Title(), ", ", cs::Fmt(offer->Pay, "N0"), " cr")
			: cs::String("no protection contract posted here right now");
		cs::String line2 = raidable ? cs::Cat("Shift+Home  raid the ", nearOp->Platforms[nearIndex]->Name, "'s tanks (piracy)") : cs::String();
		auto a = GUIContent(head);
		auto b = GUIContent(line1);
		auto m = GUIContent(line2);
		float w = Mathf::Max(text->CalcSize(a).x, Mathf::Max(small->CalcSize(b).x, small->CalcSize(m).x)) + 28;
		auto r = Panel(Rect(Screen::width - w - 16, 150, w, raidable ? 68.0f : 50.0f));
		GUI::contentColor = amber;
		GUI::Label(Rect(r.x + 14, r.y + 5, w, 20), a, text);
		GUI::contentColor = offer != nullptr ? Color::white() : Dim;
		GUI::Label(Rect(r.x + 14, r.y + 27, w, 18), b, small);
		if (raidable)
		{
			GUI::contentColor = Color(1.0f, 0.5f, 0.4f);
			GUI::Label(Rect(r.x + 14, r.y + 45, w, 18), m, small);
		}
		GUI::contentColor = Color::white();
	}
}
