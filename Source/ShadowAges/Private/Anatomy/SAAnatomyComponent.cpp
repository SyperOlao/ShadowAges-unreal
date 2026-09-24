#include "Anatomy/SAAnatomyComponent.h"

#include "Anatomy/SACorpseSliceAdapterComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "Vitals/SAVitalsComponent.h"

USAAnatomyComponent::USAAnatomyComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void USAAnatomyComponent::BeginPlay()
{
    Super::BeginPlay();
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (Definition && Character)
    {
        InitializeAnatomy(Definition, Character->GetMesh());
    }
}

void USAAnatomyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    LifeId.Invalidate();
    DeliveryTokens.Reset();
    Super::EndPlay(Reason);
}

bool USAAnatomyComponent::IsUsable() const
{
    return HasBegunPlay() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed()
        && LifeId.IsValid();
}

bool USAAnatomyComponent::InitializeAnatomy(USAAnatomyDefinition* Profile, USkeletalMeshComponent* BodyMesh)
{
    FString Error;
    if (!IsValid(Profile) || !Profile->Validate(Error) || !IsValid(BodyMesh)
        || BodyMesh->GetOwner() != GetOwner() || !BodyMesh->GetSkeletalMeshAsset())
    {
        return false;
    }
    const FReferenceSkeleton& Skeleton = BodyMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    for (const FSAAnatomyZone& Rule : Profile->Zones)
    {
        if (Skeleton.FindBoneIndex(Rule.RootBone) == INDEX_NONE) { return false; }
    }
    Definition = Profile;
    Mesh = BodyMesh;
    LifeId = FGuid::NewGuid();
    DeliveryTokens.Reset();
    Rules.Reset();
    Limbs.Reset();
    Revisions.Reset();
    for (const FSAAnatomyZone& Rule : Profile->Zones)
    {
        Rules.Add(Rule.ZoneId, Rule);
        FSALimbState State;
        State.ZoneId = Rule.ZoneId;
        Limbs.Add(Rule.ZoneId, State);
        Revisions.Add(Rule.ZoneId, 0);
    }
    return RebuildBoneCache();
}

FName USAAnatomyComponent::FindZoneByAncestry(const FReferenceSkeleton& Skeleton, FName Bone,
    const TMap<FName, FName>& Roots)
{
    for (int32 Index = Skeleton.FindBoneIndex(Bone); Index != INDEX_NONE; Index = Skeleton.GetParentIndex(Index))
    {
        if (const FName* Zone = Roots.Find(Skeleton.GetBoneName(Index))) { return *Zone; }
    }
    return NAME_None;
}

bool USAAnatomyComponent::RebuildBoneCache()
{
    BoneToZone.Reset();
    CachedMeshAsset = Mesh.IsValid() ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!CachedMeshAsset.IsValid()) { return false; }
    TMap<FName, FName> Roots;
    for (const auto& Pair : Rules) { Roots.Add(Pair.Value.RootBone, Pair.Key); }
    const FReferenceSkeleton& Skeleton = CachedMeshAsset->GetRefSkeleton();
    for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
    {
        const FName Bone = Skeleton.GetBoneName(Index);
        BoneToZone.Add(Bone, FindZoneByAncestry(Skeleton, Bone, Roots));
    }
    return true;
}

FName USAAnatomyComponent::ResolveBone(FName Bone)
{
    if (!Mesh.IsValid()) { return NAME_None; }
    if (CachedMeshAsset.Get() != Mesh->GetSkeletalMeshAsset()) { RebuildBoneCache(); }
    const FName* Zone = BoneToZone.Find(Bone);
    return Zone ? *Zone : NAME_None;
}

bool USAAnatomyComponent::GetLimbState(FName ZoneId, FSALimbState& State) const
{
    const FSALimbState* Found = Limbs.Find(ZoneId);
    State = Found ? *Found : FSALimbState();
    return Found != nullptr;
}

bool USAAnatomyComponent::HasFunctionalBodyTags(const TArray<FName>& RequiredTags) const
{
    TSet<FName> Available;
    for (const auto& Pair : Limbs)
    {
        if (Pair.Value.Condition != ESALimbCondition::Severed)
        {
            for (FName Tag : Rules.FindChecked(Pair.Key).FunctionalBodyTags) { Available.Add(Tag); }
        }
    }
    for (FName Tag : RequiredTags) { if (!Available.Contains(Tag)) { return false; } }
    return true;
}

