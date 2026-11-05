// US-T44: CoopSmoke.cs (US-T39 + US-T43) in C++; see CoopSmoke.h. The same steps at the same replayed frames, the same
// log lines: the client joins, presses '.' and '3', takes a station's seat through the picker, keeps it through a
// deliberate resync, flies a freighter's helm (on every peer, on its input stream), trades at Earth, spawns a pirate
// carrier to start a fight and takes a fighter's cockpit there (its keys as commands), takes the party to another star
// and has the host load its quicksave.

#include "Coop/CoopSmoke.h"

#include "Coop/CoopPeer.h"
#include "Coop/CoopSettings.h"
#include "Camera/SpaceCamera.h"
#include "Smoke/ScriptedDevice.h"
#include "UnitySpaceApp.h"
#include "UI/Hud.h"

#include "Combat/Battle.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"
#include "Fleet.h"
#include "Persistence/SaveGame.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h" // US-T47
#include "Systems/Statecraft.h"
#include "Systems/StrategicCommand.h"
#include "Target.h"
#include "UI/GalaxyMapScreen.h"
#include "World.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include <memory>

using namespace SpaceGod::Core;

namespace SpaceGod::Coop::CoopSmoke
{
	namespace
	{
		constexpr double TimeoutSeconds = 9 * 60;

		double Since()
		{
			static const double start = FPlatformTime::Seconds();
			return FPlatformTime::Seconds() - start;
		}

		std::unique_ptr<Smoke::ScriptedDevice> script;
		long long byeAt = -1;
		double byeSeconds = 0; // US-T47
		long long replayed = 0;
		int quitIn = -1;
		bool finished = false, hostSaved = false;
		cs::String warpBefore;
		long long fighterFrom = -1, travelFrom = -1, loadFrom = -1;
		cs::Ref<FighterSeat> fighter;
		bool fighterHeld = false; // US-T68
		int fighterRetakes = 0;
		cs::Ref<Faction> speaker; // US-T47
		cs::Ref<Ship> hailed;
		cs::String productionBefore;
		int objectivesBefore = 0, recordBefore = 0;
		double treasuryBefore = 0;
		UnitySpaceApp* lastApp = nullptr;
		cs::Ref<StarSystem> travelTo; // US-T64

		// US-T64: a step that needs an earlier ask carried out waits for it to come back in a host frame. A client's ask
		// reaches the host while the host is as far ahead as the client's backlog, so it comes back only once the client
		// has replayed that far: on a client that replays more slowly than the host makes frames (the C++ client under
		// load, its backlog growing to hundreds of frames), a fixed number of frames is no answer. The script's frame
		// count stands still while it waits (as at 729 for the freighter's helm), so every later step keeps its place
		// after the one it follows; a wait gives up after MaxWait replayed frames and the step goes on (its check then
		// fails, with a warning naming the wait).
		constexpr int MaxWait = 3600;
		int waited = 0;
		long long waitingAt = -1;
		int mostBehind = 0; // the client's largest backlog, for the done line

		/// US-T64: where the camera is (the spawns are at it): a packaged client's went NaN on its first focus (Cs.h IsNaNFast).
		cs::String CameraText(UnitySpaceApp& app)
		{
			auto p = app.SpaceCamera->ScenePosition;
			auto f = app.SpaceCamera->FocusTarget;
			return cs::Cat("camera scene (", cs::Fmt(p.X, "G6"), ", ", cs::Fmt(p.Y, "G6"), ", ", cs::Fmt(p.Z, "G6"), ") ", Cameras::ToString(app.SpaceCamera->Mode), " on ",
				app.CoopTargetKey(f), ", selected ", app.CoopTargetKey(app.Selected));
		}

