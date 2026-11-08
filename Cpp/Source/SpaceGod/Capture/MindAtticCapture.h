#pragma once

// MindAttic.Capture for the C++ game (US-T72): what MindAttic.Capture's Unity package (CaptureUnity) is to a Unity
// player. C++ can't load .NET, so on -capture the game starts the bundled capture host (Capture/SpaceGod.Capture, the
// vendored MindAttic.Capture library; published by tools/package.ps1 as <package>/Capture/capture.exe), hands it its
// window and its command line, and drives it over a pipe: one JSON request a line on its stdin, one JSON reply or
// notice a line from its stdout (Capture/README.md). The host builds its options exactly as CaptureUnity does: off
// unless -capture is literally on the command line; -capture video / screen N / -capture-config path.json; output in
// R:\MindAttic\Capture\SpaceGod\. The game's events, markers, screenshots and hotkeys go through here.

#include "CoreMinimal.h"

namespace SpaceGod::MindAtticCapture
{
	/// A CaptureData bag: event data for the capture.json rules' "where" conditions and the sidecars.
	class Data
	{
	public:
		Data& Num(const TCHAR* key, double value);
		Data& Str(const TCHAR* key, const FString& value);
		Data& Bool(const TCHAR* key, bool value);
		/// The bag as a JSON object ("{}" when empty).
		FString ToJson() const;
		bool IsEmpty() const { return Fields.IsEmpty(); }

	private:
		/// Key and its value already in JSON.
		TArray<TPair<FString, FString>> Fields;
	};

	/// `CaptureUnity.Args != null && CaptureUnity.Args.Any`'s first half: -capture (or -capture=...) is on the command
	/// line, in a build that has capture. Whether a mode was asked for is the host's to say (<see cref="Any"/>).
	bool Requested();

	/// Once a frame from the game's actor: starts the host when the game window exists and reads its replies.
	void Tick();

	/// The host has answered (started, or said why not): <see cref="Any"/> and <see cref="Enabled"/> are known.
	bool Ready();

	/// `CaptureUnity.Args.Any`: a capture mode was asked for (-capture alone is video).
	bool Any();

	/// `CaptureUnity.Engine.Enabled`: the engine is running.
	bool Enabled();

	/// `CaptureUnity.Engine.SessionFolder`.
	const FString& SessionFolder();

	/// `CaptureUnity.Raise`: a game event for the capture.json rules. Cheap; never blocks; nothing when capture is off.
	void Raise(const FString& eventName, const Data& data = Data());

	/// `CaptureUnity.Marker`: a chapter in the exported video.
	void Marker(const FString& name, const Data& data = Data());

	/// `CaptureUnity.Engine.ExecuteCommandAsync(command)`: "screenshot", "toggle-recording", "save-replay" (the game's
	/// Alt+F9/F10/F11, docs/CAPTURE.md section 5).
	void Command(const FString& command);

	/// A line of the game's log (a "[capture] event" line), kept in the session folder's .game.log too: a Shipping game
	/// writes no log of its own.
	void Note(const FString& line);

	/// `CaptureUnity.Engine.ScreenshotAsync(tag)`: a request id (0 when capture is off) to wait on with
	/// <see cref="ScreenshotResult"/>.
	int32 Screenshot(const FString& tag, const Data& data = Data());

	/// True once screenshot <paramref name="id"/> is answered: <paramref name="ok"/> and the file, or the error.
	bool ScreenshotResult(int32 id, bool& ok, FString& fileOrError);

	/// The game is quitting: asks the host to stop and export (it also does when the game's process ends).
	void Shutdown();
}
