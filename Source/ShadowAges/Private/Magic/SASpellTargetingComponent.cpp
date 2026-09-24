#include "Magic/SASpellTargetingComponent.h"
#include "Magic/SASpellDefinition.h"
#include "Magic/SASpellDamage.h"
#include "Combat/SAMeleeDamageReceiverComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Controller.h"

USASpellTargetingComponent::USASpellTargetingComponent() { PrimaryComponentTick.bCanEverTick = false; }
bool USASpellTargetingComponent::SetMuzzleComponent(USceneComponent* Component)
{
    if (!IsValid(Component) || Component->GetOwner() != GetOwner()) { return false; }
    MuzzleComponent = Component;
    return true;
}
FSASpellAim USASpellTargetingComponent::AimFromController(AController* Controller)
{
    FSASpellAim Aim;
    if (IsValid(Controller))
    {
        FRotator Rotation;
        Controller->GetPlayerViewPoint(Aim.ViewOrigin, Rotation);
        Aim.ViewDirection = Rotation.Vector();
    }
    return Aim;
}
bool USASpellTargetingComponent::HasValidMuzzle(const USASpellDefinition& Spell) const
{
    return IsValid(MuzzleComponent) && MuzzleComponent->IsRegistered() && MuzzleComponent->GetOwner() == GetOwner()
        && (Spell.bUseComponentOrigin || MuzzleComponent->DoesSocketExist(Spell.MuzzleSocket));
}

