#include "TeamArena.h"
#include "UnrealTournament.h"
#include "TeamArenaGame.h"
#include "ArenaGameState.h"
//#include "ArenaPlayerCameraManager.h"
#include "UTTeamGameMode.h"
#include "UTGameState.h"
#include "UTHUD_InstantReplay.h"
#include "UTCharacter.h"
#include "UTPlayerState.h"
#include "Sound/SoundBase.h"
#include "UTPlayerController.h"
#include "UTTeamInfo.h"
#include "UTTeamPlayerStart.h"
#include "UTDroppedPickup.h"
#include "Engine/DemoNetDriver.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "UTTeamArenaVictoryMessage.h"
#include "UTCountDownMessage.h" 
#include "UTGameMessage.h"

AUTeamArenaGame::AUTeamArenaGame(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = NSLOCTEXT("UTGameMode", "TeamArena", "Team Arena");
	bTeamGame = true;
	HUDClass = AUTHUD::StaticClass();
	// TeamGameMode defaults
	NumTeams = 2;
	bBalanceTeams = true;
	bUseTeamStarts = false;
	bAnnounceTeam = true;
	RedTeamVictorySound = nullptr;
	BlueTeamDominatingSound = nullptr;
	RedTeamDominatingSound = nullptr;
	RedTeamTakesLeadSound = nullptr;
	BlueTeamTakesLeadSound = nullptr;
	BlueTeamVictorySound = nullptr;
	RoundDrawSound = nullptr;
	MatchVictorySound = nullptr;
	LastManStandingSound = nullptr;
	EnemyLastManStandingSound = nullptr;
	OvertimeAnnouncementSound = nullptr;

	// Round defaults
	bForceRespawn = false;
	bHasRespawnChoices = false;
	ScoreLimit = 7;
	RoundTimeSeconds = 120;
	RoundStartDelaySeconds = 3.f; // This is now the "AwardDisplayTime"
	bAllowRespawnMidRound = false;
	bRoundInProgress = false;
	bAllowPlayerRespawns = false;
	LastRoundWinningTeamIndex = INDEX_NONE;
	AwardDisplayTime = 5.f; // Use this for the post-round delay
	PreRoundCountdown = 3.f; // Use this for the pre-spawn countdown
	SpawnProtectionTime = 3.f;
	bWarmupMode = false;
	SpectateDelay = 2.5f;

	// Initialize last man standing tracking
	Team0StartingSize = 0;
	Team1StartingSize = 0;
	bTeam0LastManAnnounced = false;
	bTeam1LastManAnnounced = false;
	Team0RoundDamage = 0;
	Team1RoundDamage = 0;

	// Initialize score tracking for domination/lead detection
	PreviousRedScore = 0;
	PreviousBlueScore = 0;
	bHasBroadcastTeamDominating = false;

	// Match goals (TeamGameMode uses GoalScore)
	GoalScore = ScoreLimit;
	TimeLimit = 20;
	//PlayerCameraManagerClass = AArenaPlayerCameraManager::StaticClass();

	// Overtime defaults...
	bOvertimeEnabled = true;
	OvertimeStartDelay = 5.0f;
	OvertimeBaseDamage = 5.0f;
	OvertimeDamageMultiplier = 1.5f;
	OvertimeMaxDamage = 0.0f;
	bOvertimeNonLethal = false;
	OvertimeWaveInterval = 5.0f;
	OvertimeDamageType = UUTDamageType::StaticClass();

	CurrentOvertimeWave = 0;
	CurrentWaveDamage = 0.0f;



	// OvertimeDamageType = UDamageType::StaticClass(); // Already set above
	GameStateClass = AURArenaGameState::StaticClass();
}

void AUTeamArenaGame::UpdateVictoryMessageSounds()
{
	UUTTeamArenaVictoryMessage* VictoryMessageCDO = UUTTeamArenaVictoryMessage::StaticClass()->GetDefaultObject<UUTTeamArenaVictoryMessage>();
	if (VictoryMessageCDO)
	{
		VictoryMessageCDO->RedTeamVictorySound = RedTeamVictorySound;
		VictoryMessageCDO->BlueTeamVictorySound = BlueTeamVictorySound;
		VictoryMessageCDO->DrawSound = RoundDrawSound;
		VictoryMessageCDO->RedTeamDominatingSound = RedTeamDominatingSound;
		VictoryMessageCDO->BlueTeamDominatingSound = BlueTeamDominatingSound;
		VictoryMessageCDO->RedTeamTakesLeadSound = RedTeamTakesLeadSound;
		VictoryMessageCDO->BlueTeamTakesLeadSound = BlueTeamTakesLeadSound;
	}
}

void AUTeamArenaGame::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// Override GoalScore with ScoreLimit, as ScoreLimit is our "rounds to win"
	GoalScore = ScoreLimit;

	// We manage time on a per-round basis, so set base TimeLimit to 0
	TimeLimit = 0;
}

void AUTeamArenaGame::BeginPlay()
{
	Super::BeginPlay();

	// Configure victory message sounds
	//UpdateVictoryMessageSounds();

	//GetWorldTimerManager().SetTimerForNextTick([this]()
	GetWorldTimerManager().SetTimerForNextTick(this, &AUTeamArenaGame::DeferredHandleMatchStart);
	//GetWorldTimerManager().SetTimerForNextTick(this, &AUTeamArenaGame::DeferredCheckRoundWinConditions);

	// We no longer start intermission here. 
	// HandleMatchHasStarted will be called by the engine, which will call StartIntermission.
}

/*
void AUTeamArenaGame::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();

	// This is the first time the match is starting.
	// We start in Intermission to give players time to load in.
	// We'll use the PreRoundCountdown as the initial "Get Ready" time.
	if (AURArenaGameState* GS = GetGameState<AURArenaGameState>())
	{
		GS->bMatchHasStarted = true;
	}

	StartIntermission(PreRoundCountdown);
}
*/

void AUTeamArenaGame::HandleMatchHasStarted()
{
	//Super::HandleMatchHasStarted();

	AURArenaGameState* GS = GetGameState<AURArenaGameState>();
	if (GS == nullptr)
	{
		UE_LOG(LogGameMode, Error, TEXT("HandleMatchHasStarted: AURArenaGameState is NULL. This is normal in PIE if World Settings are not set. Deferring."));

		// Try again next tick.
		GetWorldTimerManager().SetTimerForNextTick(this, &AUTeamArenaGame::HandleMatchHasStarted);
		return;
	}

	Super::HandleMatchHasStarted();

	// This is the true start of the match.
	// We start in intermission to prepare for Round 1.
	if (!GS->bMatchHasStarted)
	{
		UE_LOG(LogGameMode, Log, TEXT("HandleMatchHasStarted: Match is starting. Calling StartIntermission for the first round."));
		GS->bMatchHasStarted = true;
		StartIntermission(PreRoundCountdown);
	}
}


void AUTeamArenaGame::CallMatchStateChangeNotify()
{
	// This function intercepts all SetMatchState calls
	// and routes them to our custom handlers.

	if (GetMatchState() == MatchState::MatchIntermission)
	{
		HandleMatchIntermission();
	}
	else if (GetMatchState() == MatchState::InProgress && GetWorld()->bMatchStarted)
	{
		// We are transitioning *to* InProgress, so start the new round
		StartNextRound();
	}
	else
	{
		// Handle other states normally (WaitingToStart, etc)
		Super::CallMatchStateChangeNotify();
	}
}


void AUTeamArenaGame::DeferredHandleMatchStart()
{
	if (GetGameState<AURArenaGameState>())
	{
		// Call HandleMatchHasStarted, which will call StartIntermission
		// This mimics the PIE-safe flow
		HandleMatchHasStarted();
	}
	else
	{
		UE_LOG(LogGameMode, Error, TEXT("GameState still null after deferring BeginPlay!"));
	}
}

void AUTeamArenaGame::DeferredCheckRoundWinConditions()
{
	if (bRoundInProgress)
	{
		CheckRoundWinConditions();
	}
}


