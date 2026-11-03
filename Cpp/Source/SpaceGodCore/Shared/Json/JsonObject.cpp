// Port of Unity.Shared/Runtime/Core/Json/JsonObject.cs.

#include "Shared/Json/JsonObject.h"
#include "Shared/Json/MiniJson.h"

namespace SpaceGod::Shared::Json
{
	// ------------------------------------------------------------------ JsonValue (C# object + Normalize)
	JsonValue::JsonValue(cs::Ref<JsonNumber> n) { if (n) v = std::move(n); }
	JsonValue::JsonValue(cs::Ref<JsonObject> o) { if (o) v = std::move(o); }
	JsonValue::JsonValue(cs::Ref<JsonArray> a) { if (a) v = std::move(a); }
	JsonValue::JsonValue(int i) : v(JsonNumber::From(static_cast<long long>(i))) {}
	JsonValue::JsonValue(long i) : v(JsonNumber::From(static_cast<long long>(i))) {}
	JsonValue::JsonValue(long long i) : v(JsonNumber::From(i)) {}
	JsonValue::JsonValue(unsigned i) : v(JsonNumber::From(static_cast<long long>(i))) {}
	JsonValue::JsonValue(unsigned long i) : v(JsonNumber::From(static_cast<long long>(i))) {}
	// case ulong ul: return JsonNumber.FromRaw(ul.ToString(CultureInfo.InvariantCulture));
	JsonValue::JsonValue(unsigned long long i) : v(JsonNumber::FromRaw(cs::Str(i))) {}
	JsonValue::JsonValue(float f) : v(JsonNumber::From(f)) {}
	JsonValue::JsonValue(double d) : v(JsonNumber::From(d)) {}

	bool JsonValue::operator==(const JsonValue& o) const
	{
		if (v.index() != o.v.index()) return false;
		switch (v.index())
		{
		case 0: return true;
		case 1: return *AsString() == *o.AsString();
		case 2: return *AsBool() == *o.AsBool();
		case 3: return AsNumber()->Equals(o.AsNumber());
		case 4: return AsObject() == o.AsObject();
		default: return AsArray() == o.AsArray();
		}
	}

	cs::String JsonValue::ToString() const
	{
		switch (v.index())
		{
		case 0: return cs::String();
		case 1: return *AsString();
		case 2: return cs::Str(*AsBool());
		case 3: return AsNumber()->ToString();
		case 4: return AsObject()->ToString();
		default: return "Unity.Shared.Core.Json.JsonArray"; // List<object>.ToString() is the type name
		}
	}

	// ------------------------------------------------------------------ JsonObject
	JsonValue JsonObject::operator[](const cs::String& key) const
	{
		JsonValue v;
		return values.TryGetValue(key, v) ? v : JsonValue();
	}

	JsonObject& JsonObject::Set(const cs::String& key, JsonValue value)
	{
		// (C#: `if (key == null) throw new ArgumentNullException(nameof(key));` -- a cs::String is never null.)
		value = Normalize(value);
		if (!values.ContainsKey(key)) keys.Add(key);
		values[key] = value;
		return *this;
	}

	bool JsonObject::Remove(const cs::String& key)
	{
		if (!values.Remove(key)) return false;
		keys.Remove(key);
		return true;
	}

	cs::String JsonObject::GetString(const cs::String& key, const cs::String& fallback) const
	{
		JsonValue v = (*this)[key];
		if (auto s = v.AsString()) return *s;
		return fallback;
	}

	bool JsonObject::GetBool(const cs::String& key, bool fallback) const
	{
		JsonValue v = (*this)[key];
		if (auto b = v.AsBool()) return *b;
		return fallback;
	}

	double JsonObject::GetDouble(const cs::String& key, double fallback) const
	{
		auto n = (*this)[key].AsNumber();
		if (!n) return fallback;
		double d = n->Value();
		return cs::Double::IsNaN(d) || cs::Double::IsInfinity(d) ? fallback : d;
	}

	float JsonObject::GetFloat(const cs::String& key, float fallback) const { return static_cast<float>(GetDouble(key, fallback)); }

