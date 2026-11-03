// The UE module shell around SpaceGodCore. This is the only SpaceGodCore file allowed Unreal headers; the
// Core itself is plain C++20 (see docs/MIGRATION.md) and has no startup work, so the default module will do.

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, SpaceGodCore);
