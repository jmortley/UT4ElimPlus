#pragma once
//#include "TeamArena.h"
#include "CoreMinimal.h"
#include "UnrealTournament.h"
#include "UObject/Interface.h"
#include "GSInterface.generated.h"

UINTERFACE(BlueprintType) // Use MinimalAPI or your module's API
class URoundStateProvider : public UInterface
{
    GENERATED_BODY()
};

class IRoundStateProvider
{
    GENERATED_BODY() 

public:
    // Using BlueprintImplementableEvent if only BPs will handle it
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Arena")
    void Arena_SetIntermission(bool bInIntermission, int32 IntermissionRemain);

    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Arena")
    void Arena_SetRound(bool bInProgress, int32 RoundRemain, int32 LastWinnerTeamIndex);

    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Arena|Round")
    int32 Arena_GetLastWinnerTeamIndex();
};