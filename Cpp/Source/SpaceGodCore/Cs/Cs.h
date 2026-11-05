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
#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// SpaceGodCore is a DLL in UE's modular (editor) builds: UBT defines SPACEGODCORE_API as dllexport/dllimport there
// (empty in monolithic builds). Standalone builds (tools/build-core.ps1) define nothing, so it is empty here.
// Mark every non-template class and free function that is *defined in a .cpp* with it, as below.
#ifndef SPACEGODCORE_API
#define SPACEGODCORE_API
#endif
// The Core keeps C#'s names, cs::Math::PI among them: Unreal's legacy PI macro (UnrealMathUtility.h, deprecated since
// 5.1 for UE_PI; the only legacy math name the Core uses) would rewrite it in a module that includes Unreal's headers
// first. So it is gone from here on in any file that includes a Core header (Unreal code says UE_PI). Defining
// UE_DEFINE_LEGACY_MATH_CONSTANT_MACRO_NAMES=0 per module does not work: the engine's shared PCH has the macros.
#ifdef PI
#undef PI
#endif
// UBT defines SPACEGODCORE_API as DLLEXPORT / DLLIMPORT, which Unreal's own headers define; SpaceGodCore's files
// include none, so define them here exactly as WindowsPlatform.h does (an identical redefinition is legal).
// Exported classes hold standard-library members (std::string, shared_ptr, cs::List); both sides of the DLL
// boundary are built with the same compiler and CRT, so MSVC's "needs to have dll-interface" warnings are noise.
#ifdef _MSC_VER
#pragma warning(disable : 4251 4275)
#endif
#if defined(_WIN32) && !defined(DLLEXPORT)
#define DLLEXPORT __declspec(dllexport)
#define DLLIMPORT __declspec(dllimport)
#endif

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
	struct OverflowException : Exception { using Exception::Exception; };
	struct NullReferenceException : Exception { using Exception::Exception; };
	struct IndexOutOfRangeException : Exception { using Exception::Exception; };

	// ---------------------------------------------------------------- IEEE tests --------------------------------
	// MSVC's std::isnan / isinf / isfinite / signbit on a double are out-of-line UCRT calls (_dclass, _dtest, _dsign);
	// these are the same tests inline. Hot: every Math.Max/Min. US-T64: NaN by its bits, not by v != v: the header is also
	// compiled into SpaceGod, which is not /fp:precise (UE's default floating-point model), and there the optimizer
	// may fold v != v to false. A packaged build did: double.IsNaN(NaN) was false, so SpaceCamera::FocusOn took FocusSelected's
	// NaN "no framing given" for a framing and a packaged co-op client's camera went NaN on its first focus.
	inline bool IsNaNFast(double v) { return (std::bit_cast<uint64_t>(v) & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull; }
	inline bool IsNaNFast(float v) { return (std::bit_cast<uint32_t>(v) & 0x7FFFFFFFu) > 0x7F800000u; }
	inline bool IsInfFast(double v) { return (std::bit_cast<uint64_t>(v) & 0x7FFFFFFFFFFFFFFFull) == 0x7FF0000000000000ull; }
	inline bool IsInfFast(float v) { return (std::bit_cast<uint32_t>(v) & 0x7FFFFFFFu) == 0x7F800000u; }
	inline bool IsFiniteFast(double v) { return (std::bit_cast<uint64_t>(v) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull; }
	inline bool IsFiniteFast(float v) { return (std::bit_cast<uint32_t>(v) & 0x7F800000u) != 0x7F800000u; }
	inline bool SignBitFast(double v) { return (std::bit_cast<uint64_t>(v) >> 63) != 0; }

	// ---------------------------------------------------------------- hashing -----------------------------------
	// Only lookup uses hashes; enumeration order never depends on them (see Dictionary), so any good hash will do.
	template <class T, class = void> struct Hash { size_t operator()(const T& v) const { return std::hash<T>{}(v); } };
	template <class T> struct Hash<Ref<T>> { size_t operator()(const Ref<T>& p) const { return reinterpret_cast<size_t>(p.get()) >> 4; } };
	// Integers and doubles hash to their own value (MSVC's std::hash runs FNV-1a over the bytes); Dictionary mixes it.
	// A double's -0.0 hashes as 0.0, since the two compare equal.
	template <class T> struct Hash<T, std::enable_if_t<std::is_integral_v<T>>> { size_t operator()(T v) const { return static_cast<size_t>(v); } };
	template <> struct Hash<double> { size_t operator()(double v) const { return v == 0 ? 0 : std::bit_cast<size_t>(v); } };
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
	// C# value-tuple keys `(a, b)` / `(a, b, c)`: std::pair / std::tuple.
	template <class A, class B> struct Hash<std::pair<A, B>>
	{
		size_t operator()(const std::pair<A, B>& v) const { return HashCombine(Hash<A>{}(v.first), Hash<B>{}(v.second)); }
	};
	template <class... A> struct Hash<std::tuple<A...>>
	{
		size_t operator()(const std::tuple<A...>& v) const
		{
			size_t h = 0;
			std::apply([&](const auto&... x) { ((h = HashCombine(h, Hash<std::decay_t<decltype(x)>>{}(x))), ...); }, v);
			return h;
		}
	};

	// ---------------------------------------------------------------- default comparer ---------------------------
	// .NET's Comparer<T>.Default.Compare(a, b) as -1/0/1: doubles order NaN before everything (double.CompareTo);
	// a type with `int CompareTo(const T&) const` uses it; anything else uses operator<. Strings compare ordinally
	// (.NET's default string comparer is culture-sensitive; for Core's ASCII identifiers the two differ only in
	// case and punctuation order -- note it on your ticket if a port sorts mixed-case text by the default comparer).
	template <class T> int Compare(const T& a, const T& b)
	{
		if constexpr (std::is_floating_point_v<T>)
		{
			if (a < b) return -1;
			if (a > b) return 1;
			if (a == b) return 0;
			if (IsNaNFast(a)) return IsNaNFast(b) ? 0 : -1;
			return 1;
		}
		else if constexpr (std::is_enum_v<T>) { using U = std::underlying_type_t<T>; return Compare(static_cast<U>(a), static_cast<U>(b)); }
		else if constexpr (requires { { a.CompareTo(b) } -> std::convertible_to<int>; }) { int c = a.CompareTo(b); return c < 0 ? -1 : (c > 0 ? 1 : 0); }
		else { return a < b ? -1 : (b < a ? 1 : 0); }
	}
	template <class A, class B, std::enable_if_t<!std::is_same_v<A, B> && std::is_arithmetic_v<A> && std::is_arithmetic_v<B>, int> = 0>
	int Compare(const A& a, const B& b) { using C = std::common_type_t<A, B>; return Compare<C>(static_cast<C>(a), static_cast<C>(b)); }

	// ---------------------------------------------------------------- equality comparers -------------------------
	// C# `new Dictionary<K, V>(comparer)` / `new HashSet<T>(comparer)`: pass a comparer *type* as the H parameter,
	// `cs::Dictionary<String, int, cs::OrdinalIgnoreCaseComparer>`. A comparer is a struct with both
	// `size_t operator()(const K&) const` (GetHashCode) and `bool operator()(const K&, const K&) const` (Equals);
	// a plain hasher (like the default Hash<K>) pairs with operator==. StringComparer.Ordinal is the default.
	template <class H, class K, class = void> struct EqualFor { using type = std::equal_to<K>; };
	template <class H, class K> struct EqualFor<H, K, std::enable_if_t<std::is_invocable_r_v<bool, const H&, const K&, const K&>>> { using type = H; };
	template <class H, class K> using EqualFor_t = typename EqualFor<H, K>::type;

	// StringComparer.OrdinalIgnoreCase (ASCII case folding; Core's keys are ASCII).
	struct OrdinalIgnoreCaseComparer
	{
		static char Fold(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c; }
		size_t operator()(const std::string& s) const
		{
			size_t h = 1469598103934665603ull;
			for (char c : s) { h ^= static_cast<unsigned char>(Fold(c)); h *= 1099511628211ull; }
			return h;
		}
		bool operator()(const std::string& a, const std::string& b) const
		{
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); i++) if (Fold(a[i]) != Fold(b[i])) return false;
			return true;
		}
	};
	using OrdinalComparer = Hash<std::string>; // StringComparer.Ordinal

	// ---------------------------------------------------------------- List<T> -----------------------------------
	// List<bool> must hand out a real bool& (std::vector<bool> packs bits), so it stores one of these per item.
	// It converts to bool& / const bool&, so `for (bool b : flags)`, `for (bool& b : flags)` and `flags[i] = true`
	// all work as for any other List.
	struct BoolSlot
	{
		bool Value = false;
		BoolSlot() = default;
		BoolSlot(bool v) : Value(v) {}
		operator bool&() { return Value; }
		operator const bool&() const { return Value; }
		BoolSlot& operator=(bool v) { Value = v; return *this; }
	};
	template <class T> struct ListSlot { using type = T; };
	template <> struct ListSlot<bool> { using type = BoolSlot; };

	template <class T> class List
	{
	public:
		using value_type = T;
		using Slot = typename ListSlot<T>::type; // T itself, except BoolSlot for bool
		List() = default;
		List(std::initializer_list<T> init) : items(init.begin(), init.end()) {}
		explicit List(std::vector<T> v)
		{
			if constexpr (std::is_same_v<T, Slot>) items = std::move(v);
			else items.assign(v.begin(), v.end());
		}
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
		template <class P> T Find(P pred) const { int i = FindIndex(pred); return i >= 0 ? static_cast<const T&>(items[static_cast<size_t>(i)]) : T{}; }
		template <class P> bool Exists(P pred) const { return FindIndex(pred) >= 0; }
		template <class P> bool TrueForAll(P pred) const { for (const auto& v : items) if (!pred(v)) return false; return true; }
		void Reverse() { std::reverse(items.begin(), items.end()); }
		List<T> ToList() const { return *this; }
		List<T> ToArray() const { return *this; } // arrays are Lists too (see docs/MIGRATION.md)
		// GetRange(index, count): a shallow copy of count items from index.
		List<T> GetRange(int index, int count) const
		{
			if (index < 0 || count < 0) throw ArgumentOutOfRangeException(index < 0 ? "index" : "count");
			if (Count() - index < count) throw ArgumentException("Offset and length were out of bounds for the array or count is greater than the number of elements from index to the end of the source collection.");
			return List<T>(items.begin() + index, items.begin() + index + count);
		}

		// .NET's List.Sort: unstable introsort, ported exactly (Cs.cpp), so equal keys land where .NET puts them.
		template <class Cmp> void Sort(Cmp compare); // compare(a, b) -> int, as Comparison<T>
		void Sort();                                 // default comparer (operator<)

		auto begin() { return items.begin(); }
		auto end() { return items.end(); }
		auto begin() const { return items.begin(); }
		auto end() const { return items.end(); }
		std::vector<Slot>& Raw() { return items; }
		const std::vector<Slot>& Raw() const { return items; }
		bool operator==(const List& o) const { return this == &o; } // reference semantics, as a C# List

	private:
		void Check(int i) const { if (i < 0 || static_cast<size_t>(i) >= items.size()) throw ArgumentOutOfRangeException("index"); }
		std::vector<Slot> items;
	};

	// A C# `T[]` of fixed length.
	template <class T> List<T> NewArray(int n) { return List<T>(std::vector<T>(static_cast<size_t>(n))); }

	// ---------------------------------------------------------------- Dictionary<K,V> ---------------------------
	// Reproduces .NET's Dictionary layout: entries live in an array in insertion order, a Remove frees its slot
	// onto a free list, and the next Add fills the most recently freed slot first. Enumeration walks the array,
	// so it comes out in exactly .NET's order, including after removals.
	// Lookup (never enumeration) goes through an open-addressing index of entry numbers (linear probing over a
	// power-of-two table, at most half full, each entry keeping its hash): no node allocation per key, one probe
	// sequence per lookup (US-T42; it was a std::unordered_map<K, int>).
	template <class K, class V, class H = Hash<K>, class Eq = EqualFor_t<H, K>> class Dictionary
	{
	public:
		struct Entry { K Key; V Value; bool Used = false; int NextFree = -1; size_t HashCode = 0; };
		using KeyValuePair = std::pair<const K&, V&>;

		int Count() const { return count; }
		bool ContainsKey(const K& k) const { return Find(k) >= 0; }
		bool TryGetValue(const K& k, V& out) const { int i = Find(k); if (i < 0) return false; out = entries[static_cast<size_t>(i)].Value; return true; }
		// Indexer get throws KeyNotFoundException; set adds or replaces.
		const V& operator[](const K& k) const { int i = Find(k); if (i < 0) throw KeyNotFoundException("key"); return entries[static_cast<size_t>(i)].Value; }
		V& operator[](const K& k) { int i = Find(k); if (i >= 0) return entries[static_cast<size_t>(i)].Value; return Insert(k, V{}); }
		V& At(const K& k) { int i = Find(k); if (i < 0) throw KeyNotFoundException("key"); return entries[static_cast<size_t>(i)].Value; }
		const V& At(const K& k) const { return (*this)[k]; }
		void Set(const K& k, const V& v) { (*this)[k] = v; }
		void Add(const K& k, const V& v) { if (ContainsKey(k)) throw ArgumentException("An item with the same key has already been added."); Insert(k, v); }
		bool TryAdd(const K& k, const V& v) { if (ContainsKey(k)) return false; Insert(k, v); return true; }
		V GetValueOrDefault(const K& k, const V& fallback = V{}) const { V v; return TryGetValue(k, v) ? v : fallback; }
		bool Remove(const K& k)
		{
			size_t h = HashOf(k);
			size_t b = BucketOf(k, h);
			if (b == NoBucket) return false;
			int slot = buckets[b];
			buckets[b] = Tombstone;
			++tombstones;
			Entry& e = entries[static_cast<size_t>(slot)];
			e = Entry{};
			e.NextFree = freeList;
			freeList = slot;
			--count;
			return true;
		}
		void Clear() { entries.clear(); std::fill(buckets.begin(), buckets.end(), Empty); tombstones = 0; freeList = -1; count = 0; }

		List<K> Keys() const { List<K> r; r.Raw().reserve(static_cast<size_t>(count)); for (const auto& e : entries) if (e.Used) r.Add(e.Key); return r; }
		List<V> Values() const { List<V> r; r.Raw().reserve(static_cast<size_t>(count)); for (const auto& e : entries) if (e.Used) r.Add(e.Value); return r; }

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
		static constexpr int Empty = -1, Tombstone = -2;
		static constexpr size_t NoBucket = static_cast<size_t>(-1);
		// The comparer's hash, mixed (Fibonacci) so identity-like hashes still spread over the table's low bits.
		static size_t HashOf(const K& k) { return static_cast<size_t>(H{}(k)) * 0x9E3779B97F4A7C15ull; }
		size_t Home(size_t h) const { return h >> shift; } // the product's top bits: Fibonacci hashing
		// The bucket holding k, or NoBucket.
		size_t BucketOf(const K& k, size_t h) const
		{
			if (count == 0) return NoBucket;
			const size_t mask = buckets.size() - 1;
			for (size_t b = Home(h);; b = (b + 1) & mask)
			{
				int s = buckets[b];
				if (s == Empty) return NoBucket;
				if (s >= 0)
				{
					const Entry& e = entries[static_cast<size_t>(s)];
					if (e.HashCode == h && Eq{}(e.Key, k)) return b;
				}
			}
		}
		int Find(const K& k) const { size_t b = BucketOf(k, HashOf(k)); return b == NoBucket ? -1 : buckets[b]; }
		void Place(int slot, size_t h)
		{
			const size_t mask = buckets.size() - 1;
			size_t b = Home(h);
			while (buckets[b] >= 0) b = (b + 1) & mask;
			if (buckets[b] == Tombstone) --tombstones;
			buckets[b] = slot;
		}
		void Grow()
		{
			// At most half full counting tombstones; a rehash drops the tombstones.
			size_t used = static_cast<size_t>(count) + 1;
			if (!buckets.empty() && (used + tombstones) * 2 <= buckets.size()) return;
			size_t n = 8;
			while (n < used * 2) n *= 2;
			buckets.assign(n, Empty);
			shift = 64 - static_cast<int>(std::countr_zero(n));
			tombstones = 0;
			for (size_t i = 0; i < entries.size(); ++i)
				if (entries[i].Used) Place(static_cast<int>(i), entries[i].HashCode);
		}
		V& Insert(const K& k, const V& v)
		{
			size_t h = HashOf(k);
			Grow();
			int slot;
			if (freeList >= 0) { slot = freeList; freeList = entries[static_cast<size_t>(slot)].NextFree; }
			else { slot = static_cast<int>(entries.size()); entries.emplace_back(); }
			Entry& e = entries[static_cast<size_t>(slot)];
			e.Key = k; e.Value = v; e.Used = true; e.NextFree = -1; e.HashCode = h;
			Place(slot, h);
			++count;
			return e.Value;
		}
		std::vector<Entry> entries;
		std::vector<int> buckets;
		int shift = 64;
		size_t tombstones = 0;
		int freeList = -1;
		int count = 0;
	};

	// ---------------------------------------------------------------- HashSet<T> --------------------------------
	// Same slot layout as Dictionary, so enumeration order matches .NET's HashSet.
	template <class T, class H = Hash<T>, class Eq = EqualFor_t<H, T>> class HashSet
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
			explicit Iterator(typename Dictionary<T, bool, H, Eq>::Iterator it) : it(it) {}
			const T& operator*() const { return (*it).first; }
			Iterator& operator++() { ++it; return *this; }
			bool operator!=(const Iterator& o) const { return it != o.it; }
		private:
			typename Dictionary<T, bool, H, Eq>::Iterator it;
		};
		Iterator begin() const { return Iterator(map.begin()); }
		Iterator end() const { return Iterator(map.end()); }
	private:
		Dictionary<T, bool, H, Eq> map;
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

	// A C# delegate *field* (not an event) of type Action<...> that code chains with `+=`: multicast, in order.
	// `d += h` appends, `d = h` replaces (`d = nullptr` clears), `d(args)` / `d.Invoke(args)` runs every handler
	// on a snapshot, `if (d)` is C#'s null check. (`-=` needs delegate equality, which std::function lacks.)
	template <class... A> class Delegate
	{
	public:
		Delegate() = default;
		Delegate(std::nullptr_t) {}
		template <class F, std::enable_if_t<std::is_invocable_v<F&, A...> && !std::is_same_v<std::decay_t<F>, Delegate>, int> = 0>
		explicit Delegate(F f) { handlers.emplace_back(std::move(f)); }
		Delegate& operator=(std::nullptr_t) { handlers.clear(); return *this; }
		template <class F, std::enable_if_t<std::is_invocable_v<F&, A...> && !std::is_same_v<std::decay_t<F>, Delegate>, int> = 0>
		Delegate& operator=(F f) { handlers.clear(); handlers.emplace_back(std::move(f)); return *this; }
		Delegate& operator+=(Action<A...> h) { if (h) handlers.push_back(std::move(h)); return *this; }
		Delegate& operator+=(const Delegate& d) { handlers.insert(handlers.end(), d.handlers.begin(), d.handlers.end()); return *this; }
		void Invoke(A... args) const { auto snapshot = handlers; for (auto& h : snapshot) h(args...); }
		void operator()(A... args) const { Invoke(args...); }
		explicit operator bool() const { return !handlers.empty(); }
		bool operator==(std::nullptr_t) const { return handlers.empty(); }
	private:
		std::vector<Action<A...>> handlers;
	};

	// ---------------------------------------------------------------- System.Random -----------------------------
	// .NET's seeded Random (Net5CompatSeedImpl: Knuth's subtractive generator), bit-exact with `new Random(seed)`
	// on .NET 5+ and on Unity's Mono. The unseeded constructor seeds from the clock (non-deterministic, as .NET).
	class SPACEGODCORE_API Random
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
		inline double Max(double a, double b) { if (IsNaNFast(a) || IsNaNFast(b)) return std::numeric_limits<double>::quiet_NaN(); return a > b ? a : (a < b ? b : (a == 0 && b == 0 ? (SignBitFast(a) ? b : a) : a)); }
		inline double Min(double a, double b) { if (IsNaNFast(a) || IsNaNFast(b)) return std::numeric_limits<double>::quiet_NaN(); return a < b ? a : (a > b ? b : (a == 0 && b == 0 ? (SignBitFast(a) ? a : b) : a)); }
		inline int Max(int a, int b) { return a > b ? a : b; }
		inline int Min(int a, int b) { return a < b ? a : b; }
		inline long long Max(long long a, long long b) { return a > b ? a : b; }
		inline long long Min(long long a, long long b) { return a < b ? a : b; }
		inline double Clamp(double v, double lo, double hi) { if (lo > hi) throw ArgumentException("min > max"); return v < lo ? lo : (v > hi ? hi : v); }
		inline int Clamp(int v, int lo, int hi) { if (lo > hi) throw ArgumentException("min > max"); return v < lo ? lo : (v > hi ? hi : v); }
		// Math.Sign(NaN) throws ArithmeticException.
		struct ArithmeticErrorT : Exception { ArithmeticErrorT() : Exception("Function does not accept floating point Not-a-Number values.") {} };
		inline ArithmeticErrorT ArithmeticError() { return {}; }
		inline int Sign(double v) { if (IsNaNFast(v)) throw ArithmeticError(); return v > 0 ? 1 : (v < 0 ? -1 : 0); }
		inline int Sign(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
		inline int Sign(long long v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
		inline float Abs(float v) { return std::fabs(v); }
		inline float Max(float a, float b) { return static_cast<float>(Max(static_cast<double>(a), static_cast<double>(b))); }
		inline float Min(float a, float b) { return static_cast<float>(Min(static_cast<double>(a), static_cast<double>(b))); }
		// Mixed argument types (C# `Math.Max(i, d)` widens the int to double; C++ would call that ambiguous).
		// Each picks the overload C#'s implicit conversions would: the common arithmetic type.
		template <class A, class B, std::enable_if_t<std::is_arithmetic_v<A> && std::is_arithmetic_v<B> && !std::is_same_v<A, B>, int> = 0>
		auto Max(A a, B b)
		{
			using C = std::common_type_t<A, B>;
			if constexpr (std::is_floating_point_v<C>) return static_cast<C>(Max(static_cast<double>(a), static_cast<double>(b)));
			else return static_cast<C>(a) > static_cast<C>(b) ? static_cast<C>(a) : static_cast<C>(b);
		}
		template <class A, class B, std::enable_if_t<std::is_arithmetic_v<A> && std::is_arithmetic_v<B> && !std::is_same_v<A, B>, int> = 0>
		auto Min(A a, B b)
		{
			using C = std::common_type_t<A, B>;
			if constexpr (std::is_floating_point_v<C>) return static_cast<C>(Min(static_cast<double>(a), static_cast<double>(b)));
			else return static_cast<C>(a) < static_cast<C>(b) ? static_cast<C>(a) : static_cast<C>(b);
		}
		template <class V, class L, class H, std::enable_if_t<std::is_arithmetic_v<V> && std::is_arithmetic_v<L> && std::is_arithmetic_v<H> && !(std::is_same_v<V, L> && std::is_same_v<L, H>), int> = 0>
		auto Clamp(V v, L lo, H hi)
		{
			using C = std::common_type_t<V, L, H>;
			C cv = static_cast<C>(v), cl = static_cast<C>(lo), ch = static_cast<C>(hi);
			if (cl > ch) throw ArgumentException("min > max");
			return cv < cl ? cl : (cv > ch ? ch : cv);
		}
		inline float Clamp(float v, float lo, float hi) { if (lo > hi) throw ArgumentException("min > max"); return v < lo ? lo : (v > hi ? hi : v); }
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
		SPACEGODCORE_API double Round(double v, int digits);                          // ToEven, as .NET (Cs.cpp)
		enum class MidpointRounding { ToEven, AwayFromZero };
		SPACEGODCORE_API double Round(double v, MidpointRounding mode);
		SPACEGODCORE_API double Round(double v, int digits, MidpointRounding mode);
	}

	// double.NaN, double.MaxValue, ... and the double.IsX tests.
	namespace Double
	{
		// .NET's double.NaN is 0.0 / 0.0: the x64 default NaN, sign bit set (0xFFF8000000000000), unlike quiet_NaN().
		constexpr double NaN = std::bit_cast<double>(0xFFF8000000000000ull);
		constexpr double PositiveInfinity = std::numeric_limits<double>::infinity();
		constexpr double NegativeInfinity = -std::numeric_limits<double>::infinity();
		constexpr double MaxValue = std::numeric_limits<double>::max();
		constexpr double MinValue = -std::numeric_limits<double>::max();
		constexpr double Epsilon = std::numeric_limits<double>::denorm_min();
		inline bool IsNaN(double v) { return IsNaNFast(v); }
		inline bool IsInfinity(double v) { return IsInfFast(v); }
		inline bool IsPositiveInfinity(double v) { return IsInfFast(v) && v > 0; }
		inline bool IsNegativeInfinity(double v) { return IsInfFast(v) && v < 0; }
		inline bool IsFinite(double v) { return IsFiniteFast(v); }
		SPACEGODCORE_API bool TryParse(std::string_view s, double& out); // invariant culture
		SPACEGODCORE_API double Parse(std::string_view s);
	}
	// float.NaN, float.MaxValue, ... (C# float is IEEE single, as C++ float).
	namespace Single
	{
		constexpr float NaN = std::bit_cast<float>(0xFFC00000u); // 0f / 0f, as .NET
		constexpr float PositiveInfinity = std::numeric_limits<float>::infinity();
		constexpr float NegativeInfinity = -std::numeric_limits<float>::infinity();
		constexpr float MaxValue = std::numeric_limits<float>::max();
		constexpr float MinValue = -std::numeric_limits<float>::max();
		constexpr float Epsilon = std::numeric_limits<float>::denorm_min();
		inline bool IsNaN(float v) { return IsNaNFast(v); }
		inline bool IsInfinity(float v) { return IsInfFast(v); }
		inline bool IsFinite(float v) { return IsFiniteFast(v); }
	}
	namespace Long
	{
		constexpr long long MaxValue = std::numeric_limits<long long>::max();
		constexpr long long MinValue = std::numeric_limits<long long>::min();
	}
	// double.CompareTo / float.CompareTo / int.CompareTo: -1, 0 or 1, with NaN equal to NaN and below every number.
	inline int CompareTo(double a, double b)
	{
		if (a < b) return -1;
		if (a > b) return 1;
		if (a == b) return 0;
		if (IsNaNFast(a)) return IsNaNFast(b) ? 0 : -1;
		return 1;
	}
	inline int CompareTo(float a, float b) { return CompareTo(static_cast<double>(a), static_cast<double>(b)); }
	inline int CompareTo(int a, int b) { return a < b ? -1 : (a > b ? 1 : 0); }
	inline int CompareTo(long long a, long long b) { return a < b ? -1 : (a > b ? 1 : 0); }
	namespace Int
	{
		constexpr int MaxValue = std::numeric_limits<int>::max();
		constexpr int MinValue = std::numeric_limits<int>::min();
		SPACEGODCORE_API bool TryParse(std::string_view s, int& out);
		SPACEGODCORE_API int Parse(std::string_view s);
	}

	// C# `a % b` on doubles is fmod (truncated, sign of a), which C++ spells std::fmod: write cs::Mod(a, b).
	inline double Mod(double a, double b) { return std::fmod(a, b); }
	inline float Mod(float a, float b) { return std::fmod(a, b); }

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
	SPACEGODCORE_API String Str(double v);
	SPACEGODCORE_API String Str(float v);
	inline String Str(int v) { return std::to_string(v); }
	inline String Str(long long v) { return std::to_string(v); }
	inline String Str(long v) { return std::to_string(v); }
	inline String Str(unsigned long v) { return std::to_string(v); }
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
	SPACEGODCORE_API String Fmt(double v, std::string_view format);
	SPACEGODCORE_API String Fmt(int v, std::string_view format);
	SPACEGODCORE_API String Fmt(long long v, std::string_view format);
	SPACEGODCORE_API String Fmt(float v, std::string_view format); // a float formats with float's precision (7 digits), as .NET does
	inline String Fmt(long v, std::string_view format) { return Fmt(static_cast<long long>(v), format); }
	SPACEGODCORE_API String Fmt(unsigned long long v, std::string_view format);
	inline String Fmt(unsigned v, std::string_view format) { return Fmt(static_cast<unsigned long long>(v), format); }
	inline String Fmt(unsigned long v, std::string_view format) { return Fmt(static_cast<unsigned long long>(v), format); }

	// String concatenation: C# `"a" + x + "b"` is `cs::Cat("a", x, "b")`, each piece through Str.
	template <class... A> String Cat(const A&... parts) { String s; ((s += Str(parts)), ...); return s; }

	// string.Format(CultureInfo.InvariantCulture, "...{0:0.###}...{1,5}", a, b): composite formatting, each
	// argument through Fmt (with a format) or Str. Supports {i}, {i:format}, {i,alignment[:format]}; "{{" "}}".
	namespace FormatDetail
	{
		template <class T> String Arg(const T& v, std::string_view f)
		{
			if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>) { if (!f.empty()) return Fmt(v, f); }
			return Str(v);
		}
		SPACEGODCORE_API String Composite(std::string_view composite, const Func<String, int, std::string_view>& arg, int count);
	}
	template <class... A> String Format(std::string_view composite, const A&... args)
	{
		Func<String, int, std::string_view> get = [&](int i, std::string_view f)
		{
			String r; int k = 0;
			((k++ == i ? (void)(r = FormatDetail::Arg(args, f)) : (void)0), ...);
			return r;
		};
		return FormatDetail::Composite(composite, get, static_cast<int>(sizeof...(A)));
	}

	namespace Strings
	{
		inline bool IsNullOrEmpty(const String& s) { return s.empty(); }
		SPACEGODCORE_API bool IsNullOrWhiteSpace(const String& s);
		SPACEGODCORE_API String ToLowerInvariant(const String& s);
		SPACEGODCORE_API String ToUpperInvariant(const String& s);
		SPACEGODCORE_API String Trim(const String& s);
		SPACEGODCORE_API String TrimStart(const String& s);
		SPACEGODCORE_API String TrimEnd(const String& s);
		SPACEGODCORE_API String Substring(const String& s, int start);
		SPACEGODCORE_API String Substring(const String& s, int start, int length);
		SPACEGODCORE_API bool StartsWith(const String& s, std::string_view prefix); // ordinal
		SPACEGODCORE_API bool EndsWith(const String& s, std::string_view suffix);   // ordinal
		SPACEGODCORE_API bool Contains(const String& s, std::string_view part);     // ordinal
		SPACEGODCORE_API int IndexOf(const String& s, std::string_view part, int start = 0);
		SPACEGODCORE_API int IndexOf(const String& s, char c, int start = 0);
		SPACEGODCORE_API int LastIndexOf(const String& s, char c);
		SPACEGODCORE_API String Replace(const String& s, std::string_view from, std::string_view to);
		SPACEGODCORE_API List<String> Split(const String& s, char sep);
		SPACEGODCORE_API List<String> Split(const String& s, std::string_view seps); // any of the characters
		SPACEGODCORE_API String PadLeft(const String& s, int width, char pad = ' ');
		SPACEGODCORE_API String PadRight(const String& s, int width, char pad = ' ');
		SPACEGODCORE_API int CompareOrdinal(const String& a, const String& b);
		SPACEGODCORE_API bool EqualsOrdinalIgnoreCase(const String& a, const String& b);
		// string.Join(sep, items): each item through Str.
		template <class C> String Join(std::string_view sep, const C& items)
		{
			String r; bool first = true;
			for (const auto& v : items) { if (!first) r += sep; r += Str(v); first = false; }
			return r;
		}
	}

	// A string key compared StringComparer.OrdinalIgnoreCase, for code that keys a Dictionary/HashSet on the key
	// type rather than passing OrdinalIgnoreCaseComparer: `cs::HashSet<cs::NoCase>`. Keeps the original spelling
	// (what enumeration yields, as .NET stores the first-added spelling).
	struct NoCase
	{
		String Value;
		NoCase() = default;
		NoCase(const String& v) : Value(v) {}
		NoCase(const char* v) : Value(v) {}
		bool operator==(const NoCase& o) const { return OrdinalIgnoreCaseComparer{}(Value, o.Value); }
		size_t HashCode() const { return OrdinalIgnoreCaseComparer{}(Value); }
		String ToString() const { return Value; }
	};

	// C# `foreach (char c in s)`: visits the UTF-16 code units of the (UTF-8) string, as .NET's char does
	// (string.GetHashCode-style loops `h = h * 31 + c` need the UTF-16 units, not the UTF-8 bytes).
	template <class F> void ForEachUtf16(const String& s, F visit)
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

	// System.Char over the UTF-8 strings: a C# `char` is one UTF-16 code unit, here a uint32_t holding it.
	// `s[i]` for a char -> `cs::Char::At(s, i)` (the i-th UTF-16 unit); IsLetter / ToUpperInvariant / ToLowerInvariant
	// follow .NET's tables (simple case mapping, invariant culture) for Latin (Basic, Latin-1, Extended-A/B), Greek and
	// Cyrillic; other scripts (none in the Core's text) are not letters and map to themselves. `tag += c` ->
	// `cs::Char::Append(tag, c)` (UTF-8). Surrogate units are not letters, as in .NET.
	namespace Char
	{
		SPACEGODCORE_API uint32_t At(const String& s, int utf16Index);
		SPACEGODCORE_API bool IsLetter(uint32_t c);
		SPACEGODCORE_API uint32_t ToUpperInvariant(uint32_t c);
		SPACEGODCORE_API uint32_t ToLowerInvariant(uint32_t c);
		SPACEGODCORE_API void Append(String& s, uint32_t c);
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
	// OrderBy / OrderByDescending are stable sorts in LINQ, unlike List.Sort. Keys compare with cs::Compare
	// (.NET's Comparer<TKey>.Default).
	template <class C, class KeyF> auto OrderBy(const C& c, KeyF key)
	{
		List<typename C::value_type> r; for (const auto& v : c) r.Add(v);
		std::stable_sort(r.Raw().begin(), r.Raw().end(), [&](const auto& a, const auto& b) { return Compare(key(a), key(b)) < 0; });
		return r;
	}
	template <class C, class KeyF> auto OrderByDescending(const C& c, KeyF key)
	{
		List<typename C::value_type> r; for (const auto& v : c) r.Add(v);
		std::stable_sort(r.Raw().begin(), r.Raw().end(), [&](const auto& a, const auto& b) { return Compare(key(b), key(a)) < 0; });
		return r;
	}
}

