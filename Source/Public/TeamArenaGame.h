#pragma once

// Always include CoreMinimal first in UCLASS headers.
//#include "CoreMinimal.h"
#include "TeamArena.h"
#include "CoreMinimal.h"
#include "UTTeamGameMode.h"
#include "UTGameState.h"
#include "UTDamageType.h" 
#include "Templates/SubclassOf.h"
#include "TimerManager.h"
#include "UTLineUpHelper.h"
#include "TeamArenaGame.generated.h"

// Forward declarations to avoid pulling heavy headers here.
class AUTHUD;
class AController;
class APlayerStart;


USTRUCT()
struct FSpawnPointData
{
	GENERATED_BODY()

	UPROPERTY()
	APlayerStart* PlayerStart;

	UPROPERTY()
	float HeightScore;

	UPROPERTY()
	float TeamSideScore;

	UPROPERTY()
	int32 Team0UsageCount;

	UPROPERTY()
	int32 Team1UsageCount;

	UPROPERTY()
	int32 LastUsedRound;

	FSpawnPointData()
		: PlayerStart(nullptr)
		, HeightScore(0.0f)
		, TeamSideScore(0.0f)
		, Team0UsageCount(0)
		, Team1UsageCount(0)
		, LastUsedRound(-1)
	{
	}

	FSpawnPointData(APlayerStart* InPlayerStart)
		: PlayerStart(InPlayerStart)
		, HeightScore(0.0f)
		, TeamSideScore(0.0f)
		, Team0UsageCount(0)
		, Team1UsageCount(0)
		, LastUsedRound(-1)
	{
	}

	int32 GetUsageCountForTeam(int32 TeamIndex) const
	{
		return (TeamIndex == 0) ? Team0UsageCount : Team1UsageCount;
	}

	void IncrementUsageForTeam(int32 TeamIndex, int32 CurrentRound)
	{
		if (TeamIndex == 0)
		{
			Team0UsageCount++;
		}
		else
		{
			Team1UsageCount++;
		}
		LastUsedRound = CurrentRound;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerACE, AUTPlayerState*, PlayerState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerDarkHorse, AUTPlayerState*, PlayerState, int32, EnemiesKilled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerHighDamageCarry, AUTPlayerState*, PlayerState, float, DamagePercentage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnClutchSituationStarted, AUTPlayerState*, ClutchPlayer, int32, EnemiesAlive);


UCLASS(Config = Game)
class TEAMARENA_API AUTeamArenaGame : public AUTTeamGameMode {
	GENERATED_BODY()

public:
	AUTeamArenaGame(const FObjectInitializer& ObjectInitializer);

	// === Configurable Timings ===
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Timing")
	float AwardDisplayTime;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Timing")
	float PreRoundCountdown;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Timing")
	float SpawnProtectionTime;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Timing")
	float SpectateDelay;
	// -------- Round Rules --------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Rules")
	int32 ScoreLimit;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Rules")
	int32 RoundTimeSeconds;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Rules")
	int32 RoundStartDelaySeconds;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Rules")
	bool bAllowRespawnMidRound;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Arena|State")
	bool bRoundInProgress;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Arena|State")
	int32 LastRoundWinningTeamIndex;

	/** Total number of rounds played (including draws) - accessible from Blueprint */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Arena|State")
	int32 TotalRoundsPlayed;

	/** Restart the current round - useful for handling disconnections or other issues during official matches */
	UFUNCTION(BlueprintCallable, Category = "TeamArena|Round Control")
	void BP_RestartCurrentRound();

	// C++ calls these; BP implements them (no headers, no reflection in C++).
	UFUNCTION(BlueprintImplementableEvent, Category = "Arena|Bridge")
	void BP_OnSetIntermission(bool bInIntermission, int32 IntermissionRemain);

	UFUNCTION(BlueprintImplementableEvent, Category = "Arena|Bridge")
	void BP_OnSetRound(bool bInProgress, int32 RoundRemain, int32 LastWinnerTeamIndex);

	UFUNCTION(BlueprintImplementableEvent, Category = "Arena|Bridge")
	void BP_OnLastManStanding(int32 LastManTeamIndex, AUTPlayerState* LastManPlayerState);

	UFUNCTION(BlueprintImplementableEvent, Category = "Arena|Bridge")
	void BP_OnRoundResults(int32 WinnerTeamIndex, bool bIsDraw, bool bAllWinnersAlive);

	UFUNCTION(BlueprintImplementableEvent, Category = "Arena|Bridge")
	void BP_OnDomination(int32 DominatingTeamIndex);

