#include "UI/GalaxyMapScreen.h"

#include "UnitySpaceApp.h"
#include "UI/Hud.h"
#include "UI/Icons.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Diplomacy.h"
#include "Information/GalaxyMap.h"
#include "Information/LightSpeed.h"
#include "Information/News.h"
#include "Systems/FtlNavigation.h"
#include "Systems/GateConstruction.h"
#include "Systems/GateNetwork.h"
#include "Systems/HelmFtl.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"
#include "Systems/Statecraft.h"
#include "Systems/TravelCourse.h"

#include <iterator>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	float GalaxyMapScreen::BandWidth() { return Mathf::Min(static_cast<float>(Screen::width), Screen::height * MaxPanelAspect); }
	float GalaxyMapScreen::BandLeft() { return (Screen::width - BandWidth()) * 0.5f; }
	float GalaxyMapScreen::BandRight() { return BandLeft() + BandWidth(); }

	void GalaxyMapScreen::Opened()
	{
		selected = app->ActiveSystem;
		selectedStar.reset();
		fitted = false;
		builtAt = -10.0f;
	}

	void GalaxyMapScreen::Draw()
	{
		EnsureStyles();
		auto screen = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		// The map has the whole screen: nothing under it is clicked or steered while it is up.
		if (Event::current().type == EventType::Layout || Event::current().type == EventType::Repaint)
			if (!app->UiRects.Contains(screen)) app->UiRects.Add(screen);

		double jd = app->Clock->Jd;
		if (model == nullptr || builtFor != app->ActiveSystem || Time::unscaledTime - builtAt > 0.25f)
		{
			// (C#'s `Construction?.Works()`: null is the empty list here, which shows none either.)
			model = GalaxyMap::Build(app->Galaxy, app->ActiveSystem, jd,
				app->Galaxy->Construction ? app->Galaxy->Construction->Works() : cs::List<cs::Ref<GateWork>>());
			builtAt = Time::unscaledTime;
			builtFor = app->ActiveSystem;
			GatherChartStars();
		}
		if (selected == nullptr || model->Of(selected) == nullptr) selected = app->ActiveSystem;

		auto old = GUI::color;
		GUI::color = Color(0.01f, 0.015f, 0.03f, 0.9f);
		GUI::DrawTexture(screen, Texture2D::whiteTexture());
		GUI::color = old;

		constexpr float legendW = 270, detailW = 360, headerH = 46;
		float left = BandLeft() + 10, right = BandRight() - 10;
		auto header = Rect(left, 10, right - left, headerH);
		auto legend = Rect(left, header.yMax() + 8, legendW, Screen::height - header.yMax() - 18);
		auto detail = Rect(right - detailW, header.yMax() + 8, detailW, Screen::height - header.yMax() - 18);
		auto canvas = Rect(legend.xMax() + 8, header.yMax() + 8, detail.xMin() - legend.xMax() - 16, Screen::height - header.yMax() - 18);

		if (!fitted) Fit(canvas);
		HandleInput(canvas);
		if (Event::current().type == EventType::Repaint) DrawMap(canvas, jd);
		DrawHeader(header, jd);
		DrawLegend(legend);
		if (app->EnvoyTo != nullptr)
		{
			// A hail from the helm (F12) opens the envoy on the polity hailed.
			envoyWith = app->EnvoyTo;
			app->EnvoyTo = nullptr;
			diplomacyMode = true;
			commandMode = false;
			warArmed = false;
		}
		if (diplomacyMode) DrawDiplomacy(detail, jd);
		else if (commandMode) DrawCommand(detail, jd);
		else DrawDetail(detail, jd);
	}

	// ---------------------------------------------------------------- projection

	void GalaxyMapScreen::Fit(Rect canvas)
	{
		const auto& systems = app->Galaxy->Systems;
		auto sum = DVec3::Zero;
		for (const auto& s : systems) sum += s->PositionLy;
		centreLy = systems.Count() > 0 ? sum / systems.Count() : DVec3::Zero;
		double reach = 1;
		for (const auto& s : systems) reach = cs::Math::Max(reach, (s->PositionLy - centreLy).Length());
		fitScale = 0.46f * Mathf::Min(canvas.width, canvas.height) / static_cast<float>(reach);
		// A wide canvas has room to spare across: use some of it.
		fitScale *= Mathf::Clamp(canvas.width / Mathf::Max(canvas.height, 1.0f), 1.0f, 1.6f);
		pan = Vector2::zero();
		zoom = 1;
		fitted = true;
	}

	Vector2 GalaxyMapScreen::Project(Rect canvas, DVec3 ly, float& depth)
	{
		auto d = ly - centreLy;
		float c = Mathf::Cos(yaw), s = Mathf::Sin(yaw);
		float x = static_cast<float>(d.X * c - d.Y * s);
		float y = static_cast<float>(d.X * s + d.Y * c);
		float z = static_cast<float>(d.Z);
		float up = y * Mathf::Cos(tilt) + z * Mathf::Sin(tilt);
		depth = -y * Mathf::Sin(tilt) + z * Mathf::Cos(tilt);
		float k = fitScale * zoom;
		return canvas.center() + pan + Vector2(x * k, -up * k);
	}

	Vector2 GalaxyMapScreen::Project(Rect canvas, DVec3 ly)
	{
		float unused;
		return Project(canvas, ly, unused);
	}

	Vector2 GalaxyMapScreen::Foot(Rect canvas, DVec3 ly) { return Project(canvas, DVec3(ly.X, ly.Y, centreLy.Z)); }

	void GalaxyMapScreen::GatherChartStars()
	{
		chartStars.Clear();
		auto here = app->ActiveSystem->PositionLy;
		chartStars.AddRange(app->Galaxy->NearbyStars);
		chartStars.RemoveAll([](const NearbyStar& n) { return n.Name.empty(); });
		chartStars.Sort([&here](const NearbyStar& a, const NearbyStar& b)
		{
			return cs::CompareTo((a.PositionLy - here).LengthSquared(), (b.PositionLy - here).LengthSquared());
		});
		if (chartStars.Count() > MaxChartStars) chartStars.RemoveRange(MaxChartStars, chartStars.Count() - MaxChartStars);
	}

	cs::Nullable<NearbyStar> GalaxyMapScreen::NearestStar(Rect canvas, Vector2 mouse)
	{
		cs::Nullable<NearbyStar> best;
		float bestPx = 12;
		for (const auto& n : chartStars)
		{
			float px = (Project(canvas, n.PositionLy) - mouse).magnitude();
			if (px < bestPx)
			{
				bestPx = px;
				best = n;
			}
		}
		return best;
	}

	cs::Ref<StarSystem> GalaxyMapScreen::Nearest(Rect canvas, Vector2 mouse)
	{
		cs::Ref<StarSystem> best = nullptr;
		float bestPx = 22;
		for (const auto& m : model->Systems)
		{
			float px = (Project(canvas, m->System->PositionLy) - mouse).magnitude();
			if (px < bestPx)
			{
				bestPx = px;
				best = m->System;
			}
		}
		return best;
	}

	// ---------------------------------------------------------------- input

	void GalaxyMapScreen::HandleInput(Rect canvas)
	{
		Event& e = Event::current();
		if (!canvas.Contains(e.mousePosition))
		{
			if (e.type == EventType::Repaint)
			{
				hovered = nullptr;
				hoveredStar.reset();
			}
			return;
		}
		if (e.type == EventType::MouseMove || e.type == EventType::Repaint)
		{
			hovered = Nearest(canvas, e.mousePosition);
			hoveredStar = hovered == nullptr ? NearestStar(canvas, e.mousePosition) : cs::Nullable<NearbyStar>();
		}
		// (C#'s switch with `case ... when` guards.)
		if (e.type == EventType::MouseDown && e.button == 0)
		{
			auto hit = Nearest(canvas, e.mousePosition);
			if (hit != nullptr)
			{
				selected = hit;
				selectedStar.reset();
				if (e.clickCount >= 2) Travel(hit);
			}
			else if (auto star = NearestStar(canvas, e.mousePosition))
			{
				// A plain star: no gate goes there, but an FTL drive can (U.SPC-T420).
				selectedStar = *star;
				if (e.clickCount >= 2 && app->CanPlotFtl()) app->PlotFtlCourse(FtlTarget::OfStar(*star));
			}
			e.Use();
		}
		else if (e.type == EventType::MouseDrag && (e.button == 0 || e.button == 2))
		{
			if (e.shift || e.button == 2) pan += e.delta;
			else
			{
				yaw += e.delta.x * 0.008f;
				tilt = Mathf::Clamp(tilt + e.delta.y * 0.008f, 0.0f, 1.45f);
			}
			e.Use();
		}
		else if (e.type == EventType::ScrollWheel)
		{
			float before = zoom;
			zoom = Mathf::Clamp(zoom * Mathf::Pow(1.12f, -e.delta.y / 3.0f), 0.35f, 8.0f);
			// Zoom about the cursor: the point under it stays put.
			auto towards = e.mousePosition - (canvas.center() + pan);
			pan -= towards * (zoom / before - 1);
			e.Use();
		}
	}

	void GalaxyMapScreen::Travel(const cs::Ref<StarSystem>& to)
	{
		if (to == nullptr || to == app->ActiveSystem) return;
		app->ShowGalaxyMap = false;
		app->TravelTo(to);
	}

	// ---------------------------------------------------------------- the map

	void GalaxyMapScreen::DrawMap(Rect canvas, double jd)
	{
		GUI::BeginClip(canvas);
		auto local = Rect(0, 0, canvas.width, canvas.height);
		auto focus = selected != nullptr && selected != app->ActiveSystem ? selected : hovered;
		// Nothing picked: the helm's plotted course (U.SPC-T516).
		if ((focus == nullptr || focus == app->ActiveSystem) && app->Course != nullptr && !app->Course->Done()) focus = app->Course->Destination;
		auto route = focus != nullptr && focus != app->ActiveSystem ? app->Galaxy->Network->Route(app->ActiveSystem, focus, nullptr, jd) : nullptr;
		cs::HashSet<cs::Ref<GateLink>> onRoute;
		if (route != nullptr)
			for (const auto& g : *route)
				onRoute.Add(g->Link);

		// Stalks first: each star's height above or below the plane, so the depth reads.
		for (const auto& m : model->Systems)
		{
			auto p = Project(local, m->System->PositionLy);
			auto f = Foot(local, m->System->PositionLy);
			Line(f, p, Color(Dim.r, Dim.g, Dim.b, 0.18f), 1.0f);
			Disc(f, 3, Color(Dim.r, Dim.g, Dim.b, 0.25f));
		}

		// Territory: a wash of the holder's colour round each system it holds, every other holder's share a smaller one.
		if (showTerritory)
			for (const auto& m : model->Systems)
			{
				if (!m->Known() || m->Holdings.Count() == 0) continue;
				auto p = Project(local, m->System->PositionLy);
				double total = 0;
				for (const auto& h : m->Holdings) total += h->Population;
				for (int i = m->Holdings.Count() - 1; i >= 0; i--)
				{
					const auto& h = m->Holdings[i];
					float share = total > 0 ? static_cast<float>(h->Population / total) : 1.0f / m->Holdings.Count();
					float r = i == 0 ? 46 + 10 * Mathf::Log10(1 + static_cast<float>(total) / 1e6f) : 18 + 30 * Mathf::Sqrt(share);
					Disc(p, r, Tint(h->Faction->Color, i == 0 ? 0.32f : 0.22f));
				}
			}

		// The gate links.
		for (const auto& l : model->Links)
		{
			auto a = Project(local, l->A->PositionLy);
			auto b = Project(local, l->B->PositionLy);
			if (l->UnderConstruction)
			{
				if (!showConstruction) continue;
				Dashed(a, b, Color(BuildColor.r, BuildColor.g, BuildColor.b, 0.55f), 1.5f, 6, 6);
				auto mid = Vector2::Lerp(a, b, 0.5f);
				Line(a, Vector2::Lerp(a, b, static_cast<float>(l->Progress)), Color(BuildColor.r, BuildColor.g, BuildColor.b, 0.9f), 2.0f);
				Label(mid + Vector2(6, -16), cs::Cat("building ", cs::Fmt(l->Progress * 100, "0"), "%"), BuildColor, true);
				continue;
			}
			bool routed = l->Link != nullptr && onRoute.Contains(l->Link);
			if (!l->Known()) Dashed(a, b, Color(Unknown.r, Unknown.g, Unknown.b, 0.4f), 1.0f, 4, 6);
			else Line(a, b, Color(0.45f, 0.75f, 0.95f, 0.45f), 1.4f);
			if (showFlows && l->Crossings > 0)
			{
				float w = 2 + 7 * static_cast<float>(l->Flow);
				Line(a, b, Color(FlowColor.r, FlowColor.g, FlowColor.b, 0.28f), w);
				FlowDots(a, b, l);
			}
			if (routed) Line(a, b, Color(Accent.r, Accent.g, Accent.b, 0.95f), 3.0f);
			if (showFronts && l->WarFront) Dashed(a, b, FrontColor, 3.5f, 10, 6);
			if (showFronts && l->Shut())
			{
				// A bar across the line at the shut end.
				if (l->ShutAtA) Barrier(a, b);
				if (l->ShutAtB) Barrier(b, a);
			}
		}

		// The plain stars on the charts (U.SPC-T420): no system yet, but an FTL drive goes anywhere.
		for (const auto& n : chartStars)
		{
			auto p = Project(local, n.PositionLy);
			if (!local.Contains(p)) continue;
			auto c = Color::Lerp(Color(n.Color.R, n.Color.G, n.Color.B), Unknown, 0.45f);
			bool picked = selectedStar.has_value() && selectedStar->Name == n.Name;
			bool hover = hoveredStar.has_value() && hoveredStar->Name == n.Name;
			Disc(p, picked || hover ? 4.5f : 2.6f, Color(c.r, c.g, c.b, picked || hover ? 0.95f : 0.6f));
			if (picked) Brackets(p, 11, Accent);
			if (picked || hover) Label(p + Vector2(8, -14), cs::Cat(n.Name, "  ·  ", n.SpectralType), picked ? Accent : Dim, true);
		}

		// The plotted FTL course (U.SPC-T420): a straight line through open space, unlike any gate route.
		auto course = app->PlottedCourse;
		if (course != nullptr && course->Target != nullptr && course->From == app->ActiveSystem)
		{
			auto a = Project(local, app->ActiveSystem->PositionLy);
			auto b = Project(local, course->Target->Origin.Ly());
			auto c = course->Flyable() ? CourseColor : FrontColor;
			Dashed(a, b, Color(c.r, c.g, c.b, 0.9f), 2.5f, 12, 5);
			Ring(b, 15, c, 2.0f);
			Label(Vector2::Lerp(a, b, 0.5f) + Vector2(8, 4), cs::Cat("FTL ", cs::Fmt(course->Ly, "0.00"), " ly",
				(course->Flyable() ? cs::Cat(", ETA ", FtlNavigation::Describe(course->TotalDays())) : cs::String())), c, true);
		}

		// The stars.
		for (const auto& m : model->Systems)
		{
			auto p = Project(local, m->System->PositionLy);
			auto c = StarColor(m->System);
			if (!m->Known() && !m->Here) c = Color::Lerp(c, Unknown, 0.6f);
			Disc(p, 9, Color(c.r, c.g, c.b, 0.35f));
			Disc(p, 4.5f, c);
			if (m->Here) Ring(p, 13, Accent, 2.0f);
			if (m->System == selected) Brackets(p, 17, Accent);
			else if (m->System == hovered) Brackets(p, 15, Color(1, 1, 1, 0.6f));
			if (showFronts && (m->Contested || m->Threats.Count() > 0)) Ring(p, 20 + 2 * Mathf::Sin(Time::unscaledTime * 4), FrontColor, 2.0f);
			if (showPirates && m->Pirates != PirateActivity::None)
			{
				float size = m->Pirates == PirateActivity::High ? 15 : 11;
				auto at = p + Vector2(-20, 12);
				auto old = GUI::color;
				GUI::color = Color(PirateColor.r, PirateColor.g, PirateColor.b, m->Pirates == PirateActivity::High ? 0.75f + 0.25f * Mathf::Sin(Time::unscaledTime * 5) : 0.8f);
				GUI::DrawTexture(Rect(at.x - size * 0.5f, at.y - size * 0.5f, size, size), Icons::ForShip(ShipClass::Destroyer));
				GUI::color = old;
			}

			cs::String name = m->System->Name == "Sol System" ? cs::String("SOL") : cs::Strings::ToUpperInvariant(m->System->Name);
			Label(p + Vector2(12, -20), name, m->Here ? Accent : m->Known() ? Color::white() : Unknown, false);
			cs::String sub = m->Here ? cs::String("you are here")
				: !m->Known() ? cs::String("no news")
				: (m->Holder != nullptr ? cs::Cat(m->Holder->Name, "  ·  ") : cs::String("unclaimed  ·  ")) + Age(m->AgeDays);
			Label(p + Vector2(12, -5), sub, m->Holder != nullptr && showTerritory ? Tint(m->Holder->Color, 0.95f) : Dim, true);
		}
		GUI::EndClip();
	}

	void GalaxyMapScreen::FlowDots(Vector2 a, Vector2 b, const cs::Ref<MapLink>& l)
	{
		int n = 2 + Mathf::RoundToInt(5 * static_cast<float>(l->Flow));
		int towardB = Mathf::RoundToInt(n * (l->Crossings > 0 ? static_cast<float>(l->TowardB) / l->Crossings : 0.5f));
		float t = Time::unscaledTime * 0.12f;
		for (int i = 0; i < n; i++)
		{
			// (C#'s Name.Length counts UTF-16 units; the byte count here is the same for an ASCII name.)
			float f = Mathf::Repeat(t + i / static_cast<float>(n) + (static_cast<int>(l->A->Name.size()) * 0.137f), 1.0f);
			auto p = i < towardB ? Vector2::Lerp(a, b, f) : Vector2::Lerp(b, a, f);
			Disc(p, 2.6f, FlowColor);
		}
	}

	void GalaxyMapScreen::Barrier(Vector2 at, Vector2 toward)
	{
		auto d = (toward - at).normalized();
		auto p = at + d * 22;
		auto n = Vector2(-d.y, d.x);
		Line(p - n * 8, p + n * 8, FrontColor, 3.0f);
	}

	Color GalaxyMapScreen::StarColor(const cs::Ref<StarSystem>& s)
	{
		auto root = s->Catalog->Root();
		for (const auto& b : s->Catalog->Bodies())
			if (b->IsStar())
			{
				root = b;
				break;
			}
		auto c = Color(root->Color.R, root->Color.G, root->Color.B);
		float max = Mathf::Max(c.r, Mathf::Max(c.g, c.b));
		// (Unity's Color / float divides every channel, alpha included.)
		c = max > 0 ? Color(c.r / max, c.g / max, c.b / max, c.a / max) : Color::white();
		return Color::Lerp(c, Color::white(), 0.35f);
	}

	Color GalaxyMapScreen::Tint(const Rgb& c, float a) { return Color(c.R, c.G, c.B, a); }

	cs::String GalaxyMapScreen::Age(double days)
	{
		return cs::Double::IsNaN(days) ? cs::String("no news") : days <= 0 ? cs::String("live") : cs::Cat("news ", LightSpeed::Describe(days), " old");
	}

	// ---------------------------------------------------------------- panels

	void GalaxyMapScreen::DrawHeader(Rect r, double jd)
	{
		GUI::Box(r, GUIContent::none(), Panel());
		GUI::Label(Rect(r.x + 14, r.y + 7, 220, 20), "GALAXY MAP", title);
		int known = 0;
		for (const auto& m : model->Systems)
			if (m->Known()) known++;
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(r.x + 14, r.y + 26, r.width - 530, 16),
			cs::Cat("As known in ", app->ActiveSystem->Name, "   ·   ", known, " of ", model->Systems.Count(), " systems heard from   ·   ",
				model->Links.Count(), " gate links   ·   traffic over the last ", cs::Fmt(GalaxyMap::FlowWindowDays, "0"), " days"), small);
		GUI::contentColor = old;
		if (GUI::Button(Rect(r.xMax() - 470, r.y + 10, 120, 26), diplomacyMode ? "SYSTEM DETAIL" : "DIPLOMACY", diplomacyMode ? buttonOn : button))
		{
			diplomacyMode = !diplomacyMode;
			commandMode = false;
			warArmed = false;
		}
		if (GUI::Button(Rect(r.xMax() - 340, r.y + 10, 120, 26), commandMode ? "SYSTEM DETAIL" : "COMMAND", commandMode ? buttonOn : button))
		{
			commandMode = !commandMode;
			diplomacyMode = false;
			reportAt = -10.0f;
		}
		if (GUI::Button(Rect(r.xMax() - 210, r.y + 10, 120, 26), "Reset view", button)) fitted = false;
		if (GUI::Button(Rect(r.xMax() - 82, r.y + 10, 72, 26), "Close", button)) app->ShowGalaxyMap = false;
	}

	void GalaxyMapScreen::DrawLegend(Rect r)
	{
		GUI::Box(r, GUIContent::none(), Panel());
		float y = r.y + 10;
		float x = r.x + 12, w = r.width - 24;
		Heading(y, x, w, "LAYERS");
		Toggle(y, x, w, showTerritory, "Territory", "each system washed in its holder's colour");
		Toggle(y, x, w, showFlows, "Traffic", "gate crossings: thicker is busier");
		Toggle(y, x, w, showFronts, "Fronts & blockades", "war fronts, shut gates, sieges");
		Toggle(y, x, w, showPirates, "Raiders", "raider hulls, havens and raids");
		Toggle(y, x, w, showConstruction, "Gates being built", "dashed, filled as far as built");
		y += 6;

		Heading(y, x, w, "KEY");
		Swatch(y, x, w, Color(0.45f, 0.75f, 0.95f), "gate link", false);
		Swatch(y, x, w, Unknown, "link to a system never heard from", true);
		Swatch(y, x, w, FlowColor, "traffic through the gate", false);
		Swatch(y, x, w, Accent, "the way there by the gates", false);
		Swatch(y, x, w, CourseColor, "plotted FTL course, through open space", true);
		Swatch(y, x, w, FrontColor, "war front (dashed) or shut gate (bar)", true);
		Swatch(y, x, w, PirateColor, "raiders about", false);
		y += 6;

		Heading(y, x, w, "WARS KNOWN");
		if (model->Wars.Count() == 0) Line(y, x, w, "none: the polities are at peace", Dim);
		for (const auto& war : model->Wars)
		{
			if (y > r.yMax() - 110) break;
			Line(y, x, w, cs::Cat(war.A->Name, "  vs  ", war.B->Name), FrontColor);
		}
		y += 6;

		Heading(y, x, w, "HOLDERS");
		cs::List<cs::Ref<Faction>> seen;
		for (const auto& m : model->Systems)
		{
			if (m->Holder == nullptr || seen.Contains(m->Holder)) continue;
			seen.Add(m->Holder);
			if (y > r.yMax() - 90) break;
			int held = 0;
			for (const auto& n : model->Systems)
				if (n->Holder == m->Holder) held++;
			Line(y, x, w, cs::Cat("■  ", m->Holder->Name, "   ", held, (held == 1 ? " system" : " systems")), Tint(m->Holder->Color, 1));
		}

		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(x, r.yMax() - 92, w, 86),
			"Click a star to inspect it, double-click to travel.\nAt the helm: plot an FTL course to any star.\nDrag to turn, shift-drag to pan, wheel to zoom.\n[ or Esc closes the map.", small);
		GUI::contentColor = old;
	}

	void GalaxyMapScreen::DrawDetail(Rect r, double jd)
	{
		GUI::Box(r, GUIContent::none(), Panel());
		if (selectedStar.has_value())
		{
			// (C#'s `selectedStar is NearbyStar star`: a copy.)
			NearbyStar star = *selectedStar;
			DrawStarDetail(r, star);
			return;
		}
		auto s = selected != nullptr ? selected : app->ActiveSystem;
		auto m = model->Of(s);
		if (m == nullptr) return;
		float x = r.x + 14, w = r.width - 28, y = r.y + 10;
		GUI::Label(Rect(x, y, w, 22), cs::Strings::ToUpperInvariant(s->Name), title);
		y += 24;
		bool here = s == app->ActiveSystem;
		if (here) Line(y, x, w, "You are here: seen as it is.", Accent);
		else
		{
			auto route = app->Galaxy->Network->Route(app->ActiveSystem, s, nullptr, jd);
			cs::String way = route == nullptr ? cs::String("no way through the gates") : cs::Cat(route->Count(), (route->Count() == 1 ? " gate, " : " gates, "),
				Days(GateNetwork::RouteDays(route)), " in the gates");
			Line(y, x, w, cs::Cat(cs::Fmt(Core::Galaxy::DistanceLy(app->ActiveSystem, s), "0.00"), " ly away   ·   ", way), Color::white());
			auto pilotFaction = app->Piloted.Ship != nullptr ? app->Piloted.Ship->Faction : nullptr;
			auto openRoute = app->Galaxy->Network->Route(app->ActiveSystem, s, pilotFaction != nullptr ? pilotFaction : app->ActiveSystem->Holder(), jd);
			if (route != nullptr && openRoute == nullptr) Line(y, x, w, "Every way there is shut to you just now", FrontColor);
			cs::String news = app->Galaxy->Traffic != nullptr ? app->Galaxy->Traffic->News->Describe(app->ActiveSystem, s, jd) : cs::String();
			Line(y, x, w, !news.empty() ? news : cs::String("no news"), Dim);
			if (GUI::Button(Rect(x, y + 2, w, 28), app->IsPiloting() ? "PLOT A COURSE HERE THROUGH THE GATES" : "TRAVEL HERE THROUGH THE GATES", buttonOn)) Travel(s);
			y += 36;
			DrawFtlSection(y, x, w, FtlTarget::OfStar(s));
		}
		y += 4;

		if (!m->Known())
		{
			Line(y, x, w, "Nothing has ever been heard from it: a star on the charts, and its gates.", Dim);
			y += 6;
		}
		else
		{
			Heading(y, x, w, "WHO HOLDS IT");
			if (m->Holdings.Count() == 0) Line(y, x, w, "Nobody: no settled markets", Dim);
			for (const auto& h : m->Holdings)
				Line(y, x, w, cs::Cat(h->Faction->Name, (h->Faction == m->Holder ? "  (holds it)" : ""), "   ", h->Ports, (h->Ports == 1 ? " port, " : " ports, "), People(h->Population)), Tint(h->Faction->Color, 1));
			if (m->Contested) Line(y, x, w, cs::Cat("Contested: ", m->ContestedBy.A->Name, " and ", m->ContestedBy.B->Name, " are at war here"), FrontColor);
			for (const auto& t : m->Threats)
			{
				cs::String what = t.BesiegedBy != nullptr ? cs::Cat("under siege by the ", t.BesiegedBy->Name) : cs::Cat("blockaded by the ", t.BlockadedBy->Name);
				Line(y, x, w, cs::Cat(t.Port, " ", what), FrontColor);
			}
			y += 6;

			Heading(y, x, w, "RAIDERS");
			if (m->Pirates == PirateActivity::None) Line(y, x, w, "None reported", Dim);
			else
				Line(y, x, w, cs::Cat((m->Pirates == PirateActivity::High ? "Heavy: " : "Some: "), m->PirateShips, " armed raiders, ", m->Havens,
					(m->Havens == 1 ? " haven known, " : " havens known, "), m->Raids, (m->Raids == 1 ? " raid" : " raids"), " through its gates"), PirateColor);
			y += 6;
		}

		Heading(y, x, w, "GATES");
		for (const auto& l : model->Links)
		{
			if (l->A != s && l->B != s) continue;
			if (y > r.yMax() - 24) break;
			auto other = l->Other(s);
			cs::String line;
			Color c;
			if (l->UnderConstruction)
			{
				line = cs::Cat("to ", other->Name, ": being built, ", cs::Fmt(l->Progress * 100, "0"), "%", (l->Builder != nullptr ? cs::Cat(" by the ", l->Builder->Name) : cs::String()));
				c = BuildColor;
			}
			else
			{
				line = cs::Cat("to ", other->Name, "   ", cs::Fmt(l->Link->Ly, "0.00"), " ly");
				if (!l->Known()) line += "   traffic unknown";
				else
				{
					line += cs::Cat("   ", l->Crossings, (l->Crossings == 1 ? " crossing" : " crossings"));
					if (l->Tonnes > 0) line += cs::Cat(", ", Tonnes(l->Tonnes));
					if (l->Raids > 0) line += cs::Cat(", ", l->Raids, (l->Raids == 1 ? " raid" : " raids"));
				}
				bool shutHere = l->A == s ? l->ShutAtA : l->ShutAtB;
				bool shutThere = l->A == s ? l->ShutAtB : l->ShutAtA;
				if (shutHere) line += "   SHUT here";
				else if (shutThere) line += "   SHUT at the far end";
				if (l->WarFront) line += "   FRONT";
				c = l->Shut() || l->WarFront ? FrontColor : l->Known() ? Color::white() : Dim;
			}
			Line(y, x, w, line, c);
		}
	}

	// ---------------------------------------------------------------- command (U.SPC-T423)

	cs::List<cs::Ref<Faction>> GalaxyMapScreen::CommandableStates()
	{
		auto polities = app->Galaxy->Polities;
		cs::List<cs::Ref<Faction>> list;
		for (const auto& f : polities->Factions())
		{
			if (f->IsPirate || !f->IsMilitary) continue;
			// (C#'s foreach that adds and breaks on the first settlement: any settlement at all.)
			if (polities->SettlementsOf(f).Count() > 0) list.Add(f);
		}
		return list;
	}

	void GalaxyMapScreen::DrawCommand(Rect r, double jd)
	{
		auto command = app->Galaxy->Polities->Command;
		auto states = CommandableStates();
		float x = r.x + 14, w = r.width - 28, y = r.y + 10;
		if (states.Count() == 0)
		{
			Line(y, x, w, "No polity with a navy to command.", Dim);
			return;
		}
		if (commanded == nullptr || !states.Contains(commanded))
		{
			auto mine = command->Player != nullptr ? command->Player : (app->Piloted.Ship != nullptr ? app->Piloted.Ship->Faction : nullptr);
			auto holder = app->ActiveSystem->Holder();
			commanded = mine != nullptr && states.Contains(mine) ? mine : holder != nullptr && states.Contains(holder) ? holder : states[0];
			if (!CoopSpeaks(commanded)) // US-T47: the party's speaker goes through the frames
				command->Player = commanded;
			reportAt = -10.0f;
		}
		if (report == nullptr || report->Faction != commanded || Time::unscaledTime - reportAt > 0.5f)
		{
			report = command->Report(commanded, jd);
			reportAt = Time::unscaledTime;
		}

		if (GUI::Button(Rect(x, y, 26, 22), "<", button)) Cycle(states, -1);
		if (GUI::Button(Rect(x + w - 26, y, 26, 22), ">", button)) Cycle(states, 1);
		auto old = GUI::contentColor;
		GUI::contentColor = Tint(commanded->Color, 1);
		GUI::Label(Rect(x + 34, y + 2, w - 68, 22), cs::Strings::ToUpperInvariant(commanded->Name), title);
		GUI::contentColor = old;
		y += 28;

		Line(y, x, w, cs::Cat(report->Systems, (report->Systems == 1 ? " system held, " : " systems held, "), report->Ports, (report->Ports == 1 ? " port, " : " ports, "),
			People(report->Population)), Color::white());
		Line(y, x, w, cs::Cat("Treasury ", cs::Fmt(report->Treasury, "N0"), " cr   ·   unrest ", cs::Fmt(report->Unrest * 100, "0"), "%"), Color::white());
		Line(y, x, w, cs::Cat("Navy: ", report->Warships, " hulls in ", report->Groups, " groups (", report->GateFleets, " gate, ", report->StrikeGroups, " jump strike), strength ", cs::Fmt(report->Strength, "N0"), "; ",
			report->Commanded, " under your orders", (report->Campaigns > 0 ? cs::Cat(", ", report->Campaigns, " on campaign") : cs::String())), Color::white());
		cs::List<cs::String> warNames;
		for (const auto& f : report->AtWarWith) warNames.Add(f->Name);
		Line(y, x, w, report->AtWarWith.Count() == 0 ? cs::String("At peace") : cs::Cat("At war with the ", cs::Strings::Join(", the ", warNames)),
			report->AtWarWith.Count() == 0 ? Dim : FrontColor);
		Line(y, x, w, cs::Cat("Marines x", cs::Fmt(report->MarineQuality, "0.00"), ", crew morale ", cs::Fmt(report->Morale, "0.00"), ", ",
			cs::Fmt(report->CrewLost, "N0"), " spacers lost"), Dim);
		y += 4;

		Heading(y, x, w, "PRIORITIES");
		auto d = CoopDirectives(command, commanded); // US-T47: command->Of(commanded) in single-player
		if (GUI::Button(Rect(x, y, w, 22), cs::Cat("Production: ", d->Production->Name), button))
			if (!CoopSendsProduction(Next(ProductionFocus::All(), d->Production))) // US-T47
			command->SetProduction(commanded, Next(ProductionFocus::All(), d->Production));
		y += 24;
		Line(y, x, w, d->Production->Description, Dim);
		if (GUI::Button(Rect(x, y, w, 22), cs::Cat("Build: ", d->Build->Name), button)) { if (!CoopSendsBuild(Next(BuildPlan::All(), d->Build))) command->SetBuild(commanded, Next(BuildPlan::All(), d->Build)); } // US-T47
		y += 24;
		Line(y, x, w, d->Build->Description, Dim);
		cs::String research = d->Research == nullptr ? cs::String("nothing")
			: cs::Cat(d->Research->Name, " ", (d->Level(d->Research) + 1), "  (", cs::Fmt(d->ResearchPoints, "0"), " of ", cs::Fmt(d->Research->CostOf(d->Level(d->Research)), "0"), ")");
		if (d->Research != nullptr && d->Level(d->Research) >= d->Research->MaxLevel) research = cs::Cat(d->Research->Name, ": every level reached");
		if (GUI::Button(Rect(x, y, w, 22), cs::Cat("Research: ", research), button))
		{
			cs::List<cs::Ref<ResearchTrack>> tracks;
			tracks.Add(nullptr);
			for (const auto& t : ResearchTrack::All()) tracks.Add(t);
			if (!CoopSendsResearch(tracks[(tracks.IndexOf(d->Research) + 1) % tracks.Count()])) // US-T47
			command->SetResearch(commanded, tracks[(tracks.IndexOf(d->Research) + 1) % tracks.Count()]);
		}
		y += 24;
		cs::List<cs::String> levels;
		for (const auto& t : ResearchTrack::All()) levels.Add(cs::Cat(t->Name, " ", d->Level(t)));
		Line(y, x, w, cs::Strings::Join("   ·   ", levels), Dim);
		y += 4;

		auto target = selected != nullptr ? selected : app->ActiveSystem;
		Heading(y, x, w, cs::Cat("ORDERS FOR ", cs::Strings::ToUpperInvariant(target->Name)));
		float third = (w - 8) / 3;
		const char* scopes[] = { "SQUADRON", "SYSTEM", "EMPIRE" };
		for (int i = 0; i < 3; i++)
			if (GUI::Button(Rect(x + i * (third + 4), y, third, 22), scopes[i], static_cast<int>(commandScope) == i ? buttonOn : button)) commandScope = static_cast<CommandScope>(i);
		y += 26;
		cs::Ref<Core::World> from = commandGroup != nullptr ? command->WorldOf(commandGroup) : nullptr;
		cs::String who;
		if (commandScope == CommandScope::Group) who = commandGroup != nullptr ? cs::Cat("the ", commandGroup->Name, "'s group") : cs::String("pick a group below");
		else if (commandScope == CommandScope::System)
		{
			auto system = command->SystemOf(from != nullptr ? from : app->ActiveSystem->World);
			who = cs::Cat("every group in ", (system != nullptr ? system->Name : cs::String("this system")));
		}
		else who = cs::Cat("every group of the ", commanded->Name);
		Line(y, x, w, cs::Cat("To: ", who), Dim);
		const ObjectiveKind kinds[] = { ObjectiveKind::Take, ObjectiveKind::Hold, ObjectiveKind::Blockade, ObjectiveKind::Escort, ObjectiveKind::Explore };
		float fifth = (w - 16) / 5;
		for (int i = 0; i < static_cast<int>(std::size(kinds)); i++)
			if (GUI::Button(Rect(x + i * (fifth + 4), y, fifth, 24), cs::Strings::ToUpperInvariant(Core::ToString(kinds[i])), buttonOn)) Give(command, kinds[i], target, from, jd);
		y += 28;
		if (GUI::Button(Rect(x, y, w, 22), "CLEAR ORDERS (back to their own command's plans)", button))
		{
			if (CoopSendsClear(from != nullptr ? from : app->ActiveSystem->World)) { reportAt = -10.0f; return; } // US-T47
			int n = command->Clear(commanded, commandScope, commandGroup, from != nullptr ? from : app->ActiveSystem->World);
			app->ShowToast(n == 0 ? cs::String("No group there was under orders") : cs::Cat(n, (n == 1 ? " group stands down" : " groups stand down")));
			reportAt = -10.0f;
		}
		y += 28;

		Heading(y, x, w, cs::Cat("FLEET  (", report->Fleet.Count(), " groups)"));
		auto list = Rect(x - 4, y, w + 8, Mathf::Max(40.0f, r.yMax() - y - 8));
		constexpr float rowH = 34;
		groupScroll = GUI::BeginScrollView(list, groupScroll, Rect(0, 0, w - 12, report->Fleet.Count() * rowH));
		for (int i = 0; i < report->Fleet.Count(); i++)
		{
			const auto& g = report->Fleet[i];
			bool on = g.Leader == commandGroup;
			if (GUI::Button(Rect(0, i * rowH, w - 12, rowH - 2), GUIContent::none(), on ? buttonOn : button))
			{
				commandGroup = g.Leader;
				commandScope = CommandScope::Group;
			}
			GUI::Label(Rect(6, i * rowH + 1, w - 24, 16),
				cs::Cat(g.Leader->Name, "  (", g.Hulls, (g.Hulls == 1 ? " hull" : " hulls"), ", ", cs::Fmt(g.Strength, "N0"), ")   ", g.System), labelDim);
			auto c = GUI::contentColor;
			GUI::contentColor = g.Objective.empty() ? Dim : Accent;
			GUI::Label(Rect(6, i * rowH + 16, w - 24, 16), g.Objective.empty() ? g.Order : cs::Cat(g.Objective, "  ·  ", g.Order), small);
			GUI::contentColor = c;
		}
		GUI::EndScrollView();
	}

	void GalaxyMapScreen::Cycle(const cs::List<cs::Ref<Faction>>& states, int step)
	{
		int i = states.IndexOf(commanded);
		commanded = states[((i < 0 ? 0 : i) + step + states.Count()) % states.Count()];
		if (!CoopSpeaks(commanded)) // US-T47
		app->Galaxy->Polities->Command->Player = commanded;
		commandGroup = nullptr;
		reportAt = -10.0f;
	}

	void GalaxyMapScreen::Give(const cs::Ref<StrategicCommand>& command, ObjectiveKind kind, const cs::Ref<StarSystem>& target, const cs::Ref<Core::World>& from, double jd)
	{
		if (commandScope == CommandScope::Group && commandGroup == nullptr)
		{
			app->ShowToast("Pick a group from the fleet list first, or give the order to a whole system or the empire");
			return;
		}
		// An escort for your own hull, when you are flying in that system and are no enemy of theirs; else command's pick.
		cs::Ref<Ship> charge = nullptr;
		auto pilot = app->Piloted.Ship;
		if (kind == ObjectiveKind::Escort && pilot != nullptr && target->World->Fleet->IsPresent(pilot) && !app->Galaxy->Polities->Diplomacy->Hostile(commanded, pilot->Faction))
			charge = pilot;
		if (CoopSendsOrder(kind, target, from, charge)) { reportAt = -10.0f; return; } // US-T47
		int given = 0;
		cs::String why = command->Order(commanded, commandScope, commandGroup, from != nullptr ? from : app->ActiveSystem->World, kind, target->World, jd, given, -1, charge);
		app->ShowToast(!why.empty() ? why : cs::Cat(given, (given == 1 ? " group: " : " groups: "), cs::Strings::ToLowerInvariant(command->Describe(kind, target->World, -1, charge))));
		reportAt = -10.0f;
	}

	// ---------------------------------------------------------------- diplomacy (U.SPC-T427)

	void GalaxyMapScreen::DrawDiplomacy(Rect r, double jd)
	{
		auto statecraft = app->Galaxy->Polities->Statecraft;
		auto command = app->Galaxy->Polities->Command;
		cs::List<cs::Ref<Faction>> states;
		for (const auto& f : CommandableStates())
			if (Statecraft::Party(f)) states.Add(f);
		float x = r.x + 14, w = r.width - 28, top = r.y + 10;
		if (states.Count() == 0)
		{
			Line(top, x, w, "No state to speak for.", Dim);
			return;
		}
		if (commanded == nullptr || !states.Contains(commanded))
		{
			auto mine = command->Player != nullptr ? command->Player : (app->Piloted.Ship != nullptr ? app->Piloted.Ship->Faction : nullptr);
			auto holder = app->ActiveSystem->Holder();
			commanded = mine != nullptr && states.Contains(mine) ? mine : holder != nullptr && states.Contains(holder) ? holder : states[0];
			envoyWith = nullptr;
		}
		CoopAdoptSpeaker(states); // US-T47: the party's speaker, which another player may have picked
		if (!CoopSpeaks(commanded)) // US-T47
		command->Player = commanded;

		if (GUI::Button(Rect(x, top, 26, 22), "<", button)) CycleSpeaker(states, -1);
		if (GUI::Button(Rect(x + w - 26, top, 26, 22), ">", button)) CycleSpeaker(states, 1);
		auto old = GUI::contentColor;
		GUI::contentColor = Tint(commanded->Color, 1);
		GUI::Label(Rect(x + 34, top + 2, w - 68, 22), cs::Strings::ToUpperInvariant(commanded->Name), title);
		GUI::contentColor = old;
		top += 28;

		auto view = Rect(r.x + 4, top, r.width - 8, r.yMax() - top - 6);
		diplomacyScroll = GUI::BeginScrollView(view, diplomacyScroll, Rect(0, 0, view.width - 16, Mathf::Max(diplomacyHeight, view.height)));
		x = 10;
		w = view.width - 36;
		float y = 0;
		auto overlord = statecraft->OverlordOf(commanded);
		Line(y, x, w, cs::Cat("Treasury ", cs::Fmt(commanded->Treasury, "N0"), " cr", (overlord != nullptr ? cs::Cat("   ·   vassal of the ", statecraft->OverlordOf(commanded)->Name) : cs::String())), Color::white());

		// What the others have put to you.
		cs::List<cs::Ref<Proposal>> waiting;
		for (const auto& p : statecraft->Inbox())
			if (p->To == commanded) waiting.Add(p);
		if (waiting.Count() > 0)
		{
			Heading(y, x, w, "AWAITING YOUR ANSWER");
			for (const auto& p : waiting)
			{
				double left = Statecraft::InboxDays - (jd - p->Jd);
				Line(y, x, w, cs::Cat(Capital(p->Describe()), "  (", cs::Fmt(cs::Math::Max(0, left), "0"), " days to answer)"), Accent);
				float half = (w - 4) / 2;
				if (GUI::Button(Rect(x, y, half, 22), "ACCEPT", buttonOn))
					if (!CoopSendsAnswer(p, true)) // US-T47
					app->ShowToast(statecraft->Answer(p, true, jd) ? cs::Cat("Agreed: ", p->Describe()) : cs::String("That offer no longer stands"));
				if (GUI::Button(Rect(x + half + 4, y, half, 22), "DECLINE", button))
				{
					if (CoopSendsAnswer(p, false)) { y += 26; continue; } // US-T47
					statecraft->Answer(p, false, jd);
					app->ShowToast(cs::Cat("Declined: ", p->Describe()));
				}
				y += 26;
			}
		}

		Heading(y, x, w, "TREAT WITH");
		for (const auto& g : statecraft->Parties())
		{
			if (g == commanded) continue;
			if (envoyWith == nullptr) envoyWith = g;
			bool on = g == envoyWith;
			if (GUI::Button(Rect(x, y, w, 34), GUIContent::none(), on ? buttonOn : button))
			{
				envoyWith = g;
				warArmed = false;
			}
			auto c = GUI::contentColor;
			GUI::contentColor = Tint(g->Color, 1);
			GUI::Label(Rect(x + 6, y + 1, w - 12, 16), g->Name, labelDim);
			GUI::contentColor = statecraft->Polities->Diplomacy->Get(commanded, g) == Relation::War ? FrontColor : Dim;
			GUI::Label(Rect(x + 6, y + 16, w - 12, 16), cs::Cat(Statecraft::Mood(statecraft->Opinion(g, commanded)), "  ·  ", statecraft->Standing(commanded, g)), small);
			GUI::contentColor = c;
			y += 36;
		}

		if (envoyWith != nullptr && envoyWith != commanded && Statecraft::Party(envoyWith))
		{
			auto them = envoyWith;
			y += 4;
			Heading(y, x, w, cs::Cat("ENVOY TO THE ", cs::Strings::ToUpperInvariant(them->Name)));
			Line(y, x, w, statecraft->Hail(commanded, them, jd), Color::white());
			Line(y, x, w, cs::Cat("They think of you ", cs::Fmt(statecraft->Opinion(them, commanded), "0"), " (", Statecraft::Mood(statecraft->Opinion(them, commanded)),
				"); you of them ", cs::Fmt(statecraft->Opinion(commanded, them), "0"), ".  Navies ",
				cs::Fmt(statecraft->Power(commanded), "N0"), " to ", cs::Fmt(statecraft->Power(them), "N0")), Dim);

			for (const auto& p : statecraft->PactsOf(commanded))
			{
				if (!p->Involves(them)) continue;
				cs::String until = cs::Double::IsNaN(p->UntilJd) ? cs::String() : cs::Cat("  (", cs::Fmt(cs::Math::Max(0, p->UntilJd - jd), "0"), " days left)");
				Line(y, x, w, Capital(p->Describe()) + until, CourseColor);
				if (GUI::Button(Rect(x, y, w, 20), p->Kind == PactKind::Vassalage() && p->A == commanded ? "THROW OFF THEIR RULE" : "BREAK IT", button))
				{
					if (CoopSendsBreak(p)) { y += 24; continue; } // US-T47
					statecraft->Break(p, commanded, jd);
					app->ShowToast(cs::Cat("You break the ", p->Describe(), ": they will not forget it"));
				}
				y += 24;
			}

			Heading(y, x, w, "PROPOSE");
			for (const auto& o : statecraft->Envoy(commanded, them, jd))
			{
				cs::String name = cs::Strings::ToUpperInvariant(o->Deal->Name) + (o->Deal->HasAmount ? cs::Cat("  ", cs::Fmt(o->Amount, "N0"), " cr a month") : cs::String());
				bool can = o->Unavailable.empty();
				auto was = GUI::enabled;
				GUI::enabled = was && can;
				if (GUI::Button(Rect(x, y, w, 22), name, can && o->Read->Accepted() ? buttonOn : button))
				if (!CoopSendsPropose(o, them)) // US-T47
				{
					cs::Ref<Appraisal> answer;
					cs::String why = statecraft->Propose(o->Deal, commanded, them, jd, answer, o->Amount);
					app->ShowToast(!why.empty() ? why : (answer == nullptr ? cs::String("Put to them: they will answer") : cs::Cat((answer->Accepted() ? "Agreed: " : "Refused: "), answer->Main())));
				}
				GUI::enabled = was;
				y += 24;
				if (!can) Line(y, x + 8, w - 8, o->Unavailable, Unknown);
				else Line(y, x + 8, w - 8, cs::Cat((o->Read->Accepted() ? "They would agree: " : "They would refuse: "), o->Read->Main()), o->Read->Accepted() ? CourseColor : FrontColor);
			}

			double gift = Statecraft::SuggestedGift(commanded);
			if (GUI::Button(Rect(x, y, w, 22), cs::Cat("SEND A GIFT  ", cs::Fmt(gift, "N0"), " cr"), button))
			if (!CoopSendsGift(them, gift)) // US-T47
			{
				cs::String refused = statecraft->Gift(commanded, them, gift, jd);
				app->ShowToast(!refused.empty() ? refused : cs::Cat("The ", them->Name, " receives your gift"));
			}
			y += 26;
			if (statecraft->Polities->Diplomacy->Get(commanded, them) != Relation::War)
			{
				if (GUI::Button(Rect(x, y, w, 24), warArmed ? "CONFIRM: DECLARE WAR (breaks every pact with them)" : "DECLARE WAR", buttonOn))
				{
					if (!warArmed) warArmed = true;
					else
					{
						warArmed = false;
						if (!CoopSendsWar(them)) // US-T47
						{
						cs::String why = statecraft->DeclareWar(commanded, them, jd);
						app->ShowToast(!why.empty() ? why : cs::Cat("The ", commanded->Name, " is at war with the ", them->Name));
						} // US-T47
					}
				}
				y += 28;
			}
		}

		Heading(y, x, w, "LATELY");
		const auto& record = statecraft->Record();
		if (record.Count() == 0) Line(y, x, w, "Nothing has passed between the states yet.", Dim);
		for (int i = record.Count() - 1; i >= 0 && i >= record.Count() - 8; i--) Line(y, x, w, record[i], Dim);
		if (Event::current().type == EventType::Repaint) diplomacyHeight = y + 10;
		GUI::EndScrollView();
	}

	void GalaxyMapScreen::CycleSpeaker(const cs::List<cs::Ref<Faction>>& states, int step)
	{
		int i = states.IndexOf(commanded);
		commanded = states[((i < 0 ? 0 : i) + step + states.Count()) % states.Count()];
		if (!CoopSpeaks(commanded)) // US-T47
		app->Galaxy->Polities->Command->Player = commanded;
		envoyWith = nullptr;
		warArmed = false;
		commandGroup = nullptr;
		reportAt = -10.0f;
	}

	cs::String GalaxyMapScreen::Capital(const cs::String& s)
	{
		return s.empty() ? s : cs::Strings::ToUpperInvariant(cs::Strings::Substring(s, 0, 1)) + cs::Strings::Substring(s, 1);
	}

	void GalaxyMapScreen::DrawStarDetail(Rect r, const NearbyStar& star)
	{
		float x = r.x + 14, w = r.width - 28, y = r.y + 10;
		GUI::Label(Rect(x, y, w, 22), cs::Strings::ToUpperInvariant(star.Name), title);
		y += 24;
		Line(y, x, w, cs::Cat(cs::Fmt((star.PositionLy - app->ActiveSystem->PositionLy).Length(), "0.00"), " ly away   ·   ",
			(star.SpectralType.empty() ? cs::String("type unknown") : cs::Cat(star.SpectralType, " star"))), Color::white());
		Line(y, x, w, "A star on the charts: nobody simulates it yet and no gate goes there. A hull with its own FTL drive can fly "
			"to it, and drops out at the edge of its well.", Dim);
		y += 6;
		DrawFtlSection(y, x, w, FtlTarget::OfStar(star));
	}

	void GalaxyMapScreen::DrawFtlSection(float& y, float x, float w, const cs::Ref<FtlTarget>& target)
	{
		Heading(y, x, w, "FTL");
		if (!app->IsPiloting())
		{
			Line(y, x, w, "Take the helm of a capital ship or an explorer (P) to plot an FTL course here: no gates needed.", Dim);
			y += 4;
			return;
		}
		if (!app->CanPlotFtl())
		{
			Line(y, x, w, app->RidingLane() ? cs::String("Not from a lane: come off it first.") : HelmFtl::Refusal(app->Piloted.Ship), Dim);
			y += 4;
			return;
		}
		auto course = app->PlottedCourse;
		bool plotted = course != nullptr && FtlNavigation::Same(course->Target, target);
		if (plotted)
		{
			if (!course->Flyable()) Line(y, x, w, course->Refusal, FrontColor);
			else
			{
				if (course->NeedsOutRun())
					Line(y, x, w, cs::Cat("Out to the rim: ", cs::Fmt(course->OutRunKm / Astro::AuKm, "0.##"), " AU under the ordinary drive, ",
						FtlNavigation::Describe(course->OutRunDays)), Color::white());
				if (course->CoolDays > 0) Line(y, x, w, cs::Cat("The drive cools first: ", FtlNavigation::Describe(course->CoolDays)), Color::white());
				Line(y, x, w, cs::Cat("Jump: ", cs::Fmt(course->Ly, "0.00"), " ly, charging ", FtlNavigation::Describe(course->SpoolDays), " and ",
					FtlNavigation::Describe(course->FtlDays), " under FTL"), Color::white());
				if (course->Interdictions.Count() > 0)
					Line(y, x, w, cs::Cat("Pulled out by ", cs::Strings::Join(", ", course->Interdictions), " on the way: ",
						FtlNavigation::Describe(course->CrossingDays), " crossing under the ordinary drive"), PirateColor);
				Line(y, x, w, cs::Cat("ETA ", Astro::ToDateTime(course->EtaJd()).ToString("yyyy-MM-dd HH:mm"), " UTC, ", FtlNavigation::Describe(course->TotalDays()),
					" from now"), CourseColor);
				Line(y, x, w, cs::Cat("Reaction mass: ", cs::Fmt(course->FuelTonnes(), "N0"), " t of ", cs::Fmt(course->TanksTonnes, "N0"),
					" t in the tanks (one jump reaches ", (cs::Double::IsInfinity(course->RangeLy) ? cs::String("anywhere") : cs::Cat(cs::Fmt(course->RangeLy, "0.#"), " ly")), ")"), Dim);
			}
		}
		if (GUI::Button(Rect(x, y + 2, w * 0.48f, 28), plotted ? "PLOT AGAIN" : "PLOT FTL COURSE", button)) app->PlotFtlCourse(target);
		if (GUI::Button(Rect(x + w * 0.52f, y + 2, w * 0.48f, 28), "ENGAGE AUTOPILOT", buttonOn))
		{
			if (app->PlotFtlCourse(target, /*quiet:*/ true) && app->EngageFtlAutopilot()) app->ShowGalaxyMap = false;
			else if (app->PlottedCourse != nullptr && !app->PlottedCourse->Flyable()) app->ShowToast(UnitySpaceApp::CourseSummary(app->PlottedCourse));
		}
		y += 36;
	}

	cs::String GalaxyMapScreen::Days(double d)
	{
		return d < 1 ? cs::Cat(cs::Fmt(d * 24, "0"), " h") : cs::Cat(cs::Fmt(d, "0.0"), " days");
	}

	cs::String GalaxyMapScreen::People(double n)
	{
		return n >= 1e9 ? cs::Cat(cs::Fmt(n / 1e9, "0.0"), " bn people")
			: n >= 1e6 ? cs::Cat(cs::Fmt(n / 1e6, "0.0"), " m people")
			: cs::Cat(cs::Fmt(n, "N0"), " people");
	}

	cs::String GalaxyMapScreen::Tonnes(double t)
	{
		return t >= 1e6 ? cs::Cat(cs::Fmt(t / 1e6, "0.0"), " Mt")
			: t >= 1e3 ? cs::Cat(cs::Fmt(t / 1e3, "0"), " kt")
			: cs::Cat(cs::Fmt(t, "0"), " t");
	}

	// ---------------------------------------------------------------- drawing helpers

	const cs::Ref<GUIStyle>& GalaxyMapScreen::Panel()
	{
		if (!panelStyle)
		{
			panelStyle = UiStyle::PanelStyle(panelTex);
		}
		return panelStyle;
	}

	void GalaxyMapScreen::Heading(float& y, float x, float w, const cs::String& s)
	{
		auto old = GUI::contentColor;
		GUI::contentColor = Accent;
		GUI::Label(Rect(x, y, w, 16), s, small);
		GUI::contentColor = old;
		y += 18;
	}

	void GalaxyMapScreen::Line(float& y, float x, float w, const cs::String& s, Color c)
	{
		auto content = GUIContent(s);
		float h = Mathf::Max(16.0f, text->CalcHeight(content, w));
		auto old = GUI::contentColor;
		GUI::contentColor = c;
		GUI::Label(Rect(x, y, w, h), content, text);
		GUI::contentColor = old;
		y += h + 2;
	}

	void GalaxyMapScreen::Toggle(float& y, float x, float w, bool& on, const cs::String& name, const cs::String& hint)
	{
		if (GUI::Button(Rect(x, y, w, 22), cs::Cat((on ? "[x]  " : "[  ]  "), name), on ? buttonOn : button)) on = !on;
		y += 24;
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(x + 4, y, w - 4, 14), hint, small);
		GUI::contentColor = old;
		y += 17;
	}

	void GalaxyMapScreen::Swatch(float& y, float x, float w, Color c, const cs::String& s, bool dashed)
	{
		if (Event::current().type == EventType::Repaint)
		{
			auto a = Vector2(x, y + 8);
			auto b = Vector2(x + 30, y + 8);
			if (dashed) Dashed(a, b, c, 2.5f, 6, 4);
			else Line(a, b, c, 2.5f);
		}
		auto old = GUI::contentColor;
		GUI::contentColor = Color::white();
		GUI::Label(Rect(x + 38, y, w - 38, 16), s, small);
		GUI::contentColor = old;
		y += 18;
	}

	void GalaxyMapScreen::Label(Vector2 p, const cs::String& s, Color c, bool minor)
	{
		const auto& style = minor ? labelDim : label;
		auto content = GUIContent(s);
		auto size = style->CalcSize(content);
		auto old = GUI::contentColor;
		GUI::contentColor = Color(0, 0, 0, 0.85f);
		GUI::Label(Rect(p.x + 1, p.y + 1, size.x, size.y), content, style);
		GUI::contentColor = c;
		GUI::Label(Rect(p.x, p.y, size.x, size.y), content, style);
		GUI::contentColor = old;
	}

	void GalaxyMapScreen::Line(Vector2 a, Vector2 b, Color color, float thickness)
	{
		auto d = b - a;
		float length = d.magnitude();
		if (length < 0.5f) return;
		auto matrix = GUI::matrix;
		auto old = GUI::color;
		GUIUtility::RotateAroundPivot(Mathf::Atan2(d.y, d.x) * Mathf::Rad2Deg, a);
		GUI::color = color;
		GUI::DrawTexture(Rect(a.x, a.y - thickness * 0.5f, length, thickness), Icons::White());
		GUI::color = old;
		GUI::matrix = matrix;
	}

	void GalaxyMapScreen::Dashed(Vector2 a, Vector2 b, Color color, float thickness, float dash, float gap)
	{
		float length = (b - a).magnitude();
		if (length < 0.5f) return;
		auto d = (b - a) / length;
		for (float s = 0; s < length; s += dash + gap)
			Line(a + d * s, a + d * Mathf::Min(s + dash, length), color, thickness);
	}

	void GalaxyMapScreen::Disc(Vector2 p, float r, Color c)
	{
		auto old = GUI::color;
		GUI::color = c;
		GUI::DrawTexture(Rect(p.x - r, p.y - r, r * 2, r * 2), glow);
		GUI::color = old;
	}

	void GalaxyMapScreen::Ring(Vector2 p, float r, Color c, float thickness)
	{
		constexpr int n = 28;
		for (int i = 0; i < n; i++)
		{
			float a0 = i * Mathf::PI * 2 / n, a1 = (i + 1) * Mathf::PI * 2 / n;
			Line(p + Vector2(Mathf::Cos(a0), Mathf::Sin(a0)) * r, p + Vector2(Mathf::Cos(a1), Mathf::Sin(a1)) * r, c, thickness);
		}
	}

	void GalaxyMapScreen::Brackets(Vector2 p, float half, Color c)
	{
		float len = half * 0.5f;
		auto old = GUI::color;
		GUI::color = c;
		const auto& w = Icons::White();
		auto r = Rect(p.x - half, p.y - half, half * 2, half * 2);
		GUI::DrawTexture(Rect(r.xMin(), r.yMin(), len, 2), w);
		GUI::DrawTexture(Rect(r.xMin(), r.yMin(), 2, len), w);
		GUI::DrawTexture(Rect(r.xMax() - len, r.yMin(), len, 2), w);
		GUI::DrawTexture(Rect(r.xMax() - 2, r.yMin(), 2, len), w);
		GUI::DrawTexture(Rect(r.xMin(), r.yMax() - 2, len, 2), w);
		GUI::DrawTexture(Rect(r.xMin(), r.yMax() - len, 2, len), w);
		GUI::DrawTexture(Rect(r.xMax() - len, r.yMax() - 2, len, 2), w);
		GUI::DrawTexture(Rect(r.xMax() - 2, r.yMax() - len, 2, len), w);
		GUI::color = old;
	}

	void GalaxyMapScreen::EnsureStyles()
	{
		if (title != nullptr) return;
		// The shared look (US-T199, UiStyle.h) at overlay depth: the side panels sit over the chart. Map text is the dense
		// label size; the star labels are white (Heading) so they read over the chart, the panel body off-white.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTex = S::PanelFrame(S::Depth::Overlay);
		buttonTex = S::ControlTexture();
		buttonOnTex = S::ControlOnTexture();
		glow = SoftDisc();
		title = S::TitleStyle(T::Title);
		text = S::TextStyle(T::Label);
		text->wordWrap = true;
		small = S::CaptionStyle(T::Caption);
		label = S::TextStyle(T::Label, S::Heading, FontStyle::Bold);
		labelDim = S::TextStyle(T::Caption, S::Heading);
		button = S::ButtonStyle(T::Label, buttonTex, buttonOnTex);
		button->alignment = TextAnchor::MiddleLeft;
		button->padding = RectOffset(static_cast<int>(S::Space::Pad), S::Space::TextPad, 0, 0);
		buttonOn = S::ButtonOnStyle(button, buttonOnTex);
	}

	cs::Ref<Texture2D> GalaxyMapScreen::SoftDisc()
	{
		constexpr int n = 64;
		auto t = cs::New<Texture2D>(n, n, TextureFormat::RGBA32, false);
		t->hideFlags = HideFlags::DontSave;
		t->wrapMode = TextureWrapMode::Clamp;
		t->filterMode = FilterMode::Bilinear;
		for (int y = 0; y < n; y++)
			for (int x = 0; x < n; x++)
			{
				float dx = (x + 0.5f) / n * 2 - 1, dy = (y + 0.5f) / n * 2 - 1;
				float r = Mathf::Sqrt(dx * dx + dy * dy);
				float a = Mathf::Clamp01(1 - r);
				t->SetPixel(x, y, Color(1, 1, 1, Mathf::Clamp01(a * a * 1.6f + (r < 0.5f ? 0.35f * (1 - r * 2) : 0))));
			}
		t->Apply();
		return t;
	}

	void GalaxyMapScreen::DestroyOwnedTextures()
	{
		// (Object.Destroy -> dropping the reference; the styles made from them still hold theirs, as C#'s styles keep
		// pointing at the destroyed textures: the map goes with the HUD that owns it.)
		panelTex = nullptr;
		buttonTex = nullptr;
		buttonOnTex = nullptr;
		glow = nullptr;
	}
}
