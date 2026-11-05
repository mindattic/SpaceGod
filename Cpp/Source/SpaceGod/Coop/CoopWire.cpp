// US-T44: CoopWire.cs (US-T39) in C++; see CoopWire.h.

#include "Coop/CoopWire.h"

#include "SimClock.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace SpaceGod::Coop
{
	namespace
	{
		// The eased pause's flags are private to SimClock and the Core stays unchanged: an explicit instantiation may name
		// a private member, and hands its pointer out through a friend (the same seam StaticTables.cpp uses, US-T41).
		template <class Tag, typename Tag::type M>
		struct Expose
		{
			friend typename Tag::type Member(Tag) { return M; }
		};

		struct PausedField { using type = bool Core::SimClock::*; friend type Member(PausedField); };
		struct PausingField { using type = bool Core::SimClock::*; friend type Member(PausingField); };
		struct ResumingField { using type = bool Core::SimClock::*; friend type Member(ResumingField); };

		template struct Expose<PausedField, &Core::SimClock::paused>;
		template struct Expose<PausingField, &Core::SimClock::pausing>;
		template struct Expose<ResumingField, &Core::SimClock::resuming>;

		const char* const Base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		int Base64Value(char c)
		{
			if (c >= 'A' && c <= 'Z') return c - 'A';
			if (c >= 'a' && c <= 'z') return c - 'a' + 26;
			if (c >= '0' && c <= '9') return c - '0' + 52;
			if (c == '+') return 62;
			if (c == '/') return 63;
			return -1;
		}
	}

	// ---------------------------------------------------------------- ClockState

	ClockState ClockState::Of(const Core::SimClock& c)
	{
		ClockState s;
		s.Jd = c.Jd;
		s.Warp = c.WarpIndex;
		s.Paused = c.Paused();
		s.Pausing = c.Pausing();
		s.Resuming = c.Resuming();
		s.Speed = c.Speed;
		return s;
	}

	void ClockState::ApplyTo(Core::SimClock& c) const
	{
		c.SetJd(Jd);
		c.SetWarp(Warp);
		c.*Member(PausedField{}) = Paused;
		c.*Member(PausingField{}) = Pausing;
		c.*Member(ResumingField{}) = Resuming;
		c.Speed = Speed;
	}

	cs::String ClockState::Write() const
	{
		cs::String flags;
		flags += Paused ? '1' : '0';
		flags += Pausing ? '1' : '0';
		flags += Resuming ? '1' : '0';
		return cs::Cat(Wire::D(Jd), " ", Wire::I(Warp), " ", flags, " ", Wire::D(Speed));
	}

	ClockState ClockState::Read(const std::vector<cs::String>& f, int at)
	{
		ClockState s;
		s.Jd = Wire::ParseD(f[at]);
		s.Warp = Wire::ParseI(f[at + 1]);
		const cs::String& flags = f[at + 2];
		if (flags.size() < 3) throw std::runtime_error("bad clock flags '" + flags + "'");
		s.Paused = flags[0] == '1';
		s.Pausing = flags[1] == '1';
		s.Resuming = flags[2] == '1';
		s.Speed = Wire::ParseD(f[at + 3]);
		return s;
	}

	// ---------------------------------------------------------------- FrameRecord

	cs::String FrameRecord::Write() const
	{
		cs::String s;
		s.reserve(160 + 40 * (Entries.size() + Post.size()));
		s += "frame ";
		s += Wire::L(Frame);
		s += ' ';
		s += Wire::F(Dt);
		s += ' ';
		s += Before.Write();
		s += ' ';
		s += Wire::D(JdAfter);
		s += ' ';
		s += Adventures ? '1' : '0';
		s += ' ';
		s += Wire::D(AdventurePoint.X);
		s += ' ';
		s += Wire::D(AdventurePoint.Y);
		s += ' ';
		s += Wire::D(AdventurePoint.Z);
		for (const auto& e : Entries) { s += "\ne "; s += e; }
		for (const auto& p : Post) { s += "\np "; s += p; }
		return s;
	}

	FrameRecord FrameRecord::Read(const cs::String& text)
	{
		auto lines = Wire::Split(text, '\n');
		auto f = Wire::Split(lines[0], ' ');
		if (f.size() < 12 || f[0] != "frame") throw std::runtime_error("Not a frame: '" + lines[0] + "'");
		FrameRecord r;
		r.Frame = Wire::ParseL(f[1]);
		r.Dt = Wire::ParseF(f[2]);
		r.Before = ClockState::Read(f, 3);
		r.JdAfter = Wire::ParseD(f[7]);
		r.Adventures = f[8] == "1";
		r.AdventurePoint = Core::DVec3(Wire::ParseD(f[9]), Wire::ParseD(f[10]), Wire::ParseD(f[11]));
		for (size_t i = 1; i < lines.size(); i++)
		{
			const auto& l = lines[i];
			if (Wire::StartsWith(l, "e ")) r.Entries.push_back(l.substr(2));
			else if (Wire::StartsWith(l, "p ")) r.Post.push_back(l.substr(2));
		}
		return r;
	}

	// ---------------------------------------------------------------- Wire

	namespace Wire
	{
		cs::String D(double v)
		{
			uint64_t bits;
			std::memcpy(&bits, &v, sizeof bits);
			return Hex(bits);
		}

		double ParseD(const cs::String& s)
		{
			char* end = nullptr;
			uint64_t bits = std::strtoull(s.c_str(), &end, 16);
			if (s.empty() || end == s.c_str() || *end != '\0') throw std::runtime_error("bad double bits '" + s + "'");
			double v;
			std::memcpy(&v, &bits, sizeof v);
			return v;
		}

		cs::String F(float v)
		{
			uint32_t bits;
			std::memcpy(&bits, &v, sizeof bits);
			char buf[16];
			std::snprintf(buf, sizeof buf, "%08X", bits);
			return buf;
		}

		float ParseF(const cs::String& s)
		{
			char* end = nullptr;
			uint32_t bits = static_cast<uint32_t>(std::strtoul(s.c_str(), &end, 16));
			if (s.empty() || end == s.c_str() || *end != '\0') throw std::runtime_error("bad float bits '" + s + "'");
			float v;
			std::memcpy(&v, &bits, sizeof v);
			return v;
		}

		cs::String I(int v) { return std::to_string(v); }

		int ParseI(const cs::String& s)
		{
			char* end = nullptr;
			long v = std::strtol(s.c_str(), &end, 10);
			if (s.empty() || end == s.c_str() || *end != '\0') throw std::runtime_error("bad int '" + s + "'");
			return static_cast<int>(v);
		}

		cs::String L(long long v) { return std::to_string(v); }

		long long ParseL(const cs::String& s)
		{
			char* end = nullptr;
			long long v = std::strtoll(s.c_str(), &end, 10);
			if (s.empty() || end == s.c_str() || *end != '\0') throw std::runtime_error("bad long '" + s + "'");
			return v;
		}

		std::vector<cs::String> Pack(const cs::List<uint8_t>& save)
		{
			const auto& bytes = save.Raw();
			cs::String b64;
			b64.reserve((bytes.size() + 2) / 3 * 4);
			size_t i = 0;
			for (; i + 2 < bytes.size(); i += 3)
			{
				uint32_t n = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8) | uint32_t(bytes[i + 2]);
				b64 += Base64Chars[(n >> 18) & 63];
				b64 += Base64Chars[(n >> 12) & 63];
				b64 += Base64Chars[(n >> 6) & 63];
				b64 += Base64Chars[n & 63];
			}
			if (i < bytes.size())
			{
				uint32_t n = uint32_t(bytes[i]) << 16;
				if (i + 1 < bytes.size()) n |= uint32_t(bytes[i + 1]) << 8;
				b64 += Base64Chars[(n >> 18) & 63];
				b64 += Base64Chars[(n >> 12) & 63];
				b64 += i + 1 < bytes.size() ? Base64Chars[(n >> 6) & 63] : '=';
				b64 += '=';
			}
			std::vector<cs::String> chunks;
			chunks.reserve((b64.size() + ChunkChars - 1) / ChunkChars);
			for (size_t at = 0; at < b64.size(); at += ChunkChars) chunks.push_back(b64.substr(at, ChunkChars));
			return chunks;
		}

		cs::List<uint8_t> Unpack(const std::vector<cs::String>& chunks)
		{
			std::vector<uint8_t> out;
			size_t total = 0;
			for (const auto& c : chunks) total += c.size();
			out.reserve(total / 4 * 3);
			uint32_t acc = 0;
			int bits = 0;
			for (const auto& c : chunks)
				for (char ch : c)
				{
					if (ch == '=') continue;
					int v = Base64Value(ch);
					if (v < 0) throw std::runtime_error("bad base64 in the save");
					acc = (acc << 6) | uint32_t(v);
					bits += 6;
					if (bits >= 8)
					{
						bits -= 8;
						out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
					}
				}
			return cs::List<uint8_t>(std::move(out));
		}

		uint64_t Fnv1a64(const cs::String& text)
		{
			uint64_t h = 0xCBF29CE484222325ULL;
			for (unsigned char b : text)
			{
				h ^= b;
				h *= 0x100000001B3ULL;
			}
			return h;
		}

		cs::String Hex(uint64_t h)
		{
			char buf[24];
			std::snprintf(buf, sizeof buf, "%016llX", static_cast<unsigned long long>(h));
			return buf;
		}

		std::vector<cs::String> Split(const cs::String& s, char c)
		{
			std::vector<cs::String> out;
			size_t start = 0;
			for (;;)
			{
				size_t at = s.find(c, start);
				if (at == cs::String::npos)
				{
					out.push_back(s.substr(start));
					return out;
				}
				out.push_back(s.substr(start, at - start));
				start = at + 1;
			}
		}

		std::vector<cs::String> Split(const cs::String& s, char c, int n)
		{
			std::vector<cs::String> out;
			size_t start = 0;
			while (static_cast<int>(out.size()) < n - 1)
			{
				size_t at = s.find(c, start);
				if (at == cs::String::npos) break;
				out.push_back(s.substr(start, at - start));
				start = at + 1;
			}
			out.push_back(s.substr(start));
			return out;
		}

		bool StartsWith(const cs::String& s, const char* prefix)
		{
			size_t n = std::strlen(prefix);
			return s.size() >= n && s.compare(0, n, prefix) == 0;
		}
	}
}
