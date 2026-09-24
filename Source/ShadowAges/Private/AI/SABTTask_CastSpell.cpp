#include "AI/SABTTask_CastSpell.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "Combat/SACombatComponent.h"
#include "Magic/SASpellcastingComponent.h"
#include "Magic/SASpellTargetingComponent.h"
#include "GameFramework/Pawn.h"
#include "Vitals/SAVitalsComponent.h"

USABTTask_CastSpell::USABTTask_CastSpell()
{
    NodeName = TEXT("SA Cast Prepared Spell");
    bCreateNodeInstance = true; bNotifyTick = true; bNotifyTaskFinished = true;
    TargetActorKey.SelectedKeyName = TEXT("TargetActor");
    TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(USABTTask_CastSpell, TargetActorKey), AActor::StaticClass());
}
void USABTTask_CastSpell::InitializeFromAsset(UBehaviorTree& Asset)
{
    Super::InitializeFromAsset(Asset);
    TargetActorKey.InvalidateResolvedKey();
    if (Asset.BlackboardAsset) { TargetActorKey.ResolveSelectedKey(*Asset.BlackboardAsset); }
}
bool USABTTask_CastSpell::HasTarget() const
{
    auto* Controller = Tree.IsValid() ? Tree->GetAIOwner() : nullptr;
    auto* Blackboard = Tree.IsValid() ? Tree->GetBlackboardComponent() : nullptr;
    if (!Controller || !Blackboard || !Target.IsValid() || !Combat.IsValid()
        || Controller->GetPawn() != Combat->GetOwner() || Target->IsActorBeingDestroyed()
        || Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName) != Target.Get()) { return false; }
    const auto* Vitals = Target->FindComponentByClass<USAVitalsComponent>();
    return Vitals && Vitals->IsAlive();
}
EBTNodeResult::Type USABTTask_CastSpell::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8*)
{
    Cleanup(true);
    auto* Controller = OwnerComp.GetAIOwner();
    APawn* ControlledPawn = Controller ? Controller->GetPawn() : nullptr;
    auto* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!IsValid(ControlledPawn) || !Blackboard || TargetActorKey.IsNone()) { return EBTNodeResult::Failed; }
    Tree = &OwnerComp;
    Combat = ControlledPawn->FindComponentByClass<USACombatComponent>();
    Caster = ControlledPawn->FindComponentByClass<USASpellcastingComponent>();
    Target = Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName));
    if (!HasTarget() || !Caster.IsValid()) { Cleanup(false); return EBTNodeResult::Failed; }
    FSASpellAim Aim = USASpellTargetingComponent::AimFromController(Controller);
    FVector Center, Extent; Target->GetActorBounds(true, Center, Extent);
    Aim.Target = Target;
    Aim.ViewDirection = (Center - Aim.ViewOrigin).GetSafeNormal();
    if (Caster->ValidateCast(Aim) != ESACastFailure::None) { Cleanup(false); return EBTNodeResult::Failed; }
    Action = Combat->ReserveAction(ESAActionKind::Cast);
    if (!Action.IsValid()) { Cleanup(false); return EBTNodeResult::Failed; }
    const auto Started = Action;
    FinishedHandle = Combat->OnActionFinished.AddUObject(this, &USABTTask_CastSpell::HandleFinished);
    bStarting = true;
    const ESACastFailure Failure = Caster->TryStartCast(Started, Aim);
    if (Action != Started) { return EBTNodeResult::Failed; }
    bStarting = false;
    if (bSynchronousFinish)
    {
        const auto Result = SynchronousResult;
        Cleanup(false);
        return Result;
    }
    if (Failure != ESACastFailure::None || !Combat.IsValid() || !Combat->IsCurrentAction(Started))
    { Cleanup(true); return EBTNodeResult::Failed; }
    return EBTNodeResult::InProgress;
}
void USABTTask_CastSpell::TickTask(UBehaviorTreeComponent& OwnerComp, uint8*, float)
{
    const bool bNeedsTarget = Caster.IsValid() && (Caster->GetCastPhase() == ESACastPhase::Windup
        || Caster->GetCastPhase() == ESACastPhase::PreparingRelease);
    if (!Combat.IsValid() || !Combat->IsCurrentAction(Action) || !Caster.IsValid() || (bNeedsTarget && !HasTarget()))
    {
        Cleanup(true);
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
    }
}
void USABTTask_CastSpell::HandleFinished(FSAActionHandle Handle, ESAActionEndReason Reason)
{
    if (!Action.IsValid() || Action != Handle) { return; }
    const auto Result = Reason == ESAActionEndReason::Completed ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
    if (bStarting) { bSynchronousFinish = true; SynchronousResult = Result; return; }
    const auto OwnerTree = Tree;
    Cleanup(false);
    if (OwnerTree.IsValid()) { FinishLatentTask(*OwnerTree.Get(), Result); }
}
void USABTTask_CastSpell::Cleanup(bool bCancel)
{
    const auto Runner = Combat;
    const auto OwnedAction = Action;
    Action = {}; Tree.Reset(); Target.Reset(); Caster.Reset();
    bStarting = false; bSynchronousFinish = false;
    if (Runner.IsValid()) { Runner->OnActionFinished.Remove(FinishedHandle); }
    FinishedHandle.Reset(); Combat.Reset();
    if (bCancel && Runner.IsValid()) { Runner->CancelActionIfCurrent(OwnedAction, ESAActionEndReason::Cancelled); }
}
EBTNodeResult::Type USABTTask_CastSpell::AbortTask(UBehaviorTreeComponent&, uint8*)
{ Cleanup(true); return EBTNodeResult::Aborted; }
void USABTTask_CastSpell::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* Memory, EBTNodeResult::Type Result)
{ Cleanup(true); Super::OnTaskFinished(OwnerComp, Memory, Result); }
