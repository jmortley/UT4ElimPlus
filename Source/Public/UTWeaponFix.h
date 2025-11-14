#pragma once
#include "TeamArena.h"
#include "CoreMinimal.h"
#include "UTWeapon.h"
#include "Net/UnrealNetwork.h"
#include "UTWeaponFix.generated.h"

UCLASS(BlueprintType, Blueprintable)
class WEAPONFIX_API AUTWeaponFix : public AUTWeapon
{
    GENERATED_BODY()

public:
    AUTWeaponFix(const FObjectInitializer& ObjectInitializer);

    // Override the problematic StartFire method
    virtual void StartFire(uint8 FireModeNum) override;
    virtual void StopFire(uint8 FireModeNum) override;

protected:
    // Enhanced fire event tracking per fire mode (using int32 to avoid overflow)
    UPROPERTY(Replicated)
    TArray<int32> AuthoritativeFireEventIndex;

    UPROPERTY()
    TArray<int32> ClientFireEventIndex;

    // Cooldown tracking to prevent rapid-fire issues
    UPROPERTY()
    TArray<float> LastFireTime;

    // State tracking to prevent phantom shots - this is the key difference
    UPROPERTY(Replicated, ReplicatedUsing = OnRep_FireModeState)
    TArray<uint8> FireModeActiveState;

    // Fire mode mutex to prevent simultaneous firing
    UPROPERTY()
    uint8 CurrentlyFiringMode;

    // Enhanced validation with timing checks
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerStartFireFixed(uint8 FireModeNum, int32 FireEventIndex, float ClientTimestamp, bool bClientPredicted);

    UFUNCTION(Server, Reliable, WithValidation) 
    void ServerStopFireFixed(uint8 FireModeNum, int32 FireEventIndex, float ClientTimestamp);

    // Client confirmation to sync fire event indices
    UFUNCTION(Client, Reliable)
    void ClientConfirmFireEvent(uint8 FireModeNum, int32 AuthorizedEventIndex);

    // State replication handler
    UFUNCTION()
    void OnRep_FireModeState();

    // Anti-spam and timing validation
    bool ValidateFireRequest(uint8 FireModeNum, int32 EventIndex, float ClientTime);
    bool IsFireModeOnCooldown(uint8 FireModeNum, float CurrentTime);
    
    // Fire event index management (no more uint8 overflow)
    int32 GetNextClientFireEventIndex(uint8 FireModeNum);
    bool IsFireEventSequenceValid(uint8 FireModeNum, int32 EventIndex);

public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void BeginPlay() override;
};