ESACastFailure USASpellTargetingComponent::PrepareAim(const USASpellDefinition& Spell,
    const FSASpellAim& Aim, const FSASpellPayload& Source, FSAPreparedSpellAim& Out) const
{
    Out = {};
    if (!HasValidMuzzle(Spell) || !GetWorld() || !IsValid(GetOwner())) { return ESACastFailure::MissingDependencies; }
    Out.Muzzle = Spell.bUseComponentOrigin ? MuzzleComponent->GetComponentLocation() : MuzzleComponent->GetSocketLocation(Spell.MuzzleSocket);
    if (Out.Muzzle.ContainsNaN() || Aim.ViewOrigin.ContainsNaN() || Aim.ViewDirection.ContainsNaN()
        || Aim.ViewDirection.IsNearlyZero() || FVector::DistSquared(Aim.ViewOrigin, GetOwner()->GetActorLocation()) > FMath::Square(2000.))
    { return ESACastFailure::InvalidAim; }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(SASpellAim), false, GetOwner());
    TArray<AActor*> AttachedActors;
    GetOwner()->GetAttachedActors(AttachedActors, true, true);
    Query.AddIgnoredActors(AttachedActors);
    FCollisionObjectQueryParams WorldObjects;
    WorldObjects.AddObjectTypesToQuery(ECC_WorldStatic);
    WorldObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FHitResult Obstruction;
    const auto Sphere = FCollisionShape::MakeSphere(Spell.ProjectileRadiusCm);
    if (GetWorld()->SweepSingleByObjectType(Obstruction, GetOwner()->GetActorLocation(), Out.Muzzle,
        FQuat::Identity, WorldObjects, Sphere, Query)
        || GetWorld()->OverlapAnyTestByObjectType(Out.Muzzle, FQuat::Identity, WorldObjects, Sphere, Query))
    { return ESACastFailure::MuzzleBlocked; }

    if (Spell.Delivery == ESASpellDelivery::Instant && Spell.TargetPolicy == ESASpellTargetPolicy::Self)
    {
        Out.AimPoint = GetOwner()->GetActorLocation();
        Out.Hits.Add(FHitResult(GetOwner(), Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()), Out.AimPoint, FVector::UpVector));
        return ESACastFailure::None;
    }
    const FVector ViewEnd = Aim.ViewOrigin + Aim.ViewDirection.GetSafeNormal() * Spell.RangeCm;
    FHitResult ViewHit;
    Out.AimPoint = GetWorld()->LineTraceSingleByChannel(ViewHit, Aim.ViewOrigin, ViewEnd, ECC_GameTraceChannel4, Query)
        ? ViewHit.ImpactPoint : ViewEnd;
    if (Spell.Delivery == ESASpellDelivery::Instant && Spell.AreaRadiusCm == 0.f)
    {
        if (!Aim.Target.IsValid() || !SA::CanApplySpell(Aim.Target.Get(), Source)) { return ESACastFailure::InvalidAim; }
        FVector Extent;
        Aim.Target->GetActorBounds(true, Out.AimPoint, Extent);
    }
    const FVector Delta = Out.AimPoint - Out.Muzzle;
    if (Delta.ContainsNaN() || Delta.IsNearlyZero() || FVector::DotProduct(Delta, GetOwner()->GetActorForwardVector()) < 0.)
    { return ESACastFailure::InvalidAim; }
    if (Spell.Delivery == ESASpellDelivery::Instant && Spell.AreaRadiusCm == 0.f && Delta.SizeSquared() > FMath::Square(Spell.RangeCm))
    { return ESACastFailure::InvalidAim; }
    Out.Direction = Delta.GetSafeNormal();
    Out.AimPoint = Out.Muzzle + Out.Direction * FMath::Min(Delta.Size(), double(Spell.RangeCm));
    FHitResult PathHit;
    const bool bHit = GetWorld()->SweepSingleByChannel(PathHit, Out.Muzzle, Out.AimPoint, FQuat::Identity,
        ECC_GameTraceChannel5, Sphere, Query);
    if (bHit && PathHit.bStartPenetrating) { return ESACastFailure::MuzzleBlocked; }
    if (Spell.Delivery == ESASpellDelivery::Ray)
    {
        if (bHit) { Out.Hits.Add(PathHit); }
        return ESACastFailure::None; // An unobstructed ray may legitimately miss.
    }
    if (Spell.Delivery == ESASpellDelivery::Projectile) { return ESACastFailure::None; }
    if (Spell.AreaRadiusCm == 0.f)
    {
        if (bHit && PathHit.GetActor() != Aim.Target.Get()) { return ESACastFailure::InvalidAim; }
        Out.Hits.Add(bHit ? PathHit : FHitResult(Aim.Target.Get(), Cast<UPrimitiveComponent>(Aim.Target->GetRootComponent()), Out.AimPoint, -Out.Direction));
        return ESACastFailure::None;
    }
    if (bHit) { Out.AimPoint = PathHit.Location; }
    FCollisionObjectQueryParams Targets;
    Targets.AddObjectTypesToQuery(ECC_Pawn);
    Targets.AddObjectTypesToQuery(ECC_PhysicsBody);
    Targets.AddObjectTypesToQuery(ECC_WorldDynamic);
    TArray<FOverlapResult> Overlaps;
    GetWorld()->OverlapMultiByObjectType(Overlaps, Out.AimPoint, FQuat::Identity, Targets,
        FCollisionShape::MakeSphere(Spell.AreaRadiusCm), Query);
    // Fail before charging instead of arbitrarily selecting a physics-query prefix.
    if (Overlaps.Num() > 256) { return ESACastFailure::InvalidAim; }
    struct FCandidate { FHitResult Hit; double Distance; FGuid Id; };
    TArray<FCandidate> Candidates;
    TSet<TWeakObjectPtr<AActor>> Seen;
    for (const auto& Overlap : Overlaps)
    {
        AActor* Target = Overlap.GetActor();
        if (!IsValid(Target) || Seen.Contains(Target) || !SA::CanApplySpell(Target, Source)) { continue; }
        Seen.Add(Target);
        FVector Point, Extent;
        Target->GetActorBounds(true, Point, Extent);
        const double Distance = FVector::DistSquared(Point, Out.AimPoint);
        if (Distance > FMath::Square(Spell.AreaRadiusCm)) { continue; }
        FHitResult LOS;
        if (Spell.bRequireAreaLineOfSight && GetWorld()->LineTraceSingleByChannel(LOS, Out.AimPoint, Point,
            ECC_GameTraceChannel5, Query) && LOS.GetActor() != Target) { continue; }
        FCandidate Candidate;
        Candidate.Hit = FHitResult(Target, Overlap.GetComponent(), Point, (Out.AimPoint - Point).GetSafeNormal());
        Candidate.Distance = Distance;
        Candidate.Id = Target->FindComponentByClass<USAMeleeDamageReceiverComponent>()->GetSourceCreditId();
        Candidates.Add(Candidate);
    }
    Candidates.Sort([](const FCandidate& A, const FCandidate& B)
    { return A.Distance == B.Distance ? A.Id < B.Id : A.Distance < B.Distance; });
    for (int32 I = 0; I < FMath::Min(Spell.MaxAreaTargets, Candidates.Num()); ++I) { Out.Hits.Add(Candidates[I].Hit); }
    return ESACastFailure::None;
}