// ---------------------------------------------------------------- [Flags] enums ---------------------------------
// `CS_FLAGS(MyFlags)` after a `[Flags] enum class MyFlags : int` gives it C#'s bitwise operators (| & ^ ~ |= &= ^=);
// cs::HasFlag(v, f) is `v.HasFlag(f)` and cs::Any(v) is `v != 0`. Use it at namespace scope, beside the enum.
#define CS_FLAGS(E) \
	[[maybe_unused]] constexpr E operator|(E a, E b) { using U = std::underlying_type_t<E>; return static_cast<E>(static_cast<U>(a) | static_cast<U>(b)); } \
	[[maybe_unused]] constexpr E operator&(E a, E b) { using U = std::underlying_type_t<E>; return static_cast<E>(static_cast<U>(a) & static_cast<U>(b)); } \
	[[maybe_unused]] constexpr E operator^(E a, E b) { using U = std::underlying_type_t<E>; return static_cast<E>(static_cast<U>(a) ^ static_cast<U>(b)); } \
	[[maybe_unused]] constexpr E operator~(E a) { using U = std::underlying_type_t<E>; return static_cast<E>(~static_cast<U>(a)); } \
	[[maybe_unused]] constexpr E& operator|=(E& a, E b) { return a = a | b; } \
	[[maybe_unused]] constexpr E& operator&=(E& a, E b) { return a = a & b; } \
	[[maybe_unused]] constexpr E& operator^=(E& a, E b) { return a = a ^ b; }