	UFUNCTION(BlueprintImplementableEvent, Category = "Arena|Bridge")
	void BP_OnTakesLead(int32 LeadingTeamIndex);

	// Awards
	UFUNCTION(BlueprintCallable, Category = "TeamArena|Achievements")
	void RecordACE(AUTPlayerState* PlayerState);

	UFUNCTION(BlueprintCallable, Category = "TeamArena|Achievements")
	void RecordDarkHorse(AUTPlayerState* PlayerState, int32 EnemiesKilled);

	UFUNCTION(BlueprintCallable, Category = "TeamArena|Achievements")
	void RecordHighDamageCarry(AUTPlayerState* PlayerState, float DamagePercentage);

	/** Minimum damage percentage required for WRECKER achievement (default 70%) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Achievements", meta = (ClampMin = "0.0", ClampMax = "100.0", ToolTip = "Minimum percentage of team damage a player must deal to earn High Damage Carry achievement"))
	float HighDamageCarryThreshold = 75.0f;

	/** Called when a player becomes the last one alive on their team (1vX) */
	UPROPERTY(BlueprintAssignable, Category = "TeamArena|Events")
	FOnClutchSituationStarted OnClutchSituationStarted;

	// Awards UPROPERTYs
	UPROPERTY(BlueprintAssignable, Category = "TeamArena|Events")
	FOnPlayerACE OnPlayerACE;

	UPROPERTY(BlueprintAssignable, Category = "TeamArena|Events")
	FOnPlayerDarkHorse OnPlayerDarkHorse;

	UPROPERTY(BlueprintAssignable, Category = "TeamArena|Events")
	FOnPlayerHighDamageCarry OnPlayerHighDamageCarry;

	/** BP Callable functions regarding changing MatchState */
	UFUNCTION(BlueprintCallable, Category = "TeamArena|Match State")
	void BP_SetMatchState_RoundCooldown();

	UFUNCTION(BlueprintCallable, Category = "TeamArena|Match State")
	void BP_SetMatchState_Intermission();

	UFUNCTION(BlueprintCallable, Category = "TeamArena|Match State")
	void BP_SetMatchState_InProgress();

	// -------- Replay opt-in (FlagRun does this) --------
	//virtual bool SupportsInstantReplay() const override;

	UFUNCTION(BlueprintNativeEvent)
	bool CanSpectate(APlayerController* Viewer, APlayerState* ViewTarget);
	virtual bool CanSpectate_Implementation(APlayerController* Viewer, APlayerState* ViewTarget) override;

	/** Optional HUD to use while replay is active (can be left null). */
	//UPROPERTY(EditDefaultsOnly, Category = "Replay")
	//TSubclassOf<AUTHUD> ReplayHUDClass;

	// -------- Overtime Settings --------
	// -------- Wave-Based Overtime Settings --------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Overtime")
	bool bOvertimeEnabled;

	/** Delay before first damage wave hits after overtime starts */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Overtime", meta = (ClampMin = "0.0"))
	float OvertimeStartDelay;

	/** Base damage dealt by the first wave */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Overtime", meta = (ClampMin = "1.0"))
	float OvertimeBaseDamage;

	/** Damage multiplier applied each wave (1.5 = 50% increase) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Overtime", meta = (ClampMin = "1.0"))
	float OvertimeDamageMultiplier;

	/** Maximum damage per wave (0 = no limit) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Overtime", meta = (ClampMin = "0.0"))
	float OvertimeMaxDamage;

	/** If true, damage waves cannot kill players (leaves them at 1 HP) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Overtime")
	bool bOvertimeNonLethal;
	/** Time interval between damage waves (in seconds) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Overtime", meta = (ClampMin = "0.1"))
	float OvertimeWaveInterval;

	/** Damage type used for overtime waves */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Overtime")
	TSubclassOf<UUTDamageType> OvertimeDamageType;



	// BP hooks for UI/SFX (must be UFUNCTIONs)
	UFUNCTION(BlueprintImplementableEvent, Category = "Arena|Overtime")
	void BP_OnOvertimeStarted();

	/** Called when a damage wave is about to hit - spawn your UI flash/wave effects here */
	UFUNCTION(BlueprintImplementableEvent, Category = "Arena|Overtime")
	void BP_OnOvertimeWave(float WaveDamage, int32 WaveNumber);

	/** Legacy function - kept for compatibility but no longer called */
	UFUNCTION(BlueprintImplementableEvent, Category = "Arena|Overtime", meta = (DeprecatedFunction))
	void BP_OnOvertimeTick(float CurrentDPS);