FSAAnatomyContactSnapshot USAAnatomyComponent::CaptureContact(const FSAHitContext& Context)
{
    FSAAnatomyContactSnapshot Snapshot;
    if (!IsUsable() || Context.TargetActor.Get() != GetOwner()
        || Context.Hit.GetComponent() != Mesh.Get() || !Mesh.IsValid()) { return Snapshot; }
    Snapshot.ZoneId = ResolveBone(Context.Hit.BoneName);
#if ENABLE_DRAW_DEBUG
    if (bDrawDebug && GetWorld())
    {
        DrawDebugString(GetWorld(), Context.Hit.ImpactPoint,
            FString::Printf(TEXT("%s -> %s"), *Context.Hit.BoneName.ToString(), *Snapshot.ZoneId.ToString()),
            nullptr, Snapshot.ZoneId.IsNone() ? FColor::Red : FColor::Green, 2.f);
    }
#endif
    Snapshot.Context = Context;
    Snapshot.TargetTransform = GetOwner()->GetActorTransform();
    const int32 BoneIndex = Mesh->GetBoneIndex(Context.Hit.BoneName);
    if (BoneIndex != INDEX_NONE) { Snapshot.BoneTransform = Mesh->GetBoneTransform(BoneIndex); }
    Snapshot.Mesh = Mesh;
    Snapshot.MeshAsset = CachedMeshAsset;
    Snapshot.LifeId = LifeId;
    return Snapshot;
}

FSALimbChangeBatch USAAnatomyComponent::ConsumeResolvedDamageStateOnly(
    const FSADamageResult& Result, const FSAAnatomyContactSnapshot& Snapshot)
{
    FSALimbChangeBatch Batch;
    if (!IsUsable() || Snapshot.LifeId != LifeId || !Result.bAccepted || Result.bBlocked
        || Result.bParried || !FMath::IsFinite(Result.AppliedHealthDamage)
        || Result.AppliedHealthDamage <= 0.f) { return Batch; }
    FSALimbState* Limb = Limbs.Find(Snapshot.ZoneId);
    const FSAAnatomyZone* Rule = Rules.Find(Snapshot.ZoneId);
    if (!Limb || !Rule || Limb->Condition == ESALimbCondition::Severed) { return Batch; }
    const float OldDamage = Limb->AccumulatedDamage;
    Limb->AccumulatedDamage = static_cast<float>(FMath::Min(double(Rule->DamageCap),
        double(OldDamage) + double(Result.AppliedHealthDamage) * Rule->DamageMultiplier));
    if (Limb->AccumulatedDamage >= Rule->InjuryThreshold) { Limb->Condition = ESALimbCondition::Injured; }
    // A fatal hit can request presentation even when this zone already reached its cap.
    Batch.bRequestSever = Result.bFatal && Rule->bAllowCorpseSlice
        && Limb->AccumulatedDamage >= Rule->SeverThreshold
        && Snapshot.Context.DamageType == Rule->SeverDamageType;
    if (OldDamage == Limb->AccumulatedDamage && !Batch.bRequestSever) { return Batch; }
    Batch.Issuer = this;
    Batch.LifeId = LifeId;
    Batch.Token = FGuid::NewGuid();
    DeliveryTokens.Add(Batch.Token);
    Batch.State = *Limb;
    Batch.Revision = ++Revisions.FindChecked(Snapshot.ZoneId);
    Batch.Snapshot = Snapshot;
    Batch.bFatal = Result.bFatal;
    Batch.Intent.ZoneId = Snapshot.ZoneId;
    Batch.Intent.BoundaryBone = Rule->RootBone;
    Batch.Intent.ContactPointWS = Snapshot.Context.Hit.ImpactPoint;
    Batch.Intent.SuggestedPlaneNormalWS = FVector::CrossProduct(
        Snapshot.Context.BladeDirection.GetSafeNormal(), Snapshot.Context.BladeVelocity.GetSafeNormal()).GetSafeNormal();
    Batch.Intent.SourceAction = Snapshot.Context.Playback.Action;
    Batch.Intent.bFatalHit = Result.bFatal;
    return Batch;
}

void USAAnatomyComponent::DeliverLimbChanges(FSALimbChangeBatch Batch)
{
    if (Batch.Issuer.Get() != this || Batch.LifeId != LifeId || !DeliveryTokens.Remove(Batch.Token)) { return; }
    const auto Current = [&]()
    {
        const uint64* Revision = Revisions.Find(Batch.State.ZoneId);
        return IsUsable() && LifeId == Batch.LifeId && Revision && *Revision == Batch.Revision;
    };
    if (!Current()) { return; }
    const USAVitalsComponent* Vitals = GetOwner()->FindComponentByClass<USAVitalsComponent>();
    const bool bAlive = Vitals && Vitals->IsAlive() && !Batch.bFatal;
    OnLimbChangedNative.Broadcast(Batch.State, Batch.Snapshot, bAlive);
    if (!Current()) { return; }
    Vitals = GetOwner()->FindComponentByClass<USAVitalsComponent>();
    OnLimbChanged.Broadcast(Batch.State, Batch.Snapshot, Vitals && Vitals->IsAlive() && !Batch.bFatal);
    if (!Current() || !Batch.bRequestSever) { return; }
    OnSeverRequested.Broadcast(Batch.Intent);
    if (!Current()) { return; }
    if (USACorpseSliceAdapterComponent* Adapter = GetOwner()->FindComponentByClass<USACorpseSliceAdapterComponent>())
    {
        Adapter->TrySliceCorpse(Batch.Intent, Batch.Snapshot);
    }
    // Corpse geometry is not a live limb transition: no OnLimbLost, no kill credit.
}
