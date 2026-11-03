#pragma once
// Port of Unity.SpaceGod Core/Session/CommandCodec.cs (US-T21, see docs/MIGRATION.md).
//
// The text written here is the co-op wire and replay format for BOTH tracks: it must stay byte-identical to the C#
// codec. Ints go through cs::Str (int.ToString(Invariant)), doubles through cs::Fmt(v, "R") (.NET Core 3.0+ shortest
// round-trip), fields are separated by one ' ', and WriteLog ends every line with '\n'.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class IGameCommand;

	/// <summary>
	/// Writes every session command as one line of text and reads it back equal (U.SPC-T410, docs/BIBLE.md U.SPC-LAW-1):
	/// what a command log, a replay or a wire would carry. Numbers are written round-trip exact in the invariant culture.
	/// <code>
	/// take 0 7
	/// release 1
	/// host 0
	/// control 0 aimX aimY aimZ throttle strafeRight strafeUp roll flags   (flags: 1 primary, 2 secondary, 4 boost)
	/// </code>
	/// </summary>
	class SPACEGODCORE_API CommandCodec
	{
	public:
		static cs::String Write(const cs::Ref<IGameCommand>& command);

		static cs::Ref<IGameCommand> Read(const cs::String& line);

		/// <summary>A command log, one command a line.</summary>
		static cs::String WriteLog(const cs::List<cs::Ref<IGameCommand>>& commands);

		static cs::List<cs::Ref<IGameCommand>> ReadLog(const cs::String& text);

	private:
		static void Need(const cs::List<cs::String>& f, int n, const cs::String& line);

		static cs::String Int(int v);
		static cs::String Num(double v);
		static int ParseInt(const cs::String& s);
		static double ParseNum(const cs::String& s);
	};
}
