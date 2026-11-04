#include "Capture/MindAtticCapture.h"

#include "UnrealSpaceGodLog.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericWindow.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SWindow.h"

// docs/CAPTURE.md section 6: capture code runs only in builds with MINDATTIC_CAPTURE, and in this copy the capture
// backend is compiled into every configuration (the published Shipping package too). Off unless -capture is on the
// command line either way. MINDATTIC_CAPTURE=0 builds a game with none of it.
#ifndef MINDATTIC_CAPTURE
#define MINDATTIC_CAPTURE 1
#endif

namespace SpaceGod::MindAtticCapture
{
	namespace
	{
		/// The folder name under R:\MindAttic\Capture (CaptureUnity's ProjectName: the repo the game comes from).
		const TCHAR* AppName = TEXT("SpaceGod.Unreal.Cpp");

		/// The game's shipped default rules (docs/CAPTURE.md section 4): <Project>/Content/Capture/, staged as a non-UFS file.
		const TCHAR* DefaultsFile = TEXT("SpaceGod.capture.json");

		/// How long the host gets to answer before the game gives up on it (CaptureRunner's own wait is 15 s).
		constexpr double ReadyTimeoutSeconds = 20.0;

		struct State
		{
			bool Tried = false, IsReady = false, IsAny = false, IsEnabled = false, ShutDown = false;
			FString Folder;
			FProcHandle Proc;
			void* StdinRead = nullptr;   // the host's end
			void* StdinWrite = nullptr;  // ours
			void* StdoutRead = nullptr;  // ours
			void* StdoutWrite = nullptr; // the host's end
			TArray<uint8> Pending;
			int32 NextId = 0;
			TMap<int32, TPair<bool, FString>> Results;
			double StartedAt = 0;
		};

		State& S()
		{
			static State state;
			return state;
		}

		FString JsonString(const FString& text)
		{
			FString out;
			out.Reserve(text.Len() + 2);
			out += TEXT('"');
			for (TCHAR c : text)
			{
				switch (c)
				{
				case TEXT('"'): out += TEXT("\\\""); break;
				case TEXT('\\'): out += TEXT("\\\\"); break;
				case TEXT('\n'): out += TEXT("\\n"); break;
				case TEXT('\r'): out += TEXT("\\r"); break;
				case TEXT('\t'): out += TEXT("\\t"); break;
				default:
					if (c < 0x20) out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(c));
					else out += c;
				}
			}
			out += TEXT('"');
			return out;
		}

		/// One argument for CommandLineToArgvW: quoted, with its quotes and the backslashes before them escaped.
		FString QuoteArg(const FString& arg)
		{
			FString out = TEXT("\"");
			int32 slashes = 0;
			for (TCHAR c : arg)
			{
				if (c == TEXT('\\')) { ++slashes; continue; }
				if (c == TEXT('"'))
				{
					for (int32 i = 0; i < slashes * 2 + 1; ++i) out += TEXT('\\');
					out += TEXT('"');
				}
				else
				{
					for (int32 i = 0; i < slashes; ++i) out += TEXT('\\');
					out += c;
				}
				slashes = 0;
			}
			for (int32 i = 0; i < slashes * 2; ++i) out += TEXT('\\');
			out += TEXT('"');
			return out;
		}

		/// The bundled host: MINDATTIC_CAPTURE_HOST, the package's Capture/ folder, else a build of Capture/SpaceGod.Capture
		/// in the repo (an editor or uncooked run).
		FString FindHost()
		{
			const FString env = FPlatformMisc::GetEnvironmentVariable(TEXT("MINDATTIC_CAPTURE_HOST"));
			TArray<FString> candidates;
			if (!env.IsEmpty()) candidates.Add(env);
			candidates.Add(FPaths::Combine(FPaths::RootDir(), TEXT("Capture"), TEXT("capture.exe")));
			const FString built = FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Capture"), TEXT("SpaceGod.Capture"), TEXT("bin"));
			for (const TCHAR* config : { TEXT("Release"), TEXT("Debug") })
				candidates.Add(FPaths::Combine(built, config, TEXT("net10.0-windows10.0.19041.0"), TEXT("capture.exe")));
			for (const FString& c : candidates)
			{
				const FString full = FPaths::ConvertRelativePathToFull(c);
				if (FPaths::FileExists(full)) return full;
			}
			return FString();
		}

