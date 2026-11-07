// US-T236: GameLore (GameLore.h) and sg.Lore, the console view of the Prose texts the game has loaded:
//   sg.Lore            the folder, the count, the keys and any file left out
//   sg.Lore <key>      one text: its id, version and payload

#include "GameLore.h"

#include "SpaceGodLog.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

namespace SpaceGod
{
	namespace
	{
		FString LoreFolder()
		{
			const FString env = FPlatformMisc::GetEnvironmentVariable(TEXT("SPACEGOD_LORE_DIR"));
			if (!env.IsEmpty()) return FPaths::ConvertRelativePathToFull(env);
			const FString packaged = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::RootDir(), TEXT("Lore")));
			if (FPaths::DirectoryExists(packaged)) return packaged;
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Data"), TEXT("Lore")));
		}

		FString U(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	}

	const Core::LoreLibrary& GameLore()
	{
		static const Core::LoreLibrary lore = []
		{
			const FString folder = LoreFolder();
			auto lib = Core::LoreLibrary::Load(cs::String(TCHAR_TO_UTF8(*folder)));
			UE_LOG(LogSpaceGod, Display, TEXT("Lore: %d Prose text(s) from %s"), lib.Count(), *folder);
			for (const auto& p : lib.Problems()) UE_LOG(LogSpaceGod, Warning, TEXT("Lore: left out %s"), *U(p));
			return lib;
		}();
		return lore;
	}

	namespace
	{
		void LoreCommand(const TArray<FString>& args)
		{
			const auto& lore = GameLore();
			if (args.Num() == 0)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Lore: %d text(s) from %s, %d file(s) left out"), lore.Count(), *U(lore.Folder()), lore.Problems().Count());
				for (const auto& k : lore.Keys())
				{
					const auto* t = lore.Find(k);
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Lore:   %s  [%s] %s v%d"), *U(k), *U(t->Kind), *U(t->Id), t->Version);
				}
				for (const auto& p : lore.Problems()) UE_LOG(LogSpaceGod, Display, TEXT("sg.Lore:   left out %s"), *U(p));
				return;
			}
			const cs::String key = TCHAR_TO_UTF8(*args[0]);
			const auto* t = lore.Find(key);
			if (t == nullptr)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Lore: no text for '%s' (the game keeps its placeholder)"), *args[0]);
				return;
			}
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Lore: %s  [%s] %s v%d %s"), *U(t->Key), *U(t->Kind), *U(t->Id), t->Version, *U(t->Hash));
			UE_LOG(LogSpaceGod, Display, TEXT("%s"), *U(t->Payload->ToJson()));
		}

		FAutoConsoleCommand GLoreCommand(
			TEXT("sg.Lore"),
			TEXT("US-T236: the Prose texts the game loaded from Data/Lore: sg.Lore (list) or sg.Lore <key> (one text)."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { LoreCommand(args); }));
	}
}
