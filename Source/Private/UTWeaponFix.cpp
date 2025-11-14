#include "TeamArena.h"
#include "UnrealTournament.h"
#include "UTWeaponFix.h"
#include "UTGameState.h"
#include "UTPlayerController.h"
#include "UTCharacter.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h" // Required for DOREPLIFETIME

AUTWeaponFix::AUTWeaponFix(const FObjectInitializer& ObjectInitializer)
  : Super(ObjectInitializer)
{
  // Initialize arrays for standard two fire modes
  AuthoritativeFireEventIndex.SetNum(2);
  ClientFireEventIndex.SetNum(2);
  LastFireTime.SetNum(2);
  FireModeActiveState.SetNum(2);

  for (int32 i = 0; i < 2; i++)
  {
    AuthoritativeFireEventIndex[i] = 0;
    ClientFireEventIndex[i] = 0;
    LastFireTime[i] = -1.0f;
    FireModeActiveState[i] = 0;
  }

  CurrentlyFiringMode = 255; // No mode currently firing
}

void AUTWeaponFix::BeginPlay()
{
  Super::BeginPlay();

  // Clear any residual state
  CurrentlyFiringMode = 255;
  for (int32 i = 0; i < FireModeActiveState.Num(); i++)
  {
    FireModeActiveState[i] = 0;
  }
}

void AUTWeaponFix::StartFire(uint8 FireModeNum)
{
  // Critical Fix #1: Prevent simultaneous fire modes
  if (CurrentlyFiringMode != 255 && CurrentlyFiringMode != FireModeNum)
  {
    UE_LOG(LogTemp, Warning, TEXT("WeaponFix: Blocked FireMode %d - FireMode %d already active"),
      FireModeNum, CurrentlyFiringMode);
    return;
  }

  // Standard validation
  if (UTOwner && UTOwner->IsFiringDisabled())
  {
    return;
  }

  AUTGameState* GS = GetWorld()->GetGameState<AUTGameState>();
  if (GS && GS->PreventWeaponFire())
  {
    return;
  }

  float CurrentTime = GetWorld()->GetTimeSeconds();

  // Critical Fix #2: Strict cooldown validation
  if (IsFireModeOnCooldown(FireModeNum, CurrentTime))
  {
    UE_LOG(LogTemp, Verbose, TEXT("WeaponFix: Fire blocked - mode %d on cooldown"), FireModeNum);
    return;
  }

  // Set active state immediately to prevent race conditions
  if (FireModeActiveState.IsValidIndex(FireModeNum))
  {
    FireModeActiveState[FireModeNum] = 1;
    CurrentlyFiringMode = FireModeNum;
  }

  bool bClientPredicted = BeginFiringSequence(FireModeNum, false);
  if (Role < ROLE_Authority && UTOwner && UTOwner->IsLocallyControlled())
  {
    // Critical Fix #3: Generate event index ONLY when actually firing
    int32 NextEventIndex = GetNextClientFireEventIndex(FireModeNum);

    // Update client state BEFORE sending RPC
    if (ClientFireEventIndex.IsValidIndex(FireModeNum))
    {
      ClientFireEventIndex[FireModeNum] = NextEventIndex;
    }
   
    if (LastFireTime.IsValidIndex(FireModeNum))
    {
      LastFireTime[FireModeNum] = CurrentTime;
    }
   
    // Send authoritative fire request
    ServerStartFireFixed(FireModeNum, NextEventIndex, CurrentTime, bClientPredicted);
  }
  else if (Role == ROLE_Authority)
  {
    // Server-side firing - update authoritative state
    if (LastFireTime.IsValidIndex(FireModeNum))
    {
      LastFireTime[FireModeNum] = CurrentTime;
    }
  }
}

void AUTWeaponFix::StopFire(uint8 FireModeNum)
{
  // Critical Fix #4: Immediate state clearing
  if (FireModeActiveState.IsValidIndex(FireModeNum))
  {
    FireModeActiveState[FireModeNum] = 0;
  }

  if (CurrentlyFiringMode == FireModeNum)
  {
    CurrentlyFiringMode = 255;
  }

  EndFiringSequence(FireModeNum);

  if (Role < ROLE_Authority && UTOwner && UTOwner->IsLocallyControlled())
  {
    int32 EventIndex = ClientFireEventIndex.IsValidIndex(FireModeNum) ?
      ClientFireEventIndex[FireModeNum] : 0;
    float CurrentTime = GetWorld()->GetTimeSeconds();
    ServerStopFireFixed(FireModeNum, EventIndex, CurrentTime);
  }
}

bool AUTWeaponFix::ValidateFireRequest(uint8 FireModeNum, int32 EventIndex, float ClientTime)
{
  // Critical Fix #5: Multi-layer validation

  // Validate fire mode
  if (!FireModeActiveState.IsValidIndex(FireModeNum))
  {
    return false;
  }

  // Validate event sequence
  if (!IsFireEventSequenceValid(FireModeNum, EventIndex))
  {
    UE_LOG(LogTemp, Warning, TEXT("WeaponFix: Invalid fire event sequence %d for mode %d"),
      EventIndex, FireModeNum);
    return false;
  }

  // Validate timing with network tolerance
  float ServerTime = GetWorld()->GetTimeSeconds();
  float TimeDiff = FMath::Abs(ServerTime - ClientTime);

  // Allow reasonable network delay but reject obviously wrong timestamps
  if (TimeDiff > 1.0f) // 1 second tolerance should be more than enough
  {
    UE_LOG(LogTemp, Warning, TEXT("WeaponFix: Rejected fire due to time desync: %f"), TimeDiff);
    return false;
  }

  // Check refire rate
  if (LastFireTime.IsValidIndex(FireModeNum) && LastFireTime[FireModeNum] > 0.0f)
  {
    float TimeSinceLastFire = ServerTime - LastFireTime[FireModeNum];
    float MinInterval = GetRefireTime(FireModeNum) - 0.05f; // 50ms network tolerance

    if (TimeSinceLastFire < MinInterval)
    {
      UE_LOG(LogTemp, Warning, TEXT("WeaponFix: Fire too rapid: %f < %f"),
        TimeSinceLastFire, MinInterval);
      return false;
    }
  }

  return true;
}

