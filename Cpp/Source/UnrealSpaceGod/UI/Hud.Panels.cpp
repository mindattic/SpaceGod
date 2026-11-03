// Port of Unity.SpaceGod's Runtime/UI/Hud.cs lines 1223-2382 (US-T31): between the stars (warp, lane rides,
// interdictions, crosshair and markers), opportunities (U.SPC-T44) and the panels (top bar, navigator, selection card).
// Members are declared in UI/Hud.Panels.inl.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "UI/Icons.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "DVec3.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Kepler.h"
#include "Lagrange.h"
#include "OrbitalElements.h"
#include "ScaleModel.h"
#include "ShipLod.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/FlightModels.h"
#include "Combat/PilotTuning.h"
#include "Combat/War.h"
#include "Economy/Companies.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Events/EventDirector.h"
#include "Events/Opportunity.h"
#include "Infrastructure/Infrastructure.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Sensors/Sensors.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/GateConstruction.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"
#include "Systems/StarSystemSpec.h" // HabitableZone

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	// ---------------------------------------------------------------- between the stars

	void Hud::DrawWarp()
	{
		auto travel = app->Travel;
		float warp = travel->Warp();
		auto screenCentre = Vector2(Screen::width * 0.5f, Screen::height * 0.5f);
		if (warp < 0.01f) { warpCentre = screenCentre; return; }
		// The lines pour from the star ahead once the view has turned to it, gliding there rather than jumping.
		Vector2 ahead;
		auto target = travel->Stage == Cameras::StarTravel::Leg::Cross && DirectionToGui(travel->Heading(), ahead) ? ahead : screenCentre;
		warpCentre = Vector2::Lerp(warpCentre, target, 1.0f - Mathf::Exp(-5.0f * Time::unscaledDeltaTime));
		auto centre = warpCentre;

		float reach = Vector2(static_cast<float>(Screen::width), static_cast<float>(Screen::height)).magnitude() * 0.7f;
		float t = Time::unscaledTime;
		auto old = GUI::color;
		for (int i = 0; i < DustCount; i++)
		{
			int k = i + 1000;
			float h1 = Frac(Mathf::Sin(k * 12.9898f) * 43758.55f);
			float h2 = Frac(Mathf::Sin(k * 78.233f) * 12345.67f);
			float h3 = Frac(Mathf::Sin(k * 37.719f) * 24634.63f);
			float angle = h1 * Mathf::PI * 2.0f;
			float phase = Frac(t * (0.06f + 0.45f * warp) * (0.5f + h2) + h3);
			// Cubed, so a mote creeps out of the distance and rushes past at the edge, as if coming nearer.
			float r = 6.0f + phase * phase * phase * reach;
			float size = 1.0f + 2.2f * phase;
			GUI::color = Color(DustColor.r, DustColor.g, DustColor.b, Mathf::Clamp01(warp * 1.6f) * Mathf::Min(1.0f, phase * 4.0f) * (0.2f + 0.45f * h3));
			auto p = centre + Vector2(Mathf::Cos(angle), Mathf::Sin(angle)) * r;
			GUI::DrawTexture(Rect(p.x - size * 0.5f, p.y - size * 0.5f, size, size), Icons::White());
		}
		GUI::color = old;
		DrawStreaks(centre, warp, WarpLineCount, WarpColor, 500, 320.0f);
	}

	void Hud::DrawLaneRide()
	{
		if (!app->RidingLane()) return;
		auto ship = app->Piloted.Ship;
		auto hop = ship->LaneLeg;
		double progress = Fleet::LaneProgress(ship, app->Clock->Jd);
		auto lanes = app->Fleet()->Lanes;
		// The ring chain flown through (U.SPC-T379): how many rings are behind the hull, and whether they are pushing it on or braking it.
		int passed = 0;
		bool accelerating = false;
		int rings = app->RideRingCount(passed, accelerating);
		cs::String head = cs::Cat(hop.Lane->IsGateSpur() ? "GATE SPUR  ·  " : "HYPERLANE  ·  ", cs::Strings::ToUpperInvariant(hop.Lane->Name),
			"  ·  bound for ", lanes->EndName(hop.Lane, hop.To),
			"  ·  ", Percent(progress), "  ·  ", SpeedText(app->PilotSpeedKmS()),
			"  ·  ring ", passed, "/", rings, accelerating ? " accelerating" : " braking",
			"  ·  off-ramp in ", Mathf::CeilToInt(static_cast<float>(app->RideSecondsLeft())), " s");
		cs::List<cs::String> exits;
		exits.Add(cs::Cat(app->RideChoice < 0 ? "> " : "", "1 come off at ", lanes->EndName(hop.Lane, hop.To)));
		for (int i = 0; i < app->RideOnward.Count() && i < 8; i++)
		{
			auto next = app->RideOnward[i];
			exits.Add(cs::Cat(app->RideChoice == i ? "> " : "", i + 2, " on to ", lanes->EndName(next.Lane, next.To)));
		}
		cs::String tail = cs::Cat("AT THE OFF-RAMP:  ", cs::Strings::Join("    ", exits), "    (P hands the ride to the crew)");
		auto a = GUIContent(head);
		auto b = GUIContent(tail);
		float w = Mathf::Max(text->CalcSize(a).x, small->CalcSize(b).x) + 28;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, Screen::height - 128, w, 50));
		GUI::contentColor = StreakColor;
		GUI::Label(Rect(r.x + 14, r.y + 5, w, 20), a, text);
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 14, r.y + 27, w, 18), b, small);
		GUI::contentColor = Color::white();
	}

	void Hud::DrawInterdiction()
	{
		if (!app->IsPiloting()) return;
		// C#'s null strings: empty here.
		cs::String head, tail;
		if (app->RidingLane() && app->RideThreat.has_value())
		{
			LaneThreat t = *app->RideThreat;
			auto ship = app->Piloted.Ship;
			head = cs::Cat("INTERDICTION FIELD AHEAD  ·  ", cs::Fmt(Mathf::RoundToInt(static_cast<float>(t.DistanceKm / 1000)), "N0"), " thousand km  ·  pulse in ",
				Mathf::CeilToInt(static_cast<float>(app->ThreatRealSeconds())), " s", ship->LaneSurged ? "  ·  SURGING" : "");
			tail = cs::Cat(t.Braced ? "a raider's hull made out lying dark: braced, the drives should hold    " : "",
				ship->LaneSurged ? cs::String() : cs::Cat("Shift surge through (", cs::Fmt(Fleet::SurgeFactor, "0.#"), "x pace)    "),
				"Ctrl drop out here    or ride on into it");
		}
		else if (app->PilotStunSeconds() > 0)
		{
			head = cs::Cat("EMP  ·  DRIVES AND SHIELDS DOWN  ·  ", Mathf::CeilToInt(static_cast<float>(app->PilotStunSeconds())), " s");
			tail = "knocked out by an electromagnetic pulse: drifting, no thrust, no turning, no guns until the systems come back";
		}
		else if (app->CanRejoinLane())
		{
			head = cs::Cat("BESIDE THE ", cs::Strings::ToUpperInvariant(app->Piloted.Ship->LaneLeg.Lane->Name), " LANE");
			auto leg = app->Piloted.Ship->LaneLeg;
			tail = cs::Cat("Y  back into the lane where you left it, on to ", app->Fleet()->Lanes->EndName(leg.Lane, leg.To));
		}
		if (head.empty()) return;
		auto a = GUIContent(head);
		auto b = GUIContent(tail);
		float w = Mathf::Max(text->CalcSize(a).x, small->CalcSize(b).x) + 28;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, Screen::height - 184, w, 50));
		GUI::contentColor = app->CanRejoinLane() && !app->RidingLane() && app->PilotStunSeconds() <= 0 ? StreakColor : InterdictColor;
		GUI::Label(Rect(r.x + 14, r.y + 5, w, 20), a, text);
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 14, r.y + 27, w, 18), b, small);
		GUI::contentColor = Color::white();
	}

	bool Hud::DirectionToGui(DVec3 direction, Vector2& p)
	{
		auto d = direction.SwapYZ();
		if (!app->RelativeToGui(Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z)) * 1e6f, p)) return false;
		return p.x >= 0 && p.y >= 0 && p.x <= Screen::width && p.y <= Screen::height;
	}

	cs::String Hud::TelegraphText(EngineTelegraph t)
	{
		return t == EngineTelegraph::FullAstern ? "FULL ASTERN"
			: t == EngineTelegraph::Stop ? "STOP"
			: t == EngineTelegraph::Slow ? "SLOW AHEAD"
			: t == EngineTelegraph::Half ? "HALF AHEAD"
			: t == EngineTelegraph::Full ? "FULL AHEAD"
			: "FLANK";
	}

	cs::String Hud::ControlHint(PilotControl control)
	{
		return control == PilotControl::FreeAim ? "WASD throttle/strafe, mouse aims, the hull turns after it"
			: control == PilotControl::Helm ? "W/S ring the engine telegraph, A/D Q/E turn the hull, Z/X roll, mouse looks around, 1-9 0 captain's orders"
			: "WASD throttle/strafe, mouse steers the nose";
	}

	float Hud::SafeLeft() { return (Screen::width - SafeWidth()) * 0.5f; }
	float Hud::SafeRight() { return SafeLeft() + SafeWidth(); }
	float Hud::SafeWidth() { return Mathf::Min(static_cast<float>(Screen::width), Screen::height * MaxHudAspect); }

	void Hud::DrawCrosshair(Vector2 p, Color color)
	{
		auto old = GUI::color;
		GUI::color = color;
		const auto& w = Icons::White();
		GUI::DrawTexture(Rect(p.x - 16, p.y - 1, 9, 2), w);
		GUI::DrawTexture(Rect(p.x + 7, p.y - 1, 9, 2), w);
		GUI::DrawTexture(Rect(p.x - 1, p.y - 16, 2, 9), w);
		GUI::DrawTexture(Rect(p.x - 1, p.y + 7, 2, 9), w);
		GUI::color = old;
	}

	void Hud::DrawBoresight(Vector2 p, Color color)
	{
		auto old = GUI::color;
		GUI::color = color;
		const auto& w = Icons::White();
		GUI::DrawTexture(Rect(p.x - 12, p.y - 0.75f, 8, 1.5f), w);
		GUI::DrawTexture(Rect(p.x + 4, p.y - 0.75f, 8, 1.5f), w);
		GUI::DrawTexture(Rect(p.x - 0.75f, p.y + 4, 1.5f, 6), w);
		GUI::DrawTexture(Rect(p.x - 1, p.y - 1, 2, 2), w);
		GUI::color = old;
	}

	void Hud::DrawLine(Vector2 a, Vector2 b, Color color, float thickness)
	{
		auto d = b - a;
		float length = d.magnitude();
		if (length < 1) return;
		auto matrix = GUI::matrix;
		auto old = GUI::color;
		GUIUtility::RotateAroundPivot(Mathf::Atan2(d.y, d.x) * Mathf::Rad2Deg, a);
		GUI::color = color;
		GUI::DrawTexture(Rect(a.x, a.y - thickness * 0.5f, length, thickness), Icons::White());
		GUI::color = old;
		GUI::matrix = matrix;
	}

	void Hud::DrawTargetMarker(const Target& t, bool selected)
	{
		if (t.IsNone()) return;
		auto scene = app->TargetScene(t);
		// A lost contact is bracketed on its question mark, where the scanners last had it.
		if (t.IsShip())
		{
			auto picture = Picture();
			cs::Ref<Contact> lost = picture ? picture->Of(t.Ship) : nullptr;
			if (lost && lost->Cold() && !lastSeenScene.TryGetValue(t.Ship, scene)) return;
		}
		double radius = app->TargetRadius(t);
		Vector2 p;
		float depth = 0;
		bool onScreen = app->ToGui(scene, p, depth) &&
			p.x >= 0 && p.x <= Screen::width && p.y >= 0 && p.y <= Screen::height;
		if (!onScreen)
		{
			if (selected) DrawOffscreenPointer(scene, TargetName(t));
			return;
		}
		float px = static_cast<float>(radius / depth * app->PixelsPerUnitAtUnitDistance());
		float half = Mathf::Max(px * 1.18f, 13.0f);
		auto color = selected ? Accent : Color(1, 1, 1, 0.55f);
		DrawBrackets(Rect(p.x - half, p.y - half, half * 2, half * 2), color, selected ? 2.0f : 1.0f);
	}

	// ---------------------------------------------------------------- opportunities (U.SPC-T44)

	Color Hud::OpportunityColor(const cs::Ref<Opportunity>& op, float alpha)
	{
		auto c = op->MarkerColor();
		return Color(c.R, c.G, c.B, alpha);
	}

	void Hud::DrawOpportunityMarkers()
	{
		auto director = app->World()->Adventures;
		if (director == nullptr || director->Active.Count() == 0) return;
		auto player = app->PlayerHelioKm();
		for (const auto& op : director->Active)
		{
			DVec3 scene;
			Vector2 p;
			float unusedDepth = 0;
			if (!app->OpportunityScene(op, scene) || !app->ToGui(scene, p, unusedDepth)) continue;
			if (p.x < 0 || p.x > Screen::width || p.y < 0 || p.y > Screen::height) continue;
			auto color = OpportunityColor(op, 0.95f);
			DrawBrackets(Rect(p.x - 11, p.y - 11, 22, 22), color, 1.5f);
			DrawLabel(cs::Cat(op->MarkerLabel(), "  ", Km((op->HelioKm - player).Length())), p + Vector2(14, 6), color, true);
		}
	}

	void Hud::DrawOpportunityBoard()
	{
		auto director = app->World()->Adventures;
		if (director == nullptr || director->Active.Count() == 0) return;
		cs::List<cs::Ref<Opportunity>> list = director->Active;
		double jd = app->Clock->Jd;
		list.Sort([](const cs::Ref<Opportunity>& a, const cs::Ref<Opportunity>& b) { return cs::CompareTo(a->ExpiresJd, b->ExpiresJd); });
		int n = Mathf::Min(5, list.Count());
		float w = 380, rowH = 40;
		auto r = Panel(Rect(SafeRight() - w - 10, 72, w, 30 + n * rowH));
		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 18), cs::Cat("OPPORTUNITIES", app->IsPiloting() ? "" : "   (pilot a ship to answer)"), small);
		auto player = app->PlayerHelioKm();
		auto old = GUI::contentColor;
		if (!opportunityHook)
		{
			opportunityHook = cs::New<GUIStyle>(*small);
			opportunityHook->wordWrap = true;
			opportunityHook->clipping = TextClipping::Clip;
		}
		for (int i = 0; i < n; i++)
		{
			auto op = list[i];
			float y = r.y + 26 + i * rowH;
			GUI::contentColor = OpportunityColor(op);
			GUI::Label(Rect(r.x + 12, y, w - 24, 16), cs::Cat(op->MarkerLabel(), "   ", op->Title, "   ", Km((op->HelioKm - player).Length()), "   ",
				Duration(op->DaysLeft(jd)), " left", op->Engaged ? "   · on scene" : ""), small);
			GUI::contentColor = Dim;
			GUI::Label(Rect(r.x + 12, y + 15, w - 24, rowH - 16), op->Hook, opportunityHook);
		}
		GUI::contentColor = old;
	}

	void Hud::DrawBrackets(Rect r, Color color, float thickness)
	{
		float len = Mathf::Clamp(r.width * 0.28f, 5.0f, 18.0f);
		auto old = GUI::color;
		GUI::color = color;
		const auto& w = Icons::White();
		GUI::DrawTexture(Rect(r.xMin(), r.yMin(), len, thickness), w);
		GUI::DrawTexture(Rect(r.xMin(), r.yMin(), thickness, len), w);
		GUI::DrawTexture(Rect(r.xMax() - len, r.yMin(), len, thickness), w);
		GUI::DrawTexture(Rect(r.xMax() - thickness, r.yMin(), thickness, len), w);
		GUI::DrawTexture(Rect(r.xMin(), r.yMax() - thickness, len, thickness), w);
		GUI::DrawTexture(Rect(r.xMin(), r.yMax() - len, thickness, len), w);
		GUI::DrawTexture(Rect(r.xMax() - len, r.yMax() - thickness, len, thickness), w);
		GUI::DrawTexture(Rect(r.xMax() - thickness, r.yMax() - len, thickness, len), w);
		GUI::color = old;
	}

	void Hud::DrawOffscreenPointer(DVec3 scene, const cs::String& name)
	{
		auto rel = app->Relative(scene);
		auto local = Quaternion::Inverse(app->SpaceCamera->Rotation) * rel;
		auto dir = Vector2(local.x, -local.y);
		if (dir.sqrMagnitude() < 1e-12f) return;
		dir = dir.normalized(); // Vector2.Normalize()
		auto center = Vector2(Screen::width * 0.5f, Screen::height * 0.5f);
		float margin = 34.0f;
		float sx = (center.x - margin) / Mathf::Max(Mathf::Abs(dir.x), 1e-4f);
		float sy = (center.y - margin) / Mathf::Max(Mathf::Abs(dir.y), 1e-4f);
		auto p = center + dir * Mathf::Min(sx, sy);

		float angle = Mathf::Atan2(dir.y, dir.x) * Mathf::Rad2Deg + 90.0f;
		auto matrix = GUI::matrix;
		GUIUtility::RotateAroundPivot(angle, p);
		auto old = GUI::color;
		GUI::color = Accent;
		GUI::DrawTexture(Rect(p.x - 9, p.y - 9, 18, 18), Icons::ForShip(ShipClass::Cruiser));
		GUI::color = old;
		GUI::matrix = matrix;
		DrawLabel(name, p + Vector2(dir.x < 0 ? 14 : -14 - label->CalcSize(GUIContent(name)).x, dir.y < 0 ? 10.0f : -26.0f), Accent, false);
	}

	void Hud::DrawIcon(const cs::Ref<Texture2D>& icon, Vector2 p, float size, Color tint)
	{
		auto old = GUI::color;
		GUI::color = Color(0, 0, 0, tint.a * 0.6f);
		GUI::DrawTexture(Rect(p.x - size * 0.5f + 1, p.y - size * 0.5f + 1, size, size), icon);
		GUI::color = tint;
		GUI::DrawTexture(Rect(p.x - size * 0.5f, p.y - size * 0.5f, size, size), icon);
		GUI::color = old;
	}

	void Hud::DrawLabel(const cs::String& s, Vector2 p, Color color, bool minor)
	{
		auto style = minor ? minorLabel : label;
		auto content = GUIContent(s);
		auto size = style->CalcSize(content);
		auto r = Rect(p.x, p.y, size.x, size.y);
		labelShadow->fontSize = style->fontSize;
		GUI::Label(Rect(r.x + 1, r.y + 1, r.width, r.height), content, labelShadow);
		auto old = GUI::contentColor;
		GUI::contentColor = minor ? Color::Lerp(color, Color::gray(), 0.3f) : color;
		GUI::Label(r, content, style);
		GUI::contentColor = old;
	}

	Color Hud::BodyTint(const cs::Ref<BodyDefinition>& b)
	{
		auto c = Color(b->Color.R, b->Color.G, b->Color.B);
		// (The C# local is `max`, renamed for the Windows macro, MIGRATION.md rule 10.)
		float maxC = Mathf::Max(c.r, Mathf::Max(c.g, c.b));
		// Unity's Color / float divides all four channels, alpha included.
		c = maxC > 0 ? Color(c.r / maxC, c.g / maxC, c.b / maxC, c.a / maxC) : Color::white();
		return Color::Lerp(c, Color::white(), 0.35f);
	}

	// ---------------------------------------------------------------- panels

	Rect Hud::Panel(Rect r)
	{
		GUI::Box(r, GUIContent::none(), panel);
		if (Event::current().type == EventType::Layout || Event::current().type == EventType::Repaint) RegisterUi(r);
		return r;
	}

	void Hud::RegisterUi(Rect r)
	{
		if (!app->UiRects.Contains(r)) app->UiRects.Add(r);
	}

	void Hud::DrawTopBar()
	{
		float w = 640, h = 52;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, 10, w, h));
		auto clock = app->Clock;

		GUI::Label(Rect(r.x + 14, r.y + 7, 220, 20), cs::Cat(clock->Utc().ToString("yyyy-MM-dd  HH:mm"), " UTC"), title);
		cs::String rate = clock->Paused() ? cs::String("PAUSED")
			: clock->Pausing() ? cs::Cat("PAUSING  x", cs::Fmt(clock->Speed, "0.00"))
			: clock->Resuming() ? cs::Cat("RESUMING  x", cs::Fmt(clock->Speed, "0.00"), "  of ", clock->Warp().Label)
			: clock->Warp().Label;
		GUI::Label(Rect(r.x + 14, r.y + 28, 220, 18), rate, small);

		float bx = r.x + 200;
		if (GUI::Button(Rect(bx, r.y + 12, 34, 28), "<<", button)) clock->Slower();
		if (GUI::Button(Rect(bx + 38, r.y + 12, 44, 28), clock->PauseHeld() ? ">" : "||", clock->PauseHeld() ? buttonActive : button)) app->TogglePause();
		if (GUI::Button(Rect(bx + 86, r.y + 12, 34, 28), ">>", button)) app->TimeFaster();
		if (GUI::Button(Rect(bx + 124, r.y + 12, 44, 28), "Now", button)) app->ResetToNow();

		float sx = r.x + 390;
		bool gameplay = app->ScaleGoal > 0.5;
		if (GUI::Button(Rect(sx, r.y + 9, 236, 22), gameplay ? "GAMEPLAY SCALE   [G]" : "REALISTIC SCALE   [G]", buttonActive))
			app->ToggleScale();
		auto bar = Rect(sx, r.y + 36, 236, 6);
		GUI::DrawTexture(bar, buttonTexture);
		auto old = GUI::color;
		GUI::color = Accent;
		float blend = static_cast<float>(app->Scale()->Blend());
		GUI::DrawTexture(Rect(bar.x, bar.y, bar.width * blend, bar.height), Icons::White());
		GUI::color = old;
		GUI::Label(Rect(sx - 2, r.y + 40, 120, 14), "real", small);
		auto g = GUIContent("gameplay");
		GUI::Label(Rect(sx + 238 - small->CalcSize(g).x, r.y + 40, 120, 14), g, small);
	}

	void Hud::DrawNavigator()
	{
		float w = 268;
		float h = navigatorOpen ? Mathf::Min(Screen::height - 24, 640) : 34;
		auto r = Panel(Rect(SafeLeft() + 10, 10, w, h));
		if (GUI::Button(Rect(r.x + 8, r.y + 6, w - 16, 22), cs::Cat(navigatorOpen ? "[-]  " : "[+]  ", "NAVIGATOR"), title)) navigatorOpen = !navigatorOpen;
		if (!navigatorOpen) return;

		// Star systems: one button each; the active one is where the body tree below belongs.
		float sy = r.y + 34;
		auto oldColor = GUI::contentColor;
		GUI::contentColor = Accent;
		GUI::Label(Rect(r.x + 12, sy, w - 24, 16), "STAR SYSTEMS", small);
		GUI::contentColor = oldColor;
		// The whole neighbourhood at once: territory, traffic, fronts and raiders, as far as the news goes (U.SPC-T103).
		if (GUI::Button(Rect(r.x + w - 126, sy - 2, 116, 18), "GALAXY MAP   [", button)) app->ShowGalaxyMap = true;
		sy += 17;
		auto network = app->Galaxy->Network;
		double jd = app->Clock->Jd;
		for (const auto& system : app->Galaxy->Systems)
		{
			bool here = system == app->ActiveSystem;
			bool going = app->Travel->Active && app->Travel->To == system;
			// How many gates away it is, by the quickest open way (a gate network, not all-pairs).
			auto route = here ? nullptr : network->Route(app->ActiveSystem, system, nullptr, jd);
			cs::String hops = route == nullptr ? cs::String() : cs::Cat("  ", route->Count(), route->Count() == 1 ? " gate" : " gates");
			cs::String details = here ? cs::String("◉ here") : going ? cs::String("travelling…")
				: cs::Cat(cs::Fmt(Galaxy::DistanceLy(app->ActiveSystem, system), "0.00"), " ly", hops, NewsAge(system, jd), "  ›");
			// Rows tighten as the list of systems grows, so the body tree below keeps its room.
			float rowHeight = app->Galaxy->Systems.Count() > 4 ? 18 : 22;
			auto systemRow = Rect(r.x + 10, sy, w - 20, rowHeight);
			if (GUI::Button(systemRow, GUIContent::none(), here ? buttonActive : button) && !here) app->TravelTo(system);
			NameAndDetails(systemRow, system->Name, details, here ? buttonActive : button);
			sy += rowHeight + (app->Galaxy->Systems.Count() > 4 ? 2 : 3);
			if (!here) continue;
			// Who holds it (U.SPC-T91): the polity owning its biggest settlement, which may hold other systems too.
			auto holder = system->Holder();
			if (holder != nullptr)
			{
				auto old = GUI::contentColor;
				GUI::contentColor = Color(holder->Color.R, holder->Color.G, holder->Color.B);
				NameAndDetails(Rect(r.x + 12, sy, w - 24, 15), "   Held by", holder->Name, small);
				GUI::contentColor = old;
				sy += 15;
			}
			// This system's own gates: where each leads, how long the crossing, and whether it is shut.
			for (const auto& gate : system->Gates())
			{
				bool shut = gate->Blockaded(jd) || gate->Partner->Blockaded(jd);
				bool down = !gate->Link->Working();
				cs::String line = cs::Cat(cs::Fmt(gate->Link->Ly, "0.00"), " ly, ",
					cs::Fmt(gate->Link->TransitDays() * 24, "0"), " h", down ? "   DOWN" : shut ? "   SHUT" : "",
					gate->Integrity < 1 ? cs::Cat("  ", cs::Fmt(gate->Integrity * 100, "0"), "% whole") : cs::String());
				auto old = GUI::contentColor;
				GUI::contentColor = down || shut ? Color(1.0f, 0.5f, 0.42f) : Color(0.7f, 0.82f, 0.95f);
				NameAndDetails(Rect(r.x + 12, sy, w - 24, 15), cs::Cat("   ", gate->Name), line, small);
				GUI::contentColor = old;
				sy += 15;
			}
			// Gates being surveyed, built or rebuilt here (U.SPC-T97), and how far along.
			if (app->Galaxy->Construction != nullptr)
				for (const auto& p : app->Galaxy->Construction->Active())
				{
					if (p->From != system) continue;
					cs::String what = p->Work == GateJob::Repair ? cs::Cat("rebuilding the ", p->Gate->Name)
						: p->Stage == GateStage::Survey ? cs::Cat("surveying for a gate to ", p->TargetName)
						: cs::Cat("building a gate to ", p->TargetName);
					cs::String line = cs::Cat(what,
						p->Stage == GateStage::Building ? cs::Cat("  ", cs::Fmt(p->Progress() * 100, "0"), "%") : cs::String());
					auto old = GUI::contentColor;
					GUI::contentColor = Color(0.85f, 0.8f, 0.55f);
					NameAndDetails(Rect(r.x + 12, sy, w - 24, 15), cs::Cat("   ", p->Builder->Name), line, small);
					GUI::contentColor = old;
					sy += 15;
				}
		}
		int crossing = app->Galaxy->Traffic->InGates().Count();
		if (crossing > 0)
		{
			NameAndDetails(Rect(r.x + 12, sy, w - 24, 15), cs::Cat("   ", crossing, crossing == 1 ? " ship" : " ships"), "in the gates", small);
			sy += 15;
		}
		// Groups out in open space under their own drives (U.SPC-T412).
		int cruising = app->Galaxy->DeepSpace->Cruises().Count();
		if (cruising > 0)
		{
			NameAndDetails(Rect(r.x + 12, sy, w - 24, 15), cs::Cat("   ", cruising, cruising == 1 ? " group" : " groups"), "between the stars", small);
			sy += 15;
		}
		float treeTop = sy + 4 - r.y;

		auto catalog = app->Catalog();
		cs::List<NavigatorRow> rows;
		auto war = app->World()->War;
		if (war->Battles.Count() > 0)
		{
			rows.Add(NavigatorRow{ "BATTLES", "", Target::None(), 0, Color(1.0f, 0.5f, 0.4f) });
			for (const auto& b : war->Battles)
			{
				auto flag = Flagship(b);
				rows.Add(NavigatorRow{ b->Name, BattleScore(b), flag != nullptr ? Target::OfShip(flag) : Target::OfBody(b->Body), 1, Color(1.0f, 0.72f, 0.6f) });
			}
			rows.Add(NavigatorRow{ "", "", Target::None(), 0, Color::clear() });
		}
		AddBody(rows, 0, 0);
		for (const auto& faction : app->Fleet()->Factions())
		{
			rows.Add(NavigatorRow{ "", "", Target::None(), 0, Color::clear() });
			auto fc = Color(faction->Color.R, faction->Color.G, faction->Color.B);
			rows.Add(NavigatorRow{ cs::Strings::ToUpperInvariant(faction->Name), Credits(faction->Treasury), Target::None(), 0, fc });
			cs::String polity = PolityText(faction);
			int cut = cs::Strings::IndexOf(polity, ",  ");
			rows.Add(NavigatorRow{ cut < 0 ? polity : cs::Strings::Substring(polity, 0, cut), cut < 0 ? cs::String() : cs::Strings::Substring(polity, cut + 3), Target::None(), 1, Color::Lerp(fc, Color::gray(), 0.35f) });
			for (const auto& ship : app->Fleet()->Ships())
			{
				if (ship->Faction != faction || ship->IsEscort() || ship->Destroyed) continue;
				rows.Add(NavigatorRow{ ship->Name, ShortStatus(ship), Target::OfShip(ship), 1, fc });
				for (const auto& escort : app->Fleet()->Ships())
					if (escort->Leader == ship && !escort->Destroyed) rows.Add(NavigatorRow{ escort->Name, cs::Cat("· ", escort->Spec->DisplayName), Target::OfShip(escort), 2, Color::Lerp(fc, Color::gray(), 0.35f) });
			}
		}
		// The trading companies that own the merchant fleet, in their liveries.
		auto companies = app->World()->Companies;
		if (companies != nullptr && companies->Enabled())
		{
			rows.Add(NavigatorRow{ "", "", Target::None(), 0, Color::clear() });
			rows.Add(NavigatorRow{ "TRADING COMPANIES", "", Target::None(), 0, Color(0.9f, 0.86f, 0.7f) });
			for (const auto& co : companies->All)
			{
				auto lc = Color(co->Livery.R, co->Livery.G, co->Livery.B);
				if (!co->Trading()) lc = Color::Lerp(lc, Color::gray(), 0.6f);
				rows.Add(NavigatorRow{ co->Name, cs::Strings::TrimStart(cs::Strings::Substring(CompanyText(co), static_cast<int>(co->Name.size()))), Target::None(), 1, lc });
			}
		}
		// The player's own fleet: its house flag is no faction of the system's, but its ships are no visitors.
		auto holdings = app->World()->Holdings;
		cs::Ref<Faction> house = holdings ? holdings->House : nullptr;
		bool houseHeader = false;
		if (house != nullptr)
			for (const auto& ship : app->Fleet()->Ships())
			{
				if (ship->IsEscort() || ship->Destroyed || ship->Faction != house) continue;
				if (!houseHeader)
				{
					rows.Add(NavigatorRow{ "", "", Target::None(), 0, Color::clear() });
					rows.Add(NavigatorRow{ "YOUR FLEET", "", Target::None(), 0, Color(0.85f, 0.95f, 0.7f) });
					houseHeader = true;
				}
				auto hc = Color(house->Color.R, house->Color.G, house->Color.B);
				rows.Add(NavigatorRow{ ship->Name, ShortStatus(ship), Target::OfShip(ship), 1, hc });
			}
		// Ships from other systems, come through the gates (their factions belong to their own worlds).
		bool visitorsHeader = false;
		cs::HashSet<cs::Ref<Faction>> local(app->Fleet()->Factions());
		if (house != nullptr) local.Add(house);
		for (const auto& ship : app->Fleet()->Ships())
		{
			if (ship->IsEscort() || ship->Destroyed || local.Contains(ship->Faction)) continue;
			if (!visitorsHeader)
			{
				rows.Add(NavigatorRow{ "", "", Target::None(), 0, Color::clear() });
				rows.Add(NavigatorRow{ "FROM BEYOND THE GATES", "", Target::None(), 0, Color(0.7f, 0.82f, 0.95f) });
				visitorsHeader = true;
			}
			auto fc = Color(ship->Faction->Color.R, ship->Faction->Color.G, ship->Faction->Color.B);
			cs::String from = ship->Voyage != nullptr ? cs::Cat("· ", ship->Voyage->Home->Name, "  ") : cs::String();
			rows.Add(NavigatorRow{ ship->Name, cs::Cat(from, ShortStatus(ship)), Target::OfShip(ship), 1, fc });
		}

		// Docking rings and what is in orbit, after the bodies.
		auto infra = app->World()->Infrastructure;
		if (infra != nullptr && infra->Rings.Count() > 0)
		{
			rows.Add(NavigatorRow{ "", "", Target::None(), 0, Color::clear() });
			rows.Add(NavigatorRow{ "DOCKING RINGS", "", Target::None(), 0, Color(0.6f, 0.9f, 1.0f) });
			for (const auto& ring : infra->Rings)
			{
				int queued = 0;
				for (const auto& s : app->Fleet()->Ships())
					if (!s->Destroyed && s->Phase == ShipPhase::Queued && s->Ring == ring) queued++;
				rows.Add(NavigatorRow{ ring->Name, cs::Cat("· ", (*catalog)[ring->Body]->Name, queued > 0 ? cs::Cat("  · ", queued, " queued") : cs::String()), Target::OfRing(ring), 1, Color(0.6f, 0.9f, 1.0f) });
			}
		}
		if (infra != nullptr && infra->Structures.Count() > 0)
		{
			rows.Add(NavigatorRow{ "", "", Target::None(), 0, Color::clear() });
			rows.Add(NavigatorRow{ "IN ORBIT", "", Target::None(), 0, Color(0.85f, 0.88f, 0.92f) });
			for (const auto& s : infra->Structures)
			{
				auto c = s->Owner() != nullptr ? Color(s->Owner()->Color.R, s->Owner()->Color.G, s->Owner()->Color.B) : Color(0.85f, 0.88f, 0.92f);
				rows.Add(NavigatorRow{ s->Name, cs::Cat("· ", (*catalog)[s->Body]->Name), Target::OfStructure(s), 1, c });
			}
		}

		constexpr float rowH = 19;
		auto view = Rect(r.x + 4, r.y + treeTop, w - 8, h - treeTop - 6);
		auto content = Rect(0, 0, w - 26, rows.Count() * rowH);
		navigatorScroll = GUI::BeginScrollView(view, navigatorScroll, content);
		for (int i = 0; i < rows.Count(); i++)
		{
			const auto& [s, details, target, indent, color] = rows[i];
			auto rr = Rect(0, i * rowH, content.width, rowH);
			if (target.IsNone())
			{
				if (s.empty()) continue;
				auto oldC = GUI::contentColor;
				GUI::contentColor = color;
				NameAndDetails(Rect(rr.x + 6, rr.y + 2, rr.width - 6, rr.height - 2), s, details, small);
				GUI::contentColor = oldC;
				continue;
			}
			bool selected = app->Selected == target;
			auto old = GUI::contentColor;
			GUI::contentColor = selected ? Color::white() : color;
			auto rowRect = Rect(rr.x + 4 + indent * 14, rr.y, rr.width - indent * 14 - 4, rr.height);
			if (GUI::Button(rowRect, GUIContent::none(), selected ? rowSelected : row))
			{
				bool doubleClick = lastRowTarget == target && Time::unscaledTime - lastRowClick < 0.4f;
				app->Select(target);
				if (doubleClick && !app->IsPiloting()) app->SpaceCamera->FocusOn(target);
				lastRowClick = Time::unscaledTime;
				lastRowTarget = target;
			}
			NameAndDetails(rowRect, s, details, selected ? rowSelected : row);
			GUI::contentColor = old;
		}
		GUI::EndScrollView();
	}

	cs::Ref<GUIStyle> Hud::TextOnly(const cs::Ref<GUIStyle>& style)
	{
		cs::Ref<GUIStyle> t;
		if (textOnly.TryGetValue(style, t)) return t;
		auto colour = style->normal.textColor;
		t = cs::New<GUIStyle>(*style);
		t->normal.background = nullptr;
		t->normal.textColor = colour;
		t->hover.background = nullptr;
		t->hover.textColor = colour;
		t->active.background = nullptr;
		t->active.textColor = colour;
		t->focused.background = nullptr;
		t->focused.textColor = colour;
		t->padding = RectOffset(0, 0, style->padding.top, style->padding.bottom);
		t->margin = RectOffset();
		t->wordWrap = false;
		t->clipping = TextClipping::Overflow;
		t->alignment = style->alignment == TextAnchor::MiddleCenter || style->alignment == TextAnchor::MiddleRight ? TextAnchor::MiddleLeft
			: style->alignment == TextAnchor::UpperCenter || style->alignment == TextAnchor::UpperRight ? TextAnchor::UpperLeft
			: style->alignment;
		textOnly[style] = t;
		return t;
	}

	void Hud::NameAndDetails(Rect r, const cs::String& name, const cs::String& details, const cs::Ref<GUIStyle>& style)
	{
		auto t = TextOnly(style);
		float left = r.x + style->padding.left, right = r.xMax() - Mathf::Max(style->padding.right, 2);
		float nameW = Mathf::Min(t->CalcSize(GUIContent(name)).x, right - left);
		GUI::BeginGroup(Rect(left, r.y, nameW, r.height));
		GUI::Label(Rect(0, 0, nameW + 40, r.height), name, t);
		GUI::EndGroup();
		if (details.empty()) return;
		auto area = Rect(left + nameW + 8, r.y, right - left - nameW - 8, r.height);
		if (area.width < 12) return;
		auto content = GUIContent(details);
		float dw = t->CalcSize(content).x;
		float offset = 0, loop = dw + MarqueeGap;
		if (dw > area.width)
		{
			float phase = cs::Mod(Time::unscaledTime, MarqueeHold + loop / MarqueeSpeed);
			offset = phase < MarqueeHold ? 0 : (phase - MarqueeHold) * MarqueeSpeed;
		}
		GUI::BeginGroup(area);
		GUI::Label(Rect(-offset, 0, dw + 2, r.height), content, t);
		if (dw > area.width) GUI::Label(Rect(loop - offset, 0, dw + 2, r.height), content, t);
		GUI::EndGroup();
	}

	void Hud::AddBody(cs::List<NavigatorRow>& rows, int index, int indent)
	{
		auto b = (*app->Catalog())[index];
		auto tint = BodyTint(b);
		if (!b->IsVisible())
		{
			// A barycentre heads the tree as a title, not a selectable body.
			rows.Add(NavigatorRow{ cs::Strings::ToUpperInvariant(b->Name), "(barycentre)", Target::None(), 0, Dim });
			for (int child : app->Catalog()->ChildrenOf(index)) AddBody(rows, child, indent);
			return;
		}
		rows.Add(NavigatorRow{ b->Name, cs::Strings::Trim(StatusMark(b)), Target::OfBody(index), indent, b->Kind == BodyKind::Moon ? Color::Lerp(tint, Color::gray(), 0.3f) : tint });
		for (int child : app->Catalog()->ChildrenOf(index)) AddBody(rows, child, cs::Math::Min(indent + 1, 2));
	}

	cs::Ref<Ship> Hud::Flagship(const cs::Ref<Battle>& b)
	{
		cs::Ref<Ship> best = nullptr;
		for (const auto& u : b->Units)
			if (u->Ship != nullptr && u->Present() && (best == nullptr || u->Ship->Combat->Spec->Hull > best->Combat->Spec->Hull)) best = u->Ship;
		return best;
	}

	cs::String Hud::BattleScore(const cs::Ref<Battle>& b)
	{
		int a = 0, d = 0;
		for (const auto& u : b->Units)
		{
			if (!u->Present() || u->Kind == UnitKind::Fighter) continue;
			if (u->Side == 0) a++;
			else d++;
		}
		return cs::Cat(a, " v ", d);
	}

	cs::String Hud::ShortStatus(const cs::Ref<Ship>& ship)
	{
		if (ship->Battle != nullptr) return cs::Cat("IN BATTLE  ", ship->Battle->Name);
		auto c = ship->Controller();
		if (ship->Derelict) return "DERELICT";
		if (c->Phase == ShipPhase::Lurk && c->Stalled) return cs::Cat("stalled on ", c->LaneLeg.Lane->Name);
		if (c->Phase == ShipPhase::Lurk) return cs::Cat(c->Faction != nullptr && !c->Faction->IsPirate ? "lane picket on " : "lurking on ", c->LaneLeg.Lane->Name);
		if (c->Phase == ShipPhase::Adrift) return cs::Cat("adrift by ", c->LaneLeg.Lane->Name);
		if (c->GateKm.has_value())
		{
			DVec3 gateKm = *c->GateKm;
			// Bound for a jump gate: name it, and the system beyond.
			// A cruise voyage's GateKm is the edge of the well, not a gate (U.SPC-T576).
			bool cruiseOut = c->Voyage != nullptr && c->Voyage->ByCruise;
			cs::String gate = cruiseOut ? "the edge of the well" : "a jump gate";
			if (!cruiseOut)
				for (const auto& g : app->ActiveSystem->Gates())
					if ((g->PositionKm - gateKm).Length() < 1) gate = cs::Cat("the ", g->Name);
			if (c->Voyage != nullptr && c->Voyage->AtGate) return cs::Cat("at ", gate);
			if (c->Phase == ShipPhase::Transit) return cs::Cat("-> ", gate);
		}
		if (c->Phase == ShipPhase::Parked) return cs::Cat(c->Role == ShipRole::Free ? "drifting @ " : "@ ", (*app->Catalog())[c->Orbit->Body]->Name);
		if (c->Phase == ShipPhase::Transit && c->Wander) return cs::Cat("free flight near ", (*app->Catalog())[c->Orbit->Body]->Name);
		if (c->Phase == ShipPhase::Queued) return cs::Cat("queued @ ", c->Ring->Name);
		if (c->Phase == ShipPhase::Descent) return cs::Cat("descending to ", c->Ring->City);
		if (c->Phase == ShipPhase::Landed) return cs::Cat("landed @ ", c->Ring->City);
		if (c->Phase == ShipPhase::Ascent) return cs::Cat("climbing to ", c->Ring->Name);
		// c.Journey != null: a null journey is the empty list in the C++ Core.
		int dest = c->Journey.Count() > 0 ? c->FinalBody : c->Orbit->Body;
		return cs::Cat(c->Phase == ShipPhase::Lane ? "=> " : "-> ", (*app->Catalog())[dest]->Name);
	}

	void Hud::DrawSelection()
	{
		auto t = app->Selected;
		if (t.IsNone()) return;
		cs::List<SelectionLine> lines;
		cs::String heading, sub;
		Color headingColor = Accent;

		if (t.IsStructure()) StructureLines(t.Structure, lines, heading, sub, headingColor);
		else if (t.IsRing()) RingLines(t.Ring, lines, heading, sub, headingColor);
		else if (t.IsBody())
		{
			auto b = (*app->Catalog())[t.Body];
			heading = b->Name;
			sub = cs::Cat(KindName(b), b->Status == BodyStatus::Candidate ? "  ·  CANDIDATE, unconfirmed" : b->Status == BodyStatus::Disputed ? "  ·  DISPUTED" : b->Status == BodyStatus::Fictional ? "  ·  fiction (game layer)" : "");
			auto eph = app->Ephemeris();
			if (b->IsStar()) lines.Add(SelectionLine{ "Photosphere", cs::Cat(cs::Fmt(b->TeffK, "N0"), " K,  ", cs::Fmt(b->LuminositySolar, b->LuminositySolar < 0.01 ? "0.00000" : "0.000"), " x the Sun's light") });
			lines.Add(SelectionLine{ "Radius", Km(b->RadiusKm) });
			if (b->DiscoveryYear > 0)
			{
				double earths = b->Gm / 398600.435;
				lines.Add(SelectionLine{ "Mass", cs::Cat(b->MassIsMinimum ? "at least " : "", cs::Fmt(earths, earths < 10 ? "0.00" : "N0"), " Earth masses") });
				lines.Add(SelectionLine{ "Habitable zone", b->HabitableZone == HabitableZone::Conservative ? "inside"
					: b->HabitableZone == HabitableZone::Optimistic ? "inside the optimistic zone only" : "outside" });
				lines.Add(SelectionLine{ "Found", cs::Str(b->DiscoveryYear) });
			}
			if (b->ParentIndex >= 0)
			{
				auto parent = (*app->Catalog())[b->ParentIndex];
				auto k = b->Elements->At(app->Clock->Jd);
				double dist = eph->Relative(t.Body).Length();
				if (parent->UsesStellarMapping()) lines.Add(SelectionLine{ cs::Cat("From ", parent->IsVisible() ? parent->Name : cs::String("barycentre")), cs::Cat(StellarDistance(dist), "  (", LightTime(dist), " light)") });
				else lines.Add(SelectionLine{ cs::Cat("From ", parent->Name), Km(dist) });
				lines.Add(SelectionLine{ "Orbital speed", cs::Cat(cs::Fmt(eph->OrbitalSpeedKmS(t.Body), "0.00"), " km/s") });
				lines.Add(SelectionLine{ "Period", Duration(k.PeriodDays()) });
				lines.Add(SelectionLine{ "Eccentricity", cs::Fmt(k.E, "0.0000") });
				lines.Add(SelectionLine{ "Semi-major axis", parent->UsesStellarMapping() ? StellarDistance(k.A) : Km(k.A) });
			}
			if (b->RotationHours != 0)
				lines.Add(SelectionLine{ "Day (sidereal)", cs::Cat(cs::Math::Abs(b->RotationHours) < 48 ? cs::Cat(cs::Fmt(cs::Math::Abs(b->RotationHours), "0.00"), " h") : Duration(cs::Math::Abs(b->RotationHours) / 24), b->RotationHours < 0 ? " retro" : "") });
			if (b->Kind != BodyKind::Station) lines.Add(SelectionLine{ "Surface gravity", cs::Cat(cs::Fmt(b->Gm / (b->RadiusKm * b->RadiusKm) * 1000 / 9.80665, "0.000"), " g") });
			auto infraHere = app->World()->Infrastructure;
			cs::List<cs::Ref<DockingRing>> ringsHere = infraHere ? infraHere->RingsAt(t.Body) : cs::List<cs::Ref<DockingRing>>();
			if (ringsHere.Count() > 0)
			{
				cs::List<cs::String> cities;
				for (const auto& x : ringsHere) cities.Add(x->City);
				lines.Add(SelectionLine{ "Docking rings", cs::Cat(ringsHere.Count(), ": ", cs::Strings::Join(", ", cities)) });
			}
			if (!b->Summary.empty()) lines.Add(SelectionLine{ "", b->Summary });
			for (const auto& s : app->Economy()->At(t.Body))
				if (s->Discovered()) AddSettlementLines(lines, s);
			auto battleHere = app->World()->War->BattleAt(t.Body);
			if (battleHere != nullptr) lines.Add(SelectionLine{ "Battle", cs::Cat(battleHere->Name, ", ", BattleScore(battleHere), ", ", Duration(battleHere->Seconds / Astro::SecondsPerDay)) });
		}
		else
		{
			auto ship = t.Ship;
			auto c = ship->Controller();
			auto st = app->ShipStateOf(ship);
			heading = ship->Name;
			auto company = c->Company;
			sub = company != nullptr
				? cs::Cat(company->Name, " ", cs::Strings::ToLowerInvariant(ship->Spec->DisplayName), ", under the ", ship->Faction->Name, " flag")
				: cs::Cat(ship->Faction->Name, " ", cs::Strings::ToLowerInvariant(ship->Spec->DisplayName));
			Rgb livery = company != nullptr ? company->Livery : ship->Faction->Color;
			headingColor = Color(livery.R, livery.G, livery.B);
			lines.Add(SelectionLine{ "Length", Km(ship->LengthKm()) });
			lines.Add(SelectionLine{ "Drive", cs::Cat(cs::Fmt(ship->Spec->AccelG, "0.##"), " g, flip-and-burn; hyperlanes between systems") });
			lines.Add(SelectionLine{ "Propellant", PropellantText(ship) });
			if (ship->IsEscort()) lines.Add(SelectionLine{ "Escorting", ship->Leader->Name });
			if (app->IsPiloting() && app->Piloted.Ship == ship && ship->Battle == nullptr)
			{
				// Under the player's own hand: where it really is, not the spot its schedule was pinned at.
				// (The C# local is `near`, renamed for the Windows macro, MIGRATION.md rule 10.)
				auto nearBody = (*app->Catalog())[app->PilotFrameBody()];
				lines.Add(SelectionLine{ "Status", cs::Cat("Under your helm near ", nearBody->Name) });
				lines.Add(SelectionLine{ "Altitude", Km(app->PilotOffsetKm().Length() - nearBody->RadiusKm) });
				lines.Add(SelectionLine{ "Speed", SpeedText(app->PilotSpeedKmS()) });
			}
			else if (c->Phase == ShipPhase::Lurk && c->Stalled)
			{
				lines.Add(SelectionLine{ "Status", cs::Cat("Stalled beside the ", c->LaneLeg.Lane->Name, " hyperlane (rings misaligned)") });
				lines.Add(SelectionLine{ "Until", cs::Cat(Duration(Fleet::RemainingDays(c, app->Clock->Jd)), " more") });
			}
			else if (c->Phase == ShipPhase::Lurk)
			{
				lines.Add(SelectionLine{ "Status", cs::Cat("Lying in wait beside the ", c->LaneLeg.Lane->Name, " hyperlane") });
				lines.Add(SelectionLine{ "Until", cs::Cat(Duration(Fleet::RemainingDays(c, app->Clock->Jd)), " more, unless prey comes by") });
			}
			else if (c->Phase == ShipPhase::Adrift)
			{
				lines.Add(SelectionLine{ "Status", cs::Cat("Pulled out of the ", c->LaneLeg.Lane->Name, " hyperlane") });
			}
			else if (c->Phase == ShipPhase::Lane)
			{
				lines.Add(SelectionLine{ "Status", cs::Cat("In the ", c->LaneLeg.Lane->Name, " hyperlane, bound for ", (*app->Catalog())[c->FinalBody]->Name) });
				lines.Add(SelectionLine{ "Speed", SpeedText(st.SpeedKmS) });
				lines.Add(SelectionLine{ "Remaining", Km(c->LaneLengthKm * (1 - st.Progress)) });
				lines.Add(SelectionLine{ "Lane exit", Duration(Fleet::RemainingDays(c, app->Clock->Jd)) });
			}
			else if (c->Phase == ShipPhase::Queued)
			{
				lines.Add(SelectionLine{ "Status", cs::Cat("Waiting off the ", c->Ring->Name, " to be cleared down to ", c->Ring->City) });
				lines.Add(SelectionLine{ "Cleared in", cs::Cat(Duration(Fleet::RemainingDays(c, app->Clock->Jd)), "  (", QueuePosition(c), " in line)") });
			}
			else if (c->Phase == ShipPhase::Descent || c->Phase == ShipPhase::Ascent)
			{
				double alt = (st.HelioKm - app->Ephemeris()->Helio(c->Ring->Body)).Length() - c->Ring->SurfaceKm;
				lines.Add(SelectionLine{ "Status", c->Phase == ShipPhase::Descent ? cs::Cat("Descending from the ", c->Ring->Name, " to ", c->Ring->City) : cs::Cat("Climbing from ", c->Ring->City, " to the ", c->Ring->Name) });
				lines.Add(SelectionLine{ "Altitude", cs::Cat(Km(cs::Math::Max(0, alt)), alt < c->Ring->EntryInterfaceKm ? "  ·  IN THE ATMOSPHERE" : "") });
				lines.Add(SelectionLine{ "Vertical speed", SpeedText(st.SpeedKmS) });
				lines.Add(SelectionLine{ "Done in", Duration(Fleet::RemainingDays(c, app->Clock->Jd)) });
			}
			else if (c->Phase == ShipPhase::Landed)
			{
				lines.Add(SelectionLine{ "Status", cs::Cat("Landed at ", c->Ring->City, ", ", (*app->Catalog())[c->Ring->Body]->Name) });
				if (c->WaitingForPropellant && c->DestinationBody == c->Ring->Body) lines[lines.Count() - 1] = SelectionLine{ "Status", cs::Cat("Landed at ", c->Ring->City, ", waiting for propellant") };
				else lines.Add(SelectionLine{ "Next", cs::Cat((*app->Catalog())[c->DestinationBody]->Name, " in ", Duration(Fleet::RemainingDays(c, app->Clock->Jd)), ", by the ", c->Ring->Name) });
			}
			else if (c->Phase == ShipPhase::Transit)
			{
				// A fixed point is a lane's ramp, unless the ship is bound for a jump gate or a raider stakeout off one (U.SPC-T435).
				cs::String where = c->Ring != nullptr && c->Orbit == c->HoldPoint ? cs::Cat(c->Ring->Name, ", ", (*app->Catalog())[c->Orbit->Body]->Name)
					: c->Orbit->Fixed && c->GateKm.has_value() && c->Voyage != nullptr && c->Voyage->ByCruise ? cs::String("the edge of the well")
					: c->Orbit->Fixed && (c->GateKm.has_value() || c->StakeoutKm.has_value()) ? cs::String("a jump gate's approach")
					: c->Orbit->Fixed ? cs::Cat((*app->Catalog())[c->Orbit->Body]->Name, " hyperlane ramp") : (*app->Catalog())[c->Orbit->Body]->Name;
				lines.Add(SelectionLine{ "Status", cs::Cat(st.Progress < 0.5 ? "Burning toward " : "Braking for ", where) });
				// c.Journey != null: a null journey is the empty list in the C++ Core.
				if (c->Journey.Count() > 0) lines.Add(SelectionLine{ "Bound for", cs::Cat((*app->Catalog())[c->FinalBody]->Name, " by ", JourneyText(c)) });
				lines.Add(SelectionLine{ "From", (*app->Catalog())[c->OriginBody]->Name });
				lines.Add(SelectionLine{ "Speed", cs::Cat(cs::Fmt(st.SpeedKmS, "N0"), " km/s") });
				lines.Add(SelectionLine{ "Remaining", Km(c->PathLength * (1 - st.Progress)) });
				lines.Add(SelectionLine{ "ETA", Duration(Fleet::RemainingDays(c, app->Clock->Jd)) });
			}
			else
			{
				auto body = (*app->Catalog())[c->Orbit->Body];
				lines.Add(SelectionLine{ "Status", ship->Battle != nullptr ? cs::Cat("Fighting in the ", ship->Battle->Name)
					: c->WaitingForPropellant && c->DestinationBody == c->Orbit->Body ? cs::Cat("Waiting at ", body->Name, " for propellant")
					: c->OutOfPropellant ? cs::Cat("Drifting off ", body->Name, " with dry tanks; a tanker is ", Duration(Fleet::RemainingDays(c, app->Clock->Jd)), " out")
					: cs::Cat(c->Orbit->Fixed ? "Holding position at " : "Holding orbit at ", body->Name) });
				// From where it actually is: a held position (a released or spawned hull) is a fixed point
				// with no orbit radius, and a ring's holding point sits off the body's centre.
				lines.Add(SelectionLine{ "Altitude", Km(c->Orbit->OffsetAt(app->Clock->Jd).Length() - body->RadiusKm) });
				lines.Add(SelectionLine{ "Orbital speed", cs::Cat(cs::Fmt(c->Orbit->SpeedKmS(), "0.00"), " km/s") });
				// A freshly spawned or not-yet-scheduled ship has no destination yet (-1).
				if (c->DestinationBody >= 0 && !(c->WaitingForPropellant && c->DestinationBody == c->Orbit->Body))
					lines.Add(SelectionLine{ "Next", cs::Cat((*app->Catalog())[c->DestinationBody]->Name, " in ", Duration(Fleet::RemainingDays(c, app->Clock->Jd))) });
			}
			auto giving = app->Fleet()->EvadingFrom(ship);
			auto off = app->Fleet()->EvasionOf(ship);
			if (giving != nullptr || off.Length() > 0.5)
				lines.Add(SelectionLine{ "Manoeuvring", cs::Cat(giving != nullptr ? cs::Cat("giving way to ", giving->Name, ", ") : cs::String("easing back onto course, "), Km(off.Length()), " off its path") });
			if (c->Role == ShipRole::Free) lines.Add(SelectionLine{ "Pilot", cs::Cat("Independent: goes where it pleases", c->Wander ? " (a hop out to open space)" : "") });
			auto combat = ship->Combat;
			lines.Add(SelectionLine{ "Hull", cs::Cat(Percent(combat->HullFraction()), "   Shields ", Percent(combat->Spec->Shield > 0 ? combat->Shield() / combat->Spec->Shield : 0)) });
			lines.Add(SelectionLine{ "Armament", ShipLod::Armament(combat->Subsystems) });
			int lost = 0;
			for (const auto& hp : combat->Subsystems) if (hp->Destroyed()) lost++;
			if (lost > 0) lines.Add(SelectionLine{ "Damage", cs::Cat(lost, " of ", combat->Subsystems.Count(), " hardpoints destroyed") });
			if (combat->Spec->Squadrons > 0) lines.Add(SelectionLine{ "Fighters", CarrierWings::Describe(combat) });
			auto inside = combat->Interior;
			lines.Add(SelectionLine{ "Aboard", cs::Cat(cs::Fmt(inside->Crew(), "0"), " crew, ", cs::Fmt(inside->Marines(), "0"), " marines",
				combat->Disabled() ? "  ·  DISABLED" : "") });
			if (inside->UnderBoarding())
			{
				lines.Add(SelectionLine{ "BOARDED", cs::Cat(inside->BoarderNames(), ": ", cs::Fmt(inside->Boarders(), "0"), " boarders hold ", inside->TakenCount(), " of ", inside->Compartments.Count(), " spaces") });
				for (const auto& space : inside->Compartments) lines.Add(SelectionLine{ cs::Cat("  ", space->Name), SpaceText(space) });
				if (inside->Log.Count() > 0) lines.Add(SelectionLine{ "", inside->Log[inside->Log.Count() - 1].Text });
			}
			if (ship->Derelict) lines.Add(SelectionLine{ "DERELICT", "Boarded, stripped and abandoned" });
			if (ship->Battle != nullptr) lines.Add(SelectionLine{ "In battle", cs::Cat(ship->Battle->Name, ", ", BattleScore(ship->Battle)) });
			if (ship->Spec->IsMilitary || ship->Faction->IsPirate)
			{
				auto order = app->World()->War->OrderOf(c);
				auto outlaws = app->World()->Outlaws;
				cs::Ref<BountyHunter> hunter = outlaws ? outlaws->HunterOf(c) : nullptr;
				cs::Ref<Outlaw> outlaw = outlaws ? outlaws->Of(c) : nullptr;
				if (hunter != nullptr)
				{
					cs::Ref<LawRecord> target = hunter->Contract ? hunter->Contract->Target : nullptr;
					cs::String after;
					if (target == nullptr) after = "looking for a contract";
					else
					{
						auto targetOutlaw = outlaws->Of(target);
						after = cs::Cat("hunting ", targetOutlaw ? targetOutlaw->Title() : (target->IsPlayer() ? cs::String("YOU") : target->Name));
					}
					lines.Add(SelectionLine{ "Bounty hunter", cs::Cat(after, hunter->Takes > 0 ? cs::Cat(",  ", hunter->Takes, " taken for ", Credits(hunter->Collected)) : cs::String()) });
				}
				else lines.Add(SelectionLine{ "Orders", order != nullptr ? OrderText(order) : cs::Cat("Patrol ", RouteText(c)) });
				if (outlaw != nullptr)
					lines.Add(SelectionLine{ "Outlaw", cs::Cat(outlaw->Title(), ",  ", outlaw->Deeds(), ",  ", cs::Fmt(outlaw->DaysAtLarge(app->Clock->Jd), "0"), " days at large",
						outlaw->Haven != nullptr && outlaw->Haven->Discovered() ? cs::Cat(",  sails from ", outlaw->Haven->Name) : cs::String()) });
			}
			else
			{
				lines.Add(SelectionLine{ "Cargo", c->Cargo.empty() ? cs::String("Empty") : c->Cargo });
				lines.Add(SelectionLine{ "Hold", Tonnes(c->CapacityTonnes) });
				if (c->HasCargo) lines.Add(SelectionLine{ "Paid", cs::Cat(Credits(c->CargoCost), c->CargoBonus > 0 ? cs::Cat("  (+", Credits(c->CargoBonus), " contract)") : cs::String()) });
				lines.Add(SelectionLine{ "Trade profit", Credits(c->Credits) });
				if (c->Company != nullptr) lines.Add(SelectionLine{ "Owner", CompanyText(c->Company) });
			}
			lines.Add(SelectionLine{ "Trips", cs::Str(c->TripsCompleted) });
			// The law's file on whoever commands it: the player's own record for the ship they fly.
			auto law = app->World()->Law;
			cs::Ref<LawRecord> record = law ? law->FindRecord(ship) : nullptr;
			if (record != nullptr && (record->Wanted() || record->Crimes.Count() > 0))
				lines.Add(SelectionLine{ record->IsPlayer() ? "Your record" : "Captain", cs::Cat(record->IsPlayer() ? cs::String() : cs::Cat(record->Name, ", "), RecordText(record)) });
		}

		float w = 410;
		float linesH = lines.Count() * 19;
		// A long card (a boarded hull lists every compartment) never runs off the top of the screen: its lines scroll.
		float maxLinesH = Mathf::Max(19 * 4, Screen::height - 140 - (64 + 30));
		bool scrolls = linesH > maxLinesH;
		float shownH = scrolls ? maxLinesH : linesH;
		float h = 64 + shownH + 30;
		auto r = Panel(Rect(SafeRight() - w - 10, Screen::height - h - 10, w, h));
		auto old = GUI::contentColor;
		GUI::contentColor = headingColor;
		GUI::Label(Rect(r.x + 14, r.y + 8, w - 28, 24), heading, title);
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 14, r.y + 30, w - 28, 18), sub, small);
		GUI::contentColor = old;

		if (selectionScrollFor != t)
		{
			selectionScrollFor = t;
			selectionScroll = Vector2::zero();
		}
		float x0 = r.x, y = r.y + 54, valueW = w - 146;
		if (scrolls)
		{
			selectionScroll = GUI::BeginScrollView(Rect(r.x, y, w - 4, shownH), selectionScroll, Rect(0, 0, w - 20, linesH));
			x0 = 0;
			y = 0;
			valueW = w - 162;
		}
		for (const auto& [key, value] : lines)
		{
			if (key.empty())
			{
				GUI::Label(Rect(x0 + 14, y, w - 28, 36), value, small);
				y += 19;
				continue;
			}
			GUI::contentColor = Dim;
			GUI::Label(Rect(x0 + 14, y, 120, 19), key, small);
			GUI::contentColor = Color::white();
			GUI::Label(Rect(x0 + 132, y, valueW, 19), value, text);
			y += 19;
		}
		if (scrolls) GUI::EndScrollView();
		GUI::contentColor = old;

		bool focused = app->SpaceCamera->FocusTarget == t && app->SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Free;
		if (GUI::Button(Rect(r.x + 14, r.yMax() - 32, 150, 24), focused ? "Re-frame  [F]" : "Focus  [F]", buttonActive)) app->FocusSelected();
		if (GUI::Button(Rect(r.x + 172, r.yMax() - 32, 150, 24), "Deselect  [Esc]", button)) app->Select(Target::None());
	}

	cs::String Hud::PropellantText(const cs::Ref<Ship>& ship)
	{
		if (ship->OutOfPropellant || ship->Propellant() <= 1e-6) return cs::Cat("DRY  ·  0 of ", Tonnes(ship->PropellantCapacity()));
		return cs::Cat(Percent(ship->PropellantFraction()), "  ·  ", Tonnes(ship->Propellant()), " of ", Tonnes(ship->PropellantCapacity()),
			"  ·  Δv ", cs::Fmt(ship->DeltaVBudgetKmS(), "N0"), " km/s");
	}

	cs::String Hud::QueuePosition(const cs::Ref<Ship>& ship)
	{
		int ahead = 1;
		for (const auto& s : app->Fleet()->Ships())
			if (!s->Destroyed && s->Phase == ShipPhase::Queued && s->Ring == ship->Ring && s->PhaseEndJd < ship->PhaseEndJd) ahead++;
		return ahead == 1 ? cs::String("1st") : ahead == 2 ? cs::String("2nd") : ahead == 3 ? cs::String("3rd") : cs::Cat(ahead, "th");
	}

	void Hud::StructureLines(const cs::Ref<OrbitalStructure>& s, cs::List<SelectionLine>& lines, cs::String& heading, cs::String& sub, Color& headingColor)
	{
		heading = s->Name;
		cs::String kind = s->Kind == StructureKind::Shipyard ? "Orbital shipyard" : s->Kind == StructureKind::Factory ? "Factory station"
			: s->Kind == StructureKind::Habitat ? "Orbital habitat" : s->Kind == StructureKind::CrewedStation ? "Crewed space station"
			: s->Kind == StructureKind::Telescope ? "Space telescope" : s->Kind == StructureKind::Depot ? "Propellant depot"
			: s->Kind == StructureKind::Relay ? "Communications relay" : s->Kind == StructureKind::Gateway ? "Transfer station" : "Robotic orbiter";
		sub = cs::Cat(kind, s->Real ? cs::String("  ·  real spacecraft") : cs::Cat("  ·  ", s->Owner() != nullptr ? s->Owner()->Name : cs::String()));
		headingColor = s->Owner() != nullptr ? Color(s->Owner()->Color.R, s->Owner()->Color.G, s->Owner()->Color.B) : Accent;
		auto b = (*app->Catalog())[s->Body];
		if (s->AtLagrange())
		{
			// Co-orbital: the point moves with its pair, so it is always this far from the smaller body.
			lines.Add(SelectionLine{ "Where", Lagrange::Name(app->Catalog(), s->Body, s->Lagrange) });
			lines.Add(SelectionLine{ cs::Cat("From ", b->Name), Km((s->Helio(app->Ephemeris()) - app->Ephemeris()->Helio(s->Body)).Length()) });
		}
		else
		{
			lines.Add(SelectionLine{ "Orbiting", b->Name });
			lines.Add(SelectionLine{ "Altitude", Km(s->Orbit->RadiusKm - b->RadiusKm) });
			lines.Add(SelectionLine{ "Period", Duration(Astro::TwoPi / cs::Math::Max(s->Orbit->RateRadPerDay, 1e-12)) });
			lines.Add(SelectionLine{ "Speed", cs::Cat(cs::Fmt(s->Orbit->SpeedKmS(), "0.00"), " km/s") });
		}
		lines.Add(SelectionLine{ "Size", Km(s->LengthKm) });
		if (s->Kind == StructureKind::Shipyard && s->Settlement != nullptr)
		{
			auto slip = s->Settlement->OnSlip;
			lines.Add(SelectionLine{ "On the slip", slip.has_value() ? cs::Cat(ShipClassSpec::Of(*slip)->DisplayName, ", ", Percent(s->SlipProgress()), " built") : cs::String("Empty") });
			lines.Add(SelectionLine{ "Build points", cs::Fmt(s->Settlement->BuildPoints, "N0") });
		}
		if (s->Kind == StructureKind::Factory && s->Settlement != nullptr)
			for (const auto& f : s->Settlement->Facilities)
				if (f->Recipe->Kind == s->Industry)
					lines.Add(SelectionLine{ "Output", cs::Cat(f->Recipe->Name, ", level ", cs::Fmt(f->Level, "0.0"), ", running ", Percent(f->Utilisation)) });
		if (s->Settlement != nullptr) lines.Add(SelectionLine{ "Serves", cs::Cat(s->Settlement->Name, ", pop ", People(s->Settlement->Population)) });
		if (!s->Note.empty()) lines.Add(SelectionLine{ "", s->Note });
	}

	void Hud::RingLines(const cs::Ref<DockingRing>& r, cs::List<SelectionLine>& lines, cs::String& heading, cs::String& sub, Color& headingColor)
	{
		heading = r->Name;
		sub = cs::Cat("Docking ring over ", r->City, ", ", (*app->Catalog())[r->Body]->Name, "  ·  fiction: held on station by thrust");
		headingColor = Color(0.6f, 0.9f, 1.0f);
		lines.Add(SelectionLine{ "Over", cs::Cat(r->City, "  ", cs::Fmt(cs::Math::Abs(r->LatDeg), "0.00"), r->LatDeg >= 0 ? "° N  " : "° S  ",
			cs::Fmt(cs::Math::Abs(r->LonDeg), "0.00"), r->LonDeg >= 0 ? "° E" : "° W") });
		lines.Add(SelectionLine{ "Altitude", cs::Cat(Km(r->RadiusKm - r->SurfaceKm), cs::Math::Abs(r->RadiusKm - Infrastructure::RingRadiusKm((*app->Catalog())[r->Body])) < 1 && r->RadiusKm > r->SurfaceKm * 3.5 ? "  (stationary-orbit height)" : "") });
		int queued = 0, down = 0, landed = 0, up = 0;
		for (const auto& s : app->Fleet()->Ships())
		{
			if (s->Destroyed || s->Ring != r) continue;
			if (s->Phase == ShipPhase::Queued) queued++;
			else if (s->Phase == ShipPhase::Descent) down++;
			else if (s->Phase == ShipPhase::Landed) landed++;
			else if (s->Phase == ShipPhase::Ascent) up++;
		}
		lines.Add(SelectionLine{ "Traffic", cs::Cat(queued, " queued, ", down, " descending, ", landed, " landed, ", up, " climbing") });
		lines.Add(SelectionLine{ "Clearance", cs::Cat("one ship every ", cs::Fmt(r->EntryIntervalDays * 1440, "0"), " min",
			r->NextEntryJd > app->Clock->Jd ? cs::Cat(", next in ", Duration(r->NextEntryJd - app->Clock->Jd)) : cs::String(", clear now")) });
		lines.Add(SelectionLine{ "Descent", cs::Cat(Duration(r->DescentDays), " to the ground; entry interface ", Km(r->EntryInterfaceKm), " up") });
		if (r->Settlement != nullptr) lines.Add(SelectionLine{ "Serves", cs::Cat(r->Settlement->Name, " (", r->Owner() != nullptr ? r->Owner()->Name : cs::String(), ")") });
		lines.Add(SelectionLine{ "", "Merchants and couriers land through the rings; warships and pirates hold orbit." });
	}

	cs::String Hud::CompanyText(const cs::Ref<Company>& co)
	{
		if (co->Fate == CompanyFate::Merged) return cs::Cat(co->Name, "  · merged into ", co->MergedInto != nullptr ? co->MergedInto->Name : cs::String());
		if (co->Fate == CompanyFate::Bankrupt) return cs::Cat(co->Name, "  · bankrupt");
		cs::String order = co->OnOrder.has_value()
			? cs::Cat(",  ", cs::Strings::ToLowerInvariant(cs::String(Core::ToString(*co->OnOrder))), " on order at ", co->OrderYard != nullptr ? co->OrderYard->Name : cs::String())
			: cs::String();
		return cs::Cat(co->Name, " (", co->HomePort != nullptr ? co->HomePort->Name : cs::String(), ")  ", co->Hulls(), co->Hulls() == 1 ? " hull" : " hulls", ",  ", Credits(co->Cash),
			",  ", Credits(co->IncomePerDay), "/day", order);
	}

	cs::String Hud::PolityText(const cs::Ref<Faction>& f)
	{
		auto polities = app->World()->Polities;
		cs::String kind;
		switch (f->Kind)
		{
			case PolityKind::Company: kind = "company"; break;
			case PolityKind::FreeTraders: kind = "free traders"; break;
			case PolityKind::PirateClan: kind = "pirate clan"; break;
			default: kind = "state"; break;
		}
		auto capital = polities->CapitalOf(f);
		cs::String seat;
		if (capital != nullptr)
		{
			auto at = polities->WorldOf(capital);
			// C#'s null string: empty here.
			cs::String system;
			for (const auto& x : app->Galaxy->Systems)
				if (x->World == at && at != app->World()) system = x->Name;
			seat = cs::Cat(f->IsPirate ? ",  den " : ",  capital ", capital->Name, !system.empty() ? cs::Cat(" (", system, ")") : cs::String());
		}
		int held = f->IsPirate ? 0 : polities->SystemsHeldBy(f).Count();
		int navy = f->IsMilitary || f->IsPirate ? polities->Navy(f).Count() : 0;
		return cs::Cat(kind, seat, held > 0 ? cs::Cat(",  holds ", held, held == 1 ? " system" : " systems") : cs::String(),
			navy > 0 ? cs::Cat(",  ", navy, f->IsPirate ? " raiders" : " warships") : cs::String());
	}

	void Hud::AddSettlementLines(cs::List<SelectionLine>& lines, const cs::Ref<Settlement>& s)
	{
		lines.Add(SelectionLine{ "", "" });
		bool seat = app->World()->Polities->CapitalOf(s->Owner) == s;
		lines.Add(SelectionLine{ s->Name, cs::Cat(s->Owner->Name, " ", KindName(s->Kind), seat ? " (capital)" : "", ", pop ", People(s->Population)) });
		auto houses = app->World()->Companies;
		if (houses != nullptr)
			for (const auto& co : houses->All)
				if (co->Trading() && co->HomePort == s) lines.Add(SelectionLine{ "Company", CompanyText(co) });
		if (s->Kind != SettlementKind::Haven && !s->Fallen)
		{
			double moving = s->NetMigration;
			cs::String migration = cs::Math::Abs(moving) < 1 ? cs::String("no migration") :
				cs::Cat(moving > 0 ? "+" : "-", cs::Fmt(cs::Math::Abs(moving), "N0"), "/day ", moving > 0 ? "moving in" : "leaving");
			lines.Add(SelectionLine{ "People", cs::Cat(migration, "  ·  jobs ", cs::Fmt(s->Staffing * 100, "0"), "% filled  ·  wage tax ", Credits(s->WageTax), "/day") });
		}
		cs::List<ExportShare> exports;
		cs::List<Shortage> shortages;
		for (int g = 0; g < GoodInfo::Count; g++)
		{
			auto info = GoodInfo::Of(static_cast<Good>(g));
			double net = s->Supply[g] - s->Demand[g];
			if (net > 0) exports.Add(ExportShare{ net * info->BasePrice, info->Name });
			double ratio = s->Price[g] / info->BasePrice;
			if (s->Demand[g] > 0 && ratio > 1.4) shortages.Add(Shortage{ ratio, cs::Cat(info->Name, " x", cs::Fmt(ratio, "0.0")) });
		}
		exports.Sort([](const ExportShare& a, const ExportShare& b) { return cs::CompareTo(b.value, a.value); });
		shortages.Sort([](const Shortage& a, const Shortage& b) { return cs::CompareTo(b.ratio, a.ratio); });
		if (exports.Count() > 0)
		{
			cs::List<cs::String> names;
			for (const auto& e : exports.GetRange(0, cs::Math::Min(3, exports.Count()))) names.Add(e.name);
			lines.Add(SelectionLine{ "Exports", cs::Strings::Join(", ", names) });
		}
		cs::String shortOf = "nothing";
		if (shortages.Count() > 0)
		{
			cs::List<cs::String> names;
			for (const auto& e : shortages.GetRange(0, cs::Math::Min(3, shortages.Count()))) names.Add(e.name);
			shortOf = cs::Strings::Join(", ", names);
		}
		lines.Add(SelectionLine{ "Short of", shortOf });
		double siege = app->World()->War->SiegeDays(s);
		lines.Add(SelectionLine{ "Defences", cs::Cat(s->Platforms.Count(), " of ", s->MaxPlatforms, " platforms",
			siege > 0 ? cs::Cat("  ·  under siege ", cs::Fmt(siege, "0"), " of ", cs::Fmt(War::SiegeNeeded(s), "0"), " days") : cs::String()) });
	}

	cs::String Hud::HavenMark(const cs::Ref<BodyDefinition>& body)
	{
		for (const auto& s : app->Economy()->At(body->Index))
			if (s->Kind == SettlementKind::Haven && s->Discovered()) return cs::Cat("  · ", s->Name, s->Fallen ? " (fallen)" : " (pirate haven)");
		return "";
	}

	cs::String Hud::KindName(SettlementKind k)
	{
		switch (k)
		{
			case SettlementKind::Station: return "station";
			case SettlementKind::MiningOutpost: return "mining outpost";
			case SettlementKind::Haven: return "pirate haven";
			default: return "colony";
		}
	}
}