void AUTeamArenaGame::DefaultTimer()
{
	Super::DefaultTimer();

	AURArenaGameState* GS = GetGameState<AURArenaGameState>();
	if (GS == nullptr) return; // Not ready yet

	// --- Intermission Logic ---
	if (GS->GetMatchState() == MatchState::MatchIntermission)
	{
		if (IntermissionSecondsRemaining > 0)
		{
			--IntermissionSecondsRemaining;
			GS->IntermissionSecondsRemaining = (uint8)IntermissionSecondsRemaining;

			// Broadcast 3, 2, 1
			if (IntermissionSecondsRemaining > 0 && IntermissionSecondsRemaining <= 3)
			{
				BroadcastLocalized(this, UUTCountDownMessage::StaticClass(), IntermissionSecondsRemaining, nullptr, nullptr, nullptr);
			}

			if (IntermissionSecondsRemaining <= 0)
			{
				UE_LOG(LogGameMode, Log, TEXT("DefaultTimer: Intermission complete. Cleaning world and setting state to InProgress."));
				// Intermission is over. Clean up items and start the round.
				CleanupWorldForNewRound();
				SetMatchState(MatchState::InProgress);
			}
		}
		return;
	}

	// --- Round Active Logic ---
	if (bRoundInProgress)
	{
		int32 RoundRemain = 0;
		if (RoundEndTimeSeconds > 0.f)
		{
			RoundRemain = FMath::Max(0, (int32)FMath::CeilToInt(RoundEndTimeSeconds - GetWorld()->GetTimeSeconds()));
			GS->RoundSecondsRemaining = RoundRemain;

			if (RoundRemain == 0)
			{
				// --- TIME IS UP ---
				// Check if both teams are still alive.
				int32 Alive0, Alive1;
				GetAliveCounts(Alive0, Alive1);

				// SCENARIO 1: BOTH teams are alive. This is a TRUE TIE.
				if (Alive0 > 0 && Alive1 > 0)
				{
					if (bOvertimeEnabled)
					{
						// Start overtime, as requested
						if (!OvertimeWaveTimerHandle.IsValid())
						{
							StartOvertime();
						}
					}
					else
					{
						// No overtime, so do a health tiebreak
						const int32 Winner = GetTiebreakWinnerByTeamHealth();
						FTimerDelegate TimerDelegate;
						TimerDelegate.BindUFunction(this, FName("DelayedEndRound"), Winner, FName(TEXT("TimeTiebreak")));
						GetWorldTimerManager().SetTimer(TH_RoundEndDelay, TimerDelegate, 0.1f, false);
					}
				}
				// SCENARIO 2: One team is already dead. This is NOT a tie.
				else
				{
					// This is an elimination win that happened as time expired.
					// Do not start overtime. Just run the elimination check one last time.
					CheckRoundWinConditions();
				}

				return; // Stop further processing this tick
			}
		}

		// Time has not run out, so check for eliminations normally.
		CheckRoundWinConditions();
	}
}



/**
 * NEW: This function is called when the state *enters* MatchIntermission.
 * It's responsible for pre-round setup, hiding pawns, and forcing spectators.
 */
void AUTeamArenaGame::HandleMatchIntermission()
{
	UE_LOG(LogGameMode, Warning, TEXT("HandleMatchIntermission: Preparing for next round."));


	// Reset spawn points for the new round
	ResetSpawnSelectionForNewRound();


	// Force losers to view winners (if LastRoundWinningTeamIndex is set)
	if (AURArenaGameState* GS = GetGameState<AURArenaGameState>())
	{
		ForceLosersToViewWinners(GS->LastRoundWinningTeamIndex);
	}
}


/**
 * REWRITTEN: This function just sets the state.
 * The DefaultTimer will handle the countdown.
 */
void AUTeamArenaGame::StartIntermission(int32 Seconds)
{
	UE_LOG(LogGameMode, Warning, TEXT("StartIntermission: Entering intermission for %d seconds."), Seconds);

	bRoundInProgress = false;
	IntermissionSecondsRemaining = FMath::Max(1, Seconds); // Ensure at least 1 second
	RoundEndTimeSeconds = 0.f;

	// Stop overtime if it's running
	StopOvertime();

	// PIE-SAFETY CHECK: Only update GameState if it's the correct class
	if (AURArenaGameState* GS = GetGameState<AURArenaGameState>())
	{
		GS->bRoundInProgress = false;
		GS->IntermissionSecondsRemaining = (uint8)IntermissionSecondsRemaining;
		GS->ForceNetUpdate();
	}
	else
	{
		UE_LOG(LogGameMode, Warning, TEXT("StartIntermission: AURArenaGameState is NULL. State will not be replicated."));
	}

	// This is the key: Set the match state.
	// This will trigger CallMatchStateChangeNotify -> HandleMatchIntermission
	SetMatchState(MatchState::MatchIntermission);
}

/**
 * REWRITTEN: This function is now ONLY responsible for spawning players and starting the round timer.
 * It is called by CallMatchStateChangeNotify when the state changes to InProgress.
 */
void AUTeamArenaGame::StartNextRound()
{
	UE_LOG(LogGameMode, Warning, TEXT("StartNextRound: Spawning players and starting round."));

	if (bWarmupMode)
	{
		bRoundInProgress = false;
		return;
	}

	if (bAnnounceTeam)
	{
		bAnnounceTeam = false;
		UE_LOG(LogGameMode, Warning, TEXT("Disabled team announcements for subsequent rounds"));
	}

	// Reset per-round trackers
	LastRoundWinningTeamIndex = INDEX_NONE;
	bTeam0LastManAnnounced = false;
	bTeam1LastManAnnounced = false;
	Team0StartingSize = 0;
	Team1StartingSize = 0;
	Team0RoundDamage = 0.0f;
	Team1RoundDamage = 0.0f;
	PlayerRoundDamage.Empty();

	// 1. Reset all player pawns from the *previous* round
	ResetPlayersForNewRound();

	// 2. Spawn players for the *new* round
	bAllowPlayerRespawns = true; // Allow RestartPlayer to work
	int32 PlayersSpawned = 0;

	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AController* C = It->Get();
		if (!C) continue;

		AUTPlayerState* PS = Cast<AUTPlayerState>(C->PlayerState);
		if (PS && !PS->bOnlySpectator)
		{
			// Make sure flags are reset
			PS->bOutOfLives = false;
			PS->ForceNetUpdate();

			if (AUTPlayerController* PC = Cast<AUTPlayerController>(C))
			{
				// Force controller back into playing state
				PC->ChangeState(NAME_Playing);
				PC->ClientGotoState(NAME_Playing);
			}

			// Spawn the player
			RestartPlayer(C);
			PlayersSpawned++;

			// Track team sizes for Last Man Standing
			if (PS->Team)
			{
				if (PS->Team->TeamIndex == 0) Team0StartingSize++;
				else if (PS->Team->TeamIndex == 1) Team1StartingSize++;
			}
		}
	}

	bAllowPlayerRespawns = false; // Disable respawns again

	UE_LOG(LogGameMode, Warning, TEXT("Round starting sizes - Team0: %d, Team1: %d"), Team0StartingSize, Team1StartingSize);

	// 3. Set round timer
	if (RoundTimeSeconds > 0)
	{
		RoundEndTimeSeconds = GetWorld()->GetTimeSeconds() + RoundTimeSeconds;
	}
	else
	{
		RoundEndTimeSeconds = 0.f;
	}

	// 4. Set final round state
	bRoundInProgress = true;

	// 5. Update GameState
	if (AURArenaGameState* GS = GetWorld()->GetGameState<AURArenaGameState>())
	{
		GS->bRoundInProgress = true;
		GS->RoundSecondsRemaining = (uint8)RoundTimeSeconds;
		GS->IntermissionSecondsRemaining = 0; // Ensure intermission is over
		GS->LastRoundWinningTeamIndex = INDEX_NONE; // Clear winner
		GS->ForceNetUpdate();
	}

	// 6. Broadcast "Round Start" message
	// TODO: You may want a different message, but 0 is "Fight!"
	BroadcastLocalized(this, UUTGameMessage::StaticClass(), 0, NULL, NULL, NULL);

	// 7. Add a small delay before checking win conditions (in case of empty teams, etc)
	WinCheckHoldUntilSeconds = GetWorld()->GetTimeSeconds() + 0.25f;
	GetWorldTimerManager().ClearTimer(InitialWinCheckHandle);
	GetWorldTimerManager().SetTimer(
		InitialWinCheckHandle, this,
		&AUTeamArenaGame::DelayedInitialWinCheck, 0.25f, false);

	UE_LOG(LogGameMode, Warning, TEXT("New round started. Manually attempted to spawn %d players."), PlayersSpawned);
}


void AUTeamArenaGame::EndRoundForTeam(int32 WinnerTeamIndex, FName Reason)
{
	UE_LOG(LogGameMode, Warning, TEXT("EndRoundForTeam called: Winner=%d, Reason=%s"),
		WinnerTeamIndex, *Reason.ToString());

	if (bWarmupMode || !bRoundInProgress)
	{
		// No scoring in warmup, or round already ended
		return;
	}

	// Stop the round *immediately*
	bRoundInProgress = false;
	RoundEndTimeSeconds = 0.f;
	LastRoundWinningTeamIndex = WinnerTeamIndex;
	// Stop overtime if it's running
	StopOvertime();

	// Check achievements before we score (in case of end-game)
	CheckRoundAchievements(WinnerTeamIndex, Reason);

	// --- Score the round ---
	bool bIsDraw = (WinnerTeamIndex == INDEX_NONE);
	if (!bIsDraw)
	{
		if (Teams.IsValidIndex(WinnerTeamIndex))
		{
			Teams[WinnerTeamIndex]->Score += 1;
			Teams[WinnerTeamIndex]->ForceNetUpdate();
			UE_LOG(LogGameMode, Warning, TEXT("Team %d wins! New score: %d"), WinnerTeamIndex, Teams[WinnerTeamIndex]->Score);
		}
	}
	else
	{
		UE_LOG(LogGameMode, Warning, TEXT("Round draw - no score change"));
	}

	// --- Check for Game End ---
	if (!bIsDraw && Teams[WinnerTeamIndex]->Score >= GoalScore)
	{
		// Game Over
		UE_LOG(LogGameMode, Warning, TEXT("Team %d has reached the score limit. Ending game."), WinnerTeamIndex);
		AUTPlayerState* BestPlayer = FindBestPlayerOnTeam(WinnerTeamIndex);
		EndGame(BestPlayer, FName(TEXT("ScoreLimit")));
		return; // Do not proceed to intermission
	}

	// --- Game is NOT over, proceed to intermission ---

	// Update GameState with winner for this intermission
	if (AURArenaGameState* GS = GetGameState<AURArenaGameState>())
	{
		GS->bRoundInProgress = false;
		GS->LastRoundWinningTeamIndex = WinnerTeamIndex;
		GS->ForceNetUpdate();
	}

	// Broadcast round results (audio/visual)
	BroadcastRoundResults(WinnerTeamIndex, bIsDraw);

	// Check for domination/lead messages
	CheckForDominationAndLead(WinnerTeamIndex);

	// This is now called by HandleMatchIntermission, but we do it here
	// to immediately snap losers' cameras.
	ForceLosersToViewWinners(WinnerTeamIndex);

	// Start the intermission timer
	StartIntermission(AwardDisplayTime);
}

