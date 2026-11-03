#pragma once
// (Names avoid the Win32 macros CreateDirectory, MoveFile, DeleteFile, ReplaceFile, GetEnvironmentVariable.)
// C++-only helpers for the Profiles port (no C# counterpart): System.IO's File/Directory/Path calls the
// Unity.Shared code makes, over std::filesystem with UTF-8 strings, and the UTF-16 views C# string code
// relies on (Length counts UTF-16 units). Filesystem failures throw cs::IOException, which stands for both
// IOException and UnauthorizedAccessException (the code always catches the two together).

#include "Cs/Cs.h"

namespace SpaceGod::Shared::Io
{
	// Path.GetFullPath (absolute, normalised, the platform's separators).
	SPACEGODCORE_API cs::String GetFullPath(const cs::String& path);
	// Path.Combine(a, b).
	SPACEGODCORE_API cs::String Combine(const cs::String& a, const cs::String& b);
	// Path.GetFileName / Path.GetDirectoryName.
	SPACEGODCORE_API cs::String GetFileName(const cs::String& path);
	SPACEGODCORE_API cs::String GetDirectoryName(const cs::String& path);

	SPACEGODCORE_API bool FileExists(const cs::String& path);      // File.Exists
	SPACEGODCORE_API bool DirectoryExists(const cs::String& path); // Directory.Exists
	SPACEGODCORE_API void CreateFolder(const cs::String& path); // Directory.CreateDirectory
	// Directory.GetDirectories(path): full paths, in the file system's order.
	SPACEGODCORE_API cs::List<cs::String> GetDirectories(const cs::String& path);
	// Directory.Move / File.Move: fail when the destination exists.
	SPACEGODCORE_API void MoveFolder(const cs::String& from, const cs::String& to);
	SPACEGODCORE_API void MoveFileTo(const cs::String& from, const cs::String& to);
	// Replaces `to` with `from` in one step (File.Replace without a backup).
	SPACEGODCORE_API void ReplaceFileWith(const cs::String& from, const cs::String& to);
	SPACEGODCORE_API void RemoveFile(const cs::String& path); // File.Delete (no error when missing)
	// File.ReadAllText (UTF-8; a BOM is dropped).
	SPACEGODCORE_API cs::String ReadAllText(const cs::String& path);
	// FileStream(Create) + Write + Flush(true): writes and flushes to disk.
	SPACEGODCORE_API void WriteAllBytesDurable(const cs::String& path, const cs::List<uint8_t>& bytes);

	// Environment.GetEnvironmentVariable: empty when unset.
	SPACEGODCORE_API cs::String EnvironmentVariable(const cs::String& name);

	// string.Length: the UTF-16 length of UTF-8 text.
	SPACEGODCORE_API int Utf16Length(const cs::String& s);
	// The UTF-16 units of UTF-8 text (`foreach (char c in s)`).
	SPACEGODCORE_API cs::List<uint16_t> Utf16Units(const cs::String& s);
	// The longest prefix of at most `units` UTF-16 units that does not split a code point.
	SPACEGODCORE_API cs::String Utf16Prefix(const cs::String& s, int units);
	// Guid.NewGuid().ToString("N"): 32 random lower-case hex digits.
	SPACEGODCORE_API cs::String NewGuidN();
}
