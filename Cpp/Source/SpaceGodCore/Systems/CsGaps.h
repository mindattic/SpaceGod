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

	/// string.Equals(a, b, StringComparison.OrdinalIgnoreCase).
	inline bool EqualsNoCase(const cs::String& a, const cs::String& b) { return cs::Strings::EqualsOrdinalIgnoreCase(a, b); }
}
