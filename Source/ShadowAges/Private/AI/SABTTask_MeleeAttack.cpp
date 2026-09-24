#include "AI/SABTTask_MeleeAttack.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKey.h"
#include "BehaviorTree/BlackboardData.h"
#include "Combat/SACombatComponent.h"
#include "Combat/SAMeleeRequest.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Vitals/SAVitalsComponent.h"

USABTTask_MeleeAttack::USABTTask_MeleeAttack(const FObjectInitializer& ObjectInitializer)
{
	NodeName = TEXT("SA Melee Attack");
	bCreateNodeInstance = true;
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	TargetActorKey.SelectedKeyName = TEXT("TargetActor");
	TargetActorKey.AddObjectFilter(this,
	                               GET_MEMBER_NAME_CHECKED(USABTTask_MeleeAttack, TargetActorKey),
	                               AActor::StaticClass());
}

void USABTTask_MeleeAttack::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	TargetActorKey.InvalidateResolvedKey();
	if (Asset.BlackboardAsset)
	{
		TargetActorKey.ResolveSelectedKey(*Asset.BlackboardAsset);
	}
}

EBTNodeResult::Type USABTTask_MeleeAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	 Cleanup(true);
    AAIController* Controller = OwnerComp.GetAIOwner();
    APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!IsValid(Pawn) || !IsValid(Blackboard)
        || TargetActorKey.GetSelectedKeyID() == FBlackboard::InvalidKey)
    {
        return EBTNodeResult::Failed;
    }
    RunningOwner = &OwnerComp;
    Combat = Pawn->FindComponentByClass<USACombatComponent>();
    Target = Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName));
    if (!HasValidTarget() || !IsTargetInReach())
    {
        Cleanup(false);
        return EBTNodeResult::Failed;
    }

    if (bStopMovementBeforeAttack) Controller->StopMovement();
    if (!HasValidTarget() || !IsTargetInReach())
    {
        Cleanup(false);
        return EBTNodeResult::Failed;
    }
    PlannedSteps = FMath::RandRange(1, FMath::Clamp(MaxPlannedSteps, 1, 3));
    USACombatComponent* Runner = Combat.Get();
    Action = Runner->ReserveAction(ESAActionKind::Melee);
    if (!Action.IsValid())
    {
        Cleanup(false);
        return EBTNodeResult::Failed;
    }
    const FSAActionHandle StartedAction = Action;
    FinishedSubscription = Runner->OnActionFinished.AddUObject(
        this, &ThisClass::HandleActionFinished);
    StepSubscription = Runner->OnMeleeStepStarted.AddUObject(
        this, &ThisClass::HandleStepStarted);
    AcceptSubscription = Runner->OnComboAcceptOpened.AddUObject(
        this, &ThisClass::HandleComboAcceptOpened);

    FSAMeleeRequest Request;
    Request.Moveset = Runner->GetMoveset();
    bStarting = true;
    const ESACombatRequestResult StartResult = Runner->TryStartMelee(StartedAction, Request);
    if (Action != StartedAction) return EBTNodeResult::Failed;
    bStarting = false;
    if (bHasPendingResult)
    {
        const EBTNodeResult::Type Result = PendingResult;
        Cleanup(false);
        return Result;
    }
    Runner = Combat.Get();
    if (StartResult != ESACombatRequestResult::Started || !Runner
        || !Runner->IsCurrentAction(StartedAction))
    {
        Cleanup(true);
        return EBTNodeResult::Failed;
    }
    TaskDeadline = Runner->GetActionDeadline(StartedAction) + 0.25;
    return EBTNodeResult::InProgress;
}

void USABTTask_MeleeAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    USACombatComponent* Runner = Combat.Get();
    if (!Runner || !Action.IsValid() || !Runner->IsCurrentAction(Action))
    {
        FinishTask(EBTNodeResult::Failed);
        return;
    }
    UWorld* World = OwnerComp.GetWorld();
    if (!World || World->GetTimeSeconds() >= TaskDeadline)
    {
        const FSAActionHandle ExpiredAction = Action;
        Runner->CancelActionIfCurrent(ExpiredAction, ESAActionEndReason::TimeOut);
        if (Action == ExpiredAction) FinishTask(EBTNodeResult::Failed);
        return;
    }
    if (!HasValidTarget())
    {
        const FSAActionHandle CancelledAction = Action;
        Runner->CancelActionIfCurrent(CancelledAction, ESAActionEndReason::Interrupted);
        if (Action == CancelledAction) FinishTask(EBTNodeResult::Failed);
    }
}

EBTNodeResult::Type USABTTask_MeleeAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    Cleanup(true);
    return EBTNodeResult::Aborted;
}

void USABTTask_MeleeAttack::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
    EBTNodeResult::Type TaskResult)
{
    Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
    Cleanup(true);
}

