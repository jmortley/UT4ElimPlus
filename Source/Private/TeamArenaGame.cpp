#include "TeamArena.h"
#include "UnrealTournament.h"
#include "TeamArenaGame.h"
//#include "GSInterface.h"
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
	ScoreLimit = 10;
	RoundTimeSeconds = 90;
	RoundStartDelaySeconds = 5.f; // This is now the "AwardDisplayTime"
	bAllowRespawnMidRound = false;
	bRoundInProgress = false;
	bAllowPlayerRespawns = false;
	LastRoundWinningTeamIndex = INDEX_NONE;
	AwardDisplayTime = 5.f; // Use this for the post-round delay
	PreRoundCountdown = 3.f; // Use this for the pre-spawn countdown
	//SpawnProtectionTime = 3.f;
	bWarmupMode = false;
	SpectateDelay = 2.f;
	TotalRoundsPlayed = 0;
	HighDamageCarryThreshold = 75.0f;
	//bCasterControl = true;
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
	//TimeLimit = 20;
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

	//Spawn weighting system with default values
	SpawnDistanceWeight = 0.30f;
	SpawnHeightWeight = 0.10f;
	SpawnUsageWeight = 0.45f;
	SpawnSeparationWeight = 0.15f;

	// OvertimeDamageType = UDamageType::StaticClass(); // Already set above
	//GameStateClass = AUTGameState::StaticClass();
}


void AUTeamArenaGame::BP_SetMatchState_RoundCooldown()
{
	if (HasAuthority())
	{
		SetMatchState(FName(TEXT("RoundCooldown")));
	}
}

void AUTeamArenaGame::BP_SetMatchState_Intermission()
{
	if (HasAuthority())
	{
		SetMatchState(FName(TEXT("Intermission")));
	}
}

void AUTeamArenaGame::BP_SetMatchState_InProgress()
{
	if (HasAuthority())
	{
		SetMatchState(MatchState::InProgress);
	}
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

	if (!bSpawnPointsInitialized)
	{
		UE_LOG(LogGameMode, Warning, TEXT("Force initializing spawn system in BeginPlay"));
		InitializeSpawnPointSystem();
		bSpawnPointsInitialized = true;
	}
	//GetWorldTimerManager().SetTimerForNextTick([this]()
	GetWorldTimerManager().SetTimerForNextTick(this, &AUTeamArenaGame::DeferredHandleMatchStart);
	UE_LOG(LogTemp, Error, TEXT("GameMode::BeginPlay called"));
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

	AUTGameState* GS = GetGameState<AUTGameState>();
	if (GS == nullptr)
	{
		UE_LOG(LogGameMode, Error, TEXT("HandleMatchHasStarted: AURArenaGameState is NULL. This is normal in PIE if World Settings are not set. Deferring."));

		// Try again next tick.
		GetWorldTimerManager().SetTimerForNextTick(this, &AUTeamArenaGame::HandleMatchHasStarted);
		return;
	}

	Super::HandleMatchHasStarted();
	bWarmupMode = false;
	// This is the true start of the match.
	// We start in intermission to prepare for Round 1.
	/*
	if (!GS->bMatchHasStarted)
	{
		UE_LOG(LogGameMode, Log, TEXT("HandleMatchHasStarted: Match is starting. Calling StartIntermission for the first round."));
		GS->bMatchHasStarted = true;
		StartIntermission(PreRoundCountdown);
	}
	*/
}


