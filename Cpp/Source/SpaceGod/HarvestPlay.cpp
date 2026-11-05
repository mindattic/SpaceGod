// US-T146: the player's side of the gas works (see HarvestPlay.h), its dev menu entries and sg.Harvest.

#include "HarvestPlay.h"

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"
#include "Input/InputDevice.h"
#include "UI/CheatMenu.h"
#include "Camera/SpaceCamera.h"
#include "Tour/ShowcaseTour.h"
#include "Views/HarvesterView.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/BioMass.h"
#include "Combat/CombatSpecs.h"
#include "Combat/Diplomacy.h"
#include "Economy/GasHarvest.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Infrastructure/Infrastructure.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::HarvestPlay
{
	namespace
	{
		cs::Ref<GasHarvest> Works(UnitySpaceApp& app)
		{
			auto world = app.World();
			return world != nullptr ? world->Harvest : nullptr;
		}

		cs::Ref<Ship> Held(UnitySpaceApp& app)
		{
			if (!app.IsPiloting()) return nullptr;
			auto ship = app.Piloted.Ship;
			return ship != nullptr && !ship->Destroyed ? ship : nullptr;
		}

		cs::String Cr(double v) { return cs::Fmt(v, "N0"); }

		/// <summary>The giant the hull is at (within its guard range), its works, or null.</summary>
		cs::Ref<HarvestOperation> GiantHere(UnitySpaceApp& app, const cs::Ref<GasHarvest>& works, DVec3 helio)
		{
			cs::Ref<HarvestOperation> best;
			double bestKm = cs::Double::PositiveInfinity;
			for (const auto& op : works->Operations)
			{
				double km = (helio - app.Ephemeris()->Helio(op->Body)).Length();
				if (km <= works->GuardKm(op->Body) && km < bestKm) { bestKm = km; best = op; }
			}
			return best;
		}

		/// <summary>Puts a wave on the scene beside the guard: hulls into a fight with it, or a Bio Mass globule.</summary>
		void SpawnWave(UnitySpaceApp& app, const cs::Ref<GasHarvest>& works, const cs::Ref<HarvestContract>& c, const cs::Ref<HarvestWave>& w)
		{
			auto world = app.World();
			double jd = app.Clock->Jd;
			auto op = c->Operation;
			auto platform = op->Platforms[c->Platform];
			cs::List<cs::Ref<Ship>> made;
			int globule = -1;
			if (w->Kind == HarvestThreat::BioMass)
			{
				// A globule drifting in from the platform's side, a few tens of km off the guard.
				auto helio = app.PlayerHelioKm();
				auto toPlatform = works->PlatformHelio(op, c->Platform, jd) - helio;
				toPlatform = toPlatform.LengthSquared() > 1e-6 ? toPlatform.Normalized() : DVec3::UnitX;
				auto anchor = helio + toPlatform * 35.0 - app.Ephemeris()->Helio(op->Body);
				auto g = world->EnsureBioMass(jd)->Spawn(op->Body, anchor, DVec3::Zero, 1.5e5 * w->Size, jd);
				g->GoalGate = g->GoalBody = -1;
				globule = g->Id;
			}
			else
			{
				using Slot = UnitySpaceApp::CheatSlot;
				cs::String prefix = w->Kind == HarvestThreat::VoidRaiders ? cs::String("Ghost") : w->Kind == HarvestThreat::Rival && w->By != nullptr ? w->By->Prefix : cs::String("Raider");
				if (prefix.empty()) prefix = "Raider";
				static int serial = 0;
				auto name = [&](ShipClass cls) { return cs::Cat(prefix, " ", ToString(cls), " ", ++serial); };
				cs::List<Slot> slots;
				if (w->Size <= 1)
					slots = { Slot{ ShipClass::Interceptor, name(ShipClass::Interceptor), 5, 0, 0.3 }, Slot{ ShipClass::Fighter, name(ShipClass::Fighter), 5.5, 1.2, 0.2 },
						Slot{ ShipClass::Fighter, name(ShipClass::Fighter), 5.5, -1.2, 0.4 } };
				else if (w->Size == 2)
					slots = { Slot{ ShipClass::Corvette, name(ShipClass::Corvette), 4.5, 0, 0.3 }, Slot{ ShipClass::Gunship, name(ShipClass::Gunship), 4, 1.4, 0.2 },
						Slot{ ShipClass::Fighter, name(ShipClass::Fighter), 4.5, -1.3, 0.5 } };
				else
					slots = { Slot{ ShipClass::Frigate, name(ShipClass::Frigate), 4, 0.8, 0.3 }, Slot{ ShipClass::Destroyer, name(ShipClass::Destroyer), 4.5, -0.9, 0.2 },
						Slot{ ShipClass::Gunship, name(ShipClass::Gunship), 3.5, 1.8, 0.5 }, Slot{ ShipClass::Gunship, name(ShipClass::Gunship), 3.5, -1.8, 0.1 } };
				auto faction = w->By != nullptr ? w->By : world->Pirates;
				made = app.CheatSpawn(slots, faction);
				// Nothing could be put on the scene (co-op, between the stars): Core lets the wave hit on its own after its grace.
				if (made.Count() == 0) return;
			}
			works->Spawned(w, made, globule, jd);
			cs::String what = w->Kind == HarvestThreat::BioMass ? cs::String("BIO MASS drifting in on the ")
				: w->Kind == HarvestThreat::VoidRaiders ? cs::String("GHOST SHIPS out of the dark, closing on the ")
				: w->Kind == HarvestThreat::Rival ? cs::Cat(w->By != nullptr ? cs::Strings::ToUpperInvariant(w->By->Name) : cs::String("ENEMY"), " WARSHIPS cutting the supply at the ")
				: cs::String("RAIDERS closing on the ");
			app.ShowToast(cs::Cat(what, platform->Name, ": beat them off before they gut it"));
		}

		std::map<int, ContractState>& Seen()
		{
			static std::map<int, ContractState> seen;
			return seen;
		}
	}

	void Tick(UnitySpaceApp& app)
	{
		auto works = Works(app);
		if (works == nullptr) return;
		double jd = app.Clock->Jd;
		auto ship = Held(app);
		if (ship != nullptr)
		{
			works->Guard(ship, app.PlayerHelioKm(), jd);
			auto c = works->ActiveFor(ship);
			if (c != nullptr)
			{
				Seen()[c->Id] = c->State;
				if (auto w = works->DueWave(c, jd)) SpawnWave(app, works, c, w);
			}
			// The keys, at a helm by a giant: Home takes its contract, Shift+Home raids the nearest skimmer.
			auto menu = app.CheatMenuUi();
			if (menu == nullptr || !menu->IsOpen)
			{
				const auto& kb = Input::InputDevice::current();
				if (kb.present && kb[Input::Key::Home].wasPressedThisFrame)
				{
					bool shift = kb[Input::Key::LeftShift].isPressed || kb[Input::Key::RightShift].isPressed;
					cs::String said;
					if (shift) RaidHere(app, said);
					else TakeHere(app, said);
					if (!said.empty()) app.ShowToast(said);
				}
			}
		}
		// A toast as a contract the player was working ends (whoever holds the hull now).
		for (const auto& c : works->Contracts)
		{
			auto it = Seen().find(c->Id);
			if (it == Seen().end() || it->second == c->State) continue;
			it->second = c->State;
			if (c->State == ContractState::Completed)
				app.ShowToast(cs::Cat("CONTRACT DONE: ", c->Title(), ": ", Cr(c->Pay), " cr to the ", c->Taker != nullptr ? c->Taker->Name : cs::String("guard"),
					c->PilotName.empty() ? cs::String() : cs::Cat(" (", c->PilotName, ")")));
			else if (c->State == ContractState::Failed || c->State == ContractState::Abandoned)
				app.ShowToast(cs::Cat("CONTRACT ", c->State == ContractState::Failed ? "FAILED" : "ABANDONED", ": ", c->Title(), " (", c->Outcome, ")"));
		}
	}

	bool TakeHere(UnitySpaceApp& app, cs::String& said)
	{
		auto works = Works(app);
		auto ship = Held(app);
		if (works == nullptr || ship == nullptr) { said = "Take a helm first"; return false; }
		double jd = app.Clock->Jd;
		auto helio = app.PlayerHelioKm();
		if (auto active = works->ActiveFor(ship))
		{
			said = cs::Cat("Already on contract: ", active->Title(), ", orbit ", active->OrbitsDone(jd) + 1, " of ", active->Orbits);
			return false;
		}
		auto op = GiantHere(app, works, helio);
		if (op == nullptr) { said = "No gas works here: come to a gas giant"; return false; }
		auto c = works->OpenAt(op);
		if (c == nullptr) { said = cs::Cat("No protection contract posted at ", (*app.Catalog())[op->Body]->Name, " right now"); return false; }
		cs::String why;
		if (!works->Take(c, ship, helio, jd, why)) { said = cs::Cat("Can't take it: ", why); return false; }
		Seen()[c->Id] = c->State;
		said = cs::Cat("Contract taken: ", c->Title(), " for ", c->Patron != nullptr ? c->Patron->Name : cs::String("the works"), ", ", Cr(c->Pay),
			" cr. Stay at ", (*app.Catalog())[op->Body]->Name, "; raiders will come");
		return true;
	}

	bool StartContract(UnitySpaceApp& app, double firstWaveS, cs::String& said)
	{
		auto works = Works(app);
		auto ship = Held(app);
		if (works == nullptr || ship == nullptr) { said = "Take a helm first"; return false; }
		if (works->ActiveFor(ship) != nullptr) { said = "Already on a protection contract"; return false; }
		double jd = app.Clock->Jd;
		auto helio = app.PlayerHelioKm();
		// The nearest skimmer whose owner will hire this hull.
		auto diplomacy = app.World()->Diplomacy();
		cs::Ref<HarvestOperation> op;
		int platform = -1;
		double bestKm = cs::Double::PositiveInfinity;
		for (const auto& o : works->Operations)
		{
			if (o->Owner != nullptr && ship->Faction != nullptr && diplomacy != nullptr && diplomacy->Hostile(o->Owner, ship->Faction)) continue;
			for (int i = 0; i < o->Platforms.Count(); i++)
			{
				double km = (works->PlatformHelio(o, i, jd) - helio).Length();
				if (km < bestKm) { bestKm = km; op = o; platform = i; }
			}
		}
		if (op == nullptr) { said = "No gas works here would hire you"; return false; }
		if ((helio - app.Ephemeris()->Helio(op->Body)).Length() > works->GuardKm(op->Body))
		{
			app.CheatTeleportToStructure(op->Platforms[platform]->Structure);
			helio = app.PlayerHelioKm();
		}
		auto c = works->OpenAt(op, platform);
		if (c == nullptr) c = works->Offer(op, platform, jd);
		cs::String why;
		if (!works->Take(c, ship, helio, jd, why, firstWaveS)) { said = cs::Cat("Can't take it: ", why); return false; }
		Seen()[c->Id] = c->State;
		said = cs::Cat("Contract taken: ", c->Title(), " for ", c->Patron != nullptr ? c->Patron->Name : cs::String("the works"), ", ", Cr(c->Pay), " cr; the first wave in ",
			cs::Fmt(firstWaveS, "N0"), " s");
		return true;
	}

	bool WaveNow(UnitySpaceApp& app, cs::String& said)
	{
		auto works = Works(app);
		auto ship = Held(app);
		auto c = works != nullptr ? works->ActiveFor(ship) : nullptr;
		if (c == nullptr) { said = "No protection contract under way"; return false; }
		if (c->LiveWave() != nullptr) { said = "A wave is on you already"; return false; }
		auto w = c->NextWave();
		if (w == nullptr) { said = "No waves left in this contract"; return false; }
		w->Jd = app.Clock->Jd;
		SpawnWave(app, works, c, w);
		said = cs::String();
		return true;
	}

	bool RaidHere(UnitySpaceApp& app, cs::String& said)
	{
		auto works = Works(app);
		auto ship = Held(app);
		if (works == nullptr || ship == nullptr) { said = "Take a helm first"; return false; }
		double jd = app.Clock->Jd;
		cs::Ref<HarvestOperation> op;
		int platform = -1;
		double km = 0;
		if (!works->Nearest(app.PlayerHelioKm(), jd, op, platform, km) || km > RaidKm)
		{
			said = cs::Cat("No skimmer within ", cs::Fmt(RaidKm, "N0"), " km to raid");
			return false;
		}
		return works->Raid(op, platform, ship, jd, said) > 0;
	}

	// ---------------------------------------------------------------- the dev menu (US-T126)

	namespace
	{
		cs::String HelmBlocker(UnitySpaceApp& a)
		{
			auto b = a.CheatBlocker();
			if (!b.empty()) return b;
			return a.IsPiloting() ? cs::String() : cs::String("take a helm first");
		}

		void RegisterCheats()
		{
			using UI::CheatEntry;
			using UI::CheatRegistry;
			CheatRegistry::Add("Travel", CheatEntry::Menu("Go to a harvester", "a gas giant's skimmer platform, the hull flown (or the camera) beside it", [](UnitySpaceApp& a)
			{
				std::vector<CheatEntry> out;
				auto works = a.World() != nullptr ? a.World()->EnsureHarvest(a.Clock->Jd) : nullptr;
				if (works == nullptr) return out;
				double jd = a.Clock->Jd;
				for (const auto& op : works->Operations)
					for (int i = 0; i < op->Platforms.Count(); i++)
					{
						auto p = op->Platforms[i];
						auto st = p->Structure;
						cs::String detail = cs::Cat(op->Owner != nullptr ? op->Owner->Name : cs::String("unowned"), ", ", cs::Fmt(op->OrbitDays(i) * 24, "N1"), " h orbit, tanks ",
							cs::Fmt(p->TankTonnes(), "N0"), " t", p->Down(jd) ? "  DOWN" : "");
						out.push_back(CheatEntry::Action(p->Name, detail, [st](UnitySpaceApp& app) { app.CheatTeleportToStructure(st); }));
					}
				return out;
			}).WithBlocker([](UnitySpaceApp& a) { return a.CheatBlocker(); }));
			CheatRegistry::Add("Combat", CheatEntry::Action("Start a protection contract", "guard the nearest gas skimmer for some orbits (taken to it if you are not at its giant); the first wave in 20 s",
				[](UnitySpaceApp& a)
				{
					cs::String said;
					StartContract(a, 20, said);
					if (!said.empty()) a.ShowToast(said);
				}).WithKeys("Home").WithBlocker(HelmBlocker));
			CheatRegistry::Add("Combat", CheatEntry::Action("Harvester wave now", "the protection contract's next wave of raiders, ghosts, warships or Bio Mass, now", [](UnitySpaceApp& a)
			{
				cs::String said;
				WaveNow(a, said);
				if (!said.empty()) a.ShowToast(said);
			}).WithBlocker([](UnitySpaceApp& a)
			{
				auto b = HelmBlocker(a);
				if (!b.empty()) return b;
				auto works = Works(a);
				return works != nullptr && works->ActiveFor(a.Piloted.Ship) != nullptr ? cs::String() : cs::String("no protection contract under way");
			}));
			CheatRegistry::Add("Combat", CheatEntry::Action("Abandon the protection contract", "give it up: no pay, and the works' owner remembers", [](UnitySpaceApp& a)
			{
				auto works = Works(a);
				auto c = works != nullptr ? works->ActiveFor(a.Piloted.Ship) : nullptr;
				if (c != nullptr) works->Abandon(c, a.Clock->Jd);
			}).WithBlocker([](UnitySpaceApp& a)
			{
				auto works = Works(a);
				return a.IsPiloting() && works != nullptr && works->ActiveFor(a.Piloted.Ship) != nullptr ? cs::String() : cs::String("no protection contract under way");
			}));
			CheatRegistry::Add("Combat", CheatEntry::Action("Raid a harvester", "siphon the nearest gas skimmer's tanks into your hold (piracy)", [](UnitySpaceApp& a)
			{
				cs::String said;
				RaidHere(a, said);
				if (!said.empty()) a.ShowToast(said);
			}).WithKeys("Shift+Home").WithBlocker(HelmBlocker));
		}

		UI::CheatRegistration registration(&RegisterCheats);

		// ---------------------------------------------------------------- the showcase tour's stop (US-T147)

		void RegisterTour()
		{
			Tour::TourRegistry::Add(Tour::TourStop::Make("Places", "gas-giant-harvesters", "Gas giant harvesters",
				"Jupiter's gas works: a skimmer platform in low orbit, its plume of scooped gas down to the clouds, a tanker astern")
				.At(90)
				.WithSetup([](UnitySpaceApp& app) -> cs::String
				{
					auto works = app.World() != nullptr ? app.World()->EnsureHarvest(app.Clock->Jd) : nullptr;
					if (works == nullptr || works->Operations.Count() == 0) return "no gas giant here";
					app.Clock->WarpIndex = 0; // real time: the platforms race round in low orbit
					return cs::String();
				})
				.WithCamera([](UnitySpaceApp& app, double t)
				{
					auto works = Works(app);
					if (works == nullptr || works->Operations.Count() == 0) return;
					auto op = works->Operations[0];
					for (const auto& o : works->Operations)
						if ((*app.Catalog())[o->Body]->Name == "Jupiter") op = o;
					auto subject = Views::HarvesterView::PlatformScene(&app, op, 0);
					double size = Views::HarvesterView::PlatformSize(&app, op, 0);
					auto out = subject - app.BodyScene(op->Body);
					out = out.LengthSquared() > 1e-18 ? out.Normalized() : DVec3::UnitY;
					auto side = DVec3::Cross(out, DVec3::UnitY);
					side = side.LengthSquared() > 1e-18 ? side.Normalized() : DVec3::UnitX;
					// Above and beside the platform, looking down past it at the clouds its plume runs into.
					double d = size * (7 + t * 0.15);
					auto eye = subject + out * (d * 0.8) + side * d;
					auto look = (subject - out * (size * 2) - eye).Normalized();
					if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
					app.SpaceCamera->SetPose(eye, Quaternion::LookRotation(Vector3(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z)), Vector3::up()));
				})
				.For(9).AtBothScales());
		}

		Tour::TourRegistration tourRegistration(&RegisterTour);
	}

	// ---------------------------------------------------------------- sg.Harvest

	namespace
	{
		UnitySpaceApp* RunningApp()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& c : GEngine->GetWorldContexts())
			{
				if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
				auto* actor = AUnitySpaceAppActor::Find(c.World());
				if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
			}
			return nullptr;
		}

		/// <summary>One run of the command, stepped once a frame (each step returns true when done), as sg.BioMass's.</summary>
		struct Run
		{
			using Step = std::function<bool(UnitySpaceApp&, double)>;
			std::vector<std::pair<FString, Step>> steps;
			size_t at = 0;
			double stepStart = -1;
			double started = FPlatformTime::Seconds();
			bool askedNewGame = false;
			bool quit = false;

			bool Tick()
			{
				auto* app = RunningApp();
				double now = FPlatformTime::Seconds();
				if (app == nullptr)
				{
					if (!askedNewGame && now - started > 2.0)
					{
						askedNewGame = true;
						UE_LOG(LogSpaceGod, Display, TEXT("sg.Harvest: no game running; starting a new one"));
						GameLaunch::NewGame();
					}
					if (now - started > 180.0) return Finish(TEXT("no game came up"));
					return true;
				}
				if (at >= steps.size()) return Finish(nullptr);
				if (stepStart < 0) stepStart = now;
				if (steps[at].second(*app, now - stepStart))
				{
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Harvest: %s"), *steps[at].first);
					at++;
					stepStart = -1;
				}
				else if (now - stepStart > 240.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
				return true;
			}

			bool Finish(const TCHAR* failure)
			{
				if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Harvest: %s"), failure); }
				else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Harvest: done")); }
				if (quit) FPlatformMisc::RequestExit(false);
				return false;
			}
		};

		cs::Ref<HarvestOperation> Giant(UnitySpaceApp& app, const FString& name)
		{
			auto works = app.World() != nullptr ? app.World()->EnsureHarvest(app.Clock->Jd) : nullptr;
			if (works == nullptr || works->Operations.Count() == 0) return nullptr;
			for (const auto& op : works->Operations)
				if (FString(UTF8_TO_TCHAR((*app.Catalog())[op->Body]->Name.c_str())).Equals(name, ESearchCase::IgnoreCase)) return op;
			return works->Operations[0];
		}

		void Say(const cs::String& s) { UE_LOG(LogSpaceGod, Display, TEXT("sg.Harvest: %s"), UTF8_TO_TCHAR(s.c_str())); }

		void Start(const TArray<FString>& args)
		{
			FString verb = args.Num() > 0 ? args[0].ToLower() : FString(TEXT("goto"));
			FString giant = TEXT("Jupiter"), shots;
			bool quit = false;
			for (int i = 1; i < args.Num(); i++)
			{
				if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
				else if (verb == TEXT("shots") && shots.IsEmpty()) shots = args[i];
				else giant = args[i];
			}
			auto run = std::make_shared<Run>();
			run->quit = quit;
			auto wait = [](double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; };
			auto say = [](std::function<bool(UnitySpaceApp&, cs::String&)> f)
			{
				return [f](UnitySpaceApp& app, double)
				{
					cs::String said;
					f(app, said);
					if (!said.empty()) { Say(said); app.ShowToast(said); }
					return true;
				};
			};
			auto toGiant = [giant](UnitySpaceApp& app, double)
			{
				auto op = Giant(app, giant);
				if (op == nullptr) throw std::runtime_error("no gas giant in this system");
				app.CheatTeleportToStructure(op->Platforms[0]->Structure);
				return true;
			};
			if (verb == TEXT("goto")) run->steps.push_back({ TEXT("at the skimmer"), toGiant });
			else if (verb == TEXT("contract")) run->steps.push_back({ TEXT("contract"), say([](UnitySpaceApp& a, cs::String& s) { return StartContract(a, 20, s); }) });
			else if (verb == TEXT("wave")) run->steps.push_back({ TEXT("wave"), say(&WaveNow) });
			else if (verb == TEXT("raid")) run->steps.push_back({ TEXT("raid"), say(&RaidHere) });
			else if (verb == TEXT("report"))
				run->steps.push_back({ TEXT("report"), [](UnitySpaceApp& app, double)
				{
					auto works = app.World() != nullptr ? app.World()->EnsureHarvest(app.Clock->Jd) : nullptr;
					if (works == nullptr) return true;
					for (const auto& op : works->Operations)
						Say(cs::Cat(op->Name, ": ", op->Platforms.Count(), " skimmers (", op->Working(app.Clock->Jd), " working), ", op->Tankers.Count(), " tankers, owner ",
							op->Owner != nullptr ? op->Owner->Name : cs::String("none"), ", port ", op->Port != nullptr ? op->Port->Name : cs::String("none"),
							"; delivered ", cs::Fmt(op->Delivered[static_cast<int>(Good::HeliumThree)], "N0"), " t He-3 / ",
							cs::Fmt(op->Delivered[static_cast<int>(Good::Hydrocarbons)], "N0"), " t hydrocarbons in ", op->Deliveries, " runs; raids ", op->Raids,
							", fuel lost ", cs::Fmt(op->FuelLost, "N0"), " t"));
					return true;
				} });
			else if (verb == TEXT("shots"))
			{
				if (shots.IsEmpty()) shots = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Harvest"));
				auto shot = [](FString path) { return [path](UnitySpaceApp&, double) { FScreenshotRequest::RequestScreenshot(path, false, false); return true; }; };
				auto scaleTo = [](double goal) { return [goal](UnitySpaceApp& app, double) { if (app.ScaleGoal != goal) app.ToggleScale(); return !app.ScaleAnimating(); }; };
				auto paused = [](bool on) { return [on](UnitySpaceApp& app, double) { app.Clock->SetPaused(on); return true; }; };
				run->steps.push_back({ TEXT("game up"), wait(4.0) });
				run->steps.push_back({ TEXT("game scale"), scaleTo(1.0) });
				run->steps.push_back({ TEXT("camera to the giant"), [giant](UnitySpaceApp& app, double)
				{
					auto op = Giant(app, giant);
					if (op == nullptr) throw std::runtime_error("no gas giant in this system");
					app.Clock->WarpIndex = 0;
					app.CheatTeleportToBody(op->Body);
					return true;
				} });
				run->steps.push_back({ TEXT("camera settled"), wait(2.5) });
				run->steps.push_back({ TEXT("a helm"), [](UnitySpaceApp& app, double) { if (!app.IsPiloting()) app.SpawnPracticeFighter(); return true; } });
				run->steps.push_back({ TEXT("helm up"), wait(2.0) });
				run->steps.push_back({ TEXT("beside the skimmer"), toGiant });
				run->steps.push_back({ TEXT("paused"), paused(true) });
				run->steps.push_back({ TEXT("chase camera settled"), wait(3.0) });
				run->steps.push_back({ TEXT("shot game"), shot(FPaths::Combine(shots, TEXT("harvester-jupiter-game.png"))) });
				run->steps.push_back({ TEXT("written"), wait(1.5) });
				run->steps.push_back({ TEXT("realistic scale"), scaleTo(0.0) });
				run->steps.push_back({ TEXT("settled"), wait(2.5) });
				run->steps.push_back({ TEXT("shot real"), shot(FPaths::Combine(shots, TEXT("harvester-jupiter-real.png"))) });
				run->steps.push_back({ TEXT("written"), wait(1.5) });
				run->steps.push_back({ TEXT("game scale again"), scaleTo(1.0) });
				run->steps.push_back({ TEXT("running"), paused(false) });
				run->steps.push_back({ TEXT("back beside the skimmer"), toGiant });
				run->steps.push_back({ TEXT("contract, first wave in 2 s"), say([](UnitySpaceApp& a, cs::String& s) { return StartContract(a, 2, s); }) });
				run->steps.push_back({ TEXT("wave on the scene"), [](UnitySpaceApp& app, double t)
				{
					auto works = Works(app);
					auto c = works != nullptr ? works->ActiveFor(app.Piloted.Ship) : nullptr;
					return t > 12 || (c != nullptr && c->LiveWave() != nullptr);
				} });
				run->steps.push_back({ TEXT("fighting"), wait(4.0) });
				run->steps.push_back({ TEXT("paused"), paused(true) });
				run->steps.push_back({ TEXT("held"), wait(1.0) });
				run->steps.push_back({ TEXT("shot fight"), shot(FPaths::Combine(shots, TEXT("protection-fight-game.png"))) });
				run->steps.push_back({ TEXT("written"), wait(1.5) });
				run->steps.push_back({ TEXT("running"), paused(false) });
				run->steps.push_back({ TEXT("fighting on"), wait(6.0) });
				run->steps.push_back({ TEXT("paused"), paused(true) });
				run->steps.push_back({ TEXT("held"), wait(1.0) });
				run->steps.push_back({ TEXT("shot fight later"), shot(FPaths::Combine(shots, TEXT("protection-fight-game-2.png"))) });
				run->steps.push_back({ TEXT("written"), wait(1.5) });
				run->steps.push_back({ TEXT("running"), paused(false) });
			}
			else { Say("usage: sg.Harvest goto [giant] | contract | wave | raid | report | shots <dir> [quit]"); return; }
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
			{
				try { return run->Tick(); }
				catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
			}));
		}

		/// US-T146: <c>sg.Harvest goto [giant]</c> takes the hull flown (or the camera) to a giant's first skimmer;
		/// <c>contract</c> starts a protection contract at the nearest (first wave in 20 s); <c>wave</c> brings its next wave;
		/// <c>raid</c> raids the nearest skimmer; <c>report</c> logs every works; <c>shots &lt;dir&gt; [quit]</c> takes a
		/// harvester at Jupiter at both scales and a protection fight.
		FAutoConsoleCommand GHarvestCommand(
			TEXT("sg.Harvest"),
			TEXT("US-T146: the gas giant works: sg.Harvest goto [giant] | contract | wave | raid | report | shots <dir> [quit]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
	}
}
