#pragma once
#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Core/Types/SAActionTypes.h"
#include "SABTTask_CastSpell.generated.h"

class USACombatComponent;
class USASpellcastingComponent;
UCLASS()
class SHADOWAGES_API USABTTask_CastSpell : public UBTTaskNode
{
    GENERATED_BODY()
public:
    USABTTask_CastSpell();
protected:
    virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type Result) override;
private:
    bool HasTarget() const;
    void HandleFinished(FSAActionHandle Handle, ESAActionEndReason Reason);
    void Cleanup(bool bCancel);
    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector TargetActorKey;
    TWeakObjectPtr<UBehaviorTreeComponent> Tree;
    TWeakObjectPtr<USACombatComponent> Combat;
    TWeakObjectPtr<USASpellcastingComponent> Caster;
    TWeakObjectPtr<AActor> Target;
    FSAActionHandle Action;
    FDelegateHandle FinishedHandle;
    bool bStarting = false;
    bool bSynchronousFinish = false;
    EBTNodeResult::Type SynchronousResult = EBTNodeResult::Failed;
};