void AUTeamArenaGame::CallMatchStateChangeNotify()
{
	UE_LOG(LogGameMode, Log, TEXT("Current matchstate: %s"), *GetMatchState().ToString());
	// This function intercepts all SetMatchState calls
	// and routes them to our custom handlers.
	if (GetMatchState() == MatchState::WaitingToStart)
	{
		bWarmupMode = true;

	}
	else if (GetMatchState() == MatchState::PlayerIntro)
	{
		// Ensure warmup mode is disabled once match is starting
		bWarmupMode = false;
	}
	if (GetMatchState() == MatchState::MatchIntermission || GetMatchState() == FName(TEXT("RoundCooldown")))
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
	if (GetGameState<AUTGameState>())
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
	
	if (IsPendingKill() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
		return;



	AUTGameState* GS = GetWorld()->GetGameState<AUTGameState>();
	if (GS == nullptr || GS->IsPendingKill() || GetWorld()->bIsTearingDown) return; // Not ready yet

	//Because we don't call super defaulttimer except when roundinprogress. Need to do server management work here
	HandleServerManagement();


	// --- Intermission Logic-- -
	if (GS->GetMatchState() == MatchState::MatchIntermission || GetMatchState() == FName(TEXT("RoundCooldown")))
	{
		if (IntermissionSecondsRemaining > 0)
		{
			--IntermissionSecondsRemaining;

			BP_OnSetIntermission(/*bInIntermission*/true, IntermissionSecondsRemaining);
			// Broadcast 3, 2, 1
			if (IntermissionSecondsRemaining > 0 && IntermissionSecondsRemaining <= 3)
			{
				BroadcastLocalized(this, UUTCountDownMessage::StaticClass(), IntermissionSecondsRemaining, nullptr, nullptr, nullptr);
			}
		}

		else // Intermission timer hit zero
		{
			// Notify BP that intermission is over (optional)
			BP_OnSetIntermission(/*bInIntermission*/false, IntermissionSecondsRemaining);
			CleanupWorldForNewRound();
			SetMatchState(MatchState::InProgress);
			UE_LOG(LogGameMode, Log, TEXT("DefaultTimer: Intermission complete. Cleaning world and setting state to InProgress."));


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

			/*if (GS->GetClass()->ImplementsInterface(URoundStateProvider::StaticClass()))
				
					IRoundStateProvider::Execute_Arena_SetRound(
						GS,
						//bInProgress true,
						//RoundRemain RoundRemain,
						//LastWinnerTeamIndex INDEX_NONE  // or cached Winner if you track it
					);
			*/
			BP_OnSetRound(true, RoundRemain, LastRoundWinningTeamIndex);
			

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
		Super::DefaultTimer();
	}
}




void AUTeamArenaGame::HandleServerManagement()
{
	// 1. Instance cleanup logic
	HandleInstanceCleanup();

	// 2. Map voting logic
	HandleMapVoting();

	// 3. Bot management (from UTGameMode::DefaultTimer)
	CheckBotCount();

	// 4. REMOVED: Force respawn logic - not needed for round-based gameplay
	// Your spawn control in RestartPlayer() already prevents unwanted spawning
}

// NEW: Handle map voting logic (from Epic's UTGameMode::DefaultTimer)
void AUTeamArenaGame::HandleMapVoting()
{
	if (MatchState == MatchState::MapVoteHappening)
	{
		if (GetWorld()->GetNetMode() != NM_Standalone)
		{
			UTGameState->VoteTimer--;
			if (UTGameState->VoteTimer <= 0)
			{
				UTGameState->VoteTimer = 0;
			}
		}

		// Scan the maps and see if we have a winner
		TArray<AUTReplicatedMapInfo*> Best;
		for (int32 i = 0; i < UTGameState->MapVoteList.Num(); i++)
		{
			if (UTGameState->MapVoteList[i]->VoteCount > 0)
			{
				if (Best.Num() == 0 || Best[0]->VoteCount < UTGameState->MapVoteList[i]->VoteCount)
				{
					Best.Empty();
					Best.Add(UTGameState->MapVoteList[i]);
				}
			}
		}

		if (Best.Num() > 0)
		{
			int32 Target = int32(float(GetNumPlayers()) * 0.5);
			if (Best[0]->VoteCount > Target)
			{
				TallyMapVotes();
			}
		}
	}
}


void AUTeamArenaGame::HandleInstanceCleanup()
{
	// 1. Hub instance cleanup
	if (IsGameInstanceServer() && LobbyBeacon)
	{
		// Update lobby stats periodically
		if (GetWorld()->GetTimeSeconds() - LastLobbyUpdateTime >= 10.0f)
		{
			UpdateLobbyMatchStats();
		}

		if (!bDedicatedInstance)
		{
			// Empty server timeout
			if (!HasMatchStarted())
			{
				if (GetWorld()->GetRealTimeSeconds() > LobbyInitialTimeoutTime && NumPlayers <= 0 &&
					(GetNetDriver() == NULL || GetNetDriver()->ClientConnections.Num() == 0))
				{
					UE_LOG(LogGameMode, Warning, TEXT("Instance timeout - shutting down"));
					ShutdownGameInstance();
					return;
				}
			}
			else
			{
				if (NumPlayers <= 0)
				{
					UE_LOG(LogGameMode, Warning, TEXT("Instance empty - shutting down"));
					ShutdownGameInstance();
					return;
				}
			}

			// Idle player check
			if (!bIgnoreIdlePlayers && UTGameState && UTGameState->IsMatchInProgress())
			{
				bool bAllPlayersAreIdle = true;
				for (int32 i = 0; i < UTGameState->PlayerArray.Num(); i++)
				{
					AUTPlayerState* UTPlayerState = Cast<AUTPlayerState>(UTGameState->PlayerArray[i]);
					if (UTPlayerState && !IsPlayerIdle(UTPlayerState))
					{
						bAllPlayersAreIdle = false;
						break;
					}
				}

				if (bAllPlayersAreIdle)
				{
					UE_LOG(LogGameMode, Warning, TEXT("All players idle - shutting down"));
					ShutdownGameInstance();
					return;
				}
			}
		}
	}
	else
	{
		// 2. Non-hub server cleanup
		if (NumPlayers <= 0 && NumSpectators <= 0 && HasMatchStarted())
		{
			EmptyServerTime++;
			if (EmptyServerTime >= AutoRestartTime)
			{
				UE_LOG(LogGameMode, Warning, TEXT("Empty server timeout - restarting"));
				TravelToNextMap();
				return;
			}
		}
		else
		{
			EmptyServerTime = 0;
		}
	}

	// 3. Hub beacon connection monitoring
	if (LobbyBeacon && LobbyBeacon->GetNetConnection()->State == EConnectionState::USOCK_Closed)
	{
		if (!bDedicatedInstance && NumPlayers <= 0 && MatchState != MatchState::WaitingToStart)
		{
			UE_LOG(LogGameMode, Warning, TEXT("Lost hub connection and server empty - shutting down"));
			FPlatformMisc::RequestExit(false);
			return;
		}

		// Try to reconnect
		RecreateLobbyBeacon();
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
	int32 WinnerIdx = LastRoundWinningTeamIndex;

	ForceLosersToViewWinners(WinnerIdx);
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
	if (AUTGameState* GS = GetGameState<AUTGameState>())
	{
		BP_OnSetIntermission(true, IntermissionSecondsRemaining);

		// Optionally push a network update to clients anyway
		GS->ForceNetUpdate();
	}
	else
	{
		UE_LOG(LogGameMode, Warning, TEXT("StartIntermission: GameState is NULL. State will not be replicated."));
	}

	// This is the key: Set the match state.
	// This will trigger CallMatchStateChangeNotify -> HandleMatchIntermission
	SetMatchState(FName(TEXT("RoundCooldown")));//MatchIntermission);
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

	if (AUTGameState* GS = GetWorld()->GetGameState<AUTGameState>())
	{
		
		BP_OnSetRound(true, RoundTimeSeconds, LastRoundWinningTeamIndex);
		BP_OnSetIntermission(false, 0);
		// Still fine to force a net update on the base GameState
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

	//UE_LOG(LogGameMode, Warning, TEXT("New round started. Manually attempted to spawn %d players."), PlayersSpawned);
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
	
	// Increment total rounds counter (includes wins and draws)
	TotalRoundsPlayed++;

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
	if (AUTGameState* GS = GetGameState<AUTGameState>())
	{
		BP_OnSetRound(false, 0, LastRoundWinningTeamIndex);
		// Replication push — still valid on the base class
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
			PS->RoundDamageDone = 0;
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
	for (FConstPawnIterator It = GetWorld()->GetPawnIterator(); It; ++It)
	{
		if (AUTCharacter* UTC = Cast<AUTCharacter>(It->Get()))
		{
			if (UTC->IsDead() && !UTC->IsPendingKill())
			{
				UTC->Destroy();
			}
		}
	}
}


void AUTeamArenaGame::RestartPlayer(AController* NewPlayer)
{
	// Always log the attempt
	//UE_LOG(LogGameMode, Warning, TEXT("RestartPlayer called for %s, bRoundInProgress=%s"),
		//NewPlayer ? *NewPlayer->GetName() : TEXT("NULL"),
		//bRoundInProgress ? TEXT("true") : TEXT("false"));
	if (!NewPlayer) return;

	if (APlayerController* PC = Cast<APlayerController>(NewPlayer))
	{
		if (MustSpectate(PC))
		{
			UE_LOG(LogGameMode, Verbose, TEXT("RestartPlayer: Skipping spectator-only player %s"),
				PC->PlayerState ? *PC->PlayerState->PlayerName : TEXT("Unknown"));
			return;
		}
	}

	// Skip if they already have a pawn (protect mid-equip)
	if (NewPlayer->GetPawn())
	{
		//UE_LOG(LogGameMode, Warning, TEXT("  Controller already has pawn; skipping respawn"));
		return;
	}
	//if (NewPlayer && NewPlayer->PlayerState)
	//{
		//UE_LOG(LogGameMode, Warning, TEXT("  PlayerState: %s"), *NewPlayer->PlayerState->GetName());
	//}
	AUTGameState* GS = GetGameState<AUTGameState>();

	// Check if the lineup system is active.
	// This check is from the base AUTGameMode::RestartPlayer
	bool bLineupIsActive = (GS && GS->ActiveLineUpHelper && GS->ActiveLineUpHelper->bIsPlacingPlayers);

	if (bLineupIsActive)
	{
		Super::RestartPlayer(NewPlayer);
		return;
	}
	// The new logic: Allow spawn if (it's for a lineup) OR (it's for your round)
	//const bool bShouldAllowSpawn = bLineupIsActive || bShouldAllowSpawnInRound;
	// ForceRespawnTime will still work during warmup and pre-match phases
	//bool hasStarted = HasMatchStarted();
	const bool bShouldAllowSpawn = (bAllowRespawnMidRound || bAllowPlayerRespawns || bWarmupMode || GetMatchState() == MatchState::WaitingToStart);

	//UE_LOG(LogGameMode, Warning, TEXT("  bAllowRespawnMidRound=%s, bAllowPlayerRespawns=%s, bShouldAllowSpawn=%s"),
		//bAllowRespawnMidRound ? TEXT("true") : TEXT("false"),
		//bAllowPlayerRespawns ? TEXT("true") : TEXT("false"),
		//bShouldAllowSpawn ? TEXT("true") : TEXT("false"));


	if (bShouldAllowSpawn)
	{
		// FIX: Call your ChoosePlayerStart and actually use the result
		AActor* ChosenStart = ChoosePlayerStart_Implementation(NewPlayer);
		UE_LOG(LogGameMode, Warning, TEXT("RestartPlayer: Chosen PlayerStart: %s"),
			ChosenStart ? *ChosenStart->GetName() : TEXT("NULL"));
		//bSetPlayerDefaultsNewSpawn = true;
		if (ChosenStart)
		{
			// Use RestartPlayerAtPlayerStart to ensure we use YOUR chosen spawn point
			//RestartPlayerAtPlayerStart(NewPlayer, ChosenStart);
			Super::RestartPlayer(NewPlayer);
		}
		/*else
		{
			// Fallback to base logic if no spawn found
			Super::RestartPlayer(NewPlayer);
		}*/

		if (NewPlayer && NewPlayer->GetPawn())
		{
			//return;
		}
		else
		{
			if (!bLineupIsActive || !bWarmupMode) {
				UE_LOG(LogGameMode, Warning, TEXT("RestartPlayer: FAILED to spawn pawn!"));
			}
		}
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
			//UE_LOG(LogGameMode, Warning, TEXT("ScoreKill: Round continues. Setting timer for DelayedForceSpectate in %.1f sec."), SpectateDelay);
			
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
		//UE_LOG(LogGameMode, Log, TEXT("DelayedForceSpectate: Round ended during delay. Aborting spectate."), *DeadPS->PlayerName);
		// EndRoundForTeam will handle their camera now
		return;
	}

	// --- ALL CHECKS PASSED ---
	//UE_LOG(LogGameMode, Log, TEXT("DelayedForceSpectate: Delay complete. Forcing %s to spectate."), *DeadPS->PlayerName);
	ForceTeamSpectate(DeadPS);
}



AActor* AUTeamArenaGame::ChoosePlayerStart_Implementation(AController* Player)
{


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
				//UE_LOG(LogGameMode, Log, TEXT("Using offset spawn location for %s (attempt %d)"), *PS->PlayerName, Attempt);
				break;
			}
		}
	}

	UE_LOG(LogGameMode, Log, TEXT("Selected spawn %s for team %d player %s"),
		*ChosenSpawn->GetName(), TeamIndex, *PS->PlayerName);

	return ChosenSpawn;
}


AActor* AUTeamArenaGame::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	// If we have an overridden player start (from RestartPlayer), use it
	if (OverriddenPlayerStart)
	{
		return OverriddenPlayerStart;
	}

	// Otherwise use normal logic
	return Super::FindPlayerStart_Implementation(Player, IncomingName);
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
	// NEW: Add randomization to prevent always picking the same pairs
	// Shuffle candidates to introduce variety
		for (int32 i = Candidates.Num() - 1; i > 0; i--)
		{
		int32 j = FMath::RandRange(0, i);
			if (i != j)
			{
				Candidates.Swap(i, j);
			}
		}
	APlayerStart* PrimarySpawn = nullptr;
	APlayerStart* SecondarySpawn = nullptr;

	// NEW: Use multiple selection strategies and rotate between them
	int32 SelectionStrategy = CurrentRoundNumber % 3; // Rotate between 3 strategies

	switch (SelectionStrategy)
	{
	case 0: // Distance-focused selection
		FindMaxDistanceSpawnPair(Candidates, EnemySpawns, PrimarySpawn, SecondarySpawn);
		break;

	case 1: // Usage-focused selection (favor least used)
		FindLeastUsedSpawnPair(Candidates, TeamIndex, PrimarySpawn, SecondarySpawn);
		break;

	case 2: // Balanced selection with random element
		FindBalancedRandomSpawnPair(Candidates, EnemySpawns, TeamIndex, PrimarySpawn, SecondarySpawn);
		break;
	}

	//FindMaxDistanceSpawnPair(Candidates, EnemySpawns, PrimarySpawn, SecondarySpawn);
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


/*
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

	// Normalize weights to ensure they sum to 1.0
	float TotalWeight = SpawnDistanceWeight + SpawnHeightWeight + SpawnUsageWeight + SpawnSeparationWeight;
	float NormalizedDistanceWeight = (TotalWeight > 0.0f) ? SpawnDistanceWeight / TotalWeight : 0.25f;
	float NormalizedHeightWeight = (TotalWeight > 0.0f) ? SpawnHeightWeight / TotalWeight : 0.25f;
	float NormalizedUsageWeight = (TotalWeight > 0.0f) ? SpawnUsageWeight / TotalWeight : 0.25f;
	float NormalizedSeparationWeight = (TotalWeight > 0.0f) ? SpawnSeparationWeight / TotalWeight : 0.25f;

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

			// CHANGE: Increase the weight of usage score to encourage more variety
			float UsageScore1 = 1.0f / (1.0f + CandidateSpawns[i]->GetUsageCountForTeam(0) + CandidateSpawns[i]->GetUsageCountForTeam(1));
			float UsageScore2 = 1.0f / (1.0f + CandidateSpawns[j]->GetUsageCountForTeam(0) + CandidateSpawns[j]->GetUsageCountForTeam(1));

			float SpawnSeparation = FVector::Dist(Spawn1->GetActorLocation(), Spawn2->GetActorLocation());
			float SeparationScore = FMath::Min(SpawnSeparation / 1000.0f, 1.0f);

			// CHANGE: Rebalance the weights to prioritize usage variety more
			// Old: Distance=40%, Height=30%, Usage=20%, Separation=10%
			// New: Distance=30%, Height=10%, Usage=45%, Separation=15%
			//float CombinedScore = (MinDist1 + MinDist2) * 0.3f + (HeightScore1 + HeightScore2) * 0.10f + (UsageScore1 + UsageScore2) * 0.45f + SeparationScore * 0.15f;

			float CombinedScore = (MinDist1 + MinDist2) * NormalizedDistanceWeight +
				(HeightScore1 + HeightScore2) * NormalizedHeightWeight +
				(UsageScore1 + UsageScore2) * NormalizedUsageWeight +
				SeparationScore * NormalizedSeparationWeight;

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
*/

// IMPROVED: Modified version of existing function with dynamic weights
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

	// NEW: Add some randomness to weights each round
	float RandomVariation = 0.1f; // 10% variation
	float CurrentDistanceWeight = SpawnDistanceWeight + FMath::FRandRange(-RandomVariation, RandomVariation);
	float CurrentHeightWeight = SpawnHeightWeight + FMath::FRandRange(-RandomVariation, RandomVariation);
	float CurrentUsageWeight = SpawnUsageWeight + FMath::FRandRange(-RandomVariation, RandomVariation);
	float CurrentSeparationWeight = SpawnSeparationWeight + FMath::FRandRange(-RandomVariation, RandomVariation);

	// Normalize weights
	float TotalWeight = CurrentDistanceWeight + CurrentHeightWeight + CurrentUsageWeight + CurrentSeparationWeight;
	CurrentDistanceWeight /= TotalWeight;
	CurrentHeightWeight /= TotalWeight;
	CurrentUsageWeight /= TotalWeight;
	CurrentSeparationWeight /= TotalWeight;

	float BestScore = -1.0f;
	// FIX: Use a simpler structure instead of nested TPair
	struct FSpawnPairScore
	{
		float Score;
		int32 Index1;
		int32 Index2;

		FSpawnPairScore(float InScore, int32 InIndex1, int32 InIndex2)
			: Score(InScore), Index1(InIndex1), Index2(InIndex2) {
		}
	};

	TArray<FSpawnPairScore> ScoredPairs;

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

			float CombinedScore = (MinDist1 + MinDist2) * CurrentDistanceWeight +
				(HeightScore1 + HeightScore2) * CurrentHeightWeight +
				(UsageScore1 + UsageScore2) * CurrentUsageWeight +
				SeparationScore * CurrentSeparationWeight;

			// Add small random factor to break ties
			CombinedScore += FMath::FRandRange(-0.01f, 0.01f);

			ScoredPairs.Add(FSpawnPairScore(CombinedScore, i, j));
		}
	}

	if (ScoredPairs.Num() > 0)
	{
		// Sort by score (descending)
		ScoredPairs.Sort([](const FSpawnPairScore& A, const FSpawnPairScore& B)
			{
				return A.Score > B.Score;
			});

		// NEW: Instead of always picking the best, randomly select from top 3 pairs
		int32 TopPairsToConsider = FMath::Min(3, ScoredPairs.Num());
		int32 SelectedPairIndex = FMath::RandRange(0, TopPairsToConsider - 1);

		const FSpawnPairScore& SelectedPair = ScoredPairs[SelectedPairIndex];

		APlayerStart* Spawn1 = CandidateSpawns[SelectedPair.Index1]->PlayerStart;
		APlayerStart* Spawn2 = CandidateSpawns[SelectedPair.Index2]->PlayerStart;

		float HeightScore1 = CandidateSpawns[SelectedPair.Index1]->HeightScore;
		float HeightScore2 = CandidateSpawns[SelectedPair.Index2]->HeightScore;

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


void AUTeamArenaGame::FindLeastUsedSpawnPair(const TArray<FSpawnPointData*>& CandidateSpawns, int32 TeamIndex, APlayerStart*& OutPrimary, APlayerStart*& OutSecondary)
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

	// Sort by usage count (ascending) with some randomization
	TArray<FSpawnPointData*> SortedCandidates = CandidateSpawns;
	SortedCandidates.Sort([TeamIndex](const FSpawnPointData& A, const FSpawnPointData& B)
		{
			int32 UsageA = A.GetUsageCountForTeam(TeamIndex);
			int32 UsageB = B.GetUsageCountForTeam(TeamIndex);

			// If usage counts are equal, randomize
			if (UsageA == UsageB)
			{
				return FMath::RandBool();
			}
			return UsageA < UsageB;
		});

	// Take the two least used spawns
	OutPrimary = SortedCandidates[0]->PlayerStart;
	if (SortedCandidates.Num() > 1)
	{
		OutSecondary = SortedCandidates[1]->PlayerStart;
	}
}

