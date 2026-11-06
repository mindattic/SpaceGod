// US-T147: the showcase tour's chapters, and the stops of the features on main that have no command file of their own to
// register from (the broadside, the cruise drive, the seats, the jump drive and the void, the void station places, the
// graveyards, the orbits, the scales, the fleet lineup), plus the placeholders for features being built now. The features
// with a command register their own stops beside it (Practice, EvaRepairCommand, CommsCommand, BioMassCommand,
// SkyboxCommand, VoidStationCommand). Every setup here calls the feature's own code (its method, its dev menu entry).

#include "Tour/ShowcaseTour.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "SceneTour.h"
#include "Target.h"
#include "UI/CheatMenu.h"
#include "UnitySpaceApp.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Ephemeris.h"
#include "Embodiment/Influence.h" // US-T140: the dev waiver
#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/DeepSpace.h"
#include "Systems/FtlNavigation.h"
#include "Systems/Galaxy.h"
#include "Systems/Graveyards.h"
#include "Systems/HelmFtl.h"
#include "Systems/SystemSources.h"
#include "Systems/VoidRaiders.h"
#include "Systems/VoidSites.h"
#include "Systems/VoidStations.h"
#include "World.h"

#include "SpaceGodLog.h"

#include <algorithm>
#include <cstdio>
#include <memory>

using namespace SpaceGod::Core;

namespace SpaceGod::Tour
{
	namespace
	{
		// ---------------------------------------------------------------- helpers

		int BodyNamed(UnitySpaceApp& app, const cs::String& name)
		{
			for (const auto& b : app.Catalog()->Bodies())
				if (b->Name == name) return b->Index;
			return -1;
		}

		bool Helm(UnitySpaceApp& app, const cs::String& path)
		{
			// US-T140: the tour shows each feature from hulls it sets up (capitals, seats in its own skirmishes), so it runs
			// with the dev waiver of the embody limits (never saved); the limits have their own stop.
			if (app.World() != nullptr) SpaceGodInfluence::Of(app.World())->Waived = true;
			cs::String said;
			return UI::CheatMenu::RunPath(app, path, said);
		}

