#pragma once
// Port of Unity.SpaceGod Core/Session/SessionEvents.cs (US-T21, see docs/MIGRATION.md). As with the commands
// (Session/Commands.h), the C# readonly structs used through IGameEvent are structs deriving from an abstract
// IGameEvent, passed as cs::Ref<IGameEvent>.

#include "Cs/Cs.h"
#include "Session/PlayerId.h"

namespace SpaceGod::Core
{
	class IGameCommand;

	/// <summary>
	/// What the session's authority tells presentation happened (U.SPC-T410): presentation reads state and listens to
	/// these, and never writes state itself. Each names the player it concerns.
	/// </summary>
	class IGameEvent
	{
	public:
		virtual ~IGameEvent() = default;
		virtual PlayerId Player() const = 0;
	};

	/// <summary>The player now holds the seat with this id.</summary>
	struct SeatTaken final : IGameEvent
	{
		SeatTaken(PlayerId player, int seat)
		{
			player_ = player;
			Seat = seat;
		}

		PlayerId Player() const override { return player_; }
		int Seat = 0;

	private:
		PlayerId player_;
	};

	/// <summary>
	/// The player no longer holds the seat with this id: let go or switched (<see cref="Reason"/> null), or lost by itself
	/// (shot down, the fight over), when <see cref="Reason"/> says why.
	/// </summary>
	/// (An empty Reason stands for C#'s null.)
	struct SeatReleased final : IGameEvent
	{
		SeatReleased(PlayerId player, int seat, cs::String reason)
		{
			player_ = player;
			Seat = seat;
			Reason = std::move(reason);
		}

		PlayerId Player() const override { return player_; }
		int Seat = 0;
		cs::String Reason;

	private:
		PlayerId player_;
	};

	/// <summary>A command the authority would not carry out, and why (a player not in the session, a seat held by another).</summary>
	struct SPACEGODCORE_API CommandRefused final : IGameEvent
	{
		CommandRefused(cs::Ref<IGameCommand> command, cs::String reason)
		{
			Command = std::move(command);
			Reason = std::move(reason);
		}

		/// C#: Command?.Player ?? PlayerId.None
		PlayerId Player() const override;
		cs::Ref<IGameCommand> Command;
		cs::String Reason;
	};
}
