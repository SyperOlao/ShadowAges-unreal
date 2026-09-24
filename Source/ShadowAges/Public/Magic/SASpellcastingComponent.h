#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Magic/SAMagicTypes.h"
#include "SASpellcastingComponent.generated.h"

class USASpellDefinition;
class USASpellTargetingComponent;
class USACombatComponent;
class USAVitalsComponent;
class USkeletalMeshComponent;
class UAnimInstance;
class UAnimMontage;
class ASAProjectile;
struct FStreamableHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSAOnCastPhase, FSAActionHandle, Action);

UCLASS(ClassGroup=(ShadowAges), meta=(BlueprintSpawnableComponent))
class SHADOWAGES_API USASpellcastingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USASpellcastingComponent();
    UFUNCTION(BlueprintCallable, Category="SA|Magic")
    bool ConfigureTargeting(USASpellTargetingComponent* Targeting, USkeletalMeshComponent* MontageMesh);
    // Preload outside combat. Changing the selected spell cancels only this executor's cast.
    UFUNCTION(BlueprintCallable, Category="SA|Magic")
    void SetPreparedSpell(USASpellDefinition* Spell);
    UFUNCTION(BlueprintCallable, Category="SA|Magic")
    ESACastFailure RequestCast(const FSASpellAim& Aim, FSAActionHandle& OutAction);
    UFUNCTION(BlueprintPure, Category="SA|Magic")
    ESACastPhase GetCastPhase() const { return Phase; }
    UFUNCTION(BlueprintPure, Category="SA|Magic")
    float GetCooldownRemaining(FGameplayTag Group) const;
    UFUNCTION(BlueprintPure, Category="SA|Magic")
    bool AreAssetsReady() const;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SA|Magic")
    ESACastFailure LastFailure = ESACastFailure::None;
    UPROPERTY(BlueprintAssignable, Category="SA|Magic")
    FSAOnCastPhase OnWindup;
    UPROPERTY(BlueprintAssignable, Category="SA|Magic")
    FSAOnCastPhase OnReleased;

    ESACastFailure ValidateCast(const FSASpellAim& Aim, bool bOwnReservation = false) const;
    ESACastFailure TryStartCast(FSAActionHandle Handle, const FSASpellAim& Aim);
    void CancelExecutor(FSAActionHandle Handle, ESAActionEndReason Reason);
    void AdvanceCast(double Now);
    void ReleaseCast(FSAActionHandle Handle);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
    bool IsCurrent(FSAActionHandle Handle) const;
    void Finish(FSAActionHandle Handle, ESAActionEndReason Reason);
    void RefreshLoadedAssets(uint64 Generation);
    void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted, FSAActionHandle Handle);
    void HandleMontageBlend(UAnimMontage* Montage, bool bInterrupted, FSAActionHandle Handle);
    UPROPERTY(Transient)
    TObjectPtr<USASpellDefinition> PreparedSpell;
    UPROPERTY(Transient)
    TObjectPtr<USASpellDefinition> ActiveSpell;
    UPROPERTY(Transient)
    TObjectPtr<USASpellTargetingComponent> AimSource;
    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> CastMesh;
    UPROPERTY(Transient)
    TSubclassOf<ASAProjectile> LoadedProjectileClass;
    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> LoadedMontage;
    UPROPERTY(Transient)
    TSubclassOf<ASAProjectile> ActiveProjectileClass;
    UPROPERTY(Transient)
    TObjectPtr<UAnimMontage> ActiveMontage;
    TWeakObjectPtr<USACombatComponent> Combat;
    TWeakObjectPtr<USAVitalsComponent> Vitals;
    TWeakObjectPtr<UAnimInstance> OwnedAnim;
    TWeakObjectPtr<ASAProjectile> PendingProjectile;
    TSharedPtr<FStreamableHandle> AssetLoad;
    TMap<FGameplayTag, double> CooldownUntil;
    FSASpellAim ActiveAim;
    FSAActionHandle ActiveAction;
    FGuid ManaReservation;
    ESACastPhase Phase = ESACastPhase::None;
    int32 MontageInstanceId = INDEX_NONE;
    uint64 LoadGeneration = 0;
    double PhaseEnd = 0.;
    bool bEndingPlay = false;
};
