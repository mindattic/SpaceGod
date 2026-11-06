#include "Capture/FrameFeed.h"

#include "SpaceGodLog.h"

#include "Async/ParallelFor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "PixelFormat.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/SlateViewportProvider.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

#include <atomic>

namespace SpaceGod::MindAtticCapture::FrameFeed
{
	namespace
	{
		// The shared ring (Capture/README.md, "Frames"; read by Capture/SpaceGod.Capture/FrameFeed.cs). Little-endian,
		// fixed offsets: a 64-byte header, Slots 32-byte slot records from byte 64, the frames from HeaderBytes on.
		constexpr uint32 Magic = 0x52464753; // "SGFR"
		constexpr uint32 Version = 1;
		constexpr int32 Slots = 3;
		constexpr int32 HeaderBytes = 4096;

		struct FHeader
		{
			uint32 Magic;
			uint32 Version;
			int32 Width;
			int32 Height;
			int32 Slots;
			int32 HeaderBytes;
			volatile int64 Latest;  // the newest whole frame's sequence number (1, 2, ...); 0 before the first
			volatile int32 Closed;  // 1 once the game stops feeding
			int32 Fps;              // the rate the game reads back at (informational)
			int64 Reserved[3];
		};
		static_assert(sizeof(FHeader) == 64, "the host reads the header at fixed offsets");

		struct FSlot
		{
			volatile int64 Seq;     // the frame in this slot; 0 while it is being written
			int64 Cycles;           // FPlatformTime::Cycles64 when it was written
			int64 Reserved[2];
		};
		static_assert(sizeof(FSlot) == 32, "the host reads the slots at fixed offsets");

		/// Readbacks in flight on the GPU: a frame is copied out once its fence has passed, never waited on, so a slow
		/// GPU costs dropped feed frames (counted), not a game hitch.
		constexpr int32 Depth = 4;

		/// Seconds between stats lines.
		constexpr double StatsEvery = 5.0;

		struct FRenderSide
		{
			TUniquePtr<FRHIGPUTextureReadback> Readbacks[Depth];
			FIntPoint Sizes[Depth];
			EPixelFormat Formats[Depth] = {};
			bool Busy[Depth] = {};
			TArray<int32, TInlineAllocator<Depth>> InFlight; // oldest first
			bool WarnedFormat = false;
		};

		struct FState
		{
			FPlatformMemory::FSharedMemoryRegion* Region = nullptr;
			uint8* Base = nullptr;
			FString Name;
			int32 Width = 0, Height = 0;
			int64 FrameBytes = 0;
			const void* TargetWindow = nullptr;
			FDelegateHandle Handle;
			bool Active = false;
			double Interval = 1.0 / 60.0, NextGrab = 0;
			std::atomic<int32> Requests{ 0 };
			FRenderSide Render; // render thread only

			// Stats: written on the render thread, read on the game thread.
			std::atomic<int64> Fed{ 0 }, Skipped{ 0 }, RenderMicros{ 0 }, RenderMicrosMax{ 0 }, CopyMicros{ 0 };
			std::atomic<int32> Format{ PF_Unknown };

			// Game-thread stats window.
			double StatsFrom = 0, FrameSeconds = 0, WorstFrame = 0;
			int64 Frames = 0, FedAtWindow = 0, SkippedAtWindow = 0, RenderMicrosAtWindow = 0, CopyMicrosAtWindow = 0;
		};

		FState& S()
		{
			static FState state;
			return state;
		}

		FHeader* Header() { return reinterpret_cast<FHeader*>(S().Base); }
		FSlot* SlotAt(int32 i) { return reinterpret_cast<FSlot*>(S().Base + sizeof(FHeader) + i * sizeof(FSlot)); }