		/// True while the script waits at step `at` for `ready`: the step is held.
		bool Waits(long long at, bool ready, const char* what, CoopPeer& peer)
		{
			if (ready || replayed + 1 != at)
			{
				if (waitingAt == at && waited > 0)
					CoopPeer::Log(cs::Cat("smoke: step ", at, " waited ", waited, " frames for ", what, " (to frame ", peer.Frame(), ")"));
				if (waitingAt == at) { waitingAt = -1; waited = 0; }
				return false;
			}
			if (waitingAt != at) { waitingAt = at; waited = 0; }
			if (++waited <= MaxWait) return true;
			CoopPeer::Warn(cs::Cat("smoke: step ", at, " gave up waiting for ", what, " after ", MaxWait, " frames (frame ", peer.Frame(), ", backlog ", peer.Backlog(), ")"));
			waitingAt = -1;
			waited = 0;
			return false;
		}

		void Finish(const cs::String& why, CoopPeer& peer, UnitySpaceApp* app)
		{
			if (finished) return;
			finished = true;
			CoopPeer::Log(cs::Cat("smoke (", Role(), ") done: ", why, "; frame ", peer.Frame(), ", ",
				peer.IsHost() ? cs::Cat(peer.DigestMatches, " digest matches, ", peer.Desyncs, " desyncs") : cs::Cat(peer.ClockCorrections, " clock corrections, at most ", mostBehind, " frames behind the host"),
				app != nullptr ? cs::Cat(", ", app->CoopPilotFrames, " helm frames made here, ", peer.PilotSent, " helm inputs sent, ", app->CoopCockpitKeys, " cockpit keys, ",
					app->CoopActions, " actions, ", peer.Retakes, " seats kept through syncs, ", CoopSettings::Applied(), " settings taken") : cs::String(),
				", ", cs::Fmt(Since(), "F0"), " s; quitting"));
			if (GEngine)
				for (const FWorldContext& c : GEngine->GetWorldContexts())
					if (c.World() && c.World()->IsGameWorld())
					{
						GEngine->Exec(c.World(), TEXT("quit"));
						return;
					}
		}
	}

	const cs::String& Role()
	{
		static const cs::String role = []
		{
			Since(); // the clock starts with the process's first look at the run
			FString v;
			if (!FParse::Value(FCommandLine::Get(), TEXT("-SpaceGodCoopSmoke="), v)) return cs::String();
			return cs::String(TCHAR_TO_UTF8(*v.ToLower()));
		}();
		return role;
	}

	bool Active() { return !Role().empty(); }

	Smoke::ScriptedDevice* Script() { return script.get(); }

	void UseScript()
	{
		if (!script) script = std::make_unique<Smoke::ScriptedDevice>();
	}

	void HostFrame(CoopPeer& peer, UnitySpaceApp* app, const FrameRecord& rec)
	{
		if (Role() != "host" || finished) return;
		// The host's quicksave, early (the client asks for it to be loaded near the end).
		if (!hostSaved && app != nullptr && peer.DigestMatches >= 2)
		{
			hostSaved = true;
			bool saved = app->SaveTo(SaveSlots::Quick, "Quicksave");
			CoopPeer::Log(cs::Cat("smoke: host quicksaves at frame ", rec.Frame, ": ", saved ? "True" : "False"));
		}
		if (peer.AnyBye() && byeAt < 0) { byeAt = rec.Frame; byeSeconds = Since(); }
		// US-T47: and 5 s, so the client (30 of its own ticks, slower than the host's frames when catching up) quits first:
		// a client whose host has gone goes back to the start map, which is no end for the smoke.
		if (byeAt >= 0 && rec.Frame >= byeAt + 30 && Since() - byeSeconds >= 5) Finish("the client said bye", peer, app);
		else if (Since() > TimeoutSeconds) Finish("TIMEOUT", peer, app);
	}

	void ClientTick(CoopPeer& peer)
	{
		if (Role() != "client" || finished) return;
		if (quitIn > 0 && --quitIn == 0) Finish("said bye", peer, lastApp);
		else if (Since() > TimeoutSeconds) Finish("TIMEOUT", peer, lastApp);
	}

