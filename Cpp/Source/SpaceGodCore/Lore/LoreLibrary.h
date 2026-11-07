#pragma once
// US-T236: the story text Prose writes for SpaceGod, read into the game (docs/PROSE.md, "The game's side").
//
// Prose (the MindAttic narrative engine) owns every word a player reads as story; the game owns the simulation. Texts the
// author approves in Prose come back through the consumer kit and tools/prose_apply.py as Data/Lore/<kind>/<id>.json
// files in the `spacegod-text/1` shape, committed like any other data. This is the game's only reader of them.
//
// Gameplay never depends on a text being there: a key with no text answers the caller's placeholder, a missing folder is
// an empty library, and a file that cannot be read is listed in Problems() and left out. Nothing here throws.
// Loading is deterministic: files are read in ordinal path order and the library is ordered by key, so the same folder
// gives the same library on every machine (the Bench digests never see it: no simulation code reads lore).

#include "Cs/Cs.h"
#include "Shared/Json/JsonObject.h"

#include <map>

namespace SpaceGod::Core
{
	/// <summary>One applied Prose text (a Data/Lore file, spacegod-text/1).</summary>
	struct LoreText
	{
		/// <summary>Prose's stable text id, e.g. description-planet-sol-earth; also the file's name.</summary>
		cs::String Id;
		/// <summary>description, book, bark or conversation (Prose's text kinds SpaceGod uses).</summary>
		cs::String Kind;
		/// <summary>The game's lookup key: a universe entity id (planet-sol-earth), optionally with /name (pilot-tomas-brennan/hail).</summary>
		cs::String Key;
		/// <summary>Prose's version number and the hash of its payload.</summary>
		int Version = 0;
		cs::String Hash;
		/// <summary>Entity ids the text is about; its speakers (entity ids or $placeholders the game resolves).</summary>
		cs::List<cs::String> About, Speakers;
		/// <summary>The kind's payload as Prose defines it (description: short/long; book: title/author/pages; bark:
		/// speaker/context/lines; conversation: title/start/nodes).</summary>
		cs::Ref<Shared::Json::JsonObject> Payload;
		/// <summary>The file it came from, relative to the library's folder, with forward slashes.</summary>
		cs::String File;
	};

	/// <summary>Every applied Prose text of a Data/Lore folder, found by key.</summary>
	class SPACEGODCORE_API LoreLibrary
	{
	public:
		/// <summary>The file format tools/prose_apply.py writes.</summary>
		static constexpr const char* Schema = "spacegod-text/1";

		/// <summary>The kinds the library reads, one sub-folder each.</summary>
		static const cs::List<cs::String>& Kinds();

		/// <summary>Reads every &lt;kind&gt;/*.json under <paramref name="folder"/>. Never throws.</summary>
		static LoreLibrary Load(const cs::String& folder);

		/// <summary>How many texts were loaded.</summary>
		int Count() const { return static_cast<int>(texts.size()); }

		/// <summary>The text with this key, or null.</summary>
		const LoreText* Find(const cs::String& key) const;

		/// <summary>Whether a text has this key.</summary>
		bool Has(const cs::String& key) const { return Find(key) != nullptr; }

		/// <summary>A description's short line (its long text when it has no short one), else <paramref name="placeholder"/>.</summary>
		cs::String Short(const cs::String& key, const cs::String& placeholder) const;

		/// <summary>A description's long text (its short line when it has no long one), else <paramref name="placeholder"/>.</summary>
		cs::String Long(const cs::String& key, const cs::String& placeholder) const;

		/// <summary>A bark's lines (or a book's pages), empty when there is no such text.</summary>
		cs::List<cs::String> Lines(const cs::String& key) const;

		/// <summary>Every key, in ordinal order.</summary>
		cs::List<cs::String> Keys() const;

		/// <summary>The folder it was loaded from.</summary>
		const cs::String& Folder() const { return folder; }

		/// <summary>Files that were left out and why (unreadable, not spacegod-text/1, a key already taken), in file order.</summary>
		const cs::List<cs::String>& Problems() const { return problems; }

	private:
		cs::String folder;
		std::map<cs::String, LoreText> texts; // by key, ordinal
		cs::List<cs::String> problems;

		cs::String Field(const cs::String& key, const char* first, const char* second, const cs::String& placeholder) const;
	};
}
