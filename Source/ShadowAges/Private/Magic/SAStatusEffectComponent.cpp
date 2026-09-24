#include "Magic/SAStatusEffectComponent.h"
#include "Magic/SASpellDamage.h"
#include "Combat/SACombatComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Vitals/SAVitalsComponent.h"

USAStatusEffectComponent::USAStatusEffectComponent() { PrimaryComponentTick.bCanEverTick = false; }
void USAStatusEffectComponent::BeginPlay()
{
    Super::BeginPlay();
    Vitals = GetOwner()->FindComponentByClass<USAVitalsComponent>();
    if (Vitals.IsValid()) { DeathHandle = Vitals->OnDied.AddUObject(this, &USAStatusEffectComponent::HandleDeath); }
    if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        BaseWalkSpeed = Character->GetCharacterMovement()->MaxWalkSpeed;
        BaseCrouchedSpeed = Character->GetCharacterMovement()->MaxWalkSpeedCrouched;
    }
}
bool USAStatusEffectComponent::CanOperate() const
{
    return !bEndingPlay && !bClearing && HasBegunPlay() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed()
        && Vitals.IsValid() && Vitals->IsAlive();
}
int32 USAStatusEffectComponent::FindEffect(FGuid Id) const
{ return ActiveEffects.IndexOfByPredicate([Id](const FSAActiveEffect& E) { return E.Id == Id; }); }

