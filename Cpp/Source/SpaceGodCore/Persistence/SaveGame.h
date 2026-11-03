#pragma once
// Port of Unity.SpaceGod Core/Persistence/SaveGame.cs (US-T20): SaveData, SaveSession, SaveHeader, SaveGame, SaveSlots.
//
// C++ notes:
// - C#'s Stream is std::ostream / std::istream (binary); byte[] is cs::List<uint8_t>.
// - The object graph is compressed with raw DEFLATE (RFC 1951: .NET's DeflateStream, no zlib or gzip header). SaveGame.cpp
//   calls zlib's deflateInit2/inflateInit2 with windowBits -15: define SPACEGOD_WITH_ZLIB inside Unreal (the engine's
//   ThirdParty "zlib" module) or SPACEGOD_WITH_MINIZ for the standalone build (miniz.h, zlib-compatible names); without
//   either, saving and loading throw NotSupportedException (see docs/PERSISTENCE.md).
// - A C# null string is an empty cs::String (SaveSession.ActiveSystem, SaveHeader.PilotedShip, a label).
// - SaveSlots.AllSlots (a C# iterator) returns its list eagerly.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/PilotFlight.h" // PilotState (by value)

#include <istream>
#include <ostream>

namespace SpaceGod::Core
{
	class SimClock;
	class Galaxy;
	class Ship;

	/// <summary>
	/// The parts of the player's session the simulation does not hold itself: which system was being
	/// watched, and, while a ship was being flown, its motion (the hull's schedule is pinned while it is
	/// piloted, so its true pose lives with the pilot).
	/// </summary>
	class SPACEGODCORE_API SaveSession final
	{
	public:
		cs::String ActiveSystem;

		/// <summary>The ship at the helm, or null when only watching (the world's <see cref="World.PilotedShip"/>).</summary>
		cs::Ref<Ship> Piloted;
		int PilotFrameBody = 0;
		PilotState Pilot;
		DVec3 PilotAim;
		bool FlightAssistOff = false;

		/// <summary>1 gameplay, 0 realistic.</summary>
		double ScaleGoal = 1;
	};

	/// <summary>
	/// Everything a save holds: the clock, the whole galaxy (every system's world with its ships,
	/// cargo, battles, markets, factions, wrecks, pods, law and adventures, the jump gates and the
	/// traffic between them) and the player's session at the helm.
	/// </summary>
	class SPACEGODCORE_API SaveData final
	{
	public:
		cs::Ref<SimClock> Clock;
		cs::Ref<::SpaceGod::Core::Galaxy> Galaxy;
		cs::Ref<SaveSession> Session = cs::New<SaveSession>();
	};

	/// <summary>What a save slot shows before it is loaded.</summary>
	class SPACEGODCORE_API SaveHeader final
	{
	public:
		int Format = 0;
		cs::String Slot;
		cs::String Label;
		cs::DateTime SavedUtc;
		double SimJd = 0;
		cs::String ActiveSystem;
		cs::String PilotedShip;

		/// <summary>The player's purse when it was saved: the treasury of the flown ship's faction (0 when only watching).</summary>
		double Credits = 0;
		cs::String Path;

		cs::DateTime SimUtc() const;
	};

	/// <summary>
	/// The save file: a small readable header (when, where, who was flying, their credits) and then the
	/// compressed object graph of a <see cref="SaveData"/> (see <see cref="ObjectGraph"/>).
	/// </summary>
	class SPACEGODCORE_API SaveGame
	{
	public:
		static constexpr int Format = 1;

		static void Write(std::ostream& stream, const cs::Ref<SaveData>& data, const cs::String& slot, const cs::String& label, const cs::DateTime& savedUtc);

		static cs::Ref<SaveHeader> ReadHeader(std::istream& stream);

		static cs::Ref<SaveData> Read(std::istream& stream, cs::Ref<SaveHeader>& header);

		static cs::List<uint8_t> ToBytes(const cs::Ref<SaveData>& data, const cs::String& slot = "", const cs::String& label = "");

