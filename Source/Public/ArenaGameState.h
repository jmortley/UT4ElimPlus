#pragma once
#include "TeamArena.h"
#include "UnrealTournament.h"
#include "UTGameState.h"
#include "ArenaGameState.generated.h"

UCLASS()
class TEAMARENA_API AURArenaGameState : public AUTGameState
{
	GENERATED_BODY()

public:
	AURArenaGameState(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadOnly, Replicated)
	bool bRoundInProgress;

	UPROPERTY(BlueprintReadOnly, Replicated)
	uint8 IntermissionSecondsRemaining;

	UPROPERTY(BlueprintReadOnly, Replicated)
	int32 RoundSecondsRemaining;

	UPROPERTY(BlueprintReadOnly, Replicated)
	bool bMatchHasStarted;

	UPROPERTY(BlueprintReadOnly, Replicated)
	int32 LastRoundWinningTeamIndex;

	virtual void CheckTimerMessage() override;
	virtual void OnRep_MatchState() override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

};