// This function is unchanged, but its role is now clear:
// It's called by the timer set in CheckRoundWinConditions.
void AUTeamArenaGame::DelayedEndRound(int32 WinnerTeamIndex, FName Reason)
{
	EndRoundForTeam(WinnerTeamIndex, Reason);
}


// This function is unchanged, it's the "cleanup" step
void AUTeamArenaGame::ResetPlayersForNewRound()
{
	Team0RoundDamage = 0.0f;
	Team1RoundDamage = 0.0f;
	PlayerRoundDamage.Empty();

	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		AController* C = It->Get();
		if (!C) continue;

		AUTPlayerState* PS = Cast<AUTPlayerState>(C->PlayerState);
		if (PS)
		{
			// Only reset OutOfLives, other flags are handled in StartNextRound
			PS->SetOutOfLives(false);
			PS->RoundKills = 0;
		}

		// Get and destroy the controller's current pawn (if any)
		APawn* Pawn = C->GetPawn();
		if (Pawn)
		{
			DiscardInventory(Pawn, C);
			C->UnPossess();
			Pawn->Destroy();
		}
	}
}

// This function is unchanged
void AUTeamArenaGame::CleanupWorldForNewRound()
{
	// Clean up any dropped items
	for (TActorIterator<AUTDroppedPickup> It(GetWorld()); It; ++It)
	{
		It->Destroy();
	}

	// Reset any placed pickups
	for (FActorIterator It(GetWorld()); It; ++It)
	{
		if (It->GetClass()->ImplementsInterface(UUTResetInterface::StaticClass()))
		{
			IUTResetInterface::Execute_Reset(*It);
		}
	}
}


/*
 *
 * --- NO CHANGES NEEDED TO FUNCTIONS BELOW THIS LINE ---
 * (They are provided for context and completeness)
 *
 */

 // ... (Rest of your functions: RestartPlayer, ScoreKill_Implementation, GetAliveCounts, CheckLastManStanding, etc.) ...
 // ... (These functions are already correct or were fixed in the previous step and don't need changes for the new state logic) ...

 // [The rest of TeamArenaGame.cpp follows]

 // Example: Your existing RestartPlayer (no changes needed)
void AUTeamArenaGame::RestartPlayer(AController* NewPlayer)
{
	// Always log the attempt
	UE_LOG(LogGameMode, Warning, TEXT("RestartPlayer called for %s, bRoundInProgress=%s"),
		NewPlayer ? *NewPlayer->GetName() : TEXT("NULL"),
		bRoundInProgress ? TEXT("true") : TEXT("false"));
	if (!NewPlayer) return;

	// Skip if they already have a pawn (protect mid-equip)
	if (NewPlayer->GetPawn())
	{
		UE_LOG(LogGameMode, Warning, TEXT("  Controller already has pawn; skipping respawn"));
		return;
	}
	if (NewPlayer && NewPlayer->PlayerState)
	{
		UE_LOG(LogGameMode, Warning, TEXT("  PlayerState: %s"), *NewPlayer->PlayerState->GetName());
	}

	const bool bShouldAllowSpawn = (bAllowRespawnMidRound || bAllowPlayerRespawns || bWarmupMode);
	UE_LOG(LogGameMode, Warning, TEXT("  bAllowRespawnMidRound=%s, bAllowPlayerRespawns=%s, bShouldAllowSpawn=%s"),
		bAllowRespawnMidRound ? TEXT("true") : TEXT("false"),
		bAllowPlayerRespawns ? TEXT("true") : TEXT("false"),
		bShouldAllowSpawn ? TEXT("true") : TEXT("false"));


	if (bShouldAllowSpawn)
	{
		AActor* ChosenStart = ChoosePlayerStart(NewPlayer);
		UE_LOG(LogGameMode, Warning, TEXT("  Chosen PlayerStart: %s"),
			ChosenStart ? *ChosenStart->GetName() : TEXT("NULL"));

		Super::RestartPlayer(NewPlayer);

		if (NewPlayer && NewPlayer->GetPawn())
		{
			UE_LOG(LogGameMode, Warning, TEXT("  Successfully spawned pawn: %s"), *NewPlayer->GetPawn()->GetClass()->GetName());
		}
		else
		{
			UE_LOG(LogGameMode, Warning, TEXT("  FAILED to spawn pawn!"));
		}
	}
	else
	{
		UE_LOG(LogGameMode, Warning, TEXT("  RestartPlayer blocked (mid-round respawn disabled)"));
	}
}

// Example: Your existing ScoreKill_Implementation (no changes needed)
void AUTeamArenaGame::ScoreKill_Implementation(AController* Killer, AController* Other, APawn* KilledPawn, TSubclassOf<UDamageType> DamageType)
{
	// Call parent for individual player scoring only (no team score changes)
	Super::ScoreKill_Implementation(Killer, Other, KilledPawn, DamageType);

	if (GetMatchState() != MatchState::InProgress || !bRoundInProgress)
		return;

	AUTPlayerState* OtherPS = Other ? Cast<AUTPlayerState>(Other->PlayerState) : nullptr;
	if (!OtherPS) return;

	// Mark them as out of lives immediately
	if (!OtherPS->bOutOfLives)
	{
		OtherPS->bOutOfLives = true;
		OtherPS->ForceNetUpdate();
	}

	int32 Alive0, Alive1;
	GetAliveCounts(Alive0, Alive1);

	CheckLastManStanding(Alive0, Alive1);

	const bool Team0Eliminated = (Alive0 == 0);
	const bool Team1Eliminated = (Alive1 == 0);

	if (Team0Eliminated || Team1Eliminated)
	{
		UE_LOG(LogGameMode, Warning, TEXT("ScoreKill: This kill ends the round (Team0=%d, Team1=%d). Deferring spectate to EndRoundForTeam."), Alive0, Alive1);
	}
	else
	{
		AUTPlayerController* PC = Cast<AUTPlayerController>(Other);
		if (PC && !bAllowRespawnMidRound)
		{
			// --- THIS IS THE CHANGE ---
			// Don't call ForceTeamSpectate immediately.
			// Set a timer to call it after SpectateDelay.
			UE_LOG(LogGameMode, Warning, TEXT("ScoreKill: Round continues. Setting timer for DelayedForceSpectate in %.1f sec."), SpectateDelay);
			
			FTimerHandle TimerHandle_SpectateDelay;
			FTimerDelegate SpectateDelegate;
			SpectateDelegate.BindUFunction(this, FName("DelayedForceSpectate"), OtherPS);
			GetWorldTimerManager().SetTimer(TimerHandle_SpectateDelay, SpectateDelegate, SpectateDelay, false);
		}
	}
}



/** NEW: Implementation for the delayed spectator function */
void AUTeamArenaGame::DelayedForceSpectate(AUTPlayerState* DeadPS)
{
	// --- SAFETY CHECKS ---
	// 1. Check if the PlayerState is still valid
	if (DeadPS == nullptr || DeadPS->IsPendingKill())
	{
		UE_LOG(LogGameMode, Log, TEXT("DelayedForceSpectate: DeadPS is null or pending kill. Aborting."));
		return;
	}

	// 2. Check if the PlayerController is still valid
	AUTPlayerController* PC = Cast<AUTPlayerController>(DeadPS->GetOwner());
	if (PC == nullptr)
	{
		UE_LOG(LogGameMode, Log, TEXT("DelayedForceSpectate: PlayerController for %s is null. Aborting."), *DeadPS->PlayerName);
		return;
	}

	// 3. Check if the round ended *during* our delay
	if (!bRoundInProgress)
	{
		UE_LOG(LogGameMode, Log, TEXT("DelayedForceSpectate: Round ended during delay. Aborting spectate."), *DeadPS->PlayerName);
		// EndRoundForTeam will handle their camera now
		return;
	}

	// --- ALL CHECKS PASSED ---
	UE_LOG(LogGameMode, Log, TEXT("DelayedForceSpectate: Delay complete. Forcing %s to spectate."), *DeadPS->PlayerName);
	ForceTeamSpectate(DeadPS);
}

// --- This line is just to show where the other functions would be ---
#pragma region "--- NO OTHER CHANGES NEEDED BELOW THIS LINE ---"

