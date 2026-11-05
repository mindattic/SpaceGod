#pragma once
// Port of Unity.SpaceGod Core/Session/ISession.cs (US-T21, see docs/MIGRATION.md).
//
// The interfaces' properties are pure virtual methods (Tick(), Seed(), LocalPlayer(), ...). An interface cannot hold
// a field, so ISession's `event Action<IGameEvent> EventRaised` is the virtual EventRaised() returning the
// implementation's cs::Event: `session.EventRaised += h` is `session->EventRaised() += h`.

#include "Cs/Cs.h"
#include "Session/PlayerId.h"

namespace SpaceGod::Core
{
	class IEmbodiable;
	class IGameCommand;
	class IGameEvent;

	/// <summary>One seat held (US-T135): by whom, its session id, and the seat.</summary>
	struct HeldSeat
	{
		PlayerId Player = PlayerId::None();
		int SeatId = 0;
		cs::Ref<IEmbodiable> Seat;
	};

	/// <summary>
	/// The read-only view of the session's state that presentation reads (U.SPC-T410): who holds which seat. Everything in
	/// it is keyed by <see cref="PlayerId"/> or by a seat's id, never by "the player".
	/// </summary>
	class ISessionState
	{
	public:
		virtual ~ISessionState() = default;

		/// <summary>Fixed ticks stepped so far (<see cref="LocalSession.TickSeconds"/> each).</summary>
		virtual long long Tick() const = 0;

		/// <summary>The seed the session was started with: its rules draw on nothing unseeded.</summary>
		virtual int Seed() const = 0;

		/// <summary>What <paramref name="player"/> holds now, or null.</summary>
		virtual cs::Ref<IEmbodiable> SeatOf(PlayerId player) const = 0;

		/// <summary>The id of what <paramref name="player"/> holds, or 0 for nothing.</summary>
		virtual int SeatIdOf(PlayerId player) const = 0;

		/// <summary>Who holds the seat with this id, or <see cref="PlayerId.None"/>.</summary>
		virtual PlayerId HolderOf(int seat) const = 0;

		/// <summary>Who holds <paramref name="seat"/> (by the seats' own Equals, so any wrapper round the same thing), or
		/// <see cref="PlayerId.None"/> (US-T135).</summary>
		virtual PlayerId HolderOf(const cs::Ref<IEmbodiable>& seat) const = 0;

		/// <summary>
		/// Every seat held now and who holds it (US-T135), in player order: what a view reads to mark the units other players
		/// embody (a co-op or PvP glint, later), each seat's <see cref="IEmbodiable.Subject"/> naming its hull and part. The
		/// authority's own state, identical on every lockstep peer (it changes only on the command stream: TakeSeat,
		/// MoveToHost, ReleaseSeat, a seat lost on a tick). A ship's helm flown by the app's pilot loop is the app's to add
		/// (<see cref="UnitySpaceApp.EmbodiedNow"/>). Transient: nothing here is saved or credited to a player.
		/// </summary>
		virtual cs::List<HeldSeat> Held() const = 0;

		/// <summary>The seat with this id, or null for one the session was never told of.</summary>
		virtual cs::Ref<IEmbodiable> Seat(int seat) const = 0;

		/// <summary>Why <paramref name="player"/>'s seat was last lost by itself, or null (<see cref="EmbodimentSession.EndedBecause"/>).</summary>
		/// (Empty stands for C#'s null.)
		virtual cs::String EndedBecause(PlayerId player) const = 0;
	};

	/// <summary>
	/// The one seam between the game and whoever runs its rules (U.SPC-T410, per Unity.Multiplayer's multiplayer-ready
	/// standard): input submits commands here, presentation reads <see cref="State"/> and <see cref="EventRaised"/>, and
	/// <see cref="LocalPlayer"/> is the only place that says which player this machine plays. The single-player game runs
	/// on <see cref="LocalSession"/>; a networked session would replace it without touching input, rules or views.
	/// </summary>
	class ISession
	{
	public:
		virtual ~ISession() = default;

		/// <summary>The player this machine plays. Only input and presentation ask; the rules never do.</summary>
		virtual PlayerId LocalPlayer() const = 0;

		virtual const cs::List<PlayerId>& Players() const = 0;

		virtual cs::Ref<ISessionState> State() const = 0;

		virtual cs::Event<cs::Ref<IGameEvent>>& EventRaised() = 0;

		/// <summary>The one command pipeline: the authority validates the command and applies it on the next tick.</summary>
		virtual void Submit(const cs::Ref<IGameCommand>& command) = 0;

		/// <summary>
		/// The stable id the authority gives a seat: a counter, assigned the first time the seat is named and kept for as
		/// long as the session lives, so a command names a seat without holding a reference to it.
		/// </summary>
		virtual int IdOf(const cs::Ref<IEmbodiable>& seat) = 0;
	};
}