// NEW: Balanced selection with randomization
void AUTeamArenaGame::FindBalancedRandomSpawnPair(const TArray<FSpawnPointData*>& CandidateSpawns, const TArray<APlayerStart*>& EnemySpawns, int32 TeamIndex, APlayerStart*& OutPrimary, APlayerStart*& OutSecondary)
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

	// Create a weighted selection pool
	TArray<float> SpawnScores;
	SpawnScores.Reserve(CandidateSpawns.Num());

	// Calculate scores for all candidates
	for (const FSpawnPointData* SpawnData : CandidateSpawns)
	{
		if (!SpawnData || !SpawnData->PlayerStart) continue;

		float MinDist = CalculateMinDistanceToEnemySpawns(SpawnData->PlayerStart, EnemySpawns);
		float UsageScore = 1.0f / (1.0f + SpawnData->GetUsageCountForTeam(TeamIndex) + 1.0f); // +1 to avoid division issues
		float HeightScore = SpawnData->HeightScore;

		// Add some randomness to the score
		float RandomFactor = FMath::FRandRange(0.8f, 1.2f);

		float CombinedScore = (MinDist * 0.4f + UsageScore * 0.4f + HeightScore * 0.2f) * RandomFactor;
		SpawnScores.Add(CombinedScore);
	}

	// Select top candidates with some randomness
	int32 TopCandidatesCount = FMath::Min(6, CandidateSpawns.Num()); // Consider top 6 candidates

	// Sort indices by score (descending)
	TArray<int32> SortedIndices;
	for (int32 i = 0; i < CandidateSpawns.Num(); i++)
	{
		SortedIndices.Add(i);
	}

	SortedIndices.Sort([&SpawnScores](const int32& A, const int32& B)
		{
			return SpawnScores[A] > SpawnScores[B];
		});

	// Randomly select from top candidates
	int32 PrimaryIndex = SortedIndices[FMath::RandRange(0, FMath::Min(2, TopCandidatesCount - 1))]; // Top 3
	OutPrimary = CandidateSpawns[PrimaryIndex]->PlayerStart;

	// Select secondary from remaining top candidates
	if (TopCandidatesCount > 1)
	{
		TArray<int32> RemainingIndices;
		for (int32 i = 0; i < TopCandidatesCount; i++)
		{
			if (SortedIndices[i] != PrimaryIndex)
			{
				RemainingIndices.Add(SortedIndices[i]);
			}
		}

		if (RemainingIndices.Num() > 0)
		{
			int32 SecondaryIndex = RemainingIndices[FMath::RandRange(0, FMath::Min(2, RemainingIndices.Num() - 1))];
			OutSecondary = CandidateSpawns[SecondaryIndex]->PlayerStart;
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

/*
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
*/
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

// IMPROVED: Add randomization to candidate selection
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

	// NEW: Don't always sort the same way - add randomization
	int32 SortStrategy = CurrentRoundNumber % 2;

	if (SortStrategy == 0)
	{
		// Usage-first sorting (original)
		Candidates.Sort([TeamIndex](const FSpawnPointData& A, const FSpawnPointData& B)
			{
				int32 UsageA = A.GetUsageCountForTeam(TeamIndex);
				int32 UsageB = B.GetUsageCountForTeam(TeamIndex);
				if (UsageA != UsageB)
				{
					return UsageA < UsageB;
				}
				// Add randomness to break ties
				return FMath::RandBool();
			});
	}
	else
	{
		// Quality-first sorting with randomization
		Candidates.Sort([TeamIndex](const FSpawnPointData& A, const FSpawnPointData& B)
			{
				float ScoreA = A.HeightScore + FMath::Abs(A.TeamSideScore);
				float ScoreB = B.HeightScore + FMath::Abs(B.TeamSideScore);

				// If scores are very close, randomize
				if (FMath::Abs(ScoreA - ScoreB) < 0.1f)
				{
					return FMath::RandBool();
				}
				return ScoreA > ScoreB;
			});
	}

	return Candidates;
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
	//UE_LOG(LogGameMode, Log, TEXT("ResetSpawnSelectionForNewRound: Starting round %d"), CurrentRoundNumber);
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
		PC->bSpectateBehindView = false;  // Force first person view
		PC->BehindView(false);
		return;
	}
	if (AUTPlayerState* EnemyTarget = FindAliveEnemy(DeadPS))
	{
		//PC->SetViewTarget(EnemyTarget->GetUTCharacter());
		PC->ServerViewPlayerState(EnemyTarget);
		PC->bSpectateBehindView = false;  // Force first person view
		PC->BehindView(false);
		return;
	}
	PC->ServerViewSelf();
}