AActor* AUTeamArenaGame::ChoosePlayerStart_Implementation(AController* Player)
{
	// Initialize system if needed
	if (!bSpawnPointsInitialized)
	{
		InitializeSpawnPointSystem();
		bSpawnPointsInitialized = true;
	}

	// Get player's team
	AUTPlayerState* PS = Player ? Cast<AUTPlayerState>(Player->PlayerState) : nullptr;
	if (!PS)
	{
		UE_LOG(LogGameMode, Warning, TEXT("ChoosePlayerStart: PlayerState is NULL, using fallback"));
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	if (!PS->Team)
	{
		UE_LOG(LogGameMode, Warning, TEXT("ChoosePlayerStart: Player %s has no team, using fallback"), *PS->PlayerName);
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	const int32 TeamIndex = PS->Team->TeamIndex;
	TArray<APlayerStart*>& SelectedSpawns = (TeamIndex == 0) ? Team0SelectedSpawns : Team1SelectedSpawns;

	if (SelectedSpawns.Num() == 0)
	{
		SelectOptimalSpawnPairForTeam(TeamIndex);
	}

	if (SelectedSpawns.Num() == 0)
	{
		UE_LOG(LogGameMode, Warning, TEXT("ChoosePlayerStart: No spawns selected for team %d, using fallback"), TeamIndex);
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	APlayerStart* ChosenSpawn = nullptr;

	if (SelectedSpawns.Num() >= 2)
	{
		APlayerStart* SpawnA = SelectedSpawns[0];
		APlayerStart* SpawnB = SelectedSpawns[1];

		int32 CountAtSpawnA = 0;
		int32 CountAtSpawnB = 0;
		float SpawnCheckRadiusSq = 150.f * 150.f;

		for (FConstPawnIterator It = GetWorld()->GetPawnIterator(); It; ++It)
		{
			APawn* Pawn = It->Get();
			if (Pawn && Pawn->PlayerState)
			{
				AUTPlayerState* OtherPS = Cast<AUTPlayerState>(Pawn->PlayerState);
				if (OtherPS && OtherPS != PS && OtherPS->Team && OtherPS->Team->TeamIndex == TeamIndex)
				{
					FVector PawnLocation = Pawn->GetActorLocation();
					if (SpawnA && (FVector::DistSquared(PawnLocation, SpawnA->GetActorLocation()) < SpawnCheckRadiusSq))
					{
						CountAtSpawnA++;
					}
					else if (SpawnB && (FVector::DistSquared(PawnLocation, SpawnB->GetActorLocation()) < SpawnCheckRadiusSq))
					{
						CountAtSpawnB++;
					}
				}
			}
		}

		if (CountAtSpawnA <= CountAtSpawnB)
		{
			ChosenSpawn = SpawnA;
		}
		else
		{
			ChosenSpawn = SpawnB;
		}
	}
	else if (SelectedSpawns.Num() > 0)
	{
		ChosenSpawn = SelectedSpawns[0];
	}

	if (!ChosenSpawn)
	{
		UE_LOG(LogGameMode, Warning, TEXT("ChoosePlayerStart: Failed to choose spawn for team %d, using fallback"), TeamIndex);
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	FVector BaseLocation = ChosenSpawn->GetActorLocation();
	if (!IsLocationClearOfPlayers(BaseLocation))
	{
		for (int32 Attempt = 0; Attempt < MaxSpawnOffsetAttempts; ++Attempt)
		{
			FVector OffsetLocation = FindSafeSpawnOffset(ChosenSpawn, Attempt);
			if (IsLocationClearOfPlayers(OffsetLocation))
			{
				UE_LOG(LogGameMode, Log, TEXT("Using offset spawn location for %s (attempt %d)"), *PS->PlayerName, Attempt);
				break;
			}
		}
	}

	UE_LOG(LogGameMode, Log, TEXT("Selected spawn %s for team %d player %s"),
		*ChosenSpawn->GetName(), TeamIndex, *PS->PlayerName);

	return ChosenSpawn;
}

void AUTeamArenaGame::InitializeSpawnPointSystem()
{
	UE_LOG(LogGameMode, Log, TEXT("InitializeSpawnPointSystem: Starting comprehensive spawn analysis"));
	AllSpawnPoints.Empty();
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (APlayerStart* Spawn = *It)
		{
			AllSpawnPoints.Add(FSpawnPointData(Spawn));
		}
	}
	if (AllSpawnPoints.Num() < 2)
	{
		UE_LOG(LogGameMode, Error, TEXT("InitializeSpawnPointSystem: Insufficient spawn points (%d), need at least 4"), AllSpawnPoints.Num());
		return;
	}
	ScoreAllSpawnPoints();
	UE_LOG(LogGameMode, Log, TEXT("InitializeSpawnPointSystem: Analyzed %d spawn points"), AllSpawnPoints.Num());
}

void AUTeamArenaGame::ScoreAllSpawnPoints()
{
	if (AllSpawnPoints.Num() == 0) return;
	FVector MapMin = AllSpawnPoints[0].PlayerStart->GetActorLocation();
	FVector MapMax = MapMin;
	FVector MapCenter = FVector::ZeroVector;
	for (const FSpawnPointData& SpawnData : AllSpawnPoints)
	{
		FVector Location = SpawnData.PlayerStart->GetActorLocation();
		MapMin = FVector(FMath::Min(MapMin.X, Location.X), FMath::Min(MapMin.Y, Location.Y), FMath::Min(MapMin.Z, Location.Z));
		MapMax = FVector(FMath::Max(MapMax.X, Location.X), FMath::Max(MapMax.Y, Location.Y), FMath::Max(MapMax.Z, Location.Z));
		MapCenter += Location;
	}
	MapCenter /= AllSpawnPoints.Num();
	FVector MapSize = MapMax - MapMin;
	float ZRange = MapSize.Z;
	for (FSpawnPointData& SpawnData : AllSpawnPoints)
	{
		FVector Location = SpawnData.PlayerStart->GetActorLocation();
		SpawnData.HeightScore = (ZRange > 0) ? ((Location.Z - MapMin.Z) / ZRange) : 0.5f;
		FVector RelativePos = Location - MapCenter;
		float LargestAxis = FMath::Max3(MapSize.X, MapSize.Y, MapSize.Z);
		if (LargestAxis > 0)
		{
			FVector NormalizedPos = RelativePos / LargestAxis;
			if (FMath::Abs(MapSize.X) >= FMath::Abs(MapSize.Y))
			{
				SpawnData.TeamSideScore = NormalizedPos.X;
			}
			else
			{
				SpawnData.TeamSideScore = NormalizedPos.Y;
			}
		}
		else
		{
			SpawnData.TeamSideScore = 0.0f;
		}
	}
}

void AUTeamArenaGame::SelectOptimalSpawnPairForTeam(int32 TeamIndex)
{
	TArray<APlayerStart*>& SelectedSpawns = (TeamIndex == 0) ? Team0SelectedSpawns : Team1SelectedSpawns;
	const TArray<APlayerStart*>& EnemySpawns = (TeamIndex == 0) ? Team1SelectedSpawns : Team0SelectedSpawns;
	SelectedSpawns.Empty();
	TArray<FSpawnPointData*> Candidates = GetSpawnCandidatesForTeam(TeamIndex);
	if (Candidates.Num() < 2)
	{
		UE_LOG(LogGameMode, Warning, TEXT("SelectOptimalSpawnPairForTeam: Insufficient candidates for team %d (%d available)"), TeamIndex, Candidates.Num());
		for (FSpawnPointData* Candidate : Candidates)
		{
			if (Candidate && Candidate->PlayerStart)
			{
				SelectedSpawns.Add(Candidate->PlayerStart);
				Candidate->IncrementUsageForTeam(TeamIndex, CurrentRoundNumber);
			}
		}
		return;
	}
	APlayerStart* PrimarySpawn = nullptr;
	APlayerStart* SecondarySpawn = nullptr;
	FindMaxDistanceSpawnPair(Candidates, EnemySpawns, PrimarySpawn, SecondarySpawn);
	if (PrimarySpawn)
	{
		SelectedSpawns.Add(PrimarySpawn);
		for (FSpawnPointData& SpawnData : AllSpawnPoints)
		{
			if (SpawnData.PlayerStart == PrimarySpawn)
			{
				SpawnData.IncrementUsageForTeam(TeamIndex, CurrentRoundNumber);
				break;
			}
		}
	}
	if (SecondarySpawn)
	{
		SelectedSpawns.Add(SecondarySpawn);
		for (FSpawnPointData& SpawnData : AllSpawnPoints)
		{
			if (SpawnData.PlayerStart == SecondarySpawn)
			{
				SpawnData.IncrementUsageForTeam(TeamIndex, CurrentRoundNumber);
				break;
			}
		}
	}
	UE_LOG(LogGameMode, Log, TEXT("SelectOptimalSpawnPairForTeam: Team %d selected %s and %s"),
		TeamIndex,
		PrimarySpawn ? *PrimarySpawn->GetName() : TEXT("None"),
		SecondarySpawn ? *SecondarySpawn->GetName() : TEXT("None"));
}

void AUTeamArenaGame::FindMaxDistanceSpawnPair(const TArray<FSpawnPointData*>& CandidateSpawns, const TArray<APlayerStart*>& EnemySpawns, APlayerStart*& OutPrimary, APlayerStart*& OutSecondary)
{
	OutPrimary = nullptr;
	OutSecondary = nullptr;
	if (CandidateSpawns.Num() < 2)
	{
		if (CandidateSpawns.Num() == 1)
		{
			OutPrimary = CandidateSpawns[0]->PlayerStart;
		}
		return;
	}
	float BestScore = -1.0f;
	for (int32 i = 0; i < CandidateSpawns.Num(); ++i)
	{
		for (int32 j = i + 1; j < CandidateSpawns.Num(); ++j)
		{
			APlayerStart* Spawn1 = CandidateSpawns[i]->PlayerStart;
			APlayerStart* Spawn2 = CandidateSpawns[j]->PlayerStart;
			if (!Spawn1 || !Spawn2) continue;
			float MinDist1 = CalculateMinDistanceToEnemySpawns(Spawn1, EnemySpawns);
			float MinDist2 = CalculateMinDistanceToEnemySpawns(Spawn2, EnemySpawns);
			float HeightScore1 = CandidateSpawns[i]->HeightScore;
			float HeightScore2 = CandidateSpawns[j]->HeightScore;
			float UsageScore1 = 1.0f / (1.0f + CandidateSpawns[i]->GetUsageCountForTeam(0) + CandidateSpawns[i]->GetUsageCountForTeam(1));
			float UsageScore2 = 1.0f / (1.0f + CandidateSpawns[j]->GetUsageCountForTeam(0) + CandidateSpawns[j]->GetUsageCountForTeam(1));
			float SpawnSeparation = FVector::Dist(Spawn1->GetActorLocation(), Spawn2->GetActorLocation());
			float SeparationScore = FMath::Min(SpawnSeparation / 1000.0f, 1.0f);
			float CombinedScore = (MinDist1 + MinDist2) * 0.4f + (HeightScore1 + HeightScore2) * 0.3f + (UsageScore1 + UsageScore2) * 0.2f + SeparationScore * 0.1f;
			if (CombinedScore > BestScore)
			{
				BestScore = CombinedScore;
				if (HeightScore1 >= HeightScore2)
				{
					OutPrimary = Spawn1;
					OutSecondary = Spawn2;
				}
				else
				{
					OutPrimary = Spawn2;
					OutSecondary = Spawn1;
				}
			}
		}
	}
}

float AUTeamArenaGame::CalculateMinDistanceToEnemySpawns(APlayerStart* SpawnPoint, const TArray<APlayerStart*>& EnemySpawns)
{
	if (!SpawnPoint || EnemySpawns.Num() == 0)
	{
		return 10000.0f;
	}
	float MinDistance = FLT_MAX;
	FVector SpawnLocation = SpawnPoint->GetActorLocation();
	for (APlayerStart* EnemySpawn : EnemySpawns)
	{
		if (EnemySpawn)
		{
			float Distance = FVector::Dist(SpawnLocation, EnemySpawn->GetActorLocation());
			MinDistance = FMath::Min(MinDistance, Distance);
		}
	}
	return (MinDistance == FLT_MAX) ? 10000.0f : MinDistance;
}

TArray<FSpawnPointData*> AUTeamArenaGame::GetSpawnCandidatesForTeam(int32 TeamIndex)
{
	TArray<FSpawnPointData*> Candidates;
	for (FSpawnPointData& SpawnData : AllSpawnPoints)
	{
		if (!SpawnData.PlayerStart) continue;
		bool bIsTeamSide = (TeamIndex == 0 && SpawnData.TeamSideScore <= 0.0f) || (TeamIndex == 1 && SpawnData.TeamSideScore >= 0.0f);
		if (bIsTeamSide || FMath::Abs(SpawnData.TeamSideScore) < 0.2f)
		{
			Candidates.Add(&SpawnData);
		}
	}
	Candidates.Sort([TeamIndex](const FSpawnPointData& A, const FSpawnPointData& B)
		{
			int32 UsageA = A.GetUsageCountForTeam(TeamIndex);
			int32 UsageB = B.GetUsageCountForTeam(TeamIndex);
			if (UsageA != UsageB)
			{
				return UsageA < UsageB;
			}
			float ScoreA = A.HeightScore + FMath::Abs(A.TeamSideScore);
			float ScoreB = B.HeightScore + FMath::Abs(B.TeamSideScore);
			return ScoreA > ScoreB;
		});
	return Candidates;
}

FVector AUTeamArenaGame::FindSafeSpawnOffset(APlayerStart* BaseSpawn, int32 AttemptIndex)
{
	if (!BaseSpawn) return FVector::ZeroVector;
	FVector BaseLocation = BaseSpawn->GetActorLocation();
	float Angle = (AttemptIndex * 45.0f) * PI / 180.0f;
	float Distance = SpawnOffsetDistance * (1.0f + AttemptIndex * 0.5f);
	FVector Offset = FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f);
	FVector TestLocation = BaseLocation + Offset;
	return TestLocation;
}

bool AUTeamArenaGame::IsLocationClearOfPlayers(const FVector& Location, float CheckRadius)
{
	for (FConstPawnIterator It = GetWorld()->GetPawnIterator(); It; ++It)
	{
		if (APawn* Pawn = It->Get())
		{
			if (Pawn->IsValidLowLevel() && !Pawn->IsPendingKill())
			{
				float Distance = FVector::Dist(Location, Pawn->GetActorLocation());
				if (Distance < CheckRadius)
				{
					return false;
				}
			}
		}
	}
	return true;
}

void AUTeamArenaGame::ResetSpawnSelectionForNewRound()
{
	Team0SelectedSpawns.Empty();
	Team1SelectedSpawns.Empty();
	++CurrentRoundNumber;
	UE_LOG(LogGameMode, Log, TEXT("ResetSpawnSelectionForNewRound: Starting round %d"), CurrentRoundNumber);
}

void AUTeamArenaGame::ForceTeamSpectate(AUTPlayerState* DeadPS)
{
	if (DeadPS == nullptr) return;
	if (!DeadPS->bOutOfLives)
	{
		DeadPS->bOutOfLives = true;
		DeadPS->ForceNetUpdate();
	}
	AUTPlayerController* PC = Cast<AUTPlayerController>(DeadPS->GetOwner());
	if (!PC) return;
	PC->ChangeState(NAME_Spectating);
	PC->ClientGotoState(NAME_Spectating);
	if (AUTPlayerState* TeamTarget = FindAliveTeammate(DeadPS))
	{
		//PC->SetViewTarget(TeamTarget->GetUTCharacter());
		PC->ServerViewPlayerState(TeamTarget);
		return;
	}
	if (AUTPlayerState* EnemyTarget = FindAliveEnemy(DeadPS))
	{
		//PC->SetViewTarget(EnemyTarget->GetUTCharacter());
		PC->ServerViewPlayerState(EnemyTarget);
		return;
	}
	PC->ServerViewSelf();
}

AUTPlayerState* AUTeamArenaGame::FindAliveTeammate(AUTPlayerState* PS) const
{
	if (!PS || !PS->Team) return nullptr;
	const int32 TeamIdx = PS->Team->TeamIndex;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		AUTPlayerState* OtherPS = PC ? Cast<AUTPlayerState>(PC->PlayerState) : nullptr;
		if (!OtherPS || OtherPS == PS || !OtherPS->Team || OtherPS->Team->TeamIndex != TeamIdx) continue;
		APawn* P = PC->GetPawn();
		AUTCharacter* C = Cast<AUTCharacter>(P);
		if (P && (!C || !C->IsDead()))
		{
			return OtherPS;
		}
	}
	return nullptr;
}

AUTPlayerState* AUTeamArenaGame::FindAliveEnemy(AUTPlayerState* PS) const
{
	if (!PS || !PS->Team) return nullptr;
	const int32 MyTeam = PS->Team->TeamIndex;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		AUTPlayerState* OtherPS = PC ? Cast<AUTPlayerState>(PC->PlayerState) : nullptr;
		if (!OtherPS || !OtherPS->Team || OtherPS->Team->TeamIndex == MyTeam) continue;
		APawn* P = PC->GetPawn();
		AUTCharacter* C = Cast<AUTCharacter>(P);
		if (P && (!C || !C->IsDead()))
		{
			return OtherPS;
		}
	}
	return nullptr;
}

