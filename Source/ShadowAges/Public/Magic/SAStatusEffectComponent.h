#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Magic/SAMagicTypes.h"
#include "SAStatusEffectComponent.generated.h"

class USAVitalsComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSAOnEffectChanged, const FSAActiveEffect&, Effect);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSAOnEffectRemoved, FGuid, EffectId, ESAEffectRemovalReason, Reason);

UCLASS(ClassGroup=(ShadowAges), meta=(BlueprintSpawnableComponent))
class SHADOWAGES_API USAStatusEffectComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USAStatusEffectComponent();
    // Native accepted-impact entry. Effects never own the caster's action.
    FGuid ApplyEffect(const FSAEffectSpec& Spec, const FSASpellPayload& Source);
    UFUNCTION(BlueprintCallable, Category="SA|Effects")
    bool RemoveEffect(FGuid Id, ESAEffectRemovalReason Reason = ESAEffectRemovalReason::Cleansed);
    UFUNCTION(BlueprintCallable, Category="SA|Effects")
    void ClearEffects(ESAEffectRemovalReason Reason = ESAEffectRemovalReason::Cleansed);
    UFUNCTION(BlueprintPure, Category="SA|Effects")
    TArray<FSAActiveEffect> GetActiveEffects() const { return ActiveEffects; }
    UFUNCTION(BlueprintPure, Category="SA|Effects")
    float GetMovementMultiplier() const;
    UFUNCTION(BlueprintPure, Category="SA|Effects")
    bool IsSilenced() const;
    UFUNCTION(BlueprintPure, Category="SA|Effects")
    bool IsDisarmed() const;
    UFUNCTION(BlueprintCallable, Category="SA|Effects")
    void SetBaseMovementSpeeds(float WalkSpeed, float CrouchedSpeed);
    void RefreshMovement();
    UPROPERTY(BlueprintAssignable, Category="SA|Effects")
    FSAOnEffectChanged OnEffectChanged;
    UPROPERTY(BlueprintAssignable, Category="SA|Effects")
    FSAOnEffectRemoved OnEffectRemoved;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SA|Effects")
    int32 EffectTickDebt = 0;
    // Executes by game time; public native boundary also used by deterministic tests.
    void AdvanceEffects(double Now);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void WakeScheduler();
    void ScheduleNext();
    void HandleDeath();
    bool CanOperate() const;
    void ExecuteEffect(const FSAEffectSpec& Spec, FSASpellPayload Source, int32 Stacks);
    int32 FindEffect(FGuid Id) const;
    UPROPERTY(Transient)
    TArray<FSAActiveEffect> ActiveEffects;
    TWeakObjectPtr<USAVitalsComponent> Vitals;
    FTimerHandle WakeTimer;
    FDelegateHandle DeathHandle;
    bool bAdvancing = false;
    bool bClearing = false;
    bool bEndingPlay = false;
    float BaseWalkSpeed = 0.f;
    float BaseCrouchedSpeed = 0.f;
};
