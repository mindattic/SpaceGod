#pragma once
// Port of Unity.SpaceGod Core/Session/Commands.cs (US-T21, see docs/MIGRATION.md).
//
// The C# commands are readonly structs used through the IGameCommand interface (boxed). Here IGameCommand is an
// abstract class, each command a struct deriving from it, and a command is passed as cs::Ref<IGameCommand> (the box;
// a command never changes, so sharing it is the same as copying it). The interface's `Player { get; }` is the
// virtual Player(); each command's other properties are fields. `switch (command) { case TakeSeatCommand t: ... }`
// is `if (auto t = cs::As<TakeSeatCommand>(command))`.

#include "Cs/Cs.h"
#include "Embodiment/Embodiment.h" // EmbodimentInput (by value)
#include "Session/PlayerId.h"

#include <typeinfo>

namespace SpaceGod::Core
{
	/// <summary>
	/// Something a player wants done (U.SPC-T410, docs/BIBLE.md U.SPC-LAW-1): a small value of plain data and ids, stamped
	/// with the <see cref="PlayerId"/> that issued it and handed to <see cref="ISession.Submit"/>, the one command pipeline.
	/// Input code builds commands and never changes the session's state itself; <see cref="CommandCodec"/> writes each one
	/// as text and reads it back equal.
	/// </summary>
	class IGameCommand
	{
	public:
		virtual ~IGameCommand() = default;
		virtual PlayerId Player() const = 0;
	};

	/// <summary>Take charge of the seat with this id (<see cref="ISession.IdOf"/>), handing back whatever the player held.</summary>
	struct TakeSeatCommand final : IGameCommand
	{
		TakeSeatCommand(PlayerId player, int seat)
		{
			player_ = player;
			Seat = seat;
		}

		PlayerId Player() const override { return player_; }
		int Seat = 0;

	private:
		PlayerId player_;
	};

	/// <summary>Hand what the player holds back to its AI.</summary>
	struct ReleaseSeatCommand final : IGameCommand
	{
		explicit ReleaseSeatCommand(PlayerId player) { player_ = player; }
		PlayerId Player() const override { return player_; }

	private:
		PlayerId player_;
	};

	/// <summary>Move up from a mounted seat to what it is mounted on (a gun to its station, <see cref="EmbodimentSession.MoveToHost"/>).</summary>
	struct MoveToHostCommand final : IGameCommand
	{
		explicit MoveToHostCommand(PlayerId player) { player_ = player; }
		PlayerId Player() const override { return player_; }

	private:
		PlayerId player_;
	};

	/// <summary>
	/// The player's controls at the seat they hold (stick, throttle, triggers, already in world terms), held from this tick
	/// until the next one replaces it, as a stick stays where it is put.
	/// </summary>
	struct SeatControlCommand final : IGameCommand
	{
		SeatControlCommand(PlayerId player, EmbodimentInput input)
		{
			player_ = player;
			Input = input;
		}

		PlayerId Player() const override { return player_; }
		EmbodimentInput Input;

	private:
		PlayerId player_;
	};

	/// <summary>
	/// Something said over comms by the unit the player holds (US-T137): to whom (<see cref="CommsParty.Key"/>: a hull's id,
	/// or -(settlement id + 1)) and what (a <see cref="CommsVerb"/>). Comms answers from the world's state alone
	/// (<see cref="Comms.Say"/>), so the same command at the same tick says and does the same on every peer and in a replay.
	/// The seat authority passes it by: the world, not the seats, applies it.
	/// </summary>
	struct CommsCommand final : IGameCommand
	{
		CommsCommand(PlayerId player, int target, int verb)
		{
			player_ = player;
			Target = target;
			Verb = verb;
		}

		PlayerId Player() const override { return player_; }
		int Target = 0;
		int Verb = 0;

	private:
		PlayerId player_;
	};

	/// Not C#: `command.GetType().Name` for the codec's and the authority's messages -- the class name without its
	/// namespace (typeid names are compiler-specific; MSVC's are "struct SpaceGod::Core::X", so the last segment is kept).
	inline cs::String GameCommandTypeName(const IGameCommand& command)
	{
		cs::String n = typeid(command).name();
		auto colon = n.rfind(':');
		if (colon != cs::String::npos) n = n.substr(colon + 1);
		auto space = n.rfind(' ');
		if (space != cs::String::npos) n = n.substr(space + 1);
		return n;
	}
}
