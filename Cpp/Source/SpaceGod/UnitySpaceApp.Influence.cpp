// US-T140: embody limits, Influence and squad orders as the player meets them (declarations in UnitySpaceApp.Influence.inl).

#include "UnitySpaceApp.h"

#include "Astro.h"
#include "Camera/StarTravel.h"
#include "Combat/CombatSpecs.h"
#include "Combat/SquadOrders.h"
#include "Coop/CoopPeer.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/HelmAbilities.h"
#include "Embodiment/Influence.h"
#include "Embodiment/ShipHelm.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "PracticeSpawn.h"
#include "Session/LocalSession.h"
#include "Ships.h"
#include "SimClock.h"
#include "UI/CheatMenu.h"
#include "UI/Hud.h"
#include "World.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// The test harnesses written before the limits (the smoke, the audit, the co-op smoke): they take seats in fights
		/// and big hulls freely, so they run with the dev waiver, on every peer alike (both are started with the flag).
		bool HarnessRun()
		{
			static const bool harness = FParse::Param(FCommandLine::Get(), TEXT("SpaceGodSmoke")) || FParse::Param(FCommandLine::Get(), TEXT("SpaceGodAudit"))
				|| FCString::Strifind(FCommandLine::Get(), TEXT("-SpaceGodCoopSmoke")) != nullptr;
			return harness;
		}

		/// The stand-down order's slot on the bar of <paramref name="ship"/>'s helm (1-based), 0 when it has none.
		int StandDownSlot(const cs::Ref<Ship>& ship, const cs::Ref<World>& world)
		{
			auto bar = HelmAbilities::Of(ship, world);
			for (int i = 0; i < bar.Count() && i < 10; i++)
				if (bar[i].Ability == HelmAbility::StandDown) return i + 1;
			return 0;
		}

		int scenarioCount = 0;
	}

	// ---------------------------------------------------------------- the frame

	void UnitySpaceApp::UpdateInfluence()
	{
		session->SetGateWorld(World());
		if (HarnessRun() && World() != nullptr) SpaceGodInfluence::Of(World())->Waived = true;
	}

	void UnitySpaceApp::NoteEmbodyRefusal(const cs::String& what, const EmbodyVerdict& verdict)
	{
		embodyRefusal = cs::Cat("Can't embody ", what, ": ", verdict.Reason);
		embodyRefusalRule = static_cast<int>(verdict.Rule);
		embodyRefusalNeed = verdict.Need;
		embodyRefusalHave = verdict.Have;
		embodyRefusalUntil = Time::unscaledTime + 6.0f;
		ShowToast(embodyRefusal);
	}

	cs::String UnitySpaceApp::OrderSquadStandDown()
	{
		if (!IsPiloting() || Piloted.Ship == nullptr) return "not at a helm";
		// The helm's own path, as Shift+n would go (the pilot loop's ShipHelm::Control), its toast through NoteHelmAbility.
		HelmAbilities::Use(Piloted.Ship, HelmAbility::StandDown, World());
		NoteHelmAbility();
		return HelmAbilities::Last().Toast;
	}

	// ---------------------------------------------------------------- the HUD

	void UnitySpaceApp::DrawInfluence()
	{
		if (ScreenCleared || World() == nullptr) return;
		if (influenceTitle == nullptr)
		{
			influenceBack = UI::Hud::Solid(Color(0.03f, 0.05f, 0.08f, 0.82f), Color(0.80f, 0.68f, 0.35f, 0.75f));
			influenceFill = UI::Hud::Solid(Color(0.95f, 0.78f, 0.35f, 0.95f), Color::clear());
			influenceTrack = UI::Hud::Solid(Color(1, 1, 1, 0.12f), Color::clear());
			influenceRefuseBack = UI::Hud::Solid(Color(0.16f, 0.03f, 0.03f, 0.88f), Color(1.0f, 0.35f, 0.30f, 0.85f));
			influencePanel = cs::New<GUIStyle>();
			influencePanel->normal.background = influenceBack;
			influencePanel->border = RectOffset(2, 2, 2, 2);
			influenceRefusePanel = cs::New<GUIStyle>(*influencePanel);
			influenceRefusePanel->normal.background = influenceRefuseBack;
			influenceTitle = cs::New<GUIStyle>();
			influenceTitle->fontSize = 13;
			influenceTitle->fontStyle = FontStyle::Bold;
			influenceTitle->normal.textColor = Color(0.98f, 0.84f, 0.45f, 1);
			influenceTitle->clipping = TextClipping::Clip;
			influenceSmall = cs::New<GUIStyle>(*influenceTitle);
			influenceSmall->fontSize = 11;
			influenceSmall->fontStyle = FontStyle::Normal;
			influenceSmall->normal.textColor = UI::Hud::Dim;
			influenceRefuse = cs::New<GUIStyle>(*influenceTitle);
			influenceRefuse->fontSize = 14;
			influenceRefuse->alignment = TextAnchor::MiddleCenter;
			influenceRefuse->wordWrap = true;
			influenceRefuse->normal.textColor = Color(1.0f, 0.72f, 0.66f, 1);
		}
		float now = Time::unscaledTime;
		auto inf = SpaceGodInfluence::Find(World());
		double xp = inf != nullptr ? inf->Xp : 0;
		int level = inf != nullptr ? inf->Level() : 1;
		double progress = inf != nullptr ? inf->Progress() : 0;
		if (inf != nullptr && inf->GainSeq != influenceGainSeq)
		{
			influenceGainSeq = inf->GainSeq;
			influenceGainUntil = now + 5.0f;
		}

		// The meter: under the navigator, top left. The SpaceGod's own, whatever body it holds.
		{
			float w = 268, h = 46;
			auto r = Rect(10, 50, w, h);
			// US-T208: a full-screen screen (spawn, manual, galaxy map) owns the top left: the badge would sit on its titles.
			bool fullScreen = ShowSpawn || ShowHelp || (ShowGalaxyMap && !Travel->Active);
			if (!fullScreen)
			{
			GUI::Box(r, GUIContent::none(), influencePanel);
			cs::String head = cs::Cat("INFLUENCE  ", level, "  \xC2\xB7  ", cs::Strings::ToUpperInvariant(SpaceGodInfluence::Title(level)),
				inf != nullptr && inf->Waived ? cs::String("  \xC2\xB7  LIMITS OFF (dev)") : cs::String());
			GUI::Label(Rect(r.x + 8, r.y + 3, w - 16, 18), head, influenceTitle);
			auto bar = Rect(r.x + 8, r.y + 23, w - 16, 5);
			GUI::DrawTexture(bar, influenceTrack);
			GUI::DrawTexture(Rect(bar.x, bar.y, bar.width * static_cast<float>(progress), bar.height), influenceFill);
			cs::String foot = level >= SpaceGodInfluence::MaxLevel ? cs::Cat(cs::Fmt(xp, "N0"), " XP  \xC2\xB7  the top")
				: cs::Cat(cs::Fmt(xp, "N0"), " / ", cs::Fmt(SpaceGodInfluence::XpFor(level + 1), "N0"), " XP to ", level + 1);
			if (now < influenceGainUntil && inf != nullptr)
				foot = cs::Cat("+", cs::Fmt(inf->LastGainXp, "N0"), " XP  \xC2\xB7  ", inf->LastGain);
			GUI::Label(Rect(r.x + 8, r.y + 29, w - 16, 15), foot, influenceSmall);
			}
		}

		// The squad a band captain held is closing with, and the key that orders it to stand down.
		if (IsPiloting() && Piloted.Ship != nullptr && SquadOrders::SquadOf(World(), Piloted.Ship).Count() > 0)
		{
			cs::String state, why;
			bool can = SquadOrders::CanOrder(World(), Piloted.Ship, World()->Jd, state, why);
			int slot = StandDownSlot(Piloted.Ship, World());
			cs::String key = slot > 0 ? cs::Cat("Shift+", slot == 10 ? 0 : slot) : cs::String("Shift+n");
			cs::String line = cs::Cat("YOUR SQUAD  \xC2\xB7  ", cs::Strings::ToUpperInvariant(state), can ? cs::Cat("  \xC2\xB7  ", key, " orders them to stand down") : cs::String());
			float w = 640, h = 26;
			auto r = Rect((Screen::width - w) * 0.5f, 158, w, h);
			GUI::Box(r, GUIContent::none(), influencePanel);
			auto old = influenceTitle->alignment;
			influenceTitle->alignment = TextAnchor::MiddleCenter;
			GUI::Label(Rect(r.x + 6, r.y + 3, w - 12, h - 6), line, influenceTitle);
			influenceTitle->alignment = old;
		}

		// The refusal, while fresh: what was refused, and why.
		if (now < embodyRefusalUntil && !embodyRefusal.empty())
		{
			float w = 780, h = 58;
			auto r = Rect((Screen::width - w) * 0.5f, 192, w, h);
			GUI::Box(r, GUIContent::none(), influenceRefusePanel);
			cs::String tag = embodyRefusalRule == static_cast<int>(EmbodyRefusal::InBattle) ? cs::String("EMBODY REFUSED  \xC2\xB7  IN BATTLE")
				: embodyRefusalRule == static_cast<int>(EmbodyRefusal::Influence) ? cs::Cat("EMBODY REFUSED  \xC2\xB7  INFLUENCE ", embodyRefusalHave, " OF ", embodyRefusalNeed)
				: cs::String("EMBODY REFUSED");
			auto old = influenceTitle->alignment;
			influenceTitle->alignment = TextAnchor::MiddleCenter;
			GUI::Label(Rect(r.x + 8, r.y + 4, w - 16, 18), tag, influenceTitle);
			influenceTitle->alignment = old;
			GUI::Label(Rect(r.x + 10, r.y + 22, w - 20, h - 26), embodyRefusal, influenceRefuse);
		}
	}

	// ---------------------------------------------------------------- the stand-down scenario

	bool UnitySpaceApp::StartStandDownScenario(double seconds)
	{
		if (!CheatBlocker().empty())
		{
			ShowToast(cs::Cat("Stand-down scenario: ", CheatBlocker()));
			return false;
		}
		int frame;
		DVec3 base, forward, up;
		CheatPlayerFrame(frame, base, forward, up);
		auto right = DVec3::Cross(forward, up);
		right = right.LengthSquared() > 1e-12 ? right.Normalized() : DVec3::UnitY;
		double jd = Clock->Jd;
		auto frameHelio = Ephemeris()->Helio(frame);
		int n = ++scenarioCount;
		// Laid out in hull lengths (each class's own), so the picture is the same at realistic and game scale.
		auto place = [&](ShipClass cls, const cs::String& name, const cs::Ref<Faction>& faction, double ahead, double side, double rise, DVec3 facing)
		{
			double unit = cs::Math::Max(1.0, CombatSpec::Of(cls)->LengthKm * 2.5);
			auto local = forward * (ahead * unit) + right * (side * unit) + up * (rise * unit);
			auto at = PracticeSpawn::Clear(frameHelio + base + local, facing, CombatSpec::Of(cls)->LengthKm, practiceObstacles) - frameHelio;
			auto ship = Fleet()->SpawnAt(name, cls, faction, frame, at, facing, jd);
			ship->Ports = World()->FreeShipPorts();
			practiceObstacles.Add(SpawnObstacle(frameHelio + at, ship->LengthKm() * 0.5));
			return ship;
		};
		CollectPracticeObstacles(nullptr);
		auto freighter = place(ShipClass::Freighter, cs::Cat("Patient Ilse ", n), World()->Independents, 9, 0.6, 0.2, forward);
		auto leader = place(ShipClass::Corvette, cs::Cat("Raider Corvette ", n), World()->Pirates, 2.5, -1.2, 0.3, forward);
		cs::List<cs::Ref<Ship>> band { leader };
		for (int i = 0; i < 2; i++)
		{
			// Of the corvette's band (its Band id), each holding its own spot a few km wide of it: not in formation as its escorts,
			// which share their leader's place out of a fight and would break up on the hull you fly the moment you take it.
			auto f = place(ShipClass::Fighter, cs::Cat("Raider Fighter ", n, i == 0 ? "a" : "b"), World()->Pirates, 2.5, -1.2 + (i == 0 ? 4.0 : -4.0), 1.5, forward);
			leader->BandId = leader->Id;
			f->BandId = leader->Id;
			band.Add(f);
		}
		SquadOrders::Begin(World(), leader, freighter, seconds, jd);
		// You at the band captain's helm: a dev path (the scenario's own hull), past the embody gate as V's spawn is.
		RememberCommand();
		ReleaseSeat();
		if (IsPiloting()) ExitPiloting();
		EnterPiloting(Target::OfShip(leader));
		int slot = StandDownSlot(leader, World());
		ShowToast(cs::Cat("STAND-DOWN SCENARIO: you are the captain of the ", leader->Name, "; your band is closing on the unescorted ", freighter->Name,
			", contact in ", cs::Fmt(seconds, "0"), " s. ", slot > 0 ? cs::Cat("Shift+", slot == 10 ? 0 : slot) : cs::String("Shift+n"),
			" orders the squad to stand down: they obey, hesitate, argue or mutiny, by your Influence and the captain's hold on them"));
		return true;
	}

	// ---------------------------------------------------------------- the dev menu's entries (US-T126)

	void UnitySpaceApp::RegisterInfluenceCheats()
	{
		using UI::CheatEntry;
		using UI::CheatRegistry;
		CheatRegistry::DeclareCategory("SpaceGod", 45, "Influence, the embody limits and squad orders (US-T140)");
		auto blocker = [](UnitySpaceApp& a) { return a.CheatBlocker(); };
		auto level = [](UnitySpaceApp& a)
		{
			auto inf = SpaceGodInfluence::Find(a.World());
			int l = inf != nullptr ? inf->Level() : 1;
			return cs::Cat("LEVEL ", l, "  ", cs::Strings::ToUpperInvariant(SpaceGodInfluence::Title(l)), "  ", cs::Fmt(inf != nullptr ? inf->Xp : 0.0, "N0"), " XP");
		};
		auto grant = [](UnitySpaceApp& a, double xp)
		{
			bool up = SpaceGodInfluence::Grant(a.World(), InfluenceSource::Dev, xp, "dev grant");
			auto inf = SpaceGodInfluence::Of(a.World());
			a.ShowToast(cs::Cat("Influence +", cs::Fmt(xp, "N0"), " XP (dev)", up ? cs::Cat(": level ", inf->Level(), ", ", SpaceGodInfluence::Title(inf->Level())) : cs::String()));
		};
		CheatRegistry::Add("SpaceGod", CheatEntry::Action("Grant Influence +100 XP", "testing: as if the SpaceGod had intervened", [grant](UnitySpaceApp& a) { grant(a, 100); })
			.WithState(level).WithBlocker(blocker).StaysOpen());
		CheatRegistry::Add("SpaceGod", CheatEntry::Action("Grant Influence: next level", "testing: straight to the next level", [grant](UnitySpaceApp& a)
		{
			auto inf = SpaceGodInfluence::Of(a.World());
			int l = inf->Level();
			if (l >= SpaceGodInfluence::MaxLevel) { a.ShowToast("Influence is at the top already"); return; }
			grant(a, SpaceGodInfluence::XpFor(l + 1) - inf->Xp);
		}).WithState(level).WithBlocker(blocker).StaysOpen());
		CheatRegistry::Add("SpaceGod", CheatEntry::Action("Influence to the top", "testing: level 10, everything embodiable", [grant](UnitySpaceApp& a)
		{
			auto inf = SpaceGodInfluence::Of(a.World());
			grant(a, cs::Math::Max(0.0, SpaceGodInfluence::XpFor(SpaceGodInfluence::MaxLevel) - inf->Xp));
		}).WithState(level).WithBlocker(blocker).StaysOpen());
		CheatRegistry::Add("SpaceGod", CheatEntry::Action("Reset Influence", "testing: back to level 1, Whisper", [](UnitySpaceApp& a)
		{
			auto inf = SpaceGodInfluence::Of(a.World());
			inf->Xp = 0;
			inf->Interventions = inf->Missions = inf->CareersSteered = 0;
			a.ShowToast("Influence reset to level 1 (dev)");
		}).WithState(level).WithBlocker(blocker).StaysOpen());
		CheatRegistry::Add("SpaceGod", CheatEntry::Action("Embody limits", "dev: lift the in-battle and Influence limits (never saved)", [](UnitySpaceApp& a)
		{
			auto inf = SpaceGodInfluence::Of(a.World());
			inf->Waived = !inf->Waived;
			a.ShowToast(inf->Waived ? "Embody limits OFF (dev): anything, in battle or not, whatever the Influence" : "Embody limits ON");
		}).WithState([](UnitySpaceApp& a)
		{
			auto inf = SpaceGodInfluence::Find(a.World());
			return cs::String(inf != nullptr && inf->Waived ? "OFF (dev)" : "ON");
		}).WithBlocker(blocker).StaysOpen());
		CheatRegistry::Add("SpaceGod", CheatEntry::Action("Stand-down scenario", "a pirate band closing on a lone freighter, you at its captain's helm", [](UnitySpaceApp& a)
		{
			a.StartStandDownScenario();
		}).WithKeys("sg.StandDown").WithBlocker(blocker));
		CheatRegistry::Add("SpaceGod", CheatEntry::Action("Order the squad to stand down", "at a band captain's helm (the bar's squad order)", [](UnitySpaceApp& a)
		{
			a.OrderSquadStandDown();
		}).WithBlocker([](UnitySpaceApp& a)
		{
			if (!a.IsPiloting() || a.Piloted.Ship == nullptr) return cs::String("take a band captain's helm first");
			cs::String state, why;
			return SquadOrders::CanOrder(a.World(), a.Piloted.Ship, a.World()->Jd, state, why) ? cs::String() : why;
		}));
	}

	namespace
	{
		UI::CheatRegistration GInfluenceCheats(&UnitySpaceApp::RegisterInfluenceCheats);
	}
}
