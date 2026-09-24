// Source/ShadowAges/Private/Combat/SAMeleeDamageReceiverComponent.cpp
#include "Combat/SAMeleeDamageReceiverComponent.h"

#include "Combat/SACombatComponent.h"
#include "Anatomy/SAAnatomyComponent.h"
#include "Magic/SAMagicTypes.h"
#include "GameFramework/Actor.h"
#include "Vitals/SAVitalsComponent.h"

USAMeleeDamageReceiverComponent::USAMeleeDamageReceiverComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void USAMeleeDamageReceiverComponent::BeginPlay()
{
    Super::BeginPlay();
    bEndingPlay = false;
    SourceCreditId = FGuid::NewGuid();

    AActor* Owner = GetOwner();
    TArray<USAMeleeDamageReceiverComponent*> Receivers;
    TArray<USAVitalsComponent*> VitalsComponents;
    if (IsValid(Owner))
    {
        Owner->GetComponents<USAMeleeDamageReceiverComponent>(Receivers);
        Owner->GetComponents<USAVitalsComponent>(VitalsComponents);
    }
    bConfigurationValid = Receivers.Num() == 1 && Receivers[0] == this
        && VitalsComponents.Num() == 1 && IsValid(VitalsComponents[0]);
    if (!ensureMsgf(bConfigurationValid,
        TEXT("Melee receiver requires exactly one receiver and one Vitals on its Actor.")))
    {
        CachedVitals.Reset();
        return;
    }
    CachedVitals = VitalsComponents[0];
}

void USAMeleeDamageReceiverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bEndingPlay = true;
    bConfigurationValid = false;
    CachedVitals.Reset();
    Super::EndPlay(EndPlayReason);
}

USAVitalsComponent* USAMeleeDamageReceiverComponent::FindUsableVitals() const
{
    AActor* Owner = GetOwner();
    if (!bConfigurationValid || bEndingPlay || !HasBegunPlay() || !IsValid(Owner))
    {
        return nullptr;
    }
    USAVitalsComponent* Vitals = CachedVitals.Get();
    if (IsValid(Vitals) && Vitals->GetOwner() == Owner && Vitals->HasBegunPlay())
    {
        return Vitals;
    }
    TArray<USAVitalsComponent*> VitalsComponents;
    Owner->GetComponents<USAVitalsComponent>(VitalsComponents);
    if (VitalsComponents.Num() != 1 || !IsValid(VitalsComponents[0])
        || !VitalsComponents[0]->HasBegunPlay())
    {
        return nullptr;
    }
    return VitalsComponents[0];
}

bool USAMeleeDamageReceiverComponent::CanReceiveFrom(const AActor* Source) const
{
    const AActor* Owner = GetOwner();
    if (!bCanReceiveMelee || !IsValid(Source) || !IsValid(Owner)
        || Source == Owner || !HasBegunPlay() || bEndingPlay || !bConfigurationValid)
    {
        return false;
    }
    const USAVitalsComponent* TargetVitals = FindUsableVitals();
    if (!TargetVitals || !TargetVitals->IsAlive())
    {
        return false;
    }

    TArray<USAMeleeDamageReceiverComponent*> SourceReceivers;
    Source->GetComponents<USAMeleeDamageReceiverComponent>(SourceReceivers);
    if (SourceReceivers.Num() != 1 || !IsValid(SourceReceivers[0]))
    {
        return false;
    }
    const USAMeleeDamageReceiverComponent* SourceReceiver = SourceReceivers[0];
    const USAVitalsComponent* SourceVitals = SourceReceiver->FindUsableVitals();
    if (!SourceVitals || !SourceVitals->IsAlive() || !SourceReceiver->SourceCreditId.IsValid())
    {
        return false;
    }
    return FactionId.IsNone() || SourceReceiver->FactionId.IsNone()
        || FactionId != SourceReceiver->FactionId;
}

