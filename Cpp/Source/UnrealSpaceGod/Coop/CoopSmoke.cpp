// US-T44: CoopSmoke.cs (US-T39) in C++; see CoopSmoke.h. The same steps at the same replayed frames, the same log lines.

#include "Coop/CoopSmoke.h"

#include "Coop/CoopPeer.h"
#include "Smoke/ScriptedDevice.h"
#include "UnitySpaceApp.h"

#include "Embodiment/Embodiables.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"
#include "Combat/War.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
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
		constexpr double TimeoutSeconds = 6 * 60;

		double Since()
		{
			static const double start = FPlatformTime::Seconds();
			return FPlatformTime::Seconds() - start;
		}

		std::unique_ptr<Smoke::ScriptedDevice> script;
		long long byeAt = -1;
		long long replayed = 0;
		int quitIn = -1;
		bool finished = false;
		cs::String warpBefore;

		void Finish(const cs::String& why, CoopPeer& peer)
		{
			if (finished) return;
			finished = true;
			CoopPeer::Log(cs::Cat("smoke (", Role(), ") done: ", why, "; frame ", peer.Frame(), ", ",
				peer.IsHost() ? cs::Cat(peer.DigestMatches, " digest matches, ", peer.Desyncs, " desyncs") : cs::Cat(peer.ClockCorrections, " clock corrections"),
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
		(void)app;
		if (Role() != "host" || finished) return;
		if (peer.AnyBye() && byeAt < 0) byeAt = rec.Frame;
		if (byeAt >= 0 && rec.Frame >= byeAt + 30) Finish("the client said bye", peer);
		else if (Since() > TimeoutSeconds) Finish("TIMEOUT", peer);
	}

	void ClientTick(CoopPeer& peer)
	{
		if (Role() != "client" || finished) return;
		if (quitIn > 0 && --quitIn == 0) Finish("said bye", peer);
		else if (Since() > TimeoutSeconds) Finish("TIMEOUT", peer);
	}

	void ClientFrame(CoopPeer& peer, UnitySpaceApp& app)
	{
		if (Role() != "client" || finished || !script) return;
		long long n = ++replayed;
		switch (n)
		{
			case 30:
				warpBefore = app.Clock->Warp().Label;
				script->Tap(Input::Key::Period);
				CoopPeer::Log(cs::Cat("smoke: client presses '.' (time warp up) at frame ", peer.Frame(), ", warp ", warpBefore));
				break;
			case 300:
				CoopPeer::Log(cs::Cat("smoke: client's warp ", warpBefore, " -> ", app.Clock->Warp().Label, " (the host's clock) at frame ", peer.Frame()));
				break;
			case 180:
				script->Tap(Input::Key::Digit3);
				CoopPeer::Log(cs::Cat("smoke: client presses '3' (select) at frame ", peer.Frame()));
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
			case 700:
			{
				// A desync on purpose: one credit more in a faction's treasury here only, as a client acting on the Core
				// behind the session's back would. The next digest differs; the host resyncs everyone from its save.
				const auto& factions = app.World()->Fleet->Factions();
				cs::Ref<Faction> faction = factions.Count() > 0 ? factions[0] : nullptr;
				if (faction != nullptr) faction->Treasury += 1;
				CoopPeer::Log(cs::Cat("smoke: client tampers with ", faction != nullptr ? faction->Name : cs::String(), "'s treasury at frame ", peer.Frame(),
					" (an injected desync)"));
				break;
			}
			case 600:
			{
				auto held = app.Seat();
				CoopPeer::Log(cs::Cat("smoke: client holds ", held != nullptr ? held->Name() : cs::String("nothing"), " at frame ", peer.Frame(), ", warp ",
					app.Clock->Warp().Label));
				break;
			}
			case ClientFrames:
				peer.SayBye();
				quitIn = 30;
				break;
			default:
				break;
		}
	}
}
