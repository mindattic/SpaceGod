#pragma once

// US-T163: the game's own frames for the capture host, so it can record a game that has no window. Under
// -RenderOffScreen the GPU still renders every frame, but Unreal opens no window (a NullApplication), so the host's
// Windows Graphics Capture has nothing to record. Instead the game reads back each presented backbuffer (Slate's
// OnBackBufferReadyToPresent, an async FRHIGPUTextureReadback a few frames deep, so the game never waits on the GPU)
// at the host's capture rate and writes it, top-down BGRA, into a shared-memory ring the host reads
// (Capture/SpaceGod.Capture/FrameFeed.cs; the layout is in Capture/README.md). The host feeds those frames to the same
// tape, ffmpeg pipeline, session folder, markers, events and screenshots as window capture.
//
// Used automatically under -RenderOffScreen, or with -capture-source=frames when a window exists too; otherwise the
// host records the window as before. -nullrhi renders nothing, so it still has no images at all.

#include "CoreMinimal.h"

namespace SpaceGod::MindAtticCapture::FrameFeed
{
	/// The host should record the game's frames, not its window: -RenderOffScreen or -capture-source=frames.
	bool Wanted();

	/// Creates the shared frame ring at the game viewport's size; false while there is no viewport yet. Its name (a
	/// {GUID}) is what the host opens with --frames.
	bool Open(FString& outName);

	/// The host is up and capturing at <paramref name="fps"/>: start reading back frames at that rate.
	void Start(int32 fps);

	/// Once a game frame: asks the render thread for a frame when one is due; logs the feed's cost every few seconds.
	void Tick();

	/// Stops reading back, marks the ring closed and unmaps it (the host keeps its own view until it stops).
	void Close();

	/// Every few seconds, the game's frame rate (and, while feeding, the feed's cost) in the log: under -capture with
	/// frames, or with -CaptureStats alone for a baseline without capture.
	void TickStats();
}
