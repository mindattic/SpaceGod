#pragma once
// Port of Unity.Shared/Runtime/Core/Profiles/AtomicFile.cs.

#include "Cs/Cs.h"

namespace SpaceGod::Shared
{
	/// <summary>
	/// Writes a file so a crash or power cut mid-write leaves either the old file or the new one, never half
	/// of one: the bytes go to <c>&lt;path&gt;.tmp</c> first, which then replaces the file in one step.
	/// </summary>
	class SPACEGODCORE_API AtomicFile
	{
	public:
		/// <summary>The suffix of the temporary file a write goes through.</summary>
		static constexpr const char* TempSuffix = ".tmp";

		/// <summary>Writes <paramref name="text"/> as UTF-8 (no BOM) to <paramref name="path"/>, atomically.</summary>
		static void WriteAllText(const cs::String& path, const cs::String& text);

		/// <summary>Writes <paramref name="bytes"/> to <paramref name="path"/>, atomically. The folder is made if needed.</summary>
		static void WriteAllBytes(const cs::String& path, const cs::List<uint8_t>& bytes);

	private:
		static void TryDelete(const cs::String& path);
	};
}