		/// One read-back frame into the ring's next slot, as top-down BGRA at the ring's size (cropped or padded black).
		void Write(const uint8* data, int32 pitchPixels, int32 rows, FIntPoint size, EPixelFormat format)
		{
			FState& s = S();
			const uint64 c0 = FPlatformTime::Cycles64();
			const int32 bpp = GPixelFormats[format].BlockBytes;
			const bool bgra = format == PF_B8G8R8A8;
			const bool rgba = format == PF_R8G8B8A8;
			const bool a2bgr10 = format == PF_A2B10G10R10;
			if (bpp != 4 || !(bgra || rgba || a2bgr10))
			{
				if (!s.Render.WarnedFormat)
				{
					s.Render.WarnedFormat = true;
					UE_LOG(LogSpaceGod, Error, TEXT("[MindAttic.Capture] The backbuffer is %s; the frame feed reads only 8-bit BGRA/RGBA and 10-bit RGB; no frames."),
						GPixelFormats[format].Name);
				}
				return;
			}
			FHeader* header = Header();
			const int64 seq = header->Latest + 1; // the render thread is the only writer
			const int32 slotIndex = static_cast<int32>(seq % Slots);
			FSlot* slot = SlotAt(slotIndex);
			FPlatformAtomics::InterlockedExchange(&slot->Seq, 0);
			uint8* dst = s.Base + HeaderBytes + slotIndex * s.FrameBytes;
			const int32 w = FMath::Min(s.Width, size.X);
			const int32 h = FMath::Min(s.Height, FMath::Min(size.Y, rows));
			const int32 rowBytes = s.Width * 4;
			// In bands across the task graph's workers: the 10-bit backbuffer (Unreal's default, offscreen too) is converted
			// per pixel, about 2 ms a 1600x900 frame on one core, which would all be render-thread time.
			constexpr int32 Bands = 8;
			ParallelFor(Bands, [&](int32 band)
			{
				const int32 y0 = h * band / Bands, y1 = h * (band + 1) / Bands;
				for (int32 y = y0; y < y1; ++y)
				{
					const uint8* src = data + static_cast<int64>(y) * pitchPixels * 4;
					uint8* out = dst + static_cast<int64>(y) * rowBytes;
					if (bgra)
					{
						FMemory::Memcpy(out, src, w * 4);
					}
					else if (rgba)
					{
						for (int32 x = 0; x < w; ++x)
						{
							out[x * 4 + 0] = src[x * 4 + 2];
							out[x * 4 + 1] = src[x * 4 + 1];
							out[x * 4 + 2] = src[x * 4 + 0];
							out[x * 4 + 3] = 255;
						}
					}
					else
					{
						// R10G10B10A2: red in the low bits.
						const uint32* px = reinterpret_cast<const uint32*>(src);
						for (int32 x = 0; x < w; ++x)
						{
							const uint32 v = px[x];
							out[x * 4 + 0] = static_cast<uint8>(((v >> 20) & 0x3FF) >> 2);
							out[x * 4 + 1] = static_cast<uint8>(((v >> 10) & 0x3FF) >> 2);
							out[x * 4 + 2] = static_cast<uint8>((v & 0x3FF) >> 2);
							out[x * 4 + 3] = 255;
						}
					}
					if (w < s.Width) FMemory::Memzero(out + w * 4, (s.Width - w) * 4);
				}
			});
			if (h < s.Height) FMemory::Memzero(dst + static_cast<int64>(h) * rowBytes, static_cast<int64>(s.Height - h) * rowBytes);
			slot->Cycles = static_cast<int64>(FPlatformTime::Cycles64());
			FPlatformMisc::MemoryBarrier();
			FPlatformAtomics::InterlockedExchange(&slot->Seq, seq);
			FPlatformAtomics::InterlockedExchange(&header->Latest, seq);
			s.Fed.fetch_add(1, std::memory_order_relaxed);
			s.CopyMicros.fetch_add(static_cast<int64>(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - c0) * 1e6), std::memory_order_relaxed);
			s.Format.store(format, std::memory_order_relaxed);
		}