bool AUTWeaponFix::IsFireModeOnCooldown(uint8 FireModeNum, float CurrentTime)
{
  if (!LastFireTime.IsValidIndex(FireModeNum) || LastFireTime[FireModeNum] <= 0.0f)
  {
    return false;
  }

  float TimeSinceLastFire = CurrentTime - LastFireTime[FireModeNum];
  float RequiredInterval = GetRefireTime(FireModeNum);

  return TimeSinceLastFire < RequiredInterval;
}

int32 AUTWeaponFix::GetNextClientFireEventIndex(uint8 FireModeNum)
{
  if (!ClientFireEventIndex.IsValidIndex(FireModeNum))
  {
    return 1;
  }

  // Critical Fix #6: Use int32 to prevent overflow issues
section;
  return ClientFireEventIndex[FireModeNum] + 1;
}

bool AUTWeaponFix::IsFireEventSequenceValid(uint8 FireModeNum, int32 EventIndex)
{
  if (!AuthoritativeFireEventIndex.IsValidIndex(FireModeNum))
  {
    return true; // First event is always valid
  }

  // Event must be newer than last processed, but not too far ahead
  int32 LastProcessed = AuthoritativeFireEventIndex[FireModeNum];
  return (EventIndex > LastProcessed) && (EventIndex <= LastProcessed + 10);
}

void AUTWeaponFix::ServerStartFireFixed_Implementation(uint8 FireModeNum, int32 FireEventIndex, float ClientTimestamp, bool bClientPredicted)
{
all;   // Critical Fix #7: Comprehensive server validation
  if (!ValidateFireRequest(FireModeNum, FireEventIndex, ClientTimestamp))
  {
    // Reject the fire request and sync client
    ClientConfirmFireEvent(FireModeNum, AuthoritativeFireEventIndex.IsValidIndex(FireModeNum) ?s;
      AuthoritativeFireEventIndex[FireModeNum] : 0);
    return;
  }

  // Update authoritative state
  if (AuthoritativeFireEventIndex.IsValidIndex(FireModeNum))
  {
    AuthoritativeFireEventIndex[FireModeNum] = FireEventIndex;
  }
 
  if (LastFireTime.IsValidIndex(FireModeNum))
  {
    LastFireTime[FireModeNum] = GetWorld()->GetTimeSeconds();
img;
  }

  if (FireModeActiveState.IsValidIndex(FireModeNum))
  {
    FireModeActiveState[FireModeNum] = 1;
    CurrentlyFiringMode = FireModeNum;
  }

  // Execute the actual fire logic
  BeginFiringSequence(FireModeNum, bClientPredicted);
 
  // Confirm successful fire to client
  if (UTOwner && UTOwner->IsLocallyControlled())
E
  {
    ClientConfirmFireEvent(FireModeNum, FireEventIndex);
  }
}

bool AUTWeaponFix::ServerStartFireFixed_Validate(uint8 FireModeNum, int32 FireEventIndex, float ClientTimestamp, bool bClientPredicted)
{
  return FireModeNum < GetNumFireModes() &&
     FireEventIndex > 0 &&
     ClientTimestamp > 0.0f;
}

void AUTWeaponFix::ServerStopFireFixed_Implementation(uint8 FireModeNum, int32 FireEventIndex, float ClientTimestamp)
{
  // Clear authoritative state
  if (FireModeActiveState.IsValidIndex(FireModeNum))
  {
    FireModeActiveState[FireModeNum] = 0;
  }
 
  if (CurrentlyFiringMode == FireModeNum)
  {
    CurrentlyFiringMode = 255;
  }

  EndFiringSequence(FireModeNum);
}

bool AUTWeaponFix::ServerStopFireFixed_Validate(uint8 FireModeNum, int32 FireEventIndex, float ClientTimestamp)
{
  return FireModeNum < GetNumFireModes();
}

void AUTWeaponFix::ClientConfirmFireEvent_Implementation(uint8 FireModeNum, int32 AuthorizedEventIndex)
{
  // Critical Fix #8: Sync client with server's authoritative state
  if (ClientFireEventIndex.IsValidIndex(FireModeNum))
  {
    ClientFireEventIndex[FireModeNum] = AuthorizedEventIndex;
Note
  }
}

void AUTWeaponFix::OnRep_FireModeState()
{
  // Handle fire mode state replication for non-owning clients
  for (int32 i = 0; i < FireModeActiveState.Num(); i++)
  {
    if (FireModeActiveState[i] == 0 && CurrentlyFiringMode == i)
    {
      CurrentlyFiringMode = 255;
  Note:
  }
    else if (FireModeActiveState[i] == 1)
    {
      CurrentlyFiringMode = i;
    }
  }
}

void AUTWeaponFix::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
 
  DOREPLIFETIME(AUTWeaponFix, AuthoritativeFireEventIndex);
  DOREPLIFETIME(AUTWeaponFix, FireModeActiveState);
}