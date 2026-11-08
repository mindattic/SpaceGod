// Port of Unity.SpaceGod Core/Persistence/SaveGame.cs (US-T20).

#include "Persistence/SaveGame.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include "Astro.h"
#include "NewGameSetup.h" // US-T247
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"

#if defined(SPACEGOD_WITH_ZLIB)
#include "zlib.h"
#define SPACEGOD_HAS_DEFLATE 1
#elif defined(SPACEGOD_WITH_MINIZ)
#include "miniz.h"
#define SPACEGOD_HAS_DEFLATE 1
#else
#define SPACEGOD_HAS_DEFLATE 0
#endif

namespace SpaceGod::Core
{
	namespace
	{
		namespace fs = std::filesystem;

		/// A UTF-8 cs::String as a path (std::filesystem would read a narrow string in the ANSI code page on Windows).
		fs::path ToPath(const cs::String& s) { return fs::path(std::u8string(s.begin(), s.end())); }

		cs::String FromPath(const fs::path& p)
		{
			auto u = p.u8string();
			return cs::String(u.begin(), u.end());
		}

		// ---- BinaryReader over a std::istream (the header is read straight off the stream, as the C# does)

		uint8_t ReadByte(std::istream& s)
		{
			int c = s.get();
			if (c == std::char_traits<char>::eof()) throw EndOfStreamException("Unable to read beyond the end of the stream.");
			return static_cast<uint8_t>(c);
		}

		void ReadRaw(std::istream& s, void* out, size_t n)
		{
			s.read(static_cast<char*>(out), static_cast<std::streamsize>(n));
			if (static_cast<size_t>(s.gcount()) != n) throw EndOfStreamException("Unable to read beyond the end of the stream.");
		}

		int32_t ReadInt32(std::istream& s)
		{
			int32_t v;
			ReadRaw(s, &v, 4);
			return v;
		}

		int64_t ReadInt64(std::istream& s)
		{
			int64_t v;
			ReadRaw(s, &v, 8);
			return v;
		}

		double ReadDouble(std::istream& s)
		{
			double v;
			ReadRaw(s, &v, 8);
			return v;
		}

		cs::String ReadString(std::istream& s)
		{
			uint32_t len = 0;
			int shift = 0;
			uint8_t b;
			do
			{
				if (shift == 35) throw cs::FormatException("Too many bytes in what should have been a 7-bit encoded integer.");
				b = ReadByte(s);
				len |= static_cast<uint32_t>(b & 0x7f) << shift;
				shift += 7;
			} while ((b & 0x80) != 0);
			if (static_cast<int32_t>(len) < 0) throw cs::IOException("Invalid string length");
			cs::String str(len, '\0');
			if (len > 0) ReadRaw(s, str.data(), len);
			return str;
		}

		void WriteBytes(std::ostream& s, const std::vector<uint8_t>& b)
		{
			s.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
			if (!s) throw cs::IOException("Could not write the save");
		}

		/// int.TryParse(s, NumberStyles.None, CultureInfo.InvariantCulture, out n): digits only.
		bool TryParseDigits(const cs::String& s, int& n)
		{
			if (s.empty()) return false;
			long long v = 0;
			for (char c : s)
			{
				if (c < '0' || c > '9') return false;
				v = v * 10 + (c - '0');
				if (v > cs::Int::MaxValue) return false;
			}
			n = static_cast<int>(v);
			return true;
		}
	}

	// ================================================================ SaveHeader

	cs::DateTime SaveHeader::SimUtc() const { return Astro::ToDateTime(SimJd); }

	// ================================================================ SaveGame

	void SaveGame::Write(std::ostream& stream, const cs::Ref<SaveData>& data, const cs::String& slot, const cs::String& label, const cs::DateTime& savedUtc)
	{
		Graph::ByteWriter w;
		w.Str(Magic);
		w.I32(Format);
		auto h = HeaderOf(data, slot, label, savedUtc);
		w.Str(h->Slot);
		w.Str(h->Label);
		w.I64(h->SavedUtc.ToBinary());
		w.F64(h->SimJd);
		w.Str(h->ActiveSystem);
		w.Str(h->PilotedShip);
		w.F64(h->Credits);
		w.Str(h->Setup); // US-T247 (format 2)
		WriteBytes(stream, w.Bytes);
		auto graph = ObjectGraph::ToBytes(data);
		WriteBytes(stream, Deflate(graph.Raw()));
	}

