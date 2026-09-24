#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Magic/SAMagicTypes.h"
#include "SAProjectile.generated.h"
class USphereComponent;
class UProjectileMovementComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSAOnSpellImpact, const FHitResult&, Hit, const FSADamageResult&, Result);

UCLASS(Blueprintable)
class SHADOWAGES_API ASAProjectile : public AActor
{
    GENERATED_BODY()
public:
    ASAProjectile();
    bool Prepare(const FSASpellPayload& Payload, const FVector& Direction, float Speed, float Radius, float Range);
    void ActivatePrepared();
    void ConsumeImpact(const FHitResult& Hit);
    UFUNCTION(BlueprintPure, Category="SA|Magic")
    bool HasConsumedImpact() const { return bImpactConsumed; }
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA|Magic")
    TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA|Magic")
    TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(BlueprintAssignable, Category="SA|Magic")
    FSAOnSpellImpact OnImpact;
protected:
    virtual void BeginPlay() override;
private:
    UFUNCTION()
    void HandleOverlap(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComponent,
        int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);
    UFUNCTION()
    void HandleStop(const FHitResult& Hit);
    UPROPERTY(Transient)
    FSASpellPayload Snapshot;
    FVector PreparedVelocity = FVector::ZeroVector;
    float TimeToLive = 1.f;
    bool bPrepared = false;
    bool bActive = false;
    bool bImpactConsumed = false;
};
