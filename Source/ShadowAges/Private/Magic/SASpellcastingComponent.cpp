#include "Magic/SASpellcastingComponent.h"
#include "Magic/SASpellDefinition.h"
#include "Magic/SASpellTargetingComponent.h"
#include "Magic/SASpellDamage.h"
#include "Magic/SAProjectile.h"
#include "Magic/SAStatusEffectComponent.h"
#include "Combat/SACombatComponent.h"
#include "Combat/SAMeleeDamageReceiverComponent.h"
#include "Vitals/SAVitalsComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AlphaBlend.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

USASpellcastingComponent::USASpellcastingComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}
void USASpellcastingComponent::BeginPlay()
{
    Super::BeginPlay();
    Combat = GetOwner()->FindComponentByClass<USACombatComponent>();
    Vitals = GetOwner()->FindComponentByClass<USAVitalsComponent>();
}
bool USASpellcastingComponent::ConfigureTargeting(USASpellTargetingComponent* Targeting, USkeletalMeshComponent* MontageMesh)
{
    if (ActiveAction.IsValid() || !IsValid(Targeting) || Targeting->GetOwner() != GetOwner()
        || (MontageMesh && MontageMesh->GetOwner() != GetOwner())) { return false; }
    AimSource = Targeting; CastMesh = MontageMesh;
    return true;
}

