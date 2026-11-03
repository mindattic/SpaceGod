#pragma once
// Small .NET behaviours the Systems port needs that Cs/Cs.h does not have yet (US-T17). Not a C# file: if Cs.h
// grows these, point the users at it and delete this header.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// A string key compared StringComparer.OrdinalIgnoreCase (cs::Dictionary/HashSet take a hash but no equality,
	/// so the comparer goes into the key). Keeps the original spelling for enumeration.
	struct NoCase
	{
		cs::String Value;
		NoCase() = default;
		NoCase(const cs::String& v) : Value(v) {}
		NoCase(const char* v) : Value(v) {}
		bool operator==(const NoCase& o) const { return cs::Strings::EqualsOrdinalIgnoreCase(Value, o.Value); }
		size_t HashCode() const { return std::hash<cs::String>{}(cs::Strings::ToUpperInvariant(Value)); }
		cs::String ToString() const { return Value; }
	};

	/// C# double.CompareTo: NaN sorts below everything (and equals itself).
	inline int CompareTo(double a, double b)
	{
		if (a < b) return -1;
		if (a > b) return 1;
		if (a == b) return 0;
		if (cs::Double::IsNaN(a)) return cs::Double::IsNaN(b) ? 0 : -1;
		return 1;
	}

	/// C# int.CompareTo.
	inline int CompareTo(int a, int b) { return a < b ? -1 : (a > b ? 1 : 0); }

	/// C# `foreach (char c in s)`: visits the UTF-16 code units of the (UTF-8) string, as .NET's char does.
	template <class F> void ForEachUtf16(const cs::String& s, F visit)
	{
		size_t i = 0, n = s.size();
		while (i < n)
		{
			uint32_t c = static_cast<unsigned char>(s[i]);
			uint32_t cp; size_t len;
			if (c < 0x80) { cp = c; len = 1; }
			else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
			else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
			else { cp = c & 0x07; len = 4; }
			for (size_t k = 1; k < len && i + k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
			i += len;
			if (cp >= 0x10000) { cp -= 0x10000; visit(static_cast<uint32_t>(0xD800 + (cp >> 10))); visit(static_cast<uint32_t>(0xDC00 + (cp & 0x3FF))); }
			else visit(cp);
		}
	}

	/// string.Equals(a, b, StringComparison.OrdinalIgnoreCase).
	inline bool EqualsNoCase(const cs::String& a, const cs::String& b) { return cs::Strings::EqualsOrdinalIgnoreCase(a, b); }
}