	// === Warmup config ===
	/** If true, play TDM-like warmup: instant (or short-delay) respawns, no rounds, no scoring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warmup")
	bool bWarmupMode = false;

	/** Seconds before auto-respawn in warmup (0 = instant). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warmup", meta = (ClampMin = "0.0"))
	float WarmupRespawnDelay = 0.5f;

	// -------- UT overrides --------
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void InitGameState() override;
	//virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleMatchHasStarted() override;
	virtual void DefaultTimer() override;
	void Logout(AController* Exiting) override;
	//void BeginDestroy() override;
	//static void CleanupCDO();
	/**
	 * NEW: Intercepts match state changes to drive round flow.
	 * This is the key function from UTShowdown.
	 */
	virtual void CallMatchStateChangeNotify() override;
	virtual bool ModifyDamage_Implementation(int32& Damage, FVector& Momentum, APawn* Injured, AController* InstigatedBy, const FHitResult& HitInfo, AActor* DamageCauser, TSubclassOf<UDamageType> DamageType) override;


	// In UE4 these are valid overrides on AGameMode*
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName = TEXT("")) override;
	//virtual float   RatePlayerStart(APlayerStart* Start, AController* Player) override;
	virtual void    RestartPlayer(AController* NewPlayer) override;
	virtual void    ScoreKill_Implementation(AController* Killer, AController* Other, APawn* KilledPawn, TSubclassOf<UDamageType> DamageType) override;
	virtual void	ScoreDamage_Implementation(int32 DamageAmount, AUTPlayerState* Victim, AUTPlayerState* Attacker) override;

	// -------- Victory Audio (Blueprint Editable) --------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Victory Audio")
	USoundBase* RedTeamVictorySound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Victory Audio")
	USoundBase* BlueTeamVictorySound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Victory Audio")
	USoundBase* RoundDrawSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Victory Audio")
	USoundBase* MatchVictorySound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Victory Audio")
	USoundBase* RedTeamDominatingSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Victory Audio")
	USoundBase* BlueTeamDominatingSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Victory Audio")
	USoundBase* RedTeamTakesLeadSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Victory Audio")
	USoundBase* BlueTeamTakesLeadSound;

	// -------- Last Man Standing Sounds --------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Last Man Standing")
	USoundBase* LastManStandingSound;
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Last Man Standing")
	USoundBase* EnemyLastManStandingSound;

	/** Check and broadcast last man standing status */
	void CheckLastManStanding(int32 Alive0, int32 Alive1);

	/** Broadcast last man standing sounds */
	void BroadcastLastManStanding(int32 LastManTeamIndex, AUTPlayerState* LastManPlayerState);

	void BroadcastOvertimeCountdown(int32 CountdownValue);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Overtime Audio")
	USoundBase* OvertimeAnnouncementSound;

	void BroadcastOvertimeAnnouncement();


protected:
	// -------- Round flow --------

	/** * REWRITTEN: Now just sets the state to MatchIntermission and sets the timer.
	 */
	void StartIntermission(int32 Seconds);
	/** NEW: Delay after dying before forcing a player to spectate teammates */
	/**
	 * NEW: Called when state enters MatchIntermission.
	 * Hides pawns, resets spawns, and forces spectator views.
	 */
	virtual void HandleMatchIntermission();

	/** * REWRITTEN: Now called by the state machine *after* intermission.
	 * Responsible for cleaning old pawns and spawning new ones.
	 */
	void StartNextRound();

	void CheckRoundWinConditions();
	//void PrepareNextRound(); // This function is no longer needed and has been removed.

	bool GetAliveCounts(int32& OutAliveTeam0, int32& OutAliveTeam1) const;

	/**
	 * REWRITTEN: Now handles scoring, checks for game-end, and calls StartIntermission.
	 */
	void EndRoundForTeam(int32 WinnerTeamIndex, FName Reason);

	int32 GetTiebreakWinnerByTeamHealth() const;
	void ResetPlayersForNewRound();
	void CleanupWorldForNewRound();
	//void GrantSpawnProtection(AController* PC);
	//void OnSpawnProtectionBroken(class AUTCharacter* Character);
	void BroadcastRoundResults(int32 WinnerTeamIndex, bool bIsDraw);

	// -------- Victory Message Configuration --------
	/** Updates the victory message class with sounds from GameMode settings */
	void UpdateVictoryMessageSounds();

	// -------- Domination and Lead Tracking --------
	/** Check and broadcast domination/lead messages after a round ends */
	void CheckForDominationAndLead(int32 WinnerTeamIndex);

