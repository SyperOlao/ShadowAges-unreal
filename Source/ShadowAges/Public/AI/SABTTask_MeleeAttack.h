#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Core/Types/SAActionTypes.h"
#include "SABTTask_MeleeAttack.generated.h"

class AActor;
class UBehaviorTree;
class UBehaviorTreeComponent;
class USACombatComponent;


UCLASS()
class SHADOWAGES_API USABTTask_MeleeAttack : public UBTTaskNode
{
    GENERATED_BODY()

public:
    USABTTask_MeleeAttack(const FObjectInitializer& ObjectInitializer);

protected:
    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        float DeltaSeconds) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory) override;
    virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
        EBTNodeResult::Type TaskResult) override;

private:
    bool HasValidTarget() const;
    bool IsTargetInReach() const;
    void HandleStepStarted(FSAMeleePlaybackKey Key);
    void HandleComboAcceptOpened(FSAMeleePlaybackKey Key);
    void HandleActionFinished(FSAActionHandle Handle, ESAActionEndReason Reason);
    void FinishTask(EBTNodeResult::Type Result);
    void Cleanup(bool bCancelOwnedAction);

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector TargetActorKey;

    UPROPERTY(EditAnywhere, Category="Combat", meta=(ClampMin="1", ClampMax="3"))
    int32 MaxPlannedSteps = 3;

    UPROPERTY(EditAnywhere, Category="Combat", meta=(ClampMin="1.0", Units="cm"))
    float AttackRange = 200.0f;

    UPROPERTY(EditAnywhere, Category="Combat", meta=(ClampMin="0.0", ClampMax="180.0"))
    float FacingHalfAngleDegrees = 70.0f;

    UPROPERTY(EditAnywhere, Category="Combat")
    bool bStopMovementBeforeAttack = true;

    TWeakObjectPtr<UBehaviorTreeComponent> RunningOwner;
    TWeakObjectPtr<USACombatComponent> Combat;
    TWeakObjectPtr<AActor> Target;
    FSAActionHandle Action;
    FDelegateHandle FinishedSubscription;
    FDelegateHandle StepSubscription;
    FDelegateHandle AcceptSubscription;
    int32 PlannedSteps = 0;
    int32 StepsStarted = 0;
    int32 LastStartedGeneration = 0;
    int32 LastAcceptedGeneration = 0;
    double TaskDeadline = 0.0;
    bool bStarting = false;
    bool bHasPendingResult = false;
    EBTNodeResult::Type PendingResult = EBTNodeResult::Failed;
};