AUTPlayerState* AUTeamArenaGame::FindAliveEnemy(AUTPlayerState* PS) const
{
	if (!PS || !PS->Team) return nullptr;

	const int32 MyTeam = PS->Team->TeamIndex;

	// Use team members to include both human players and bots
	for (int32 TeamIdx = 0; TeamIdx < Teams.Num(); TeamIdx++)
	{
		if (TeamIdx == MyTeam || !Teams.IsValidIndex(TeamIdx)) continue;

		TArray<AController*> Members = Teams[TeamIdx]->GetTeamMembers();
		for (AController* C : Members)
		{
			if (!C) continue;

			AUTPlayerState* OtherPS = Cast<AUTPlayerState>(C->PlayerState);
			if (!OtherPS || OtherPS->bOnlySpectator) continue;

			APawn* P = C->GetPawn();
			AUTCharacter* UTC = Cast<AUTCharacter>(P);
			if (P && (!UTC || !UTC->IsDead()))
			{
				return OtherPS;
			}
		}
	}

	return nullptr;
}


AUTPlayerState* AUTeamArenaGame::FindAliveTeammate(AUTPlayerState* PS) const
{
	if (!PS || !PS->Team) return nullptr;

	const int32 TeamIdx = PS->Team->TeamIndex;
	//if (!Teams.IsValidIndex(TeamIdx)) return nullptr;
	if (!Teams.IsValidIndex(TeamIdx) || !Teams[TeamIdx]) return nullptr;

	TArray<AController*> Members = Teams[TeamIdx]->GetTeamMembers();
	for (AController* C : Members)
	{
		if (!C) continue;

		AUTPlayerState* OtherPS = Cast<AUTPlayerState>(C->PlayerState);
		if (!OtherPS || OtherPS == PS || OtherPS->bOnlySpectator) continue;

		APawn* P = C->GetPawn();
		AUTCharacter* UTC = Cast<AUTCharacter>(P);
		if (P && (!UTC || !UTC->IsDead()))
		{
			return OtherPS;
		}
	}

	return nullptr;
}