int32 AUTeamArenaGame::CountAliveOnTeam(int32 TeamIndex) const
{
	int32 Alive = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const AUTPlayerState* PS = PC ? Cast<AUTPlayerState>(PC->PlayerState) : nullptr;
		if (!PS || !PS->Team || PS->Team->TeamIndex != TeamIndex || PS->bOnlySpectator) continue;
		const APawn* P = PC->GetPawn();
		const AUTCharacter* C = Cast<AUTCharacter>(P);
		if (P && (!C || !C->IsDead()))
		{
			++Alive;
		}
	}
	return Alive;
}

void AUTeamArenaGame::ForceLosersToViewWinners(int32 WinnerTeamIndex)
{
	UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Called with WinnerTeamIndex: %d"), WinnerTeamIndex);
	if (WinnerTeamIndex < 0)
	{
		UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Round was a draw. No view change."));
		return;
	}
	const int32 LoserTeamIndex = (WinnerTeamIndex == 0) ? 1 : 0;
	UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: LoserTeamIndex is: %d"), LoserTeamIndex);
	AUTPlayerState* TargetPS = FindAliveOnTeamPS(WinnerTeamIndex);
	if (!TargetPS)
	{
		UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: No alive winner found. Finding any winner..."));
		TargetPS = FindAnyOnTeamPS(WinnerTeamIndex);
		if (!TargetPS)
		{
			UE_LOG(LogGameMode, Error, TEXT("ForceLosersToViewWinners: FAILED. No winner PlayerState found to spectate."));
			return;
		}
	}
	AUTCharacter* TargetCharacter = TargetPS->GetUTCharacter();
	if (!TargetCharacter)
	{
		UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Target PlayerState %s has no character!"), *TargetPS->PlayerName);
		return;
	}
	UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Found target character to spectate: %s (owned by %s)"),
		*TargetCharacter->GetName(), *TargetPS->PlayerName);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AUTPlayerController* PC = Cast<AUTPlayerController>(It->Get());
		AUTPlayerState* PS = PC ? Cast<AUTPlayerState>(PC->PlayerState) : nullptr;
		if (!PC || !PS || !PS->Team) continue;
		if (PS->Team->TeamIndex == LoserTeamIndex)
		{
			UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Processing player %s. Current state: %s"),
				*PS->PlayerName, PC->GetStateName().IsValid() ? *PC->GetStateName().ToString() : TEXT("Unknown"));
			if (!PC->IsInState(NAME_Spectating))
			{
				PC->ChangeState(NAME_Spectating);
				PC->ClientGotoState(NAME_Spectating);
				UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Changed %s to spectating state"), *PS->PlayerName);
			}
			if (!PS->bOutOfLives)
			{
				PS->bOutOfLives = true;
				PS->ForceNetUpdate();
			}
			PC->SetViewTarget(TargetCharacter);
			UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Set %s to directly view character %s"),
				*PS->PlayerName, *TargetCharacter->GetName());
		}
	}
}

