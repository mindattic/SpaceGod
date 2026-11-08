// US-T139: GameVoice (GameVoice.h): the player's own LLM key over HTTP, and sg.Voice:
//   sg.Voice           the model, persona and how many lines it has voiced
//   sg.Voice on|off    read the key and persona again and install the voice, or put the pools back alone
//   sg.Voice clear     forget every voiced line (they are asked for again)

#include "GameVoice.h"

#include "SpaceGodLog.h"

#include "Comms/StoryVoice.h"

#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <memory>

namespace SpaceGod
{
	namespace
	{
		const TCHAR* DefaultModel = TEXT("claude-opus-5-5");
		const TCHAR* DefaultUrl = TEXT("https://api.anthropic.com/v1/messages");

		FString U(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
		cs::String C(const FString& s) { return cs::String(TCHAR_TO_UTF8(*s)); }

		FString VoiceDir() { return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Voice"))); }

		FString ReadSetting(const TCHAR* env, const TCHAR* file)
		{
			FString v = FPlatformMisc::GetEnvironmentVariable(env);
			if (v.IsEmpty() && file != nullptr) FFileHelper::LoadFileToString(v, *FPaths::Combine(VoiceDir(), file));
			return v.TrimStartAndEnd();
		}

		/// <summary>The Anthropic Messages API on the player's key. At most a few calls in flight: past that it reports not
		/// ready, and the lines wait for the pools (asked again on a later look). A rejected key turns it off for the session.</summary>
		class AnthropicVoice final : public Core::ILlmClient
		{
		public:
			AnthropicVoice(FString key, FString model, FString url) : key(MoveTemp(key)), model(MoveTemp(model)), url(MoveTemp(url)), state(std::make_shared<Shared>()) {}

			cs::String Name() const override { return C(model); }
			bool Ready() const override { return !state->Rejected && state->InFlight < MaxInFlight; }

			void Ask(const Core::VoiceRequest& request, std::function<void(const cs::String&)> done) override
			{
				TSharedRef<FJsonObject> body = MakeShared<FJsonObject>();
				body->SetStringField(TEXT("model"), model);
				body->SetNumberField(TEXT("max_tokens"), request.MaxTokens);
				body->SetStringField(TEXT("system"), U(request.System));
				TSharedRef<FJsonObject> message = MakeShared<FJsonObject>();
				message->SetStringField(TEXT("role"), TEXT("user"));
				message->SetStringField(TEXT("content"), U(request.Prompt));
				TArray<TSharedPtr<FJsonValue>> messages;
				messages.Add(MakeShared<FJsonValueObject>(message));
				body->SetArrayField(TEXT("messages"), messages);
				// A line of dialogue needs little thought: low effort keeps it quick and cheap on the player's key.
				TSharedRef<FJsonObject> output = MakeShared<FJsonObject>();
				output->SetStringField(TEXT("effort"), TEXT("low"));
				body->SetObjectField(TEXT("output_config"), output);
				const bool fallbacks = model == DefaultModel;
				// On the default model a declined request is retried server-side on a model that can take it.
				if (fallbacks) body->SetStringField(TEXT("fallbacks"), TEXT("default"));
				FString json;
				auto writer = TJsonWriterFactory<>::Create(&json);
				FJsonSerializer::Serialize(body, writer);

				auto http = FHttpModule::Get().CreateRequest();
				http->SetURL(url);
				http->SetVerb(TEXT("POST"));
				http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
				http->SetHeader(TEXT("x-api-key"), key);
				http->SetHeader(TEXT("anthropic-version"), TEXT("2026-10-21"));
				if (fallbacks) http->SetHeader(TEXT("anthropic-beta"), TEXT("server-side-fallback-2026-07-01"));
				http->SetContentAsString(json);
				http->SetTimeout(static_cast<float>(request.BudgetSeconds));
				auto shared = state;
				shared->InFlight++;
				http->OnProcessRequestComplete().BindLambda([shared, done](FHttpRequestPtr, FHttpResponsePtr response, bool connected)
				{
					shared->InFlight = FMath::Max(0, shared->InFlight - 1);
					done(Reply(*shared, response, connected));
				});
				if (!http->ProcessRequest())
				{
					shared->InFlight = FMath::Max(0, shared->InFlight - 1);
					done(cs::String());
				}
			}

		private:
			struct Shared
			{
				int InFlight = 0;
				bool Rejected = false;
			};

			static constexpr int MaxInFlight = 3;

			static cs::String Reply(Shared& shared, const FHttpResponsePtr& response, bool connected)
			{
				if (!connected || !response.IsValid()) return cs::String();
				const int code = response->GetResponseCode();
				if (code == 401 || code == 403)
				{
					if (!shared.Rejected) UE_LOG(LogSpaceGod, Warning, TEXT("Voice: the key was refused (HTTP %d); the story pools speak for the rest of the session"), code);
					shared.Rejected = true;
					return cs::String();
				}
				if (code < 200 || code >= 300)
				{
					UE_LOG(LogSpaceGod, Verbose, TEXT("Voice: HTTP %d"), code);
					return cs::String();
				}
				TSharedPtr<FJsonObject> root;
				auto reader = TJsonReaderFactory<>::Create(response->GetContentAsString());
				if (!FJsonSerializer::Deserialize(reader, root) || !root.IsValid()) return cs::String();
				FString stop;
				// A declined request has no line to show: the pools speak instead.
				if (root->TryGetStringField(TEXT("stop_reason"), stop) && stop == TEXT("refusal")) return cs::String();
				const TArray<TSharedPtr<FJsonValue>>* content = nullptr;
				if (!root->TryGetArrayField(TEXT("content"), content)) return cs::String();
				FString text;
				for (const auto& v : *content)
				{
					const TSharedPtr<FJsonObject>* block = nullptr;
					if (!v.IsValid() || !v->TryGetObject(block)) continue;
					FString type, part;
					if ((*block)->TryGetStringField(TEXT("type"), type) && type == TEXT("text") && (*block)->TryGetStringField(TEXT("text"), part)) text += part;
				}
				return C(text);
			}

			FString key, model, url;
			std::shared_ptr<Shared> state;
		};

		void Install(bool on)
		{
			if (!on)
			{
				Core::StoryVoice::SetClient(nullptr);
				return;
			}
			const FString persona = [] {
				FString path = FPlatformMisc::GetEnvironmentVariable(TEXT("SPACEGOD_PERSONA"));
				if (path.IsEmpty()) path = FPaths::Combine(VoiceDir(), TEXT("persona.txt"));
				FString text;
				FFileHelper::LoadFileToString(text, *path);
				return text;
			}();
			Core::StoryVoice::SetPersona(Core::PersonaProfile::Parse(C(persona)));
			const FString key = ReadSetting(TEXT("SPACEGOD_LLM_KEY"), TEXT("api-key.txt"));
			if (key.IsEmpty())
			{
				Core::StoryVoice::SetClient(nullptr);
				UE_LOG(LogSpaceGod, Display, TEXT("Voice: no LLM key (SPACEGOD_LLM_KEY or %s/api-key.txt); the story pools speak"), *VoiceDir());
				return;
			}
			FString model = ReadSetting(TEXT("SPACEGOD_LLM_MODEL"), TEXT("model.txt"));
			if (model.IsEmpty()) model = DefaultModel;
			FString url = ReadSetting(TEXT("SPACEGOD_LLM_URL"), nullptr);
			if (url.IsEmpty()) url = DefaultUrl;
			Core::StoryVoice::SetClient(cs::New<AnthropicVoice>(key, model, url));
			const auto& p = Core::StoryVoice::Persona();
			UE_LOG(LogSpaceGod, Display, TEXT("Voice: the player's own key on %s, persona %s"), *model,
				p.Empty() ? TEXT("(none)") : (p.Name.empty() ? TEXT("(unnamed)") : *U(p.Name)));
		}

		void VoiceCommand(const TArray<FString>& args)
		{
			if (args.Num() > 0 && args[0] == TEXT("off")) { Install(false); UE_LOG(LogSpaceGod, Display, TEXT("sg.Voice: off, the story pools speak")); return; }
			if (args.Num() > 0 && args[0] == TEXT("on")) { Install(true); return; }
			if (args.Num() > 0 && args[0] == TEXT("clear")) { Core::StoryVoice::Log().Clear(); UE_LOG(LogSpaceGod, Display, TEXT("sg.Voice: voiced lines forgotten")); return; }
			const auto& client = Core::StoryVoice::Client();
			const auto& log = Core::StoryVoice::Log();
			const auto& p = Core::StoryVoice::Persona();
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Voice: %s, persona %s, %d line(s) voiced, %d waiting"),
				client != nullptr ? *FString::Printf(TEXT("%s%s"), *U(client->Name()), client->Ready() ? TEXT("") : TEXT(" (busy or refused)")) : TEXT("off (story pools)"),
				p.Empty() ? TEXT("(none)") : (p.Name.empty() ? TEXT("(unnamed)") : *U(p.Name)), log.Count(), log.WaitingCount());
			for (const auto& [k, v] : log.Entries()) UE_LOG(LogSpaceGod, Display, TEXT("sg.Voice:   %s  %s"), *U(k), *U(v));
		}

		FAutoConsoleCommand GVoiceCommand(
			TEXT("sg.Voice"),
			TEXT("US-T139: the BYOKey voice: sg.Voice (status and voiced lines), sg.Voice on|off, sg.Voice clear."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { VoiceCommand(args); }));
	}

	void StartGameVoice()
	{
		static bool started = false;
		if (started) return;
		started = true;
		Install(true);
	}
}