		/// Slate's present of a window (render thread): copy out finished readbacks, then start one of this frame if due.
		void OnBackBufferReady(SWindow& window, ISlateViewportProvider& provider)
		{
			FState& s = S();
			if (&window != s.TargetWindow || !s.Base) return;
			const uint64 c0 = FPlatformTime::Cycles64();
			FRenderSide& r = s.Render;
			bool did = false;
			while (r.InFlight.Num() > 0)
			{
				const int32 i = r.InFlight[0];
				if (!r.Readbacks[i]->IsReady()) break;
				int32 pitch = 0, rows = 0;
				if (const void* data = r.Readbacks[i]->Lock(pitch, &rows))
				{
					Write(static_cast<const uint8*>(data), pitch, rows > 0 ? rows : r.Sizes[i].Y, r.Sizes[i], r.Formats[i]);
					r.Readbacks[i]->Unlock();
				}
				r.InFlight.RemoveAt(0);
				r.Busy[i] = false;
				did = true;
			}
			if (s.Requests.exchange(0) > 0)
			{
				FRHITexture* backBuffer = provider.GetBackBufferResource();
				int32 free = INDEX_NONE;
				for (int32 i = 0; i < Depth && free == INDEX_NONE; ++i)
					if (!r.Busy[i]) free = i;
				if (!backBuffer || backBuffer->IsMultisampled())
				{
				}
				else if (free == INDEX_NONE)
				{
					s.Skipped.fetch_add(1, std::memory_order_relaxed); // every readback still on the GPU
				}
				else
				{
					const FIntPoint size = backBuffer->GetSizeXY();
					const EPixelFormat format = backBuffer->GetFormat();
					// The readback's staging texture is made for the first source's size and format.
					if (!r.Readbacks[free] || r.Sizes[free] != size || r.Formats[free] != format)
						r.Readbacks[free] = MakeUnique<FRHIGPUTextureReadback>(TEXT("SpaceGodCaptureFeed"));
					FRHICommandListImmediate& cmd = FRHICommandListImmediate::Get();
					cmd.Transition(FRHITransitionInfo(backBuffer, ERHIAccess::Unknown, ERHIAccess::CopySrc));
					r.Readbacks[free]->EnqueueCopy(cmd, backBuffer);
					// Where TransitionAndCopyTexture leaves a copy source (GameplayMediaEncoder reads the backbuffer so too).
					cmd.Transition(FRHITransitionInfo(backBuffer, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
					r.Sizes[free] = size;
					r.Formats[free] = format;
					r.Busy[free] = true;
					r.InFlight.Add(free);
					did = true;
				}
			}
			if (did)
			{
				const int64 us = static_cast<int64>(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - c0) * 1e6);
				s.RenderMicros.fetch_add(us, std::memory_order_relaxed);
				int64 prev = s.RenderMicrosMax.load(std::memory_order_relaxed);
				while (us > prev && !s.RenderMicrosMax.compare_exchange_weak(prev, us)) {}
			}
		}
	}

	bool Wanted()
	{
		FString source;
		if (FParse::Value(FCommandLine::Get(), TEXT("-capture-source="), source))
		{
			if (source.Equals(TEXT("frames"), ESearchCase::IgnoreCase)) return true;
			if (source.Equals(TEXT("window"), ESearchCase::IgnoreCase)) return false;
		}
		return FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen"));
	}

