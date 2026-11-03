// Port of Unity.Shared/Runtime/Core/Json/MiniJson.cs.

#include "Shared/Json/MiniJson.h"

namespace SpaceGod::Shared::Json
{
	cs::Ref<JsonObject> MiniJson::ParseObject(const cs::String& json)
	{
		if (auto o = Parse(json).AsObject()) return o;
		throw JsonFormatException("The top level is not a JSON object.", 0);
	}

	cs::String MiniJson::Write(const JsonValue& value)
	{
		cs::StringBuilder sb;
		WriteValue(sb, JsonObject::Normalize(value), 0);
		sb.Append('\n');
		return sb.ToString();
	}

	cs::String MiniJson::Write(const JsonObject& value)
	{
		cs::StringBuilder sb;
		WriteObject(sb, value, 0);
		sb.Append('\n');
		return sb.ToString();
	}

	bool MiniJson::IsNumber(const cs::String& text)
	{
		if (cs::Strings::IsNullOrEmpty(text)) return false;
		size_t i = 0, n = text.size();
		if (text[i] == '-') i++;
		if (i >= n) return false;
		if (text[i] == '0') i++;
		else if (text[i] >= '1' && text[i] <= '9') { while (i < n && IsDigit(text[i])) i++; }
		else return false;
		if (i < n && text[i] == '.')
		{
			i++;
			size_t start = i;
			while (i < n && IsDigit(text[i])) i++;
			if (i == start) return false;
		}
		if (i < n && (text[i] == 'e' || text[i] == 'E'))
		{
			i++;
			if (i < n && (text[i] == '+' || text[i] == '-')) i++;
			size_t start = i;
			while (i < n && IsDigit(text[i])) i++;
			if (i == start) return false;
		}
		return i == n;
	}

	void MiniJson::WriteValue(cs::StringBuilder& sb, const JsonValue& value, int indent)
	{
		if (value.IsNull()) { sb.Append("null"); return; }
		if (auto b = value.AsBool()) { sb.Append(*b ? "true" : "false"); return; }
		if (auto s = value.AsString()) { WriteString(sb, *s); return; }
		if (auto n = value.AsNumber()) { sb.Append(n->Raw); return; }
		if (auto o = value.AsObject()) { WriteObject(sb, *o, indent); return; }
		if (auto a = value.AsArray())
		{
			if (a->Count() == 0) { sb.Append("[]"); return; }
			sb.Append('[');
			for (int i = 0; i < a->Count(); i++)
			{
				if (i > 0) sb.Append(',');
				sb.Append('\n');
				Indent(sb, indent + 1);
				WriteValue(sb, JsonObject::Normalize((*a)[i]), indent + 1);
			}
			sb.Append('\n');
			Indent(sb, indent);
			sb.Append(']');
			return;
		}
		throw cs::ArgumentException("Not a JSON value");
	}

	void MiniJson::WriteObject(cs::StringBuilder& sb, const JsonObject& o, int indent)
	{
		if (o.Count() == 0) { sb.Append("{}"); return; }
		sb.Append('{');
		bool first = true;
		for (auto [key, value] : o)
		{
			if (!first) sb.Append(',');
			first = false;
			sb.Append('\n');
			Indent(sb, indent + 1);
			WriteString(sb, key);
			sb.Append(": ");
			WriteValue(sb, value, indent + 1);
		}
		sb.Append('\n');
		Indent(sb, indent);
		sb.Append('}');
	}

	void MiniJson::Indent(cs::StringBuilder& sb, int level) { sb.Append(cs::String(static_cast<size_t>(level * 2), ' ')); }

