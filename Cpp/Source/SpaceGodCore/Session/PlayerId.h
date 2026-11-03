#pragma once
// Port of Unity.SpaceGod Core/Session/PlayerId.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// A player, by seat (U.SPC-T410, per Unity.Multiplayer's multiplayer-ready standard, docs/BIBLE.md U.SPC-LAW-1): the
	/// only way the session's rules know who did something. Seat 0 is the first player; <see cref="None"/> is nobody.
	/// Which seat this machine plays is answered only by <see cref="ISession.LocalPlayer"/>, never by the rules.
	/// </summary>
	struct PlayerId
	{
		PlayerId() = default;
		explicit PlayerId(int seat) { Seat = seat; }

		/// <summary>The seat index, 0 up; negative for <see cref="None"/>.</summary>
		int Seat = 0;

		/// <summary>No player: what a command carries when nobody is behind it, and always refused.</summary>
		static PlayerId None() { return PlayerId(-1); }

		bool IsNone() const { return Seat < 0; }

		bool Equals(const PlayerId& other) const { return Seat == other.Seat; }
		size_t HashCode() const { return static_cast<size_t>(static_cast<uint32_t>(Seat)); } // C#: GetHashCode() => Seat
		bool operator==(const PlayerId& b) const { return Seat == b.Seat; }
		bool operator!=(const PlayerId& b) const { return Seat != b.Seat; }
		cs::String ToString() const { return IsNone() ? cs::String("nobody") : cs::Cat("player ", Seat + 1); }
	};
}
