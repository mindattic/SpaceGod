// CaptureBackend: Unity's ScreenCapture.CaptureScreenshot for CaptureRunner (US-T31), on Unreal's screenshot request
// with the UI in the shot (Unity's capture includes the OnGUI layer; Unreal's showui takes the Slate overlay the GUI
// host draws on). Like Unity's, the file is written at the end of the frame, not by the time the call returns.

#include "CaptureRunner.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace SpaceGod::CaptureBackend
{
	void Screenshot(const cs::String& path)
	{
		FString file = FString(UTF8_TO_TCHAR(path.c_str()));
		if (FPaths::IsRelative(file)) file = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), file);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(file), true);
		FScreenshotRequest::RequestScreenshot(file, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}
}
