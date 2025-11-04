#include "TeamArena.h"
#include "Modules/ModuleManager.h"




IMPLEMENT_MODULE(FTeamArenaModule, TeamArena)

void FTeamArenaModule::StartupModule()
{
	UE_LOG(LogLoad, Log, TEXT("TeamArena loaded"));
}

void FTeamArenaModule::ShutdownModule()
{
	UE_LOG(LogLoad, Log, TEXT("TeamArena unloaded"));
}
