// Port of Unity.SpaceGod Core/Session/SeatAuthority.cs (US-T21, see docs/MIGRATION.md).

#include "Session/SeatAuthority.h"
#include "Embodiment/EmbodimentSession.h"
#include "Session/Commands.h"
#include "Session/SessionEvents.h"

namespace SpaceGod::Core
{
	SeatAuthority::SeatAuthority(int seed, const cs::List<PlayerId>& players)
	{
		this->seed = seed;
		this->players = cs::List<PlayerId>(players.begin(), players.end());
		for (const auto& p : this->players) seats[p] = cs::New<EmbodimentSession>();
	}

	bool SeatAuthority::Has(PlayerId player) const { return !player.IsNone() && seats.ContainsKey(player); }

	cs::Ref<IEmbodiable> SeatAuthority::SeatOf(PlayerId player) const
	{
		cs::Ref<EmbodimentSession> s;
		return seats.TryGetValue(player, s) ? s->Current : nullptr;
	}

	int SeatAuthority::SeatIdOf(PlayerId player) const
	{
		auto held = SeatOf(player);
		int id = 0;
		return held != nullptr && idOf.TryGetValue(EmbodiableKey(held), id) ? id : 0;
	}

	PlayerId SeatAuthority::HolderOf(int seat) const
	{
		cs::Ref<IEmbodiable> s;
		if (!byId.TryGetValue(seat, s)) return PlayerId::None();
		for (const auto& p : players)
			if (EmbodiableEquals(seats[p]->Current, s)) return p;
		return PlayerId::None();
	}

	cs::Ref<IEmbodiable> SeatAuthority::Seat(int seat) const
	{
		cs::Ref<IEmbodiable> s;
		return byId.TryGetValue(seat, s) ? s : nullptr;
	}

	cs::String SeatAuthority::EndedBecause(PlayerId player) const
	{
		cs::Ref<EmbodimentSession> s;
		return seats.TryGetValue(player, s) ? s->EndedBecause : cs::String();
	}

	int SeatAuthority::IdOf(const cs::Ref<IEmbodiable>& seat)
	{
		if (seat == nullptr) return 0;
		int id = 0;
		if (idOf.TryGetValue(EmbodiableKey(seat), id)) return id;
		id = nextId++;
		idOf[EmbodiableKey(seat)] = id;
		byId[id] = seat;
		return id;
	}

	void SeatAuthority::Apply(const cs::Ref<IGameCommand>& command, const cs::Action<cs::Ref<IGameEvent>>& raise)
	{
		if (command == nullptr) return;
		auto p = command->Player();
		if (!Has(p))
		{
			raise(cs::New<CommandRefused>(command, p.IsNone() ? cs::String("no player issued it") : cs::Cat(p, " is not in this session")));
			return;
		}
		auto session = seats[p];
		if (auto take = cs::As<TakeSeatCommand>(command))
		{
			auto next = Seat(take->Seat);
			if (next == nullptr)
			{
				raise(cs::New<CommandRefused>(command, cs::Cat("no seat ", take->Seat)));
				return;
			}
			Take(p, session, next, command, raise);
			return;
		}
		if (cs::Is<MoveToHostCommand>(command))
		{
			auto host = session->Current ? session->Current->Host() : nullptr;
			if (host == nullptr)
			{
				raise(cs::New<CommandRefused>(command, "nothing to move up to"));
				return;
			}
			Take(p, session, host, command, raise);
			return;
		}
		if (cs::Is<ReleaseSeatCommand>(command))
		{
			int was = SeatIdOf(p);
			session->Release();
			controls.Remove(p);
			if (was != 0) raise(cs::New<SeatReleased>(p, was, cs::String()));
			return;
		}
		if (auto control = cs::As<SeatControlCommand>(command))
		{
			if (session->Current == nullptr)
			{
				raise(cs::New<CommandRefused>(command, cs::Cat(p, " holds no seat")));
				return;
			}
			controls[p] = control->Input;
			return;
		}
		raise(cs::New<CommandRefused>(command, cs::Cat("unknown command ", GameCommandTypeName(*command))));
	}

	void SeatAuthority::Take(PlayerId p, const cs::Ref<EmbodimentSession>& session, const cs::Ref<IEmbodiable>& next, const cs::Ref<IGameCommand>& command, const cs::Action<cs::Ref<IGameEvent>>& raise)
	{
		int id = IdOf(next);
		auto holder = HolderOf(id);
		if (!holder.IsNone() && holder != p)
		{
			raise(cs::New<CommandRefused>(command, cs::Cat(next->Name(), " is held by ", holder)));
			return;
		}
		int was = SeatIdOf(p);
		bool took = session->Switch(next);
		// Retaking the seat already held can fail too (it can no longer be embodied): it is let go all the same.
		if (was != 0 && (was != id || !took)) raise(cs::New<SeatReleased>(p, was, cs::String()));
		if (!took)
		{
			controls.Remove(p);
			// C#: next.Blocker ?? "out of reach"
			auto blocker = next->Blocker();
			raise(cs::New<CommandRefused>(command, cs::Cat("can't take ", next->Name(), ": ", blocker.empty() ? cs::String("out of reach") : blocker)));
			return;
		}
		if (was != id)
		{
			controls.Remove(p);
			raise(cs::New<SeatTaken>(p, id));
		}
	}

	void SeatAuthority::Advance(double dt, const cs::Action<cs::Ref<IGameEvent>>& raise, bool countTick)
	{
		for (const auto& p : players)
		{
			auto session = seats[p];
			if (session->Current == nullptr) continue;
			int was = SeatIdOf(p);
			EmbodimentInput input{};
			controls.TryGetValue(p, input);
			if (!session->Step(input, dt))
			{
				controls.Remove(p);
				// C#: session.EndedBecause ?? "lost"
				raise(cs::New<SeatReleased>(p, was, session->EndedBecause.empty() ? cs::String("lost") : session->EndedBecause));
			}
		}
		if (countTick) tick++;
	}
}
