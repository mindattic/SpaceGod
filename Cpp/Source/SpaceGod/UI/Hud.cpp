// Hud: the C++ port of Unity.SpaceGod's Runtime/UI/Hud.cs lines 1-1222 and 3746-3868 (fields, Draw, saves, world
// overlay, flight markers, the chase, aimed combat, hyperlane rides; formatting; styles). Declarations in Hud.Main.inl.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "ActiveProfile.h"
#include "UI/GalaxyMapScreen.h"
#include "UI/HelpScreen.h"
#include "UI/Icons.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Views/InterstellarView.h"
#include "Views/StructureViews.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "RenderLod.h"
#include "ShipLod.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Arsenal.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/Dogfighting.h"
#include "Embodiment/CraftPower.h"
#include "Embodiment/SelfDestruct.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/FireCones.h"
#include "Combat/FlightModels.h"
#include "Combat/Gunnery.h"
#include "Combat/HaulerFlight.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/Pursuit.h"
#include "Combat/Radiators.h"
#include "Combat/WeakPatches.h"
#include "Economy/CargoHold.h"
#include "Infrastructure/Infrastructure.h"
#include "Information/LightSpeed.h"
#include "Information/Ranges.h" // US-T156
#include "Persistence/SaveGame.h"
#include "Sensors/Sensors.h"
#include "Sensors/SensorPosture.h"
#include "Settings/GameSetting.h"
#include "Systems/VoidSites.h"
#include "Systems/CruiseDrive.h"
#include "Systems/DeepSpace.h"
#include "Systems/FtlNavigation.h"
#include "Systems/FtlRules.h"
#include "Systems/Galaxy.h"
#include "Systems/HelmFtl.h"
#include "Systems/VoidRaiders.h"
#include "Shared/Profiles/ProfileStore.h"