void AUTeamArenaGame::DebugPlayerStates()
{
	UE_LOG(LogGameMode, Warning, TEXT("=== DEBUG PLAYER STATES ==="));
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AUTPlayerController* PC = Cast<AUTPlayerController>(It->Get());
		AUTPlayerState* PS = PC ? Cast<AUTPlayerState>(PC->PlayerState) : nullptr;
		if (!PC || !PS) continue;
		FName StateName = PC->GetStateName();
		AActor* ViewTarget = PC->GetViewTarget();
		FString ViewTargetName = ViewTarget ? ViewTarget->GetName() : TEXT("None");
		UE_LOG(LogGameMode, Warning, TEXT("Player: %s, Team: %d, State: %s, ViewTarget: %s, OutOfLives: %s, HasPawn: %s"),
			*PS->PlayerName,
			PS->Team ? PS->Team->TeamIndex : -1,
			StateName.IsValid() ? *StateName.ToString() : TEXT("Unknown"),
			*ViewTargetName,
			PS->bOutOfLives ? TEXT("Yes") : TEXT("No"),
			PC->GetPawn() ? TEXT("Yes") : TEXT("No"));
	}
	UE_LOG(LogGameMode, Warning, TEXT("=== END DEBUG ==="));
}

bool AUTeamArenaGame::CanSpectate_Implementation(APlayerController* Viewer, APlayerState* ViewTarget)
{
	if (!Viewer || !ViewTarget) return false;
	const AUTPlayerState* ViewerPS = Cast<AUTPlayerState>(Viewer->PlayerState);
	const AUTPlayerState* TargetPS = Cast<AUTPlayerState>(ViewTarget);
	if (!ViewerPS || !TargetPS || !ViewerPS->Team || !TargetPS->Team)
	{
		return Super::CanSpectate_Implementation(Viewer, ViewTarget);
	}
	if (!bRoundInProgress)
	{
		return Super::CanSpectate_Implementation(Viewer, ViewTarget);
	}
	if (bRoundInProgress)
	{
		const int32 MyTeamAlive = CountAliveOnTeam(ViewerPS->Team->TeamIndex);
		if (MyTeamAlive > 0)
		{
			if (ViewerPS->Team != TargetPS->Team)
			{
				return false;
			}
		}
	}
	const AController* TargetPC = Cast<AController>(TargetPS->GetOwner());
	const APawn* P = TargetPC ? TargetPC->GetPawn() : nullptr;
	const AUTCharacter* C = Cast<AUTCharacter>(P);
	const bool bTargetAlive = P && (!C || !C->IsDead());
	if (!bTargetAlive && bRoundInProgress)
	{
		return false;
	}
	return Super::CanSpectate_Implementation(Viewer, ViewTarget);
}

AUTPlayerState* AUTeamArenaGame::FindAliveOnTeamPS(int32 TeamIndex) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		AUTPlayerState* PS = PC ? Cast<AUTPlayerState>(PC->PlayerState) : nullptr;
		if (!PS || !PS->Team || PS->Team->TeamIndex != TeamIndex || PS->bOnlySpectator) continue;
		APawn* P = PC->GetPawn();
		const AUTCharacter* C = Cast<AUTCharacter>(P);
		if (P && (!C || !C->IsDead()))
		{
			return PS;
		}
	}
	return nullptr;
}

AUTPlayerState* AUTeamArenaGame::FindAnyOnTeamPS(int32 TeamIndex) const
{
	if (const AUTGameState* GS = GetWorld()->GetGameState<AUTGameState>())
	{
		for (APlayerState* APS : GS->PlayerArray)
		{
			if (AUTPlayerState* UTPS = Cast<AUTPlayerState>(APS))
			{
				if (UTPS->Team && UTPS->Team->TeamIndex == TeamIndex)
				{
					return UTPS;
				}
			}
		}
	}
	return nullptr;
}

bool AUTeamArenaGame::GetAliveCounts(int32& OutAliveTeam0, int32& OutAliveTeam1) const
{
	OutAliveTeam0 = 0;
	OutAliveTeam1 = 0;
	int32 TotalPlayerStates = 0;
	int32 ValidCharacters = 0;
	int32 DeadOrNoPawn = 0;
	int32 NoTeam = 0;
	int32 Spectators = 0;
	int32 Inactive = 0;
	AURArenaGameState* GS = GetGameState<AURArenaGameState>();
	if (!GS)
	{
		UE_LOG(LogGameMode, Error, TEXT("GetAliveCounts: Could not find GameState!"));
		return false;
	}
	TotalPlayerStates = GS->PlayerArray.Num();
	for (APlayerState* PSBase : GS->PlayerArray)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(PSBase);
		if (!PS) continue;
		if (PS->bOnlySpectator)
		{
			Spectators++;
			continue;
		}
		if (PS->bIsInactive)
		{
			Inactive++;
			continue;
		}
		if (!PS->Team)
		{
			NoTeam++;
			continue;
		}
		AUTCharacter* Pawn = Cast<AUTCharacter>(PS->GetUTCharacter());
		if (!Pawn || Pawn->IsDead() || Pawn->Health <= 0)
		{
			DeadOrNoPawn++;
			continue;
		}
		ValidCharacters++;
		const int32 TeamIndex = PS->Team->TeamIndex;
		if (TeamIndex == 0)
		{
			OutAliveTeam0++;
		}
		else if (TeamIndex == 1)
		{
			OutAliveTeam1++;
		}
	}
	return true;
}

void AUTeamArenaGame::CheckLastManStanding(int32 Alive0, int32 Alive1)
{
	if (Team0StartingSize > 1 && Alive0 == 1 && !bTeam0LastManAnnounced)
	{
		BroadcastLastManStanding(0);
		bTeam0LastManAnnounced = true;
	}
	if (Team1StartingSize > 1 && Alive1 == 1 && !bTeam1LastManAnnounced)
	{
		BroadcastLastManStanding(1);
		bTeam1LastManAnnounced = true;
	}
}

void AUTeamArenaGame::BroadcastLastManStanding(int32 LastManTeamIndex)
{
	if (!LastManStandingSound && !EnemyLastManStandingSound)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AUTPlayerController* PC = Cast<AUTPlayerController>(It->Get());
		if (!PC) continue;
		AUTPlayerState* PS = Cast<AUTPlayerState>(PC->PlayerState);
		if (!PS || !PS->Team) continue;
		if (PS->Team->TeamIndex == LastManTeamIndex)
		{
			if (LastManStandingSound)
			{
				PC->UTClientPlaySound(LastManStandingSound);
			}
		}
		else
		{
			if (EnemyLastManStandingSound)
			{
				PC->UTClientPlaySound(EnemyLastManStandingSound);
			}
		}
	}
	UE_LOG(LogGameMode, Log, TEXT("Last Man Standing: Team %d has only one player remaining"), LastManTeamIndex);
}

