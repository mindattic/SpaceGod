#pragma once
// The C# compatibility layer the C++ port of Unity.SpaceGod's Core is written against (see docs/MIGRATION.md).
//
// Core is translated file-for-file and line-for-line from C#. Wherever the C# result depends on a .NET
// behaviour (Dictionary enumeration order, System.Random's sequence, List.Sort's unstable introsort,
// Math.Round's banker's rounding, double.ToString formatting, unchecked int wraparound, (int)double casts),
// this layer reproduces .NET's behaviour exactly, so the C++ and C# builds run the same simulation bit for bit
// and the Bench can compare them. Pure C++20 with the standard library only: no Unreal headers, so Core also
// builds standalone (tools/build-core.ps1) for the headless Bench.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cs
{
	// ---------------------------------------------------------------- references --------------------------------
	// A C# class instance. Shared ownership stands in for the GC; reference equality is pointer equality.
	template <class T> using Ref = std::shared_ptr<T>;
	template <class T, class... A> Ref<T> New(A&&... args) { return std::make_shared<T>(std::forward<A>(args)...); }

	// C# `x is T t` / `x as T`.
	template <class T, class U> Ref<T> As(const Ref<U>& p) { return std::dynamic_pointer_cast<T>(p); }
	template <class T, class U> bool Is(const Ref<U>& p) { return std::dynamic_pointer_cast<T>(p) != nullptr; }

	// C# `T?` for value types.
	template <class T> using Nullable = std::optional<T>;

	using String = std::string; // UTF-8. Core's text is ASCII apart from a few symbols; lengths are bytes.

	// ---------------------------------------------------------------- exceptions --------------------------------
	struct Exception : std::runtime_error { using std::runtime_error::runtime_error; };
	struct ArgumentException : Exception { using Exception::Exception; };
	struct ArgumentOutOfRangeException : ArgumentException { using ArgumentException::ArgumentException; };
	struct ArgumentNullException : ArgumentException { using ArgumentException::ArgumentException; };
	struct InvalidOperationException : Exception { using Exception::Exception; };
	struct KeyNotFoundException : Exception { using Exception::Exception; };
	struct NotSupportedException : Exception { using Exception::Exception; };
	struct FormatException : Exception { using Exception::Exception; };
	struct IOException : Exception { using Exception::Exception; };

	// ---------------------------------------------------------------- hashing -----------------------------------
	// Only lookup uses hashes; enumeration order never depends on them (see Dictionary), so any good hash will do.
	template <class T, class = void> struct Hash { size_t operator()(const T& v) const { return std::hash<T>{}(v); } };
	template <class T> struct Hash<Ref<T>> { size_t operator()(const Ref<T>& p) const { return std::hash<const void*>{}(p.get()); } };
	// A struct or record key gives itself `size_t HashCode() const` and `operator==`.
	template <class T> struct Hash<T, std::void_t<decltype(std::declval<const T&>().HashCode())>>
	{
		size_t operator()(const T& v) const { return v.HashCode(); }
	};
	template <class T> struct Hash<T, std::enable_if_t<std::is_enum_v<T>>>
	{
		size_t operator()(T v) const { return std::hash<std::underlying_type_t<T>>{}(static_cast<std::underlying_type_t<T>>(v)); }
	};
	inline size_t HashCombine(size_t seed, size_t h) { return seed ^ (h + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2)); }

	// ---------------------------------------------------------------- List<T> -----------------------------------
	template <class T> class List
	{
	public:
		using value_type = T;
		List() = default;
		List(std::initializer_list<T> init) : items(init) {}
		explicit List(std::vector<T> v) : items(std::move(v)) {}
		template <class It> List(It first, It last) : items(first, last) {}

		int Count() const { return static_cast<int>(items.size()); }
		T& operator[](int i) { Check(i); return items[static_cast<size_t>(i)]; }
		const T& operator[](int i) const { Check(i); return items[static_cast<size_t>(i)]; }

		void Add(const T& v) { items.push_back(v); }
		void Add(T&& v) { items.push_back(std::move(v)); }
		template <class C> void AddRange(const C& c) { for (const auto& v : c) items.push_back(v); }
		void Insert(int i, const T& v) { if (i < 0 || i > Count()) throw ArgumentOutOfRangeException("index"); items.insert(items.begin() + i, v); }
		void RemoveAt(int i) { Check(i); items.erase(items.begin() + i); }
		void RemoveRange(int i, int n) { items.erase(items.begin() + i, items.begin() + i + n); }
		bool Remove(const T& v) { int i = IndexOf(v); if (i < 0) return false; RemoveAt(i); return true; }
		template <class P> int RemoveAll(P pred)
		{
			auto it = std::remove_if(items.begin(), items.end(), [&](const T& v) { return static_cast<bool>(pred(v)); });
			int n = static_cast<int>(items.end() - it);
			items.erase(it, items.end());
			return n;
		}
		void Clear() { items.clear(); }
		int IndexOf(const T& v) const { for (size_t i = 0; i < items.size(); ++i) if (items[i] == v) return static_cast<int>(i); return -1; }
		bool Contains(const T& v) const { return IndexOf(v) >= 0; }
		template <class P> int FindIndex(P pred) const { for (size_t i = 0; i < items.size(); ++i) if (pred(items[i])) return static_cast<int>(i); return -1; }
		// C# Find returns default(T) when nothing matches: nullptr for a Ref, a zeroed struct otherwise.
		template <class P> T Find(P pred) const { int i = FindIndex(pred); return i >= 0 ? items[static_cast<size_t>(i)] : T{}; }
		template <class P> bool Exists(P pred) const { return FindIndex(pred) >= 0; }
		template <class P> bool TrueForAll(P pred) const { for (const auto& v : items) if (!pred(v)) return false; return true; }
		void Reverse() { std::reverse(items.begin(), items.end()); }
		List<T> ToList() const { return *this; }
		List<T> ToArray() const { return *this; } // arrays are Lists too (see docs/MIGRATION.md)

		// .NET's List.Sort: unstable introsort, ported exactly (Cs.cpp), so equal keys land where .NET puts them.
		template <class Cmp> void Sort(Cmp compare); // compare(a, b) -> int, as Comparison<T>
		void Sort();                                 // default comparer (operator<)

		auto begin() { return items.begin(); }
		auto end() { return items.end(); }
		auto begin() const { return items.begin(); }
		auto end() const { return items.end(); }
		std::vector<T>& Raw() { return items; }
		const std::vector<T>& Raw() const { return items; }
		bool operator==(const List& o) const { return this == &o; } // reference semantics, as a C# List

	private:
		void Check(int i) const { if (i < 0 || static_cast<size_t>(i) >= items.size()) throw ArgumentOutOfRangeException("index"); }
		std::vector<T> items;
	};

	// A C# `T[]` of fixed length.
	template <class T> List<T> NewArray(int n) { return List<T>(std::vector<T>(static_cast<size_t>(n))); }

	// ---------------------------------------------------------------- Dictionary<K,V> ---------------------------
	// Reproduces .NET's Dictionary layout: entries live in an array in insertion order, a Remove frees its slot
	// onto a free list, and the next Add fills the most recently freed slot first. Enumeration walks the array,
	// so it comes out in exactly .NET's order, including after removals.
	template <class K, class V, class H = Hash<K>> class Dictionary
	{
	public:
		struct Entry { K Key; V Value; bool Used = false; int NextFree = -1; };
		using KeyValuePair = std::pair<const K&, V&>;

		int Count() const { return count; }
		bool ContainsKey(const K& k) const { return index.find(k) != index.end(); }
		bool TryGetValue(const K& k, V& out) const { auto it = index.find(k); if (it == index.end()) return false; out = entries[static_cast<size_t>(it->second)].Value; return true; }
		// Indexer get throws KeyNotFoundException; set adds or replaces.
		const V& operator[](const K& k) const { auto it = index.find(k); if (it == index.end()) throw KeyNotFoundException("key"); return entries[static_cast<size_t>(it->second)].Value; }
		V& operator[](const K& k) { auto it = index.find(k); if (it != index.end()) return entries[static_cast<size_t>(it->second)].Value; return Insert(k, V{}); }
		V& At(const K& k) { auto it = index.find(k); if (it == index.end()) throw KeyNotFoundException("key"); return entries[static_cast<size_t>(it->second)].Value; }
		void Set(const K& k, const V& v) { (*this)[k] = v; }
		void Add(const K& k, const V& v) { if (ContainsKey(k)) throw ArgumentException("An item with the same key has already been added."); Insert(k, v); }
		bool TryAdd(const K& k, const V& v) { if (ContainsKey(k)) return false; Insert(k, v); return true; }
		V GetValueOrDefault(const K& k, const V& fallback = V{}) const { V v; return TryGetValue(k, v) ? v : fallback; }
		bool Remove(const K& k)
		{
			auto it = index.find(k);
			if (it == index.end()) return false;
			int slot = it->second;
			index.erase(it);
			Entry& e = entries[static_cast<size_t>(slot)];
			e = Entry{};
			e.NextFree = freeList;
			freeList = slot;
			--count;
			return true;
		}
		void Clear() { entries.clear(); index.clear(); freeList = -1; count = 0; }

		List<K> Keys() const { List<K> r; for (const auto& e : entries) if (e.Used) r.Add(e.Key); return r; }
		List<V> Values() const { List<V> r; for (const auto& e : entries) if (e.Used) r.Add(e.Value); return r; }

		// for (auto [key, value] : dict) — in .NET's enumeration order. Don't add or remove while iterating.
		class Iterator
		{
		public:
			Iterator(std::vector<Entry>* e, size_t i) : es(e), i(i) { Skip(); }
			KeyValuePair operator*() const { return { (*es)[i].Key, (*es)[i].Value }; }
			Iterator& operator++() { ++i; Skip(); return *this; }
			bool operator!=(const Iterator& o) const { return i != o.i; }
		private:
			void Skip() { while (i < es->size() && !(*es)[i].Used) ++i; }
			std::vector<Entry>* es; size_t i;
		};
		Iterator begin() { return Iterator(&entries, 0); }
		Iterator end() { return Iterator(&entries, entries.size()); }
		Iterator begin() const { return Iterator(const_cast<std::vector<Entry>*>(&entries), 0); }
		Iterator end() const { return Iterator(const_cast<std::vector<Entry>*>(&entries), entries.size()); }

	private:
		V& Insert(const K& k, const V& v)
		{
			int slot;
			if (freeList >= 0) { slot = freeList; freeList = entries[static_cast<size_t>(slot)].NextFree; }
			else { slot = static_cast<int>(entries.size()); entries.emplace_back(); }
			Entry& e = entries[static_cast<size_t>(slot)];
			e.Key = k; e.Value = v; e.Used = true; e.NextFree = -1;
			index.emplace(k, slot);
			++count;
			return e.Value;
		}
		std::vector<Entry> entries;
		std::unordered_map<K, int, H> index;
		int freeList = -1;
		int count = 0;
	};

	// ---------------------------------------------------------------- HashSet<T> --------------------------------
	// Same slot layout as Dictionary, so enumeration order matches .NET's HashSet.
	template <class T, class H = Hash<T>> class HashSet
	{
	public:
		HashSet() = default;
		HashSet(std::initializer_list<T> init) { for (const auto& v : init) Add(v); }
		template <class C> explicit HashSet(const C& c) { for (const auto& v : c) Add(v); }
		int Count() const { return map.Count(); }
		bool Add(const T& v) { return map.TryAdd(v, true); }
		bool Remove(const T& v) { return map.Remove(v); }
		bool Contains(const T& v) const { return map.ContainsKey(v); }
		void Clear() { map.Clear(); }
		template <class C> void UnionWith(const C& c) { for (const auto& v : c) Add(v); }
		List<T> ToList() const { return map.Keys(); }
		class Iterator
		{
		public:
			explicit Iterator(typename Dictionary<T, bool, H>::Iterator it) : it(it) {}
			const T& operator*() const { return (*it).first; }
			Iterator& operator++() { ++it; return *this; }
			bool operator!=(const Iterator& o) const { return it != o.it; }
		private:
			typename Dictionary<T, bool, H>::Iterator it;
		};
		Iterator begin() const { return Iterator(map.begin()); }
		Iterator end() const { return Iterator(map.end()); }
	private:
		Dictionary<T, bool, H> map;
	};

	// ---------------------------------------------------------------- Queue / Stack -----------------------------
	template <class T> class Queue
	{
	public:
		int Count() const { return static_cast<int>(items.size() - head); }
		void Enqueue(const T& v) { items.push_back(v); }
		T Dequeue() { if (Count() == 0) throw InvalidOperationException("Queue empty."); T v = std::move(items[head++]); Compact(); return v; }
		const T& Peek() const { if (Count() == 0) throw InvalidOperationException("Queue empty."); return items[head]; }
		bool TryDequeue(T& out) { if (Count() == 0) return false; out = Dequeue(); return true; }
		void Clear() { items.clear(); head = 0; }
		auto begin() const { return items.begin() + static_cast<std::ptrdiff_t>(head); }
		auto end() const { return items.end(); }
	private:
		void Compact() { if (head > 64 && head * 2 > items.size()) { items.erase(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(head)); head = 0; } }
		std::vector<T> items; size_t head = 0;
	};

	template <class T> class Stack
	{
	public:
		int Count() const { return static_cast<int>(items.size()); }
		void Push(const T& v) { items.push_back(v); }
		T Pop() { if (items.empty()) throw InvalidOperationException("Stack empty."); T v = std::move(items.back()); items.pop_back(); return v; }
		const T& Peek() const { if (items.empty()) throw InvalidOperationException("Stack empty."); return items.back(); }
		void Clear() { items.clear(); }
	private:
		std::vector<T> items;
	};

	// ---------------------------------------------------------------- events & delegates ------------------------
	// C# Func<...> / Action<...> are std::function; `event Action<A, B> Name;` is `cs::Event<A, B> Name;`.
	template <class... A> using Action = std::function<void(A...)>;
	template <class R, class... A> using Func = std::function<R(A...)>;

	template <class... A> class Event
	{
	public:
		// `Name += handler;` returns a token; `Name -= token;` removes it (C# removes by delegate equality,
		// which a std::function can't do, so keep the token where the C# unsubscribes).
		int operator+=(Action<A...> handler) { handlers.push_back({ ++next, std::move(handler) }); return next; }
		void operator-=(int token) { handlers.erase(std::remove_if(handlers.begin(), handlers.end(), [&](auto& h) { return h.first == token; }), handlers.end()); }
		// `Name?.Invoke(args)`. Handlers run in subscription order on a snapshot, as a C# multicast delegate.
		void Invoke(A... args) const { auto snapshot = handlers; for (auto& h : snapshot) h.second(args...); }
		bool HasHandlers() const { return !handlers.empty(); }
		void Clear() { handlers.clear(); }
	private:
		std::vector<std::pair<int, Action<A...>>> handlers;
		int next = 0;
	};

	// ---------------------------------------------------------------- System.Random -----------------------------
	// .NET's seeded Random (Net5CompatSeedImpl: Knuth's subtractive generator), bit-exact with `new Random(seed)`
	// on .NET 5+ and on Unity's Mono. The unseeded constructor seeds from the clock (non-deterministic, as .NET).
	class Random
	{
	public:
		Random();
		explicit Random(int seed);
		int Next();                          // [0, int.MaxValue)
		int Next(int maxValue);              // [0, maxValue)
		int Next(int minValue, int maxValue);// [minValue, maxValue)
		double NextDouble();                 // [0, 1)
		void NextBytes(List<uint8_t>& buffer);
	private:
		double Sample();
		int InternalSample();
		double GetSampleForLargeRange();
		int seedArray[56] = {};
		int inext = 0, inextp = 21;
	};

	// ---------------------------------------------------------------- Math --------------------------------------
	namespace Math
	{
		constexpr double PI = 3.14159265358979323846;
		constexpr double E = 2.71828182845904523536;
		constexpr double Tau = 2 * PI;
		using std::abs; using std::sqrt; using std::sin; using std::cos; using std::tan; using std::asin; using std::acos;
		using std::atan; using std::atan2; using std::exp; using std::log; using std::log10; using std::pow; using std::floor;
		using std::ceil; using std::sinh; using std::cosh; using std::tanh; using std::cbrt;
		inline double Abs(double v) { return std::fabs(v); }
		inline int Abs(int v) { if (v == std::numeric_limits<int>::min()) throw std::overflow_error("Negating the minimum value of a twos complement number is invalid."); return v < 0 ? -v : v; }
		inline long long Abs(long long v) { return v < 0 ? -v : v; }
		// .NET Math.Max/Min on doubles return NaN if either is NaN.
		inline double Max(double a, double b) { if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<double>::quiet_NaN(); return a > b ? a : (a < b ? b : (a == 0 && b == 0 ? (std::signbit(a) ? b : a) : a)); }
		inline double Min(double a, double b) { if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<double>::quiet_NaN(); return a < b ? a : (a > b ? b : (a == 0 && b == 0 ? (std::signbit(a) ? a : b) : a)); }
		inline int Max(int a, int b) { return a > b ? a : b; }
		inline int Min(int a, int b) { return a < b ? a : b; }
		inline long long Max(long long a, long long b) { return a > b ? a : b; }
		inline long long Min(long long a, long long b) { return a < b ? a : b; }
		inline double Clamp(double v, double lo, double hi) { if (lo > hi) throw ArgumentException("min > max"); return v < lo ? lo : (v > hi ? hi : v); }
		inline int Clamp(int v, int lo, int hi) { if (lo > hi) throw ArgumentException("min > max"); return v < lo ? lo : (v > hi ? hi : v); }
		inline int Sign(double v) { if (std::isnan(v)) throw ArithmeticError(); return v > 0 ? 1 : (v < 0 ? -1 : 0); }
		inline int Sign(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
		inline double Sqrt(double v) { return std::sqrt(v); }
		inline double Sin(double v) { return std::sin(v); }
		inline double Cos(double v) { return std::cos(v); }
		inline double Tan(double v) { return std::tan(v); }
		inline double Asin(double v) { return std::asin(v); }
		inline double Acos(double v) { return std::acos(v); }
		inline double Atan(double v) { return std::atan(v); }
		inline double Atan2(double y, double x) { return std::atan2(y, x); }
		inline double Exp(double v) { return std::exp(v); }
		inline double Log(double v) { return std::log(v); }
		inline double Log(double v, double b) { return std::log(v) / std::log(b); }
		inline double Log10(double v) { return std::log10(v); }
		inline double Pow(double a, double b) { return std::pow(a, b); }
		inline double Floor(double v) { return std::floor(v); }
		inline double Ceiling(double v) { return std::ceil(v); }
		inline double Truncate(double v) { return std::trunc(v); }
		inline double Sinh(double v) { return std::sinh(v); }
		inline double Cosh(double v) { return std::cosh(v); }
		inline double Tanh(double v) { return std::tanh(v); }
		inline double Cbrt(double v) { return std::cbrt(v); }
		inline double IEEERemainder(double a, double b) { return std::remainder(a, b); }
		// Math.Round defaults to MidpointRounding.ToEven (banker's rounding), unlike C's round().
		inline double Round(double v) { return std::nearbyint(v); } // FE_TONEAREST is ties-to-even
		double Round(double v, int digits);                          // ToEven, as .NET (Cs.cpp)
		enum class MidpointRounding { ToEven, AwayFromZero };
		double Round(double v, MidpointRounding mode);
		double Round(double v, int digits, MidpointRounding mode);
		struct ArithmeticErrorT : Exception { ArithmeticErrorT() : Exception("Function does not accept floating point Not-a-Number values.") {} };
		inline ArithmeticErrorT ArithmeticError() { return {}; }
	}

	// double.NaN, double.MaxValue, ... and the double.IsX tests.
	namespace Double
	{
		constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
		constexpr double PositiveInfinity = std::numeric_limits<double>::infinity();
		constexpr double NegativeInfinity = -std::numeric_limits<double>::infinity();
		constexpr double MaxValue = std::numeric_limits<double>::max();
		constexpr double MinValue = -std::numeric_limits<double>::max();
		constexpr double Epsilon = std::numeric_limits<double>::denorm_min();
		inline bool IsNaN(double v) { return std::isnan(v); }
		inline bool IsInfinity(double v) { return std::isinf(v); }
		inline bool IsPositiveInfinity(double v) { return std::isinf(v) && v > 0; }
		inline bool IsNegativeInfinity(double v) { return std::isinf(v) && v < 0; }
		inline bool IsFinite(double v) { return std::isfinite(v); }
		bool TryParse(std::string_view s, double& out); // invariant culture
		double Parse(std::string_view s);
	}
	namespace Int
	{
		constexpr int MaxValue = std::numeric_limits<int>::max();
		constexpr int MinValue = std::numeric_limits<int>::min();
		bool TryParse(std::string_view s, int& out);
		int Parse(std::string_view s);
	}

	// ---------------------------------------------------------------- conversions -------------------------------
	// C# `(int)d`: truncates toward zero; NaN and out-of-range give int.MinValue (x64 cvttsd2si, as .NET 8 and
	// Mono do). C++'s static_cast is undefined there, so every double->int cast in Core goes through ToInt.
	inline int ToInt(double v) { if (!(v > -2147483649.0 && v < 2147483648.0)) return std::numeric_limits<int>::min(); return static_cast<int>(v); }
	inline long long ToLong(double v) { if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0)) return std::numeric_limits<long long>::min(); return static_cast<long long>(v); }
	// C# `unchecked` int arithmetic wraps; signed overflow is undefined in C++, so wrap through uint32.
	inline int WrapAdd(int a, int b) { return static_cast<int>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
	inline int WrapSub(int a, int b) { return static_cast<int>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
	inline int WrapMul(int a, int b) { return static_cast<int>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }
	inline long long WrapMul(long long a, long long b) { return static_cast<long long>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b)); }

	// ---------------------------------------------------------------- strings & formatting ----------------------
	// Invariant culture throughout. Str(x) is C#'s x.ToString(); for a double that is .NET Core 3.0+'s shortest
	// round-trip text ("0.1", "1E+15", "NaN", "∞" -> "Infinity" as .NET prints it, "-0" for negative zero).
	String Str(double v);
	String Str(float v);
	inline String Str(int v) { return std::to_string(v); }
	inline String Str(long long v) { return std::to_string(v); }
	inline String Str(unsigned v) { return std::to_string(v); }
	inline String Str(unsigned long long v) { return std::to_string(v); }
	inline String Str(bool v) { return v ? "True" : "False"; }
	inline String Str(const String& v) { return v; }
	inline String Str(const char* v) { return v; }
	inline String Str(char v) { return String(1, v); }
	// Enums: declare `const char* ToString(MyEnum)` beside the enum (see docs/MIGRATION.md); Str finds it by ADL.
	template <class E, std::enable_if_t<std::is_enum_v<E>, int> = 0> String Str(E v) { return ToString(v); }
	// A class or struct with `String ToString() const`.
	template <class T> auto Str(const T& v) -> decltype(v.ToString(), String()) { return v.ToString(); }
	template <class T> auto Str(const Ref<T>& v) -> decltype(v->ToString(), String()) { return v ? v->ToString() : String(); }

	// x.ToString(format) for the .NET standard and custom numeric formats Core uses: "0", "0.0", "0.00", "0.000",
	// "0.#", "0.##", "#,0", "N0".."N3", "F0".."F3", "P0".."P1", "R", "G", "+0;-0", ... Rounding follows .NET Core 3.0+
	// (exact decimal value of the double, midpoint away from zero).
	String Fmt(double v, std::string_view format);
	String Fmt(int v, std::string_view format);
	String Fmt(long long v, std::string_view format);

	// String concatenation: C# `"a" + x + "b"` is `cs::Cat("a", x, "b")`, each piece through Str.
	template <class... A> String Cat(const A&... parts) { String s; ((s += Str(parts)), ...); return s; }

	namespace Strings
	{
		inline bool IsNullOrEmpty(const String& s) { return s.empty(); }
		bool IsNullOrWhiteSpace(const String& s);
		String ToLowerInvariant(const String& s);
		String ToUpperInvariant(const String& s);
		String Trim(const String& s);
		String TrimStart(const String& s);
		String TrimEnd(const String& s);
		String Substring(const String& s, int start);
		String Substring(const String& s, int start, int length);
		bool StartsWith(const String& s, std::string_view prefix); // ordinal
		bool EndsWith(const String& s, std::string_view suffix);   // ordinal
		bool Contains(const String& s, std::string_view part);     // ordinal
		int IndexOf(const String& s, std::string_view part, int start = 0);
		int IndexOf(const String& s, char c, int start = 0);
		int LastIndexOf(const String& s, char c);
		String Replace(const String& s, std::string_view from, std::string_view to);
		List<String> Split(const String& s, char sep);
		List<String> Split(const String& s, std::string_view seps); // any of the characters
		String PadLeft(const String& s, int width, char pad = ' ');
		String PadRight(const String& s, int width, char pad = ' ');
		int CompareOrdinal(const String& a, const String& b);
		bool EqualsOrdinalIgnoreCase(const String& a, const String& b);
		// string.Join(sep, items): each item through Str.
		template <class C> String Join(std::string_view sep, const C& items)
		{
			String r; bool first = true;
			for (const auto& v : items) { if (!first) r += sep; r += Str(v); first = false; }
			return r;
		}
	}

	// A mutable string builder (System.Text.StringBuilder).
	class StringBuilder
	{
	public:
		StringBuilder& Append(const String& s) { buf += s; return *this; }
		template <class T> StringBuilder& Append(const T& v) { buf += Str(v); return *this; }
		StringBuilder& AppendLine(const String& s = String()) { buf += s; buf += '\n'; return *this; }
		int Length() const { return static_cast<int>(buf.size()); }
		void Clear() { buf.clear(); }
		String ToString() const { return buf; }
	private:
		String buf;
	};

	// ---------------------------------------------------------------- LINQ (the few uses) -----------------------
	// OrderBy / OrderByDescending are stable sorts in LINQ, unlike List.Sort.
	template <class C, class KeyF> auto OrderBy(const C& c, KeyF key)
	{
		List<typename C::value_type> r; for (const auto& v : c) r.Add(v);
		std::stable_sort(r.Raw().begin(), r.Raw().end(), [&](const auto& a, const auto& b) { return key(a) < key(b); });
		return r;
	}
	template <class C, class KeyF> auto OrderByDescending(const C& c, KeyF key)
	{
		List<typename C::value_type> r; for (const auto& v : c) r.Add(v);
		std::stable_sort(r.Raw().begin(), r.Raw().end(), [&](const auto& a, const auto& b) { return key(b) < key(a); });
		return r;
	}
}

#include "Cs/CsSort.inl"