FSADamageResult USAMeleeDamageReceiverComponent::ResolveMeleeHit(const FSAMeleeHitRequest& Request)
{
    FSADamageResult Result;
    const FSAHitContext& Context = Request.Context;
    AActor* Source = Context.SourceActor.Get();
    if (Context.TargetActor.Get() != GetOwner() || !Context.Playback.IsValid()
        || Context.WindowSerial < 0 || !Context.SourceCreditId.IsValid()
        || !FMath::IsFinite(Request.ProposedDamage) || Request.ProposedDamage < 0.0f
        || !FMath::IsFinite(Request.ProposedPoiseDamage) || Request.ProposedPoiseDamage < 0.0f
        || !FMath::IsFinite(Context.FrameAlpha) || Context.FrameAlpha < 0.0 || Context.FrameAlpha > 1.0
        || !CanReceiveFrom(Source))
    {
        return Result;
    }

    TArray<USACombatComponent*> SourceCombats;
    TArray<USAMeleeDamageReceiverComponent*> SourceReceivers;
    Source->GetComponents<USACombatComponent>(SourceCombats);
    Source->GetComponents<USAMeleeDamageReceiverComponent>(SourceReceivers);
    if (SourceCombats.Num() != 1 || !IsValid(SourceCombats[0])
        || !SourceCombats[0]->IsCurrentPlayback(Context.Playback)
        || SourceReceivers.Num() != 1 || !IsValid(SourceReceivers[0])
        || SourceReceivers[0]->GetSourceCreditId() != Context.SourceCreditId)
    {
        return Result;
    }

    USAVitalsComponent* Vitals = FindUsableVitals();
    if (!Vitals || !Vitals->IsAlive())
    {
        return Result;
    }
    CachedVitals = Vitals;
    return CommitIncomingDamage(Context, Request.ProposedDamage, false);
}

FSAIncomingDamageDecision USAMeleeDamageReceiverComponent::EvaluateIncomingDamage_Implementation(
    const FSAHitContext& Context, float ProposedDamage, bool bIsSpell) const
{
    FSAIncomingDamageDecision Decision;
    Decision.HealthDamage = ProposedDamage;
    return Decision;
}

FSADamageResult USAMeleeDamageReceiverComponent::CommitIncomingDamage(
    const FSAHitContext& Context, float ProposedDamage, bool bIsSpell)
{
    const auto Decision = EvaluateIncomingDamage(Context, ProposedDamage, bIsSpell);
    USAVitalsComponent* Vitals = FindUsableVitals();
    FSADamageResult Result;
    if (!IsValid(this) || !Vitals || !Vitals->IsAlive() || GetOwner()->IsActorBeingDestroyed()
        || !Decision.bAccepted || !FMath::IsFinite(Decision.HealthDamage) || Decision.HealthDamage < 0.f) { return Result; }
    Result.bAccepted = true;
    Result.bBlocked = Decision.bBlocked;
    Result.bParried = Decision.bParried;
    if (Result.bBlocked || Result.bParried) { return Result; }
    TWeakObjectPtr<USAAnatomyComponent> Anatomy = GetOwner()->FindComponentByClass<USAAnatomyComponent>();
    const FSAAnatomyContactSnapshot Snapshot = Anatomy.IsValid()
        ? Anatomy->CaptureContact(Context) : FSAAnatomyContactSnapshot();
    Result = Vitals->CommitDamageWithoutEvents(Decision.HealthDamage, Context);
    FSALimbChangeBatch Batch;
    if (Anatomy.IsValid()) { Batch = Anatomy->ConsumeResolvedDamageStateOnly(Result, Snapshot); }
    Vitals->PublishCommittedDamage(Result);
    if (Anatomy.IsValid()) { Anatomy->DeliverLimbChanges(MoveTemp(Batch)); }
    return Result;
}

FGuid USAMeleeDamageReceiverComponent::GetSourceCreditId() const
{
    return SourceCreditId;
}

FName USAMeleeDamageReceiverComponent::GetFactionId() const
{
    return FactionId;
}

bool USAMeleeDamageReceiverComponent::CanReceiveSpell(const FSASpellPayload& Payload) const
{
    const USAVitalsComponent* Vitals = FindUsableVitals();
    if (!bCanReceiveSpells || !Vitals || !Vitals->IsAlive() || GetOwner()->IsActorBeingDestroyed()
        || !Payload.Context.SourceCreditId.IsValid()) { return false; }
    const bool bSelf = Payload.Context.SourceCreditId == SourceCreditId || Payload.Context.SourceActor.Get() == GetOwner();
    const bool bFriendly = bSelf || (!FactionId.IsNone() && FactionId == Payload.SourceFaction);
    switch (Payload.TargetPolicy)
    {
        case ESASpellTargetPolicy::Hostile: return !bFriendly;
        case ESASpellTargetPolicy::Friendly: return bFriendly;
        case ESASpellTargetPolicy::Any: return true;
        case ESASpellTargetPolicy::Self: return bSelf;
        default: return false;
    }
}

FSADamageResult USAMeleeDamageReceiverComponent::ResolveSpellHit(const FSASpellPayload& Payload)
{
    if (!FMath::IsFinite(Payload.Damage) || Payload.Damage < 0.f || !CanReceiveSpell(Payload)) { return {}; }
    FSAHitContext Context = Payload.Context;
    Context.TargetActor = GetOwner();
    return CommitIncomingDamage(Context, Payload.Damage, true);
}