	void ClientFrame(CoopPeer& peer, UnitySpaceApp& app)
	{
		lastApp = &app;
		if (Role() != "client" || finished || !script) return;
		if (peer.Backlog() > mostBehind) mostBehind = peer.Backlog();
		// The flying (from 730 on: W, X, F) waits until the spawned freighter's helm has come back in a host frame: the C++
		// client replays the host's backlog more slowly than the C# one, so the helm can still be in flight at 730, and X
		// (roll at the helm) would then open the news panel in command (InputState: X is also ToggleNews), which keeps the
		// cockpit's keys off later on. The C# run reaches the helm a frame before its X; the steps after are unchanged.
		if (replayed == 729 && app.Piloted.IsNone()) return;
		// US-T64: the other steps that follow an ask (see Waits): the selection (applied with the frames) before the camera
		// goes to it; the seat held before the resync that must keep it, and handed back before Earth is picked again; the freighter's helm handed back before the carrier; the carrier's helm held
		// before it goes to its crew; the fighter's cockpit held before its keys, and handed back before the trip; the
		// party arrived before the galaxy map's screens; the speaker carried before the orders given as it; the orders and
		// the gift carried before they are checked, and so before the load (whose sync would skip the frames that carry
		// them on this client).
		auto strategy = app.Galaxy->Polities->Command;
		bool bodySelected = Wire::StartsWith(app.CoopTargetKey(app.Selected), "b ");
		if (Waits(200, bodySelected, "the selection", peer)) return;
		if (Waits(480, app.Seat() != nullptr, "the station's seat", peer)) return;
		if (Waits(670, app.Seat() == nullptr, "the station's seat handed back", peer)) return;
		if (Waits(680, bodySelected, "the selection", peer)) return;
		if (Waits(1040, app.Piloted.IsNone(), "the freighter's helm handed back", peer)) return;
		if (Waits(1340, app.Piloted.Ship != nullptr, "the carrier's helm", peer)) return;
		// US-T68 (CoopSmoke.cs's US-T67): a fighter shot down under the client before its keys (a Shipping run: 44 frames
		// after the take) leaves no cockpit to wait for; the client takes another, a few times at most.
		if (fighter != nullptr && replayed < fighterFrom + 100)
		{
			if (cs::Is<FighterSeat>(app.Seat())) fighterHeld = true;
			else if (fighterHeld && fighterRetakes < 3)
			{
				CoopPeer::Log(cs::Cat("smoke: client lost the cockpit of ", fighter->Name(), " before its keys at frame ", peer.Frame(), "; takes another"));
				fighter = nullptr;
				fighterHeld = false;
				fighterRetakes++;
				fighterFrom = replayed + 1;
			}
		}
		if (fighter != nullptr && Waits(fighterFrom + 60, cs::Is<FighterSeat>(app.Seat()), "the fighter's cockpit", peer)) return;
		if (Waits(1760, app.Seat() == nullptr, "the fighter's cockpit handed back", peer)) return;
		if (Waits(2050, travelTo == nullptr || app.ActiveSystem == travelTo, "the party's trip", peer)) return;
		if (Waits(2080, speaker == nullptr || strategy->Player == speaker, "the speaker", peer)) return;
		if (speaker != nullptr && hailed != nullptr)
		{
			auto directives = strategy->Find(speaker);
			bool carried = directives != nullptr && directives->Production->Name != productionBefore && app.Galaxy->Polities->Statecraft->Record().Count() > recordBefore;
			if (Waits(2320, carried, "the orders and the gift", peer)) return;
		}
		long long n = ++replayed;
		// The stick at the helm: throttle up, the mouse swinging the nose, a roll.
		if (n >= 740 && n < 800) script->mouseDelta = Vector2(4.0f, 2.0f);
		switch (n)
		{
			case 30:
				warpBefore = app.Clock->Warp().Label;
				script->Tap(Input::Key::Period);
				CoopPeer::Log(cs::Cat("smoke: client presses '.' (time warp up) at frame ", peer.Frame(), ", warp ", warpBefore));
				break;
			case 180:
				script->Tap(Input::Key::Digit3);
				CoopPeer::Log(cs::Cat("smoke: client presses '3' (select) at frame ", peer.Frame()));
				break;
			case 200:
				app.FocusSelected();
				break;
			case 300:
				CoopPeer::Log(cs::Cat("smoke: client's warp ", warpBefore, " -> ", app.Clock->Warp().Label, " (the host's clock) at frame ", peer.Frame()));
				break;
			case 330:
			{
				auto me = peer.Players[peer.LocalSeat()];
				CoopPeer::Log(cs::Cat("smoke: client selected ", app.CoopTargetName(app.CoopTargetKey(app.Selected)), "; the host's roster has it as '",
					me != nullptr ? me->Selection : cs::String(), "'"));
				break;
			}
			case 360:
			{
				auto stations = Embodiables::StationsIn(app.World());
				cs::Ref<IEmbodiable> seat = stations.Count() > 0 ? cs::Ref<IEmbodiable>(stations[0]) : nullptr;
				if (seat == nullptr)
					for (const auto& b : app.World()->War->Battles)
					{
						for (const auto& s : Embodiables::SeatsIn(app.World(), b))
							if (!cs::Is<ShipHelm>(s)) { seat = s; break; }
						if (seat != nullptr) break;
					}
				if (seat == nullptr)
				{
					CoopPeer::Warn(cs::Cat("smoke: no seat to take in ", app.ActiveSystem->Name));
					break;
				}
				CoopPeer::Log(cs::Cat("smoke: client picks seat '", seat->Name(), "' (key ", app.CoopSeatKey(seat), ") at frame ", peer.Frame()));
				app.PickSeat(seat);
				break;
			}
			case 480:
			{
				auto held = app.Seat();
				CoopPeer::Log(cs::Cat("smoke: client holds ", held != nullptr ? held->Name() : cs::String("nothing"), " and asks for a resync at frame ", peer.Frame(),
					" (a deliberate one: the seat must be kept)"));
				peer.RequestResync("a deliberate resync (smoke)");
				break;
			}
			case 600:
			{
				auto held = app.Seat();
				CoopPeer::Log(cs::Cat("smoke: after the resync the client holds ", held != nullptr ? held->Name() : cs::String("nothing"), " at frame ", peer.Frame(),
					", warp ", app.Clock->Warp().Label));
				break;
			}
			case 640:
				script->Tap(Input::Key::P); // back to command: the seat goes back to its AI (a release command)
				break;
			case 670:
				script->Tap(Input::Key::Digit3); // Earth again, and the camera on it (the spawns below are at the camera)
				break;
			case 680:
				app.FocusSelected();
				break;
			case 700:
				CoopPeer::Log(cs::Cat("smoke: client spawns a freighter at its camera (the spawn grid's call) at frame ", peer.Frame(), "; ", CameraText(app)));
				app.SpawnShip(ShipClass::Freighter, false);
				break;
			case 730:
				CoopPeer::Log(cs::Cat("smoke: client at the helm of ", app.Piloted.Ship != nullptr ? app.Piloted.Ship->Name : cs::String("nothing"), " (helm seat ",
					app.CoopHelmSeat(), ") at frame ", peer.Frame(), "; flies it"));
				script->Hold(Input::Key::W);
				break;
			case 760: script->Hold(Input::Key::X); break;
			case 780: script->Release(Input::Key::X); break;
			case 820:
				script->Tap(Input::Key::F); // flight assist off, in the pilot loop on every peer
				break;
			case 900:
				script->Release(Input::Key::W);
				CoopPeer::Log(cs::Cat("smoke: client's helm at frame ", peer.Frame(), ": ", cs::Fmt(app.PilotSpeedKmS(), "0.000"), " km/s, flight assist ",
					app.PilotFlightAssist() ? "on" : "off"));
				break;
			case 920:
			{
				cs::Ref<Settlement> market;
				for (const auto& s : app.Economy()->Settlements)
					if (s->Discovered() && app.CanTradeNow(s)) { market = s; break; }
				if (market == nullptr)
				{
					CoopPeer::Warn(cs::Cat("smoke: no market in reach of the helm at frame ", peer.Frame()));
					break;
				}
				Good good = Good::Water;
				for (int g = 0; g < GoodInfo::Count; g++)
					if (market->Stock[g] > 50) { good = static_cast<Good>(g); break; }
				CoopPeer::Log(cs::Cat("smoke: client buys 1 crate of ", GoodInfo::Of(good)->Name, " at ", market->Name, " (the market board's Buy) at frame ", peer.Frame()));
				app.CoopHud()->CoopSmokeBuy(market, good);
				break;
			}
			case 1000:
				CoopPeer::Log(cs::Cat("smoke: client's hold: ", app.Piloted.Ship != nullptr ? app.Piloted.Ship->Cargo : cs::String("?"), "; releases the helm at frame ", peer.Frame()));
				script->Tap(Input::Key::P);
				break;
			case 1040:
				CoopPeer::Log(cs::Cat("smoke: client spawns a pirate carrier at its camera to start a fight at frame ", peer.Frame()));
				app.SpawnShip(ShipClass::Carrier, true);
				break;
			case 1340:
			{
				// Its crew takes it back (a carrier's AI launches its squadrons in a fight); the cockpits are then there to take.
				auto carrier = app.Piloted.Ship;
				CoopPeer::Log(cs::Cat("smoke: client's carrier ", carrier != nullptr ? carrier->Name : cs::String("?"), " is ",
					carrier != nullptr && carrier->Battle != nullptr ? cs::Cat("in ", carrier->Battle->Name) : cs::String("in no fight"), "; ",
					app.World()->War->Battles.Count(), " battles in ", app.ActiveSystem->Name, "; hands the helm to its crew at frame ", peer.Frame()));
				script->Tap(Input::Key::P);
				fighterFrom = n + 20;
				break;
			}
			case 1700:
				if (fighter == nullptr) CoopPeer::Warn(cs::Cat("smoke: no fighter aloft to take by frame ", peer.Frame()));
				script->Tap(Input::Key::P);
				break;
			case 1760:
			{
				cs::Ref<StarSystem> to;
				for (const auto& s : app.Galaxy->Systems)
					if (s != app.ActiveSystem && !s->IsVoidBase) { to = s; break; }
				CoopPeer::Log(cs::Cat("smoke: client asks for the party's trip to ", to != nullptr ? to->Name : cs::String(), " at frame ", peer.Frame(), " (from ",
					app.ActiveSystem->Name, ")"));
				if (to != nullptr) app.TravelTo(to);
				travelFrom = n;
				travelTo = to;
				break;
			}
			// US-T47: the galaxy map's strategy and envoy screens and a hail (F12), from the client.
			case 2050:
			{
				cs::Ref<Ship> other;
				for (const auto& s : app.World()->Fleet->Ships())
					if (!s->Destroyed && Statecraft::Party(s->Faction)) { other = s; break; }
				speaker = nullptr;
				for (const auto& f : app.Galaxy->Polities->Factions())
					if (Statecraft::Party(f) && (other == nullptr || f != other->Faction)) { speaker = f; break; }
				hailed = other;
				CoopPeer::Log(cs::Cat("smoke: client speaks for the ", speaker != nullptr ? speaker->Name : cs::String(), " (the COMMAND panel's arrows) at frame ", peer.Frame()));
				if (speaker != nullptr) app.CoopHud()->CoopMap()->CoopSmokeSpeak(speaker);
				break;
			}
			case 2080:
			{
				if (speaker == nullptr) break;
				auto d = app.Galaxy->Polities->Command->Find(speaker);
				productionBefore = d != nullptr ? d->Production->Name : ProductionFocus::Balanced()->Name;
				CoopPeer::Log(cs::Cat("smoke: client clicks Production (", productionBefore, ") at frame ", peer.Frame()));
				app.CoopHud()->CoopMap()->CoopSmokeProduction();
				break;
			}
			case 2110:
				if (speaker == nullptr) break;
				objectivesBefore = app.Galaxy->Polities->Command->ObjectiveCount();
				CoopPeer::Log(cs::Cat("smoke: client orders the ", speaker->Name, "'s navy to EXPLORE ", app.ActiveSystem->Name, " at frame ", peer.Frame()));
				app.CoopHud()->CoopMap()->CoopSmokeExplore(app.ActiveSystem);
				break;
			case 2160:
				if (hailed == nullptr)
				{
					CoopPeer::Warn(cs::Cat("smoke: no ship of a state to hail in ", app.ActiveSystem->Name));
					break;
				}
				app.Select(Target::OfShip(hailed));
				break;
			case 2170:
				if (hailed == nullptr) break;
				CoopPeer::Log(cs::Cat("smoke: client selects ", hailed->Name, " (", hailed->Faction->Name, ") and presses F12 (hail) at frame ", peer.Frame()));
				script->Tap(Input::Key::F12);
				break;
			case 2240:
				if (hailed == nullptr || speaker == nullptr) break;
				recordBefore = app.Galaxy->Polities->Statecraft->Record().Count();
				treasuryBefore = hailed->Faction->Treasury;
				CoopPeer::Log(cs::Cat("smoke: client sends the ", hailed->Faction->Name, " a gift (the envoy screen's button) at frame ", peer.Frame(), "; envoy open: ",
					app.ShowGalaxyMap ? "True" : "False"));
				app.CoopHud()->CoopMap()->CoopSmokeGift(hailed->Faction);
				break;
			case 2320:
			{
				if (speaker == nullptr || hailed == nullptr) break;
				const auto& command = app.Galaxy->Polities->Command;
				auto d = command->Find(speaker);
				cs::String production = d != nullptr ? d->Production->Name : cs::String("-");
				int record = app.Galaxy->Polities->Statecraft->Record().Count();
				bool ok = command->Player == speaker && production != productionBefore && record > recordBefore;
				CoopPeer::Log(cs::Cat("smoke: statecraft at frame ", peer.Frame(), ": speaker ", command->Player != nullptr ? command->Player->Name : cs::String(), ", production ",
					productionBefore, " -> ", production, ", objectives ", objectivesBefore, " -> ", command->ObjectiveCount(), ", record ", recordBefore, " -> ", record,
					", the ", hailed->Faction->Name, "'s treasury ", cs::Fmt(treasuryBefore, "N0"), " -> ", cs::Fmt(hailed->Faction->Treasury, "N0"), "; carried out: ", ok ? "True" : "False"));
				app.ShowGalaxyMap = false;
				break;
			}
			case 2500:
				CoopPeer::Log(cs::Cat("smoke: client is in ", app.ActiveSystem->Name, " at frame ", peer.Frame(), "; asks the host to load its quicksave"));
				app.LoadFrom(SaveSlots::Quick);
				loadFrom = n;
				break;
			case 2800:
				CoopPeer::Log(cs::Cat("smoke: after the load the client is in ", app.ActiveSystem->Name, " at frame ", peer.Frame()));
				break;
			case ClientFrames:
				peer.SayBye();
				quitIn = 30;
				break;
			default:
				break;
		}
		// The fight: once a fighter is aloft, take its cockpit, then its keys (power to weapons, flight assist, a flare).
		if (fighterFrom >= 0 && n >= fighterFrom && fighter == nullptr && n % 20 == 0)
		{
			for (const auto& b : app.World()->War->Battles)
			{
				for (const auto& s : Embodiables::SeatsIn(app.World(), b))
					if (auto f = cs::As<FighterSeat>(s)) { fighter = f; break; }
				if (fighter != nullptr) break;
			}
			if (fighter != nullptr)
			{
				CoopPeer::Log(cs::Cat("smoke: client takes the cockpit of ", fighter->Name(), " (key ", app.CoopSeatKey(fighter), ") at frame ", peer.Frame()));
				app.PickSeat(fighter); // hands the carrier's helm back first (a release), then the take
				fighterFrom = n;
			}
		}
		else if (fighter != nullptr && n == fighterFrom + 60)
		{
			auto held = app.Seat();
			CoopPeer::Log(cs::Cat("smoke: client in ", held != nullptr ? held->Name() : cs::String("nothing"), " presses 7 (power to weapons) at frame ", peer.Frame()));
			script->Tap(Input::Key::Digit7);
		}
		else if (fighter != nullptr && n == fighterFrom + 80) script->Tap(Input::Key::F);
		else if (fighter != nullptr && n == fighterFrom + 100) script->Tap(Input::Key::LeftCtrl);
	}
}