	void MiniJson::WriteString(cs::StringBuilder& sb, const cs::String& s)
	{
		cs::String out;
		out.push_back('"');
		for (char c : s)
		{
			switch (c)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) out += cs::Cat("\\u", cs::Fmt(static_cast<int>(c), "x4"));
				else out.push_back(c); // UTF-8 bytes pass through, as C# passes the UTF-16 units through
				break;
			}
		}
		out.push_back('"');
		sb.Append(out);
	}

	class MiniJson::Reader
	{
		const cs::String& text;
		int pos = 0;

	public:
		explicit Reader(const cs::String& text) : text(text) {}

		bool AtEnd() const { return pos >= static_cast<int>(text.size()); }

		JsonFormatException Error(const cs::String& message) const { return JsonFormatException(cs::Cat(message, " at character ", pos, "."), pos); }

		void SkipBom()
		{
			// U+FEFF, as UTF-8.
			if (text.size() >= 3 && pos == 0 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) pos += 3;
		}

		void SkipWhitespace()
		{
			while (!AtEnd())
			{
				char c = text[static_cast<size_t>(pos)];
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos++;
				else break;
			}
		}

		JsonValue ReadValue(int depth)
		{
			if (depth > MaxDepth) throw Error(cs::Cat("JSON nested deeper than ", MaxDepth));
			if (AtEnd()) throw Error("Unexpected end of JSON");
			char c = text[static_cast<size_t>(pos)];
			switch (c)
			{
			case '{': return ReadObject(depth);
			case '[': return ReadArray(depth);
			case '"': return ReadString();
			case 't': Expect("true"); return true;
			case 'f': Expect("false"); return false;
			case 'n': Expect("null"); return nullptr;
			default:
				if (c == '-' || IsDigit(c)) return ReadNumber();
				throw Error(cs::Cat("Unexpected character '", c, "'"));
			}
		}

	private:
		void Expect(std::string_view word)
		{
			if (text.compare(static_cast<size_t>(pos), word.size(), word) != 0) throw Error(cs::Cat("Expected ", cs::String(word)));
			pos += static_cast<int>(word.size());
		}

		char At() const { return text[static_cast<size_t>(pos)]; }

		cs::Ref<JsonObject> ReadObject(int depth)
		{
			auto obj = cs::New<JsonObject>();
			pos++;
			SkipWhitespace();
			if (!AtEnd() && At() == '}') { pos++; return obj; }
			while (true)
			{
				SkipWhitespace();
				if (AtEnd() || At() != '"') throw Error("Expected a field name");
				cs::String key = ReadString();
				SkipWhitespace();
				if (AtEnd() || At() != ':') throw Error("Expected ':'");
				pos++;
				SkipWhitespace();
				obj->Set(key, ReadValue(depth + 1));
				SkipWhitespace();
				if (AtEnd()) throw Error("Unexpected end of JSON in an object");
				if (At() == ',') { pos++; continue; }
				if (At() == '}') { pos++; return obj; }
				throw Error("Expected ',' or '}'");
			}
		}

		cs::Ref<JsonArray> ReadArray(int depth)
		{
			auto arr = cs::New<JsonArray>();
			pos++;
			SkipWhitespace();
			if (!AtEnd() && At() == ']') { pos++; return arr; }
			while (true)
			{
				SkipWhitespace();
				arr->Add(ReadValue(depth + 1));
				SkipWhitespace();
				if (AtEnd()) throw Error("Unexpected end of JSON in an array");
				if (At() == ',') { pos++; continue; }
				if (At() == ']') { pos++; return arr; }
				throw Error("Expected ',' or ']'");
			}
		}

		static void AppendUtf8(cs::String& sb, unsigned cp)
		{
			if (cp < 0x80) sb.push_back(static_cast<char>(cp));
			else if (cp < 0x800) { sb.push_back(static_cast<char>(0xC0 | (cp >> 6))); sb.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
			else if (cp < 0x10000) { sb.push_back(static_cast<char>(0xE0 | (cp >> 12))); sb.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); sb.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
			else { sb.push_back(static_cast<char>(0xF0 | (cp >> 18))); sb.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); sb.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); sb.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
		}

		bool TryHex4(int at, unsigned& code) const
		{
			// int.TryParse(text.Substring(pos, 4), NumberStyles.AllowHexSpecifier, ...)
			if (at + 4 > static_cast<int>(text.size())) return false;
			code = 0;
			for (int k = 0; k < 4; k++)
			{
				char h = text[static_cast<size_t>(at + k)];
				unsigned d;
				if (h >= '0' && h <= '9') d = static_cast<unsigned>(h - '0');
				else if (h >= 'a' && h <= 'f') d = static_cast<unsigned>(h - 'a' + 10);
				else if (h >= 'A' && h <= 'F') d = static_cast<unsigned>(h - 'A' + 10);
				else return false;
				code = code * 16 + d;
			}
			return true;
		}

		cs::String ReadString()
		{
			pos++; // opening quote
			cs::String sb;
			// A \u escape is one UTF-16 unit in C#; a surrogate pair written as two escapes joins into one code
			// point, and a lone surrogate becomes U+FFFD (what C#'s UTF-8 encoder writes for it).
			unsigned pendingHigh = 0;
			auto flushHigh = [&]() { if (pendingHigh) { AppendUtf8(sb, 0xFFFD); pendingHigh = 0; } };
			while (true)
			{
				if (AtEnd()) throw Error("Unterminated string");
				char c = text[static_cast<size_t>(pos++)];
				if (c == '"') { flushHigh(); return sb; }
				if (static_cast<unsigned char>(c) < 0x20) throw Error("Control character in a string");
				if (c != '\\') { flushHigh(); sb.push_back(c); continue; }
				if (AtEnd()) throw Error("Unterminated escape");
				char e = text[static_cast<size_t>(pos++)];
				if (e != 'u') flushHigh();
				switch (e)
				{
				case '"': sb.push_back('"'); break;
				case '\\': sb.push_back('\\'); break;
				case '/': sb.push_back('/'); break;
				case 'b': sb.push_back('\b'); break;
				case 'f': sb.push_back('\f'); break;
				case 'n': sb.push_back('\n'); break;
				case 'r': sb.push_back('\r'); break;
				case 't': sb.push_back('\t'); break;
				case 'u':
				{
					if (pos + 4 > static_cast<int>(text.size())) throw Error("Short \\u escape");
					unsigned code;
					if (!TryHex4(pos, code)) throw Error("Bad \\u escape");
					pos += 4;
					if (code >= 0xD800 && code <= 0xDBFF) { flushHigh(); pendingHigh = code; }
					else if (code >= 0xDC00 && code <= 0xDFFF)
					{
						if (pendingHigh) { AppendUtf8(sb, 0x10000 + ((pendingHigh - 0xD800) << 10) + (code - 0xDC00)); pendingHigh = 0; }
						else AppendUtf8(sb, 0xFFFD);
					}
					else { flushHigh(); AppendUtf8(sb, code); }
					break;
				}
				default: throw Error(cs::Cat("Bad escape '\\", e, "'"));
				}
			}
		}

		cs::Ref<JsonNumber> ReadNumber()
		{
			int start = pos;
			while (!AtEnd())
			{
				char c = At();
				if (IsDigit(c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') pos++;
				else break;
			}
			cs::String raw = text.substr(static_cast<size_t>(start), static_cast<size_t>(pos - start));
			if (!IsNumber(raw)) { pos = start; throw Error(cs::Cat("Bad number '", raw, "'")); }
			return JsonNumber::FromRaw(raw);
		}
	};

	JsonValue MiniJson::Parse(const cs::String& json)
	{
		// (C#: `if (json == null) throw new ArgumentNullException(nameof(json));` -- a cs::String is never null.)
		Reader reader(json);
		reader.SkipBom();
		reader.SkipWhitespace();
		JsonValue value = reader.ReadValue(0);
		reader.SkipWhitespace();
		if (!reader.AtEnd()) throw reader.Error("Unexpected text after the JSON value");
		return value;
	}

	const cs::Ref<MiniJsonCodec>& MiniJsonCodec::Instance()
	{
		static const cs::Ref<MiniJsonCodec> instance = cs::New<MiniJsonCodec>();
		return instance;
	}
}