	cs::Ref<SaveHeader> SaveGame::ReadHeader(std::istream& stream)
	{
		cs::String magic;
		try { magic = ReadString(stream); }
		catch (const EndOfStreamException&) { throw InvalidDataException("Not a save file"); }
		if (magic != Magic) throw InvalidDataException("Not a save file");
		int format = ReadInt32(stream);
		if (format != Format && format != 1) throw InvalidDataException(cs::Cat("A save from another version (format ", format, ")"));
		auto h = cs::New<SaveHeader>();
		h->Format = format;
		h->Slot = ReadString(stream);
		h->Label = ReadString(stream);
		h->SavedUtc = cs::DateTime::FromBinary(ReadInt64(stream));
		h->SimJd = ReadDouble(stream);
		h->ActiveSystem = ReadString(stream);
		h->PilotedShip = NullIfEmpty(ReadString(stream));
		h->Credits = ReadDouble(stream);
		if (format >= 2) h->Setup = ReadString(stream); // US-T247
		return h;
	}

	cs::Ref<SaveData> SaveGame::Read(std::istream& stream, cs::Ref<SaveHeader>& header)
	{
		header = ReadHeader(stream);
		std::vector<uint8_t> compressed((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		auto graph = Inflate(compressed.data(), compressed.size());
		auto data = ObjectGraph::Read(graph.data(), graph.size()).As<SaveData>();
		if (data == nullptr) throw InvalidDataException("The save holds no game");
		return data;
	}

	cs::List<uint8_t> SaveGame::ToBytes(const cs::Ref<SaveData>& data, const cs::String& slot, const cs::String& label) { return ToBytes(data, slot, label, cs::DateTime::UtcNow()); }

	cs::List<uint8_t> SaveGame::ToBytes(const cs::Ref<SaveData>& data, const cs::String& slot, const cs::String& label, const cs::DateTime& savedUtc)
	{
		std::ostringstream ms(std::ios::binary);
		Write(ms, data, slot, label, savedUtc);
		const std::string s = ms.str();
		return cs::List<uint8_t>(std::vector<uint8_t>(s.begin(), s.end()));
	}

	cs::Ref<SaveData> SaveGame::FromBytes(const cs::List<uint8_t>& bytes)
	{
		const auto& raw = bytes.Raw();
		std::istringstream ms(std::string(raw.begin(), raw.end()), std::ios::binary);
		cs::Ref<SaveHeader> header;
		return Read(ms, header);
	}

	void SaveGame::WriteFile(const cs::String& path, const cs::List<uint8_t>& bytes)
	{
		auto full = fs::absolute(ToPath(path));
		fs::create_directories(full.parent_path());
		fs::path target = ToPath(path);
		fs::path temp = ToPath(cs::Cat(path, ".tmp"));
		{
			std::ofstream f(temp, std::ios::binary | std::ios::trunc);
			if (!f) throw cs::IOException(cs::Cat("Could not write ", FromPath(temp)));
			const auto& raw = bytes.Raw();
			f.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
			if (!f) throw cs::IOException(cs::Cat("Could not write ", FromPath(temp)));
		}
		// Swapped in one step: deleting the old first would leave no save at all if the move then failed.
		// (std::filesystem::rename replaces an existing target in one step, as File.Replace / File.Move do.)
		std::error_code ec;
		fs::rename(temp, target, ec);
		if (ec) throw cs::IOException(cs::Cat("Could not replace ", path, ": ", ec.message()));
	}

	cs::Ref<SaveHeader> SaveGame::HeaderOf(const cs::Ref<SaveData>& data, const cs::String& slot, const cs::String& label, const cs::DateTime& savedUtc)
	{
		auto piloted = data->Session ? data->Session->Piloted : nullptr;
		auto h = cs::New<SaveHeader>();
		h->Format = Format;
		h->Slot = slot;
		h->Label = label;
		h->SavedUtc = savedUtc;
		h->SimJd = data->Clock ? data->Clock->Jd : 0;
		h->ActiveSystem = data->Session ? data->Session->ActiveSystem : cs::String();
		h->PilotedShip = piloted ? piloted->Name : cs::String();
		h->Credits = piloted && piloted->Faction ? piloted->Faction->Treasury : 0;
		if (data->Galaxy) h->Setup = NewGameSetup::Summary(data->Galaxy->SetupRecord, data->Galaxy->Seed, data->Galaxy->DifficultyRecord); // US-T247
		return h;
	}

	std::vector<uint8_t> SaveGame::Deflate(const std::vector<uint8_t>& data)
	{
#if SPACEGOD_HAS_DEFLATE
		z_stream z{};
		// CompressionLevel.Fastest is zlib's level 1; windowBits -15 is raw DEFLATE, as DeflateStream writes.
		if (deflateInit2(&z, 1, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) throw cs::IOException("deflateInit2 failed");
		std::vector<uint8_t> out(deflateBound(&z, static_cast<uLong>(data.size())) + 16);
		z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data.data()));
		z.avail_in = static_cast<uInt>(data.size());
		z.next_out = reinterpret_cast<Bytef*>(out.data());
		z.avail_out = static_cast<uInt>(out.size());
		int rc = deflate(&z, Z_FINISH);
		size_t written = static_cast<size_t>(z.total_out);
		deflateEnd(&z);
		if (rc != Z_STREAM_END) throw cs::IOException("deflate failed");
		out.resize(written);
		return out;
#else
		(void)data;
		throw cs::NotSupportedException("SaveGame needs DEFLATE: build with SPACEGOD_WITH_ZLIB (Unreal's zlib) or SPACEGOD_WITH_MINIZ (see docs/PERSISTENCE.md)");
#endif
	}

	std::vector<uint8_t> SaveGame::Inflate(const uint8_t* data, size_t size)
	{
#if SPACEGOD_HAS_DEFLATE
		z_stream z{};
		if (inflateInit2(&z, -15) != Z_OK) throw cs::IOException("inflateInit2 failed");
		std::vector<uint8_t> out;
		std::vector<uint8_t> chunk(1 << 16);
		z.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
		z.avail_in = static_cast<uInt>(size);
		int rc;
		do
		{
			z.next_out = reinterpret_cast<Bytef*>(chunk.data());
			z.avail_out = static_cast<uInt>(chunk.size());
			rc = inflate(&z, Z_NO_FLUSH);
			if (rc != Z_OK && rc != Z_STREAM_END)
			{
				inflateEnd(&z);
				throw InvalidDataException("The save's compressed data is damaged");
			}
			out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(chunk.size() - z.avail_out));
			if (rc == Z_OK && z.avail_in == 0 && z.avail_out != 0) break; // the input ran out: what there is (DeflateStream reads to its end)
		} while (rc != Z_STREAM_END);
		inflateEnd(&z);
		return out;
#else
		(void)data;
		(void)size;
		throw cs::NotSupportedException("SaveGame needs DEFLATE: build with SPACEGOD_WITH_ZLIB (Unreal's zlib) or SPACEGOD_WITH_MINIZ (see docs/PERSISTENCE.md)");
#endif
	}

