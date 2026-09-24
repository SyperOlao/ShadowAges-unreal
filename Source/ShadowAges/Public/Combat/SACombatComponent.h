// Source/ShadowAges/Public/Combat/SACombatComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/Types/SAActionTypes.h"
#include "Core/Types/SACombatTypes.h"
#include "Combat/SAComboBuffer.h"
#include "Combat/SAMeleeRequest.h"
#include "Magic/SAMagicTypes.h"
#include "SACombatComponent.generated.h"

class AActor;
class UAnimInstance;
class UAnimMontage;
class USkeletalMeshComponent;
class USAVitalsComponent;
class USASpellcastingComponent;

UCLASS(ClassGroup = (ShadowAges), meta = (BlueprintSpawnableComponent))
class SHADOWAGES_API USACombatComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USACombatComponent();

    // Player-facing command: one press starts or buffers one continuation.
    UFUNCTION(BlueprintCallable, Category = "Combat")
    ESACombatRequestResult RequestMeleeInput();

    UFUNCTION(BlueprintCallable, Category = "Combat")
    bool ConfigureCombat(USkeletalMeshComponent* Mesh, USAWeaponMoveset* NewMoveset);

    UFUNCTION(BlueprintCallable, Category = "Combat")
    bool RequestCancel(ESACombatCancelIntent Intent);

    UFUNCTION(BlueprintPure, Category = "Combat")
    bool IsBusy() const;

    UFUNCTION(BlueprintPure, Category = "Combat")
    FSAActionHandle GetCurrentAction() const;

    UFUNCTION(BlueprintPure, Category = "Combat")
    FSAMeleePlaybackKey GetCurrentPlayback() const;

    UFUNCTION(BlueprintPure, Category = "Combat")
    USAWeaponMoveset* GetMoveset() const;

    UFUNCTION(BlueprintPure, Category = "Combat")
    FVector FilterMoveInput(FVector Input) const;

    UFUNCTION(BlueprintPure, Category = "Combat")
    bool CanApplyLookInput() const;

    UFUNCTION(BlueprintPure, Category = "Combat")
    FString GetDebugString() const;

    // Native lifecycle: AI and later executors use this contract.
    FSAActionHandle ReserveAction(ESAActionKind Kind);
    bool BeginReservedAction(FSAActionHandle Handle);
    ESACombatRequestResult TryStartMelee(FSAActionHandle Handle, const FSAMeleeRequest& Request);
    bool FinishActionIfCurrent(FSAActionHandle Handle, ESAActionEndReason Reason);
    bool CancelActionIfCurrent(FSAActionHandle Handle, ESAActionEndReason Reason);
    bool IsCurrentAction(FSAActionHandle Handle) const;
    bool IsCurrentPlayback(FSAMeleePlaybackKey Key) const;
    double GetActionDeadline(FSAActionHandle Handle) const;
    ESAActionKind GetCurrentActionKind() const { return ActionKind; }
    float GetCastMovementScale() const;
    bool IsReservedAction(FSAActionHandle Handle, ESAActionKind Kind) const;
    bool ConfigureCastAction(FSAActionHandle Handle, USASpellcastingComponent* Executor,
        float Duration, const FSACastPhasePolicy& Policy);
    bool SetCastPhasePolicy(FSAActionHandle Handle, const FSACastPhasePolicy& Policy);
    bool RequestComboInput(FSAActionHandle Handle);

    // Contacts come from a native detector; this component owns attack damage.
    bool CanAttemptMeleeContact(AActor* Target) const;
    FSADamageResult ResolveMeleeContact(const FSAMeleeHitRequest& Request);
    bool NotifyMeleeWorldContact(const FSAHitContext& Context);

    FSAOnActionFinished OnActionFinished;
    FSAOnMeleeStepEvent OnMeleeStepStarted;
    FSAOnMeleeStepEvent OnMeleeStepClosed;
    FSAOnComboAcceptOpened OnComboAcceptOpened;
    FSAOnMeleePoseAdvanced OnMeleePoseAdvanced;
    FSAOnMeleeContactResolved OnMeleeContactResolved;
    FSAOnMeleeWorldContact OnMeleeWorldContact;

    UPROPERTY(BlueprintAssignable, Category = "Combat")
    FSAOnActionFinishedBP OnActionFinishedBP;

    UPROPERTY(BlueprintAssignable, Category = "Combat")
    FSAOnMeleeStepEventBP OnMeleeStepStartedBP;

    UPROPERTY(BlueprintAssignable, Category = "Combat")
    FSAOnMeleeStepEventBP OnComboAcceptOpenedBP;

    UPROPERTY(BlueprintAssignable, Category = "Combat|Contact")
    FSAOnMeleeContactResolvedBP OnMeleeContactResolvedBP;

    UPROPERTY(BlueprintAssignable, Category = "Combat|Contact")
    FSAOnMeleeWorldContactBP OnMeleeWorldContactBP;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    bool EnsureDependencies(bool bRequireMelee = true);
    bool ValidateForMesh(const USAWeaponMoveset* Candidate, FString& Error) const;
    bool IsContactWindowOpen(FSAMeleePlaybackKey Key, int32 WindowSerial) const;
    ESACombatRequestResult StartStep(FSAActionHandle Handle, int32 NewStepIndex);
    ESACombatRequestResult StartStepInternal(FSAActionHandle Handle, int32 NewStepIndex);
    void AdvanceTimeline(float Position, double Now);
    void StopPlayback(TWeakObjectPtr<UAnimInstance> Anim, int32 InstanceID, float BlendSeconds);
    void FlushTerminalEvents();
    void HandleOwnerDied();
    void HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted, FSAMeleePlaybackKey Expected);
    void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted, FSAMeleePlaybackKey Expected);

    UPROPERTY(EditAnywhere, Category = "Combat")
    TObjectPtr<USAWeaponMoveset> Moveset = nullptr;

    UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.01"))
    float ReservationTimeoutSeconds = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.1"))
    float TimeoutGraceSeconds = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0"))
    float StopBlendOutSeconds = 0.10f;

    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> CombatMesh = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USAVitalsComponent> Vitals = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USAWeaponMoveset> ActiveMoveset = nullptr;

    UPROPERTY(Transient)
    FSAMeleeStep ActiveStep;

    TWeakObjectPtr<UAnimInstance> OwnedAnim;
    TWeakObjectPtr<USASpellcastingComponent> CastExecutor;
    FSACastPhasePolicy CastPolicy;
    const FSAMeleePoseFrame* ContactFrame = nullptr;
    FDelegateHandle DeathSubscription;
    FSAActionHandle ActiveAction;
    FSAMeleePlaybackKey ActiveKey;
    FSAMeleePlaybackKey AcceptDispatchKey;
    ESAActionState State = ESAActionState::Idle;
    ESAActionKind ActionKind = ESAActionKind::Melee;
    FSAComboBuffer Combo;
    FGuid PendingStaminaReservation;
    int32 GenerationCounter = 0;
    float PreviousPosition = 0.0f;
    double PreviousWorldTime = 0.0;
    double ReservationDeadline = 0.0;
    double StepDeadline = 0.0;
    double ActionDeadline = 0.0;
    bool bFirstPoseSample = true;
    bool bAcceptNotified = false;
    bool bBranchClosed = false;
    bool bFinishing = false;
    bool bMutatingStep = false;
    bool bDispatchingTerminalEvents = false;
    bool bEndingPlay = false;
    bool bDispatchingMeleeContact = false;
    FString LastError;

    struct FTerminalEvent
    {
        FSAActionHandle Action;
        ESAActionEndReason Reason = ESAActionEndReason::Failed;
    };

    TArray<FTerminalEvent> PendingTerminalEvents;
};
