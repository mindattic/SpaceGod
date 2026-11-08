#include "BuildInfo.h"

#include "Misc/ConfigCacheIni.h"

// US-T245: written by SpaceGod.Build.cs (Intermediate/BuildStamp), only this file includes it, so a new commit rebuilds
// this file alone. A tree built without it (no git, or a tool compiling the sources itself) falls back to "unknown".
#if __has_include("SpaceGodBuildStamp.h")
#include "SpaceGodBuildStamp.h"
#endif
#ifndef SPACEGOD_BUILD_COMMIT
#define SPACEGOD_BUILD_COMMIT "unknown"
#endif
#ifndef SPACEGOD_BUILD_NUMBER
#define SPACEGOD_BUILD_NUMBER ""
#endif

namespace SpaceGod
{
	cs::String BuildInfo::Version()
	{
		static const cs::String version = []
		{
			FString v;
			if (GConfig != nullptr)
				GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), v, GGameIni);
			v.TrimStartAndEndInline();
			return v.IsEmpty() ? cs::String("dev") : cs::String(TCHAR_TO_UTF8(*v));
		}();
		return version;
	}

	cs::String BuildInfo::BuildNumber() { return SPACEGOD_BUILD_NUMBER; }

	cs::String BuildInfo::Commit() { return SPACEGOD_BUILD_COMMIT; }

	cs::String BuildInfo::Describe()
	{
		cs::String s = cs::Cat("v", Version());
		if (!BuildNumber().empty()) s = cs::Cat(s, "  \xC2\xB7  build ", BuildNumber());
		return cs::Cat(s, "  \xC2\xB7  ", Commit());
	}
}
