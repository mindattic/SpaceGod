#include "SceneTour.h"

#include "CaptureRunner.h"
#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Views/AsteroidBelt.h"

#include "Astro.h"
#include "AsteroidField.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "BodyOrientation.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "GasGiantWeather.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Infrastructure/Infrastructure.h"
#include "Systems/Galaxy.h"

#include <memory>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// LINQ's First(predicate): the first match, or InvalidOperationException when there is none.
		template <class T, class P> T First(const cs::List<T>& items, P predicate)
		{
			for (const auto& item : items)
				if (predicate(item)) return item;
			throw cs::InvalidOperationException("Sequence contains no matching element");
		}
	}

	// ---------------------------------------------------------------- the coroutine driver

	SceneTour::Runner::Runner(Routine root)
	{
		if (root) stack.push_back(std::move(root));
	}

	bool SceneTour::Runner::MoveNext()
	{
		try
		{
			while (!stack.empty())
			{
				auto handle = stack.back().Handle();
				handle.resume();
				auto& promise = handle.promise();
				if (promise.exception) std::rethrow_exception(std::exchange(promise.exception, nullptr));
				if (handle.done())
				{
					// The parent (if any) carries on now, in this same frame.
					stack.pop_back();
					continue;
				}
				if (promise.child)
				{
					// A nested routine: its first step runs now, in this same frame.
					Routine child = promise.pendingChild();
					if (child) stack.push_back(std::move(child));
					continue;
				}
				return true; // yield return null: the next frame
			}
			return false;
		}
		catch (const std::exception& e)
		{
			Debug::LogError(e.what());
		}
		catch (...)
		{
			Debug::LogError("[Unity.SpaceGod] A coroutine threw an unknown exception.");
		}
		stack.clear();
		return false;
	}

	// ---------------------------------------------------------------- the tour

	SceneTour::Routine SceneTour::Run(UnitySpaceApp& app, StopFunc stop)
	{
		co_yield Wait(1.5f);
		co_yield stop("01-earth-gameplay");

		app.FocusSystemOverview();
		co_yield Wait(2.6f);
		co_yield stop("02-inner-system-gameplay");

		auto carrier = app.Fleet()->Ships().Find([](const cs::Ref<Ship>& s) { return !s->Destroyed && s->Controller()->Phase != ShipPhase::Landed && s->Spec->Class == ShipClass::Carrier; });
		Focus(app, Target::OfShip(carrier), 7);
		co_yield Wait(2.6f);
		co_yield stop("03-carrier-group");

		auto battleship = app.Fleet()->Ships().Find([](const cs::Ref<Ship>& s) { return !s->Destroyed && s->Controller()->Phase != ShipPhase::Landed && s->Spec->Class == ShipClass::Battleship && !s->IsEscort(); });
		Focus(app, Target::OfShip(battleship), 4);
		co_yield Wait(2.6f);
		co_yield stop("04-battleship");

		auto freighter = app.Fleet()->Ships().Find([](const cs::Ref<Ship>& s) { return !s->Destroyed && s->Controller()->Phase != ShipPhase::Landed && s->Spec->Class == ShipClass::Freighter; });
		Focus(app, Target::OfShip(freighter), 3.5);
		co_yield Wait(2.6f);
		co_yield stop("05-freighter");

		auto miner = app.Fleet()->Ships().Find([](const cs::Ref<Ship>& s) { return !s->Destroyed && s->Controller()->Phase != ShipPhase::Landed && s->Spec->Class == ShipClass::Miner; });
		Focus(app, Target::OfShip(miner), 3.5);
		co_yield Wait(2.6f);
		co_yield stop("05b-miner");

		auto courier = app.Fleet()->Ships().Find([](const cs::Ref<Ship>& s) { return !s->Destroyed && s->Controller()->Phase != ShipPhase::Landed && s->Spec->Class == ShipClass::Courier; });
		Focus(app, Target::OfShip(courier), 3);
		co_yield Wait(2.6f);
		co_yield stop("05c-courier");

		Focus(app, Target::OfBody((*app.Catalog())["Saturn"]->Index), 3.2);
		co_yield Wait(2.6f);
		co_yield stop("06-saturn");

		Focus(app, Target::OfBody((*app.Catalog())["Earth"]->Index), 9);
		co_yield Wait(2.6f);
		co_yield stop("07-earth-system-gameplay");

		app.ToggleScale();
		co_yield Wait(3.2f);
		co_yield stop("08-earth-system-realistic");

		app.FocusSystemOverview();
		co_yield Wait(2.8f);
		co_yield stop("09-inner-system-realistic");

		app.ToggleScale();
		co_yield Wait(3.2f);
		Focus(app, Target::OfBody((*app.Catalog())["Jupiter"]->Index), 14);
		co_yield Wait(2.8f);
		co_yield stop("10-jupiter-system");

		co_yield InfrastructureTour(app, stop);
		co_yield CentauriTour(app, stop);
		co_yield SurfaceTour(app, stop);
		co_yield BattleTour(app, stop);
		co_yield CombatShowcaseTour(app, stop);
	}

	// ---------------------------------------------------------------- what is in orbit

	/// Earth's docking rings and satellite clouds, a ring up close, a merchant coming down through
	/// the atmosphere under it, a yard with a hull on its slip, and the ISS.
	SceneTour::Routine SceneTour::InfrastructureTour(UnitySpaceApp& app, StopFunc stop)
	{
		auto infra = app.World()->Infrastructure;
		if (infra == nullptr) co_return;
		int earth = (*app.Catalog())["Earth"]->Index;
		View(app, earth, Blend(Dir(app, earth, 0), Side(Dir(app, earth, 0)), 0.45), 7.5);
		co_yield Wait(3.0f);
		co_yield stop("11a-earth-rings-and-satellites");

		auto ny = First(infra->Rings, [](const cs::Ref<DockingRing>& r) { return r->City == "New York"; });
		auto rt = Target::OfRing(ny);
		app.Select(rt);
		app.SpaceCamera->FocusOn(rt, 9, 20.0f);
		co_yield Wait(3.0f);
		co_yield stop("11b-new-york-ring");

		// A merchant coming down: hurry the clock until one starts down to Earth, then watch it.
		auto Fresh = [&app, earth](const cs::Ref<Ship>& s)
		{
			return !s->Destroyed && s->Phase == ShipPhase::Descent && s->Ring != nullptr && s->Ring->Body == earth
				&& app.Clock->Jd - s->PhaseStartJd < s->Ring->DescentDays * 0.25;
		};
		app.Clock->SetPaused(false);
		app.Clock->SetWarp(5);
		cs::Ref<Ship> lander = nullptr;
		float deadline = Time::unscaledTime + 30.0f;
		while (lander == nullptr && Time::unscaledTime < deadline)
		{
			lander = app.Fleet()->Ships().Find(Fresh);
			co_yield nullptr;
		}
		if (lander != nullptr)
		{
			app.Clock->SetWarp(3);
			[[maybe_unused]] auto lt = Target::OfShip(lander);
			// From the side and a little above, so the ground it is heading for fills the background.
			auto up = (app.ShipScene(lander) - app.BodyScene(earth)).Normalized();
			ViewShip(app, lander, Blend(Side(up), up, 0.3), 16);
			co_yield Wait(3.0f);
			co_yield stop("11c-descent-from-the-ring");
			// Slow down for the last of it, through the entry interface.
			deadline = Time::unscaledTime + 40.0f;
			while (lander->Phase == ShipPhase::Descent && Time::unscaledTime < deadline && (app.Clock->Jd - lander->PhaseStartJd) / lander->Ring->DescentDays < 0.9)
				co_yield nullptr;
			app.Clock->SetWarp(2);
			auto low = (app.ShipScene(lander) - app.BodyScene(earth)).Normalized();
			ViewShip(app, lander, Blend(Side(low), low, 0.2), 10);
			deadline = Time::unscaledTime + 25.0f;
			while (lander->Phase == ShipPhase::Descent && app.ShipStateOf(lander).Heat < 0.5f && Time::unscaledTime < deadline) co_yield nullptr;
			if (lander->Phase == ShipPhase::Descent)
			{
				co_yield Wait(1.2f);
				co_yield stop("11d-entry-plasma");
			}
		}
		app.Clock->SetWarp(SimClock::DefaultWarpIndex);

		auto queued = app.Fleet()->Ships().Find([](const cs::Ref<Ship>& s) { return !s->Destroyed && s->Phase == ShipPhase::Queued; });
		if (queued != nullptr)
		{
			auto qt = Target::OfShip(queued);
			app.Select(qt);
			app.SpaceCamera->FocusOn(qt, 10, 18.0f);
			co_yield Wait(3.0f);
			co_yield stop(cs::Cat("11e-queued-at-", cs::Strings::Replace(cs::Strings::ToLowerInvariant(queued->Ring->Name), " ", "-")));
		}

		auto yard = infra->Structures.Find([](const cs::Ref<OrbitalStructure>& s) { return s->Kind == StructureKind::Shipyard && s->SlipProgress() > 0.3; });
		if (yard == nullptr) yard = First(infra->Structures, [](const cs::Ref<OrbitalStructure>& s) { return s->Kind == StructureKind::Shipyard; });
		auto yt = Target::OfStructure(yard);
		app.Select(yt);
		app.SpaceCamera->FocusOn(yt, 3.2, 24.0f);
		co_yield Wait(3.2f);
		co_yield stop("11f-orbital-shipyard");

		auto iss = infra->Structures.Find([](const cs::Ref<OrbitalStructure>& s) { return s->Name == "ISS"; });
		if (iss != nullptr)
		{
			auto it = Target::OfStructure(iss);
			app.Select(it);
			app.SpaceCamera->FocusOn(it, 4, 15.0f);
			co_yield Wait(3.0f);
			co_yield stop("11g-iss");
		}
	}

	// ---------------------------------------------------------------- Alpha Centauri

	/// Out to Alpha Centauri through the Centauri Gate, round its stars, worlds and gates, and home.
	SceneTour::Routine SceneTour::CentauriTour(UnitySpaceApp& app, StopFunc stop)
	{
		auto sol = app.ActiveSystem;
		auto ac = app.Galaxy->Find("Alpha Centauri");
		if (ac == nullptr) co_return;
		app.Clock->SetPaused(false);
		app.Clock->SetWarp(SimClock::DefaultWarpIndex);
		app.TravelTo(ac);
		while (app.Travel->Active && app.Travel->Stage != Cameras::StarTravel::Leg::Cross) co_yield nullptr;
		// Through the gate the crossing is a short spell inside it.
		co_yield Wait(app.Travel->ThroughGate ? 0.8f : 1.9f);
		co_yield stop("30-between-the-stars");
		while (app.Travel->Active) co_yield nullptr;
		co_yield Wait(2.2f);
		co_yield stop("31-alpha-centauri");
		// The A-B orbit is tilted 70-odd degrees to the ecliptic: level the view to it.
		app.Reorient();
		co_yield Wait(2.2f);
		co_yield stop("31b-alpha-centauri-levelled");

		int a = (*app.Catalog())["Alpha Centauri A"]->Index;
		int b = (*app.Catalog())["Alpha Centauri B"]->Index;
		View(app, a, Blend(Dir(app, a, b), DVec3(0, 1, 0), 0.35), 5);
		co_yield Wait(3.0f);
		co_yield stop("32-alpha-centauri-a-and-b");

		Focus(app, Target::OfBody((*app.Catalog())["Rigil Gate"]->Index), 7);
		co_yield Wait(3.0f);
		co_yield stop("33-rigil-gate");

		int giant = (*app.Catalog())["Alpha Centauri Ab"]->Index;
		View(app, giant, Blend(Dir(app, giant, a), Side(Dir(app, giant, a)), 0.4), 3);
		co_yield Wait(3.0f);
		co_yield stop("34-candidate-giant-s1");

		int proxima = (*app.Catalog())["Proxima Centauri"]->Index;
		int pb = (*app.Catalog())["Proxima b"]->Index;
		auto pt = Target::OfBody(proxima);
		app.Select(pt);
		app.SpaceCamera->FocusOn(pt, app.Scale()->LocalDistance((*app.Catalog())[proxima], 0.06 * Astro::AuKm) * 1.8 / app.BodyDisplayRadius(proxima), 40.0f);
		co_yield Wait(3.4f);
		co_yield stop("35-proxima-system");

		View(app, pb, Blend(Dir(app, pb, proxima), Side(Dir(app, pb, proxima)), 0.8), 3.2);
		co_yield Wait(3.0f);
		co_yield stop("36-proxima-b");

		const auto& rings = app.World()->Infrastructure->Rings;
		auto gate = rings.Count() > 0 ? rings[0] : nullptr;
		if (gate != nullptr)
		{
			auto gt = Target::OfRing(gate);
			app.Select(gt);
			app.SpaceCamera->FocusOn(gt, 10, 20.0f);
			co_yield Wait(3.0f);
			co_yield stop(cs::Cat("37-", cs::Strings::Replace(cs::Strings::ToLowerInvariant(gate->Name), " ", "-")));
		}

		app.TravelTo(sol);
		while (app.Travel->Active) co_yield nullptr;
		co_yield Wait(1.5f);
		co_yield stop("38-home-again");
	}

	// ---------------------------------------------------------------- the war

	/// Runs the clock fast until a real battle breaks out somewhere (nothing is staged), then
	/// slows to battle time and frames it: the whole engagement, then a capital ship up close.
	SceneTour::Routine SceneTour::BattleTour(UnitySpaceApp& app, StopFunc stop)
	{
		app.Clock->SetPaused(false);
		app.Clock->SetWarp(6);
		// C#'s closure over `battle` and `war`: shared with the handler, which may outlive this routine (a stopped tour).
		struct Watch
		{
			cs::Ref<Battle> battle;
			War* war = nullptr;
			int token = 0;
		};
		auto watch = std::make_shared<Watch>();
		watch->war = app.World()->War.get();
		UnitySpaceApp* appPtr = &app;
		// Catch it the moment it starts (it does not run in its first frame) and drop to battle time.
		auto Started = [watch, appPtr](cs::Ref<Battle> b)
		{
			// The tour was stopped: stop watching.
			if (CaptureRunner::Current() == nullptr)
			{
				watch->war->BattleStarted -= watch->token;
				return;
			}
			// A real fleet action: at least two armed warships or platforms on each side.
			auto Armed = [](const cs::Ref<BattleUnit>& u) { return u->Kind == UnitKind::Platform || (u->Ship != nullptr && (u->Ship->Spec->IsMilitary || u->Ship->Faction->IsPirate)); };
			auto ArmedOn = [&](int side)
			{
				int n = 0;
				for (const auto& u : b->Units)
					if (u->Side == side && Armed(u)) n++;
				return n;
			};
			bool fleetAction = ArmedOn(0) >= 2 && ArmedOn(1) >= 2;
			// A lane ambush is worth watching too: the pulse, the stunned ships, the boarders going in.
			if (watch->battle != nullptr || !(fleetAction || b->Site != nullptr)) return;
			watch->battle = b;
			appPtr->Clock->SetWarp(0);
		};
		watch->token = watch->war->BattleStarted += Started;
		app.FocusSystemOverview();
		float deadline = Time::unscaledTime + 240.0f;
		while (watch->battle == nullptr && Time::unscaledTime < deadline) co_yield nullptr;
		watch->war->BattleStarted -= watch->token;
		auto battle = watch->battle;
		if (battle == nullptr)
		{
			app.Clock->SetWarp(SimClock::DefaultWarpIndex);
			co_return;
		}
		app.EnterBattleTime(battle);
		cs::List<cs::Ref<BattleUnit>> present;
		for (const auto& u : battle->Units)
			if (u->Ship != nullptr && u->Present()) present.Add(u);
		auto byHull = cs::OrderByDescending(present, [](const cs::Ref<BattleUnit>& u) { return u->Combat->Spec->Hull; });
		if (byHull.Count() == 0) throw cs::InvalidOperationException("Sequence contains no elements");
		auto flagship = byHull[0]->Ship;
		auto t = Target::OfShip(flagship);
		app.Select(t);
		app.SpaceCamera->FocusOn(t, 30, 25.0f);
		co_yield Wait(6.0f);
		co_yield stop("20-battle");
		if (!flagship->Destroyed && flagship->Battle != nullptr)
		{
			app.SpaceCamera->FocusOn(t, 5, 12.0f);
			co_yield Wait(4.0f);
			co_yield stop("21-battle-capital-ship");
		}
		// Ride it out, cutting between the two flagships and any carrier with fighters out.
		for (int k = 0; k < 6 && !battle->Over; k++)
		{
			auto subject = Subject(battle, k);
			if (subject != nullptr)
			{
				auto st = Target::OfShip(subject);
				app.Select(st);
				app.SpaceCamera->FocusOn(st, k % 3 == 2 ? 9 : 16, k % 2 == 0 ? 30.0f : 12.0f);
			}
			co_yield Wait(8.0f);
			co_yield stop(cs::Cat("22-battle-", k + 1));
		}
		app.Clock->SetWarp(SimClock::DefaultWarpIndex);
	}

	// ---------------------------------------------------------------- combat showcase (U.SPC-T10)

	/// Three staged skirmishes, each built to put one distinct combat feel on camera reliably
	/// rather than hoping the live war happens to produce it in time for a capture: a capital-ship
	/// broadside line (Battle.BroadsideFacing), then a gunship "heavy fighter"
	/// brawl and a Courier "light fighter" one (their forward guns, see
	/// CombatSpec), each watched close since nobody is actually piloting them.
	SceneTour::Routine SceneTour::CombatShowcaseTour(UnitySpaceApp& app, StopFunc stop)
	{
		co_yield BroadsideShowcase(app, stop);
		co_yield DogfightShowcase(app, stop, "Neptune", app.World()->Sol, app.World()->Reach, ShipClass::Gunship, "41-gunship-dogfight");
		co_yield DogfightShowcase(app, stop, "Pluto", app.World()->Pirates, app.World()->Sol, ShipClass::Courier, "42-courier-dogfight");
	}

	/// A Sol/Reach battleship-and-cruiser line off Uranus: the whole engagement, then
	/// whichever capital ship has taken the most damage, to catch a flank turned in once its bow
	/// guns are out rather than its nose held on target.
	SceneTour::Routine SceneTour::BroadsideShowcase(UnitySpaceApp& app, StopFunc stop)
	{
		int uranus = (*app.Catalog())["Uranus"]->Index;
		auto battle = TriggerSkirmish(app, uranus, app.World()->Sol, app.World()->Reach,
			cs::List<ShipClass>{ ShipClass::Battleship, ShipClass::Battleship, ShipClass::Cruiser });
		if (battle == nullptr) co_return;
		app.Clock->SetPaused(false);
		app.WatchBattle(battle);
		co_yield Wait(8.0f);
		co_yield stop("40-capital-broadside");
		co_yield Wait(14.0f);
		cs::List<cs::Ref<BattleUnit>> present;
		for (const auto& u : battle->Units)
			if (u->Ship != nullptr && u->Present()) present.Add(u);
		auto byDamage = cs::OrderByDescending(present, [](const cs::Ref<BattleUnit>& u) { return u->Combat->Damage(); });
		cs::Ref<Ship> hurt = byDamage.Count() > 0 ? byDamage[0]->Ship : nullptr;
		if (hurt != nullptr)
		{
			auto t = Target::OfShip(hurt);
			app.Select(t);
			app.SpaceCamera->FocusOn(t, 5, 12.0f);
		}
		co_yield Wait(4.0f);
		co_yield stop("40b-capital-broadside-flank");
		app.Clock->SetWarp(SimClock::DefaultWarpIndex);
	}

	/// A small, fast skirmish between <paramref name="cls"/> hulls, watched in close chase
	/// from behind one of them (Cameras.SpaceCamera.EnterPilotChase, otherwise only
	/// used for the player's own piloted ship) so its forward-mounted gun -- a gunship's
	/// light railgun and turrets, a Courier's FighterGuns -- reads clearly as it fights.
	SceneTour::Routine SceneTour::DogfightShowcase(UnitySpaceApp& app, StopFunc stop, cs::String bodyName, cs::Ref<Faction> aggressor,
		cs::Ref<Faction> defender, ShipClass cls, cs::String stopId)
	{
		int body = (*app.Catalog())[bodyName]->Index;
		auto battle = TriggerSkirmish(app, body, aggressor, defender, cs::List<ShipClass>{ cls, cls, cls });
		if (battle == nullptr) co_return;
		app.Clock->SetPaused(false);
		app.EnterBattleTime(battle);
		for (int k = 0; k < 2 && !battle->Over; k++)
		{
			auto onSide0 = battle->Units.Find([](const cs::Ref<BattleUnit>& u) { return u->Ship != nullptr && u->Present() && u->Side == 0; });
			cs::Ref<Ship> subject = onSide0 != nullptr ? onSide0->Ship : nullptr;
			if (subject == nullptr)
			{
				auto any = battle->Units.Find([](const cs::Ref<BattleUnit>& u) { return u->Ship != nullptr && u->Present(); });
				subject = any != nullptr ? any->Ship : nullptr;
			}
			if (subject == nullptr) break;
			auto t = Target::OfShip(subject);
			app.Select(t);
			app.SpaceCamera->EnterPilotChase(t, cls == ShipClass::Gunship ? 5 : 4);
			co_yield Wait(8.0f);
			co_yield stop(k == 0 ? stopId : stopId + "b");
		}
		app.SpaceCamera->ExitFocus();
		app.Clock->SetWarp(SimClock::DefaultWarpIndex);
	}

	/// Spawns <paramref name="classes"/> for each side parked near <paramref name="body"/>,
	/// already hostile, and starts the fight at once -- a deterministic stand-in for waiting on the
	/// live war to produce the matchup a demo capture wants to show (an empty, unsettled body such
	/// as Uranus, Neptune or Pluto keeps the live war's own traffic out of it).
	cs::Ref<Battle> SceneTour::TriggerSkirmish(UnitySpaceApp& app, int body, const cs::Ref<Faction>& aggressor, const cs::Ref<Faction>& defender,
		const cs::List<ShipClass>& classes)
	{
		double jd = app.Clock->Jd;
		auto dir = DVec3(1, 0.15, 0.4).Normalized();
		auto side = Side(dir);
		// US-T213: held out where the fight is laid out (Battle::AnchorDistanceKm, clear of the world and its rings at both
		// scales), not a few thousand km from the centre, inside the world.
		double standoff = Battle::AnchorDistanceKm((*app.Catalog())[body]);
		for (int i = 0; i < classes.Count(); i++)
			app.Fleet()->SpawnAt(cs::Cat("Showcase ", aggressor->Name, " ", i + 1), classes[i], aggressor, body, dir * (standoff + i * 400), -dir, jd);
		for (int i = 0; i < classes.Count(); i++)
			app.Fleet()->SpawnAt(cs::Cat("Showcase ", defender->Name, " ", i + 1), classes[i], defender, body, dir * (standoff + i * 400) + side * 3000, dir, jd);
		return app.World()->War->TryStartBattle(body, aggressor, jd);
	}

	// ---------------------------------------------------------------- surfaces, weather, asteroids

	SceneTour::Routine SceneTour::SurfaceTour(UnitySpaceApp& app, StopFunc stop)
	{
		int moon = (*app.Catalog())["Moon"]->Index;
		int earth = (*app.Catalog())["Earth"]->Index;
		// Between the Earth-facing and sunlit directions: the near side, partly lit.
		View(app, moon, Blend(Dir(app, moon, earth), Dir(app, moon, 0), 0.5), 3.2);
		co_yield Wait(3.0f);
		co_yield stop("11-moon-near-side");
		// Low sun across the surface: LOLA relief along the terminator.
		View(app, moon, Blend(Dir(app, moon, 0), Side(Dir(app, moon, 0)), 0.75), 2.4);
		co_yield Wait(3.0f);
		co_yield stop("12-moon-terminator");

		const cs::List<cs::String> moons{ "Io", "Europa", "Ganymede" };
		for (const auto& name : moons)
		{
			int i = (*app.Catalog())[name]->Index;
			View(app, i, Blend(Dir(app, i, 0), Side(Dir(app, i, 0)), 0.3), 3.0);
			co_yield Wait(3.0f);
			co_yield stop(cs::Cat("13-", cs::Strings::ToLowerInvariant(name)));
		}

		// Jupiter with the Great Red Spot turned toward the Sun, seen from just off the subsolar point.
		int jupiter = (*app.Catalog())["Jupiter"]->Index;
		app.Clock->SetPaused(true); // Jupiter turns 110 degrees in 3 s at the default warp
		app.Clock->SetJd(NextGrsNoon(app, jupiter));
		co_yield nullptr;
		auto grs = GrsDirection(app, jupiter);
		View(app, jupiter, Blend(grs, Dir(app, jupiter, 0), 0.2), 2.1);
		co_yield Wait(3.0f);
		co_yield stop("14-jupiter-great-red-spot");
		View(app, jupiter, GrsDirection(app, jupiter), 1.35);
		co_yield Wait(3.0f);
		co_yield stop("15-jupiter-grs-close");
		// One System III rotation (9 h 55.5 m) later the spot is back at noon: the bands have moved
		// with their winds and the oval has turned counter-clockwise.
		app.Clock->SetJd(app.Clock->Jd + 360.0 / 870.536);
		co_yield nullptr;
		View(app, jupiter, GrsDirection(app, jupiter), 1.35);
		co_yield Wait(3.0f);
		co_yield stop("15b-jupiter-grs-close-one-rotation-later");
		app.Clock->SetPaused(false);

		const cs::List<cs::String> worlds{ "Mars", "Titan", "Pluto", "Venus" };
		for (const auto& name : worlds)
		{
			int i = (*app.Catalog())[name]->Index;
			View(app, i, Blend(Dir(app, i, 0), Side(Dir(app, i, 0)), 0.35), name == "Titan" ? 3.0 : 2.6);
			co_yield Wait(3.2f);
			co_yield stop(cs::Cat("16-", cs::Strings::ToLowerInvariant(name)));
		}

		// The belt from above at gameplay scale, then at true scale.
		auto sun = Target::OfBody(0);
		app.Select(sun);
		double span = app.Scale()->LocalDistance(app.Catalog()->Root(), 4.0 * Astro::AuKm);
		app.SpaceCamera->FocusOn(sun, span * 1.9 / app.BodyDisplayRadius(0), 62.0f);
		co_yield Wait(3.0f);
		co_yield stop("17-asteroid-belt-gameplay");
		co_yield Wait(3.0f);
		app.ToggleScale();
		co_yield Wait(3.2f);
		app.SpaceCamera->FocusOn(sun, app.Scale()->LocalDistance(app.Catalog()->Root(), 4.0 * Astro::AuKm) * 1.9 / app.BodyDisplayRadius(0), 62.0f);
		co_yield Wait(3.0f);
		co_yield stop("18-asteroid-belt-realistic");
		app.ToggleScale();
		co_yield Wait(3.2f);

		// Inside the belt near Vesta, looking along the orbit.
		auto belt = app.AsteroidBelt;
		if (belt != nullptr)
		{
			int vesta = belt->Field->IndexOfNumber(4);
			auto v = belt->SceneOf(vesta, app.Clock->Jd);
			auto outward = v.Normalized();
			auto along = DVec3::Cross(DVec3(0, 1, 0), outward).Normalized();
			// Just above the belt plane behind Vesta, looking ahead along its orbit and slightly down.
			auto eye = v - along * 1500 + DVec3(0, 1200, 0);
			auto look = (v + along * 9000 - outward * 1500 - eye).Normalized();
			app.Select(Target::None());
			app.SpaceCamera->PlaceFree(eye, static_cast<float>(cs::Math::Atan2(look.X, look.Z) * Mathf::Rad2Deg), static_cast<float>(-cs::Math::Asin(look.Y) * Mathf::Rad2Deg));
			co_yield Wait(1.5f);
			co_yield stop("19-inside-the-belt");
		}
	}

	/// Who to watch next: a carrier with fighters out, else alternately each side's biggest ship.
	cs::Ref<Ship> SceneTour::Subject(const cs::Ref<Battle>& battle, int shot)
	{
		auto Biggest = [&battle](int side) -> cs::Ref<Ship>
		{
			cs::List<cs::Ref<BattleUnit>> onSide;
			for (const auto& u : battle->Units)
				if (u->Ship != nullptr && u->Present() && u->Side == side) onSide.Add(u);
			auto byHull = cs::OrderByDescending(onSide, [](const cs::Ref<BattleUnit>& u) { return u->Combat->Spec->Hull; });
			return byHull.Count() > 0 ? byHull[0]->Ship : nullptr;
		};
		if (shot % 3 == 2)
		{
			auto carrier = battle->Units.Find([](const cs::Ref<BattleUnit>& u) { return u->Ship != nullptr && u->Present() && u->Combat->Spec->Squadrons > 0; });
			if (carrier != nullptr && battle->Units.Exists([](const cs::Ref<BattleUnit>& u) { return u->Kind == UnitKind::Fighter && u->Present(); })) return carrier->Ship;
		}
		auto biggest = Biggest(shot % 2);
		return biggest != nullptr ? biggest : Biggest(1 - shot % 2);
	}

	/// Frames body <paramref name="body"/> from scene direction <paramref name="dir"/> (unit, from the body).
	void SceneTour::View(UnitySpaceApp& app, int body, DVec3 dir, double radii)
	{
		auto t = Target::OfBody(body);
		app.Select(t);
		double r = app.BodyDisplayRadius(body);
		auto eye = app.BodyScene(body) + dir * (r * radii * 1.3);
		auto look = -dir;
		app.SpaceCamera->PlaceFree(eye, static_cast<float>(cs::Math::Atan2(look.X, look.Z) * Mathf::Rad2Deg), static_cast<float>(-cs::Math::Asin(look.Y) * Mathf::Rad2Deg));
		app.SpaceCamera->FocusOn(t, radii, LevelPitchDeg(app, dir));
	}

	/// Frames a ship from scene direction <paramref name="dir"/> (unit, from the ship).
	void SceneTour::ViewShip(UnitySpaceApp& app, const cs::Ref<Ship>& ship, DVec3 dir, double radii)
	{
		auto t = Target::OfShip(ship);
		app.Select(t);
		auto eye = app.ShipScene(ship) + dir * (app.ShipDisplayLength(ship) * 0.5 * radii * 1.3);
		auto look = -dir;
		app.SpaceCamera->PlaceFree(eye, static_cast<float>(cs::Math::Atan2(look.X, look.Z) * Mathf::Rad2Deg), static_cast<float>(-cs::Math::Asin(look.Y) * Mathf::Rad2Deg));
		app.SpaceCamera->FocusOn(t, radii, LevelPitchDeg(app, dir));
	}

	/// FocusOn's pitch override is an elevation in the (possibly tilted) level frame, not world axes.
	float SceneTour::LevelPitchDeg(UnitySpaceApp& app, DVec3 dir)
	{
		auto world = Vector3(static_cast<float>(dir.X), static_cast<float>(dir.Y), static_cast<float>(dir.Z));
		auto local = Quaternion::Inverse(app.SpaceCamera->UpFrame) * world;
		return Mathf::Asin(Mathf::Clamp(local.y, -1.0f, 1.0f)) * Mathf::Rad2Deg;
	}

	DVec3 SceneTour::Dir(UnitySpaceApp& app, int from, int to) { return (app.BodyScene(to) - app.BodyScene(from)).Normalized(); }
	DVec3 SceneTour::Blend(DVec3 a, DVec3 b, double t) { return DVec3::Lerp(a, b, t).Normalized(); }
	DVec3 SceneTour::Side(DVec3 d) { return DVec3::Cross(DVec3(0, 1, 0), d).Normalized(); }

	/// Scene direction (Unity axes) from Jupiter's centre to the Great Red Spot.
	DVec3 SceneTour::GrsDirection(UnitySpaceApp& app, int jupiter)
	{
		auto body = (*app.Catalog())[jupiter];
		double jd = app.Clock->Jd;
		DVec3 north, meridian;
		BodyOrientation::Frame(body, jd, DVec3::Zero, north, meridian);
		auto east = DVec3::Cross(north, meridian);
		double lon = GasGiantWeather::GreatRedSpot::EastLongitudeDeg(jd) * Astro::Deg2Rad;
		double lat = GasGiantWeather::GreatRedSpot::LatitudeDeg * Astro::Deg2Rad;
		auto d = (meridian * cs::Math::Cos(lon) + east * cs::Math::Sin(lon)) * cs::Math::Cos(lat) + north * cs::Math::Sin(lat);
		return d.SwapYZ().Normalized();
	}

	/// The next time within one Jovian day that the spot is closest to local noon.
	double SceneTour::NextGrsNoon(UnitySpaceApp& app, int jupiter)
	{
		auto body = (*app.Catalog())[jupiter];
		double jd0 = app.Clock->Jd, best = jd0, bestDot = -2;
		auto toSun = (-app.Ephemeris()->Helio(jupiter)).Normalized();
		for (int k = 0; k < 400; k++)
		{
			double jd = jd0 + k * (10.0 / 24.0) / 400;
			DVec3 north, meridian;
			BodyOrientation::Frame(body, jd, DVec3::Zero, north, meridian);
			auto east = DVec3::Cross(north, meridian);
			double lon = GasGiantWeather::GreatRedSpot::EastLongitudeDeg(jd) * Astro::Deg2Rad;
			double lat = GasGiantWeather::GreatRedSpot::LatitudeDeg * Astro::Deg2Rad;
			auto d = (meridian * cs::Math::Cos(lon) + east * cs::Math::Sin(lon)) * cs::Math::Cos(lat) + north * cs::Math::Sin(lat);
			double dot = DVec3::Dot(d, toSun);
			if (dot > bestDot)
			{
				bestDot = dot;
				best = jd;
			}
		}
		return best;
	}

	void SceneTour::Focus(UnitySpaceApp& app, const Target& t, double radii)
	{
		app.Select(t);
		app.SpaceCamera->FocusOn(t, radii);
	}

	SceneTour::Routine SceneTour::Wait(float seconds)
	{
		float end = Time::unscaledTime + seconds;
		while (Time::unscaledTime < end) co_yield nullptr;
	}
}
