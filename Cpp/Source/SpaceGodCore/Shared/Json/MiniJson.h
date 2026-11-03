#pragma once
// Port of Unity.Shared/Runtime/Core/Json/MiniJson.cs (MiniJson, JsonFormatException, IJsonCodec, MiniJsonCodec).
// Text is UTF-8 here where C# strings are UTF-16: positions in error messages count bytes, not UTF-16 units.

#include "Cs/Cs.h"
#include "Shared/Json/JsonObject.h"

namespace SpaceGod::Shared::Json
{
	/// <summary>
	/// Reads and writes JSON text for the settings format: settings files must be readable in plain dotnet
	/// tests and in Unity, so this package carries its own small reader/writer instead of a second copy of
	/// Newtonsoft.Json (games already ship one) or System.Text.Json (not in Unity's .NET Standard 2.1 profile).
	/// It is strict RFC 8259 JSON, keeps key order and number text, and writes two-space indented LF text.
	/// </summary>
	class SPACEGODCORE_API MiniJson
	{
	public:
		/// <summary>The deepest nesting the reader accepts, so a hostile file cannot overflow the stack.</summary>
		static constexpr int MaxDepth = 128;

		/// <summary>Reads <paramref name="json"/>, whose top level must be an object. Throws <see cref="JsonFormatException"/> on anything else.</summary>
		static cs::Ref<JsonObject> ParseObject(const cs::String& json);

		/// <summary>Reads any JSON value. Throws <see cref="JsonFormatException"/> on bad text.</summary>
		static JsonValue Parse(const cs::String& json);

		/// <summary>Writes <paramref name="value"/> (any JSON value) as indented JSON text ending in a newline.</summary>
		static cs::String Write(const JsonValue& value);
		static cs::String Write(const JsonObject& value); // (C++: for JsonObject.ToJson, which has no Ref to itself)

		/// <summary>Whether <paramref name="text"/> is exactly one JSON number.</summary>
		static bool IsNumber(const cs::String& text);

	private:
		static bool IsDigit(char c) { return c >= '0' && c <= '9'; }
		static void WriteValue(cs::StringBuilder& sb, const JsonValue& value, int indent);
		static void WriteObject(cs::StringBuilder& sb, const JsonObject& o, int indent);
		static void Indent(cs::StringBuilder& sb, int level);
		static void WriteString(cs::StringBuilder& sb, const cs::String& s);
		class Reader;
	};

	/// <summary>JSON text that could not be read.</summary>
	class SPACEGODCORE_API JsonFormatException final : public cs::FormatException
	{
	public:
		/// <summary>The character offset the reader stopped at.</summary>
		const int Position;

		/// <summary>A read failure at <paramref name="position"/>.</summary>
		JsonFormatException(const cs::String& message, int position) : cs::FormatException(message), Position(position) {}
	};

	/// <summary>
	/// Turns settings text into a <see cref="JsonObject"/> and back. The package defaults to
	/// <see cref="MiniJsonCodec"/>; a game that would rather use its own JSON library (Newtonsoft, say)
	/// implements this by converting to and from <see cref="JsonObject"/>, and passes it to
	/// <see cref="ProfileStore"/>. A codec signals unreadable text by throwing any exception.
	/// </summary>
	class IJsonCodec
	{
	public:
		virtual ~IJsonCodec() = default;

		/// <summary>Reads text whose top level is a JSON object.</summary>
		virtual cs::Ref<JsonObject> Parse(const cs::String& text) = 0;

		/// <summary>Writes the object as JSON text.</summary>
		virtual cs::String Write(const cs::Ref<JsonObject>& value) = 0;
	};

	/// <summary>The default codec: <see cref="MiniJson"/>, no dependencies, works in dotnet and in Unity.</summary>
	class SPACEGODCORE_API MiniJsonCodec final : public IJsonCodec
	{
	public:
		/// <summary>The one shared instance (it has no state).</summary>
		static const cs::Ref<MiniJsonCodec>& Instance();

		/// <inheritdoc />
		cs::Ref<JsonObject> Parse(const cs::String& text) override { return MiniJson::ParseObject(text); }

		/// <inheritdoc />
		cs::String Write(const cs::Ref<JsonObject>& value) override { return MiniJson::Write(JsonValue(value)); }
	};
}