	// ================================================================ SaveSlots

	cs::String SaveSlots::Manual(int n) { return cs::Cat(ManualPrefix, n); }
	cs::String SaveSlots::Autosave(int n) { return cs::Cat("autosave-", n); }

	int SaveSlots::ManualNumber(const cs::String& slot)
	{
		int n = 0;
		return !slot.empty() && cs::Strings::StartsWith(slot, ManualPrefix)
			&& TryParseDigits(cs::Strings::Substring(slot, static_cast<int>(std::char_traits<char>::length(ManualPrefix))), n)
			&& n > 0 ? n : 0;
	}

	cs::List<cs::String> SaveSlots::ManualSlots() const
	{
		cs::List<cs::String> names;
		std::error_code ec;
		if (!fs::is_directory(ToPath(Directory), ec)) return names;
		// Directory.GetFiles(Directory, "slot-*.sav"), then GetFileNameWithoutExtension.
		for (const auto& entry : fs::directory_iterator(ToPath(Directory), ec))
		{
			if (!entry.is_regular_file(ec)) continue;
			cs::String file = FromPath(entry.path().filename());
			if (!cs::Strings::StartsWith(file, ManualPrefix)) continue;
			cs::String ext = FromPath(entry.path().extension());
			if (!cs::Strings::EqualsOrdinalIgnoreCase(ext, ".sav")) continue;
			names.Add(FromPath(entry.path().stem()));
		}
		cs::List<cs::String> manual;
		for (const auto& n : names)
			if (IsManual(n)) manual.Add(n);
		return cs::OrderBy(manual, [](const cs::String& s) { return ManualNumber(s); });
	}

	cs::String SaveSlots::NewManual() const
	{
		int max = 0;
		bool any = false;
		for (const auto& s : ManualSlots())
		{
			int n = ManualNumber(s);
			max = any ? cs::Math::Max(max, n) : n;
			any = true;
		}
		return Manual(max + 1);
	}

	cs::List<cs::String> SaveSlots::AllSlots() const
	{
		cs::List<cs::String> list;
		list.Add(Quick);
		for (const auto& slot : ManualSlots()) list.Add(slot);
		for (int i = 1; i <= AutosaveCount; i++) list.Add(Autosave(i));
		return list;
	}

	void SaveSlots::Delete(const cs::String& slot) const
	{
		if (Exists(slot))
		{
			std::error_code ec;
			fs::remove(ToPath(PathOf(slot)), ec);
			if (ec) throw cs::IOException(cs::Cat("Could not delete ", PathOf(slot), ": ", ec.message()));
		}
	}

	cs::String SaveSlots::PathOf(const cs::String& slot) const { return FromPath(ToPath(Directory) / ToPath(cs::Cat(slot, ".sav"))); }

