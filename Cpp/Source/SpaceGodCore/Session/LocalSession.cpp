// Port of Unity.SpaceGod Core/Session/LocalSession.cs (US-T21, see docs/MIGRATION.md).

#include "Session/LocalSession.h"
#include "Session/Commands.h"
#include "Session/SeatAuthority.h"
#include "Session/SessionEvents.h"

namespace SpaceGod::Core
{
	LocalSession::LocalSession(int seed, const cs::List<PlayerId>& players, PlayerId local)
	{
		this->players = players;
		localPlayer = local;
		authority = cs::New<SeatAuthority>(seed, players);
	}

	cs::Ref<LocalSession> LocalSession::Create(int seed, int players, int local)
	{
		if (players < 1) throw cs::ArgumentOutOfRangeException("players");
		if (local < 0 || local >= players) throw cs::ArgumentOutOfRangeException("local");
		cs::List<PlayerId> ids;
		for (int i = 0; i < players; i++) ids.Add(PlayerId(i));
		// The constructor is private, as in C#, so not cs::New (make_shared cannot reach it).
		return cs::Ref<LocalSession>(new LocalSession(seed, ids, ids[local]));
	}

	cs::Ref<ISessionState> LocalSession::State() const { return authority; }

	void LocalSession::Submit(const cs::Ref<IGameCommand>& command)
	{
		if (command != nullptr) pending.Enqueue(command);
	}

	void LocalSession::SetGateWorld(const cs::Ref<World>& world) { authority->GateWorld = world; } // US-T140

	int LocalSession::IdOf(const cs::Ref<IEmbodiable>& seat) { return authority->IdOf(seat); }

	void LocalSession::ApplyPending()
	{
		cs::Action<cs::Ref<IGameEvent>> raise = [this](cs::Ref<IGameEvent> e) { Raise(e); };
		while (pending.Count() > 0)
		{
			auto c = pending.Dequeue();
			if (Logging) Log.Add(c);
			authority->Apply(c, raise);
		}
	}

	void LocalSession::Step(int ticks)
	{
		cs::Action<cs::Ref<IGameEvent>> raise = [this](cs::Ref<IGameEvent> e) { Raise(e); };
		for (int i = 0; i < ticks; i++)
		{
			ApplyPending();
			authority->Advance(TickSeconds, raise, /*countTick:*/ true);
		}
	}

	void LocalSession::Advance(double dt)
	{
		ApplyPending();
		cs::Action<cs::Ref<IGameEvent>> raise = [this](cs::Ref<IGameEvent> e) { Raise(e); };
		authority->Advance(dt, raise, /*countTick:*/ false);
	}

	void LocalSession::Raise(const cs::Ref<IGameEvent>& e) { eventRaised.Invoke(e); }
}
