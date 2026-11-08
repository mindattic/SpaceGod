// US-T261: LoreBooks, the Prose books the player recovers and reads (Lore/LoreBooks.h, docs/PROSE.md "Books").

#include "Lore/LoreBooks.h"

#include "Lore/Encounters.h"
#include "Lore/LoreKeys.h"
#include "Lore/LoreLibrary.h"
#include "Shared/Json/JsonObject.h"

namespace SpaceGod::Core
{
	namespace
	{
		bool Blank(const cs::String& s)
		{
			for (unsigned char c : s)
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
			return true;
		}
	}

	cs::String LoreBooks::ShipLog(const cs::String& systemName, const cs::String& shipName)
	{
		return LoreKeys::Named(LoreKeys::Ship(systemName, shipName), Log);
	}

	cs::String LoreBooks::StationRecords(const cs::String& systemName, const cs::String& settlementName)
	{
		return LoreKeys::Named(LoreKeys::Settlement(systemName, settlementName), Records);
	}

	bool LoreBooks::Read(const LoreLibrary& lib, const cs::String& key, LoreBook& book)
	{
		if (key.empty()) return false;
		const LoreText* t = lib.Find(key);
		if (t == nullptr || t->Kind != "book" || !t->Payload) return false;
		cs::List<cs::String> pages;
		for (const auto& p : lib.Lines(key))
			if (!Blank(p)) pages.Add(p);
		if (pages.Count() == 0) return false;

		book.Key = key;
		book.Pages = pages;
		book.Author = t->Payload->GetString("author");
		book.Title = t->Payload->GetString("title");
		if (Blank(book.Title))
		{
			auto slash = key.find('/');
			cs::String entity = slash == cs::String::npos ? key : key.substr(0, slash);
			cs::String name = slash == cs::String::npos ? cs::String() : key.substr(slash + 1);
			book.Title = cs::Cat(name == Records ? "Records of " : "Log of ", Encounters::EntityWords(entity));
		}
		return true;
	}

	bool LoreBooks::Has(const LoreLibrary& lib, const cs::String& key)
	{
		LoreBook book;
		return Read(lib, key, book);
	}

	bool LoreBooks::Recover(cs::List<cs::String>& recovered, const LoreLibrary& lib, const cs::String& key)
	{
		if (recovered.Contains(key) || !Has(lib, key)) return false;
		recovered.Add(key);
		return true;
	}
}
