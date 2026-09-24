#include "Anatomy/SACorpseSliceAdapterComponent.h"
#include "APSliceableSkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "TimerManager.h"
#include "Vitals/SAVitalsComponent.h"

USACorpseSliceAdapterComponent::USACorpseSliceAdapterComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool USACorpseSliceAdapterComponent::TrySliceCorpse(const FSASeverIntent& Intent,
    const FSAAnatomyContactSnapshot& Snapshot)
{
    AActor* Owner = GetOwner();
    const USAVitalsComponent* Vitals = IsValid(Owner) ? Owner->FindComponentByClass<USAVitalsComponent>() : nullptr;
    UAPSliceableSkeletalMeshComponent* Mesh = Cast<UAPSliceableSkeletalMeshComponent>(Snapshot.Mesh.Get());
    if (!bEnableCorpseSlicing || bAttempted || !HasBegunPlay() || !Vitals || !Vitals->HasBegunPlay()
        || Vitals->IsAlive() || Owner->IsActorBeingDestroyed() || !Intent.bFatalHit
        || !Intent.SourceAction.IsValid() || Intent.ZoneId.IsNone() || !IsValid(Mesh)
        || !Mesh->IsRegistered() || Mesh->GetOwner() != Owner || !GetWorld()
        || !FMath::IsFinite(SliceRadius) || SliceRadius < 1.f
        || !FMath::IsFinite(FragmentLifetime) || FragmentLifetime < 1.f
        || Snapshot.Context.ContactQuality != ESAContactQuality::Swept
        || Snapshot.Context.Hit.bStartPenetrating
        || Snapshot.Context.Hit.BoneName.IsNone()
        || Intent.ContactPointWS.ContainsNaN() || Intent.SuggestedPlaneNormalWS.ContainsNaN()
        || Intent.SuggestedPlaneNormalWS.IsNearlyZero()) { return false; }
    USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset || Asset != Snapshot.MeshAsset.Get() || !Asset->GetSkeleton()
        || !Asset->GetPhysicsAsset() || Mesh->GetBoneIndex(Snapshot.Context.Hit.BoneName) == INDEX_NONE
        || Mesh->GetComponentSpaceTransforms().Num() != Asset->GetRefSkeleton().GetNum()) { return false; }
    const FSkeletalMeshRenderData* RenderData = Asset->GetResourceForRendering();
    if (!RenderData || RenderData->LODRenderData.IsEmpty() || RenderData->LODRenderData[0].GetNumVertices() == 0
        || !Mesh->GetMeshObject()) { return false; }
    const FVector Scale = Mesh->GetComponentScale();
    if (Scale.X <= 0.f || !FMath::IsNearlyEqual(Scale.X, Scale.Y) || !FMath::IsNearlyEqual(Scale.X, Scale.Z)) { return false; }
    // Refine against current physics geometry and reject contact displaced by death presentation.
    FVector SurfacePoint, SurfaceNormal;
    FName SurfaceBone;
    float SurfaceDistance = 0.f;
    if (!Mesh->K2_GetClosestPointOnPhysicsAsset(Intent.ContactPointWS, SurfacePoint, SurfaceNormal, SurfaceBone, SurfaceDistance)
        || SurfaceBone != Snapshot.Context.Hit.BoneName
        || FVector::DistSquared(SurfacePoint, Intent.ContactPointWS) > FMath::Square(5.f)) { return false; }

    bAttempted = true; // Consume before plugin callbacks; a failure is not retried.
    TArray<USceneComponent*> Before;
    Owner->GetComponents<USceneComponent>(Before);
    const FSliceResult Result = Mesh->SliceMeshInRadius(Intent.ContactPointWS,
        Intent.SuggestedPlaneNormalWS.GetSafeNormal(), SliceRadius, EAPSliceSpace::World);
    // Include auxiliaries and partial allocations on failure, not just success arrays.
    TArray<USceneComponent*> After;
    Owner->GetComponents<USceneComponent>(After);
    int32 KeptParts = 0;
    for (USceneComponent* Part : After)
    {
        if (!IsValid(Part) || Before.Contains(Part)) { continue; }
        const bool bGeometry = Part->IsA<UMeshComponent>();
        if (Owner->IsActorBeingDestroyed() || !Result.bSuccess
            || (bGeometry && ++KeptParts > FMath::Clamp(MaxDetachedParts, 0, 16)))
        {
            Part->DestroyComponent();
        }
        else { Fragments.Add(Part); }
    }
    if (!Fragments.IsEmpty() && !Owner->IsActorBeingDestroyed())
    {
        GetWorld()->GetTimerManager().SetTimer(CleanupTimer, this,
            &USACorpseSliceAdapterComponent::CleanupFragments, FragmentLifetime, false);
    }
    return Result.bSuccess;
}

void USACorpseSliceAdapterComponent::CleanupFragments()
{
    for (const auto& Part : Fragments) { if (Part.IsValid()) { Part->DestroyComponent(); } }
    Fragments.Reset();
}

void USACorpseSliceAdapterComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(CleanupTimer); }
    CleanupFragments();
    Super::EndPlay(Reason);
}