		/// The camera put at <paramref name="eye"/> looking at <paramref name="subject"/> (scene units), out of any focus.
		void LookAt(UnitySpaceApp& app, DVec3 subject, DVec3 eye)
		{
			auto d = (subject - eye).Normalized();
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			app.SpaceCamera->SetPose(eye, Quaternion::LookRotation(Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z)), Vector3::up()));
		}

		/// A point km from the system's root, where the scale model draws it now (as the hulls there are drawn).
		DVec3 RootScene(UnitySpaceApp& app, DVec3 km) { return app.Scale()->MapNear(app.Ephemeris(), app.BodyScenes(), 0, km); }

		/// The scene length of <paramref name="km"/> about a point <paramref name="atKm"/> from the root: at least its ship-scale length.
		double SceneSpan(UnitySpaceApp& app, DVec3 atKm, double km)
		{
			double mapped = (RootScene(app, atKm + DVec3(km, 0, 0)) - RootScene(app, atKm)).Length();
			return std::max(mapped, app.Scale()->ShipLength(km));
		}

		/// The fight a skirmish at <paramref name="bodyName"/> started (the combat showcase's own setup, SceneTour), watched.
		cs::Ref<Battle> Skirmish(UnitySpaceApp& app, const cs::String& bodyName, const cs::List<ShipClass>& classes)
		{
			int body = BodyNamed(app, bodyName);
			if (body < 0) return nullptr;
			auto battle = SceneTour::TriggerSkirmish(app, body, app.World()->Sol, app.World()->Reach, classes);
			if (battle != nullptr) app.WatchBattle(battle);
			return battle;
		}

		/// A trip to <paramref name="system"/>, its arrival the stop's Ready.
		struct Trip
		{
			cs::Ref<StarSystem> To;
			bool Arrived(UnitySpaceApp& app, double t)
			{
				if (To == nullptr) return true;
				if (app.ActiveSystem != To && !app.Travel->Active && t > 5) app.TravelTo(To);
				return app.ActiveSystem == To && !app.Travel->Active && t > 2;
			}
		};

		cs::Ref<StarSystem> Go(UnitySpaceApp& app, const cs::String& name)
		{
			auto s = app.Galaxy->Find(name);
			if (s == nullptr) s = app.Galaxy->Realize(name, app.Clock->Jd);
			if (s != nullptr && s != app.ActiveSystem) app.TravelTo(s);
			return s;
		}

		/// The first void station whose mix <paramref name="wanted"/> accepts, nearest first.
		cs::String StationWith(UnitySpaceApp& app, const std::function<bool(const VoidSitePlan&)>& wanted)
		{
			auto here = app.ActiveSystem != nullptr ? app.ActiveSystem->PositionLy : DVec3::Zero;
			auto stations = VoidStations::Stations();
			std::vector<cs::Ref<StarEntry>> sorted(stations.begin(), stations.end());
			std::sort(sorted.begin(), sorted.end(), [&](const cs::Ref<StarEntry>& a, const cs::Ref<StarEntry>& b)
			{
				return (a->PositionLy - here).LengthSquared() < (b->PositionLy - here).LengthSquared();
			});
			for (const auto& e : sorted)
				if (wanted(VoidSites::Plan(e->Name))) return e->Name;
			return cs::String();
		}

		/// A void station stop: there, then the camera on the site <paramref name="site"/> names in its plan.
		TourStop SiteStop(const cs::String& id, const cs::String& title, const cs::String& caption,
			std::function<bool(const VoidSitePlan&)> wanted, std::function<cs::String(const VoidSitePlan&)> site, double radii)
		{
			auto trip = std::make_shared<Trip>();
			auto body = std::make_shared<cs::String>();
			return TourStop::Make("Places", id, title, caption).In(cs::String())
				.WithSetup([trip, body, wanted, site](UnitySpaceApp& app) -> cs::String
				{
					cs::String name = StationWith(app, wanted);
					if (name.empty()) return "no void station of the kind in this galaxy";
					*body = site(VoidSites::Plan(name));
					trip->To = Go(app, name);
					return trip->To != nullptr ? cs::String() : cs::Cat(name, " could not be reached");
				})
				.Until([trip](UnitySpaceApp& app, double t) { return trip->Arrived(app, t); }, 120)
				.WithCamera([body, radii](UnitySpaceApp& app, double t)
				{
					if (t > 0) return;
					int i = BodyNamed(app, *body);
					auto target = Target::OfBody(i >= 0 ? i : 0);
					app.Select(target);
					app.SpaceCamera->FocusOn(target, i >= 0 ? radii : 9.0, 18.0f);
				})
				.For(9).AtBothScales();
		}

		/// A graveyard (or a derelict station on its own) somewhere built, nearest first: the system and which.
		struct Yard
		{
			cs::Ref<StarSystem> System;
			int Index = -1;
		};

		Yard FindYard(UnitySpaceApp& app, const std::function<bool(const Graveyard&)>& wanted)
		{
			auto here = app.ActiveSystem != nullptr ? app.ActiveSystem->PositionLy : DVec3::Zero;
			// Every star the charts hold, running or not (a new game's Systems holds few): name and place, nearest first.
			std::vector<std::pair<cs::String, DVec3>> stars;
			for (const auto& s : app.Galaxy->Systems) stars.emplace_back(s->Name, s->PositionLy);
			for (const auto& n : app.Galaxy->NearbyStars) stars.emplace_back(n.Name, n.PositionLy);
			for (const auto& n : app.Galaxy->Undiscovered()) stars.emplace_back(n.Name, n.PositionLy);
			std::sort(stars.begin(), stars.end(), [&](const auto& a, const auto& b)
			{
				return (a.second - here).LengthSquared() < (b.second - here).LengthSquared();
			});
			int realized = 0;
			for (const auto& star : stars)
			{
				auto s = app.Galaxy->Find(star.first);
				if (s == nullptr || s->Catalog == nullptr)
				{
					// Not built yet: the nearest few are, to look (their plan comes from the root star).
					if (realized++ >= 12) continue;
					s = app.Galaxy->Realize(star.first, app.Clock->Jd);
					if (s == nullptr || s->Catalog == nullptr) continue;
				}
				const auto& yards = Graveyards::Of(s->Catalog->Root());
				for (int i = 0; i < yards.Count(); i++)
					if (wanted(yards[i])) return Yard{ s, i };
			}
			return Yard{};
		}

		TourStop YardStop(const cs::String& id, const cs::String& title, const cs::String& caption, std::function<bool(const Graveyard&)> wanted, bool station)
		{
			auto trip = std::make_shared<Trip>();
			auto yard = std::make_shared<Yard>();
			return TourStop::Make("Places", id, title, caption).In(cs::String())
				.WithSetup([trip, yard, wanted](UnitySpaceApp& app) -> cs::String
				{
					*yard = FindYard(app, wanted);
					if (yard->System == nullptr) return "none in the systems near enough to build";
					trip->To = yard->System;
					if (app.ActiveSystem != yard->System) app.TravelTo(yard->System);
					return cs::String();
				})
				.Until([trip, yard](UnitySpaceApp& app, double t)
				{
					if (!trip->Arrived(app, t)) return false;
					Graveyards::Populate(app.World(), app.Clock->Jd); // its hulks laid as wrecks, as when a helm first comes in
					return true;
				}, 120)
				.WithCamera([yard, station](UnitySpaceApp& app, double t)
				{
					const auto& yards = Graveyards::Of(app.Catalog()->Root());
					if (yard->Index < 0 || yard->Index >= yards.Count()) return;
					const auto& g = yards[yard->Index];
					DVec3 km = g.CentreKm;
					double reach = g.RadiusKm * 1.6;
					if (station && g.Stations.Count() > 0)
					{
						km = g.Stations[0].OffsetKm;
						reach = g.Stations[0].SizeKm * 5;
					}
					// A slow drift round it, a little above its plane.
					DVec3 subject = RootScene(app, km);
					double d = SceneSpan(app, km, reach);
					double a = 0.6 + t * 0.04;
					LookAt(app, subject, subject + DVec3(cs::Math::Cos(a), 0.35, cs::Math::Sin(a)).Normalized() * d);
				})
				.For(9).AtBothScales();
		}

		// ---------------------------------------------------------------- the jump drive (US-T86) and the void (US-T124)

		/// Out to the jump, part one: a navy battleship's helm off Earth. Empty, or why not.
		cs::String TakeJumpHelm(UnitySpaceApp& app)
		{
			if (app.InFtlCruise() || app.FtlAutopilot->Active()) return cs::String();
			if (app.AtControls()) Helm(app, "Helm/Release to command");
			int earth = BodyNamed(app, "Earth");
			if (earth < 0) earth = 0;
			DVec3 outward;
			double km = app.BodyStandoffKm(earth, outward);
			auto ship = app.Fleet()->SpawnAt("Tour Pathfinder", ShipClass::Battleship, app.World()->Sol, earth, outward * km, outward, app.Clock->Jd);
			app.Select(Target::OfShip(ship));
			if (!Helm(app, "Helm/Take the helm of the selected ship")) return "the battleship's helm could not be taken";
			return cs::String();
		}

		/// Part two, a moment later at the helm: a course plotted to a star far enough for a void stop on the way, and the
		/// drive's own autopilot engaged (it runs out past the rim, spools and jumps). Empty, or why not.
		cs::String EngageJump(UnitySpaceApp& app)
		{
			if (app.InFtlCruise() || app.FtlAutopilot->Active()) return cs::String();
			auto here = app.ActiveSystem;
			cs::Ref<FtlTarget> best;
			double bestLy = cs::Double::PositiveInfinity;
			for (const auto& t : FtlTargets::All(app.Galaxy, here))
			{
				if (t->IsBeacon()) continue;
				double ly = t->LocalKm(here).Length() / Astro::LightYearKm;
				// Past one jump's reach (7 ly), so the way has a void stop in it; the nearest such.
				if (ly > 7.6 && ly < bestLy)
				{
					best = t;
					bestLy = ly;
				}
			}
			if (best == nullptr) return "no star past one jump's reach on the charts";
			if (!app.IsPiloting()) return "not at a helm";
			if (!app.PlotFtlCourse(best, true)) return cs::Cat("no course to ", best->Name);
			if (!app.EngageFtlAutopilot()) return cs::Cat("the autopilot would not engage: ", app.FtlAutopilot->Reason);
			char ly[32];
			std::snprintf(ly, sizeof ly, "%.1f", bestLy);
			app.ShowToast(cs::Cat("Jump drive: ", best->Name, ", ", ly, " ly"));
			return cs::String();
		}

		bool AtVoidStop(UnitySpaceApp& app) { return app.FtlCruise != nullptr && app.FtlCruise->AtVoidStop(app.Clock->Jd); }

		/// Keeps the void stop open while the tour is in it (US-T175): the stop is only a few real seconds long (the drive
		/// spools out of it on its own), so the showing and the ambush stop after it would find it over. Each call tops what
		/// is left of the spool back up to most of the stop's own first length. True while at a void stop.
		bool HoldVoidStop(UnitySpaceApp& app)
		{
			if (!AtVoidStop(app)) return false;
			static double heldStopJd = -1, span = 0;
			auto c = app.FtlCruise;
			double jd = app.Clock->Jd, left = c->EngageJd() - jd;
			if (c->StopJd != heldStopJd || !(span > 0))
			{
				heldStopJd = c->StopJd;
				span = left;
			}
			if (left < span * 0.8) c->DelayCharge(span - left);
			return true;
		}

		/// The jump's Ready: the autopilot engaged a moment after the helm was taken (again every few seconds until it takes),
		/// then <paramref name="done"/>.
		std::function<bool(UnitySpaceApp&, double)> JumpUntil(std::function<bool(UnitySpaceApp&)> done)
		{
			auto next = std::make_shared<double>(1.0);
			return [next, done](UnitySpaceApp& app, double t)
			{
				if (t < 0.05) *next = 1.0;
				if (!app.InFtlCruise() && !app.FtlAutopilot->Active() && t >= *next)
				{
					*next = t + 4;
					cs::String why = EngageJump(app);
					if (!why.empty()) UE_LOG(LogSpaceGod, Warning, TEXT("sg.Tour: jump: %s"), UTF8_TO_TCHAR(why.c_str()));
				}
				return done(app);
			};
		}

		// ---------------------------------------------------------------- the USC fleet lineup (US-T142)

		struct Lineup
		{
			cs::List<cs::Ref<Ship>> Ships;
		};

		void RegisterStops()
		{
			TourRegistry::DeclareChapter("Flight and combat", 10, "the helm in a fight, the heavy fighter, the capital line, the cruise drive");
			TourRegistry::DeclareChapter("Embodiment", 20, "taking over any unit: seats, guns, the EVA worker, the comms");
			TourRegistry::DeclareChapter("Places", 30, "the skies, the void stations, the graveyards");
			TourRegistry::DeclareChapter("Threats", 40, "the Bio Mass");
			TourRegistry::DeclareChapter("Presentation", 50, "the orbit lines and the two scales");
			TourRegistry::DeclareChapter("Ships", 60, "the fleet");
			TourRegistry::DeclareChapter("Travel", 70, "the jump drive, the void stops and what waits in them");

			// ---- Flight and combat (the dogfights register from Practice).
			TourRegistry::Add(TourStop::Make("Flight and combat", "capital-broadside", "Capital broadside",
				"Battleships and a cruiser in line off Uranus: the guns run out along the flank, hulls turning to bring them to bear")
				.At(30)
				.WithSetup([](UnitySpaceApp& app) -> cs::String
				{
					return Skirmish(app, "Uranus", cs::List<ShipClass>{ ShipClass::Battleship, ShipClass::Battleship, ShipClass::Cruiser }) != nullptr ? cs::String() : cs::String("the skirmish did not start");
				})
				.WithCamera([](UnitySpaceApp& app, double t)
				{
					// The whole line, then in on the nearest capital ship's flank.
					static bool closed = false;
					if (t == 0) closed = false;
					if (t < 6 || closed || !app.Selected.IsShip()) return;
					closed = true;
					app.SpaceCamera->FocusOn(app.Selected, 5, 12.0f);
				})
				.For(12).AtBothScales());

			auto cruise = std::make_shared<int>(0);
			TourRegistry::Add(TourStop::Make("Flight and combat", "cruise-earth-mars", "Cruise drive, Earth to Mars",
				"Shift+J at the helm: the drive spools, the hull runs along its nose at up to a thousand times light speed and drops out off Mars")
				.At(40)
				.WithSetup([cruise](UnitySpaceApp& app) -> cs::String
				{
					*cruise = 0;
					int earth = BodyNamed(app, "Earth");
					if (earth < 0 || BodyNamed(app, "Mars") < 0) return "no Earth and Mars here";
					// A courier's helm, put off Earth's sunlit side by the dev menu's own teleport.
					DVec3 outward;
					double km = app.BodyStandoffKm(earth, outward);
					auto ship = app.Fleet()->SpawnAt("Tour Courier", ShipClass::Courier, app.World()->Sol, earth, outward * km, -outward, app.Clock->Jd);
					app.Select(Target::OfShip(ship));
					if (!Helm(app, "Helm/Take the helm of the selected ship")) return "the courier's helm could not be taken";
					app.CheatTeleportToBody(earth);
					return cs::String();
				})
				.Until([cruise](UnitySpaceApp& app, double t)
				{
					if (app.SystemCruiseActive()) return t > 3;
					// A frame at the helm first, then the menu's Cruise to: nose onto Mars, the drive spooled.
					if (t > 1 && (*cruise)++ % 120 == 0) app.CruiseTo(Target::OfBody(BodyNamed(app, "Mars")));
					return false;
				}, 30)
				.For(14).AtBothScales());

			TourRegistry::Add(TourStop::Placeholder("Flight and combat", "pvp-shine", "PvP: the shine on another player's unit",
				"in co-op, units embodied by another player glint so the others can pick them out", "US-T136").At(90));

			// ---- Embodiment (the EVA worker and the comms register from their commands).
			TourRegistry::Add(TourStop::Make("Embodiment", "embody-seats", "Take over any unit: seats and guns",
				"At a battleship's helm in a fight, then the next seat in the fight (' key), then down the barrel of its guns (/ key)")
				.At(10)
				.WithSetup([](UnitySpaceApp& app) -> cs::String
				{
					if (Skirmish(app, "Neptune", cs::List<ShipClass>{ ShipClass::Battleship, ShipClass::Destroyer, ShipClass::Frigate }) == nullptr) return "the skirmish did not start";
					return Helm(app, "Helm/Take the helm of the selected ship") ? cs::String() : cs::String("the helm could not be taken");
				})
				.WithCamera([](UnitySpaceApp& app, double t)
				{
					static int step = 0;
					if (t == 0) step = 0;
					if (step == 0 && t > 3.5) { step = 1; Helm(app, "Helm/Next seat in the fight"); }
					if (step == 1 && t > 7) { step = 2; Helm(app, "Helm/Man the next gun"); }
					if (step == 2 && t > 10) { step = 3; Helm(app, "Helm/Man the next gun"); }
				})
				.For(13));
			// US-T140: the embody limits and Influence, and ordering a squad through its leader: sg.StandDown lays out a pirate band
			// closing on a lone freighter with you at its captain's helm (Influence at least 4), then the squad is ordered down.
			TourRegistry::Add(TourStop::Console("Embodiment", "influence", "Influence and ordering a squad",
				"The Influence meter (top left) gates bigger hulls and Epic names; no hull in a battle under way can be taken. Here, a pirate captain's band closes on a lone freighter: Shift+1 orders it to stand down, and the crews obey, hesitate, argue or mutiny", "sg.StandDown")
				.At(90)
				.Until([](UnitySpaceApp& app, double t) { return app.IsPiloting() && t > 5; }, 30)
				.WithCamera([](UnitySpaceApp& app, double t)
				{
					static bool ordered = false;
					if (t == 0) ordered = false;
					if (!ordered && t > 4) { ordered = true; app.OrderSquadStandDown(); }
				})
				.For(12).AtBothScales());

			// ---- Places (the skies and Void Station 1 register from their commands).
			TourRegistry::Add(SiteStop("void-megastructure", "Megastructure",
				"A colossal inhabited megastructure at a void station: stacked towers lit by thousands of windows, docking spars with ships along them",
				[](const VoidSitePlan& p) { return p.Mix.Megastructure; }, [](const VoidSitePlan& p) { return p.MegastructureName; }, 3.0).At(30));
			TourRegistry::Add(SiteStop("void-nebula-haven", "Nebula pirate haven",
				"A void station inside a nebula: the gas swallows sensors, and a pirate clan has dug its haven into the rock at its heart",
				[](const VoidSitePlan& p) { return p.Mix.Nebula && !p.DenName.empty(); }, [](const VoidSitePlan& p) { return p.DenName; }, 6.0).At(31));
			TourRegistry::Add(SiteStop("void-ice-field", "Ice field",
				"A void station's field of glittering ice chunks, mined for water and volatiles",
				[](const VoidSitePlan& p) { return p.Mix.Field == VoidFieldStyle::IceField; },
				[](const VoidSitePlan& p)
				{
					for (const auto& s : p.Sites)
						if (s.Kind == VoidSiteKind::FieldRock) return s.Name;
					return cs::String();
				}, 5.0).At(32));
			TourRegistry::Add(YardStop("graveyard-warm", "Ship graveyard, warm",
				"Derelict hulks and old battle wreckage backlit by a hot star close by, in an orange haze of dust",
				[](const Graveyard& g) { return g.Warm && g.Hulks.Count() > 0; }, false).At(40));
			TourRegistry::Add(YardStop("graveyard-cold", "Ship graveyard, cold",
				"The same far from its star: hulks salvageable among dense wreckage, in an icy blue-white haze",
				[](const Graveyard& g) { return !g.Warm && g.Hulks.Count() > 0; }, false).At(41));
			TourRegistry::Add(YardStop("derelict-station", "Derelict station",
				"A station left dark and holed, its modules broken off: salvageable, and later explorable",
				[](const Graveyard& g) { return g.Stations.Count() > 0; }, true).At(42));
			TourRegistry::Add(TourStop::Placeholder("Places", "gas-giant-harvesters", "Gas giant harvesters",
				"every gas giant's gas-harvesting platforms and ships, the contracts to guard them and the raids on them", "US-T146").At(90));

			// ---- Presentation.
			TourRegistry::Add(TourStop::Make("Presentation", "orbits-fade", "Orbit lines fade with distance",
				"Near Earth its own orbit line is full strength; the others fade off with distance, never quite to nothing")
				.At(10)
				.WithSetup([](UnitySpaceApp& app) -> cs::String
				{
					int earth = BodyNamed(app, "Earth");
					if (earth < 0) return "no Earth here";
					auto t = Target::OfBody(earth);
					app.Select(t);
					app.SpaceCamera->FocusOn(t, 140, 14.0f);
					return cs::String();
				})
				.Until([](UnitySpaceApp&, double t) { return t > 3; })
				.For(8).AtBothScales());
			TourRegistry::Add(TourStop::Make("Presentation", "orbits-in-battle", "Orbit lines clear out in a fight",
				"A battle breaks out off Mars: every orbit line fades out in a moment, and comes back after")
				.At(11)
				.WithSetup([](UnitySpaceApp& app) -> cs::String
				{
					return Skirmish(app, "Mars", cs::List<ShipClass>{ ShipClass::Destroyer, ShipClass::Frigate, ShipClass::Corvette }) != nullptr ? cs::String() : cs::String("the skirmish did not start");
				})
				.Until([](UnitySpaceApp&, double t) { return t > 2; })
				.For(8).AtBothScales());
			for (int scale : { 1, 0 })
			{
				TourRegistry::Add(TourStop::Make("Presentation", scale == 1 ? "scale-game" : "scale-realistic",
					scale == 1 ? "The game scale" : "The realistic scale",
					scale == 1 ? "Earth and the Moon at the game scale: worlds drawn large and close, so the system can be flown"
						: "The same at true scale: true sizes and true distances (G toggles them at any time)")
					.At(20 + (1 - scale)).AtScale(scale)
					.WithSetup([](UnitySpaceApp& app) -> cs::String
					{
						int earth = BodyNamed(app, "Earth");
						if (earth < 0) return "no Earth here";
						auto t = Target::OfBody(earth);
						app.Select(t);
						app.SpaceCamera->FocusOn(t, 9);
						return cs::String();
					})
					.Until([](UnitySpaceApp&, double t) { return t > 3; })
					.For(7));
			}

			// ---- Ships: every hull class in a line off Earth, flown past.
			auto lineup = std::make_shared<Lineup>();
			TourRegistry::Add(TourStop::Make("Ships", "fleet-lineup", "The fleet",
				"Every hull class in one line off Earth, small craft to capital ships, drawn from Ultimate Spaceships Creator")
				.At(10)
				.WithSetup([lineup](UnitySpaceApp& app) -> cs::String
				{
					int earth = BodyNamed(app, "Earth");
					if (earth < 0) return "no Earth here";
					lineup->Ships.Clear();
					static const ShipClass classes[] = {
						ShipClass::Fighter, ShipClass::Interceptor, ShipClass::Gunship, ShipClass::Bomber, ShipClass::Recon, ShipClass::Courier,
						ShipClass::Corvette, ShipClass::Frigate, ShipClass::Destroyer, ShipClass::Miner, ShipClass::Freighter, ShipClass::Explorer,
						ShipClass::Cruiser, ShipClass::CommandCruiser, ShipClass::Battleship, ShipClass::Carrier,
					};
					DVec3 outward;
					double km = app.BodyStandoffKm(earth, outward);
					auto along = DVec3::Cross(outward, DVec3::UnitZ);
					along = along.LengthSquared() > 1e-9 ? along.Normalized() : DVec3::UnitX;
					double at = 0;
					for (auto cls : classes)
					{
						double len = CombatSpec::Of(cls)->LengthKm;
						at += len * 0.5;
						lineup->Ships.Add(app.Fleet()->SpawnAt(cs::Cat("Lineup ", ShipClassSpec::Of(cls)->DisplayName), cls, app.World()->Sol, earth,
							outward * km + along * at, along, app.Clock->Jd));
						at += len * 0.5 + std::max(0.15, len * 0.6);
					}
					return cs::String();
				})
				.Until([](UnitySpaceApp&, double t) { return t > 2; })
				.WithCamera([lineup](UnitySpaceApp& app, double t)
				{
					// Along the line from the smallest to the biggest, off its side and a little above.
					int n = lineup->Ships.Count();
					if (n == 0) return;
					double u = std::min(1.0, t / 13.0) * (n - 1);
					int i = std::min(n - 2, static_cast<int>(u));
					if (i < 0) i = 0;
					double f = u - i;
					auto a = lineup->Ships[i], b = lineup->Ships[std::min(n - 1, i + 1)];
					DVec3 subject = DVec3::Lerp(app.ShipScene(a), app.ShipScene(b), f);
					double size = std::max(app.ShipDisplayLength(a) * (1 - f) + app.ShipDisplayLength(b) * f, 1e-6);
					auto line = (app.ShipScene(lineup->Ships[n - 1]) - app.ShipScene(lineup->Ships[0]));
					auto side = DVec3::Cross(line.LengthSquared() > 0 ? line.Normalized() : DVec3::UnitX, DVec3::UnitY);
					side = side.LengthSquared() > 1e-9 ? side.Normalized() : DVec3::UnitZ;
					LookAt(app, subject, subject + (side + DVec3(0, 0.35, 0)) * (size * 3.2) - (line.LengthSquared() > 0 ? line.Normalized() : DVec3::UnitX) * (size * 1.2));
				})
				.For(14).AtBothScales());

			// ---- Travel: one jump with a void stop on the way, and a Void Raider sent to it.
			TourRegistry::Add(TourStop::Make("Travel", "jump-drive", "The jump drive",
				"A battleship's own drive: plotted to a star past one jump's reach, run out past the rim, spooled and away, the stars sliding by")
				.At(10)
				.WithSetup([](UnitySpaceApp& app) { return TakeJumpHelm(app); })
				.Until(JumpUntil([](UnitySpaceApp& app) { return app.InFtlCruise(); }), 150)
				.For(10));
			TourRegistry::Add(TourStop::Make("Travel", "void-stop", "A void stop between jumps",
				"Seven light-years a jump: the drive drops out in the dark between the stars to cool and spool again, nothing for light-years round")
				.At(20).In(cs::String()).KeepingSeat()
				.WithSetup([](UnitySpaceApp& app) { return TakeJumpHelm(app); })
				.Until(JumpUntil([](UnitySpaceApp& app) { return HoldVoidStop(app); }), 180)
				.WithCamera([](UnitySpaceApp& app, double) { HoldVoidStop(app); })
				.For(8));
			TourRegistry::Add(TourStop::Make("Travel", "void-raider-ambush", "A Void Raider ambush",
				"Stranded at the stop: an unidentified sensor ghost closes and strikes while the drive spools (Tab pings, Shift+F3 runs dark)")
				.At(30).In(cs::String()).KeepingSeat()
				.WithSetup([](UnitySpaceApp& app) -> cs::String
				{
					if (!HoldVoidStop(app)) return "not at a void stop (play the jump first)";
					auto c = app.FtlCruise;
					// A ghost found the hull: it closes for a short while and strikes (the raiders' own step does the rest).
					c->RaidStrikeJd = app.Clock->Jd + VoidRaiders::StalkDays * 0.3;
					c->RaidSeen = true;
					c->RaidStruck = false;
					return cs::String();
				})
				.Until([](UnitySpaceApp& app, double t) { HoldVoidStop(app); return t > 2 && (app.FtlCruise == nullptr || app.FtlCruise->RaidStruck || t > 20); }, 30)
				.WithCamera([](UnitySpaceApp& app, double) { HoldVoidStop(app); })
				.For(8));
			TourRegistry::Add(TourStop::Placeholder("Travel", "sky-modes", "Skybox, Milky Way and the edge",
				"each system inside its biome skybox, the Milky Way between the stars, the drop-out at the system edge already inside it", "US-T145").At(80));
			TourRegistry::Add(TourStop::Placeholder("Travel", "edge-run", "Out through the system edge and back",
				"flying straight out past the rim into open space, and what happens to the sky as you go", "US-T145").At(81));
		}

		TourRegistration GTourStops(&RegisterStops);
	}
}