		/// The shipped capture.json: the project's Content/Capture/, else beside the exe.
		FString FindDefaults()
		{
			const FString inContent = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Capture"), DefaultsFile));
			if (FPaths::FileExists(inContent)) return inContent;
			const FString besideExe = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::BaseDir(), DefaultsFile));
			return FPaths::FileExists(besideExe) ? besideExe : inContent; // the host warns about a missing file
		}

		void* GameWindowHandle()
		{
			if (!GEngine || !GEngine->GameViewport) return nullptr;
			TSharedPtr<SWindow> window = GEngine->GameViewport->GetWindow();
			if (!window.IsValid() || !window->GetNativeWindow().IsValid()) return nullptr;
			return window->GetNativeWindow()->GetOSWindowHandle();
		}

		void Send(const FString& json)
		{
			State& s = S();
			if (!s.StdinWrite || s.ShutDown) return;
			FPlatformProcess::WritePipe(s.StdinWrite, json); // one line: WritePipe adds the newline
		}

		void OnLine(const FString& line)
		{
			State& s = S();
			TSharedPtr<FJsonObject> msg;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(line), msg) || !msg.IsValid()) return;
			const FString op = msg->GetStringField(TEXT("op"));
			if (op == TEXT("log"))
			{
				// CaptureUnity's UnityCaptureLog: the engine's own lines in the game's log.
				const FString level = msg->GetStringField(TEXT("level"));
				const FString text = msg->GetStringField(TEXT("message"));
				if (level == TEXT("Error")) { UE_LOG(LogSpaceGod, Error, TEXT("[MindAttic.Capture] %s"), *text); }
				else if (level == TEXT("Warning")) { UE_LOG(LogSpaceGod, Warning, TEXT("[MindAttic.Capture] %s"), *text); }
				else { UE_LOG(LogSpaceGod, Display, TEXT("[MindAttic.Capture] %s"), *text); }
			}
			else if (op == TEXT("ready"))
			{
				s.IsReady = true;
				msg->TryGetBoolField(TEXT("any"), s.IsAny);
				msg->TryGetBoolField(TEXT("enabled"), s.IsEnabled);
				msg->TryGetStringField(TEXT("folder"), s.Folder);
				FString error;
				if (msg->TryGetStringField(TEXT("error"), error) && !error.IsEmpty())
					UE_LOG(LogSpaceGod, Error, TEXT("[MindAttic.Capture] Capture disabled: %s"), *error);
			}
			else if (op == TEXT("result"))
			{
				const int32 id = static_cast<int32>(msg->GetNumberField(TEXT("id")));
				bool ok = false;
				msg->TryGetBoolField(TEXT("ok"), ok);
				FString detail;
				const TArray<TSharedPtr<FJsonValue>>* files = nullptr;
				if (ok && msg->TryGetArrayField(TEXT("files"), files) && files && files->Num() > 0) detail = (*files)[0]->AsString();
				else msg->TryGetStringField(TEXT("error"), detail);
				s.Results.Add(id, TPair<bool, FString>(ok, detail));
			}
		}

		void Pump()
		{
			State& s = S();
			if (!s.StdoutRead) return;
			TArray<uint8> chunk;
			while (FPlatformProcess::ReadPipeToArray(s.StdoutRead, chunk) && chunk.Num() > 0)
			{
				s.Pending.Append(chunk);
				chunk.Reset();
			}
			int32 start = 0;
			for (int32 i = 0; i < s.Pending.Num(); ++i)
			{
				if (s.Pending[i] != '\n') continue;
				int32 len = i - start;
				if (len > 0 && s.Pending[i - 1] == '\r') --len;
				if (len > 0)
				{
					FUTF8ToTCHAR conv(reinterpret_cast<const ANSICHAR*>(s.Pending.GetData() + start), len);
					OnLine(FString(conv.Length(), conv.Get()));
				}
				start = i + 1;
			}
			if (start > 0) s.Pending.RemoveAt(0, start);
			// The host gave up or died before answering: capture is off.
			if (!s.IsReady && s.Proc.IsValid() && !FPlatformProcess::IsProcRunning(s.Proc))
			{
				s.IsReady = true;
				UE_LOG(LogSpaceGod, Error, TEXT("[MindAttic.Capture] The capture host exited before starting; capture is off."));
			}
			if (!s.IsReady && FPlatformTime::Seconds() - s.StartedAt > ReadyTimeoutSeconds)
			{
				s.IsReady = true;
				UE_LOG(LogSpaceGod, Error, TEXT("[MindAttic.Capture] The capture host did not answer in %.0f s; capture is off."), ReadyTimeoutSeconds);
			}
		}

		void Start(void* hwnd)
		{
			State& s = S();
			s.Tried = true;
			s.StartedAt = FPlatformTime::Seconds();
			const FString host = FindHost();
			if (host.IsEmpty())
			{
				s.IsReady = true;
				UE_LOG(LogSpaceGod, Error, TEXT("[MindAttic.Capture] -capture asked for, but the capture host (Capture/capture.exe) is not in this build; capture is off. tools/package.ps1 publishes it."));
				return;
			}
			if (!FPlatformProcess::CreatePipe(s.StdoutRead, s.StdoutWrite) || !FPlatformProcess::CreatePipe(s.StdinRead, s.StdinWrite, /*bWritePipeLocal*/ true))
			{
				s.IsReady = true;
				UE_LOG(LogSpaceGod, Error, TEXT("[MindAttic.Capture] Could not open the pipes to the capture host; capture is off."));
				return;
			}
			const FString args = FString::Printf(TEXT("host --hwnd 0x%llx --pid %u --app %s --version %s --defaults %s --cmdline %s"),
				static_cast<unsigned long long>(reinterpret_cast<UPTRINT>(hwnd)), FPlatformProcess::GetCurrentProcessId(), AppName,
				*QuoteArg(FApp::GetBuildVersion()), *QuoteArg(FindDefaults()), *QuoteArg(FCommandLine::GetOriginal()));
			uint32 pid = 0;
			s.Proc = FPlatformProcess::CreateProc(*host, *args, /*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
				&pid, 0, *FPaths::GetPath(host), s.StdoutWrite, s.StdinRead);
			// The host's ends are its own now (inherited); ours stay open until Shutdown.
			FPlatformProcess::ClosePipe(s.StdinRead, s.StdoutWrite);
			s.StdinRead = s.StdoutWrite = nullptr;
			if (!s.Proc.IsValid())
			{
				s.IsReady = true;
				UE_LOG(LogSpaceGod, Error, TEXT("[MindAttic.Capture] Could not start %s; capture is off."), *host);
				return;
			}
			UE_LOG(LogSpaceGod, Display, TEXT("[MindAttic.Capture] Capture host %s started (pid %u)."), *host, pid);
			FCoreDelegates::OnEnginePreExit.AddStatic(&Shutdown);
		}
	}

	Data& Data::Num(const TCHAR* key, double value)
	{
		Fields.Add({ key, FMath::IsFinite(value) ? FString::SanitizeFloat(value, 0) : FString(TEXT("null")) });
		return *this;
	}

	Data& Data::Str(const TCHAR* key, const FString& value)
	{
		Fields.Add({ key, JsonString(value) });
		return *this;
	}

	Data& Data::Bool(const TCHAR* key, bool value)
	{
		Fields.Add({ key, value ? FString(TEXT("true")) : FString(TEXT("false")) });
		return *this;
	}

	FString Data::ToJson() const
	{
		FString out = TEXT("{");
		for (int32 i = 0; i < Fields.Num(); ++i)
		{
			if (i > 0) out += TEXT(",");
			out += JsonString(Fields[i].Key) + TEXT(":") + Fields[i].Value;
		}
		return out + TEXT("}");
	}

	bool Requested()
	{
#if MINDATTIC_CAPTURE
		static const bool requested = []
		{
			// -capturedir=<dir> is tools/play.ps1's own screenshot folder for the -demo tour (US-T58): those shots stay
			// Unreal's screenshots there, with no capture host, as before.
			FString dir;
			if (FParse::Value(FCommandLine::Get(), TEXT("-capturedir="), dir)) return false;
			TArray<FString> tokens, switches;
			FCommandLine::Parse(FCommandLine::Get(), tokens, switches);
			for (const FString& t : tokens)
			{
				FString a = t;
				while (a.StartsWith(TEXT("-")) || a.StartsWith(TEXT("/"))) a.RightChopInline(1);
				if (a.Equals(TEXT("capture"), ESearchCase::IgnoreCase) || a.StartsWith(TEXT("capture="), ESearchCase::IgnoreCase)) return true;
			}
			return false;
		}();
		return requested;
#else
		return false;
#endif
	}

	void Tick()
	{
		State& s = S();
		if (s.ShutDown || !Requested()) return;
		if (!s.Tried)
		{
			void* hwnd = GameWindowHandle();
			if (!hwnd) return; // the game window isn't up yet
			Start(hwnd);
		}
		Pump();
	}

	bool Ready() { return !Requested() || S().IsReady; }
	bool Any() { return S().IsAny; }
	bool Enabled() { return S().IsEnabled && !S().ShutDown; }
	const FString& SessionFolder() { return S().Folder; }

	void Raise(const FString& eventName, const Data& data)
	{
		if (!Enabled()) return;
		Send(FString::Printf(TEXT("{\"op\":\"raise\",\"event\":%s,\"data\":%s}"), *JsonString(eventName), *data.ToJson()));
	}

	void Marker(const FString& name, const Data& data)
	{
		if (!Enabled()) return;
		Send(FString::Printf(TEXT("{\"op\":\"marker\",\"name\":%s,\"data\":%s}"), *JsonString(name), *data.ToJson()));
	}

	void Command(const FString& command)
	{
		if (!Enabled()) return;
		Send(FString::Printf(TEXT("{\"op\":\"command\",\"command\":%s}"), *JsonString(command)));
	}

	void Note(const FString& line)
	{
		if (!Enabled()) return;
		Send(FString::Printf(TEXT("{\"op\":\"note\",\"text\":%s}"), *JsonString(line)));
	}

	int32 Screenshot(const FString& tag, const Data& data)
	{
		if (!Enabled()) return 0;
		const int32 id = ++S().NextId;
		Send(FString::Printf(TEXT("{\"op\":\"screenshot\",\"id\":%d,\"tag\":%s,\"data\":%s}"), id, *JsonString(tag), *data.ToJson()));
		return id;
	}

	bool ScreenshotResult(int32 id, bool& ok, FString& fileOrError)
	{
		State& s = S();
		if (id == 0)
		{
			ok = false;
			fileOrError = TEXT("capture is off");
			return true;
		}
		Pump();
		if (const TPair<bool, FString>* r = s.Results.Find(id))
		{
			ok = r->Key;
			fileOrError = r->Value;
			s.Results.Remove(id);
			return true;
		}
		// The host is gone: nothing more will be answered.
		if (!s.Proc.IsValid() || !FPlatformProcess::IsProcRunning(s.Proc))
		{
			ok = false;
			fileOrError = TEXT("the capture host is not running");
			return true;
		}
		return false;
	}

	void Shutdown()
	{
		State& s = S();
		if (s.ShutDown) return;
		Send(TEXT("{\"op\":\"quit\"}"));
		s.ShutDown = true;
		// The host exports the session on its own (handed to its detached finalize), so the game never waits on it.
		if (s.StdinWrite) FPlatformProcess::ClosePipe(nullptr, s.StdinWrite);
		if (s.StdoutRead) FPlatformProcess::ClosePipe(s.StdoutRead, nullptr);
		s.StdinWrite = s.StdoutRead = nullptr;
		if (s.Proc.IsValid()) FPlatformProcess::CloseProc(s.Proc);
	}
}