bool USABTTask_MeleeAttack::HasValidTarget() const
{
    UBehaviorTreeComponent* Owner = RunningOwner.Get();
    const USACombatComponent* Runner = Combat.Get();
    const AActor* TargetActor = Target.Get();
    AAIController* Controller = Owner ? Owner->GetAIOwner() : nullptr;
    const AActor* Self = Runner ? Runner->GetOwner() : nullptr;
    UBlackboardComponent* Blackboard = Owner ? Owner->GetBlackboardComponent() : nullptr;
    if (!IsValid(Controller) || !IsValid(Self) || Controller->GetPawn() != Self
        || !TargetActor || TargetActor == Self || !IsValid(Blackboard)
        || Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName) != TargetActor)
    {
        return false;
    }
    const USAVitalsComponent* TargetVitals = TargetActor->FindComponentByClass<USAVitalsComponent>();
    return IsValid(TargetVitals) && TargetVitals->IsAlive();
}

bool USABTTask_MeleeAttack::IsTargetInReach() const
{
    const USACombatComponent* Runner = Combat.Get();
    const AActor* Self = Runner ? Runner->GetOwner() : nullptr;
    const AActor* TargetActor = Target.Get();
    if (!IsValid(Self) || !TargetActor || !FMath::IsFinite(AttackRange) || AttackRange <= 0.0f
        || !FMath::IsFinite(FacingHalfAngleDegrees)
        || FacingHalfAngleDegrees < 0.0f || FacingHalfAngleDegrees > 180.0f)
    {
        return false;
    }
    const FVector Delta = TargetActor->GetActorLocation() - Self->GetActorLocation();
    const double RangeSquared = static_cast<double>(AttackRange) * AttackRange;
    if (Delta.ContainsNaN() || !FMath::IsFinite(Delta.SizeSquared())
        || Delta.SizeSquared() > RangeSquared)
    {
        return false;
    }
    const FVector Direction = Delta.GetSafeNormal2D();
    const FVector Forward = Self->GetActorForwardVector().GetSafeNormal2D();
    if (Forward.ContainsNaN() || Forward.IsNearlyZero()) return false;
    if (Direction.IsNearlyZero()) return true;
    const double Threshold = FMath::Cos(FMath::DegreesToRadians(FacingHalfAngleDegrees));
    return FVector::DotProduct(Forward, Direction) >= Threshold;
}

void USABTTask_MeleeAttack::HandleStepStarted(FSAMeleePlaybackKey Key)
{
    const USACombatComponent* Runner = Combat.Get();
    if (!Runner || Key.Action != Action || !Runner->IsCurrentPlayback(Key)
        || Key.Generation <= LastStartedGeneration)
    {
        return;
    }
    LastStartedGeneration = Key.Generation;
    ++StepsStarted;
}

void USABTTask_MeleeAttack::HandleComboAcceptOpened(FSAMeleePlaybackKey Key)
{
    USACombatComponent* Runner = Combat.Get();
    if (!Runner || Key.Action != Action || !Runner->IsCurrentPlayback(Key)
        || Key.Generation == LastAcceptedGeneration)
    {
        return;
    }
    LastAcceptedGeneration = Key.Generation;
    if (StepsStarted > 0 && StepsStarted < PlannedSteps && HasValidTarget() && IsTargetInReach())
    {
        Runner->RequestComboInput(Action);
    }
}

void USABTTask_MeleeAttack::HandleActionFinished(FSAActionHandle Handle, ESAActionEndReason Reason)
{
    if (!Action.IsValid() || Handle != Action) return;
    const EBTNodeResult::Type Result = Reason == ESAActionEndReason::Completed
        ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
    if (bStarting)
    {
        PendingResult = Result;
        bHasPendingResult = true;
        return;
    }
    FinishTask(Result);
}

void USABTTask_MeleeAttack::FinishTask(EBTNodeResult::Type Result)
{
    UBehaviorTreeComponent* Owner = RunningOwner.Get();
    Cleanup(false);
    if (Owner) FinishLatentTask(*Owner, Result);
}

void USABTTask_MeleeAttack::Cleanup(bool bCancelOwnedAction)
{
    const TWeakObjectPtr<USACombatComponent> PreviousCombat = Combat;
    const FSAActionHandle PreviousAction = Action;
    if (USACombatComponent* Runner = PreviousCombat.Get())
    {
        Runner->OnActionFinished.Remove(FinishedSubscription);
        Runner->OnMeleeStepStarted.Remove(StepSubscription);
        Runner->OnComboAcceptOpened.Remove(AcceptSubscription);
    }
    FinishedSubscription.Reset();
    StepSubscription.Reset();
    AcceptSubscription.Reset();
    RunningOwner.Reset();
    Combat.Reset();
    Target.Reset();
    Action = {};
    PlannedSteps = 0;
    StepsStarted = 0;
    LastStartedGeneration = 0;
    LastAcceptedGeneration = 0;
    TaskDeadline = 0.0;
    bStarting = false;
    bHasPendingResult = false;
    PendingResult = EBTNodeResult::Failed;
    if (bCancelOwnedAction)
    {
        if (USACombatComponent* Runner = PreviousCombat.Get())
        {
            Runner->CancelActionIfCurrent(PreviousAction, ESAActionEndReason::Interrupted);
        }
    }
}
