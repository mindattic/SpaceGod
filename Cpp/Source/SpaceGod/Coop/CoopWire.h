#pragma once

// US-T44: co-op's wire format on the C++ track, the C# track's CSharp/.../Runtime/Coop/CoopWire.cs (US-T39) line for line
// (docs/MIGRATION.md "Co-op"). Text, invariant culture, doubles and floats as their IEEE bits in upper-case hex, so every
// peer steps on exactly the host's numbers and a record reads the same in either track.

#include "Cs/Cs.h"
#include "DVec3.h"

#include <cstdint>
#include <vector>

namespace SpaceGod::Core { class SimClock; }

namespace SpaceGod::Coop
{
	/// The sim clock's whole state at one moment (SimClock: the date, the warp, and the eased pause's three flags and
	/// speed). The host sends the state its clock had just before it advanced a frame; each client puts its own clock in
	/// exactly that state and advances by the same real seconds, so the dates agree bit for bit. The pause's flags have no
	/// setter in the Core (unchanged), so they are set through member pointers (C#: reflection).
	struct ClockState
	{
		double Jd = 0;
		int Warp = 0;
		bool Paused = false, Pausing = false, Resuming = false;
		double Speed = 1;

		bool PauseHeld() const { return Paused || Pausing; }

		static ClockState Of(const Core::SimClock& c);
		void ApplyTo(Core::SimClock& c) const;

		/// "<jd> <warp> <paused><pausing><resuming> <speed>"
		cs::String Write() const;
		static ClockState Read(const std::vector<cs::String>& f, int at);
	};

	/// One host frame as every client replays it: the real seconds the host's Update ran on, its clock just before and
	/// just after it advanced, whether (and from where) it ran the world's adventures, the commands applied at the start
	/// of the frame and what the frame did after that a client must do too (Post: a system made the active one).
	/// <code>
	/// frame 1234 3C888889 <jdBefore> 4 000 <speed> <jdAfter> 1 <x> <y> <z>
	/// e s take 1 0 @S 3        a session command (CommandCodec's line, unchanged), here with the seat's key
	/// e a 1 select b 3         player 2 selected body 3
	/// p activate Alpha Centauri
	/// </code>
	struct FrameRecord
	{
		long long Frame = 0;
		float Dt = 0;
		ClockState Before;
		double JdAfter = 0;
		bool Adventures = false;
		Core::DVec3 AdventurePoint;
		std::vector<cs::String> Entries;
		std::vector<cs::String> Post;

		cs::String Write() const;
		/// Throws std::runtime_error when the text is not a frame.
		static FrameRecord Read(const cs::String& text);
	};

	/// Number formats and the save's packing for the wire (C#: Wire).
	namespace Wire
	{
		cs::String D(double v);
		double ParseD(const cs::String& s);
		cs::String F(float v);
		float ParseF(const cs::String& s);
		cs::String I(int v);
		int ParseI(const cs::String& s);
		cs::String L(long long v);
		long long ParseL(const cs::String& s);

		/// Characters of the packed save per chunk message (well under the engine's 64 KB partial-bunch limit).
		constexpr int ChunkChars = 16000;

		/// A save (Core/Persistence's SaveGame bytes, already compressed) base64'd and cut into chunk strings.
		std::vector<cs::String> Pack(const cs::List<uint8_t>& save);
		cs::List<uint8_t> Unpack(const std::vector<cs::String>& chunks);

		/// 64-bit FNV-1a over the UTF-8 bytes of the text (Bench/CSharp's digest hash).
		uint64_t Fnv1a64(const cs::String& text);
		cs::String Hex(uint64_t h);

		/// string.Split(c): every field, empty ones included.
		std::vector<cs::String> Split(const cs::String& s, char c);
		/// string.Split(c, n): at most n fields, the last one the rest of the string.
		std::vector<cs::String> Split(const cs::String& s, char c, int n);
		bool StartsWith(const cs::String& s, const char* prefix);
	}
}