FGuid USAStatusEffectComponent::ApplyEffect(const FSAEffectSpec& Spec, const FSASpellPayload& Source)
{
    FString Error;
    // Settle the old grid before a new hit can refresh an already expired record.
    if (CanOperate() && !bAdvancing) { AdvanceEffects(GetWorld()->GetTimeSeconds()); }
    if (!CanOperate() || !Spec.Validate(Error) || !Source.Context.SourceCreditId.IsValid()
        || EffectTickDebt > 0
        || Spec.Chance <= 0.f || (Spec.Chance < 1.f && FMath::FRand() >= Spec.Chance)) { return {}; }
    if (Spec.Lifetime == ESAEffectLifetime::Instant)
    {
        const FGuid AppliedId = FGuid::NewGuid();
        ExecuteEffect(Spec, Source, 1);
        return AppliedId;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    int32 Existing = ActiveEffects.IndexOfByPredicate([&](const FSAActiveEffect& E)
    {
        return E.Spec.EffectTag == Spec.EffectTag && E.Spec.bGroupBySource == Spec.bGroupBySource
            && (!Spec.bGroupBySource || E.Source.Context.SourceCreditId == Source.Context.SourceCreditId);
    });
    if (Existing != INDEX_NONE && Spec.StackRule == ESAEffectStackRule::Ignore) { return ActiveEffects[Existing].Id; }
    if (Existing != INDEX_NONE && Spec.StackRule == ESAEffectStackRule::Replace)
    {
        const FGuid OldId = ActiveEffects[Existing].Id;
        // Remove before notification; a reentrant replacement is allowed to win.
        RemoveEffect(OldId, ESAEffectRemovalReason::Replaced);
        if (!CanOperate()) { return {}; }
        Existing = ActiveEffects.IndexOfByPredicate([&](const FSAActiveEffect& E)
        { return E.Spec.EffectTag == Spec.EffectTag && (!Spec.bGroupBySource || E.Source.Context.SourceCreditId == Source.Context.SourceCreditId); });
        if (Existing != INDEX_NONE) { return ActiveEffects[Existing].Id; }
    }
    if (Existing != INDEX_NONE)
    {
        FSAActiveEffect& E = ActiveEffects[Existing];
        // A tag denotes one semantic kind/grid. Use Replace to change those parameters.
        if (E.Spec.Kind != Spec.Kind || E.Spec.Period != Spec.Period) { return {}; }
        E.ExpiresAt = Now + Spec.Duration;
        if (Spec.StackRule == ESAEffectStackRule::AddStacks) { E.Stacks = FMath::Min(E.Stacks + 1, E.Spec.MaxStacks); }
        const FSAActiveEffect Snapshot = E;
        RefreshMovement();
        ScheduleNext();
        OnEffectChanged.Broadcast(Snapshot);
        return Snapshot.Id;
    }
    if (ActiveEffects.Num() >= 64) { return {}; }
    FSAActiveEffect E;
    E.Id = FGuid::NewGuid(); E.Spec = Spec; E.Source = Source;
    E.Source.Effects.Reset();
    E.AppliedAt = Now; E.ExpiresAt = Now + Spec.Duration;
    ActiveEffects.Add(E);
    RefreshMovement();
    ScheduleNext();
    // Restrictions are committed before action cancellation and external callbacks.
    if (Spec.Kind == ESAEffectKind::Silence || Spec.Kind == ESAEffectKind::Disarm)
    {
        if (auto* Combat = GetOwner()->FindComponentByClass<USACombatComponent>())
        {
            const ESAActionKind Kind = Combat->GetCurrentActionKind();
            if ((Spec.Kind == ESAEffectKind::Silence && Kind == ESAActionKind::Cast)
                || (Spec.Kind == ESAEffectKind::Disarm && Kind == ESAActionKind::Melee))
            { Combat->CancelActionIfCurrent(Combat->GetCurrentAction(), ESAActionEndReason::Interrupted); }
        }
    }
    if (CanOperate() && FindEffect(E.Id) != INDEX_NONE) { OnEffectChanged.Broadcast(E); }
    return E.Id;
}

void USAStatusEffectComponent::ExecuteEffect(const FSAEffectSpec& Spec, FSASpellPayload Source, int32 Stacks)
{
    if (!CanOperate()) { return; }
    if (Spec.Kind == ESAEffectKind::RestoreMana) { Vitals->RestoreMana(Spec.Magnitude * Stacks); }
    else if (Spec.Kind == ESAEffectKind::Damage)
    {
        Source.Damage = Spec.Magnitude * Stacks;
        Source.Effects.Reset();
        Source.Context.TargetActor = GetOwner();
        SA::ApplySpellImpact(GetOwner(), MoveTemp(Source));
    }
}

bool USAStatusEffectComponent::RemoveEffect(FGuid Id, ESAEffectRemovalReason Reason)
{
    const int32 Index = FindEffect(Id);
    if (Index == INDEX_NONE) { return false; }
    ActiveEffects.RemoveAt(Index);
    RefreshMovement();
    ScheduleNext();
    OnEffectRemoved.Broadcast(Id, Reason);
    return true;
}
void USAStatusEffectComponent::ClearEffects(ESAEffectRemovalReason Reason)
{
    if (bClearing) { return; }
    TGuardValue<bool> Guard(bClearing, true);
    const TArray<FSAActiveEffect> Removed = MoveTemp(ActiveEffects);
    ActiveEffects.Reset();
    RefreshMovement();
    if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(WakeTimer); }
    EffectTickDebt = 0;
    for (const auto& E : Removed) { OnEffectRemoved.Broadcast(E.Id, Reason); }
}
void USAStatusEffectComponent::HandleDeath() { ClearEffects(ESAEffectRemovalReason::OwnerDied); }
void USAStatusEffectComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEndingPlay = true;
    if (Vitals.IsValid()) { Vitals->OnDied.Remove(DeathHandle); }
    ClearEffects(ESAEffectRemovalReason::EndPlay);
    Super::EndPlay(Reason);
}

float USAStatusEffectComponent::GetMovementMultiplier() const
{
    float Multiplier = 1.f;
    for (const auto& E : ActiveEffects)
    { if (E.Spec.Kind == ESAEffectKind::Slow) { Multiplier *= FMath::Pow(1.f - E.Spec.Magnitude, E.Stacks); } }
    return FMath::Clamp(Multiplier, 0.f, 1.f);
}
bool USAStatusEffectComponent::IsSilenced() const
{ return ActiveEffects.ContainsByPredicate([](const FSAActiveEffect& E) { return E.Spec.Kind == ESAEffectKind::Silence; }); }
bool USAStatusEffectComponent::IsDisarmed() const
{ return ActiveEffects.ContainsByPredicate([](const FSAActiveEffect& E) { return E.Spec.Kind == ESAEffectKind::Disarm; }); }

