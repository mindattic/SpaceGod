// Port of Unity.Shared/Runtime/Core/Profiles/AtomicFile.cs.

#include "Shared/Profiles/AtomicFile.h"
#include "Shared/Profiles/SharedIo.h"

namespace SpaceGod::Shared
{
	// (Utf8NoBom: a cs::String is already UTF-8, written without a BOM.)
	void AtomicFile::WriteAllText(const cs::String& path, const cs::String& text)
	{
		WriteAllBytes(path, cs::List<uint8_t>(text.begin(), text.end()));
	}

	void AtomicFile::WriteAllBytes(const cs::String& path, const cs::List<uint8_t>& bytes)
	{
		if (path.empty()) throw cs::ArgumentNullException("path");
		cs::String full = Io::GetFullPath(path);
		Io::CreateFolder(Io::GetDirectoryName(full));
		cs::String temp = full + TempSuffix;
		try
		{
			Io::WriteAllBytesDurable(temp, bytes);
			if (!Io::FileExists(full))
			{
				Io::MoveFileTo(temp, full);
				return;
			}
			// One step: deleting the old file first would leave none at all if the move then failed.
			// (C#: File.Replace, with a copy-and-delete fallback where the platform has no replace; a rename
			// over the old file is that one step on every platform std::filesystem supports.)
			Io::ReplaceFileWith(temp, full);
		}
		catch (...)
		{
			TryDelete(temp);
			throw;
		}
	}

	void AtomicFile::TryDelete(const cs::String& path)
	{
		try
		{
			if (Io::FileExists(path)) Io::RemoveFile(path);
		}
		catch (const cs::IOException&)
		{
		}
	}
}