	/** Broadcast domination message (team is leading by 5+ rounds) */
	void BroadcastDomination(int32 DominatingTeamIndex);

	/** Broadcast takes lead message (team takes the lead from a tie or changes lead) */
	void BroadcastTakesLead(int32 LeadingTeamIndex);

	UPROPERTY(Transient)
	float WinCheckHoldUntilSeconds = 0.f;

	/** used to deny RestartPlayer() except for our forced spawn at round start */
	bool bAllowPlayerRespawns;

	/** Previous round scores for domination/lead detection */
	UPROPERTY(Transient)
	int32 PreviousRedScore;

	UPROPERTY(Transient)
	int32 PreviousBlueScore;

	/** Flag to track if domination message has been broadcast */
	UPROPERTY(Transient)
	bool bHasBroadcastTeamDominating;

	/** Track team sizes at round start for last man standing detection */
	UPROPERTY(Transient)
	int32 Team0StartingSize;

	UPROPERTY(Transient)
	int32 Team1StartingSize;

	/** Track if last man standing has been announced this round for each team */
	UPROPERTY(Transient)
	bool bTeam0LastManAnnounced;

	UPROPERTY(Transient)
	bool bTeam1LastManAnnounced;

	/** Force a dead player into spectate. Prefer teammates; if none alive, allow enemy spectate. */
	void ForceTeamSpectate(class AUTPlayerState* DeadPS);

	/** Finds a living teammate for PS (nullptr if none). */
	class AUTPlayerState* FindAliveTeammate(class AUTPlayerState* PS) const;

	/** Finds a living enemy for PS (nullptr if none). */
	class AUTPlayerState* FindAliveEnemy(class AUTPlayerState* PS) const;

