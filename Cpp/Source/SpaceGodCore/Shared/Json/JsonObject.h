#pragma once
// Port of Unity.Shared/Runtime/Core/Json/JsonObject.cs (JsonObject, JsonArray, JsonNumber).
//
// C# holds a JSON value as `object` (string, bool, JsonNumber, JsonObject, JsonArray or null); here that is
// JsonValue, a small tagged value. `v is string s` -> `if (auto s = v.AsString())`, `v as JsonObject` ->
// `v.AsObject()`, `v == null` -> `v.IsNull()`. Constructing a JsonValue from a number normalises it to a
// JsonNumber, as JsonObject.Normalize does, so `obj->Set("x", 0.5f)` works as in C#.

#include "Cs/Cs.h"

#include <variant>

namespace SpaceGod::Shared::Json
{
	class JsonObject;
	class JsonArray;
	class JsonNumber;

	/// <summary>A C# `object` holding a JSON value: null, string, bool, JsonNumber, JsonObject or JsonArray.</summary>
	class SPACEGODCORE_API JsonValue
	{
	public:
		JsonValue() = default;
		JsonValue(std::nullptr_t) {}
		JsonValue(const cs::String& s) : v(s) {}
		JsonValue(cs::String&& s) : v(std::move(s)) {}
		JsonValue(const char* s) : v(cs::String(s)) {}
		JsonValue(bool b) : v(b) {}
		JsonValue(cs::Ref<JsonNumber> n);
		JsonValue(cs::Ref<JsonObject> o);
		JsonValue(cs::Ref<JsonArray> a);
		// Numbers of any CLR numeric type are stored as JsonNumber (JsonObject.Normalize).
		JsonValue(int i);
		JsonValue(long i);
		JsonValue(long long i);
		JsonValue(unsigned i);
		JsonValue(unsigned long i);
		JsonValue(unsigned long long i);
		JsonValue(float f);
		JsonValue(double d);

		bool IsNull() const { return v.index() == 0; }
		const cs::String* AsString() const { return std::get_if<cs::String>(&v); }
		const bool* AsBool() const { return std::get_if<bool>(&v); }
		cs::Ref<JsonNumber> AsNumber() const { auto p = std::get_if<cs::Ref<JsonNumber>>(&v); return p ? *p : nullptr; }
		cs::Ref<JsonObject> AsObject() const { auto p = std::get_if<cs::Ref<JsonObject>>(&v); return p ? *p : nullptr; }
		cs::Ref<JsonArray> AsArray() const { auto p = std::get_if<cs::Ref<JsonArray>>(&v); return p ? *p : nullptr; }
		bool IsString() const { return AsString() != nullptr; }
		bool IsBool() const { return AsBool() != nullptr; }
		bool IsNumber() const { return v.index() == 3; }
		bool IsObject() const { return v.index() == 4; }
		bool IsArray() const { return v.index() == 5; }

		/// <summary>C#'s object.Equals: strings and bools by value, a JsonNumber by its text, objects and arrays by reference.</summary>
		bool operator==(const JsonValue& o) const;
		bool operator==(std::nullptr_t) const { return IsNull(); }

		/// <summary>C#'s object.ToString() for the value ("True"/"False" for a bool, a JsonObject's JSON, "" for null).</summary>
		cs::String ToString() const;

	private:
		std::variant<std::monostate, cs::String, bool, cs::Ref<JsonNumber>, cs::Ref<JsonObject>, cs::Ref<JsonArray>> v;
	};

	/// <summary>
	/// A JSON object that keeps its keys in the order they were read or first set, so a settings file
	/// written back looks like the one that was read, and fields this code knows nothing about survive.
	/// Values are <see cref="string"/>, <see cref="bool"/>, <see cref="JsonNumber"/>, <see cref="JsonObject"/>,
	/// <see cref="JsonArray"/> or null (JSON null).
	/// </summary>
	class SPACEGODCORE_API JsonObject final
	{
		cs::List<cs::String> keys;
		cs::Dictionary<cs::String, JsonValue> values; // StringComparer.Ordinal

	public:
		/// <summary>How many fields the object has.</summary>
		int Count() const { return keys.Count(); }

		/// <summary>The field names, in file order.</summary>
		const cs::List<cs::String>& Keys() const { return keys; }

		/// <summary>Whether the object has a field called <paramref name="key"/> (even one holding null).</summary>
		bool ContainsKey(const cs::String& key) const { return values.ContainsKey(key); }

		/// <summary>The raw value of <paramref name="key"/>, or null when it is missing.</summary>
		/// (The C# indexer's setter is Set.)
		JsonValue operator[](const cs::String& key) const;

		/// <summary>The raw value of <paramref name="key"/>, if there is one.</summary>
		bool TryGetValue(const cs::String& key, JsonValue& value) const { return values.TryGetValue(key, value); }

		/// <summary>
		/// Sets <paramref name="key"/>, keeping its place if it is already there. Numbers of any CLR numeric
		/// type are stored as <see cref="JsonNumber"/>; anything else must already be a JSON value.
		/// </summary>
		JsonObject& Set(const cs::String& key, JsonValue value);

		/// <summary>Removes <paramref name="key"/>; false when it was not there.</summary>
		bool Remove(const cs::String& key);

		/// <summary>The string at <paramref name="key"/>, or <paramref name="fallback"/> when it is missing or not a string.</summary>
		/// (C#'s null fallback is the empty string, per docs/MIGRATION.md.)
		cs::String GetString(const cs::String& key, const cs::String& fallback = cs::String()) const;

