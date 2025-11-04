//#include "TeamArenaGame.h"
#include "TeamArena.h"
#include "ArenaGameState.h"
#include "UnrealTournament.h"
#include "UnrealNetwork.h"
#include "UTCharacter.h"
#include "UTPlayerState.h"
#include "UTTimerMessage.h"
#include "UTPlayerController.h"

AURArenaGameState::AURArenaGameState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bRoundInProgress = false;
	IntermissionSecondsRemaining = 0; // Default to 0, GameMode will set it
	RoundSecondsRemaining = 0;
	bMatchHasStarted = false;
	LastRoundWinningTeamIndex = INDEX_NONE;

	// Show this on the scoreboard (like Showdown's style)
	// GoalScoreText is inherited from UTGameState
	GoalScoreText = NSLOCTEXT("UTScoreboard", "ArenaGoalScoreFormat", "Win {0} Rounds");
}

void AURArenaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AURArenaGameState, bRoundInProgress);
	DOREPLIFETIME(AURArenaGameState, IntermissionSecondsRemaining);
	DOREPLIFETIME(AURArenaGameState, RoundSecondsRemaining);
	DOREPLIFETIME(AURArenaGameState, bMatchHasStarted);
	DOREPLIFETIME(AURArenaGameState, LastRoundWinningTeamIndex);
}

void AURArenaGameState::OnRep_MatchState()
{
	// Store previous state before calling super
	// Note: In 4.15, PreviousMatchState might not be available.
	// We'll check the new MatchState value directly.

	const FName CurrentMatchState = GetMatchState();

	// --- Clean up old corpses after intermission ---
	// (This logic was already good)
	if (CurrentMatchState == MatchState::InProgress)
	{
		for (APlayerState* PS : PlayerArray)
		{
			if (AUTPlayerState* UTPS = Cast<AUTPlayerState>(PS))
			{
				UTPS->RoundDamageDone = 0;
				UTPS->RoundKills = 0;
			}
		}

		for (FConstPawnIterator It = GetWorld()->GetPawnIterator(); It; ++It)
		{
			if (AUTCharacter* UTC = Cast<AUTCharacter>(It->Get()))
			{
				// Only destroy pawns that are BOTH dead AND are not pending kill
				if (UTC->IsDead() && !UTC->IsPendingKill())
				{
					// This cleans up ragdolls from the previous round
					UTC->Destroy();
				}
			}
		}
	}
	// WHEN ENTERING INTERMISSION:
	// We no longer hide living pawns, to allow for celebration.
	/* else if (GetMatchState() == MatchState::MatchIntermission)
	{
		for (FConstPawnIterator It = GetWorld()->GetPawnIterator(); It; ++It)
		{
			AUTCharacter* UTC = Cast<AUTCharacter>(It->Get());
			if (UTC != NULL && !UTC->IsDead())
			{
				// This is the Showdown logic to hide pawns,
				// which we are INTENTIONALLY skipping.
				// UTC->GetRootComponent()->SetHiddenInGame(true, true);
			}
		}
	}
	*/


	Super::OnRep_MatchState();
}

void AURArenaGameState::CheckTimerMessage()
{
	// Send standard timer beeps/messages based on RemainingTime.
	// (This mirrors UT's pattern; server authoritatively broadcasts.)
	if (Role == ROLE_Authority && IsMatchInProgress())
	{
		int32 TimerMessageIndex = -1;
		switch (RemainingTime)
		{
		case 300: TimerMessageIndex = 13; break; // 5:00
		case 180: TimerMessageIndex = 12; break; // 3:00
		case  60: TimerMessageIndex = 11; break; // 1:00
		case  30: TimerMessageIndex = 10; break; // 0:30
		default:
			if (RemainingTime >= 1 && RemainingTime <= 10)
			{
				TimerMessageIndex = RemainingTime - 1; // 10..1
			}
			break;
		}

		if (TimerMessageIndex >= 0)
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (AUTPlayerController* PC = Cast<AUTPlayerController>(*It))
				{
					PC->ClientReceiveLocalizedMessage(UUTTimerMessage::StaticClass(), TimerMessageIndex, nullptr, nullptr, nullptr);
				}
			}
		}
	}

	// Do not call Super::CheckTimerMessage() if you want to *replace*
	// the default timer logic. Call it if you want to *add* to it.
	// Given the logic above, we are replacing it for InProgress.
	Super::CheckTimerMessage(); 
}