/*
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
*/


int32 AUTeamArenaGame::CountAliveOnTeam(int32 TeamIndex) const
{
	int32 Alive = 0;

	// Use team members instead of just player controllers to include bots
	if (Teams.IsValidIndex(TeamIndex) && Teams[TeamIndex])
	{
		TArray<AController*> Members = Teams[TeamIndex]->GetTeamMembers();
		for (AController* C : Members)
		{
			if (!C) continue;

			AUTPlayerState* PS = Cast<AUTPlayerState>(C->PlayerState);
			if (!PS || PS->bOnlySpectator) continue;

			const APawn* P = C->GetPawn();
			const AUTCharacter* UTC = Cast<AUTCharacter>(P);
			if (P && (!UTC || !UTC->IsDead()))
			{
				++Alive;
			}
		}
	}

	return Alive;
}


void AUTeamArenaGame::ForceLosersToViewWinners(int32 WinnerTeamIndex)
{
	//UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Called with WinnerTeamIndex: %d"), WinnerTeamIndex);
	if (WinnerTeamIndex < 0)
	{
		//UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Round was a draw. No view change."));
		return;
	}
	const int32 LoserTeamIndex = (WinnerTeamIndex == 0) ? 1 : 0;
	UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: LoserTeamIndex is: %d"), LoserTeamIndex);
	AUTPlayerState* TargetPS = FindAliveOnTeamPS(WinnerTeamIndex);
	if (!TargetPS)
	{
		//UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: No alive winner found. Finding any winner..."));
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
		//UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Target PlayerState %s has no character!"), *TargetPS->PlayerName);
		return;
	}
	//UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Found target character to spectate: %s (owned by %s)"),
	//	*TargetCharacter->GetName(), *TargetPS->PlayerName);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AUTPlayerController* PC = Cast<AUTPlayerController>(It->Get());
		AUTPlayerState* PS = PC ? Cast<AUTPlayerState>(PC->PlayerState) : nullptr;
		if (!PC || !PS || !PS->Team) continue;
		if (PS->Team->TeamIndex == LoserTeamIndex)
		{
			//UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Processing player %s. Current state: %s"),
			//	*PS->PlayerName, PC->GetStateName().IsValid() ? *PC->GetStateName().ToString() : TEXT("Unknown"));
			if (!PC->IsInState(NAME_Spectating))
			{
				PC->ChangeState(NAME_Spectating);
				PC->ClientGotoState(NAME_Spectating);
				//UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Changed %s to spectating state"), *PS->PlayerName);
			}
			if (!PS->bOutOfLives)
			{
				PS->bOutOfLives = true;
				PS->ForceNetUpdate();
			}
			PC->SetViewTarget(TargetCharacter);
			PC->bSpectateBehindView = false;  // Force first person view
			PC->BehindView(false);           // Apply the camera mode
			//UE_LOG(LogGameMode, Warning, TEXT("ForceLosersToViewWinners: Set %s to directly view character %s"),
			//	*PS->PlayerName, *TargetCharacter->GetName());
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
		/*UE_LOG(LogGameMode, Warning, TEXT("Player: %s, Team: %d, State: %s, ViewTarget: %s, OutOfLives: %s, HasPawn: %s"),
			*PS->PlayerName,
			PS->Team ? PS->Team->TeamIndex : -1,
			StateName.IsValid() ? *StateName.ToString() : TEXT("Unknown"),
			*ViewTargetName,
			PS->bOutOfLives ? TEXT("Yes") : TEXT("No"),
			PC->GetPawn() ? TEXT("Yes") : TEXT("No")); */
	}
	//UE_LOG(LogGameMode, Warning, TEXT("=== END DEBUG ==="));
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

/*
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
*/


AUTPlayerState* AUTeamArenaGame::FindAliveOnTeamPS(int32 TeamIndex) const
{
	if (!Teams.IsValidIndex(TeamIndex)) return nullptr;

	TArray<AController*> Members = Teams[TeamIndex]->GetTeamMembers();
	for (AController* C : Members)
	{
		if (!C) continue;

		AUTPlayerState* PS = Cast<AUTPlayerState>(C->PlayerState);
		if (!PS || PS->bOnlySpectator) continue;

		APawn* P = C->GetPawn();
		const AUTCharacter* UTC = Cast<AUTCharacter>(P);
		if (P && (!UTC || !UTC->IsDead()))
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
	AUTGameState* GS = GetGameState<AUTGameState>();
	if (GS == nullptr || GS->IsPendingKill())
	{
		// Don't log an error, this is normal during shutdown
		return false;
	}
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
		AUTPlayerState* ClutchPlayer = FindAliveOnTeamPS(0);
		BroadcastLastManStanding(0, ClutchPlayer);
		bTeam0LastManAnnounced = true;
		if (ClutchPlayer)
		{	// Broadcast the "clutch attempt" event
			// Pass the player and the number of enemies they are facing (Alive1)
			OnClutchSituationStarted.Broadcast(ClutchPlayer, Alive1);
			UE_LOG(LogGameMode, Log, TEXT("Clutch Situation Started: %s (Team 0) vs %d enemies."), *ClutchPlayer->PlayerName, Alive1);
		}
	}
	if (Team1StartingSize > 1 && Alive1 == 1 && !bTeam1LastManAnnounced)
	{
		AUTPlayerState* ClutchPlayer = FindAliveOnTeamPS(1);
		BroadcastLastManStanding(1, ClutchPlayer);
		bTeam1LastManAnnounced = true;
		if (ClutchPlayer)
		{
			// Broadcast the "clutch attempt" event
			// Pass the player and the number of enemies they are facing (Alive0)
			OnClutchSituationStarted.Broadcast(ClutchPlayer, Alive0);
			UE_LOG(LogGameMode, Log, TEXT("Clutch Situation Started: %s (Team 1) vs %d enemies."), *ClutchPlayer->PlayerName, Alive0);
		}
	}
}

void AUTeamArenaGame::BroadcastLastManStanding(int32 LastManTeamIndex, AUTPlayerState* LastManPlayerState)
{
	// Call Blueprint implementation first (for players without C++ plugin)
	BP_OnLastManStanding(LastManTeamIndex, LastManPlayerState);

	/* Then do C++ implementation for players with the plugin
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
	}*/
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
	/*if (Team0StartingSize == 0 || Team1StartingSize == 0)
	{
		// One team is empty - let the timer run out naturally
		return;
	}*/
	const bool Team0Eliminated = (Alive0 == 0);
	const bool Team1Eliminated = (Alive1 == 0);
	if (Team0Eliminated && !Team1Eliminated)
	{
		FTimerDelegate TimerDelegate;
		TimerDelegate.BindUFunction(this, FName("DelayedEndRound"), 1, FName(TEXT("Elimination")));
		GetWorldTimerManager().SetTimer(TH_RoundEndDelay, TimerDelegate, 0.2f, false);
	}
	else if (Team1Eliminated && !Team0Eliminated)
	{
		FTimerDelegate TimerDelegate;
		TimerDelegate.BindUFunction(this, FName("DelayedEndRound"), 0, FName(TEXT("Elimination")));
		GetWorldTimerManager().SetTimer(TH_RoundEndDelay, TimerDelegate, 0.2f, false);
	}
	else if (Team0Eliminated && Team1Eliminated)
	{
		FTimerDelegate TimerDelegate;
		TimerDelegate.BindUFunction(this, FName("DelayedEndRound"), INDEX_NONE, FName(TEXT("Draw")));
		GetWorldTimerManager().SetTimer(TH_RoundEndDelay, TimerDelegate, 0.2f, false);
	}
}

void AUTeamArenaGame::BroadcastDomination(int32 DominatingTeamIndex)
{
	// Pure Blueprint implementation - let BP handle all audio/UI
	BP_OnDomination(DominatingTeamIndex);
}

void AUTeamArenaGame::BroadcastTakesLead(int32 LeadingTeamIndex)
{
	// Pure Blueprint implementation - let BP handle all audio/UI
	BP_OnTakesLead(LeadingTeamIndex);
}

void AUTeamArenaGame::BroadcastRoundResults(int32 WinnerTeamIndex, bool bIsDraw)
{
	// Call Blueprint implementation first (for players without C++ plugin)
	bool bAllWinnersAlive = false;
	if (!bIsDraw && WinnerTeamIndex >= 0)
	{
		// Get total team size and alive count for the winning team
		int32 TotalWinningTeamSize = 0;
		int32 AliveWinningTeamSize = 0;

		if (Teams.IsValidIndex(WinnerTeamIndex))
		{
			TArray<AController*> WinnerMembers = Teams[WinnerTeamIndex]->GetTeamMembers();
			for (AController* C : WinnerMembers)
			{
				AUTPlayerState* PS = Cast<AUTPlayerState>(C->PlayerState);
				if (PS && !PS->bOnlySpectator)
				{
					TotalWinningTeamSize++;

					// Check if this player is alive
					APawn* P = C->GetPawn();
					AUTCharacter* UTC = Cast<AUTCharacter>(P);
					if (P && (!UTC || !UTC->IsDead()))
					{
						AliveWinningTeamSize++;
					}
				}
			}
		}

		// All winners are alive if alive count equals total count and team isn't empty
		bAllWinnersAlive = (TotalWinningTeamSize > 0 && AliveWinningTeamSize == TotalWinningTeamSize);
	}
	BP_OnRoundResults(WinnerTeamIndex, bIsDraw, bAllWinnersAlive);

	/* Then do C++ implementation for players with the plugin
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
	}*/
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

/*
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
*/

void AUTeamArenaGame::CheckForDarkHorse(int32 WinnerTeamIndex)
{
	int32 AliveWinners = CountAliveOnTeam(WinnerTeamIndex);
	if (AliveWinners == 1)
	{
		// Check for dark horse using team members to include bots
		if (Teams.IsValidIndex(WinnerTeamIndex))
		{
			TArray<AController*> WinnerMembers = Teams[WinnerTeamIndex]->GetTeamMembers();
			for (AController* C : WinnerMembers)
			{
				if (!C) continue;

				AUTPlayerState* PS = Cast<AUTPlayerState>(C->PlayerState);
				if (!PS || PS->bOnlySpectator) continue;

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


// For CheckForACE:
void AUTeamArenaGame::CheckForACE(int32 WinnerTeamIndex)
{
	int32 EnemyTeamIndex = (WinnerTeamIndex == 0) ? 1 : 0;

	// Count enemy team size using team members
	int32 EnemyTeamSize = 0;
	if (Teams.IsValidIndex(EnemyTeamIndex))
	{
		TArray<AController*> EnemyMembers = Teams[EnemyTeamIndex]->GetTeamMembers();
		for (AController* C : EnemyMembers)
		{
			AUTPlayerState* PS = Cast<AUTPlayerState>(C->PlayerState);
			if (PS && !PS->bOnlySpectator)
			{
				EnemyTeamSize++;
			}
		}
	}

	// Check for ACE using team members
	if (Teams.IsValidIndex(WinnerTeamIndex))
	{
		TArray<AController*> WinnerMembers = Teams[WinnerTeamIndex]->GetTeamMembers();
		for (AController* C : WinnerMembers)
		{
			AUTPlayerState* PS = Cast<AUTPlayerState>(C->PlayerState);
			if (PS && PS->RoundKills >= EnemyTeamSize && EnemyTeamSize >= 2)
			{
				RecordACE(PS);
				break;
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
			AUTPlayerState* PS = DamagePair.Key.Get();
			float PlayerDamage = DamagePair.Value;
			if (PS && PS->Team && PS->Team->TeamIndex == WinnerTeamIndex && PlayerDamage > 440.f)
			{
				float DamagePercentage = (PlayerDamage / TeamTotalDamage) * 100.0f;
				if (DamagePercentage >= HighDamageCarryThreshold)
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
	// Add the same safety checks as parent implementations
	if (!Victim || !Attacker || !UTGameState)
	{
		return;
	}

	// Use the same team check pattern as parent implementations
	if (UTGameState->OnSameTeam(Victim, Attacker))
	{
		// This is team damage, don't count it for achievements
		return;
	}

	// Now safely proceed with damage tracking logic
	if (!bRoundInProgress || !Attacker->Team || DamageAmount <= 0)
	{
		return;
	}


		// **NEW**: Calculate actual damage dealt (not overkill)
		int32 ActualDamageDealt = DamageAmount;

		if (Victim && Victim->GetUTCharacter())
		{
			AUTCharacter* VictimChar = Victim->GetUTCharacter();
			int32 VictimHealth = VictimChar->Health;
			float VictimArmor = VictimChar->GetArmorAmount();
			int32 TotalVictimHP = VictimHealth + FMath::FloorToInt(VictimArmor);

			// Cap damage at victim's actual health + armor
			ActualDamageDealt = FMath::Min(DamageAmount, TotalVictimHP);
		}

		if (!PlayerRoundDamage.Contains(Attacker))
		{
			PlayerRoundDamage.Add(Attacker, 0.0f);
		}
		PlayerRoundDamage[Attacker] += ActualDamageDealt; // Use actual damage, not overkill
		if (Attacker->Team->TeamIndex == 0)
		{
			Team0RoundDamage += ActualDamageDealt;
		}
		else if (Attacker->Team->TeamIndex == 1)
		{
			Team1RoundDamage += ActualDamageDealt;
		}

}

bool AUTeamArenaGame::ModifyDamage_Implementation(int32& Damage, FVector& Momentum, APawn* Injured, AController* InstigatedBy, const FHitResult& HitInfo, AActor* DamageCauser, TSubclassOf<UDamageType> DamageType)
{
	// Check for invulnerability during intermission
	if (GetMatchState() == FName(TEXT("RoundCooldown")) && Injured && Injured->PlayerState)
	{
		AUTPlayerState* InjuredPS = Cast<AUTPlayerState>(Injured->PlayerState);
		if (InjuredPS && InjuredPS->Team && InjuredPS->Team->TeamIndex == LastRoundWinningTeamIndex)
		{
			Damage = 0;
			Momentum = FVector::ZeroVector;
			return true; // Damage modified (to zero)
		}
	}

	/* Apply standard team damage reduction
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
	*/
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
	//UE_LOG(LogGameMode, Warning, TEXT("Overtime announcement sound played to all players"));
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

void AUTeamArenaGame::BP_RestartCurrentRound()
{
	if (!HasAuthority())
	{
		UE_LOG(LogGameMode, Warning, TEXT("BP_RestartCurrentRound: Only server can restart rounds"));
		return;
	}

	// Only allow round restart during a match (not in warmup or when match hasn't started)
	if (bWarmupMode || !HasMatchStarted())
	{
		UE_LOG(LogGameMode, Warning, TEXT("BP_RestartCurrentRound: Cannot restart round - match not in progress"));
		return;
	}

	UE_LOG(LogGameMode, Warning, TEXT("BP_RestartCurrentRound: Restarting current round due to admin request"));

	// Stop any active overtime
	StopOvertime();

	// Clear any pending timers that might interfere
	GetWorldTimerManager().ClearTimer(TH_RoundEndDelay);
	GetWorldTimerManager().ClearTimer(InitialWinCheckHandle);

	// Force end current round state immediately
	bRoundInProgress = false;
	RoundEndTimeSeconds = 0.f;

	// Reset round-specific tracking without incrementing TotalRoundsPlayed
	LastRoundWinningTeamIndex = INDEX_NONE;
	bTeam0LastManAnnounced = false;
	bTeam1LastManAnnounced = false;
	Team0StartingSize = 0;
	Team1StartingSize = 0;
	Team0RoundDamage = 0.0f;
	Team1RoundDamage = 0.0f;
	PlayerRoundDamage.Empty();

	// Clean up the world and reset players for the new round
	ResetPlayersForNewRound();
	CleanupWorldForNewRound();

	// Reset spawn selection for the new round attempt
	ResetSpawnSelectionForNewRound();

	// Start a brief intermission before the new round (3 seconds)
	StartIntermission(3);

	// Broadcast a message to let everyone know the round was restarted
	BroadcastLocalized(this, UUTGameMessage::StaticClass(), 10, nullptr, nullptr, nullptr); // You might want to create a custom message for this
}

void AUTeamArenaGame::Logout(AController* Exiting)
{
	if (Exiting)
	{
		AUTPlayerState* PS = Cast<AUTPlayerState>(Exiting->PlayerState);
		if (PS)
		{
			// --- THIS IS THE FIX ---
			// The PlayerState is about to be destroyed.
			// We MUST remove it from our TMap to prevent
			// stale pointer access.
			PlayerRoundDamage.Remove(PS);

		}
	}

	Super::Logout(Exiting);
}


void AUTeamArenaGame::InitGameState()
{
	Super::InitGameState();

	// CRITICAL FIX: Only change GameModeClass on clients for compatibility
	// Server needs to keep the real C++ class, but clients without the plugin
	// need a fallback class they can load

		if (AUTGameState* GS = GetGameState<AUTGameState>())
		{
			// Set GameModeClass to the parent class that all clients have
			GS->GameModeClass = AUTTeamGameMode::StaticClass();

			UE_LOG(LogGameMode, Warning, TEXT("InitGameState: - Set GameModeClass to UTTeamGameMode for compatibility"));
		}

}


#pragma endregion