	bool SaveSlots::Exists(const cs::String& slot) const
	{
		std::error_code ec;
		return fs::is_regular_file(ToPath(PathOf(slot)), ec);
	}

	cs::Ref<SaveHeader> SaveSlots::Header(const cs::String& slot) const
	{
		cs::String path = PathOf(slot);
		std::error_code ec;
		if (!fs::is_regular_file(ToPath(path), ec)) return nullptr;
		try
		{
			std::ifstream f(ToPath(path), std::ios::binary);
			if (!f) return nullptr; // (UnauthorizedAccessException / IOException)
			auto h = SaveGame::ReadHeader(f);
			h->Slot = slot;
			h->Path = path;
			return h;
		}
		// A corrupt header can also fail as a bad string length (FormatException) or out-of-range timestamp (ArgumentException).
		catch (const cs::IOException&) { return nullptr; }          // (EndOfStreamException too)
		catch (const InvalidDataException&) { return nullptr; }
		catch (const cs::FormatException&) { return nullptr; }
		catch (const cs::ArgumentException&) { return nullptr; }
	}

	cs::List<cs::Ref<SaveHeader>> SaveSlots::List() const
	{
		cs::List<cs::Ref<SaveHeader>> found;
		for (const auto& slot : AllSlots())
			if (auto h = Header(slot)) found.Add(h);
		return cs::OrderByDescending(found, [](const cs::Ref<SaveHeader>& h) { return h->SavedUtc; });
	}

	cs::Ref<SaveHeader> SaveSlots::Latest() const
	{
		auto list = List();
		return list.Count() > 0 ? list[0] : nullptr;
	}

	cs::Ref<SaveHeader> SaveSlots::Save(const cs::String& slot, const cs::Ref<SaveData>& data, const cs::String& label) { return Save(slot, data, label, cs::DateTime::UtcNow()); }

	cs::Ref<SaveHeader> SaveSlots::Save(const cs::String& slot, const cs::Ref<SaveData>& data, const cs::String& label, const cs::DateTime& savedUtc)
	{
		// (`label ?? slot`: a null label is empty here)
		SaveGame::WriteFile(PathOf(slot), SaveGame::ToBytes(data, slot, label.empty() ? slot : label, savedUtc));
		return Header(slot);
	}

	cs::String SaveSlots::NextAutosave() const
	{
		cs::String oldest;
		cs::DateTime oldestAt = cs::DateTime::MaxValue;
		for (int i = 1; i <= AutosaveCount; i++)
		{
			auto slot = Autosave(i);
			auto h = Header(slot);
			if (h == nullptr) return slot;
			if (h->SavedUtc.CompareTo(oldestAt) < 0)
			{
				oldestAt = h->SavedUtc;
				oldest = slot;
			}
		}
		return oldest;
	}

	cs::Ref<SaveHeader> SaveSlots::Autosave(const cs::Ref<SaveData>& data) { return Autosave(data, cs::DateTime::UtcNow()); }

	cs::Ref<SaveHeader> SaveSlots::Autosave(const cs::Ref<SaveData>& data, const cs::DateTime& savedUtc) { return Save(NextAutosave(), data, "Autosave", savedUtc); }

	cs::Ref<SaveData> SaveSlots::Load(const cs::String& slot) const
	{
		cs::Ref<SaveHeader> header;
		return Load(slot, header);
	}

	cs::Ref<SaveData> SaveSlots::Load(const cs::String& slot, cs::Ref<SaveHeader>& header) const
	{
		std::ifstream f(ToPath(PathOf(slot)), std::ios::binary);
		if (!f) throw cs::IOException(cs::Cat("Could not open ", PathOf(slot)));
		auto data = SaveGame::Read(f, header);
		header->Slot = slot;
		header->Path = PathOf(slot);
		return data;
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)
	// (PilotState, SimClock, Galaxy, Ship register in their own files.)

	SG_SERIALIZE(SaveSession, "Unity.SpaceGod.Core.SaveSession",
		SG_F(ActiveSystem), SG_F(Piloted), SG_F(PilotFrameBody), SG_F(Pilot), SG_F(PilotAim), SG_F(FlightAssistOff), SG_F(ScaleGoal))

	SG_SERIALIZE(SaveData, "Unity.SpaceGod.Core.SaveData",
		SG_F(Clock), SG_F(Galaxy), SG_F(Session))

	SG_SERIALIZE(SaveHeader, "Unity.SpaceGod.Core.SaveHeader",
		SG_F(Format), SG_F(Slot), SG_F(Label), SG_F(SavedUtc), SG_F(SimJd), SG_F(ActiveSystem), SG_F(PilotedShip), SG_F(Credits), SG_F(Path))
}