	namespace
	{
		// long.TryParse(s, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out long l)
		bool TryParseLong(const cs::String& s, long long& out)
		{
			out = 0;
			size_t i = 0;
			bool neg = false;
			if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
			if (i >= s.size()) return false;
			unsigned long long m = 0;
			const unsigned long long limit = neg ? 9223372036854775808ull : 9223372036854775807ull;
			for (; i < s.size(); i++)
			{
				char c = s[i];
				if (c < '0' || c > '9') return false;
				unsigned d = static_cast<unsigned>(c - '0');
				if (m > (limit - d) / 10) return false;
				m = m * 10 + d;
			}
			out = neg ? static_cast<long long>(0ull - m) : static_cast<long long>(m);
			return true;
		}
	}

	long long JsonObject::GetLong(const cs::String& key, long long fallback) const
	{
		long long l;
		auto n = (*this)[key].AsNumber();
		return n && TryParseLong(n->Raw, l) ? l : fallback;
	}

	int JsonObject::GetInt(const cs::String& key, int fallback) const
	{
		long long l = GetLong(key, fallback);
		return l < cs::Int::MinValue || l > cs::Int::MaxValue ? fallback : static_cast<int>(l);
	}

	cs::Ref<JsonObject> JsonObject::GetObject(const cs::String& key) const { return (*this)[key].AsObject(); }

	cs::Ref<JsonObject> JsonObject::GetOrAddObject(const cs::String& key)
	{
		if (auto o = (*this)[key].AsObject()) return o;
		auto o = cs::New<JsonObject>();
		Set(key, o);
		return o;
	}

	cs::Ref<JsonArray> JsonObject::GetArray(const cs::String& key) const { return (*this)[key].AsArray(); }

	cs::Ref<JsonObject> JsonObject::Clone() const
	{
		auto copy = cs::New<JsonObject>();
		for (const auto& key : keys) copy->Set(key, CloneValue(values[key]));
		return copy;
	}

	cs::String JsonObject::ToJson() const
	{
		return MiniJson::Write(*this); // MiniJson.Write(this)
	}

	cs::Ref<JsonObject> JsonObject::Parse(const cs::String& json) { return MiniJson::ParseObject(json); }

	JsonValue JsonObject::CloneValue(const JsonValue& v)
	{
		if (auto o = v.AsObject()) return o->Clone();
		if (auto a = v.AsArray()) return a->Clone();
		return v;
	}

	// ------------------------------------------------------------------ JsonArray
	JsonArray::JsonArray(const cs::List<JsonValue>& items)
	{
		for (const auto& item : items) Add(JsonObject::Normalize(item));
	}

	cs::Ref<JsonArray> JsonArray::Clone() const
	{
		auto copy = cs::New<JsonArray>();
		for (const auto& item : *this) copy->Add(JsonObject::CloneValue(item));
		return copy;
	}

	// ------------------------------------------------------------------ JsonNumber
	double JsonNumber::Value() const
	{
		// Unity's Mono/IL2CPP class library (.NET Framework rules) throws OverflowException for an
		// out-of-range number where .NET Core returns an infinity; Raw is always a valid JSON number,
		// so a failed parse can only be an overflow.
		double d;
		if (cs::Double::TryParse(Raw, d)) return d;
		return cs::Strings::StartsWith(Raw, "-") ? cs::Double::NegativeInfinity : cs::Double::PositiveInfinity;
	}

	cs::Ref<JsonNumber> JsonNumber::FromRaw(const cs::String& raw)
	{
		if (!MiniJson::IsNumber(raw)) throw cs::FormatException(cs::Cat("Not a JSON number: ", raw));
		return cs::New<JsonNumber>(raw);
	}

	cs::Ref<JsonNumber> JsonNumber::From(long long value) { return cs::New<JsonNumber>(cs::Str(value)); }

	cs::Ref<JsonNumber> JsonNumber::From(float value)
	{
		return cs::Single::IsNaN(value) || cs::Single::IsInfinity(value) ? cs::New<JsonNumber>("0") : cs::New<JsonNumber>(cs::Fmt(value, "R"));
	}

	cs::Ref<JsonNumber> JsonNumber::From(double value)
	{
		return cs::Double::IsNaN(value) || cs::Double::IsInfinity(value) ? cs::New<JsonNumber>("0") : cs::New<JsonNumber>(cs::Fmt(value, "R"));
	}
}