	bool Open(FString& outName)
	{
		FState& s = S();
		if (s.Region)
		{
			outName = s.Name;
			return true;
		}
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport) return false;
		const TSharedPtr<SWindow> window = GEngine->GameViewport->GetWindow();
		const FIntPoint size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (!window.IsValid() || size.X < 2 || size.Y < 2) return false;
		// Even sizes: the HQ tape encodes yuv420p straight from these frames.
		s.Width = size.X & ~1;
		s.Height = size.Y & ~1;
		s.FrameBytes = static_cast<int64>(s.Width) * s.Height * 4;
		// A {GUID} name: Windows maps it without the Global\ prefix (and so without administrator rights).
		s.Name = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensInBraces);
		s.Region = FPlatformMemory::MapNamedSharedMemoryRegion(s.Name, /*bCreate*/ true,
			FPlatformMemory::ESharedMemoryAccess::Read | FPlatformMemory::ESharedMemoryAccess::Write, HeaderBytes + Slots * s.FrameBytes);
		if (!s.Region)
		{
			UE_LOG(LogSpaceGod, Error, TEXT("[MindAttic.Capture] Could not map the %dx%d frame feed; capture is off."), s.Width, s.Height);
			return false;
		}
		s.Base = static_cast<uint8*>(s.Region->GetAddress());
		FMemory::Memzero(s.Base, HeaderBytes);
		FHeader* header = Header();
		header->Magic = Magic;
		header->Version = Version;
		header->Width = s.Width;
		header->Height = s.Height;
		header->Slots = Slots;
		header->HeaderBytes = HeaderBytes;
		s.TargetWindow = window.Get();
		outName = s.Name;
		UE_LOG(LogSpaceGod, Display, TEXT("[MindAttic.Capture] Frame feed %s: %dx%d, %d slots (the host records the game's frames%s)."), *s.Name, s.Width, s.Height,
			Slots, FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen")) ? TEXT(": -RenderOffScreen has no window") : TEXT(", -capture-source=frames"));
		return true;
	}

	void Start(int32 fps)
	{
		FState& s = S();
		if (!s.Region || s.Active || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer()) return;
		fps = FMath::Clamp(fps, 1, 240);
		s.Interval = 1.0 / fps;
		s.NextGrab = FPlatformTime::Seconds();
		Header()->Fps = fps;
		s.Handle = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddStatic(&OnBackBufferReady);
		s.Active = true;
		UE_LOG(LogSpaceGod, Display, TEXT("[MindAttic.Capture] Frame feed reading back %d frames/s (async, %d deep)."), fps, Depth);
	}

	void Tick()
	{
		FState& s = S();
		if (!s.Active) return;
		const double now = FPlatformTime::Seconds();
		if (now < s.NextGrab) return;
		s.Requests.store(1);
		s.NextGrab += s.Interval;
		if (s.NextGrab < now) s.NextGrab = now + s.Interval; // the game runs slower than the capture rate: every frame
	}

	void TickStats()
	{
		static const bool baseline = FParse::Param(FCommandLine::Get(), TEXT("CaptureStats"));
		FState& s = S();
		if (!s.Active && !baseline) return;
		const double now = FPlatformTime::Seconds();
		if (s.StatsFrom == 0)
		{
			s.StatsFrom = now;
			return;
		}
		const double dt = FApp::GetDeltaTime();
		++s.Frames;
		s.FrameSeconds += dt;
		s.WorstFrame = FMath::Max(s.WorstFrame, dt);
		const double span = now - s.StatsFrom;
		if (span < StatsEvery) return;
		const double fps = s.Frames / span;
		const double avgMs = s.Frames > 0 ? s.FrameSeconds / s.Frames * 1000.0 : 0;
		if (s.Active)
		{
			const int64 fed = s.Fed.load() - s.FedAtWindow, skipped = s.Skipped.load() - s.SkippedAtWindow;
			const int64 renderUs = s.RenderMicros.load() - s.RenderMicrosAtWindow, copyUs = s.CopyMicros.load() - s.CopyMicrosAtWindow;
			const int64 worstUs = s.RenderMicrosMax.exchange(0);
			UE_LOG(LogSpaceGod, Display, TEXT("[MindAttic.Capture] feed: game %.1f fps (%.2f ms avg, %.1f ms worst); fed %.1f frames/s %dx%d %s; render thread %.2f ms avg per fed frame (copy %.2f ms), %.2f ms worst; %lld skipped (GPU behind)"),
				fps, avgMs, s.WorstFrame * 1000.0, fed / span, s.Width, s.Height, GPixelFormats[s.Format.load()].Name,
				fed > 0 ? renderUs / 1000.0 / fed : 0.0, fed > 0 ? copyUs / 1000.0 / fed : 0.0, worstUs / 1000.0, skipped);
			s.FedAtWindow += fed;
			s.SkippedAtWindow += skipped;
			s.RenderMicrosAtWindow += renderUs;
			s.CopyMicrosAtWindow += copyUs;
		}
		else
		{
			UE_LOG(LogSpaceGod, Display, TEXT("[MindAttic.Capture] baseline: game %.1f fps (%.2f ms avg, %.1f ms worst); no capture"), fps, avgMs, s.WorstFrame * 1000.0);
		}
		s.StatsFrom = now;
		s.Frames = 0;
		s.FrameSeconds = 0;
		s.WorstFrame = 0;
	}

	void Close()
	{
		FState& s = S();
		if (!s.Region) return;
		if (s.Active && FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
			FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(s.Handle);
		s.Active = false;
		s.Handle.Reset();
		// No present callback runs past here; the readbacks (RHI resources) go on the render thread.
		FlushRenderingCommands();
		ENQUEUE_RENDER_COMMAND(SpaceGodCaptureFeedRelease)([](FRHICommandListImmediate&)
		{
			FRenderSide& r = S().Render;
			for (int32 i = 0; i < Depth; ++i)
			{
				r.Readbacks[i].Reset();
				r.Busy[i] = false;
			}
			r.InFlight.Reset();
		});
		FlushRenderingCommands();
		FPlatformAtomics::InterlockedExchange(&Header()->Closed, 1);
		FPlatformMemory::UnmapNamedSharedMemoryRegion(s.Region);
		s.Region = nullptr;
		s.Base = nullptr;
		s.TargetWindow = nullptr;
		UE_LOG(LogSpaceGod, Display, TEXT("[MindAttic.Capture] Frame feed closed after %lld frames (%lld skipped)."), s.Fed.load(), s.Skipped.load());
	}
}
