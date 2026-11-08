// US-T258: the system map (declarations and the rule in UI/SystemMapScreen.h).

#include "UI/SystemMapScreen.h"
#include "UI/UiMotion.h" // US-T255

#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "PlayerOptions.h" // US-T252
#include "UI/Icons.h"
#include "UI/UiStyle.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Comms/Comms.h"
#include "Economy/Economy.h"
#include "Economy/GasHarvest.h"
#include "Economy/Settlement.h"
#include "Infrastructure/Infrastructure.h"
#include "Sensors/Sensors.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/Graveyards.h"
#include "Systems/JumpGate.h"
#include "Systems/SpaceFeatures.h"
#include "Systems/StarSystemBuilder.h"
#include "Systems/VoidSites.h"

#include "GameLore.h"
#include "Lore/LoreKeys.h"

#include <algorithm>
#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		constexpr float HeaderH = 46, LegendW = 230, DetailW = 340;
		/// The most ships drawn (nearest first): a crowded system stays readable and cheap.
		constexpr int MaxContacts = 240;
		/// How near the cursor (px) a point must be to be hovered or clicked.
		constexpr float PickPx = 12;
		/// Closer to its parent than this on the screen, an item is drawn small, unlabelled and not picked.
		constexpr float CrowdPx = 9;

		Color Of(const Rgb& c, float a = 1) { return Color(c.R, c.G, c.B, a); }

		cs::String Km(double km)
		{
			// US-T252: Settings > Gameplay > Distances.
			if (PlayerOptions::WriteAu(km, 0.01 * Astro::AuKm)) return cs::Cat(cs::Fmt(km / Astro::AuKm, km < 0.01 * Astro::AuKm ? "0.0000" : "0.00"), " AU");
			if (km >= 1e9) return PlayerOptions::KmText(km);
			if (km >= 1e6) return cs::Cat(cs::Fmt(km / 1e6, "0.0"), " million km");
			return cs::Cat(cs::Fmt(km, "N0"), " km");
		}

		cs::String Lower(const char* s) { return cs::Strings::ToLowerInvariant(cs::String(s)); }

		const Color GateColor{ 0.7f, 0.82f, 1.0f };
		const Color LaneColor{ 0.35f, 0.62f, 0.9f };
		const Color RouteColor{ 1.0f, 0.86f, 0.35f };
		const Color GreyContact{ 0.62f, 0.66f, 0.7f };
	}

	void SystemMapScreen::Opened()
	{
		fitted = false;
		builtAt = -10.0f;
		dragging = false;
	}

	// ---------------------------------------------------------------- the items

	void SystemMapScreen::Build(double jd)
	{
		items.clear();
		auto system = app->ActiveSystem;
		auto catalog = app->Catalog();
		auto eph = app->Ephemeris();
		auto world = app->World();
		if (system == nullptr || catalog == nullptr || eph == nullptr || world == nullptr) return;
		const cs::String& sysName = system->Name;
		auto lanes = app->Fleet() != nullptr ? app->Fleet()->Lanes : nullptr;

		// The bodies, on their orbits, with the settlements on them (a haven no one has found stays off the map).
		for (int i = 0; i < catalog->Count(); i++)
		{
			auto b = (*catalog)[i];
			if (b == nullptr || !b->IsVisible()) continue;
			Item it;
			it.Body = i;
			it.Key = cs::Cat("b", i);
			it.Name = b->Name;
			it.Km = eph->Helio(i);
			it.What = Target::OfBody(i);
			it.Tint = Of(b->Color);
			cs::String site = VoidSites::KindLabel(b);
			it.Kind = b->IsStar() ? ItemKind::Star : !site.empty() ? ItemKind::Site : b->Kind == BodyKind::Station ? ItemKind::Station : ItemKind::Body;
			it.KindLabel = !site.empty() ? site : b->IsStar() ? cs::Cat("Star", b->SpectralType.empty() ? cs::String() : cs::Cat(", ", b->SpectralType))
				: b->Kind == BodyKind::DwarfPlanet ? cs::String("Dwarf planet") : cs::String(Core::ToString(b->Kind));
			it.Size = b->IsStar() ? 8.0f : b->Kind == BodyKind::Planet ? (b->GasGiant ? 6.0f : 5.0f) : b->Kind == BodyKind::Station ? 4.0f
				: 3.0f;
			if (b->ParentIndex >= 0)
			{
				it.HasParent = true;
				it.ParentKm = eph->Helio(b->ParentIndex);
			}
			it.LoreKey = LoreKeys::Body(b->Kind, sysName, b->Name);
			it.About = b->Summary;
			cs::List<cs::String> held;
			if (app->Economy() != nullptr)
				for (const auto& s : app->Economy()->At(i))
				{
					if (s == nullptr || !s->Discovered()) continue;
					held.Add(cs::Cat(s->Name, s->Owner != nullptr ? cs::Cat(" (", s->Owner->Name, ")") : cs::String()));
					if (it.About.empty() && LoreShort(it.LoreKey).empty()) it.LoreKey = LoreKeys::Settlement(sysName, s->Name);
					if (s->Owner != nullptr && held.Count() == 1) it.Tint = Color::Lerp(it.Tint, Of(s->Owner->Color), 0.35f);
				}
			if (held.Count() > 0) it.Details = cs::Cat(held.Count() == 1 ? "Settlement: " : "Settlements: ", cs::Strings::Join(", ", held));
			items.push_back(it);
		}

		// Stations in orbit.
		auto infra = world->Infrastructure;
		if (infra != nullptr)
			for (int i = 0; i < infra->Structures.Count(); i++)
			{
				const auto& s = infra->Structures[i];
				if (s == nullptr) continue;
				Item it;
				it.Kind = ItemKind::Station;
				it.Key = cs::Cat("t", i);
				it.Name = s->Name;
				it.KindLabel = "Station in orbit";
				it.Km = s->HelioAt(eph, jd);
				it.Body = s->Body;
				it.HasParent = s->Body >= 0 && s->Body < catalog->Count();
				if (it.HasParent) it.ParentKm = eph->Helio(s->Body);
				it.What = Target::OfStructure(s);
				it.Tint = s->Owner() != nullptr ? Of(s->Owner()->Color) : UiStyle::Text;
				it.Size = 3.5f;
				it.Details = cs::Cat(s->Owner() != nullptr ? cs::Cat(s->Owner()->Name, ", ") : cs::String(), "over ", (*catalog)[s->Body]->Name);
				items.push_back(it);
			}

		// Gas works: at the first platform, the works' world the thing a cruise is bound for.
		if (world->Harvest != nullptr)
			for (int i = 0; i < world->Harvest->Operations.Count(); i++)
			{
				const auto& op = world->Harvest->Operations[i];
				if (op == nullptr || op->Body < 0 || op->Body >= catalog->Count()) continue;
				Item it;
				it.Kind = ItemKind::Gas;
				it.Key = cs::Cat("g", i);
				it.Name = op->Name;
				it.KindLabel = "Gas works";
				it.Km = op->Platforms.Count() > 0 ? world->Harvest->PlatformHelio(op, 0, jd) : eph->Helio(op->Body);
				it.Body = op->Body;
				it.HasParent = true;
				it.ParentKm = eph->Helio(op->Body);
				it.What = Target::OfBody(op->Body);
				it.Tint = op->Owner != nullptr ? Of(op->Owner->Color) : Color(0.8f, 0.9f, 0.55f);
				it.Size = 3;
				it.Details = cs::Cat(op->Platforms.Count(), op->Platforms.Count() == 1 ? " platform" : " platforms", " over ", (*catalog)[op->Body]->Name,
					op->Owner != nullptr ? cs::Cat(", ", op->Owner->Name) : cs::String());
				items.push_back(it);
			}

		// The jump gates, and the lanes' ramps.
		for (int i = 0; i < system->Gates().Count(); i++)
		{
			const auto& g = system->Gates()[i];
			if (g == nullptr) continue;
			Item it;
			it.Kind = ItemKind::Gate;
			it.Key = cs::Cat("j", i);
			it.Name = g->Name;
			it.KindLabel = "Jump gate";
			it.Km = g->PositionKm;
			it.Gate = g;
			bool down = g->Link != nullptr && !g->Link->Working();
			bool shut = g->Blockaded(jd) || (g->Partner != nullptr && g->Partner->Blockaded(jd));
			it.Tint = down || shut ? Color(1.0f, 0.5f, 0.42f) : GateColor;
			it.Size = 5;
			auto to = g->Destination();
			it.Details = cs::Cat(to != nullptr ? cs::Cat("To ", to->Name) : cs::String("Leads nowhere yet"),
				g->Link != nullptr ? cs::Cat(", ", cs::Fmt(g->Link->Ly, "0.00"), " ly, ", cs::Fmt(g->Link->TransitDays() * 24, "0"), " h") : cs::String(),
				down ? "  DOWN" : shut ? "  SHUT" : "");
			if (to != nullptr) it.LoreKey = LoreKeys::System(to->Name);
			items.push_back(it);
		}
		if (lanes != nullptr)
		{
			auto ramp = [&](const cs::Ref<Lane>& lane, int at)
			{
				Item it;
				it.Kind = ItemKind::Ramp;
				it.Key = cs::Cat("r", lane->Id, ":", at);
				it.Name = cs::Cat(lane->Name, lane->IsGateSpur() ? " spur" : " lane");
				it.KindLabel = "On-ramp";
				it.Km = lanes->RampHelio(lane, at, jd);
				it.Body = at;
				it.Tint = LaneColor;
				it.Size = 2.5f;
				it.HasParent = at >= 0 && at < catalog->Count();
				if (it.HasParent) it.ParentKm = eph->Helio(at);
				int other = lane->Other(at);
				it.Details = cs::Cat("By ", lanes->EndName(lane, at), ", to ", lanes->EndName(lane, other), lanes->Carries(lane) ? cs::String() : cs::String("  (FAILED)"));
				items.push_back(it);
			};
			for (const auto& lane : lanes->Lanes)
				if (lane != nullptr) { ramp(lane, lane->A); ramp(lane, lane->B); }
			for (const auto& spur : lanes->GateSpurs)
				if (spur != nullptr) ramp(spur, spur->A);
		}

		// Belts, ice fields, nebulae and graveyards: areas, picked by being inside them.
		for (int i = 0; i < system->Discs.Count(); i++)
		{
			const auto& d = system->Discs[i];
			if (d == nullptr) continue;
			Item it;
			it.Kind = ItemKind::Belt;
			it.Key = cs::Cat("d", i);
			it.Name = d->Name;
			it.KindLabel = "Debris belt";
			it.HostKm = d->HostIndex >= 0 && d->HostIndex < catalog->Count() ? eph->Helio(d->HostIndex) : DVec3::Zero;
			it.Km = it.HostKm;
			it.InnerKm = d->InnerKm;
			it.ReachKm = d->OuterKm;
			it.Tint = Color(0.75f, 0.68f, 0.55f);
			it.About = d->Summary;
			it.Details = cs::Cat(Km(d->InnerKm), " to ", Km(d->OuterKm), " out");
			items.push_back(it);
		}
		const auto& root = catalog->Root();
		const auto& features = SpaceFeatures::Of(root->Name);
		for (int i = 0; i < features.Nebulae.Count(); i++)
		{
			const auto& n = features.Nebulae[i];
			Item it;
			it.Kind = ItemKind::Nebula;
			it.Key = cs::Cat("n", i);
			it.Name = n.Name;
			it.KindLabel = "Nebula";
			it.Km = n.CentreKm;
			it.ReachKm = n.RadiusKm;
			it.Tint = n.Puffs.Count() > 0 ? Of(n.Puffs[0].Color) : Color(0.7f, 0.45f, 0.9f);
			it.Details = "Sensors fall short inside it, and looking into it";
			items.push_back(it);
		}
		for (int i = 0; i < features.IceFields.Count(); i++)
		{
			const auto& f = features.IceFields[i];
			Item it;
			it.Kind = ItemKind::IceField;
			it.Key = cs::Cat("i", i);
			it.Name = f.Name;
			it.KindLabel = "Ice field";
			it.Km = f.CentreKm;
			it.ReachKm = f.RadiusKm;
			it.Tint = Color(0.7f, 0.9f, 1.0f);
			it.Details = cs::Cat(f.Chunks.Count(), " chunks of ice across ", Km(2 * f.RadiusKm));
			items.push_back(it);
		}
		const auto& yards = Graveyards::Of(root);
		for (int i = 0; i < yards.Count(); i++)
		{
			const auto& y = yards[i];
			Item it;
			it.Kind = ItemKind::Graveyard;
			it.Key = cs::Cat("w", i);
			it.Name = y.Name;
			it.KindLabel = "Ship graveyard";
			it.Km = y.CentreKm;
			it.ReachKm = cs::Math::Max(y.RadiusKm, 1.0);
			it.Tint = y.Warm ? Color(1.0f, 0.62f, 0.35f) : Color(0.7f, 0.85f, 1.0f);
			it.Details = cs::Cat(y.Hulks.Count(), y.Hulks.Count() == 1 ? " hulk" : " hulks",
				y.Stations.Count() > 0 ? cs::Cat(", ", y.Stations.Count(), y.Stations.Count() == 1 ? " derelict station" : " derelict stations") : cs::String());
			if (y.Stations.Count() > 0) it.About = y.Stations[0].Summary;
			items.push_back(it);
		}

		// The ships about, as the unit held makes them out (the god's view sees every one).
		auto c = app->CommsHere();
		DVec3 you = app->PlayerHelioKm();
		if (world->Sensors != nullptr && app->Fleet() != nullptr)
		{
			struct Seen { cs::Ref<Ship> S; DVec3 At; double Km; ContactLevel Level; };
			std::vector<Seen> seen;
			for (const auto& s : app->Fleet()->Ships())
			{
				if (s == nullptr || s->Destroyed || s->GateDeparted || s->Scrapped || s->Planetside() || s == c.Speaker) continue;
				auto level = c.Piloted ? Comms::Seen(c, s) : ContactLevel::Identified;
				if (level == ContactLevel::None) continue; // fog: what the sensors do not see is not on the map
				auto at = world->Sensors->HelioOf(s, jd);
				seen.push_back({ s, at, (at - you).Length(), level });
			}
			std::stable_sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.Km != b.Km ? a.Km < b.Km : a.S->Id < b.S->Id; });
			if (seen.size() > static_cast<size_t>(MaxContacts)) seen.resize(MaxContacts);
			for (const auto& f : seen)
			{
				const auto& s = f.S;
				bool identified = f.Level == ContactLevel::Identified;
				bool classed = static_cast<int>(f.Level) >= static_cast<int>(ContactLevel::Class);
				Item it;
				it.Kind = ItemKind::Ship;
				it.Key = cs::Cat("s", s->Id);
				it.Name = identified ? s->Name : Contact::Describe(s, f.Level);
				it.KindLabel = identified ? cs::Cat(classed && s->Spec != nullptr ? s->Spec->DisplayName : cs::String("Ship"), ", ", Lower(Core::ToString(s->Role)))
					: classed && s->Spec != nullptr ? s->Spec->DisplayName : cs::String("Contact");
				it.Km = f.At;
				it.What = Target::OfShip(s);
				it.Tint = identified && s->Faction != nullptr ? Of(s->Faction->Color) : GreyContact;
				it.Size = 2.5f;
				if (identified && s->Faction != nullptr) it.Details = cs::Cat(s->Faction->Name, s->Derelict ? ", derelict" : "");
				if (identified) it.LoreKey = LoreKeys::Ship(sysName, s->Name);
				items.push_back(it);
			}
		}

		// The unit held (or the view).
		Item me;
		me.Kind = ItemKind::You;
		me.Key = "you";
		me.Name = c.Piloted && c.Speaker != nullptr ? c.Speaker->Name : cs::String("The view");
		me.KindLabel = app->IsPiloting() ? "Your helm" : c.Piloted ? "The unit you hold" : "Where you are looking from";
		me.Km = you;
		me.Tint = UiStyle::Good;
		me.Size = 4;
		if (c.Piloted && c.Speaker != nullptr) me.What = Target::OfShip(c.Speaker);
		items.push_back(me);
	}

	const SystemMapScreen::Item* SystemMapScreen::Find(const cs::String& key) const
	{
		if (key.empty()) return nullptr;
		for (const auto& it : items)
			if (it.Key == key) return &it;
		return nullptr;
	}

	bool SystemMapScreen::Crowded(Rect canvas, const Item& it) const
	{
		if (!it.HasParent || it.Kind == ItemKind::Ship || it.Kind == ItemKind::You || it.Kind == ItemKind::Gate) return false;
		return (Project(canvas, it.Km) - Project(canvas, it.ParentKm)).magnitude() < CrowdPx;
	}

	const SystemMapScreen::Item* SystemMapScreen::Nearest(Rect canvas, Vector2 at) const
	{
		const Item* best = nullptr;
		float bestPx = PickPx;
		for (const auto& it : items)
		{
			bool area = it.ReachKm > 0;
			if (area || Crowded(canvas, it)) continue;
			if (it.Kind == ItemKind::Ship && !showContacts) continue;
			if (it.Kind == ItemKind::Ramp && !showLanes) continue;
			float px = (Project(canvas, it.Km) - at).magnitude();
			// A ramp or a contact gives way to a world or a station at the same spot.
			if (it.Kind == ItemKind::Ramp || it.Kind == ItemKind::Ship) px += 3;
			if (px < bestPx)
			{
				bestPx = px;
				best = &it;
			}
		}
		if (best != nullptr || !showFields) return best;
		// Inside an area: the smallest that holds the cursor.
		double bestReach = cs::Double::PositiveInfinity;
		for (const auto& it : items)
		{
			if (it.ReachKm <= 0) continue;
			auto p = Project(canvas, it.Km);
			float d = (p - at).magnitude();
			float outer = static_cast<float>(it.ReachKm / kmPerPx), inner = static_cast<float>(it.InnerKm / kmPerPx);
			bool inside = it.Kind == ItemKind::Belt ? d >= inner - 4 && d <= outer + 4 : d <= Mathf::Max(outer, 6.0f);
			if (inside && it.ReachKm < bestReach)
			{
				bestReach = it.ReachKm;
				best = &it;
			}
		}
		return best;
	}

	// ---------------------------------------------------------------- the view

	Vector2 SystemMapScreen::Project(Rect canvas, DVec3 km) const
	{
		auto c = canvas.center();
		double x = (km.X - centreKm.X) / kmPerPx, y = (km.Y - centreKm.Y) / kmPerPx;
		// Kept within a float's comfortable reach: far off the canvas is far off the canvas.
		x = cs::Math::Max(-1e6, cs::Math::Min(1e6, x));
		y = cs::Math::Max(-1e6, cs::Math::Min(1e6, y));
		return Vector2(c.x + static_cast<float>(x), c.y - static_cast<float>(y));
	}

	void SystemMapScreen::Fit(Rect canvas)
	{
		auto system = app->ActiveSystem;
		double reach = system != nullptr ? cs::Math::Max(system->OverviewSpanKm, 1e5) : 1e9;
		if (system != nullptr)
			for (const auto& g : system->Gates())
				if (g != nullptr) reach = cs::Math::Max(reach, g->PositionKm.Length());
		centreKm = DVec3::Zero;
		kmPerPx = reach * 2.15 / cs::Math::Max(100.0, static_cast<double>(Mathf::Min(canvas.width, canvas.height)));
		fitted = true;
	}

	void SystemMapScreen::CentreOnYou(Rect canvas)
	{
		centreKm = app->PlayerHelioKm();
		// Close enough to see what is about: the nearest world's moons, or a few hundred thousand km.
		kmPerPx = cs::Math::Min(kmPerPx, 2.5e6 / cs::Math::Max(100.0, static_cast<double>(Mathf::Min(canvas.width, canvas.height))));
	}

	void SystemMapScreen::HandleInput(Rect canvas)
	{
		Event& e = Event::current();
		bool over = canvas.Contains(e.mousePosition);
		if (e.type == EventType::MouseUp) dragging = false;
		if (!over && !dragging)
		{
			if (e.type == EventType::Repaint) hoveredKey = cs::String();
			return;
		}
		if (e.type == EventType::MouseMove || e.type == EventType::Repaint)
		{
			auto hit = Nearest(canvas, e.mousePosition);
			hoveredKey = hit != nullptr ? hit->Key : cs::String();
		}
		if (e.type == EventType::MouseDown && e.button == 0)
		{
			auto hit = Nearest(canvas, e.mousePosition);
			if (hit != nullptr)
			{
				selectedKey = hit->Key;
				// A click selects as the navigator does; a double click acts as its Enter does: at a helm the cruise drive
				// is spooled for it, else the view flies there.
				if (!hit->What.IsNone())
				{
					app->Select(hit->What);
					if (e.clickCount >= 2)
					{
						if (app->IsPiloting() && hit->Kind != ItemKind::You) app->CruiseTo(hit->What);
						else if (!app->AtControls()) app->SpaceCamera->FocusOn(hit->What);
						app->ShowSystemMap = false;
					}
				}
			}
			else
			{
				selectedKey = cs::String();
				dragging = true;
			}
			e.Use();
		}
		else if (e.type == EventType::MouseDown && e.button == 2)
		{
			dragging = true;
			e.Use();
		}
		else if (e.type == EventType::MouseDrag && (e.button == 0 || e.button == 2))
		{
			centreKm = centreKm + DVec3(-e.delta.x * kmPerPx, e.delta.y * kmPerPx, 0);
			e.Use();
		}
		else if (e.type == EventType::ScrollWheel && over)
		{
			// Zoom about the cursor: the point under it stays put.
			auto c = canvas.center();
			DVec3 under(centreKm.X + (e.mousePosition.x - c.x) * kmPerPx, centreKm.Y - (e.mousePosition.y - c.y) * kmPerPx, 0);
			double before = kmPerPx;
			kmPerPx = cs::Math::Max(0.5, cs::Math::Min(5e9, kmPerPx * std::pow(1.15, e.delta.y / 3.0)));
			double k = kmPerPx / before;
			centreKm = DVec3(under.X + (centreKm.X - under.X) * k, under.Y + (centreKm.Y - under.Y) * k, centreKm.Z);
			e.Use();
		}
	}

	// ---------------------------------------------------------------- drawing

	void SystemMapScreen::Draw()
	{
		EnsureStyles();
		UiMotion::Scope motion("system-map", 0); // US-T255: the whole screen fades in
		auto screen = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		// The map has the whole screen: nothing under it is clicked while it is up.
		if (Event::current().type == EventType::Layout || Event::current().type == EventType::Repaint)
			if (!app->UiRects.Contains(screen)) app->UiRects.Add(screen);

		double jd = app->Clock->Jd;
		if (builtFor != app->ActiveSystem.get() || Time::unscaledTime - builtAt > 0.25f)
		{
			if (builtFor != app->ActiveSystem.get()) fitted = false;
			Build(jd);
			builtAt = Time::unscaledTime;
			builtFor = app->ActiveSystem.get();
		}
		if (!selectedKey.empty() && Find(selectedKey) == nullptr) selectedKey = cs::String();

		auto old = GUI::color;
		GUI::color = Color(0.01f, 0.015f, 0.03f, 0.92f);
		GUI::DrawTexture(screen, Texture2D::whiteTexture());
		GUI::color = old;

		float left = 10, right = screen.width - 10;
		auto header = Rect(left, 10, right - left, HeaderH);
		auto legend = Rect(left, header.yMax() + 8, LegendW, screen.height - header.yMax() - 18);
		auto detail = Rect(right - DetailW, header.yMax() + 8, DetailW, screen.height - header.yMax() - 18);
		auto canvas = Rect(legend.xMax() + 8, header.yMax() + 8, detail.xMin() - legend.xMax() - 16, screen.height - header.yMax() - 18);

		if (!fitted) Fit(canvas);
		HandleInput(canvas);
		if (Event::current().type == EventType::Repaint) DrawMap(canvas, jd);
		DrawHeader(header);
		DrawLegend(legend);
		if (GUI::Button(Rect(legend.x + 10, legend.yMax() - 62, (legend.width - 26) / 2, 22), "FIT", button)) Fit(canvas);
		if (GUI::Button(Rect(legend.x + 16 + (legend.width - 26) / 2, legend.yMax() - 62, (legend.width - 26) / 2, 22), "HERE", button)) CentreOnYou(canvas);
		DrawDetail(detail, jd);
	}

	void SystemMapScreen::DrawMap(Rect canvas, double jd)
	{
		GUI::BeginClip(canvas);
		auto local = Rect(0, 0, canvas.width, canvas.height);
		const Item* hovered = Find(hoveredKey);
		const Item* selected = Find(selectedKey);

		// Areas first, under everything.
		if (showFields)
			for (const auto& it : items)
			{
				if (it.ReachKm <= 0) continue;
				auto p = Project(local, it.Km);
				float r = static_cast<float>(it.ReachKm / kmPerPx);
				bool lit = &it == hovered || &it == selected;
				Color c = it.Tint;
				if (it.Kind == ItemKind::Belt)
				{
					float inner = static_cast<float>(it.InnerKm / kmPerPx);
					c.a = lit ? 0.6f : 0.32f;
					Circle(local, p, r, c, lit ? 2.0f : 1.0f);
					Circle(local, p, inner, c, lit ? 2.0f : 1.0f);
					c.a = lit ? 0.22f : 0.12f;
					Circle(local, p, (r + inner) * 0.5f, c, Mathf::Clamp(r - inner, 1.0f, 60.0f));
				}
				else if (it.Kind == ItemKind::Nebula)
				{
					c.a = lit ? 0.45f : 0.3f;
					Disc(p, Mathf::Max(r, 6.0f) * 1.25f, c);
				}
				else
				{
					c.a = lit ? 0.8f : 0.45f;
					Circle(local, p, Mathf::Max(r, 5.0f), c, lit ? 2.0f : 1.2f);
					if (it.Kind == ItemKind::Graveyard)
					{
						Line(p + Vector2(-4, -4), p + Vector2(4, 4), c, 1.5f);
						Line(p + Vector2(-4, 4), p + Vector2(4, -4), c, 1.5f);
					}
				}
				if (showLabels && (r > 18 || lit)) Label(p + Vector2(Mathf::Max(r, 6.0f) * 0.7f + 4, -Mathf::Max(r, 6.0f) * 0.7f - 8), it.Name, c, true);
			}

		// Orbits.
		if (showOrbits)
			for (const auto& it : items)
			{
				if (!it.HasParent || it.Kind == ItemKind::Ramp || it.Kind == ItemKind::Gas || it.Kind == ItemKind::Ship || it.Kind == ItemKind::You) continue;
				float r = static_cast<float>((it.Km - it.ParentKm).Length() / kmPerPx);
				if (r < CrowdPx || r > 60000) continue;
				bool lit = &it == hovered || &it == selected;
				Circle(local, Project(local, it.ParentKm), r, Color(0.5f, 0.62f, 0.78f, lit ? 0.5f : it.Kind == ItemKind::Station ? 0.1f : 0.18f), lit ? 1.6f : 1.0f);
			}

		if (showLanes) DrawLanes(local, jd);
		DrawRoute(local, jd);

		// The points: worlds, stations, works, gates, ramps, then the ships and the unit held over them.
		auto point = [&](const Item& it)
		{
			if (it.ReachKm > 0) return;
			if (it.Kind == ItemKind::Ship && !showContacts) return;
			if (it.Kind == ItemKind::Ramp && !showLanes) return;
			auto p = Project(local, it.Km);
			if (p.x < -40 || p.y < -40 || p.x > local.width + 40 || p.y > local.height + 40) return;
			bool crowded = Crowded(local, it);
			bool lit = &it == hovered || &it == selected;
			if (crowded && !lit)
			{
				if (it.Kind != ItemKind::Ramp && it.Kind != ItemKind::Station) Disc(p, 2.5f, Color(it.Tint.r, it.Tint.g, it.Tint.b, 0.5f));
				return;
			}
			float s = it.Size;
			switch (it.Kind)
			{
				case ItemKind::Star:
					Disc(p, s * 3.2f, Color(it.Tint.r, it.Tint.g, it.Tint.b, 0.35f));
					Disc(p, s * 1.4f, Color(1, 1, 1, 0.95f));
					break;
				case ItemKind::Gate:
					Circle(local, p, s + 1, it.Tint, 2.0f);
					Disc(p, 2.5f, it.Tint);
					break;
				case ItemKind::Ramp:
					Circle(local, p, s, Color(it.Tint.r, it.Tint.g, it.Tint.b, 0.75f), 1.2f);
					break;
				case ItemKind::Station:
				case ItemKind::Gas:
				{
					auto old = GUI::color;
					GUI::color = it.Tint;
					GUI::DrawTexture(Rect(p.x - s, p.y - s, s * 2, s * 2), Icons::White());
					GUI::color = old;
					break;
				}
				case ItemKind::Ship:
					Line(p + Vector2(0, -s - 1), p + Vector2(s + 1, s), it.Tint, 1.4f);
					Line(p + Vector2(s + 1, s), p + Vector2(-s - 1, s), it.Tint, 1.4f);
					Line(p + Vector2(-s - 1, s), p + Vector2(0, -s - 1), it.Tint, 1.4f);
					break;
				case ItemKind::You:
					Circle(local, p, 7, it.Tint, 2.0f);
					Disc(p, 3, it.Tint);
					break;
				case ItemKind::Site:
					Disc(p, s * 1.6f, Color(it.Tint.r, it.Tint.g, it.Tint.b, 0.9f));
					Circle(local, p, s + 3, Color(1.0f, 0.85f, 0.5f, 0.7f), 1.0f);
					break;
				default:
					Disc(p, s * 1.6f, Color(it.Tint.r, it.Tint.g, it.Tint.b, 0.95f));
					if (!it.Details.empty()) Circle(local, p, s + 3, Color(it.Tint.r, it.Tint.g, it.Tint.b, 0.6f), 1.0f); // settled
					break;
			}
			if (lit) Circle(local, p, s + 7, &it == selected ? UiStyle::Accent : Color(1, 1, 1, 0.6f), 1.5f);
			bool named = showLabels && (lit || it.Kind == ItemKind::Star || it.Kind == ItemKind::Gate || it.Kind == ItemKind::You
				|| ((it.Kind == ItemKind::Body || it.Kind == ItemKind::Site || it.Kind == ItemKind::Station || it.Kind == ItemKind::Gas)
					&& (!it.HasParent || (Project(local, it.Km) - Project(local, it.ParentKm)).magnitude() > 34)));
			if (named) Label(p + Vector2(s + 5, -8), it.Name, it.Kind == ItemKind::You ? UiStyle::Good : Color::white(), it.Kind == ItemKind::Ramp || it.Kind == ItemKind::Ship);
		};
		for (const auto& it : items)
			if (it.Kind != ItemKind::Ship && it.Kind != ItemKind::You) point(it);
		for (const auto& it : items)
			if (it.Kind == ItemKind::Ship) point(it);
		for (const auto& it : items)
			if (it.Kind == ItemKind::You) point(it);

		// At a helm: the line from the hull to what is picked.
		if (selected != nullptr && app->IsPiloting() && selected->Kind != ItemKind::You)
		{
			const Item* you = Find("you");
			if (you != nullptr)
			{
				auto a = Project(local, you->Km), b = Project(local, selected->Km);
				auto d = b - a;
				float len = d.magnitude();
				for (float t = 0; t < len; t += 10) Line(a + d * (t / len), a + d * (Mathf::Min(t + 5, len) / len), Color(UiStyle::Good.r, UiStyle::Good.g, UiStyle::Good.b, 0.5f), 1.0f);
			}
		}

		// The scale bar, bottom left.
		{
			double barKm = kmPerPx * 120;
			double mag = std::pow(10.0, std::floor(std::log10(barKm)));
			double nice = barKm / mag >= 5 ? 5 * mag : barKm / mag >= 2 ? 2 * mag : mag;
			float w = static_cast<float>(nice / kmPerPx);
			auto a = Vector2(14, local.height - 16), b = Vector2(14 + w, local.height - 16);
			Line(a, b, UiStyle::Dim, 1.5f);
			Line(a + Vector2(0, -4), a + Vector2(0, 4), UiStyle::Dim, 1.5f);
			Line(b + Vector2(0, -4), b + Vector2(0, 4), UiStyle::Dim, 1.5f);
			Label(Vector2(14, local.height - 34), Km(nice), UiStyle::Dim, true);
		}

		if (hovered != nullptr) DrawTip(local, *hovered);
		GUI::EndClip();
	}

	void SystemMapScreen::DrawLanes(Rect canvas, double jd)
	{
		auto lanes = app->Fleet() != nullptr ? app->Fleet()->Lanes : nullptr;
		if (lanes == nullptr) return;
		auto draw = [&](const cs::Ref<Lane>& lane, Color c, float thickness, bool dashed)
		{
			auto a = lanes->RampHelio(lane, lane->A, jd), b = lanes->RampHelio(lane, lane->B, jd);
			// A lane too small on the screen to see is not drawn (Earth to the Moon at the whole system's zoom).
			if ((a - b).Length() / kmPerPx < 4) return;
			constexpr int n = 24;
			auto prev = Project(canvas, lanes->CurveKm(lane, a, b, 0, jd));
			for (int i = 1; i <= n; i++)
			{
				auto p = Project(canvas, lanes->CurveKm(lane, a, b, static_cast<double>(i) / n, jd));
				if (!dashed || i % 2 == 0) Line(prev, p, c, thickness);
				prev = p;
			}
		};
		for (const auto& lane : lanes->Lanes)
			if (lane != nullptr)
			{
				bool failed = !lanes->Carries(lane);
				Color c = failed ? Color(0.9f, 0.4f, 0.35f, 0.45f) : Color(LaneColor.r, LaneColor.g, LaneColor.b, lane->Heavy ? 0.7f : 0.45f);
				draw(lane, c, lane->Heavy ? 2.5f : 1.5f, failed);
			}
		for (const auto& spur : lanes->GateSpurs)
			if (spur != nullptr) draw(spur, Color(GateColor.r, GateColor.g, GateColor.b, 0.4f), 1.2f, true);
	}

	void SystemMapScreen::DrawRoute(Rect canvas, double jd)
	{
		auto lanes = app->Fleet() != nullptr ? app->Fleet()->Lanes : nullptr;
		if (lanes == nullptr || app->LaneRoute == nullptr) return;
		for (const auto& hop : *app->LaneRoute)
		{
			if (hop.Lane == nullptr) continue;
			auto a = lanes->RampHelio(hop.Lane, hop.Lane->A, jd), b = lanes->RampHelio(hop.Lane, hop.Lane->B, jd);
			constexpr int n = 32;
			auto prev = Project(canvas, lanes->CurveKm(hop.Lane, a, b, 0, jd));
			for (int i = 1; i <= n; i++)
			{
				auto p = Project(canvas, lanes->CurveKm(hop.Lane, a, b, static_cast<double>(i) / n, jd));
				Line(prev, p, RouteColor, 3.0f);
				prev = p;
			}
			Circle(canvas, Project(canvas, lanes->RampHelio(hop.Lane, hop.From, jd)), 6, RouteColor, 2.0f);
		}
	}

	void SystemMapScreen::DrawTip(Rect canvas, const Item& it)
	{
		cs::String about = it.LoreKey.empty() ? it.About : LoreShort(it.LoreKey, it.About);
		cs::String body = cs::Cat(it.KindLabel, "  \xC2\xB7  ", RangeOf(it), it.Details.empty() ? cs::String() : cs::Cat("\n", it.Details),
			about.empty() ? cs::String() : cs::Cat("\n", about));
		constexpr float w = 300;
		float h = tip->CalcHeight(GUIContent(body), w - 16) + 30;
		auto m = Event::current().mousePosition;
		// (Inside the canvas clip: the mouse is in the canvas's coordinates.)
		float x = m.x + 18, y = m.y + 14;
		if (x + w > canvas.width - 4) x = m.x - w - 12;
		if (y + h > canvas.height - 4) y = canvas.height - h - 4;
		auto r = Rect(x, y, w, h);
		GUI::Box(r, GUIContent::none(), tip);
		auto old = GUI::contentColor;
		GUI::contentColor = it.Kind == ItemKind::You ? UiStyle::Good : UiStyle::Accent;
		GUI::Label(Rect(r.x + 8, r.y + 5, w - 16, 18), it.Name, label);
		GUI::contentColor = UiStyle::Text;
		GUI::Label(Rect(r.x + 8, r.y + 24, w - 16, h - 28), body, tip);
		GUI::contentColor = old;
	}

	void SystemMapScreen::DrawHeader(Rect r)
	{
		GUI::Box(r, GUIContent::none(), panel);
		auto system = app->ActiveSystem;
		GUI::Label(Rect(r.x + 12, r.y + 4, r.width - 200, 22), cs::Cat("SYSTEM MAP  \xC2\xB7  ", system != nullptr ? cs::Strings::ToUpperInvariant(system->Name) : cs::String()), title);
		auto old = GUI::contentColor;
		GUI::contentColor = UiStyle::Dim;
		GUI::Label(Rect(r.x + 12, r.y + 26, r.width - 200, 16),
			app->IsPiloting() ? "Click to select  \xC2\xB7  double-click cruises there  \xC2\xB7  drag pans, wheel zooms  \xC2\xB7  Esc or Shift+[ closes"
				: "Click to select  \xC2\xB7  double-click flies the view there  \xC2\xB7  drag pans, wheel zooms  \xC2\xB7  Esc or Shift+[ closes", small);
		GUI::contentColor = old;
		if (GUI::Button(Rect(r.xMax() - 180, r.y + 12, 80, 22), "GALAXY", button))
		{
			app->ShowSystemMap = false;
			app->ShowGalaxyMap = true;
		}
		if (GUI::Button(Rect(r.xMax() - 92, r.y + 12, 80, 22), "CLOSE", button)) app->ShowSystemMap = false;
	}

	void SystemMapScreen::DrawLegend(Rect r)
	{
		GUI::Box(r, GUIContent::none(), panel);
		float x = r.x + 10, w = r.width - 20, y = r.y + 10;
		GUI::Label(Rect(x, y, w, 20), "SHOW", label);
		y += 24;
		Toggle(y, x, w, showOrbits, "Orbits");
		Toggle(y, x, w, showLanes, "Lanes, ramps and spurs");
		Toggle(y, x, w, showContacts, "Contacts");
		Toggle(y, x, w, showFields, "Belts, fields, nebulae, graveyards");
		Toggle(y, x, w, showLabels, "Names");
		y += 8;
		GUI::Label(Rect(x, y, w, 20), "KEY", label);
		y += 22;
		Text(y, x, w, "\xE2\x97\x8F  worlds (ringed: settled)", UiStyle::Text);
		Text(y, x, w, "\xE2\x97\x8F  sites about a void station", Color(1.0f, 0.85f, 0.5f));
		Text(y, x, w, "\xE2\x96\xA0  stations and gas works", UiStyle::Text);
		Text(y, x, w, "\xE2\x97\x8B  jump gates", GateColor);
		Text(y, x, w, "\xE2\x80\x94  lanes;  \xE2\x97\x8B  their ramps", LaneColor);
		Text(y, x, w, "\xE2\x80\x94  the lane route plotted", RouteColor);
		Text(y, x, w, "\xE2\x96\xB2  ships, as your sensors see them", GreyContact);
		Text(y, x, w, "\xE2\x97\x8E  you", UiStyle::Good);
		y += 6;
		auto c = app->CommsHere();
		Text(y, x, w, c.Piloted ? "Only what the unit you hold makes out is shown; fly closer, or ping (Tab), to see more."
			: "The god's view: every ship in the system.", UiStyle::Dim);
	}

	cs::Ref<cs::List<LaneHop>> SystemMapScreen::RouteTo(const Item& it, double jd, double& rideHours)
	{
		rideHours = 0;
		auto lanes = app->Fleet() != nullptr ? app->Fleet()->Lanes : nullptr;
		if (lanes == nullptr) return nullptr;
		cs::Ref<cs::List<LaneHop>> route;
		if (it.Gate != nullptr) route = app->LaneRouteTo(-1, it.Gate);
		else if (it.Body >= 0 && it.Kind != ItemKind::Ramp) route = app->LaneRouteTo(lanes->SystemOf(it.Body), nullptr);
		if (route == nullptr) return nullptr;
		auto ship = app->IsPiloting() ? app->Piloted.Ship : nullptr;
		for (const auto& h : *route)
		{
			double len = lanes->CurveLengthKm(h.Lane, lanes->RampHelio(h.Lane, h.Lane->A, jd), lanes->RampHelio(h.Lane, h.Lane->B, jd), jd);
			rideHours += (ship != nullptr ? lanes->RideDays(ship, h.Lane, len) : lanes->RideDays(h.Lane, len)) * 24;
		}
		return route;
	}

	void SystemMapScreen::DrawDetail(Rect r, double jd)
	{
		GUI::Box(r, GUIContent::none(), panel);
		float x = r.x + 12, w = r.width - 24, y = r.y + 10;
		const Item* it = Find(selectedKey);
		bool helm = app->IsPiloting();
		if (it == nullptr)
		{
			GUI::Label(Rect(x, y, w, 22), "NOTHING PICKED", label);
			y += 26;
			Text(y, x, w, "Click a world, a station, a gate, a ship or a field to see it here.", UiStyle::Dim);
			Text(y, x, w, helm ? "At the helm, a world, station or ship can be cruised to, and a world's or a gate's lane route plotted."
				: "Take a helm to cruise or plot lane routes from here.", UiStyle::Dim);
		}
		else
		{
			auto old = GUI::contentColor;
			GUI::contentColor = it->Kind == ItemKind::You ? UiStyle::Good : UiStyle::Accent;
			float th = label->CalcHeight(GUIContent(it->Name), w);
			GUI::Label(Rect(x, y, w, th), it->Name, label);
			GUI::contentColor = old;
			y += th + 2;
			Text(y, x, w, cs::Cat(it->KindLabel, "  \xC2\xB7  ", RangeOf(*it)), UiStyle::Dim);
			if (!it->Details.empty()) Text(y, x, w, it->Details, UiStyle::Text);
			cs::String about = it->LoreKey.empty() ? it->About : LoreShort(it->LoreKey, it->About);
			if (!about.empty())
			{
				y += 4;
				Text(y, x, w, about, Color(0.85f, 0.88f, 0.92f));
			}
			y += 10;

			// What can be done with it.
			if (!it->What.IsNone() && it->Kind != ItemKind::You)
			{
				if (helm)
				{
					if (GUI::Button(Rect(x, y, w, 24), "CRUISE THERE   (the cruise drive)", button))
					{
						app->Select(it->What);
						app->CruiseTo(it->What);
						app->ShowSystemMap = false;
					}
					y += 28;
				}
				else if (!app->AtControls())
				{
					if (GUI::Button(Rect(x, y, w, 24), "FLY THE VIEW THERE", button))
					{
						app->Select(it->What);
						app->SpaceCamera->FocusOn(it->What);
						app->ShowSystemMap = false;
					}
					y += 28;
				}
			}
			if (helm && (it->Gate != nullptr || (it->Body >= 0 && it->Kind != ItemKind::Ramp && it->Kind != ItemKind::Ship && it->Kind != ItemKind::You)))
			{
				double hours = 0;
				auto route = RouteTo(*it, jd, hours);
				if (route != nullptr)
				{
					int n = route->Count();
					if (GUI::Button(Rect(x, y, w, 24), cs::Cat("LANE ROUTE   ", n, n == 1 ? " lane, " : " lanes, ", cs::Fmt(hours, hours < 10 ? "0.0" : "0"), " h riding"), button))
					{
						int gateIndex = -1;
						if (it->Gate != nullptr)
							for (int i = 0; i < app->ActiveSystem->Gates().Count(); i++)
								if (app->ActiveSystem->Gates()[i] == it->Gate) gateIndex = i;
						auto lanes = app->Fleet()->Lanes;
						app->PlotLaneRoute(it->Gate != nullptr ? -1 : lanes->SystemOf(it->Body), gateIndex, it->Name);
					}
					y += 28;
				}
				else
				{
					Text(y, x, w, it->Gate != nullptr ? "No lane leads on to this gate: cruise there." : "No lane route needed (or none open): cruise there.", UiStyle::Dim);
				}
			}
			if (it->Gate != nullptr && it->Gate->Destination() != nullptr)
			{
				auto to = it->Gate->Destination();
				if (GUI::Button(Rect(x, y, w, 24), cs::Cat(helm ? "PLOT THE COURSE TO " : "TRAVEL TO ", cs::Strings::ToUpperInvariant(to->Name)), button))
				{
					app->ShowSystemMap = false;
					app->TravelTo(to); // at a helm, the course over the gates (U.SPC-T516)
				}
				y += 28;
			}
		}

		// The lane route plotted, whatever is picked.
		if (app->LaneRoute != nullptr)
		{
			y = Mathf::Max(y + 10, r.yMax() - 92);
			GUI::DrawTexture(Rect(x, y - 6, w, 1), buttonOnTex);
			auto old = GUI::contentColor;
			GUI::contentColor = RouteColor;
			GUI::Label(Rect(x, y, w, 18), cs::Cat("LANE ROUTE TO ", cs::Strings::ToUpperInvariant(app->LaneRouteName)), label);
			GUI::contentColor = old;
			y += 20;
			cs::List<cs::String> legs;
			for (const auto& h : *app->LaneRoute) legs.Add(h.Lane->Name);
			Text(y, x, w, cs::Strings::Join(" \xE2\x80\xBA ", legs), UiStyle::Text);
			if (GUI::Button(Rect(x, r.yMax() - 34, w, 24), "CLEAR THE LANE ROUTE", button)) app->ClearLaneRoute();
		}
	}

	cs::String SystemMapScreen::RangeOf(const Item& it)
	{
		if (it.Kind == ItemKind::You) return "here";
		double km = (it.Km - app->PlayerHelioKm()).Length();
		if (it.ReachKm > 0 && it.Kind != ItemKind::Belt) km = cs::Math::Max(0.0, km - it.ReachKm);
		return km <= 0 ? cs::String("you are in it") : cs::Cat(Km(km), " away");
	}

	// ---------------------------------------------------------------- primitives

	void SystemMapScreen::Line(Vector2 a, Vector2 b, Color color, float thickness)
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

	void SystemMapScreen::Circle(Rect canvas, Vector2 p, float r, Color c, float thickness)
	{
		if (r < 0.5f) return;
		// Off the canvas altogether (wholly outside, or the canvas wholly inside it): nothing to draw.
		float far = r + thickness;
		if (p.x + far < 0 || p.y + far < 0 || p.x - far > canvas.width || p.y - far > canvas.height) return;
		float dx = Mathf::Max(Mathf::Abs(p.x), Mathf::Abs(p.x - canvas.width)), dy = Mathf::Max(Mathf::Abs(p.y), Mathf::Abs(p.y - canvas.height));
		if (dx * dx + dy * dy < (r - thickness) * (r - thickness)) return;
		int n = Mathf::Clamp(static_cast<int>(r / 5), 24, 360);
		for (int i = 0; i < n; i++)
		{
			float a0 = i * Mathf::PI * 2 / n, a1 = (i + 1) * Mathf::PI * 2 / n;
			auto q0 = p + Vector2(Mathf::Cos(a0), Mathf::Sin(a0)) * r, q1 = p + Vector2(Mathf::Cos(a1), Mathf::Sin(a1)) * r;
			if ((q0.x < -4 && q1.x < -4) || (q0.y < -4 && q1.y < -4) || (q0.x > canvas.width + 4 && q1.x > canvas.width + 4) || (q0.y > canvas.height + 4 && q1.y > canvas.height + 4)) continue;
			Line(q0, q1, c, thickness);
		}
	}

	void SystemMapScreen::Disc(Vector2 p, float r, Color c)
	{
		auto old = GUI::color;
		GUI::color = c;
		GUI::DrawTexture(Rect(p.x - r, p.y - r, r * 2, r * 2), glow);
		GUI::color = old;
	}

	void SystemMapScreen::Label(Vector2 p, const cs::String& s, Color c, bool minor)
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

	void SystemMapScreen::Toggle(float& y, float x, float w, bool& on, const cs::String& name)
	{
		if (GUI::Button(Rect(x, y, w, 22), cs::Cat((on ? "[x]  " : "[  ]  "), name), on ? buttonOn : button)) on = !on;
		y += 25;
	}

	void SystemMapScreen::Text(float& y, float x, float w, const cs::String& s, Color c)
	{
		auto content = GUIContent(s);
		float h = Mathf::Max(16.0f, text->CalcHeight(content, w));
		auto old = GUI::contentColor;
		GUI::contentColor = c;
		GUI::Label(Rect(x, y, w, h), content, text);
		GUI::contentColor = old;
		y += h + 2;
	}

	void SystemMapScreen::EnsureStyles()
	{
		if (title != nullptr) return;
		// The galaxy map's look (UiStyle.h) at overlay depth.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTex = S::PanelFrame(S::Depth::Overlay);
		buttonTex = S::ControlTexture();
		buttonOnTex = S::ControlOnTexture();
		tipTex = S::PanelFrame(S::Depth::Overlay);
		panel = S::PanelStyle(panelTex);
		glow = SoftDisc();
		title = S::TitleStyle(T::Title);
		text = S::TextStyle(T::Label);
		text->wordWrap = true;
		small = S::CaptionStyle(T::Caption);
		label = S::TextStyle(T::Label, S::Heading, FontStyle::Bold);
		label->wordWrap = true;
		labelDim = S::TextStyle(T::Caption, S::Heading);
		button = S::ButtonStyle(T::Label, buttonTex, buttonOnTex);
		button->alignment = TextAnchor::MiddleLeft;
		button->padding = RectOffset(static_cast<int>(S::Space::Pad), S::Space::TextPad, 0, 0);
		buttonOn = S::ButtonOnStyle(button, buttonOnTex);
		tip = S::PanelStyle(tipTex);
		tip->wordWrap = true;
		tip->alignment = TextAnchor::UpperLeft;
		tip->fontSize = text->fontSize;
		tip->normal.textColor = S::Text;
	}

	cs::Ref<Texture2D> SystemMapScreen::SoftDisc()
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

	void SystemMapScreen::DestroyOwnedTextures()
	{
		panelTex = nullptr;
		buttonTex = nullptr;
		buttonOnTex = nullptr;
		tipTex = nullptr;
		glow = nullptr;
		title = nullptr; // the styles are made again, with fresh textures, next time it is drawn
	}
}
