// Port of SpaceGod.Unity.CSharp Runtime/CaptureEvents.cs (US-T72). The defaults section (ApplyDefaults) is the capture
// host's here: the game hands it the shipped SpaceGod.capture.json (Capture/MindAtticCapture.cpp, --defaults).

#include "CaptureEvents.h"

#include "Capture/MindAtticCapture.h"
#include "Input/InputState.h"
#include "UnitySpaceApp.h"
#include "UnityCompat/UnityRuntime.h"

#include "BodyCatalog.h"
#include "Combat/Battle.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "Systems/StarDiscovery.h"
#include "World.h"

#include <string>
#include <unordered_set>

using namespace SpaceGod::Core;
using SpaceGod::UnityCompat::Debug;

namespace SpaceGod::CaptureEvents
{
	namespace
	{
		FString ToF(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

		/// A value as the log line writes it: invariant culture, and quoted (inner quotes doubled) when it holds a space
		/// or a quote, or is empty.
		cs::String Format(const Field& f)
		{
			if (f.IsNumber) return std::to_string(f.Number);
			cs::String s = f.Text;
			if (s.empty() || s.find(' ') != cs::String::npos || s.find('"') != cs::String::npos)
			{
				cs::String q = "\"";
				for (char c : s)
				{
					if (c == '"') q += "\"\"";
					else q += c;
				}
				s = q + "\"";
			}
			return s;
		}

		void Command(const char* command)
		{
			Debug::Log(cs::Cat("[capture] hotkey ", command));
			MindAtticCapture::Command(FString(UTF8_TO_TCHAR(command)));
		}

		// ---------------------------------------------------------------- subscriptions

		UnitySpaceApp* app = nullptr;
		bool rebuiltHooked = false;
		const Galaxy* subscribed = nullptr;

		// ---------------------------------------------------------------- polled events

		bool primed = false;
		const StarSystem* lastSystem = nullptr;
		cs::String lastSystemName;
		int lastWarp = 0;
		cs::Ref<Ship> lastHelm;
		bool lastDestroyed = false;
		ShipPhase lastPhase = ShipPhase::Parked;
		int lastBody = -1;
		cs::Ref<Battle> followed;
		int followedSide = -1;
		const StarSystem* followedIn = nullptr;
		cs::String followedShip;
		std::unordered_set<const BattleUnit*> deadSeen;

		cs::String BodyName(UnitySpaceApp* host, int body)
		{
			const auto& catalog = host->Catalog();
			return body >= 0 && body < catalog->Count() ? (*catalog)[body]->Name : cs::String();
		}

		void Remember(UnitySpaceApp* host, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& helm)
		{
			lastSystem = system.get();
			lastSystemName = system ? system->Name : cs::String();
			lastWarp = host->Clock->WarpIndex;
			lastHelm = helm;
			lastDestroyed = helm != nullptr && helm->Destroyed;
			lastPhase = helm != nullptr ? helm->Phase : ShipPhase::Parked;
			lastBody = helm != nullptr && helm->Orbit != nullptr ? helm->Orbit->Body : -1;
		}

		void WatchLog(const Galaxy* galaxy, const cs::Ref<StarSystem>& system)
		{
			const SpaceGod::Core::World* world = system->World.get();
			system->World->Logged += [galaxy, world](WorldEvent e)
			{
				if (app == nullptr || galaxy != app->Galaxy.get() || app->ActiveSystem == nullptr || app->ActiveSystem->World.get() != world ||
					e.Kind != WorldEventKind::Diplomacy) return;
				if (e.Text.find(" declares war on ") != cs::String::npos) Raise("war-declared", { { "text", e.Text } });
				else if (e.Text.find("make peace") != cs::String::npos || e.Text.find("makes peace") != cs::String::npos)
					Raise("peace", { { "text", e.Text } });
			};
		}
	}

	bool Running() { return MindAtticCapture::Enabled(); }

	void Raise(const char* name, std::initializer_list<Field> fields)
	{
		if (!Running()) return;
		MindAtticCapture::Data data;
		cs::String line = cs::Cat("[capture] event ", name);
		for (const Field& f : fields)
		{
			if (f.IsNumber) data.Num(UTF8_TO_TCHAR(f.Key), static_cast<double>(f.Number));
			else data.Str(UTF8_TO_TCHAR(f.Key), ToF(f.Text));
			line += cs::Cat(" ", f.Key, "=", Format(f));
		}
		Debug::Log(line);
		MindAtticCapture::Note(ToF(line));
		MindAtticCapture::Raise(FString(UTF8_TO_TCHAR(name)), data);
	}

	// ---------------------------------------------------------------- direct events

	void Trade(const cs::String& side, const cs::String& good, int crates, const cs::String& market)
	{
		Raise("trade", { { "side", side }, { "good", good }, { "crates", crates }, { "market", market } });
	}

	void Hail(const cs::String& from, const cs::String& to) { Raise("hail", { { "from", from }, { "to", to } }); }

	void GateDeparture(const cs::String& ship, const cs::String& gate, const cs::String& system, const cs::String& to)
	{
		Raise("gate-departure", { { "ship", ship }, { "gate", gate }, { "system", system }, { "to", to } });
	}

