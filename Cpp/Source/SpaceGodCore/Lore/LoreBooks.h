#pragma once
// US-T261: Prose `book` texts the player finds and reads in the game: a ship's log, a station's records (docs/PROSE.md,
// "Books").
//
// A book is keyed by the entity it was found on: <ship>/log for a ship (salvaged off a derelict or a battle's wreck, or
// found aboard a prize taken in a fight), <settlement>/records for a port's records (read on docking clearance). The game
// recovers a book only when Prose has one with at least one page: no text, no item, never a placeholder book. What has been
// recovered is a list of keys (World::RecoveredLogs, saved), shown by the Runtime's logs screen (Shift+L).
//
// Display only: nothing here reads or changes the simulation, and the recovered list is never read by simulation code.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class LoreLibrary;

	/// <summary>One readable book, as the game shows it.</summary>
	struct SPACEGODCORE_API LoreBook
	{
		/// <summary>The text's key (ship-sol-sfs-resolute/log).</summary>
		cs::String Key;
		/// <summary>The book's title; when Prose gave none, the words of the entity it is about ("Log of Sfs Resolute").</summary>
		cs::String Title;
		/// <summary>Who wrote it, "" when Prose names nobody.</summary>
		cs::String Author;
		/// <summary>Its pages, in order, each non-empty.</summary>
		cs::List<cs::String> Pages;
	};

	class SPACEGODCORE_API LoreBooks final
	{
	public:
		/// <summary>The name of a ship's log (&lt;ship&gt;/log).</summary>
		static constexpr const char* Log = "log";

		/// <summary>The name of a settlement's records (&lt;settlement&gt;/records).</summary>
		static constexpr const char* Records = "records";

		/// <summary>A ship's log: ship-&lt;sys&gt;-&lt;name&gt;/log.</summary>
		static cs::String ShipLog(const cs::String& systemName, const cs::String& shipName);

		/// <summary>A port's records: settlement-&lt;sys&gt;-&lt;name&gt;/records.</summary>
		static cs::String StationRecords(const cs::String& systemName, const cs::String& settlementName);

		/// <summary>
		/// Reads the book keyed <paramref name="key"/>: false (and <paramref name="book"/> untouched) unless the library has a
		/// `book` text there with at least one non-empty page. Empty pages are left out.
		/// </summary>
		static bool Read(const LoreLibrary& lib, const cs::String& key, LoreBook& book);

		/// <summary>Whether <paramref name="key"/> is a readable book (<see cref="Read"/>).</summary>
		static bool Has(const LoreLibrary& lib, const cs::String& key);

		/// <summary>
		/// Adds <paramref name="key"/> to <paramref name="recovered"/> when it is a readable book not already there; true when
		/// it was added (a new find), false for no book or one already recovered.
		/// </summary>
		static bool Recover(cs::List<cs::String>& recovered, const LoreLibrary& lib, const cs::String& key);
	};
}