		static cs::List<uint8_t> ToBytes(const cs::Ref<SaveData>& data, const cs::String& slot, const cs::String& label, const cs::DateTime& savedUtc);

		static cs::Ref<SaveData> FromBytes(const cs::List<uint8_t>& bytes);

		/// <summary>Writes to a temporary file beside <paramref name="path"/> and then swaps it in, so a crash mid-save never costs the old one.</summary>
		static void WriteFile(const cs::String& path, const cs::List<uint8_t>& bytes);

		// (C++) .NET's DeflateStream: raw DEFLATE, CompressionLevel.Fastest (zlib level 1) when writing.
		static std::vector<uint8_t> Deflate(const std::vector<uint8_t>& data);
		static std::vector<uint8_t> Inflate(const uint8_t* data, size_t size);

	private:
		static constexpr const char* Magic = "USS-SAVE";

		static cs::Ref<SaveHeader> HeaderOf(const cs::Ref<SaveData>& data, const cs::String& slot, const cs::String& label, const cs::DateTime& savedUtc);

		static cs::String NullIfEmpty(const cs::String& s) { return s; } // (null is empty already)
	};

	/// <summary>
	/// The save slots in one folder: a quicksave, as many numbered saves as the player makes, and autosaves
	/// that take turns (the oldest of them is overwritten), each a <c>.sav</c> file named after its slot.
	/// </summary>
	class SPACEGODCORE_API SaveSlots final
	{
	public:
		static constexpr const char* Quick = "quicksave";
		static constexpr int AutosaveCount = 3;

		cs::String Directory;

		explicit SaveSlots(cs::String directory) : Directory(std::move(directory)) {}

		static cs::String Manual(int n);
		static cs::String Autosave(int n);

		static bool IsManual(const cs::String& slot) { return ManualNumber(slot) > 0; }

		static bool IsAutosave(const cs::String& slot) { return cs::Strings::StartsWith(slot, "autosave-"); }

		/// <summary>The numbered saves on disk, in the order they were first made.</summary>
		cs::List<cs::String> ManualSlots() const;

		/// <summary>The slot a new save goes to: the number after the highest there is.</summary>
		cs::String NewManual() const;

		/// <summary>Every slot there is or could be loaded from, in the order a list shows them: the quicksave, the player's saves, the autosaves.</summary>
		cs::List<cs::String> AllSlots() const;

		/// <summary>Removes the save in <paramref name="slot"/>, if there is one.</summary>
		void Delete(const cs::String& slot) const;

		cs::String PathOf(const cs::String& slot) const;

		bool Exists(const cs::String& slot) const;

		/// <summary>The header of the save in <paramref name="slot"/>, or null when it is empty or unreadable.</summary>
		cs::Ref<SaveHeader> Header(const cs::String& slot) const;

		/// <summary>The filled slots, newest first.</summary>
		cs::List<cs::Ref<SaveHeader>> List() const;

		/// <summary>The most recent save in any slot, or null.</summary>
		cs::Ref<SaveHeader> Latest() const;

		cs::Ref<SaveHeader> Save(const cs::String& slot, const cs::Ref<SaveData>& data, const cs::String& label = cs::String());

		cs::Ref<SaveHeader> Save(const cs::String& slot, const cs::Ref<SaveData>& data, const cs::String& label, const cs::DateTime& savedUtc);

		/// <summary>The autosave slot the next autosave takes: an empty one, else the one saved longest ago.</summary>
		cs::String NextAutosave() const;

		cs::Ref<SaveHeader> Autosave(const cs::Ref<SaveData>& data);

		cs::Ref<SaveHeader> Autosave(const cs::Ref<SaveData>& data, const cs::DateTime& savedUtc);

		cs::Ref<SaveData> Load(const cs::String& slot) const;

		cs::Ref<SaveData> Load(const cs::String& slot, cs::Ref<SaveHeader>& header) const;

	private:
		static constexpr const char* ManualPrefix = "slot-";

		static int ManualNumber(const cs::String& slot);
	};
}
