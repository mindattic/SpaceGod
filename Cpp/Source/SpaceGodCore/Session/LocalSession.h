#pragma once
// Port of Unity.SpaceGod Core/Session/LocalSession.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Session/ISession.h"
#include "Session/PlayerId.h"

namespace SpaceGod::Core
{
	class IEmbodiable;
	class IGameCommand;
	class IGameEvent;
	class SeatAuthority;
	class World;

	/// <summary>
	/// The session the game ships with (U.SPC-T410, docs/BIBLE.md U.SPC-LAW-1): the <see cref="SeatAuthority"/> in process,
	/// every submitted command queued and applied in order at the start of the next step, and the authority's events
	/// raised straight to presentation. Hot-seat or split-screen play is the same session with more than one player; a
	/// networked session would take its place behind <see cref="ISession"/>.
	///
	/// <see cref="Step(int)"/> advances on the fixed tick (<see cref="TickSeconds"/>), which is how tests and replays drive
	/// it. The single-player game still runs its flight on frame time scaled by the warp, as it always has, through
	/// <see cref="Advance"/>; that frame-time step is the one rule-4 gap left, and the BIBLE law says so.
	/// </summary>
	class SPACEGODCORE_API LocalSession final : public ISession
	{
	public:
		/// <summary>The fixed tick rate the standard defaults to.</summary>
		static constexpr int TicksPerSecond = 30;

		static constexpr double TickSeconds = 1.0 / TicksPerSecond;

	private:
		cs::Ref<SeatAuthority> authority;
		cs::Queue<cs::Ref<IGameCommand>> pending;
		cs::List<PlayerId> players;
		PlayerId localPlayer;
		cs::Event<cs::Ref<IGameEvent>> eventRaised;

		LocalSession(int seed, const cs::List<PlayerId>& players, PlayerId local);

	public:
		/// <summary>A session of <paramref name="players"/> players (seats 0 up), this machine playing <paramref name="local"/>.</summary>
		static cs::Ref<LocalSession> Create(int seed, int players = 1, int local = 0);

		PlayerId LocalPlayer() const override { return localPlayer; }
		const cs::List<PlayerId>& Players() const override { return players; }
		cs::Ref<ISessionState> State() const override;
		cs::Event<cs::Ref<IGameEvent>>& EventRaised() override { return eventRaised; }

		/// <summary>Every command applied so far, in order: with the seed, a replay of the session (see <see cref="CommandCodec"/>).</summary>
		cs::List<cs::Ref<IGameCommand>> Log;

		/// <summary>Whether to keep <see cref="Log"/>; the shipped game leaves it off so a long session does not grow it.</summary>
		bool Logging = false;

		void Submit(const cs::Ref<IGameCommand>& command) override;

		/// <summary>The world the embody gate weighs takes against (US-T140, <see cref="SeatAuthority.GateWorld"/>).</summary>
		void SetGateWorld(const cs::Ref<World>& world);

		int IdOf(const cs::Ref<IEmbodiable>& seat) override;

		/// <summary>
		/// Applies every queued command now, in the order submitted, without advancing anything: what the next step would
		/// do first. The single-player game calls it so a key takes effect in the frame it is pressed.
		/// </summary>
		void ApplyPending();

		/// <summary><paramref name="ticks"/> fixed ticks: the queued commands, then each player's controls for one tick each.</summary>
		void Step(int ticks = 1);

		/// <summary>
		/// One step of <paramref name="dt"/> seconds of sim time: the queued commands, then each player's controls. The
		/// shipped game's frame-driven flight (see the class remarks); it does not count a tick.
		/// </summary>
		void Advance(double dt);

	private:
		void Raise(const cs::Ref<IGameEvent>& e);
	};
}
