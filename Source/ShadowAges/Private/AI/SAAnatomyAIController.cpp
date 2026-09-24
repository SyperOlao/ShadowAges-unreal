#include "AI/SAAnatomyAIController.h"
#include "Anatomy/SAAnatomyComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "Vitals/SAVitalsComponent.h"

void ASAAnatomyAIController::OnPossess(APawn* ControlledPawn)
{
    UnbindAnatomy();
    Super::OnPossess(ControlledPawn);
    LastInjuredZone = NAME_None;
    if (ControlledPawn)
    {
        Anatomy = ControlledPawn->FindComponentByClass<USAAnatomyComponent>();
        if (Anatomy.IsValid())
        {
            InjuryHandle = Anatomy->OnLimbChangedNative.AddUObject(this, &ASAAnatomyAIController::HandleLimbChanged);
        }
    }
}

void ASAAnatomyAIController::HandleLimbChanged(const FSALimbState& State,
    const FSAAnatomyContactSnapshot& Contact, bool bAlive)
{
    APawn* ControlledPawn = GetPawn();
    const USAVitalsComponent* Vitals = IsValid(ControlledPawn) ? ControlledPawn->FindComponentByClass<USAVitalsComponent>() : nullptr;
    if (!bAlive || !Vitals || !Vitals->IsAlive() || ControlledPawn->IsActorBeingDestroyed()
        || State.Condition != ESALimbCondition::Injured) { return; }
    LastInjuredZone = State.ZoneId;
    if (GetBlackboardComponent() && !InjuredZoneBlackboardKey.IsNone())
    {
        GetBlackboardComponent()->SetValueAsName(InjuredZoneBlackboardKey, State.ZoneId);
    }
    // Blackboard observers can synchronously kill/unpossess the pawn.
    if (GetPawn() == ControlledPawn && IsValid(ControlledPawn) && IsValid(Vitals) && Vitals->IsAlive()
        && !ControlledPawn->IsActorBeingDestroyed()) { OnAnatomyInjury(State, Contact); }
}

void ASAAnatomyAIController::UnbindAnatomy()
{
    if (Anatomy.IsValid()) { Anatomy->OnLimbChangedNative.Remove(InjuryHandle); }
    InjuryHandle.Reset();
    Anatomy.Reset();
}

void ASAAnatomyAIController::OnUnPossess()
{
    UnbindAnatomy();
    LastInjuredZone = NAME_None;
    Super::OnUnPossess();
}

void ASAAnatomyAIController::EndPlay(const EEndPlayReason::Type Reason)
{
    UnbindAnatomy();
    Super::EndPlay(Reason);
}