void USAStatusEffectComponent::SetBaseMovementSpeeds(float WalkSpeed, float CrouchedSpeed)
{
    if (!FMath::IsFinite(WalkSpeed) || !FMath::IsFinite(CrouchedSpeed) || WalkSpeed < 0.f || CrouchedSpeed < 0.f) { return; }
    BaseWalkSpeed = WalkSpeed; BaseCrouchedSpeed = CrouchedSpeed;
    RefreshMovement();
}
void USAStatusEffectComponent::RefreshMovement()
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!IsValid(Character) || Character->IsActorBeingDestroyed()) { return; }
    float Scale = GetMovementMultiplier();
    if (const auto* Combat = Character->FindComponentByClass<USACombatComponent>())
    { Scale *= Combat->GetCastMovementScale(); }
    Character->GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * Scale;
    Character->GetCharacterMovement()->MaxWalkSpeedCrouched = BaseCrouchedSpeed * Scale;
}

void USAStatusEffectComponent::ScheduleNext()
{
    if (!GetWorld() || bAdvancing) { return; }
    auto& Timers = GetWorld()->GetTimerManager();
    Timers.ClearTimer(WakeTimer);
    if (!CanOperate() || ActiveEffects.IsEmpty()) { return; }
    double Next = TNumericLimits<double>::Max();
    for (const auto& E : ActiveEffects)
    {
        const double Due = E.Spec.Period > 0.f && E.NextTickAt() <= E.ExpiresAt + 1.e-6 ? E.NextTickAt() : E.ExpiresAt;
        Next = FMath::Min(Next, Due);
    }
    const double Delay = Next - GetWorld()->GetTimeSeconds();
    if (Delay <= 0.) { WakeTimer = Timers.SetTimerForNextTick(this, &USAStatusEffectComponent::WakeScheduler); }
    else { Timers.SetTimer(WakeTimer, this, &USAStatusEffectComponent::WakeScheduler, float(FMath::Max(.0001, Delay)), false); }
}
void USAStatusEffectComponent::WakeScheduler() { AdvanceEffects(GetWorld()->GetTimeSeconds()); }
void USAStatusEffectComponent::AdvanceEffects(double Now)
{
    if (bAdvancing || !CanOperate() || !FMath::IsFinite(Now)) { return; }
    {
        TGuardValue<bool> Guard(bAdvancing, true);
        // Stable chronological order, including expirations. Never collapse ticks after a hitch.
        for (int32 Budget = 0; Budget < 16 && CanOperate(); ++Budget)
        {
            int32 Chosen = INDEX_NONE;
            double ChosenTime = TNumericLimits<double>::Max();
            for (int32 I = 0; I < ActiveEffects.Num(); ++I)
            {
                const auto& E = ActiveEffects[I];
                const double Due = E.Spec.Period > 0.f && E.NextTickAt() <= E.ExpiresAt + 1.e-6 ? E.NextTickAt() : E.ExpiresAt;
                if (Due <= Now + 1.e-6 && (Due < ChosenTime || (Due == ChosenTime
                    && (Chosen == INDEX_NONE || E.Id < ActiveEffects[Chosen].Id)))) { Chosen = I; ChosenTime = Due; }
            }
            if (Chosen == INDEX_NONE) { break; }
            const FSAActiveEffect E = ActiveEffects[Chosen];
            if (E.Spec.Period > 0.f && E.NextTickAt() <= E.ExpiresAt + 1.e-6)
            {
                ++ActiveEffects[Chosen].NextTickIndex; // Advance before damage callbacks can clear/replace records.
                ExecuteEffect(E.Spec, E.Source, E.Stacks);
            }
            else { RemoveEffect(E.Id, ESAEffectRemovalReason::Expired); }
        }
        EffectTickDebt = 0;
        for (const auto& E : ActiveEffects)
        {
            if (E.Spec.Period > 0.f && E.NextTickAt() <= FMath::Min(Now, E.ExpiresAt) + 1.e-6)
            { EffectTickDebt += 1 + FMath::FloorToInt((FMath::Min(Now, E.ExpiresAt) - E.NextTickAt() + 1.e-6) / E.Spec.Period); }
        }
    }
    ScheduleNext();
}
