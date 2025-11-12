#include "TeamArena.h"
#include "Modules/ModuleManager.h"
//#include "UTTeamGameMode.h" // For the PARENT GameMode
//#include "UTGameState.h"    // For the PARENT GameState
//#include "GSInterface.h"




IMPLEMENT_MODULE(FTeamArenaModule, TeamArena)

void FTeamArenaModule::StartupModule()
{
	UE_LOG(LogLoad, Log, TEXT("TeamArena loaded"));
}


void FTeamArenaModule::ShutdownModule()
{
	UE_LOG(LogLoad, Log, TEXT("TeamArena unloaded"));
}