void AUTeamArenaGame::CheckRoundWinConditions()
{
	if (bWarmupMode) return;
	if (!bRoundInProgress || GetWorld()->GetTimeSeconds() < WinCheckHoldUntilSeconds) return;
	if (GetWorldTimerManager().IsTimerActive(TH_RoundEndDelay))
	{
		return;
	}
	int32 Alive0, Alive1;
	GetAliveCounts(Alive0, Alive1);
	const bool Team0Eliminated = (Alive0 == 0);
	const bool Team1Eliminated = (Alive1 == 0);
	if (Team0Eliminated && !Team1Eliminated)
	{
		FTimerDelegate TimerDelegate;
		TimerDelegate.BindUFunction(this, FName("DelayedEndRound"), 1, FName(TEXT("Elimination")));
		GetWorldTimerManager().SetTimer(TH_RoundEndDelay, TimerDelegate, 1.0f, false);
	}
	else if (Team1Eliminated && !Team0Eliminated)
	{
		FTimerDelegate TimerDelegate;
		TimerDelegate.BindUFunction(this, FName("DelayedEndRound"), 0, FName(TEXT("Elimination")));
		GetWorldTimerManager().SetTimer(TH_RoundEndDelay, TimerDelegate, 1.0f, false);
	}
	else if (Team0Eliminated && Team1Eliminated)
	{
		FTimerDelegate TimerDelegate;
		TimerDelegate.BindUFunction(this, FName("DelayedEndRound"), INDEX_NONE, FName(TEXT("Draw")));
		GetWorldTimerManager().SetTimer(TH_RoundEndDelay, TimerDelegate, 1.0f, false);
	}
}

void AUTeamArenaGame::BroadcastDomination(int32 DominatingTeamIndex)
{
	const int32 MessageIndex = (DominatingTeamIndex == 0) ? 3 : 4;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AUTPlayerController* PC = Cast<AUTPlayerController>(It->Get()))
		{
			PC->ClientReceiveLocalizedMessage(UUTTeamArenaVictoryMessage::StaticClass(), MessageIndex, nullptr, nullptr, nullptr);
		}
	}
}

void AUTeamArenaGame::BroadcastTakesLead(int32 LeadingTeamIndex)
{
	const int32 MessageIndex = (LeadingTeamIndex == 0) ? 5 : 6;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AUTPlayerController* PC = Cast<AUTPlayerController>(It->Get()))
		{
			PC->ClientReceiveLocalizedMessage(UUTTeamArenaVictoryMessage::StaticClass(), MessageIndex, nullptr, nullptr, nullptr);
		}
	}
}

void AUTeamArenaGame::BroadcastRoundResults(int32 WinnerTeamIndex, bool bIsDraw)
{
	int32 MessageIndex;
	if (bIsDraw)
	{
		MessageIndex = 2;
	}
	else if (WinnerTeamIndex == 0)
	{
		MessageIndex = 0;
	}
	else
	{
		MessageIndex = 1;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AUTPlayerController* PC = Cast<AUTPlayerController>(It->Get()))
		{
			PC->ClientReceiveLocalizedMessage(UUTTeamArenaVictoryMessage::StaticClass(), MessageIndex, nullptr, nullptr, nullptr);
		}
	}
}

void AUTeamArenaGame::CheckRoundAchievements(int32 WinnerTeamIndex, FName Reason)
{
	if (WinnerTeamIndex == INDEX_NONE || Reason != FName(TEXT("Elimination")))
	{
		return;
	}
	CheckForACE(WinnerTeamIndex);
	CheckForDarkHorse(WinnerTeamIndex);
	CheckForHighDamageCarry(WinnerTeamIndex);
}

void AUTeamArenaGame::CheckForACE(int32 WinnerTeamIndex)
{
	int32 EnemyTeamIndex = (WinnerTeamIndex == 0) ? 1 : 0;
	int32 EnemyTeamSize = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>((*It)->PlayerState);
		if (PS && PS->Team && PS->Team->TeamIndex == EnemyTeamIndex && !PS->bOnlySpectator)
		{
			EnemyTeamSize++;
		}
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>((*It)->PlayerState);
		UE_LOG(LogGameMode, Log, TEXT("checking ACE for %s"), *PS->PlayerName);
		if (PS && PS->Team && PS->Team->TeamIndex == WinnerTeamIndex && PS->RoundKills >= EnemyTeamSize && EnemyTeamSize >= 2)
		{
			RecordACE(PS);
			break;
		}
	}
}

void AUTeamArenaGame::CheckForDarkHorse(int32 WinnerTeamIndex)
{
	int32 AliveWinners = CountAliveOnTeam(WinnerTeamIndex);
	if (AliveWinners == 1)
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			AUTPlayerState* PS = Cast<AUTPlayerState>((*It)->PlayerState);
			if (PS && PS->Team && PS->Team->TeamIndex == WinnerTeamIndex)
			{
				AUTCharacter* Character = PS->GetUTCharacter();
				if (Character && !Character->IsDead() && PS->RoundKills >= 2)
				{
					RecordDarkHorse(PS, PS->RoundKills);
					break;
				}
			}
		}
	}
}

void AUTeamArenaGame::CheckForHighDamageCarry(int32 WinnerTeamIndex)
{
	float TeamTotalDamage = (WinnerTeamIndex == 0) ? Team0RoundDamage : Team1RoundDamage;
	if (TeamTotalDamage > 0)
	{
		for (auto& DamagePair : PlayerRoundDamage)
		{
			AUTPlayerState* PS = DamagePair.Key;
			float PlayerDamage = DamagePair.Value;
			if (PS && PS->Team && PS->Team->TeamIndex == WinnerTeamIndex)
			{
				float DamagePercentage = (PlayerDamage / TeamTotalDamage) * 100.0f;
				if (DamagePercentage >= 75.0f)
				{
					RecordHighDamageCarry(PS, DamagePercentage);
				}
			}
		}
	}
}

void AUTeamArenaGame::RecordACE(AUTPlayerState* PlayerState)
{
	if (!PlayerState) return;
	UE_LOG(LogGameMode, Log, TEXT("ACE Achievement: %s"), *PlayerState->PlayerName);
	OnPlayerACE.Broadcast(PlayerState);
}

void AUTeamArenaGame::RecordDarkHorse(AUTPlayerState* PlayerState, int32 EnemiesKilled)
{
	if (!PlayerState) return;
	UE_LOG(LogGameMode, Log, TEXT("Dark Horse Achievement: %s (1v%d)"), *PlayerState->PlayerName, EnemiesKilled);
	OnPlayerDarkHorse.Broadcast(PlayerState, EnemiesKilled);
}

void AUTeamArenaGame::RecordHighDamageCarry(AUTPlayerState* PlayerState, float DamagePercentage)
{
	if (!PlayerState) return;
	UE_LOG(LogGameMode, Log, TEXT("High Damage Carry Achievement: %s (%.1f%%)"), *PlayerState->PlayerName, DamagePercentage);
	OnPlayerHighDamageCarry.Broadcast(PlayerState, DamagePercentage);
}

void AUTeamArenaGame::ScoreDamage_Implementation(int32 DamageAmount, AUTPlayerState* Victim, AUTPlayerState* Attacker)
{
	Super::ScoreDamage_Implementation(DamageAmount, Victim, Attacker);
	if (bRoundInProgress && Attacker && Attacker->Team && DamageAmount > 0)
	{
		if (!PlayerRoundDamage.Contains(Attacker))
		{
			PlayerRoundDamage.Add(Attacker, 0.0f);
		}
		PlayerRoundDamage[Attacker] += DamageAmount;
		if (Attacker->Team->TeamIndex == 0)
		{
			Team0RoundDamage += DamageAmount;
		}
		else if (Attacker->Team->TeamIndex == 1)
		{
			Team1RoundDamage += DamageAmount;
		}
	}
}

bool AUTeamArenaGame::ModifyDamage_Implementation(int32& Damage, FVector& Momentum, APawn* Injured, AController* InstigatedBy, const FHitResult& HitInfo, AActor* DamageCauser, TSubclassOf<UDamageType> DamageType)
{
	// Check for invulnerability during intermission
	if (GetMatchState() == MatchState::MatchIntermission && Injured && Injured->PlayerState)
	{
		AUTPlayerState* InjuredPS = Cast<AUTPlayerState>(Injured->PlayerState);
		if (InjuredPS && InjuredPS->Team && InjuredPS->Team->TeamIndex == LastRoundWinningTeamIndex)
		{
			Damage = 0;
			Momentum = FVector::ZeroVector;
			return true; // Damage modified (to zero)
		}
	}

	// Apply standard team damage reduction
	if (InstigatedBy != NULL && Injured && InstigatedBy != Injured->Controller && Cast<AUTGameState>(GameState)->OnSameTeam(Injured, InstigatedBy))
	{
		Damage *= TeamDamagePct;
		Momentum *= TeamMomentumPct;
		AUTCharacter* InjuredChar = Cast<AUTCharacter>(Injured);
		if (InjuredChar && InjuredChar->bApplyWallSlide)
		{
			Momentum *= WallRunMomentumPct;
		}
		AUTPlayerController* InstigatorPC = Cast<AUTPlayerController>(InstigatedBy);
		if (InstigatorPC && Cast<AUTPlayerState>(Injured->PlayerState))
		{
			((AUTPlayerState*)(Injured->PlayerState))->AnnounceSameTeam(InstigatorPC);
		}
	}

	// Call parent *after* our logic
	return Super::ModifyDamage_Implementation(Damage, Momentum, Injured, InstigatedBy, HitInfo, DamageCauser, DamageType);
}