		/// <summary>The boolean at <paramref name="key"/>, or <paramref name="fallback"/> when it is missing or not a boolean.</summary>
		bool GetBool(const cs::String& key, bool fallback = false) const;

		/// <summary>The number at <paramref name="key"/>, or <paramref name="fallback"/> when it is missing, not a number or not finite.</summary>
		double GetDouble(const cs::String& key, double fallback = 0) const;

		/// <summary>The number at <paramref name="key"/> as a float, or <paramref name="fallback"/>.</summary>
		float GetFloat(const cs::String& key, float fallback = 0) const;

		/// <summary>The whole number at <paramref name="key"/>, or <paramref name="fallback"/> when it is missing or not a whole number in range.</summary>
		long long GetLong(const cs::String& key, long long fallback = 0) const;

		/// <summary>The whole number at <paramref name="key"/> as an int, or <paramref name="fallback"/>.</summary>
		int GetInt(const cs::String& key, int fallback = 0) const;

		/// <summary>The object at <paramref name="key"/>, or null when it is missing or not an object.</summary>
		cs::Ref<JsonObject> GetObject(const cs::String& key) const;

		/// <summary>The object at <paramref name="key"/>, put there (replacing a non-object) when it is missing.</summary>
		cs::Ref<JsonObject> GetOrAddObject(const cs::String& key);

		/// <summary>The array at <paramref name="key"/>, or null when it is missing or not an array.</summary>
		cs::Ref<JsonArray> GetArray(const cs::String& key) const;

		/// <summary>A deep copy: nothing in it is shared with this object.</summary>
		cs::Ref<JsonObject> Clone() const;

		/// <summary>This object as indented JSON text, written by <see cref="MiniJson"/>.</summary>
		cs::String ToJson() const;

		/// <summary>Reads JSON text whose top level is an object, with <see cref="MiniJson"/>. Throws <see cref="JsonFormatException"/> on bad text.</summary>
		static cs::Ref<JsonObject> Parse(const cs::String& json);

		/// <inheritdoc />
		/// GetEnumerator: `for (auto [key, value] : *obj)` walks the fields in order.
		class Iterator
		{
		public:
			Iterator(const JsonObject* o, int i) : o(o), i(i) {}
			std::pair<const cs::String&, const JsonValue&> operator*() const { const cs::String& k = o->keys[i]; return { k, o->values[k] }; }
			Iterator& operator++() { ++i; return *this; }
			bool operator!=(const Iterator& other) const { return i != other.i; }
		private:
			const JsonObject* o; int i;
		};
		Iterator begin() const { return Iterator(this, 0); }
		Iterator end() const { return Iterator(this, keys.Count()); }

		/// <inheritdoc />
		cs::String ToString() const { return ToJson(); }

		static JsonValue CloneValue(const JsonValue& v);

		// Normalize is the JsonValue constructors here: a JsonValue can only hold a JSON value.
		static JsonValue Normalize(const JsonValue& value) { return value; }
	};

	/// <summary>A JSON array; its items are JSON values as in <see cref="JsonObject"/>.</summary>
	/// (Its members are exported one by one: exporting a class derived from cs::List would instantiate every
	/// List member, Sort() included, which JsonValue -- having no order -- cannot.)
	class JsonArray final : public cs::List<JsonValue>
	{
	public:
		/// <summary>An empty array.</summary>
		JsonArray() {}

		/// <summary>An array of <paramref name="items"/>, each normalised as by <see cref="JsonObject.Set"/>.</summary>
		SPACEGODCORE_API explicit JsonArray(const cs::List<JsonValue>& items);

		/// <summary>A deep copy.</summary>
		SPACEGODCORE_API cs::Ref<JsonArray> Clone() const;
	};

	/// <summary>
	/// A JSON number, kept as the exact text it was read as so it is written back unchanged
	/// (no 0.6 turning into 0.6000000238 and no large integer losing digits).
	/// </summary>
	class SPACEGODCORE_API JsonNumber final
	{
	public:
		/// <summary>The number's JSON text.</summary>
		const cs::String Raw;

		/// <summary>The number as a double (an out-of-range number reads as an infinity).</summary>
		double Value() const;

		/// <summary>A number from its JSON text; throws <see cref="FormatException"/> when the text is not a JSON number.</summary>
		static cs::Ref<JsonNumber> FromRaw(const cs::String& raw);

		/// <summary>A whole number.</summary>
		static cs::Ref<JsonNumber> From(long long value);

		/// <summary>A float, written in its shortest round-trip form. NaN and infinities become 0.</summary>
		static cs::Ref<JsonNumber> From(float value);

		/// <summary>A double, written in its shortest round-trip form. NaN and infinities become 0.</summary>
		static cs::Ref<JsonNumber> From(double value);

		// (From(decimal): C++ has no decimal; nothing in SpaceGod passes one.)

		/// <inheritdoc />
		bool Equals(const cs::Ref<JsonNumber>& other) const { return other && other->Raw == Raw; }

		/// <inheritdoc />
		size_t HashCode() const { return std::hash<cs::String>{}(Raw); }

		/// <inheritdoc />
		cs::String ToString() const { return Raw; }

		explicit JsonNumber(cs::String raw) : Raw(std::move(raw)) {} // private in C#; public for cs::New
	};
}
