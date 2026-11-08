// US-T236: LoreLibrary, the game's reader of the Prose texts under Data/Lore (Lore/LoreLibrary.h, docs/PROSE.md).

#include "Lore/LoreLibrary.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "Shared/Json/MiniJson.h"

namespace SpaceGod::Core
{
	namespace
	{
		namespace fs = std::filesystem;
		using Shared::Json::JsonArray;
		using Shared::Json::JsonObject;
		using Shared::Json::JsonValue;

		fs::path ToPath(const cs::String& s) { return fs::path(std::u8string(s.begin(), s.end())); }

		cs::String FromPath(const fs::path& p)
		{
			auto u = p.generic_u8string();
			return cs::String(u.begin(), u.end());
		}

		/// The file's text, a UTF-8 byte order mark dropped; false when it cannot be read.
		bool ReadText(const fs::path& file, cs::String& text)
		{
			std::ifstream f(file, std::ios::binary);
			if (!f) return false;
			text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
				static_cast<unsigned char>(text[2]) == 0xBF)
				text.erase(0, 3);
			return true;
		}

		cs::List<cs::String> Strings(const cs::Ref<JsonArray>& a)
		{
			cs::List<cs::String> out;
			if (!a) return out;
			for (const JsonValue& v : *a)
				if (auto s = v.AsString()) out.Add(*s);
			return out;
		}

		/// The .json files of one folder, in ordinal order of their names (directory order differs between file systems).
		std::vector<fs::path> JsonFiles(const fs::path& dir)
		{
			std::vector<fs::path> files;
			std::error_code ec;
			if (!fs::is_directory(dir, ec)) return files;
			for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
			{
				std::error_code fe;
				if (it->is_regular_file(fe) && it->path().extension() == ".json") files.push_back(it->path());
			}
			std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return FromPath(a.filename()) < FromPath(b.filename()); });
			return files;
		}
	}

	const cs::List<cs::String>& LoreLibrary::Kinds()
	{
		static const cs::List<cs::String> kinds{ "bark", "book", "conversation", "description" };
		return kinds;
	}

	LoreLibrary LoreLibrary::Load(const cs::String& folder)
	{
		LoreLibrary lib;
		lib.folder = folder;
		if (folder.empty()) return lib;
		const fs::path root = ToPath(folder);
		for (const cs::String& kind : Kinds())
		{
			for (const fs::path& file : JsonFiles(root / ToPath(kind)))
			{
				const cs::String rel = cs::Cat(kind, "/", FromPath(file.filename()));
				cs::String text;
				if (!ReadText(file, text))
				{
					lib.problems.Add(cs::Cat(rel, ": cannot be read"));
					continue;
				}
				try
				{
					auto o = Shared::Json::MiniJson::ParseObject(text);
					if (o->GetString("schema") != Schema)
					{
						lib.problems.Add(cs::Cat(rel, ": schema is not ", Schema));
						continue;
					}
					LoreText t;
					t.Id = o->GetString("id");
					t.Kind = o->GetString("kind");
					t.Key = o->GetString("key");
					t.Version = o->GetInt("version");
					t.Hash = o->GetString("hash");
					t.About = Strings(o->GetArray("about"));
					t.Speakers = Strings(o->GetArray("speakers"));
					t.Payload = o->GetObject("payload");
					t.File = rel;
					if (t.Id.empty() || t.Key.empty() || !t.Payload)
					{
						lib.problems.Add(cs::Cat(rel, ": needs an id, a key and a payload"));
						continue;
					}
					if (t.Kind != kind)
					{
						lib.problems.Add(cs::Cat(rel, ": kind '", t.Kind, "' is not its folder's"));
						continue;
					}
					auto at = lib.texts.find(t.Key);
					if (at != lib.texts.end())
					{
						lib.problems.Add(cs::Cat(rel, ": key '", t.Key, "' is already ", at->second.File, "'s"));
						continue;
					}
					cs::String key = t.Key;
					lib.texts.emplace(std::move(key), std::move(t));
				}
				catch (const std::exception& ex)
				{
					lib.problems.Add(cs::Cat(rel, ": not JSON (", ex.what(), ")"));
				}
			}
		}
		return lib;
	}

	const LoreText* LoreLibrary::Find(const cs::String& key) const
	{
		auto it = texts.find(key);
		return it == texts.end() ? nullptr : &it->second;
	}

	cs::String LoreLibrary::Field(const cs::String& key, const char* first, const char* second, const cs::String& placeholder) const
	{
		const LoreText* t = Find(key);
		if (t == nullptr || t->Kind != "description") return placeholder;
		cs::String s = t->Payload->GetString(first);
		if (s.empty()) s = t->Payload->GetString(second);
		return s.empty() ? placeholder : s;
	}

	cs::String LoreLibrary::Short(const cs::String& key, const cs::String& placeholder) const { return Field(key, "short", "long", placeholder); }

	cs::String LoreLibrary::Long(const cs::String& key, const cs::String& placeholder) const { return Field(key, "long", "short", placeholder); }

	cs::List<cs::String> LoreLibrary::Lines(const cs::String& key) const
	{
		const LoreText* t = Find(key);
		if (t == nullptr) return {};
		if (t->Kind == "bark") return Strings(t->Payload->GetArray("lines"));
		if (t->Kind == "book") return Strings(t->Payload->GetArray("pages"));
		return {};
	}

	cs::List<cs::String> LoreLibrary::BarkLines(const cs::String& speaker, const cs::String& name) const
	{
		if (speaker.empty()) return {};
		const LoreText* t = Find(cs::Cat(speaker, "/", name));
		if (t != nullptr && t->Kind == "bark") return Strings(t->Payload->GetArray("lines"));
		for (const auto& kv : texts)
		{
			if (kv.second.Kind != "bark") continue;
			for (const auto& s : kv.second.Speakers)
				if (s == speaker)
				{
					auto lines = Strings(kv.second.Payload->GetArray("lines"));
					if (lines.Count() > 0) return lines;
					break;
				}
		}
		return {};
	}

	cs::String LoreLibrary::BarkLine(const cs::String& speaker, const cs::String& name, uint32_t pick, const cs::String& placeholder) const
	{
		auto lines = BarkLines(speaker, name);
		if (lines.Count() == 0) return placeholder;
		const cs::String& line = lines[static_cast<int>(pick % static_cast<uint32_t>(lines.Count()))];
		return line.empty() ? placeholder : line;
	}

	cs::List<cs::String> LoreLibrary::Keys() const
	{
		cs::List<cs::String> keys;
		for (const auto& kv : texts) keys.Add(kv.first);
		return keys;
	}
}