int32 AUTeamArenaGame::GetTiebreakWinnerByTeamHealth() const
{
	float Sum0 = 0.f, Sum1 = 0.f;
	for (TActorIterator<AUTCharacter> It(GetWorld()); It; ++It)
	{
		AUTCharacter* C = *It;
		if (!C || C->IsDead()) continue;
		if (AUTPlayerState* PS = Cast<AUTPlayerState>(C->PlayerState))
		{
			if (PS->Team)
			{
				const int32 T = PS->Team->TeamIndex % 2;
				const float Health = FMath::Max(0.f, (float)C->Health);
				const float Armor = C->GetArmorAmount();
				(T == 0 ? Sum0 : Sum1) += (Health + Armor);
			}
		}
	}
	if (Sum0 > Sum1) return 0;
	if (Sum1 > Sum0) return 1;
	return INDEX_NONE;
}

void AUTeamArenaGame::DelayedInitialWinCheck()
{
	if (bRoundInProgress)
	{
		WinCheckHoldUntilSeconds = 0.f;
		UE_LOG(LogGameMode, Warning, TEXT("Performing delayed initial win check"));
		CheckRoundWinConditions();
	}
}

void AUTeamArenaGame::StopOvertime()
{
	if (OvertimeTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(OvertimeTimerHandle);
	}
	if (OvertimeWaveTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(OvertimeWaveTimerHandle);
	}
	CurrentOvertimeWave = 0;
	CurrentWaveDamage = 0.0f;
}

void AUTeamArenaGame::StartOvertime()
{
	if (!bOvertimeEnabled || !bRoundInProgress) return;
	CurrentOvertimeWave = 0;
	CurrentWaveDamage = OvertimeBaseDamage;
	if (OvertimeStartDelay >= 3.0f)
	{
		float CountdownStart = OvertimeStartDelay - 3.0f;
		GetWorldTimerManager().SetTimer(
			OvertimeCountdownTimerHandle,
			[this]() { BroadcastOvertimeCountdown(3); },
			CountdownStart, false
		);
	}
	GetWorldTimerManager().SetTimer(
		OvertimeWaveTimerHandle,
		this,
		&AUTeamArenaGame::ExecuteOvertimeWave,
		FMath::Max(0.1f, OvertimeStartDelay),
		false
	);
	BP_OnOvertimeStarted();
	UE_LOG(LogGameMode, Warning, TEXT("Overtime started! First wave in %.1f seconds with %.1f damage"),
		OvertimeStartDelay, OvertimeBaseDamage);
}

void AUTeamArenaGame::BroadcastOvertimeCountdown(int32 CountdownValue)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AUTPlayerController* PC = Cast<AUTPlayerController>(It->Get()))
		{
			PC->ClientReceiveLocalizedMessage(UUTCountDownMessage::StaticClass(), CountdownValue);
		}
	}
	if (CountdownValue > 1)
	{
		GetWorldTimerManager().SetTimer(
			OvertimeCountdownTimerHandle,
			[this, CountdownValue]() { BroadcastOvertimeCountdown(CountdownValue - 1); },
			1.0f, false
		);
	}
	else if (CountdownValue == 1)
	{
		GetWorldTimerManager().SetTimer(
			OvertimeCountdownTimerHandle,
			[this]() { BroadcastOvertimeAnnouncement(); },
			1.0f, false
		);
	}
}

void AUTeamArenaGame::BroadcastOvertimeAnnouncement()
{
	if (!OvertimeAnnouncementSound) return;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AUTPlayerController* PC = Cast<AUTPlayerController>(It->Get()))
		{
			PC->UTClientPlaySound(OvertimeAnnouncementSound);
		}
	}
	UE_LOG(LogGameMode, Warning, TEXT("Overtime announcement sound played to all players"));
}

void AUTeamArenaGame::ExecuteOvertimeWave()
{
	if (GetNetMode() == NM_Client || !bRoundInProgress)
	{
		StopOvertime();
		return;
	}
	CurrentOvertimeWave++;
	if (CurrentOvertimeWave == 1)
	{
		CurrentWaveDamage = OvertimeBaseDamage;
	}
	else
	{
		CurrentWaveDamage *= OvertimeDamageMultiplier;
	}
	if (OvertimeMaxDamage > 0.0f)
	{
		CurrentWaveDamage = FMath::Min(CurrentWaveDamage, OvertimeMaxDamage);
	}
	UE_LOG(LogGameMode, Warning, TEXT("Overtime Wave %d: %.1f damage to %d players"),
		CurrentOvertimeWave, CurrentWaveDamage, GetWorld()->GetNumPawns());
	BP_OnOvertimeWave(CurrentWaveDamage, CurrentOvertimeWave);
	int32 DamageCount = 0;
	for (FConstPawnIterator It = GetWorld()->GetPawnIterator(); It; ++It)
	{
		if (!bRoundInProgress)
		{
			UE_LOG(LogGameMode, Warning, TEXT("Round ended during overtime wave - stopping damage application"));
			break;
		}
		AUTCharacter* C = Cast<AUTCharacter>(*It);
		if (!C || C->IsDead()) continue;
		float DamageToApply = CurrentWaveDamage;
		if (bOvertimeNonLethal && C->Health <= 1.f) continue;
		if (bOvertimeNonLethal && (C->Health - DamageToApply) < 1.f)
		{
			DamageToApply = FMath::Max(0.f, C->Health - 1.f);
			if (DamageToApply <= 0.f) continue;
		}
		UE_LOG(LogGameMode, Log, TEXT("Applying %.1f damage to %s (Health: %d, Armor: %.1f)"),
			DamageToApply, *C->GetName(), C->Health, C->GetArmorAmount());
		bool bWillKillPlayer = (C->Health - DamageToApply) <= 0;
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.Location = C->GetActorLocation();
		Hit.ImpactPoint = C->GetActorLocation();
		Hit.Normal = FVector(0, 0, 1);
		Hit.ImpactNormal = FVector(0, 0, 1);
		Hit.Actor = C;
		Hit.Component = Cast<UPrimitiveComponent>(C->GetRootComponent());
		FUTPointDamageEvent DamageEvent(
			DamageToApply,
			Hit,
			FVector(0, 0, -1),
			OvertimeDamageType ? *OvertimeDamageType : UUTDamageType::StaticClass(),
			FVector::ZeroVector
		);
		C->TakeDamage(
			DamageToApply,
			DamageEvent,
			nullptr,
			this
		);
		DamageCount++;
		if (bWillKillPlayer && bRoundInProgress)
		{
			//GetWorldTimerManager().SetTimerForNextTick([this]()
			GetWorldTimerManager().SetTimerForNextTick(this, &AUTeamArenaGame::DeferredCheckRoundWinConditions);

		}
	}
	UE_LOG(LogGameMode, Warning, TEXT("Applied damage to %d players"), DamageCount);
	CheckRoundWinConditions();
	if (bRoundInProgress)
	{
		GetWorldTimerManager().SetTimer(
			OvertimeWaveTimerHandle,
			this,
			&AUTeamArenaGame::ExecuteOvertimeWave,
			OvertimeWaveInterval,
			false
		);
	}
}

void AUTeamArenaGame::CheckForDominationAndLead(int32 WinnerTeamIndex)
{
	if (WinnerTeamIndex == INDEX_NONE || !Teams.IsValidIndex(0) || !Teams.IsValidIndex(1))
	{
		return;
	}
	const int32 RedScore = Teams[0]->Score;
	const int32 BlueScore = Teams[1]->Score;
	const int32 ScoreDifference = FMath::Abs(RedScore - BlueScore);
	const bool bWasTied = (PreviousRedScore == PreviousBlueScore);
	const bool bNowHasLead = (RedScore != BlueScore);
	if (bWasTied && bNowHasLead)
	{
		BroadcastTakesLead(WinnerTeamIndex);
	}
	else if (!bWasTied && bNowHasLead)
	{
		const bool bRedWasWinning = (PreviousRedScore > PreviousBlueScore);
		const bool bBlueWasWinning = (PreviousBlueScore > PreviousRedScore);
		const bool bRedNowWinning = (RedScore > BlueScore);
		const bool bBlueNowWinning = (BlueScore > RedScore);
		if ((bBlueWasWinning && bRedNowWinning) || (bRedWasWinning && bBlueNowWinning))
		{
			BroadcastTakesLead(WinnerTeamIndex);
		}
	}
	if (ScoreDifference >= 5)
	{
		const int32 DominatingTeam = (RedScore > BlueScore) ? 0 : 1;
		if (!bHasBroadcastTeamDominating)
		{
			BroadcastDomination(DominatingTeam);
			bHasBroadcastTeamDominating = true;
		}
		else
		{
			const int32 PreviousDominatingTeam = (PreviousRedScore > PreviousBlueScore) ? 0 : 1;
			if (DominatingTeam != PreviousDominatingTeam)
			{
				BroadcastDomination(DominatingTeam);
			}
		}
	}
	else
	{
		bHasBroadcastTeamDominating = false;
	}
	PreviousRedScore = RedScore;
	PreviousBlueScore = BlueScore;
}


void AUTeamArenaGame::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogGameMode, Log, TEXT("AUTeamArenaGame::EndPlay called. Clearing all active timers for this object."));

	// This is the graceful shutdown you were missing.
	// It tells the World's TimerManager to find and remove *all* timers
	// that are currently bound to this GameMode object.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}

	// Always call the parent implementation
	Super::EndPlay(EndPlayReason);
}


#pragma endregion