	void GameSaved(const cs::String& slot) { Raise("game-saved", { { "slot", slot } }); }
	void GameLoaded(const cs::String& slot) { Raise("game-loaded", { { "slot", slot } }); }
	void CoopJoined(const cs::String& player, int seat) { Raise("coop-joined", { { "player", player }, { "seat", seat } }); }
	void CoopLeft(const cs::String& player, int seat) { Raise("coop-left", { { "player", player }, { "seat", seat } }); }

	void CoopResync(long long sync, long long frame, const cs::String& reason)
	{
		Raise("coop-resync", { { "sync", sync }, { "frame", frame }, { "reason", reason } });
	}

	// ---------------------------------------------------------------- hotkeys (section 5)

	void Hotkeys(const Input::InputState& input)
	{
		if (!Running()) return;
		if (input.CaptureScreenshot()) Command("screenshot");
		if (input.CaptureToggleRecording()) Command("toggle-recording");
		if (input.CaptureSaveReplay()) Command("save-replay");
	}

	// ---------------------------------------------------------------- subscriptions

	void Attach(UnitySpaceApp* host)
	{
		if (app != host)
		{
			app = host;
			rebuiltHooked = false;
		}
		if (!rebuiltHooked)
		{
			rebuiltHooked = true;
			host->GalaxyRebuilt += [host] { if (app == host) Attach(host); };
		}
		primed = false;
		followed = nullptr;
		deadSeen.clear();
		const cs::Ref<Galaxy>& galaxy = host->Galaxy;
		if (galaxy == nullptr || galaxy.get() == subscribed) return;
		subscribed = galaxy.get();
		const Galaxy* g = galaxy.get();
		galaxy->StarDiscovered += [g](cs::Ref<StarDiscoveredEvent> e)
		{
			if (app != nullptr && g == app->Galaxy.get()) Raise("star-discovered", { { "star", e->Name }, { "by", e->By } });
		};
		for (const auto& system : galaxy->Systems) WatchLog(g, system);
		galaxy->SystemAdded += [g](cs::Ref<StarSystem> s, double) { WatchLog(g, s); };
	}

	void Detach(UnitySpaceApp* host)
	{
		if (app != host) return;
		app = nullptr;
		subscribed = nullptr;
		primed = false;
		lastHelm = nullptr;
		followed = nullptr;
		deadSeen.clear();
	}

	// ---------------------------------------------------------------- polled events

	void Tick(UnitySpaceApp* host)
	{
		if (!Running() || host == nullptr || host->Galaxy == nullptr || host->ActiveSystem == nullptr)
		{
			primed = false;
			return;
		}
		const cs::Ref<StarSystem>& system = host->ActiveSystem;
		cs::Ref<Ship> helm = system->World->PilotedShip;
		if (!primed)
		{
			Remember(host, system, helm);
			primed = true;
			return;
		}

		if (system.get() != lastSystem)
			Raise("system-arrival", { { "system", system->Name }, { "from", lastSystemName }, { "ship", helm != nullptr ? helm->Name : cs::String() } });
		if (host->Clock->WarpIndex != lastWarp)
			Raise("time-warp", { { "index", host->Clock->WarpIndex }, { "label", host->Clock->Warp().Label } });
		if (lastHelm != nullptr && !lastDestroyed && lastHelm->Destroyed)
			Raise("player-ship-destroyed", { { "ship", lastHelm->Name }, { "class", ToString(lastHelm->Spec->Class) }, { "system", lastSystemName } });
		if (helm != nullptr && helm == lastHelm && !helm->Destroyed)
		{
			bool parked = helm->Phase == ShipPhase::Parked, wasParked = lastPhase == ShipPhase::Parked;
			if (parked && !wasParked) Raise("orbit-arrival", { { "ship", helm->Name }, { "body", BodyName(host, helm->Orbit != nullptr ? helm->Orbit->Body : -1) } });
			else if (!parked && wasParked) Raise("orbit-departure", { { "ship", helm->Name }, { "body", BodyName(host, lastBody) } });
		}

		// A battle followed is let go, unannounced, when the system it is in is left.
		if (followed != nullptr && followedIn != system.get())
		{
			followed = nullptr;
			deadSeen.clear();
		}
		if (followed == nullptr && helm != nullptr && !helm->Destroyed && helm->Battle != nullptr && !helm->Battle->Over)
		{
			followed = helm->Battle;
			followedIn = system.get();
			followedShip = helm->Name;
			followedSide = -1;
			for (const auto& u : followed->Units)
			{
				if (u->Ship == helm) followedSide = u->FoughtOn();
				if (u->Dead) deadSeen.insert(u.get());
			}
			Raise("battle-started", { { "battle", followed->Name }, { "system", system->Name }, { "ship", helm->Name } });
		}
		if (followed != nullptr)
		{
			for (const auto& u : followed->Units)
				if (u->Dead && deadSeen.insert(u.get()).second)
					Raise("ship-destroyed", { { "name", u->Name() }, { "kind", ToString(u->Kind) },
						{ "side", followedSide >= 0 && u->FoughtOn() == followedSide ? "ours" : "theirs" }, { "battle", followed->Name } });
			if (followed->Over)
			{
				const char* end = followed->Winner < 0 ? "battle-drawn" : followed->Winner == followedSide ? "battle-won" : "battle-lost";
				Raise(end, { { "battle", followed->Name }, { "system", system->Name }, { "ship", followedShip } });
				followed = nullptr;
				deadSeen.clear();
			}
		}
		Remember(host, system, helm);
	}
}
