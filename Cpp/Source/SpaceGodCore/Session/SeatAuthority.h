#pragma once
// Port of Unity.SpaceGod Core/Session/SeatAuthority.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Embodiment/Embodiment.h" // EmbodimentInput, EmbodiableKey (by value)
#include "Session/ISession.h"
#include "Session/PlayerId.h"

namespace SpaceGod::Core
{
	class EmbodimentSession;
	class IGameCommand;
	class IGameEvent;
	class World;

	/// <summary>
	/// The one authority over who holds which seat (U.SPC-T410, docs/BIBLE.md U.SPC-LAW-1): each player's own
	/// <see cref="EmbodimentSession"/>, the controls each last submitted, and the stable id of every seat named to it. Only
	/// it writes that state, and only in response to commands and ticks; it references no view, and raises events for
	/// presentation instead. Every player is just an id here: nothing asks which one this machine plays.
	/// </summary>
	class SPACEGODCORE_API SeatAuthority final : public ISessionState
	{
		cs::List<PlayerId> players;
		cs::Dictionary<PlayerId, cs::Ref<EmbodimentSession>> seats;
		cs::Dictionary<PlayerId, EmbodimentInput> controls;
		// C#: Dictionary<IEmbodiable, int>, keyed by the seats' own Equals / GetHashCode.
		cs::Dictionary<EmbodiableKey, int> idOf;
		cs::Dictionary<int, cs::Ref<IEmbodiable>> byId;
		int nextId = 1;

		// The ISessionState auto-properties (implemented through the interface's virtual getters).
		long long tick = 0;
		int seed = 0;

	public:
		SeatAuthority(int seed, const cs::List<PlayerId>& players);

		long long Tick() const override { return tick; }
		int Seed() const override { return seed; }
		const cs::List<PlayerId>& Players() const { return players; }

		bool Has(PlayerId player) const;

		/// <summary>The world the embody gate weighs takes against (US-T140: its Influence and the helm flown), or null
		/// (tests, a bare session): then only the gate's world-free rules apply. Set by the app every frame, the same world
		/// on every lockstep peer.</summary>
		cs::Ref<World> GateWorld;

		cs::Ref<IEmbodiable> SeatOf(PlayerId player) const override;

		int SeatIdOf(PlayerId player) const override;

		PlayerId HolderOf(int seat) const override;

		PlayerId HolderOf(const cs::Ref<IEmbodiable>& seat) const override;

		cs::List<HeldSeat> Held() const override;

		cs::Ref<IEmbodiable> Seat(int seat) const override;

		cs::String EndedBecause(PlayerId player) const override;

		/// <summary>The seat's id, given the first time it is named (seats compare by what they control, so a fresh wrapper
		/// round the same gun gets the same id).</summary>
		int IdOf(const cs::Ref<IEmbodiable>& seat);

		/// <summary>Validates <paramref name="command"/> and carries it out, raising what came of it.</summary>
		void Apply(const cs::Ref<IGameCommand>& command, const cs::Action<cs::Ref<IGameEvent>>& raise);

	private:
		/// <summary>
		/// <paramref name="p"/> takes <paramref name="next"/> unless another player holds it. As
		/// <see cref="EmbodimentSession.Switch"/>, a seat that cannot be embodied leaves the player holding nothing.
		/// </summary>
		void Take(PlayerId p, const cs::Ref<EmbodimentSession>& session, const cs::Ref<IEmbodiable>& next, const cs::Ref<IGameCommand>& command, const cs::Action<cs::Ref<IGameEvent>>& raise);

	public:
		/// <summary>
		/// One step of <paramref name="dt"/> seconds: each player's held controls go to their seat, in seat order, and a
		/// seat that can no longer be embodied is handed back and reported lost.
		/// </summary>
		void Advance(double dt, const cs::Action<cs::Ref<IGameEvent>>& raise, bool countTick);
	};
}