#include <algorithm>
#include <tuple>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	cs::Ref<ContactBook> Hud::Picture() const
	{
		return app->IsPiloting() && app->Piloted.Ship != nullptr ? (app->World()->Sensors != nullptr ? app->World()->Sensors->Player : nullptr) : nullptr;
	}

	Color Hud::LostColor(const cs::Ref<Contact>& contact)
	{
		auto me = app->Piloted.Ship != nullptr ? app->Piloted.Ship->Faction : nullptr;
		auto them = contact->Ship != nullptr ? contact->Ship->Faction : nullptr;
		if (contact->Level != ContactLevel::Identified || me == nullptr || them == nullptr) return LostNeutral;
		if (them == me) return LostAlly;
		return app->World()->Diplomacy()->Hostile(me, them) ? LostEnemy : LostNeutral;
	}

	bool Hud::ContactScene(const cs::Ref<Ship>& ship, const cs::Ref<Contact>& contact, DVec3& scene)
	{
		if (contact == nullptr || !contact->Cold())
		{
			scene = app->ShipScene(ship);
			if (contact != nullptr) lastSeenScene[ship] = scene;
			return true;
		}
		return lastSeenScene.TryGetValue(ship, scene);
	}

	Hud::Hud(UnitySpaceApp* app) : app(app) {}

	Hud::~Hud() = default;

	void Hud::Draw()
	{
		EnsureStyles();
		if (Event::current().type == EventType::Layout)
		{
			app->UiRects.Clear();
			app->ShipNameRects.Clear();
		}
		// US-T152..T155: every docked panel takes its place from the layout's regions (HudLayout.h), afresh each event.
		layout.Begin(SafeLeft(), SafeRight(), static_cast<float>(Screen::width), static_cast<float>(Screen::height));

		// F11 (U.SPC-T496): a clear screen, nothing on it but the reticle in use.
		if (app->ScreenCleared)
		{
			DrawReticleOnly();
			return;
		}

		// Spawn & Pilot is its own full screen, not an overlay panel -- everything else waits.
		if (app->ShowSpawn) { DrawSpawn(); return; }

		// So is Help (U.SPC-T494): every key and control, by group, over the whole view.
		if (app->ShowHelp)
		{
			if (!helpScreen) helpScreen = cs::New<HelpScreen>();
			auto whole = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
			if (!app->UiRects.Contains(whole)) app->UiRects.Add(whole); // the world under it takes no clicks
			if (helpScreen->Draw("Esc, H or F1 to close")) app->ShowHelp = false;
			DrawToast();
			return;
		}

		// So is the galaxy map (U.SPC-T103), between the stars aside: a trip under way has the screen.
		if (app->ShowGalaxyMap && !app->Travel->Active)
		{
			if (!galaxyMap) galaxyMap = cs::New<GalaxyMapScreen>(app);
			if (!galaxyMapOpen) galaxyMap->Opened();
			galaxyMapOpen = true;
			galaxyMap->Draw();
			DrawToast();
			return;
		}
		galaxyMapOpen = false;

		LayoutNews(); // TopCentre's first slot, so every alert and banner stacks under the news

		if (app->Travel->Active) DrawTravel();
		else if (app->InFtlCruise()) DrawFtlCruise();
		else
		{
			DrawWorldOverlay();
			DrawBioMassWarning(); // US-T125, under the news (US-T154)
			// A plotted course over the gates: the way and the next leg (U.SPC-T516).
			auto courseLine = app->CourseLine();
			if (!courseLine.empty()) DrawBanner(courseLine);
			if (app->CourseAutopilotOffered())
			{
				auto autopilot = layout.Take(HudRegion::TopCentre, 240.0f, 26.0f);
				if (GUI::Button(autopilot, "AUTOPILOT: CREW FLIES THE COURSE", button)) app->EngageCourseAutopilot();
			}
		}
		DrawLaneEffects();
		DrawSelfDestruct(); // US-T97: shown with the HUD off too
		if (!app->ShowHud)
		{
			if (app->ShowSaves) DrawSaves();
			return;
		}
		DrawFlightMarkers();
		DrawOffscreenArrows(); // US-T128: targets off the screen, at its edge
		DrawCombatHud();
		DrawReactor(); // US-T94: BottomRight's first slot
		DrawSensors(); // US-T93: stacked over the reactor
		DrawTopBar();
		if (app->ShowNews) DrawNews();
		else DrawTicker();
		DrawNavigator();
		// TopRight before the selection card, which takes what the right-hand column has left between its stacks.
		if (!app->ShowMarket) DrawOpportunityBoard();
		if (app->ShowMarket) DrawMarket();
		DrawHunters();
		DrawSelection();
		DrawDrawerHandle(); // US-T200: the status band's bottom-left slot, before the status line takes the rest
		DrawStatus();
		DrawSalvage();
		DrawBoarding();
		DrawHail();
		DrawLaneRide();
		DrawFtl();
		DrawSeat();
		DrawHelmAbilities(); // US-T106
		DrawCourseMarker();
		DrawInterdiction();
		if (app->ShowCargo) DrawCargo();
		if (app->ShowOutfit) DrawOutfit();
		if (app->ShowBounties) DrawBounties();
		if (app->ShowContracts) DrawContracts();
		if (app->ShowFleet) DrawFleet();
		if (app->ShowTrade) DrawTradeDesk();
		if (app->ShowPicker) DrawPicker();
		DrawDrawer(); // US-T200: pulled up over the docked panels
		if (app->ShowSaves) DrawSaves();
		DrawToast();
	}

	// ---------------------------------------------------------------- save and load (U.SPC-T67)

	void Hud::DrawSaves()
	{
		auto slots = app->Saves;
		if (slots == nullptr) return;
		if (saveNames == nullptr || Time::unscaledTime - savesReadAt > 1.0f)
		{
			saveNames = cs::New<cs::List<cs::String>>(slots->AllSlots());
			saveHeaders = cs::NewArray<cs::Ref<SaveHeader>>(saveNames->Count());
			for (int i = 0; i < saveNames->Count(); i++) saveHeaders[i] = slots->Header((*saveNames)[i]);
			nextManualSlot = slots->NewManual();
			savesReadAt = Time::unscaledTime;
		}
		constexpr float w = 760, rowH = 24;
		float listH = Mathf::Min(saveNames->Count(), 14) * rowH;
		float h = 116 + listH;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, Mathf::Min(170.0f, Screen::height - h - 20), w, h));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 60, 22), cs::Cat("SAVE & LOAD  ·  ", cs::Strings::ToUpperInvariant(ActiveProfile::Current()->Name)), title);
		if (GUI::Button(Rect(r.xMax() - 34, r.y + 6, 24, 22), "X", button)) app->ShowSaves = false;
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		cs::String blocked = app->SaveBlocker();
		GUI::Label(Rect(r.x + 12, r.y + 30, w - 24, 18), !blocked.empty() ? blocked :
			cs::Cat("F5 quicksave   F8 quickload   F6 close   ·   autosaves every ", cs::Fmt(UnitySpaceApp::AutosaveIntervalS / 60.0f, "0"), " minutes and on quit"), small);
		GUI::contentColor = old;

		bool enabled = GUI::enabled;
		GUI::enabled = enabled && blocked.empty();
		cs::String next = nextManualSlot;
		if (GUI::Button(Rect(r.x + 8, r.y + 54, 160, 26), "New Save", buttonActive))
		{
			app->RequestSave(next, UnitySpaceApp::SlotName(next));
			savesReadAt = -10.0f;
			deleteArmed.clear();
		}
		GUI::enabled = enabled;

		auto view = Rect(r.x + 8, r.y + 88, w - 16, listH + 4);
		auto content = Rect(0, 0, view.width - (saveNames->Count() > 14 ? 18 : 0), saveNames->Count() * rowH);
		savesScroll = GUI::BeginScrollView(view, savesScroll, content);
		for (int i = 0; i < saveNames->Count(); i++)
		{
			cs::String slot = (*saveNames)[i];
			auto hdr = saveHeaders[i];
			auto row0 = Rect(0, i * rowH, content.width, rowH - 2);
			GUI::Box(row0, GUIContent::none(), row);
			GUI::Label(Rect(row0.x + 6, row0.y + 2, 150, 18), UnitySpaceApp::SaveTitle(slot, hdr), text);
			GUI::contentColor = hdr == nullptr ? Dim : Color::white();
			cs::String line = hdr == nullptr ? cs::String("empty") : cs::Cat(UnitySpaceApp::DescribeSave(hdr), "   ·   saved ", hdr->SavedUtc.ToLocalTime().ToString("MMM d HH:mm"));
			GUI::Label(Rect(row0.x + 160, row0.y + 3, row0.width - 400, 18), line, small);
			GUI::contentColor = old;
			if (!blocked.empty()) continue;
			if (!SaveSlots::IsAutosave(slot) && GUI::Button(Rect(row0.xMax() - 228, row0.y + 1, 70, 20), "Save", button))
			{
				app->RequestSave(slot, hdr != nullptr ? hdr->Label : cs::String());
				savesReadAt = -10.0f;
				deleteArmed.clear();
			}
			if (hdr != nullptr && GUI::Button(Rect(row0.xMax() - 152, row0.y + 1, 70, 20), "Load", button))
			{
				app->RequestLoad(slot);
				deleteArmed.clear();
			}
			if (hdr != nullptr && GUI::Button(Rect(row0.xMax() - 76, row0.y + 1, 72, 20), deleteArmed == slot ? "Sure?" : "Delete", deleteArmed == slot ? buttonActive : button))
			{
				if (deleteArmed == slot)
				{
					slots->Delete(slot);
					deleteArmed.clear();
					savesReadAt = -10.0f;
				}
				else deleteArmed = slot;
			}
		}
		GUI::EndScrollView();
	}

	// ---------------------------------------------------------------- world overlay

	void Hud::DrawTravel()
	{
		auto travel = app->Travel;
		if (Event::current().type == EventType::Repaint)
		{
			DrawGateLight(travel->GateGlow(), GateGlowColor, 0.3f);
			DrawWarp();
			DrawGateLight(travel->GateFlash() * travel->GateFlash(), GateFlashColor, 0.9f);
		}
		DrawInterstellarLabels();
		if (travel->Banner.empty()) return;
		DrawBanner(travel->Banner);
	}

	void Hud::DrawFtlCruise()
	{
		auto c = app->FtlCruise;
		if (c == nullptr) return;
		double jd = app->Clock->Jd;
		if (Event::current().type == EventType::Repaint)
		{
			double peak = c->Drive != nullptr ? c->Drive->LyPerDay() : 1;
			float strength = Mathf::Clamp01(static_cast<float>(c->Profile.SpeedAt(jd - c->LegJd) / cs::Math::Max(peak, 1e-12)));
			auto centre = Vector2(Screen::width * 0.5f, Screen::height * 0.5f);
			Vector2 ahead;
			if (DirectionToGui(c->Direction(), ahead)) centre = ahead;
			DrawStreaks(centre, 0.25f + 0.75f * strength, WarpLineCount, WarpColor, 500, 320.0f);
		}
		DrawInterstellarLabels();
		double ly = c->LyToGo(jd);
		double speedC = c->SpeedKmS(jd) / 299792.458;
		if (c->Crossing)
		{
			// Pulled out of FTL by a star's well or a gravity projector (U.SPC-T418), crossing it under the ordinary drive.
			DrawBanner(cs::Cat("PULLED OUT OF FTL BY ", cs::Strings::ToUpperInvariant(!c->InterdictedBy.empty() ? c->InterdictedBy : cs::String("A GRAVITY WELL")), "   ·   CROSSING UNDER NORMAL DRIVE   ·   BOUND FOR ",
				cs::Strings::ToUpperInvariant(c->ToName), ", ", cs::Fmt(ly, "0.00"), " LY TO GO"));
			return;
		}
		int left = c->JumpsLeft(jd);
		if (c->AtVoidStop(jd))
		{
			// A stop in the void between jumps (US-T86): the drive cooling, then spooling, for the next.
			bool cooling = c->Cooling(jd);
			cs::String stage = cooling
				? cs::Cat("DRIVE COOLING  ·  SPOOL STARTS IN ", Mathf::CeilToInt(static_cast<float>(app->FtlSecondsTo(c->ChargeJd))), " S")
				: cs::Cat("DRIVE SPOOLING ", Percent(c->ChargeFraction(jd)), "  ·  ENGAGES IN ", Mathf::CeilToInt(static_cast<float>(app->FtlSecondsTo(c->EngageJd()))), " S");
			DrawBanner(cs::Cat("VOID STOP ", c->Stops, "   ·   ", stage, "   ·   NEXT JUMP ", c->Stops + 1, " OF ", c->Stops + left, "   ·   BOUND FOR ",
				cs::Strings::ToUpperInvariant(c->ToName), ", ", cs::Fmt(ly, "0.00"), " LY TO GO   ·   ", cs::Strings::ToUpperInvariant(app->Clock->Warp().Label), "  (, .)"));
			if (c->Piloted) DrawVoidStopSensors(c, jd);
			return;
		}
		cs::String jump = c->Stops + left > 1 ? cs::Cat("JUMP ", c->Stops + 1, " OF ", c->Stops + left, "   ·   ") : cs::String();
		// The trip compressed on the sim clock (U.SPC-T420): when it drops out, by the calendar and in real seconds at this pace.
		DrawBanner(cs::Cat("FTL  ·  ", jump, "BOUND FOR ", cs::Strings::ToUpperInvariant(c->ToName), "   ·   ", cs::Fmt(ly, "0.00"), " LY TO GO   ·   ",
			cs::Fmt(speedC, "N0"), " C   ·   ETA ", Astro::ToDateTime(c->ArriveJd()).ToString("yyyy-MM-dd"),
			"   ·   ", c->Stopover ? "VOID STOP" : "DROPPING OUT", " IN ", Mathf::CeilToInt(static_cast<float>(app->FtlSecondsLeft())), " S AT ", cs::Strings::ToUpperInvariant(app->Clock->Warp().Label), "  (, .)"));
	}

	void Hud::DrawVoidStopSensors(const cs::Ref<Cruise>& c, double jd)
	{
		// The hull's sensors at a void stop (US-T124): its posture, and any ghost closing on it.
		int stage = app->Galaxy != nullptr && app->Galaxy->DeepSpace != nullptr ? app->Galaxy->DeepSpace->VoidRaidLore : 0;
		cs::String posture = c->RunDark ? cs::String("SENSORS DARK  ·  reactor down, drive cold: hard to find, but no charge builds")
			: VoidRaiders::Lit(*c, jd) ? cs::String("SENSORS LIT UP  ·  your ping is heard by anything out here")
			: cs::String("SENSORS PASSIVE  ·  listening");
		cs::String contact;
		bool closing = !cs::Double::IsNaN(c->RaidStrikeJd);
		if (closing && c->RaidSeen)
			contact = cs::Cat("CONTACT: ", cs::Strings::ToUpperInvariant(VoidRaiders::Called(stage)), " CLOSING  ·  strikes in ",
				Mathf::CeilToInt(static_cast<float>(app->FtlSecondsTo(c->RaidStrikeJd))), " s", c->RaidStrikeJd < c->EngageJd() ? "  ·  before the drive engages" : "  ·  the drive engages first");
		else if (c->RaidStruck) contact = "struck at this stop: the ghosts are gone for now";
		else contact = cs::Cat("no contact  ·  the longer you sit in the void, the likelier something finds you", stage > 0 ? cs::Cat(" (", VoidRaiders::Called(stage), "s)") : cs::String());
		cs::String keys = c->RunDark ? "Shift+F3  power up (the charge builds again)" : "Tab  active ping    Shift+F3  run dark";
		auto a = GUIContent(posture);
		auto m = GUIContent(contact);
		auto b = GUIContent(keys);
		float w = Mathf::Max(text->CalcSize(a).x, Mathf::Max(small->CalcSize(m).x, small->CalcSize(b).x)) + 28;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, Screen::height - 290, w, 68));
		GUI::contentColor = c->RunDark ? Dim : StreakColor;
		GUI::Label(Rect(r.x + 14, r.y + 5, w, 20), a, text);
		GUI::contentColor = closing && c->RaidSeen ? RetrogradeColor : Color::white();
		GUI::Label(Rect(r.x + 14, r.y + 27, w, 18), m, small);
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 14, r.y + 45, w, 18), b, small);
		GUI::contentColor = Color::white();
	}

	void Hud::DrawFtl()
	{
		if (!app->IsPiloting() || app->RidingLane()) return;
		auto ftl = app->Ftl;
		auto ap = app->FtlAutopilot;
		auto course = app->PlottedCourse;
		double jd = app->Clock->Jd;
		// mid: C# null is empty here (every value it takes is non-empty text).
		cs::String head, mid, tail;
		if (ap->Active() && ap->Course != nullptr)
		{
			// The FTL autopilot at the helm (U.SPC-T420): where it has got to in flying the course.
			auto c = ap->Course;
			cs::String name = cs::Strings::ToUpperInvariant(c->Target->Name);
			switch (ap->Stage)
			{
			case FtlAutopilotStage::OutRun:
				head = cs::Cat("FTL AUTOPILOT  ·  ", name, "  ·  OUT TO THE RIM  ·  ", Percent(ap->RunFraction(jd)), "  ·  ",
					SpeedText(ap->VelocityKmS(jd).Length()), "  ·  beyond it in ", Mathf::CeilToInt(static_cast<float>(app->FtlSecondsLeft())), " s");
				break;
			case FtlAutopilotStage::Aligning:
				head = cs::Cat("FTL AUTOPILOT  ·  ", name, "  ·  TURNING ONTO IT");
				break;
			case FtlAutopilotStage::Cooling:
				head = cs::Cat("FTL AUTOPILOT  ·  ", name, "  ·  DRIVE COOLING  ·  spools in ", Mathf::CeilToInt(static_cast<float>(app->FtlSecondsLeft())), " s");
				break;
			default:
				head = cs::Cat("FTL AUTOPILOT  ·  ", name, "  ·  SPOOLING  ·  ", Percent(ftl->SpoolFraction(jd)), "  ·  engages in ",
					Mathf::CeilToInt(static_cast<float>(app->FtlSecondsLeft())), " s");
				break;
			}
			mid = CourseLine(c);
			tail = cs::Cat("time runs at ", app->Clock->Warp().Label, "  (, . to change)    End takes the helm back    a fight or hunters let go of it");
		}
		else if (ftl->Stage == FtlStage::Spooling)
		{
			head = cs::Cat("FTL DRIVE SPOOLING  ·  ", cs::Strings::ToUpperInvariant(ftl->Target->Name), "  ·  ", app->Distance(ftl->Target), "  ·  ",
				Percent(ftl->SpoolFraction(jd)), "  ·  engages in ", Mathf::CeilToInt(static_cast<float>(app->FtlSecondsLeft())), " s");
			tail = "hold the nose on it, beyond the rim and out of any fight, or the charge is lost    Enter calls it off";
		}
		else if (course != nullptr)
		{
			// A plotted course (U.SPC-T420): the way there, the ETA and the fuel, and how to fly it.
			head = cs::Cat("COURSE  ·  ", cs::Strings::ToUpperInvariant(course->Target->KindName()), "  ", cs::Strings::ToUpperInvariant(course->Target->Name), "  ·  ", app->Distance(course->Target));
			mid = course->Flyable() ? CourseLine(course) : course->Refusal;
			tail = cs::Cat(course->Flyable() ? "End  autopilot: out to the rim, onto it, spool and engage    " : "", "PgUp/PgDn  another star or beacon    Del  clear    [  galaxy map");
		}
		else
		{
			auto t = app->FtlPointed;
			auto helm = app->World()->PilotedShip;
			auto drive = CruiseDrives::Of(helm);
			if (t == nullptr)
			{
				// The jump drive cooling from its last drop-out (US-T86), shown whether or not the nose is on a star.
				if (drive == nullptr || !(FtlRules::CoolUntil(helm) > jd)) return;
				head = cs::Cat("JUMP DRIVE COOLING  ·  ", FtlNavigation::Describe(FtlRules::CoolUntil(helm) - jd), " more  ·  then a ",
					FtlNavigation::Describe(drive->SpoolDays()), " spool  ·  ", cs::Fmt(drive->MaxJumpLy, "0.#"), " ly a jump");
				tail = "the drive will not charge until it has cooled    End  autopilot holds on a course and spools when it has";
			}
			else
			{
			double km;
			HelmFtl::Bearing(t, app->ActiveSystem, app->PilotRootKm(), km);
			int jumps = drive != nullptr ? drive->JumpsFor(km / Astro::LightYearKm) : 1;
			head = cs::Cat("FTL  ·  ", cs::Strings::ToUpperInvariant(t->KindName()), "  ", cs::Strings::ToUpperInvariant(t->Name), "  ·  ", app->Distance(t),
				jumps > 1 ? cs::Cat("  ·  ", jumps, " JUMPS (", cs::Fmt(drive->MaxJumpLy, "0.#"), " LY A JUMP)") : cs::String());
			tail = app->FtlBeyondRim
				? (!app->FtlBlocked.empty() ? app->FtlBlocked : cs::String("Enter  spool the drive for it (out through open space to the edge of its well)    End  autopilot"))
				: cs::Cat("inside the gravity wells: no jump until beyond the outer rim, ", cs::Fmt(app->FtlRimAu(), "0.##"), " AU out (",
					cs::Fmt(app->PilotFromStarAu(), "0.##"), " AU now)    End  autopilot flies you out and on");
			}
		}
		auto a = GUIContent(head);
		auto m = GUIContent(mid);
		auto b = GUIContent(tail);
		float w = Mathf::Max(text->CalcSize(a).x, Mathf::Max(small->CalcSize(m).x, small->CalcSize(b).x)) + 28;
		float h = mid.empty() ? 50.0f : 68.0f;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, Screen::height - 222 - h, w, h));
		GUI::contentColor = StreakColor;
		GUI::Label(Rect(r.x + 14, r.y + 5, w, 20), a, text);
		float y = r.y + 27;
		if (!mid.empty())
		{
			GUI::contentColor = course != nullptr && !course->Flyable() && !ap->Active() ? RetrogradeColor : Color::white();
			GUI::Label(Rect(r.x + 14, y, w, 18), m, small);
			y += 18;
		}
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 14, y, w, 18), b, small);
		GUI::contentColor = Color::white();
	}

	cs::String Hud::CourseLine(const cs::Ref<FtlCourse>& c)
	{
		cs::String run = c->NeedsOutRun() ? cs::Cat("out to the rim ", cs::Fmt(c->OutRunKm / Astro::AuKm, "0.##"), " AU, ", FtlNavigation::Describe(c->OutRunDays), "   ·   ") : cs::String();
		cs::String cool = c->CoolDays > 0 ? cs::Cat("cooling ", FtlNavigation::Describe(c->CoolDays), "   ·   ") : cs::String();
		cs::String jump = cs::Cat(cs::Fmt(c->Ly, "0.00"), " ly under FTL, ", FtlNavigation::Describe(c->SpoolDays + c->FtlDays),
			c->Stops > 0 ? cs::Cat(" in ", c->Jumps(), " jumps   ·   ", c->Stops, " void stop", c->Stops == 1 ? "" : "s", " (", FtlNavigation::Describe(c->StopDays), " cooling and spooling)") : cs::String());
		cs::String pulled = c->Interdictions.Count() == 0 ? cs::String() : cs::Cat("   ·   pulled out by ", cs::Strings::Join(", ", c->Interdictions), " (", FtlNavigation::Describe(c->CrossingDays), " under normal drive)");
		return cs::Cat(run, cool, jump, pulled, "   ·   ETA ", Astro::ToDateTime(c->EtaJd()).ToString("yyyy-MM-dd HH:mm"), " (", FtlNavigation::Describe(c->TotalDays()), ")",
			"   ·   fuel ", cs::Fmt(c->FuelTonnes(), "N0"), " of ", cs::Fmt(c->TanksTonnes, "N0"), " t");
	}

	void Hud::DrawCourseMarker()
	{
		DVec3 dir;
		double km = 0;
		if (Event::current().type != EventType::Repaint || !app->PlottedBearing(dir, km)) return;
		Vector2 p;
		if (!DirectionToGui(dir, p)) return;
		auto c = Color(StreakColor.r, StreakColor.g, StreakColor.b, 0.95f);
		constexpr float r = 11;
		DrawLine(p + Vector2(0, -r), p + Vector2(r, 0), c, 1.5f);
		DrawLine(p + Vector2(r, 0), p + Vector2(0, r), c, 1.5f);
		DrawLine(p + Vector2(0, r), p + Vector2(-r, 0), c, 1.5f);
		DrawLabel(cs::Cat(cs::Strings::ToUpperInvariant(app->PlottedCourse->Target->Name), "  ", app->Distance(app->PlottedCourse->Target)), p + Vector2(r + 5, -8), c, false);
		DrawLine(p + Vector2(-r, 0), p + Vector2(0, -r), c, 1.5f);
	}

	void Hud::DrawInterstellarLabels()
	{
		if (Event::current().type == EventType::Repaint && app->Interstellar->Visible() && app->ShowLabels)
		{
			for (const auto& s : app->Interstellar->Stars())
			{
				Vector2 p;
				if (!app->RelativeToGui(s->Relative, p)) continue;
				auto c = Color::Lerp(s->Color, Color::white(), 0.4f);
				if (s->Label.empty()) continue;
				bool system = s->System;
				cs::String name = s->Label;
				// A simulated system's first star names who holds it (U.SPC-T91).
				auto holder = s->Of != nullptr ? s->Of->Holder() : nullptr;
				if (holder != nullptr) name += cs::Cat("  ·  ", holder->Name);
				double ly = s->PositionLy.Length();
				DrawLabel(system ? cs::Strings::ToUpperInvariant(name) : cs::Cat(name, ly > 0.01 ? cs::Cat("  ", cs::Fmt(ly, "0.0"), " ly") : cs::String()),
					p + Vector2(10, -8), system ? Accent : Color(c.r, c.g, c.b, 0.75f), !system);
			}
		}
	}

	void Hud::LayoutNews()
	{
		// The same conditions and sizes DrawTicker and DrawNews draw by, so the slot is the panel's.
		newsRect = Rect(0, 0, 0, 0);
		if (!app->ShowHud) return;
		if (app->ShowNews)
		{
			newsRect = layout.Take(HudRegion::TopCentre, 760, Mathf::Min(Screen::height * 0.5f, 420.0f));
			return;
		}
		int n = cs::Math::Min(4, app->World()->Events.Count());
		if (n > 0) newsRect = layout.Take(HudRegion::TopCentre, 640, 10 + n * 17.0f);
	}

	cs::String Hud::FitText(const cs::String& s, const cs::Ref<GUIStyle>& style, float maxW)
	{
		if (style->CalcSize(GUIContent(s)).x <= maxW) return s;
		// Binary search on the length that still fits with the ellipsis (UTF-8: cut only at a character's first byte).
		const std::string& raw = s;
		size_t lo = 0, hi = raw.size();
		while (lo < hi)
		{
			size_t mid = (lo + hi + 1) / 2;
			while (mid > 0 && mid < raw.size() && (static_cast<unsigned char>(raw[mid]) & 0xC0) == 0x80) mid--;
			if (mid <= lo) { hi = lo; break; }
			if (style->CalcSize(GUIContent(cs::String(raw.substr(0, mid)) + "\xE2\x80\xA6")).x <= maxW) lo = mid;
			else hi = mid - 1;
		}
		while (lo > 0 && lo < raw.size() && (static_cast<unsigned char>(raw[lo]) & 0xC0) == 0x80) lo--;
		return cs::String(raw.substr(0, lo)) + "\xE2\x80\xA6";
	}

	void Hud::DrawBanner(const cs::String& banner)
	{
		auto content = GUIContent(banner);
		auto size = toast->CalcSize(content);
		// Below the clock and news feed at the top of the screen: the next slot in TopCentre (US-T154).
		auto r = layout.Take(HudRegion::TopCentre, size.x, size.y);
		auto old = GUI::contentColor;
		GUI::contentColor = Color(0, 0, 0, 0.7f);
		GUI::Label(Rect(r.x + 1, r.y + 1, r.width, r.height), content, toast);
		GUI::contentColor = old;
		GUI::Label(r, content, toast);
	}

	void Hud::DrawWorldOverlay()
	{
		if (Event::current().type != EventType::Repaint) return;
		auto catalog = app->Catalog();
		double ppu = app->PixelsPerUnitAtUnitDistance();
		DrawSkyMarkers();
		DrawStructureLabels(ppu);

		for (int i = 0; i < catalog->Count(); i++)
		{
			auto body = (*catalog)[i];
			if (!body->IsVisible()) continue;
			auto scene = app->BodyScene(i);
			Vector2 p;
			float depth = 0;
			if (!app->ToGui(scene, p, depth)) continue;
			float pxRadius = static_cast<float>(app->BodyDisplayRadius(i) / depth * ppu);

			if (body->Kind == BodyKind::Moon)
			{
				// Moons hug their planet on screen until you are close; skip them until they separate.
				Vector2 pp;
				float pd = 0;
				if (app->ToGui(app->BodyScene(body->ParentIndex), pp, pd) && (pp - p).magnitude() < 14.0f) continue;
			}
			bool occluded = pxRadius < 3 && app->Occluded(scene, i);
			if (occluded) continue;

			auto tint = BodyTint(body);
			float iconSize = Icons::PixelSize(body);
			if (pxRadius < iconSize * 0.3f)
				DrawIcon(Icons::ForBody(body), p, iconSize, tint);

			if (app->ShowLabels && (body->Kind != BodyKind::Moon || pxRadius > 1.5f || ParentSeparation(body, p) > 50))
				DrawLabel(cs::Cat(body->Name, StatusMark(body), HavenMark(body)), p + Vector2(Mathf::Max(pxRadius, iconSize * 0.5f) + 5, -8), tint, body->Kind == BodyKind::Moon);
		}

		const cs::List<cs::Ref<Ship>>& ships = app->Fleet()->Ships();
		app->FoldedShips.Clear();
		iconMarks.Clear();
		iconDraws.Clear();
		// At the helm the player sees only what the ship's own scanners hold (U.SPC-T66): an unknown blip, a hull
		// class, or -- identified -- whose it is; a track gone cold is drawn faded where it was last seen.
		auto picture = Picture();
		if (picture == nullptr || app->Fleet() != lastSeenFleet)
		{
			lastSeenScene.Clear();
			lastSeenFleet = app->Fleet();
		}
		// A destroyed ship is skipped below before it ever reaches ContactScene again, so its last-seen
		// entry would otherwise sit here for the rest of a long piloting session in this system, never
		// cleared by the whole-dictionary resets above: dropped here instead (same shape as the
		// contactToastAt leak fixed in UnitySpaceApp.Collisions.cs).
		if (lastSeenScene.Count() > 0)
		{
			cs::List<cs::Ref<Ship>> gone; // C# null until the first one: an empty list here
			for (auto kv : lastSeenScene)
				if (kv.first->Destroyed) gone.Add(kv.first);
			for (const auto& s : gone) lastSeenScene.Remove(s);
		}
		for (int i = 0; i < ships.Count(); i++)
		{
			auto ship = ships[i];
			if (ship->Destroyed || ship->Controller()->Phase == ShipPhase::Landed) continue;
			cs::Ref<Contact> contact = picture != nullptr && ship != app->Piloted.Ship ? picture->Of(ship) : nullptr;
			if (picture != nullptr && ship != app->Piloted.Ship && contact == nullptr) continue;
			DVec3 scene;
			if (!ContactScene(ship, contact, scene)) continue;
			Vector2 p;
			float depth = 0;
			if (!app->ToGui(scene, p, depth)) continue;
			if (contact != nullptr && contact->Cold())
			{
				// Lost: only a question mark where the scanners last had it, never where it really is now.
				DrawIcon(Icons::Lost(), p, 14, LostColor(contact));
				continue;
			}
			double length = app->ShipDisplayLength(ship);
			float px = static_cast<float>(ShipLod::ApparentPx(length, depth, ppu));
			// Drawn as its model (and the model built: one still streaming in shows as its icon meanwhile).
			if (app->ShipModelShown(ship, px)) continue;
			if (app->Occluded(scene)) continue;
			Vector2 lp;
			float ld = 0;
			if (ship->IsEscort() && app->ToGui(app->ShipScene(ship->Leader), lp, ld) && (lp - p).magnitude() < 7.0f)
			{
				app->FoldedShips.Add(ship);
				continue;
			}
			auto c = ship->Faction->Color;
			constexpr float alpha = 0.95f;
			cs::Ref<Texture2D> icon;
			float size;
			Color tint;
			int group;
			if (contact == nullptr || contact->Level == ContactLevel::Identified)
			{
				icon = Icons::ForShip(ship->Spec->Class);
				size = Icons::PixelSize(ship->Spec->Class);
				tint = Color(c.R, c.G, c.B, alpha);
				group = GroupOf(ship->Faction);
			}
			else if (contact->Level == ContactLevel::Blip)
			{
				icon = Icons::Blip();
				size = 12;
				tint = Color(ContactGrey.r, ContactGrey.g, ContactGrey.b, alpha);
				group = -1;
			}
			else
			{
				icon = Icons::ForShip(ship->Spec->Class);
				size = Icons::PixelSize(ship->Spec->Class);
				tint = Color(ContactGrey.r, ContactGrey.g, ContactGrey.b, alpha);
				group = -2;
			}
			auto t = Target::OfShip(ship);
			bool pinned = t == app->Hovered || t == app->Selected || app->SpaceCamera->FocusTarget == t || ship == app->Piloted.Ship;
			iconMarks.Add(LodMark(p.x, p.y, group, length, pinned));
			iconDraws.Add(IconDraw{ ship, icon, size, tint, p });
		}

		// Icons of one side that crowd on screen fold into one mark with a count (U.SPC-T415): a fleet, or the
		// traffic round a port, is drawn once, its biggest hull standing for the rest.
		for (const auto& mark : RenderLod::Aggregate(iconMarks, RenderLod::AggregatePx()->Value))
		{
			const IconDraw& d = iconDraws[mark->Representative];
			auto icon = d.icon;
			float size = d.size;
			Color tint = d.tint;
			Vector2 p = d.p;
			DrawIcon(icon, p, size, tint);
			if (mark->Count() > 1)
			{
				for (int k = 1; k < mark->Members.Count(); k++) app->FoldedShips.Add(iconDraws[mark->Members[k]].ship);
				DrawLabel(RenderLod::CountLabel(mark->Count()), p + Vector2(size * 0.5f, size * 0.5f - 6), tint, true);
			}
		}

		DrawTargetMarker(app->Hovered, false);
		DrawTargetMarker(app->Selected, true);
		DrawOpportunityMarkers();
		DrawBioMassMarkers(); // US-T125
		DrawHarvest(); // US-T146

		app->ShipNameRects.Clear();
		if (app->ShowLabels)
		{
			// Names that must show (hovered, selected, followed, a flagship on sight) go down first; a far-off ship's
			// icon name (U.SPC-T376) only where it does not run into one already drawn, so a crowded port stays legible.
			shipLabelRects.Clear();
			iconNamed.Clear();
			optionalNamed.Clear();
			// US-T157: the hull locked in a fight is always named, like the hovered and selected ones.
			auto meUnit = app->PilotUnit();
			cs::Ref<Ship> lockedShip = meUnit != nullptr && meUnit->PlayerLockTarget != nullptr && meUnit->PlayerLockTarget->Present() ? meUnit->PlayerLockTarget->Ship : nullptr;
			for (const auto& ship : ships)
			{
				if (ship->Destroyed) continue;
				cs::Ref<Contact> contact = picture != nullptr && ship != app->Piloted.Ship ? picture->Of(ship) : nullptr;
				if (picture != nullptr && ship != app->Piloted.Ship && contact == nullptr) continue;
				auto t = Target::OfShip(ship);
				bool must = t == app->Hovered || t == app->Selected || app->SpaceCamera->FocusTarget == t || (lockedShip != nullptr && ship == lockedShip);
				bool named = must;
				bool identified = contact == nullptr || contact->Level == ContactLevel::Identified;
				// A flagship is named on sight only once the scanners have identified it.
				if (!named && !ship->IsEscort() && ship->Spec->IsMilitary && identified)
				{
					// Name fleet flagships once they are big enough on screen to be a thing, not a dot.
					Vector2 ignored;
					float d = 0;
					if (app->ToGui(app->ShipScene(ship), ignored, d))
						named = app->ShipDisplayLength(ship) / d * ppu > 12;
				}
				if (!named)
				{
					// Drawn as its icon: named beside it if the settings say so and the scanners know whose it is.
					// Folded into another's mark (U.SPC-T415), the mark's own ship is named for it.
					if (identified && (contact == nullptr || !contact->Cold()) && !app->FoldedShips.Contains(ship) && ShipLod::NamesIcon(ship->Spec->IsMilitary, ship->IsEscort())) iconNamed.Add(IconName{ ship, contact });
					continue;
				}
				if (must) DrawShipName(ship, contact, ppu, false);
				else optionalNamed.Add(IconName{ ship, contact });
			}
			// US-T157: the names that are not a must after the musts, nearest the camera first and only the nearest few, each
			// stepping clear of the names already down (a dogfight's raiders bunched on screen no longer run together).
			auto camAt = app->SpaceCamera->ScenePosition;
			std::stable_sort(optionalNamed.begin(), optionalNamed.end(), [&](const IconName& a, const IconName& b)
				{ return (app->ShipScene(a.ship) - camAt).LengthSquared() < (app->ShipScene(b.ship) - camAt).LengthSquared(); });
			for (int i = 0; i < optionalNamed.Count() && i < MaxOptionalNames; i++)
				DrawShipName(optionalNamed[i].ship, optionalNamed[i].contact, ppu, false, false);
			for (const auto& n : iconNamed)
				DrawShipName(n.ship, n.contact, ppu, true);
		}
	}

	int Hud::GroupOf(const cs::Ref<Faction>& f)
	{
		if (f == nullptr) return 0;
		int g = 0;
		if (!factionGroups.TryGetValue(f, g))
		{
			g = factionGroups.Count() + 1;
			factionGroups[f] = g;
		}
		return g;
	}

	void Hud::DrawShipName(const cs::Ref<Ship>& ship, const cs::Ref<Contact>& contact, double ppu, bool iconName, bool must)
	{
		DVec3 at;
		Vector2 p;
		float depth = 0;
		if (!ContactScene(ship, contact, at) || !app->ToGui(at, p, depth) || app->Occluded(at)) return;
		float px = static_cast<float>(ShipLod::ApparentPx(app->ShipDisplayLength(ship), depth, ppu));
		if (iconName && app->ShipModelShown(ship, px)) return;
		auto c = ship->Faction->Color;
		auto at2 = p + Vector2(Mathf::Max(px * 0.5f, iconName ? Icons::PixelSize(ship->Spec->Class) * 0.5f : 10.0f) + 4, -8);
		// C# `string text` (renamed: it would hide the `text` style member).
		cs::String text_;
		Color tint;
		bool minor;
		if (contact == nullptr)
		{
			text_ = ship->Name;
			tint = Color(c.R, c.G, c.B);
			minor = iconName;
		}
		else
		{
			// A contact is named by what the scanners make of it: unknown, its class, or its name.
			text_ = cs::Cat(contact->Label(), contact->Cold() ? cs::String("  (contact lost)") : contact->LagSeconds() >= 0.1 ? cs::Cat("  (", LightSpeed::Describe(contact->LagSeconds() / Astro::SecondsPerDay), " ago, light lag)") : cs::String());
			tint = contact->Level == ContactLevel::Identified ? Color(c.R, c.G, c.B) : ContactGrey;
			if (contact->Cold()) tint.a = 0.5f;
			minor = contact->Level != ContactLevel::Identified;
		}
		auto size = (minor ? minorLabel : label)->CalcSize(GUIContent(text_));
		auto r = Rect(at2.x, at2.y, size.x, size.y);
		auto clear = [&](const Rect& q)
		{
			for (const auto& other : shipLabelRects)
				if (other.Overlaps(q)) return false;
			return true;
		};
		if (iconName && !clear(r)) return;
		if (!iconName && !must && !clear(r))
		{
			// US-T157: step down beside the hull a line at a time (then up), and leave it off if no step is clear.
			bool placed = false;
			const float steps[] = { 1, 2, -1, 3, -2 };
			for (float s : steps)
			{
				auto q = Rect(r.x, r.y + s * (size.y + 1), r.width, r.height);
				if (!clear(q)) continue;
				r = q;
				at2 = Vector2(q.x, q.y);
				placed = true;
				break;
			}
			if (!placed) return;
		}
		shipLabelRects.Add(r);
		app->ShipNameRects.Add(std::make_tuple(r, ship));
		DrawLabel(text_, at2, tint, minor);
	}

	cs::String Hud::StatusMark(const cs::Ref<BodyDefinition>& b)
	{
		return b->Status == BodyStatus::Candidate ? "  (candidate)" : b->Status == BodyStatus::Disputed ? "  (disputed)" : "";
	}

	void Hud::DrawSkyMarkers()
	{
		if (!app->ShowLabels) return;
		for (const auto& other : app->Galaxy->Systems)
		{
			if (other == app->ActiveSystem) continue;
			auto dir = app->SkyDirection(other) * 1e6f;
			Vector2 p;
			if (!app->RelativeToGui(dir, p)) continue;
			if (p.x < 0 || p.y < 0 || p.x > Screen::width || p.y > Screen::height) continue;
			auto c = Color(1.0f, 0.95f, 0.8f, 0.9f);
			// A binary's root is the invisible barycentre, so its first star stands for it; a Void Base has no
			// star at all (U.SPC-T372), just its one body.
			auto mark = other->Catalog->Root()->IsStar() || other->Catalog->Count() < 2 ? other->Catalog->Root() : (*other->Catalog)[1];
			DrawIcon(Icons::ForBody(mark), p, 14, c);
			cs::String name = other->Catalog->Root()->Name == "Sun" ? cs::String("Sun  (Sol System)") : other->Name;
			double ly = Galaxy::DistanceLy(app->ActiveSystem, other);
			// Every ship in another system is folded into its one mark (U.SPC-T415), which says how many.
			cs::String ships = RenderLod::SystemShipsLabel(RenderLod::LiveShips(other->World != nullptr ? other->World->Fleet : cs::Ref<Core::Fleet>()));
			DrawLabel(cs::Cat(name, "  ", cs::Fmt(ly, "0.00"), " ly", ships.size() > 0 ? cs::Cat("  · ", ships) : cs::String()), p + Vector2(10, -8), c, true);
		}
	}

	void Hud::DrawStructureLabels(double ppu)
	{
		auto views = app->Structures;
		if (views == nullptr || !app->ShowLabels) return;
		for (auto kv : views->Scenes)
		{
			Vector2 p;
			float depth = 0;
			if (!app->ToGui(kv.second, p, depth)) continue;
			float px = static_cast<float>(views->Sizes.At(kv.first) / depth * ppu);
			auto t = Target::OfStructure(kv.first);
			if (px < 6 && t != app->Selected && t != app->Hovered) continue;
			if (app->Occluded(kv.second)) continue;
			auto c = kv.first->Owner() != nullptr ? Color(kv.first->Owner()->Color.R, kv.first->Owner()->Color.G, kv.first->Owner()->Color.B) : Color(0.85f, 0.88f, 0.92f);
			DrawLabel(kv.first->Name, p + Vector2(Mathf::Max(px * 0.5f, 6.0f) + 4, -8), c, true);
		}
		for (auto kv : views->RingScenes)
		{
			Vector2 p;
			float depth = 0;
			if (!app->ToGui(kv.second, p, depth)) continue;
			float px = static_cast<float>(views->Sizes.At(kv.first) / depth * ppu);
			auto t = Target::OfRing(kv.first);
			if (px < 2.5f && t != app->Selected && t != app->Hovered) continue;
			if (app->Occluded(kv.second)) continue;
			int queued = 0;
			for (const auto& s : app->Fleet()->Ships())
				if (!s->Destroyed && s->Phase == ShipPhase::Queued && s->Ring == kv.first) queued++;
			DrawLabel(cs::Cat(kv.first->Name, queued > 0 ? cs::Cat("  · ", queued, " queued") : cs::String()), p + Vector2(Mathf::Max(px * 0.5f, 6.0f) + 4, -8), Color(0.6f, 0.9f, 1.0f), true);
		}
	}

	float Hud::ParentSeparation(const cs::Ref<BodyDefinition>& moon, Vector2 p)
	{
		Vector2 pp;
		float pd = 0;
		return app->ToGui(app->BodyScene(moon->ParentIndex), pp, pd) ? (pp - p).magnitude() : 0;
	}

	// ---------------------------------------------------------------- flight markers (U.SPC-T59)

	void Hud::DrawFlightMarkers()
	{
		if (!app->IsPiloting() || app->Travel->Active || Event::current().type != EventType::Repaint) return;
		auto vel = app->PilotVelocityKmS();
		double speed = vel.Length();
		Vector2 nose;
		bool noseOn = DrawAimReticle(nose);

		double driftDeg = 0;
		if (speed > 1e-6)
		{
			auto dir = vel / speed;
			driftDeg = cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(dir, app->PilotForward())))) * 180 / cs::Math::PI;
			Vector2 pro;
			if (DirectionToGui(dir, pro))
			{
				if (noseOn && (pro - nose).sqrMagnitude() > 400) DrawLine(nose, pro, Color(ProgradeColor.r, ProgradeColor.g, ProgradeColor.b, 0.45f), 1.5f);
				DrawIcon(Icons::Prograde(), pro, 28, ProgradeColor);
				DrawLabel(SpeedText(speed), pro + Vector2(17, -8), ProgradeColor, true);
			}
			Vector2 retro;
			if (DirectionToGui(-dir, retro)) DrawIcon(Icons::Retrograde(), retro, 28, RetrogradeColor);
		}

		// A hauler's stop point (U.SPC-T443): where full retro brings it to rest with what it carries, so the braking
		// can be planned; marked toward it on the track.
		auto stop = app->PilotStop;
		cs::String stopText = "";
		if (stop.has_value() && speed > 1e-6)
		{
			auto st = *stop;
			stopText = st.Reachable()
				? cs::Cat("   STOP ", RangeText(st.DistanceKm()), " in ", Mathf::CeilToInt(static_cast<float>(st.Seconds)), "s")
				: cs::String("   STOP none (no retro)");
			Vector2 sp;
			if (st.Reachable() && st.DistanceKm() > 1e-9 && DirectionToGui(st.OffsetKm / st.DistanceKm(), sp))
			{
				DrawRing(sp, 9, RetrogradeColor);
				DrawLabel(cs::Cat("STOP ", RangeText(st.DistanceKm())), sp + Vector2(12, 6), RetrogradeColor, true);
			}
		}

		bool assist = app->PilotFlightAssist();
		// A capital's engine telegraph (U.SPC-T499): the step its engines are rung to, held until rung again.
		auto telegraph = app->PilotTelegraph;
		cs::String readout = cs::Cat(assist ? "FLIGHT ASSIST ON" : "FLIGHT ASSIST OFF",
			telegraph.has_value() ? cs::Cat("   ENGINES ", TelegraphText(*telegraph), " (W/S)") : cs::String(),
			app->PilotDocking ? cs::String("   DOCKING") : app->PilotStop.has_value() && app->PilotInPortApproach ? cs::String("   hold Ctrl to dock") : cs::String(), "   ", SpeedText(speed), stopText,
			speed > 1e-6 ? cs::Cat("   track ", cs::Fmt(driftDeg, "0"), "° off the nose") : cs::String(),
			app->PilotFlightNote.empty() ? cs::String() : cs::Cat("   ", app->PilotFlightNote), "   (F)"); // US-T87: the brake plan
		auto content = GUIContent(readout);
		auto size = small->CalcSize(content);
		auto p = Vector2((Screen::width - size.x) * 0.5f, static_cast<float>(Screen::height - 66));
		DrawLabel(readout, p, assist ? Dim : RetrogradeColor, true);
	}

	bool Hud::DrawAimReticle(Vector2& nose)
	{
		bool noseOn = DirectionToGui(app->PilotForward(), nose);
		if (noseOn) DrawBoresight(nose, Color(1, 1, 1, 0.8f));
		Vector2 aim;
		if (app->PilotControlMode != PilotControl::Helm && DirectionToGui(app->PilotAimDirection(), aim) && (!noseOn || (aim - nose).sqrMagnitude() > 4))
			DrawCrosshair(aim, CrosshairColor);
		return noseOn;
	}

	void Hud::DrawReticleOnly()
	{
		if (Event::current().type != EventType::Repaint || app->Travel->Active) return;
		if (!app->SeatLine().empty()) DrawSeatSight();
		else if (app->IsPiloting())
		{
			Vector2 ignored;
			DrawAimReticle(ignored);
		}
	}

	// ---------------------------------------------------------------- the chase (U.SPC-T273)

	void Hud::DrawHunters()
	{
		if (!app->IsPiloting()) return;
		auto pursuit = app->World()->Pursuit;
		if (pursuit == nullptr || !pursuit->Active()) return;
		// TopRight, under the opportunities or the market (US-T153): its own slot, taken on every event so the selection
		// card under it is sized alike in the Layout pass and the Repaint.
		auto slot = layout.Take(HudRegion::TopRight, 380, 22.0f + 18.0f * pursuit->Groups.Count());
		if (Event::current().type != EventType::Repaint) return;
		auto me = app->PilotOffsetKm();
		auto myVel = app->PilotVelocityKmS();
		float y = slot.y;
		DrawLabel("HUNTED", Vector2(slot.x, y), HostileColor, false);
		y += 22;
		for (const auto& g : pursuit->Groups)
		{
			auto rel = g->Pos - me;
			double d = rel.Length();
			double closing = d > 1e-9 ? -DVec3::Dot(g->Vel - myVel, rel / d) : 0;
			cs::String line = cs::Cat(g->Faction()->Name, " ", g->Leader->Name, g->Ships.Count() > 1 ? cs::Cat(" +", g->Ships.Count() - 1) : cs::String(), "   ", RangeText(d),
				"   closing ", cs::Fmt(closing, "0.0"), " km/s");
			if (g->EmpCharge >= 0)
				line += cs::Cat("   EMP ", cs::Fmt(cs::Math::Max(0.0, Pursuit::EmpChargeSeconds - g->EmpCharge), "0.0"), " s (clear ",
					Mathf::RoundToInt(static_cast<float>(Pursuit::EmpRangeKm * 1.25)), " km)");
			DrawLabel(FitText(line, minorLabel, slot.width), Vector2(slot.x, y), g->EmpCharge >= 0 ? LockColor : Dim, true);
			y += 18;
		}
	}

	// ---------------------------------------------------------------- aimed combat (U.SPC-T270)

	void Hud::DrawCombatHud()
	{
		if (Event::current().type != EventType::Repaint) return;
		auto me = app->PilotUnit();
		if (me == nullptr || app->Travel->Active) return;
		auto battle = app->Piloted.Ship->Battle;
		auto c = me->Combat;

		auto locked = me->PlayerLockTarget;
		Vector2 lp;
		float ld = 0;
		if (locked != nullptr && locked->Present() && app->ToGui(app->BattleScene(battle, locked->Pos), lp, ld))
		{
			bool made = me->LockProgress >= 1;
			// The box closes in on the hull as the lock builds.
			float half = Mathf::Lerp(46.0f, 18.0f, static_cast<float>(me->LockProgress));
			DrawBrackets(Rect(lp.x - half, lp.y - half, half * 2, half * 2), made ? LockColor : HostileColor, made ? 2.5f : 1.5f);
			cs::String state = made ? cs::String("LOCKED") : cs::Cat("LOCKING ", Mathf::RoundToInt(static_cast<float>(me->LockProgress) * 100), "%");
			DrawLabel(cs::Cat(state, "  ", RangeText((locked->Pos - me->Pos).Length())), lp + Vector2(half + 6, -8), made ? LockColor : HostileColor, true);
		}

		// The lead pip for the guns' rounds, on the locked hull or else the one nearest the crosshair.
		double speed = RoundSpeed(c);
		auto pipOn = locked != nullptr && locked->Present() ? locked : NearestHostileToAim(battle, me);
		if (speed > 0 && pipOn != nullptr)
		{
			auto lead = Gunnery::LeadPoint(me->Pos, me->Vel, pipOn->Pos, pipOn->Vel, speed);
			Vector2 pip;
			float pd = 0;
			if (app->ToGui(app->BattleScene(battle, lead), pip, pd))
			{
				DrawRing(pip, 7, HostileColor);
				Vector2 at;
				float ad = 0;
				if (app->ToGui(app->BattleScene(battle, pipOn->Pos), at, ad) && (at - pip).sqrMagnitude() > 64)
					DrawLine(at, pip, Color(HostileColor.r, HostileColor.g, HostileColor.b, 0.35f), 1.0f);
			}
		}

		// The gauges, in a row above the flight readout.
		float w = 120, x0 = Screen::width * 0.5f - 2 * (w + 14) + 7, y = static_cast<float>(Screen::height - 98);
		DrawGauge(Rect(x0, y, w, 6), static_cast<float>(me->Heat), me->Overheated ? HostileColor : Color(1.0f, 0.7f, 0.3f), me->Overheated ? "OVERHEATED" : "HEAT");
		DrawGauge(Rect(x0 + (w + 14), y, w, 6), static_cast<float>(c->BoostFraction), Accent, "BOOST");
		double maxShield = cs::Math::Max(1e-9, c->Spec->Shield);
		double shieldSum = 0; // c.Subsystems.Sum(s => s.Shield)
		for (const auto& s : c->Subsystems) shieldSum += s->Shield;
		DrawGauge(Rect(x0 + 2 * (w + 14), y, w, 6), static_cast<float>(shieldSum / maxShield), Color(0.45f, 0.7f, 1.0f), "SHIELDS");
		DrawGauge(Rect(x0 + 3 * (w + 14), y, w, 6), static_cast<float>(c->Hull / cs::Math::Max(1e-9, c->Spec->Hull)), ProgradeColor, "HULL");
		// The guided rounds, homing and hive, apart from the dumb-fire rockets, which need no lock (US-T101).
		int missiles = 0, hive = 0, rockets = 0; // c.Subsystems.Where(...).Sum(s => s.Ammo)
		for (const auto& s : c->Subsystems)
		{
			if (s->Destroyed() || s->Def->Weapon == nullptr || !s->Def->Weapon->Missile || s->Kind() == SubsystemKind::Torpedo) continue;
			if (Arsenal::IsRocket(s->Def->Weapon)) rockets += s->Ammo;
			else if (Arsenal::IsHive(s->Def->Weapon)) hive += s->Ammo;
			else missiles += s->Ammo;
		}
		cs::List<cs::String> line;
		const char* lockHint = me->LockProgress >= 1 ? " (right mouse)" : " (T lock, hold the crosshair on it)";
		if (missiles > 0) line.Add(cs::Cat("HOMING MISSILES ", missiles, lockHint));
		if (hive > 0) line.Add(cs::Cat("HIVE ", hive, lockHint));
		if (rockets > 0) line.Add(cs::Cat("ROCKETS ", rockets, " (right mouse, dumb-fire)"));
		// A rotary gun's barrels (US-T101): how far they have spun up.
		for (const auto& s : c->Subsystems)
			if (!s->Destroyed() && Arsenal::IsRotary(s->Def->Weapon))
			{
				line.Add(cs::Cat("VULCAN SPIN ", Mathf::RoundToInt(static_cast<float>(s->Spin) * 100), "%"));
				break;
			}
		// The rest of the arsenal (U.SPC-T271): rounds left, and whether the mount is ready.
		AddMount(line, c, SubsystemKind::EmpLauncher, "EMP", "middle mouse");
		AddMount(line, c, SubsystemKind::MineLayer, "MINES", "Ctrl");
		if (line.Count() > 0) DrawLabel(cs::Strings::Join("    ", line), Vector2(x0, y + 12), Dim, true);
		if (app->PilotControlMode == PilotControl::Helm) DrawBroadside(battle, me, x0, y + 30);
	}

	// ---------------------------------------------------------------- the reactor's split (US-T94)

	void Hud::DrawReactor()
	{
		if (app->Travel->Active) return;
		auto seat = app->HeldSeat();
		if (seat == nullptr || !CraftPower::Shares(seat)) return;
		// Screen space, a fixed size: the same at the gameplay and the realistic scale. BottomRight's first slot, the
		// sensors and then the selection card stacked over it (US-T152); taken on every event so they are placed alike
		// in the Layout pass and the Repaint. Not registered as UI, so it never eats a click.
		constexpr float w = 232, h = 142, segW = 56, segH = 9, gap = 3, step = 76;
		auto r = layout.Take(HudRegion::BottomRight, w, h);
		if (Event::current().type != EventType::Repaint) return;
		auto held = CraftPower::Of(seat, false);
		PowerDistribution even; // nothing moved yet: the even share it runs at
		const PowerDistribution& p = held != nullptr ? *held : even;
		GUI::Box(r, GUIContent::none(), panel);
		cs::String preset = p.PresetName();
		// Powered down (US-T93): the split waits for the restart, nothing runs on it now.
		bool offline = SensorPosture::Offline(SelfDestruct::CombatOf(seat));
		if (offline) DrawLabel("REACTOR  \xC2\xB7  OFFLINE", Vector2(r.x + 10, r.y + 6), Color(0.6f, 0.75f, 1.0f), true);
		else DrawLabel(preset.empty() ? cs::String("REACTOR") : cs::Cat("REACTOR  \xC2\xB7  ", preset), Vector2(r.x + 10, r.y + 6), Accent, true);
		const char* names[] = { "WEAPONS", "SHIELDS", "ENGINES" };
		const Color colors[] = { Color(1.0f, 0.62f, 0.32f), Color(0.45f, 0.7f, 1.0f), Color(0.55f, 1.0f, 0.62f) };
		auto old = GUI::color;
		float base = r.y + 30 + PowerDistribution::MaxPips * (segH + gap);
		for (int k = 0; k < 3; k++)
		{
			auto s = static_cast<PowerSystem>(k);
			float x = r.x + 12 + k * step;
			// One bar per pip, filled from the bottom: four is all the reactor can give one system.
			for (int i = 0; i < PowerDistribution::MaxPips; i++)
			{
				GUI::color = i < p[s] ? (offline ? Color(colors[k].r, colors[k].g, colors[k].b, 0.25f) : colors[k]) : Color(0, 0, 0, 0.45f);
				GUI::DrawTexture(Rect(x, base - (i + 1) * (segH + gap), segW, segH), Icons::White());
			}
			GUI::color = old;
			DrawLabel(names[k], Vector2(x, base + 2), colors[k], true);
			DrawLabel(cs::Cat("x", cs::Fmt(p.Factor(s), "0.00")), Vector2(x, base + 18), Dim, true);
		}
		DrawLabel("arrows: L/U/R a pip, Down even, Shift all", Vector2(r.x + 10, r.y + h - 20), Dim, true);
	}

	// ---------------------------------------------------------------- the sensors and power (US-T93)

	void Hud::DrawSensors()
	{
		if (app->Travel->Active || !app->IsPiloting()) return;
		auto ship = app->Piloted.Ship;
		auto sensors = app->World()->Sensors;
		if (ship == nullptr || ship->Combat == nullptr || sensors == nullptr) return;
		auto c = ship->Combat;
		// Stacked over the reactor's panel in BottomRight (US-T152), the same width; screen space, the same at either scale.
		constexpr float w = 232, h = 62;
		auto r = layout.Take(HudRegion::BottomRight, w, h);
		if (Event::current().type != EventType::Repaint) return;
		GUI::Box(r, GUIContent::none(), panel);
		auto state = SensorPosture::StateOf(c);
		float pulse = 0.65f + 0.35f * Mathf::Abs(Mathf::Sin(Time::unscaledTime * 4.0f));
		const Color darkBlue(0.55f, 0.72f, 1.0f), amber(1.0f, 0.75f, 0.3f), hot(1.0f, 0.42f, 0.25f);
		cs::String heading, detail;
		Color tint = Accent;
		// The bar under the heading: the heat (dark), the restart (restarting) or the lit window (after a ping).
		float bar = -1;
		Color barTint = tint;
		if (state == PowerState::Dark)
		{
			bool cold = c->ResidualHeat <= SensorPosture::DarkFloor * 1.5;
			heading = cold ? "DARK  \xC2\xB7  COLD" : "DARK  \xC2\xB7  COOLING";
			detail = cs::Cat("heat ", cs::Fmt(c->ResidualHeat * 100, c->ResidualHeat < 0.1 ? "0.0" : "0"), "%  \xC2\xB7  Shift+F3 power up (",
				static_cast<int>(cs::Math::Ceiling(SensorPosture::RestartSeconds(ship))), " s)");
			tint = darkBlue;
			bar = static_cast<float>(c->ResidualHeat);
			barTint = darkBlue;
		}
		else if (state == PowerState::Restarting)
		{
			double total = SensorPosture::RestartSeconds(ship);
			heading = cs::Cat("RESTARTING  ", static_cast<int>(cs::Math::Ceiling(c->RestartLeftS)), " s");
			detail = "nothing online yet  \xC2\xB7  heat climbing  \xC2\xB7  Shift+F3 dark";
			tint = Color(amber.r, amber.g, amber.b, pulse);
			bar = total > 0 ? static_cast<float>(1 - c->RestartLeftS / total) : 1.0f;
			barTint = amber;
		}
		else if (SensorPosture::Lit(c.get()))
		{
			heading = SensorPosture::Bursting(c.get()) ? cs::String("PING  \xC2\xB7  ACTIVE SWEEP") : cs::Cat("LIT UP  ", static_cast<int>(cs::Math::Ceiling(c->PingLitS)), " s");
			detail = "every sensor in reach hears you";
			tint = Color(hot.r, hot.g, hot.b, pulse);
			bar = static_cast<float>(c->PingLitS / SensorPosture::PingLitS);
			barTint = hot;
		}
		else
		{
			heading = "SENSORS  \xC2\xB7  PASSIVE";
			detail = c->PingCooldownS > 0 ? cs::Cat("ping ready in ", static_cast<int>(cs::Math::Ceiling(c->PingCooldownS)), " s  \xC2\xB7  Shift+F3 go dark")
				: cs::String("Tab ping  \xC2\xB7  Shift+F3 go dark");
		}
		// After the restart's last seconds, a ping just past: show the lit window on the dark and restart lines too.
		if (state != PowerState::Online && SensorPosture::Lit(c.get())) detail = cs::Cat("LIT UP ", static_cast<int>(cs::Math::Ceiling(c->PingLitS)), " s  \xC2\xB7  ", detail);
		double sig = sensors->PilotSignature();
		DrawLabel(heading, Vector2(r.x + 10, r.y + 6), tint, true);
		DrawLabel(cs::Cat("SIG ", cs::Fmt(sig, sig < 10 ? "0.00" : "0")), Vector2(r.x + w - 70, r.y + 6), Dim, true);
		if (bar >= 0)
		{
			auto old = GUI::color;
			GUI::color = Color(0, 0, 0, 0.5f);
			GUI::DrawTexture(Rect(r.x + 10, r.y + 26, w - 20, 5), Icons::White());
			GUI::color = barTint;
			GUI::DrawTexture(Rect(r.x + 10, r.y + 26, (w - 20) * Mathf::Clamp01(bar), 5), Icons::White());
			GUI::color = old;
		}
		DrawLabel(detail, Vector2(r.x + 10, r.y + 38), Dim, true);
	}

	// ---------------------------------------------------------------- the self-destruct (US-T97)

	void Hud::DrawSelfDestruct()
	{
		if (Event::current().type != EventType::Repaint || app->Travel->Active) return;
		auto seat = app->HeldSeat();
		if (seat == nullptr || !SelfDestruct::Can(seat)) return;
		double left = SelfDestruct::Remaining(seat);
		float hold = app->SelfDestructHoldSpent() ? 0.0f : app->Input.SelfDestructHoldS;
		if (left < 0 && hold <= 0) return;
		static cs::Ref<GUIStyle> big, sub;
		if (big == nullptr)
		{
			big = cs::New<GUIStyle>();
			big->fontSize = 40;
			big->fontStyle = FontStyle::Bold;
			big->alignment = TextAnchor::MiddleCenter;
			sub = cs::New<GUIStyle>();
			sub->fontSize = 15;
			sub->fontStyle = FontStyle::Bold;
			sub->alignment = TextAnchor::MiddleCenter;
		}
		auto red = Color(1.0f, 0.22f, 0.15f);
		auto old = GUI::contentColor;
		auto oldColor = GUI::color;
		float w = 520, y = Mathf::Max(Screen::height * 0.22f, 120.0f);
		auto r = Rect((Screen::width - w) * 0.5f, y, w, 118);
		GUI::Box(r, GUIContent::none(), panel);
		if (left >= 0)
		{
			// The count, pulsing faster as it runs out.
			float rate = left < 3 ? 8.0f : 3.0f;
			float pulse = 0.65f + 0.35f * Mathf::Abs(Mathf::Sin(Time::unscaledTime * rate));
			GUI::contentColor = Color(red.r, red.g, red.b, pulse);
			GUI::Label(Rect(r.x, r.y + 6, w, 30), "SELF-DESTRUCT", sub);
			GUI::Label(Rect(r.x, r.y + 30, w, 50), cs::Fmt(cs::Math::Ceiling(left * 10) / 10, "0.0"), big);
			double fuse = SelfDestruct::FuseSeconds(seat);
			GUI::color = Color(0, 0, 0, 0.5f);
			GUI::DrawTexture(Rect(r.x + 20, r.y + 84, w - 40, 6), Icons::White());
			GUI::color = red;
			GUI::DrawTexture(Rect(r.x + 20, r.y + 84, (w - 40) * static_cast<float>(fuse > 0 ? left / fuse : 0), 6), Icons::White());
			GUI::color = oldColor;
			GUI::contentColor = Dim;
			GUI::Label(Rect(r.x, r.y + 94, w, 20), cs::Cat(seat->Name(), "  \xC2\xB7  everyone aboard dies  \xC2\xB7  Shift+Backspace aborts"), sub);
		}
		else
		{
			// Held, not yet armed: the bar fills over the second it takes.
			float f = Mathf::Clamp01(hold / static_cast<float>(SelfDestruct::ArmHoldS));
			GUI::contentColor = red;
			GUI::Label(Rect(r.x, r.y + 14, w, 30), "ARMING SELF-DESTRUCT", sub);
			GUI::color = Color(0, 0, 0, 0.5f);
			GUI::DrawTexture(Rect(r.x + 20, r.y + 54, w - 40, 12), Icons::White());
			GUI::color = red;
			GUI::DrawTexture(Rect(r.x + 20, r.y + 54, (w - 40) * f, 12), Icons::White());
			GUI::color = oldColor;
			GUI::contentColor = Dim;
			GUI::Label(Rect(r.x, r.y + 80, w, 20), cs::Cat("keep holding Shift+Backspace to arm the ", seat->Name(), "'s self-destruct"), sub);
		}
		GUI::contentColor = old;
	}

	void Hud::DrawBroadside(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& me, float x, float y)
	{
		auto look = app->PilotLookDirection();
		int loaded = 0, bearing = 0;
		for (const auto& s : me->Combat->Subsystems)
		{
			auto w = s->Def->Weapon;
			if (s->Destroyed() || w == nullptr || w->Missile || w->AntiSmall || w->Supporting() || !me->BearsAlong(s->Def, look)) continue;
			bearing++;
			if (s->Cooldown <= 0) loaded++;
		}
		float built = static_cast<float>(me->VolleyCharge / Battle::VolleyBuildSeconds);
		// C# `string text` (renamed: it would hide the `text` style member).
		cs::String text_ = cs::Cat(bearing == 0 ? cs::String("NO GUN BEARS HERE") : cs::Cat("BATTERY BEARING  ", loaded, "/", bearing, " loaded"),
			me->PlayerFireHeld ? cs::Cat("  ·  VOLLEY ", Mathf::RoundToInt(built * 100), "%  (let go to fire)") : cs::String("  ·  hold left mouse to build a volley"));
		DrawLabel(text_, Vector2(x, y), me->PlayerFireHeld ? LockColor : Dim, true);

		// The hull the volley will go at: the locked one near the line, else the hostile nearest it.
		cs::Ref<BattleUnit> target = nullptr;
		double best = cs::Math::Cos(Battle::VolleyConeDeg * cs::Math::PI / 180);
		for (const auto& o : battle->Units)
		{
			if (!battle->Hostile(me, o)) continue;
			auto to = o->Pos - me->Pos;
			if (to.LengthSquared() < 1e-12) continue;
			double cos = DVec3::Dot(look, to.Normalized());
			if (o == me->PlayerLockTarget && cos > cs::Math::Cos(Battle::VolleyConeDeg * cs::Math::PI / 180)) { target = o; break; }
			if (cos > best) { best = cos; target = o; }
		}
		Vector2 tp;
		float td = 0;
		if (target != nullptr && app->ToGui(app->BattleScene(battle, target->Pos), tp, td))
			DrawRing(tp, 14 + 6 * built, me->PlayerFireHeld ? LockColor : HostileColor);
		DrawCaptain(battle, me, x, y);
	}

	void Hud::DrawCaptain(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& me, float x, float y)
	{
		auto cmd = me->Broadside;
		if (cmd == nullptr) return;
		auto alongLook = Broadsides::OrderAlong(me, app->PilotLookDirection());
		cs::Nullable<BatteryOrder> order = alongLook.first;
		int mounts = alongLook.second;
		cs::String battery = mounts == 0 ? "none bear here" : !order.has_value() ? "mixed" : *order == BatteryOrder::HoldFire ? "HOLD" : *order == BatteryOrder::Volley ? "VOLLEY" : "AT WILL";
		cs::String brace = cmd->Braced() ? cs::String("BRACED") : cmd->BraceCooldown > 0 ? cs::Cat("brace in ", Mathf::CeilToInt(static_cast<float>(cmd->BraceCooldown)), "s") : cs::String("3 brace");
		cs::String orders = cs::Cat("1 BATTERY ", battery, mounts > 0 ? cs::Cat(" (", mounts, ")") : cs::String(),
			"    2 GUNNERS ", cmd->SpreadFire ? "spread" : "focus",
			"    ", brace,
			"    4 DAMAGE CONTROL ", Broadsides::Label(cmd->Focus), " (", Broadsides::Teams(me), " teams)",
			EnergyWeapons::HasPhasers(me->Combat) ? cs::Cat("    INS PHASERS ", EnergyWeapons::Label(me->Combat->PhaserTuning)) : cs::String(),
			HeatText(me));
		DrawLabel(orders, Vector2(x, y - 62), cmd->Braced() ? LockColor : Dim, true);
		double turn = me->Ship != nullptr ? PilotTuning::Of(me->Ship->Spec->Class).TurnRateDegS : me->Combat->Spec->TurnRateDegS;
		double about = Broadsides::ComeAboutSeconds(turn);
		// At the helm a capital turns by its own model's steerage at the speed it is making (U.SPC-T499).
		cs::Ref<CapitalFlightModel> capital = me->Ship != nullptr ? cs::As<CapitalFlightModel>(FlightModelRegistry::Default()->For(me->Ship->Spec->Class)) : nullptr;
		if (capital != nullptr)
		{
			auto hull = HullDynamics::Of(me->Combat->Spec);
			auto tuning = PilotTuning::Of(me->Ship->Spec->Class);
			turn = capital->TurnRateAt(me->Vel.Length(), hull, tuning);
			about = capital->ComeAboutSecondsAt(me->Vel.Length(), hull, tuning);
		}
		double radius = Broadsides::TurningRadiusKm(me->Vel.Length(), turn);
		cs::String line = cs::Cat("5-9 LINE ", Broadsides::Label(cmd->Formation), "  ·  0 all at will",
			"    TURN CIRCLE ", cs::Double::IsInfinity(radius) ? cs::String("-") : RangeText(radius), "  ·  about in ", cs::Double::IsInfinity(about) ? cs::String("-") : cs::Cat(Mathf::RoundToInt(static_cast<float>(about)), "s"));
		DrawLabel(line, Vector2(x, y - 44), Dim, true);
		DrawPatches(battle, me, cmd, x, y - 80);

		// The enemy's broadside building on us: the Black Flag tell.
		auto incoming = Broadsides::IncomingBroadside(battle, me);
		if (incoming == nullptr)
			for (const auto& o : battle->Units)
				if (battle->Hostile(me, o) && o->Broadside != nullptr && o->Broadside->Readying() && o->Broadside->ReadyingOn == me) { incoming = o; break; }
		if (incoming != nullptr)
		{
			auto r = incoming->Broadside;
			int loaded = r != nullptr ? Mathf::RoundToInt(static_cast<float>(r->ReadyingLoaded) * 100) : 100;
			// An alert in the TopCentre stack under the news and banners (HudLayout.h). Only on Repaint, as only the toast
			// (no buttons) can come after it there.
			cs::String warn = cs::Cat("INCOMING BROADSIDE  ·  ", incoming->Name(), " VOLLEY ", loaded, "%  ·  3 BRACE");
			auto size = label->CalcSize(GUIContent(warn));
			auto slot = layout.Take(HudRegion::TopCentre, size.x, size.y);
			DrawLabel(warn, Vector2(slot.x, slot.y), HostileColor, false);
		}
	}

	void Hud::DrawPatches(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& me, const cs::Ref<BroadsideCommand>& cmd, float x, float y)
	{
		auto patches = WeakPatches::Readout(battle, me);
		if (patches.Count() == 0) return;
		cs::List<cs::String> weak;
		cs::Nullable<HullPatch> under;
		for (const auto& p : patches)
		{
			if (p.Weak()) weak.Add(cs::Cat(p.Name, p.Holed ? cs::String(" HOLED") : cs::Cat(" ", Mathf::RoundToInt(static_cast<float>(p.Field) * 100), "%")));
			if (p.Fire > 0 && (!under.has_value() || p.Fire > under->Fire)) under = p;
		}
		// C# `string text` (renamed: it would hide the `text` style member).
		cs::String text_ = cs::Cat("PATCHES ", weak.Count() == 0 ? cs::String("all holding") : cs::Cat("WEAK: ", cs::Strings::Join(", ", weak)),
			under.has_value() ? cs::Cat("  ·  UNDER FIRE: ", under->Name, under->Weak() ? " (weak)" : "") : cs::String(),
			"    SHIFT+DEL roll them away: ", cmd->RollWeakPatches ? "ON" : "off");
		bool danger = under.has_value() && under->Weak();
		DrawLabel(text_, Vector2(x, y), danger ? HostileColor : weak.Count() > 0 ? LockColor : Dim, true);
	}

	cs::String Hud::HeatText(const cs::Ref<BattleUnit>& me)
	{
		if (!Radiators::Carries(me->Combat)) return "";
		int standing = 0, panels = 0;
		for (const auto& s : me->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::Radiator)
			{
				panels++;
				if (!s->Destroyed()) standing++;
			}
		double throttle = Radiators::Throttle(me);
		cs::String state = Radiators::RunsHot(me) ? cs::String(me->HullHeat > 1 ? "RUNNING HOT, COOKING" : "running hot")
			: throttle < 0.999 ? cs::Cat("throttled ", Mathf::RoundToInt(static_cast<float>(throttle) * 100), "%") : cs::String("SHIFT+INS run hot");
		return cs::Cat("    HEAT ", Mathf::RoundToInt(static_cast<float>(me->HullHeat) * 100), "% (radiators ", standing, "/", panels, ", ", state, ")");
	}

	void Hud::AddMount(cs::List<cs::String>& line, const cs::Ref<ShipCombat>& c, SubsystemKind kind, const cs::String& name, const cs::String& key)
	{
		int ammo = 0;
		bool ready = false;
		for (const auto& s : c->Subsystems)
		{
			if (s->Kind() != kind || s->Destroyed()) continue;
			ammo += s->Ammo;
			ready |= s->Ammo > 0 && s->Cooldown <= 0;
		}
		if (ammo > 0) line.Add(cs::Cat(name, " ", ammo, ready ? cs::Cat(" (", key, ")") : cs::String(" (cycling)")));
	}

	double Hud::RoundSpeed(const cs::Ref<ShipCombat>& c)
	{
		double speed = 0;
		for (const auto& s : c->Subsystems)
		{
			auto w = s->Def->Weapon;
			// The guns the player lays down the nose: those whose fire cones take in dead ahead (U.SPC-T533).
			if (s->Destroyed() || w == nullptr || w->Missile || w->AntiSmall || !s->Def->Covers(c->Spec, FireCones::Bow)) continue;
			speed = cs::Math::Max(speed, w->MuzzleSpeedKmS);
		}
		return speed;
	}

	cs::Ref<BattleUnit> Hud::NearestHostileToAim(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& me)
	{
		cs::Ref<BattleUnit> best = nullptr;
		double bestCos = cs::Math::Cos(20 * cs::Math::PI / 180);
		auto aim = app->PilotAimDirection();
		for (const auto& o : battle->Units)
		{
			if (o == me || !battle->Hostile(me, o)) continue;
			auto to = o->Pos - me->Pos;
			if (to.LengthSquared() < 1e-12) continue;
			double cos = DVec3::Dot(aim, to.Normalized());
			if (cos > bestCos)
			{
				bestCos = cos;
				best = o;
			}
		}
		return best;
	}

	void Hud::DrawGauge(Rect r, float fraction, Color color, const cs::String& caption)
	{
		fraction = Mathf::Clamp01(fraction);
		auto old = GUI::color;
		GUI::color = Color(0, 0, 0, 0.5f);
		GUI::DrawTexture(r, Icons::White());
		GUI::color = color;
		GUI::DrawTexture(Rect(r.x, r.y, r.width * fraction, r.height), Icons::White());
		GUI::color = old;
		DrawLabel(caption, Vector2(r.x, r.y - 17), color, true);
	}

	void Hud::DrawRing(Vector2 p, float radius, Color color)
	{
		auto old = GUI::color;
		GUI::color = color;
		for (int i = 0; i < 8; i++)
		{
			float a = i * Mathf::PI / 4;
			GUI::DrawTexture(Rect(p.x + Mathf::Cos(a) * radius - 1.5f, p.y + Mathf::Sin(a) * radius - 1.5f, 3, 3), Icons::White());
		}
		GUI::color = old;
	}

	cs::String Hud::RangeText(double km)
	{
		return Ranges::Readout(km); // US-T156: the one source comms reads its ranges from too
	}

	// ---------------------------------------------------------------- hyperlane rides (U.SPC-T41)

	void Hud::DrawLaneEffects()
	{
		if (Event::current().type != EventType::Repaint || !app->IsPiloting() || app->Travel->Active) return;
		float strength = app->LaneStreaks();
		if (strength > 0.01f)
		{
			auto centre = Vector2(Screen::width * 0.5f, Screen::height * 0.5f);
			Vector2 ahead;
			if (app->RidingLane() && DirectionToGui(app->PilotForward(), ahead)) centre = ahead;
			DrawStreaks(centre, strength, StreakCount, StreakColor, 0, 170.0f);
		}
		// Through a gate at the helm (U.SPC-T414): the gate's violet glow and its streaks held over the far side
		// while it streams in, space folding shut and open again with it (the view narrows, see HelmGateFold).
		float glow = app->HelmGateGlow();
		if (glow > 0.001f)
		{
			DrawGateLight(glow, GateGlowColor, 0.55f);
			DrawStreaks(Vector2(Screen::width * 0.5f, Screen::height * 0.5f), glow, WarpLineCount, WarpColor, 500, 320.0f);
		}
		if (app->GateFlash > 0.001f)
		{
			auto old = GUI::color;
			GUI::color = Color(0.78f, 0.93f, 1.0f, app->GateFlash * app->GateFlash * 0.85f);
			GUI::DrawTexture(Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height)), Icons::White());
			GUI::color = old;
		}
	}

	void Hud::DrawGateLight(float amount, Color color, float opacity)
	{
		if (amount < 0.001f) return;
		auto old = GUI::color;
		GUI::color = Color(color.r, color.g, color.b, amount * opacity);
		GUI::DrawTexture(Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height)), Icons::White());
		GUI::color = old;
	}

	float Hud::Frac(float x) { return x - Mathf::Floor(x); }

	void Hud::DrawStreaks(Vector2 centre, float strength, int count, Color tint, int seed, float maxLength)
	{
		float reach = Vector2(static_cast<float>(Screen::width), static_cast<float>(Screen::height)).magnitude() * 0.6f;
		float t = Time::unscaledTime;
		for (int i = 0; i < count; i++)
		{
			// Fixed per streak: its bearing, pace and starting point (a cheap hash of its index).
			int k = i + seed;
			float h1 = Frac(Mathf::Sin(k * 12.9898f) * 43758.55f);
			float h2 = Frac(Mathf::Sin(k * 78.233f) * 12345.67f);
			float h3 = Frac(Mathf::Sin(k * 37.719f) * 24634.63f);
			float angle = h1 * Mathf::PI * 2.0f;
			float phase = Frac(t * (0.35f + 1.6f * strength) * (0.6f + h2) + h3);
			auto dir = Vector2(Mathf::Cos(angle), Mathf::Sin(angle));
			float r = 30.0f + phase * phase * reach;
			float length = (8.0f + maxLength * strength) * (0.3f + phase);
			auto color = Color(tint.r, tint.g, tint.b, strength * phase * 0.75f);
			DrawLine(centre + dir * r, centre + dir * (r + length), color, 1.0f + 1.5f * phase);
		}
	}

	// ---------------------------------------------------------------- formatting

	cs::String Hud::TargetName(const Target& t)
	{
		return t.IsBody() ? (*app->Catalog())[t.Body]->Name : t.IsShip() ? t.Ship->Name : t.IsStructure() ? t.Structure->Name : t.IsRing() ? t.Ring->Name : cs::String();
	}

	cs::String Hud::KindName(const cs::Ref<BodyDefinition>& b)
	{
		// US-T123: a void station's drift rock, broken planetoid or sight says what it is.
		if (auto site = VoidSites::KindLabel(b); !site.empty()) return site;
		switch (b->Kind)
		{
		case BodyKind::Star:
		{
			if (b->Singularity) return "Artificial gravity generator";
			cs::String kind = b->TeffK < 2400 ? "Brown dwarf" : b->RadiusKm < 0.05 * 695700 ? (b->TeffK >= 7500 ? "White dwarf" : "Tiny sun")
				: b->TeffK < 3900 ? "Red dwarf star" : b->TeffK < 5300 ? "Orange dwarf star" : b->TeffK < 7500 ? "Yellow dwarf star" : "White main-sequence star";
			return b->SpectralType.empty() ? kind : cs::Cat(kind, "  (", b->SpectralType, ")");
		}
		case BodyKind::DwarfPlanet: return "Dwarf planet";
		case BodyKind::Asteroid: return "Asteroid";
		case BodyKind::Moon: return "Moon";
		case BodyKind::Station: return "Free-flying station";
		case BodyKind::Barycentre: return "Barycentre";
		default:
		{
			if (b->GasGiant || b->Name == "Jupiter" || b->Name == "Saturn") return "Gas giant";
			if (b->Name == "Uranus" || b->Name == "Neptune") return "Ice giant";
			double earths = b->Gm / 398600.435;
			return earths >= 8 ? "Neptune-like planet" : earths >= 2 ? "Super-Earth" : "Terrestrial planet";
		}
		}
	}

	cs::String Hud::Km(double km)
	{
		if (km < 1) return cs::Cat(cs::Fmt(km * 1000, "N0"), " m");
		if (km < 1e7) return cs::Cat(cs::Fmt(km, "N0"), " km");
		return Au(km);
	}

	cs::String Hud::StellarDistance(double km)
	{
		return km < 1e6 ? Km(km) : km > 1000 * Astro::AuKm ? cs::Cat(cs::Fmt(km / Astro::AuKm, "N0"), " AU") : Au(km);
	}

	cs::String Hud::Au(double km)
	{
		return cs::Cat(cs::Fmt(km / Astro::AuKm, km / Astro::AuKm < 10 ? "0.000" : "0.00"), " AU");
	}

	cs::String Hud::LightTime(double km)
	{
		double s = km / Astro::LightSpeedKmPerSec;
		return s < 120 ? cs::Cat(cs::Fmt(s, "0"), " s") : s < 7200 ? cs::Cat(cs::Fmt(s / 60, "0.0"), " min")
			: s < 172800 ? cs::Cat(cs::Fmt(s / 3600, "0.0"), " h") : cs::Cat(cs::Fmt(s / 86400, "0.0"), " days");
	}

	cs::String Hud::Duration(double days)
	{
		if (days < 1.0 / 24) return cs::Cat(cs::Fmt(days * 1440, "0"), " min");
		if (days < 2) return cs::Cat(cs::Fmt(days * 24, "0.0"), " h");
		if (days < 400) return cs::Cat(cs::Fmt(days, "0.0"), " days");
		return cs::Cat(cs::Fmt(days / 365.25, "0.00"), " years");
	}

	cs::String Hud::SpeedText(double kmPerSec)
	{
		if (kmPerSec > Astro::LightSpeedKmPerSec * 0.5) return cs::Cat(cs::Fmt(kmPerSec / Astro::LightSpeedKmPerSec, "0.0"), " c");
		if (kmPerSec >= 1) return cs::Cat(cs::Fmt(kmPerSec, "N0"), " km/s");
		return cs::Cat(cs::Fmt(kmPerSec * 1000, "N0"), " m/s");
	}

	// ---------------------------------------------------------------- styles

	void Hud::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// US-T199: every style from the shared look (UiStyle.h): the panel framed with corner ticks over the live view,
		// sizes from the type scale, body text off-white, captions dim.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Hud);
		rowHoverTexture = S::Flat(S::RowHover, Color::clear());
		rowSelectedTexture = S::Flat(S::RowSelected, Color::clear());
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();

		panel = S::PanelStyle(panelTexture);

		title = S::TitleStyle(T::Title);

		text = S::TextStyle(T::Body);
		text->clipping = TextClipping::Clip;

		small = S::CaptionStyle(T::Caption);

		label = S::TextStyle(T::Label, S::Heading, FontStyle::Bold);

		minorLabel = S::TextStyle(T::Caption);

		labelShadow = cs::New<GUIStyle>(*label);
		labelShadow->normal.textColor = Color(0, 0, 0, 0.85f);

		button = S::ButtonStyle(T::Label, buttonTexture, buttonActiveTexture);
		buttonActive = S::ButtonOnStyle(button, buttonActiveTexture);

		row = S::RowStyle(T::Label, rowHoverTexture);
		rowSelected = S::RowSelectedStyle(row, rowSelectedTexture);

		toast = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
	}

	cs::Ref<Texture2D> Hud::Solid(Color fill, Color edge)
	{
		return UiStyle::Flat(fill, edge);
	}

	void Hud::DestroyOwnedTextures()
	{
		// UnityEngine.Object.Destroy(tex) -> dropping the reference (the last one releases the Unreal texture).
		panelTexture = rowHoverTexture = rowSelectedTexture = buttonTexture = buttonActiveTexture = nullptr;
		if (galaxyMap) galaxyMap->DestroyOwnedTextures();
		if (helpScreen) helpScreen->DestroyOwnedTextures();
	}
}