	AUTPlayerState* FindAliveOnTeamPS(int32 TeamIndex) const;
	AUTPlayerState* FindAnyOnTeamPS(int32 TeamIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Team Arena")
	void ForceLosersToViewWinners(int32 WinnerTeamIndex);
	void DebugPlayerStates();
	/** NEW: Called by a timer to force a dead player to spectate after a delay */
	UFUNCTION()
	void DelayedForceSpectate(class AUTPlayerState* DeadPS);
	/** Returns number of living players on a given team index (0/1). */
	int32 CountAliveOnTeam(int32 TeamIndex) const;
	/** Deferred call to HandleMatchHasStarted from BeginPlay */
	UFUNCTION()
	void DeferredHandleMatchStart();

	/** Deferred call to CheckRoundWinConditions from overtime */
	UFUNCTION()
	void DeferredCheckRoundWinConditions();

	/** Timer for delayed win check at round start */
	FTimerHandle InitialWinCheckHandle;

	/** Performs the delayed win check */
	void DelayedInitialWinCheck();

	/** Timer for delaying the round end logic to fix camera bugs */
	FTimerHandle TH_RoundEndDelay;

	/** Function to be called by the timer to end the round */
	UFUNCTION()
	void DelayedEndRound(int32 WinnerTeamIndex, FName Reason);

	/** Deferred call to HandleMatchHasStarted from BeginPlay */
	//UFUNCTION()
	//void DeferredHandleMatchStart();

	/** Deferred call to CheckRoundWinConditions from overtime */
	//UFUNCTION()
	//void DeferredCheckRoundWinConditions();

	// -------- Overtime --------
	void StartOvertime();
	void StopOvertime();
	void ExecuteOvertimeWave();


	// Wave-based overtime state
	FTimerHandle OvertimeWaveTimerHandle;
	FTimerHandle OvertimeCountdownTimerHandle;
	//FTimerHandle TH_NextRound; // No longer needed
	int32 CurrentOvertimeWave = 0;
	float CurrentWaveDamage = 0.0f;

	// Replay HUD helper (optional)
	//void MaybeApplyReplayHUD();

	// -------- Runtime --------
	/** REPURPOSED: This now acts as the master countdown for intermission */
	int32 IntermissionSecondsRemaining = 0;

	float RoundEndTimeSeconds = 0.f;

	// Needs TimerManager.h in the .cpp, but type is fine here.
	FTimerHandle OvertimeTimerHandle;
	//FTimerHandle TH_NextRound;
	//float OvertimeStartTimeSeconds = 0.f;

	// -------- Enhanced Spawn Selection System --------
	UPROPERTY(Transient)
	TArray<FSpawnPointData> AllSpawnPoints;

	UPROPERTY(Transient)
	TArray<APlayerStart*> Team0SelectedSpawns;

	UPROPERTY(Transient)
	TArray<APlayerStart*> Team1SelectedSpawns;

	UPROPERTY(Transient)
	int32 CurrentRoundNumber = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Spawning")
	float SpawnOffsetDistance = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Spawning")
	int32 MaxSpawnOffsetAttempts = 8;

	UPROPERTY(Transient)
	bool bSpawnPointsInitialized = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Spawning", meta = (ClampMin = "0.0", ClampMax = "1.0", ToolTip = "Weight for distance from enemy spawns when selecting spawn points"))
	float SpawnDistanceWeight = 0.30f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Spawning", meta = (ClampMin = "0.0", ClampMax = "1.0", ToolTip = "Weight for spawn point height when selecting spawn points"))
	float SpawnHeightWeight = 0.10f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Spawning", meta = (ClampMin = "0.0", ClampMax = "1.0", ToolTip = "Weight for spawn usage frequency (promotes variety) when selecting spawn points"))
	float SpawnUsageWeight = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Spawning", meta = (ClampMin = "0.0", ClampMax = "1.0", ToolTip = "Weight for separation between team spawn points when selecting spawn points"))
	float SpawnSeparationWeight = 0.15f;

	// -------- Enhanced Spawn Selection Functions --------
	void InitializeSpawnPointSystem();
	void ScoreAllSpawnPoints();
	void SelectOptimalSpawnPairForTeam(int32 TeamIndex);
	void FindMaxDistanceSpawnPair(const TArray<FSpawnPointData*>& CandidateSpawns, const TArray<APlayerStart*>& EnemySpawns, APlayerStart*& OutPrimary, APlayerStart*& OutSecondary);
	float CalculateMinDistanceToEnemySpawns(APlayerStart* SpawnPoint, const TArray<APlayerStart*>& EnemySpawns);
	TArray<FSpawnPointData*> GetSpawnCandidatesForTeam(int32 TeamIndex);
	FVector FindSafeSpawnOffset(APlayerStart* BaseSpawn, int32 AttemptIndex);
	bool IsLocationClearOfPlayers(const FVector& Location, float CheckRadius = 150.0f);
	void ResetSpawnSelectionForNewRound();
	/** Minimum distance required between team spawns and enemy spawns */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Spawning", meta = (ClampMin = "500.0", ClampMax = "5000.0"))
	float MinimumEnemySpawnDistance;

	/** Preferred distance between team spawns and enemy spawns (used for scoring) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Arena|Spawning", meta = (ClampMin = "1000.0", ClampMax = "10000.0"))
	float PreferredEnemySpawnDistance;
	
	UPROPERTY(Transient)
	AActor* OverriddenPlayerStart;


	// Server Management 
		/** Handle all server management tasks (cleanup, map voting, bots, etc.) */
	void HandleServerManagement();

	/** Handle instance cleanup logic */
	void HandleInstanceCleanup();

	/** Handle map voting logic */
	void HandleMapVoting();

	/** Track empty server time for non-hub cleanup */
	UPROPERTY()
	int32 EmptyServerTime;

	/** Last lobby update timestamp */
	UPROPERTY()
	float LastLobbyUpdateTime;



	/** Track team damage for each round */
	UPROPERTY(Transient)
	float Team0RoundDamage;
	UPROPERTY(Transient)
	float Team1RoundDamage;

	/** Track individual player damage for the current round */
	UPROPERTY(Transient)
	//TMap<AUTPlayerState*, float> PlayerRoundDamage;
	TMap<TWeakObjectPtr<AUTPlayerState>, float> PlayerRoundDamage;

	void CheckRoundAchievements(int32 WinnerTeamIndex, FName Reason);
	void CheckForACE(int32 WinnerTeamIndex);
	void CheckForDarkHorse(int32 WinnerTeamIndex);
	void CheckForHighDamageCarry(int32 WinnerTeamIndex);
	TMap<TWeakObjectPtr<AUTPlayerState>, int32> DarkHorseCandidates;
	// New spawn selection methods
	void FindLeastUsedSpawnPair(const TArray<FSpawnPointData*>& CandidateSpawns, int32 TeamIndex, APlayerStart*& OutPrimary, APlayerStart*& OutSecondary);
	void FindBalancedRandomSpawnPair(const TArray<FSpawnPointData*>& CandidateSpawns, const TArray<APlayerStart*>& EnemySpawns, int32 TeamIndex, APlayerStart*& OutPrimary, APlayerStart*& OutSecondary);
};