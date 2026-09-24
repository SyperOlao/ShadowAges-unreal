// Source/ShadowAges/Public/Combat/Tracing/SAMeleeTraceComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/Types/SAMeleeTypes.h"
#include "Combat/Tracing/SABladeTraceKernel.h"
#include "SAMeleeTraceComponent.generated.h"

class USACombatComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMeshComponent;

UCLASS(ClassGroup = (ShadowAges), meta = (BlueprintSpawnableComponent))
class SHADOWAGES_API USAMeleeTraceComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USAMeleeTraceComponent();

    UFUNCTION(BlueprintCallable, Category = "Combat|Trace")
    bool ConfigureBlade(UStaticMeshComponent* Blade, USABladeTraceProfile* Profile,
        FName ReachAnchorSocket, const TArray<AActor*>& ExtraIgnoredActors);

    UFUNCTION(BlueprintCallable, Category = "Combat|Trace")
    void ClearBlade();

    UFUNCTION(BlueprintPure, Category = "Combat|Trace")
    FString GetDebugString() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    struct FPreparedWindow
    {
        int32 Serial = INDEX_NONE;
        TArray<FSABladeContact> Contacts;
        TOptional<FSABladeContact> WorldBlock;
    };

    void HandleStepStarted(FSAMeleePlaybackKey Key);
    void HandleStepClosed(FSAMeleePlaybackKey Key);
    void HandlePoseAdvanced(const FSAMeleePoseFrame& Frame);
    bool ReadPose(FTransform& OutPose, FVector& OutAnchor, FString& OutError) const;
    bool IsCurrent(FSAMeleePlaybackKey Key) const;
    void ResetStep();
    void Fail(FSAMeleePlaybackKey Key, const FString& Error);
    bool PrepareContacts(UWorld& World, const FSAMeleePoseFrame& Frame,
        TArray<FPreparedWindow>& Windows, FSABladeTraceBudget& Budget);
    void DispatchContacts(const FSAMeleePoseFrame& Frame,
        const TArray<FPreparedWindow>& Windows);
    FSAHitContext MakeContext(const FSAMeleePoseFrame& Frame,
        int32 WindowSerial, const FSABladeContact& Contact) const;

    UPROPERTY(EditAnywhere, Category = "Combat|Trace")
    bool bDrawDebug = false;

    UPROPERTY(Transient)
    TObjectPtr<USACombatComponent> Combat = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USABladeTraceProfile> ConfiguredProfile = nullptr;

    TWeakObjectPtr<UStaticMeshComponent> WeaponMesh;
    TWeakObjectPtr<UStaticMesh> ConfiguredMeshAsset;
    TWeakObjectPtr<USkeletalMeshComponent> CharacterMesh;
    TWeakObjectPtr<AActor> WeaponActor;
    TWeakObjectPtr<AController> CreditController;
    FGuid CreditId;
    FName AnchorSocket = NAME_None;
    FName ConfiguredAttachSocket = NAME_None;
    FName ConfiguredProfileId = NAME_None;
    FVector LocalBase = FVector::ZeroVector;
    FVector LocalTip = FVector::ZeroVector;
    double ConfiguredScale = 1.0;
    FSABladeTraceSettings Settings;
    FSABladeTraceKernel Kernel;
    FSABladeTraceBudget LastBudget;
    FDelegateHandle StartedSubscription;
    FDelegateHandle PoseSubscription;
    FDelegateHandle ClosedSubscription;
    FSAMeleePlaybackKey ActiveKey;
    TSet<TWeakObjectPtr<AActor>> StepTargets;
    TSet<int32> ClosedWindows;
    FTransform PreviousPose = FTransform::Identity;
    FVector PreviousAnchor = FVector::ZeroVector;
    double PreviousPoseTime = 0.0;
    float PreviousPosePosition = 0.0f;
    uint32 LastBoneRevision = 0;
    bool bConfigured = false;
    bool bHasPose = false;
    bool bWaitingForFirstRevision = false;
    bool bProcessing = false;
    bool bEndingPlay = false;
    int32 BaselineIntervalsSkipped = 0;
    FString LastError;
};