namespace cs
{
	// Enum.HasFlag: (value & flag) == flag.
	template <class E, std::enable_if_t<std::is_enum_v<E>, int> = 0> constexpr bool HasFlag(E value, E flag)
	{
		using U = std::underlying_type_t<E>;
		return (static_cast<U>(value) & static_cast<U>(flag)) == static_cast<U>(flag);
	}
	// Any bit set: C# `(flags & X) != 0` is `cs::Any(flags & X)`.
	template <class E, std::enable_if_t<std::is_enum_v<E>, int> = 0> constexpr bool Any(E value) { return static_cast<std::underlying_type_t<E>>(value) != 0; }
}

#include "Cs/CsSort.inl"
#include "Cs/CsDateTime.h"
// ---------------------------------------------------------------- layout guard (US-T49) --------------------------
// Cs.h's containers are templates, so every object file carries its own inline copy of their code and its own idea
// of their layout; the linker keeps one copy of each function, picked from any object. Objects built against two
// versions of this header therefore mix silently: US-T42 step 3 added Dictionary::shift (64 -> 72 bytes) and an
// incremental UE build that kept objects compiled from the step-2 header (the header was edited while UBT compiled,
// so those objects looked newer than it) read `shift` from where step 2 keeps `tombstones` (0): Home(h) = h, an
// access violation reading 0xffffffffffffffff in BucketOf (or a quiet heap overwrite in Place).
// So every translation unit that includes Cs.h checks, while its module loads, that it agrees with Cs.cpp's (the
// SpaceGodCore DLL's, for the game module) on this fingerprint, and stops with a clear message if not. Sizes change
// on their own; bump CsAbiRevision for a change that keeps the sizes but changes what inline code means (a different
// Home, hash, probe or growth rule), so that a stale object cannot place keys where a fresh one looks them up.
namespace cs::Abi
{
	inline constexpr uint64_t CsAbiRevision = 3; // 3: US-T42 step 3 (Fibonacci Home, value hashes)
	inline constexpr uint64_t Fingerprint = (((((CsAbiRevision * 1009 + sizeof(Dictionary<int, int>)) * 1009 + sizeof(Dictionary<int, int>::Entry)) * 1009
		+ sizeof(HashSet<int>)) * 1009 + sizeof(List<int>)) * 1009 + sizeof(Queue<int>)) * 1009 + sizeof(Stack<int>);
	static_assert(sizeof(Dictionary<int, int>) == 72, "cs::Dictionary's layout changed: also bump cs::Abi::CsAbiRevision, then fix this check");
	// Compares an object's fingerprint with Cs.cpp's (the Core module's); prints both and aborts when they differ.
	SPACEGODCORE_API int Check(uint64_t objectFingerprint);
	namespace
	{
		// One per translation unit (internal linkage, so the linker cannot fold it into another object's copy).
		[[maybe_unused]] const int checked = Check(Fingerprint);
	}
}