void USASpellcastingComponent::SetPreparedSpell(USASpellDefinition* Spell)
{
    if (bEndingPlay) { return; }
    const uint64 Generation = ++LoadGeneration;
    auto OldLoad = MoveTemp(AssetLoad);
    PreparedSpell = nullptr; LoadedMontage = nullptr; LoadedProjectileClass = nullptr;
    if (OldLoad) { OldLoad->CancelHandle(); }
    if (ActiveAction.IsValid()) { Finish(ActiveAction, ESAActionEndReason::Cancelled); }
    // Finish may reenter equipment selection. Latest request wins.
    if (bEndingPlay || LoadGeneration != Generation) { return; }
    PreparedSpell = Spell;
    if (!IsValid(Spell)) { return; }
    TArray<FSoftObjectPath> Paths;
    if (!Spell->ProjectileClass.IsNull()) { Paths.Add(Spell->ProjectileClass.ToSoftObjectPath()); }
    if (!Spell->CastMontage.IsNull()) { Paths.Add(Spell->CastMontage.ToSoftObjectPath()); }
    RefreshLoadedAssets(Generation);
    if (Paths.IsEmpty() || AreAssetsReady()) { return; }
    const TWeakObjectPtr<USASpellcastingComponent> Self = this;
    AssetLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda([Self, Generation]()
    { if (Self.IsValid()) { Self->RefreshLoadedAssets(Generation); } }));
}
void USASpellcastingComponent::RefreshLoadedAssets(uint64 Generation)
{
    if (bEndingPlay || Generation != LoadGeneration || !IsValid(PreparedSpell)) { return; }
    LoadedProjectileClass = PreparedSpell->ProjectileClass.Get();
    LoadedMontage = PreparedSpell->CastMontage.Get();
}
bool USASpellcastingComponent::AreAssetsReady() const
{
    return IsValid(PreparedSpell) && (PreparedSpell->Delivery != ESASpellDelivery::Projectile
        || (IsValid(LoadedProjectileClass.Get()) && LoadedProjectileClass.Get() == PreparedSpell->ProjectileClass.Get()))
        && (PreparedSpell->CastMontage.IsNull() || (IsValid(LoadedMontage) && LoadedMontage == PreparedSpell->CastMontage.Get()));
}
float USASpellcastingComponent::GetCooldownRemaining(FGameplayTag Group) const
{
    const double* Until = CooldownUntil.Find(Group);
    return Until && GetWorld() ? float(FMath::Max(0., *Until - GetWorld()->GetTimeSeconds())) : 0.f;
}
ESACastFailure USASpellcastingComponent::ValidateCast(const FSASpellAim& Aim, bool bOwnReservation) const
{
    if (bEndingPlay || !HasBegunPlay() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed()
        || !Combat.IsValid() || !Vitals.IsValid() || !IsValid(AimSource)) { return ESACastFailure::MissingDependencies; }
    if (!Vitals->IsAlive()) { return ESACastFailure::Dead; }
    if (!bOwnReservation && Combat->IsBusy()) { return ESACastFailure::Busy; }
    if (const auto* Effects = GetOwner()->FindComponentByClass<USAStatusEffectComponent>())
    { if (Effects->IsSilenced()) { return ESACastFailure::Silenced; } }
    if (!IsValid(PreparedSpell)) { return ESACastFailure::InvalidData; }
    if (uint8(PreparedSpell->Delivery) > uint8(ESASpellDelivery::Ray)) { return ESACastFailure::UnsupportedDelivery; }
    FString Error;
    if (!PreparedSpell->Validate(Error)) { return ESACastFailure::InvalidData; }
    if (!AreAssetsReady()) { return ESACastFailure::AssetsNotReady; }
    if (PreparedSpell->Delivery == ESASpellDelivery::Projectile && LoadedProjectileClass->HasAnyClassFlags(CLASS_Abstract))
    { return ESACastFailure::InvalidData; }
    if (!AimSource->HasValidMuzzle(*PreparedSpell)) { return ESACastFailure::InvalidData; }
    if (LoadedMontage && (!IsValid(CastMesh) || !CastMesh->GetAnimInstance()
        || !FMath::IsFinite(LoadedMontage->RateScale) || LoadedMontage->RateScale <= 0.f)) { return ESACastFailure::InvalidData; }
    if (Aim.ViewOrigin.ContainsNaN() || Aim.ViewDirection.ContainsNaN() || Aim.ViewDirection.IsNearlyZero()) { return ESACastFailure::InvalidAim; }
    const auto* Receiver = GetOwner()->FindComponentByClass<USAMeleeDamageReceiverComponent>();
    if (!Receiver || !Receiver->GetSourceCreditId().IsValid()) { return ESACastFailure::MissingDependencies; }
    if (Vitals->GetAvailableMana() < PreparedSpell->ManaCost) { return ESACastFailure::NotEnoughMana; }
    if (GetCooldownRemaining(PreparedSpell->CooldownGroup) > 0.f) { return ESACastFailure::Cooldown; }
    return ESACastFailure::None;
}
ESACastFailure USASpellcastingComponent::RequestCast(const FSASpellAim& Aim, FSAActionHandle& OutAction)
{
    OutAction = {};
    const auto Failure = ValidateCast(Aim);
    LastFailure = Failure;
    if (Failure != ESACastFailure::None) { return Failure; }
    OutAction = Combat->ReserveAction(ESAActionKind::Cast);
    return OutAction.IsValid() ? TryStartCast(OutAction, Aim) : ESACastFailure::Busy;
}
ESACastFailure USASpellcastingComponent::TryStartCast(FSAActionHandle Handle, const FSASpellAim& Aim)
{
    if (!Combat.IsValid() || !Combat->IsReservedAction(Handle, ESAActionKind::Cast)) { return ESACastFailure::InvalidHandle; }
    const auto Failure = ValidateCast(Aim, true);
    LastFailure = Failure;
    if (Failure != ESACastFailure::None) { Combat->FinishActionIfCurrent(Handle, ESAActionEndReason::Failed); return Failure; }
    ActiveAction = Handle; ActiveAim = Aim; Phase = ESACastPhase::Windup;
    // Snapshot authored numbers; subsequent asset edits cannot change the paid release.
    ActiveSpell = DuplicateObject<USASpellDefinition>(PreparedSpell, this,
        MakeUniqueObjectName(this, USASpellDefinition::StaticClass(), TEXT("CastSnapshot")));
    ActiveProjectileClass = LoadedProjectileClass;
    ActiveMontage = LoadedMontage;
    if (!Vitals->TryReserveMana(Handle, ActiveSpell->ManaCost, ManaReservation)
        || !Combat->ConfigureCastAction(Handle, this, ActiveSpell->WindupSeconds + ActiveSpell->RecoverySeconds, ActiveSpell->WindupPolicy)
        || !Combat->BeginReservedAction(Handle))
    {
        CancelExecutor(Handle, ESAActionEndReason::Failed);
        Combat->FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
        return ESACastFailure::NotEnoughMana;
    }
    PhaseEnd = GetWorld()->GetTimeSeconds() + ActiveSpell->WindupSeconds;
    SetComponentTickEnabled(true);
    if (ActiveMontage)
    {
        UAnimInstance* Anim = CastMesh->GetAnimInstance();
        OwnedAnim = Anim;
        const float Played = Anim->Montage_Play(ActiveMontage, 1.f);
        if (!IsCurrent(Handle)) { return ESACastFailure::Interrupted; }
        FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(ActiveMontage);
        if (Played <= 0.f || !Instance) { Finish(Handle, ESAActionEndReason::Failed); return ESACastFailure::InvalidData; }
        MontageInstanceId = Instance->GetInstanceID();
        Instance->OnMontageEnded.BindUObject(this, &USASpellcastingComponent::HandleMontageEnded, Handle);
        Instance->OnMontageBlendingOutStarted.BindUObject(this, &USASpellcastingComponent::HandleMontageBlend, Handle);
    }
    OnWindup.Broadcast(Handle);
    return ESACastFailure::None;
}
bool USASpellcastingComponent::IsCurrent(FSAActionHandle Handle) const
{
    return !bEndingPlay && ActiveAction == Handle && Handle.IsValid() && Combat.IsValid()
        && Combat->IsCurrentAction(Handle) && Vitals.IsValid() && Vitals->IsAlive()
        && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed();
}
void USASpellcastingComponent::Finish(FSAActionHandle Handle, ESAActionEndReason Reason)
{
    if (Combat.IsValid()) { Combat->FinishActionIfCurrent(Handle, Reason); }
    if (ActiveAction == Handle) { CancelExecutor(Handle, Reason); }
}
void USASpellcastingComponent::ReleaseCast(FSAActionHandle Handle)
{
    if (!IsCurrent(Handle) || Phase != ESACastPhase::Windup) { return; }
    Phase = ESACastPhase::PreparingRelease;
    const auto* Receiver = GetOwner()->FindComponentByClass<USAMeleeDamageReceiverComponent>();
    if (!Receiver || !IsValid(AimSource)) { Finish(Handle, ESAActionEndReason::Failed); return; }
    FSASpellPayload Payload;
    Payload.Context.Playback.Action = Handle;
    Payload.Context.SourceActor = GetOwner();
    if (const APawn* PawnOwner = Cast<APawn>(GetOwner())) { Payload.Context.SourceController = PawnOwner->GetController(); }
    Payload.Context.SourceCreditId = Receiver->GetSourceCreditId();
    Payload.Context.DamageType = ActiveSpell->DamageType;
    Payload.Context.DamageCauser = GetOwner();
    Payload.SourceFaction = Receiver->GetFactionId();
    Payload.SpellTag = ActiveSpell->SpellTag;
    Payload.TargetPolicy = ActiveSpell->TargetPolicy;
    Payload.Damage = ActiveSpell->Damage;
    Payload.Effects = ActiveSpell->ImpactEffects;
    FSAPreparedSpellAim PreparedAim;
    LastFailure = AimSource->PrepareAim(*ActiveSpell, ActiveAim, Payload, PreparedAim);
    if (LastFailure != ESACastFailure::None)
    { Finish(Handle, ESAActionEndReason::Failed); return; }
    const ESASpellDelivery Delivery = ActiveSpell->Delivery;
    TWeakObjectPtr<ASAProjectile> Projectile;
    if (Delivery == ESASpellDelivery::Projectile)
    {
        const FTransform Transform(PreparedAim.Direction.Rotation(), PreparedAim.Muzzle);
        ASAProjectile* Spawned = GetWorld()->SpawnActorDeferred<ASAProjectile>(ActiveProjectileClass, Transform,
            GetOwner(), Cast<APawn>(GetOwner()), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        PendingProjectile = Spawned;
        Projectile = Spawned;
        if (!IsValid(Spawned)) { LastFailure = ESACastFailure::SpawnFailed; Finish(Handle, ESAActionEndReason::Failed); return; }
        Spawned->FinishSpawning(Transform);
        // Construction/BeginPlay may cancel this action or destroy the caster.
        if (!IsCurrent(Handle)) { if (Projectile.IsValid()) { Projectile->Destroy(); } return; }
        if (!Projectile.IsValid() || Projectile->IsActorBeingDestroyed()
            || !Projectile->Prepare(Payload, PreparedAim.Direction, ActiveSpell->ProjectileSpeedCmS,
                ActiveSpell->ProjectileRadiusCm, ActiveSpell->RangeCm))
        { LastFailure = ESACastFailure::SpawnFailed; Finish(Handle, ESAActionEndReason::Failed); return; }
    }
    if (!IsCurrent(Handle)) { if (Projectile.IsValid()) { Projectile->Destroy(); } return; }
    if (Delivery == ESASpellDelivery::Projectile && (!Projectile.IsValid() || Projectile->IsActorBeingDestroyed()
        || !Projectile->GetActorLocation().Equals(PreparedAim.Muzzle, .1)))
    { LastFailure = ESACastFailure::SpawnFailed; Finish(Handle, ESAActionEndReason::Failed); return; }
    FSAManaMutation Mutation;
    const TWeakObjectPtr<USAVitalsComponent> ResourceOwner = Vitals;
    if (!Vitals->CommitManaWithoutEvents(ManaReservation, Mutation)) { Finish(Handle, ESAActionEndReason::Failed); return; }
    ManaReservation.Invalidate();
    // No callbacks between cost, cooldown and phase commit.
    const double Now = GetWorld()->GetTimeSeconds();
    CooldownUntil.Add(ActiveSpell->CooldownGroup, Now + ActiveSpell->CooldownSeconds);
    Phase = ESACastPhase::Recovery;
    PhaseEnd = Now + ActiveSpell->RecoverySeconds;
    Combat->SetCastPhasePolicy(Handle, ActiveSpell->RecoveryPolicy);
    PendingProjectile.Reset(); // Ownership is transferred; cancellation never refunds/destroys release.
    const TWeakObjectPtr<USASpellcastingComponent> Self = this;
    if (Delivery == ESASpellDelivery::Projectile) { Projectile->ActivatePrepared(); }
    else
    {
        for (const FHitResult& Hit : PreparedAim.Hits)
        {
            FSASpellPayload Impact = Payload;
            Impact.Context.Hit = Hit;
            Impact.Context.TargetActor = Hit.GetActor();
            Impact.Context.ContactQuality = ESAContactQuality::Swept;
            SA::ApplySpellImpact(Hit.GetActor(), MoveTemp(Impact));
        }
    }
    if (ResourceOwner.IsValid()) { ResourceOwner->PublishManaChange(Mutation); }
    if (Self.IsValid() && Self->IsCurrent(Handle)) { Self->OnReleased.Broadcast(Handle); }
}
void USASpellcastingComponent::AdvanceCast(double Now)
{
    if (!FMath::IsFinite(Now)) { return; }
    const FSAActionHandle Handle = ActiveAction;
    if (!Handle.IsValid()) { return; }
    if (!IsCurrent(Handle)) { Finish(Handle, ESAActionEndReason::Interrupted); return; }
    if (MontageInstanceId != INDEX_NONE && (!OwnedAnim.IsValid() || !IsValid(CastMesh) || CastMesh->GetAnimInstance() != OwnedAnim.Get()))
    { Finish(Handle, ESAActionEndReason::Interrupted); return; }
    if (Now < PhaseEnd) { return; }
    if (Phase == ESACastPhase::Windup) { ReleaseCast(Handle); }
    else if (Phase == ESACastPhase::Recovery) { Finish(Handle, ESAActionEndReason::Completed); }
}
void USASpellcastingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AdvanceCast(GetWorld()->GetTimeSeconds());
}
void USASpellcastingComponent::CancelExecutor(FSAActionHandle Handle, ESAActionEndReason)
{
    if (!Handle.IsValid() || Handle != ActiveAction) { return; }
    const auto Anim = OwnedAnim;
    const int32 InstanceId = MontageInstanceId;
    const FGuid Reservation = ManaReservation;
    const auto PreparedProjectile = PendingProjectile;
    ActiveAction = {}; Phase = ESACastPhase::None; ActiveSpell = nullptr;
    ActiveProjectileClass = nullptr; ActiveMontage = nullptr;
    ManaReservation.Invalidate(); PendingProjectile.Reset(); OwnedAnim.Reset(); MontageInstanceId = INDEX_NONE;
    SetComponentTickEnabled(false);
    if (Vitals.IsValid()) { Vitals->ReleaseMana(Reservation); }
    if (PreparedProjectile.IsValid()) { PreparedProjectile->Destroy(); }
    if (Anim.IsValid())
    {
        if (FAnimMontageInstance* Instance = Anim->GetMontageInstanceForID(InstanceId))
        {
            Instance->OnMontageEnded.Unbind(); Instance->OnMontageBlendingOutStarted.Unbind();
            Instance->Stop(FAlphaBlend(.1f), true);
        }
    }
}
void USASpellcastingComponent::HandleMontageEnded(UAnimMontage*, bool bInterrupted, FSAActionHandle Handle)
{
    if (!IsCurrent(Handle)) { return; }
    if (bInterrupted) { Finish(Handle, ESAActionEndReason::Interrupted); }
    else { MontageInstanceId = INDEX_NONE; OwnedAnim.Reset(); } // Recovery owns completion.
}
void USASpellcastingComponent::HandleMontageBlend(UAnimMontage*, bool bInterrupted, FSAActionHandle Handle)
{ if (bInterrupted && IsCurrent(Handle)) { Finish(Handle, ESAActionEndReason::Interrupted); } }
void USASpellcastingComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEndingPlay = true; ++LoadGeneration;
    const FSAActionHandle Handle = ActiveAction;
    Finish(Handle, ESAActionEndReason::OwnerEnded);
    auto OldLoad = MoveTemp(AssetLoad);
    PreparedSpell = nullptr; LoadedMontage = nullptr; LoadedProjectileClass = nullptr;
    if (OldLoad) { OldLoad->CancelHandle(); }
    Super::EndPlay(Reason);
}
