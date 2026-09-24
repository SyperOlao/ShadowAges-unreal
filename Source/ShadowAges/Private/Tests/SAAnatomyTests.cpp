#include "Anatomy/SAAnatomyComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Anatomy/SACorpseSliceAdapterComponent.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#include "Vitals/SAVitalsComponent.h"

namespace SAAnatomyTests
{
FReferenceSkeleton MakeSkeleton()
{
    FReferenceSkeleton Skeleton;
    FReferenceSkeletonModifier Modifier(Skeleton, nullptr);
    Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
    Modifier.Add(FMeshBoneInfo(TEXT("arm"), TEXT("arm"), 0), FTransform::Identity);
    Modifier.Add(FMeshBoneInfo(TEXT("forearm"), TEXT("forearm"), 1), FTransform::Identity);
    Modifier.Add(FMeshBoneInfo(TEXT("hand"), TEXT("hand"), 2), FTransform::Identity);
    Modifier.Add(FMeshBoneInfo(TEXT("leg"), TEXT("leg"), 0), FTransform::Identity);
    return Skeleton;
}

struct FScene
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AActor* Owner = World->SpawnActor<AActor>();
    USAAnatomyComponent* Anatomy = NewObject<USAAnatomyComponent>(Owner);
    USAVitalsComponent* Vitals = NewObject<USAVitalsComponent>(Owner);
    USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Owner);
    USAAnatomyDefinition* Definition = NewObject<USAAnatomyDefinition>(Owner);
    FSAAnatomyContactSnapshot Snapshot;

    FScene()
    {
        USkeletalMesh* Asset = NewObject<USkeletalMesh>(Owner);
        Asset->SetRefSkeleton(MakeSkeleton());
        Asset->SetSkeleton(NewObject<USkeleton>(Owner));
        Mesh->SetSkeletalMeshAsset(Asset);
        FSAAnatomyZone Zone;
        Zone.ZoneId = TEXT("Body.Arm");
        Zone.RootBone = TEXT("arm");
        Definition->Zones.Add(Zone);
        Owner->AddInstanceComponent(Anatomy);
        Owner->AddInstanceComponent(Vitals);
        Anatomy->RegisterComponent();
        Vitals->RegisterComponent();
        Owner->DispatchBeginPlay();
        Anatomy->InitializeAnatomy(Definition, Mesh);
        FSAHitContext Context;
        Context.TargetActor = Owner;
        Context.Hit.Component = Mesh;
        Context.Hit.BoneName = TEXT("hand");
        Snapshot = Anatomy->CaptureContact(Context);
    }
    ~FScene() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAAnatomyMappingTest, "ShadowAges.Anatomy.MappingAndValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAAnatomyMappingTest::RunTest(const FString& Parameters)
{
    const FReferenceSkeleton Skeleton = SAAnatomyTests::MakeSkeleton();
    TMap<FName, FName> Roots;
    Roots.Add(TEXT("root"), TEXT("Torso"));
    Roots.Add(TEXT("arm"), TEXT("Arm"));
    Roots.Add(TEXT("forearm"), TEXT("Forearm"));
    TestEqual(TEXT("Nearest parent wins"), USAAnatomyComponent::FindZoneByAncestry(Skeleton, TEXT("hand"), Roots), FName(TEXT("Forearm")));
    TestEqual(TEXT("Exact match"), USAAnatomyComponent::FindZoneByAncestry(Skeleton, TEXT("arm"), Roots), FName(TEXT("Arm")));
    TestEqual(TEXT("Unmapped descendant uses root"), USAAnatomyComponent::FindZoneByAncestry(Skeleton, TEXT("leg"), Roots), FName(TEXT("Torso")));
    TestEqual(TEXT("Unknown bone has no zone"), USAAnatomyComponent::FindZoneByAncestry(Skeleton, TEXT("unknown"), Roots), NAME_None);
    USAAnatomyDefinition* Definition = NewObject<USAAnatomyDefinition>();
    FString Error;
    TestFalse(TEXT("Empty definition rejected"), Definition->Validate(Error));
    FSAAnatomyZone Zone;
    Zone.ZoneId = TEXT("Arm"); Zone.RootBone = TEXT("arm");
    Definition->Zones.Add(Zone);
    TestTrue(TEXT("Valid definition"), Definition->Validate(Error));
    Definition->Zones.Add(Zone);
    TestFalse(TEXT("Duplicate root/zone rejected"), Definition->Validate(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAAnatomyTransactionTest, "ShadowAges.Anatomy.DamageTransaction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAAnatomyTransactionTest::RunTest(const FString& Parameters)
{
    SAAnatomyTests::FScene Scene;
    TestEqual(TEXT("Snapshot resolves hand"), Scene.Snapshot.ZoneId, FName(TEXT("Body.Arm")));
    int32 Deaths = 0, Notifications = 0;
    bool bAliveOnNotification = true;
    Scene.Vitals->OnDied.AddLambda([&]()
    {
        ++Deaths;
        FSALimbState State;
        Scene.Anatomy->GetLimbState(TEXT("Body.Arm"), State);
        TestEqual(TEXT("Death callback sees committed anatomy"), State.AccumulatedDamage, 100.f);
        TestEqual(TEXT("No damage after death"), Scene.Vitals->ApplyHealthLoss(10.f), 0.f);
    });
    Scene.Anatomy->OnLimbChangedNative.AddLambda([&](const FSALimbState&, const FSAAnatomyContactSnapshot&, bool bAlive)
    { ++Notifications; bAliveOnNotification = bAlive; });
    FSADamageResult Rejected;
    Scene.Anatomy->DeliverLimbChanges(Scene.Anatomy->ConsumeResolvedDamageStateOnly(Rejected, Scene.Snapshot));
    Rejected.bAccepted = true; Rejected.AppliedHealthDamage = 30.f; Rejected.bBlocked = true;
    Scene.Anatomy->DeliverLimbChanges(Scene.Anatomy->ConsumeResolvedDamageStateOnly(Rejected, Scene.Snapshot));
    Rejected.bBlocked = false; Rejected.bParried = true;
    Scene.Anatomy->DeliverLimbChanges(Scene.Anatomy->ConsumeResolvedDamageStateOnly(Rejected, Scene.Snapshot));
    TestEqual(TEXT("Rejected/block/parry have no anatomy events"), Notifications, 0);
    const FSADamageResult Result = Scene.Vitals->CommitDamageWithoutEvents(100.f);
    const FSALimbChangeBatch Batch = Scene.Anatomy->ConsumeResolvedDamageStateOnly(Result, Scene.Snapshot);
    TestEqual(TEXT("Commit is silent"), Deaths, 0);
    Scene.Vitals->PublishCommittedDamage(Result);
    Scene.Vitals->PublishCommittedDamage(Result);
    Scene.Anatomy->DeliverLimbChanges(Batch);
    Scene.Anatomy->DeliverLimbChanges(Batch);
    TestEqual(TEXT("One death"), Deaths, 1);
    TestEqual(TEXT("Copied token cannot deliver twice"), Notifications, 1);
    TestFalse(TEXT("Fatal event cannot trigger live reaction"), bAliveOnNotification);
    TestEqual(TEXT("Exact zero health"), Scene.Vitals->GetHealth(), 0.f);
    TestFalse(TEXT("Live sever unsupported"), Scene.Anatomy->RequestLiveSever(FSASeverIntent()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAAnatomyReentrancyTest, "ShadowAges.Anatomy.StaleDelivery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAAnatomyReentrancyTest::RunTest(const FString& Parameters)
{
    SAAnatomyTests::FScene Scene;
    int32 Notifications = 0;
    Scene.Anatomy->OnLimbChangedNative.AddLambda([&](const FSALimbState&, const FSAAnatomyContactSnapshot&, bool) { ++Notifications; });
    const auto First = Scene.Anatomy->ConsumeResolvedDamageStateOnly(Scene.Vitals->CommitDamageWithoutEvents(20.f), Scene.Snapshot);
    const auto Second = Scene.Anatomy->ConsumeResolvedDamageStateOnly(Scene.Vitals->CommitDamageWithoutEvents(20.f), Scene.Snapshot);
    Scene.Anatomy->DeliverLimbChanges(First);
    TestEqual(TEXT("Old revision skipped"), Notifications, 0);
    Scene.Anatomy->DeliverLimbChanges(Second);
    TestEqual(TEXT("Current revision delivered"), Notifications, 1);
    FSALimbState State;
    Scene.Anatomy->GetLimbState(TEXT("Body.Arm"), State);
    TestEqual(TEXT("Accepted hits accumulate exactly once"), State.AccumulatedDamage, 40.f);
    TestTrue(TEXT("Injury threshold crossed"), State.Condition == ESALimbCondition::Injured);
    const auto OldLife = Scene.Anatomy->ConsumeResolvedDamageStateOnly(Scene.Vitals->CommitDamageWithoutEvents(5.f), Scene.Snapshot);
    Scene.Anatomy->InitializeAnatomy(Scene.Definition, Scene.Mesh);
    Scene.Anatomy->DeliverLimbChanges(OldLife);
    TestEqual(TEXT("Previous life cannot notify"), Notifications, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAAnatomyDestroyedOwnerTest, "ShadowAges.Anatomy.DestroyedOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAAnatomyDestroyedOwnerTest::RunTest(const FString& Parameters)
{
    SAAnatomyTests::FScene Scene;
    int32 Notifications = 0;
    Scene.Anatomy->OnLimbChangedNative.AddLambda([&](const FSALimbState&, const FSAAnatomyContactSnapshot&, bool) { ++Notifications; });
    Scene.Vitals->OnDied.AddLambda([&]() { Scene.Owner->Destroy(); });
    const auto Result = Scene.Vitals->CommitDamageWithoutEvents(100.f);
    const auto Batch = Scene.Anatomy->ConsumeResolvedDamageStateOnly(Result, Scene.Snapshot);
    Scene.Vitals->PublishCommittedDamage(Result);
    Scene.Anatomy->DeliverLimbChanges(Batch);
    TestEqual(TEXT("Death listener destroying owner suppresses presentation"), Notifications, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAAnatomyNestedDamageTest, "ShadowAges.Anatomy.NestedDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAAnatomyNestedDamageTest::RunTest(const FString& Parameters)
{
    SAAnatomyTests::FScene Scene;
    int32 Notifications = 0, LiveNotifications = 0, Deaths = 0;
    Scene.Vitals->OnDied.AddLambda([&]() { ++Deaths; });
    Scene.Anatomy->OnLimbChangedNative.AddLambda([&](const FSALimbState&, const FSAAnatomyContactSnapshot&, bool bAlive)
    {
        ++Notifications;
        if (bAlive)
        {
            ++LiveNotifications;
            const auto Fatal = Scene.Vitals->CommitDamageWithoutEvents(100.f);
            const auto Nested = Scene.Anatomy->ConsumeResolvedDamageStateOnly(Fatal, Scene.Snapshot);
            Scene.Vitals->PublishCommittedDamage(Fatal);
            Scene.Anatomy->DeliverLimbChanges(Nested);
        }
    });
    const auto Result = Scene.Vitals->CommitDamageWithoutEvents(25.f);
    const auto Batch = Scene.Anatomy->ConsumeResolvedDamageStateOnly(Result, Scene.Snapshot);
    Scene.Vitals->PublishCommittedDamage(Result);
    Scene.Anatomy->DeliverLimbChanges(Batch);
    TestEqual(TEXT("Both local batches delivered"), Notifications, 2);
    TestEqual(TEXT("Only pre-death reaction alive"), LiveNotifications, 1);
    TestEqual(TEXT("Nested fatal hit kills once"), Deaths, 1);
    FSALimbState State;
    Scene.Anatomy->GetLimbState(TEXT("Body.Arm"), State);
    TestEqual(TEXT("Nested state is never overwritten by older notification"), State.AccumulatedDamage, 100.f);
    return true;
}
#endif
