// C++-only helpers for the Profiles port: see SharedIo.h.

#include "Shared/Profiles/SharedIo.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace SpaceGod::Shared::Io
{
	namespace
	{
		fs::path P(const cs::String& s) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }
		cs::String S(const fs::path& p) { std::u8string u = p.u8string(); return cs::String(reinterpret_cast<const char*>(u.data()), u.size()); }
		[[noreturn]] void Fail(const char* what, const cs::String& path, const std::error_code& ec)
		{
			throw cs::IOException(cs::Cat(what, " '", path, "': ", ec.message()));
		}
	}

	cs::String GetFullPath(const cs::String& path)
	{
		if (path.empty()) throw cs::ArgumentException("The path is empty.");
		std::error_code ec;
		fs::path full = fs::absolute(P(path), ec);
		if (ec) Fail("Can't resolve", path, ec);
		full = full.lexically_normal();
		full.make_preferred();
		cs::String r = S(full);
		// GetFullPath keeps no trailing separator (except on a root).
		while (r.size() > 3 && (r.back() == '\\' || r.back() == '/')) r.pop_back();
		return r;
	}

	cs::String Combine(const cs::String& a, const cs::String& b)
	{
		if (b.empty()) return a;
		if (a.empty() || P(b).is_absolute()) return b;
		char last = a.back();
		if (last == '\\' || last == '/' || last == ':') return a + b;
#ifdef _WIN32
		return a + "\\" + b;
#else
		return a + "/" + b;
#endif
	}

	cs::String GetFileName(const cs::String& path)
	{
		size_t i = path.find_last_of("\\/:");
		return i == cs::String::npos ? path : path.substr(i + 1);
	}

	cs::String GetDirectoryName(const cs::String& path)
	{
		fs::path p = P(path).parent_path();
		return S(p);
	}

	bool FileExists(const cs::String& path)
	{
		std::error_code ec;
		return !path.empty() && fs::is_regular_file(P(path), ec);
	}

	bool DirectoryExists(const cs::String& path)
	{
		std::error_code ec;
		return !path.empty() && fs::is_directory(P(path), ec);
	}

	void CreateFolder(const cs::String& path)
	{
		std::error_code ec;
		fs::create_directories(P(path), ec);
		if (ec && !fs::is_directory(P(path))) Fail("Can't create the folder", path, ec);
	}

	cs::List<cs::String> GetDirectories(const cs::String& path)
	{
		cs::List<cs::String> r;
		std::error_code ec;
		for (fs::directory_iterator it(P(path), ec), end; !ec && it != end; it.increment(ec))
		{
			std::error_code e2;
			if (it->is_directory(e2)) r.Add(S(it->path()));
		}
		if (ec) Fail("Can't list", path, ec);
		return r;
	}

	void MoveFolder(const cs::String& from, const cs::String& to)
	{
		std::error_code ec;
		if (fs::exists(P(to), ec)) throw cs::IOException(cs::Cat("Cannot create '", to, "' because a file or directory with the same name already exists."));
		fs::rename(P(from), P(to), ec);
		if (ec) Fail("Can't move", from, ec);
	}

	void MoveFileTo(const cs::String& from, const cs::String& to)
	{
		std::error_code ec;
		if (fs::exists(P(to), ec)) throw cs::IOException(cs::Cat("Cannot create '", to, "' because a file or directory with the same name already exists."));
		fs::rename(P(from), P(to), ec);
		if (ec) Fail("Can't move", from, ec);
	}

	void ReplaceFileWith(const cs::String& from, const cs::String& to)
	{
		// std::filesystem::rename replaces an existing file in one step (MoveFileExW REPLACE_EXISTING / rename(2)).
		std::error_code ec;
		fs::rename(P(from), P(to), ec);
		if (ec) Fail("Can't replace", to, ec);
	}

	void RemoveFile(const cs::String& path)
	{
		std::error_code ec;
		fs::remove(P(path), ec);
		if (ec) Fail("Can't delete", path, ec);
	}

	cs::String ReadAllText(const cs::String& path)
	{
		std::ifstream in(P(path), std::ios::binary);
		if (!in) throw cs::IOException(cs::Cat("Can't read '", path, "'."));
		cs::String text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (in.bad()) throw cs::IOException(cs::Cat("Can't read '", path, "'."));
		if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) text.erase(0, 3);
		return text;
	}

	void WriteAllBytesDurable(const cs::String& path, const cs::List<uint8_t>& bytes)
	{
#ifdef _WIN32
		FILE* f = nullptr;
		if (_wfopen_s(&f, P(path).c_str(), L"wb") != 0) f = nullptr;
#else
		FILE* f = std::fopen(P(path).c_str(), "wb");
#endif
		if (!f) throw cs::IOException(cs::Cat("Can't write '", path, "'."));
		bool ok = bytes.Count() == 0 || std::fwrite(bytes.Raw().data(), 1, bytes.Raw().size(), f) == bytes.Raw().size();
		ok = std::fflush(f) == 0 && ok;
#ifdef _WIN32
		ok = _commit(_fileno(f)) == 0 && ok;
#else
		ok = fsync(fileno(f)) == 0 && ok;
#endif
		ok = std::fclose(f) == 0 && ok;
		if (!ok) throw cs::IOException(cs::Cat("Can't write '", path, "'."));
	}

	cs::String EnvironmentVariable(const cs::String& name)
	{
#ifdef _WIN32
		char* value = nullptr;
		size_t len = 0;
		if (_dupenv_s(&value, &len, name.c_str()) != 0 || !value) return cs::String();
		cs::String r(value);
		std::free(value);
		return r;
#else
		const char* v = std::getenv(name.c_str());
		return v ? cs::String(v) : cs::String();
#endif
	}

	namespace
	{
		// Decodes one UTF-8 code point at s[i] (advancing i); bad bytes decode as U+FFFD.
		unsigned Next(const cs::String& s, size_t& i)
		{
			unsigned char c = static_cast<unsigned char>(s[i++]);
			if (c < 0x80) return c;
			int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
			if (extra < 0) return 0xFFFD;
			unsigned cp = c & (0x3F >> extra);
			for (int k = 0; k < extra; k++)
			{
				if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return 0xFFFD;
				cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
			}
			return cp;
		}
	}

	int Utf16Length(const cs::String& s)
	{
		int n = 0;
		for (size_t i = 0; i < s.size();) n += Next(s, i) >= 0x10000 ? 2 : 1;
		return n;
	}

	cs::List<uint16_t> Utf16Units(const cs::String& s)
	{
		cs::List<uint16_t> r;
		for (size_t i = 0; i < s.size();)
		{
			unsigned cp = Next(s, i);
			if (cp >= 0x10000)
			{
				cp -= 0x10000;
				r.Add(static_cast<uint16_t>(0xD800 + (cp >> 10)));
				r.Add(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
			}
			else r.Add(static_cast<uint16_t>(cp));
		}
		return r;
	}

	cs::String Utf16Prefix(const cs::String& s, int units)
	{
		int n = 0;
		size_t i = 0;
		while (i < s.size())
		{
			size_t at = i;
			int w = Next(s, i) >= 0x10000 ? 2 : 1;
			if (n + w > units) return s.substr(0, at);
			n += w;
		}
		return s;
	}

	cs::String NewGuidN()
	{
		static thread_local std::mt19937_64 rng{ std::random_device{}() ^ (static_cast<uint64_t>(std::random_device{}()) << 32) };
		const char* hex = "0123456789abcdef";
		cs::String r;
		for (int k = 0; k < 2; k++)
		{
			uint64_t v = rng();
			for (int b = 0; b < 16; b++) { r.push_back(hex[v & 0xF]); v >>= 4; }
		}
		return r;
	}
}
