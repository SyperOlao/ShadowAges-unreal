#include "Combat/Tracing/SABladeTraceKernel.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace SABladeTraceTests
{
struct FScene
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AActor* Source = World->SpawnActor<AActor>();

    ~FScene() { World->DestroyWorld(false); }

    AActor* AddBox(const FVector& Location, const FVector& Extent,
        ECollisionResponse Response = ECR_Overlap)
    {
        AActor* Actor = World->SpawnActor<AActor>();
        UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
        Actor->AddInstanceComponent(Box);
        Actor->SetRootComponent(Box);
        Box->SetBoxExtent(Extent);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionResponseToAllChannels(ECR_Ignore);
        Box->SetCollisionResponseToChannel(ECC_GameTraceChannel2, Response);
        Box->SetWorldLocation(Location);
        Box->RegisterComponent();
        return Actor;
    }
};

FSABladeTraceFrame Translation()
{
    FSABladeTraceFrame Frame;
    Frame.PreviousPose = FTransform(FVector(0, -40, 0));
    Frame.CurrentPose = FTransform(FVector(0, 40, 0));
    Frame.PreviousReachAnchor = FVector(0, -40, 0);
    Frame.CurrentReachAnchor = FVector(0, 40, 0);
    Frame.DeltaSeconds = 0.1;
    return Frame;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSABladeValidationTest,
    "ShadowAges.MeleeTrace.ValidationAndBudgets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSABladeValidationTest::RunTest(const FString& Parameters)
{
    FSABladeTraceSettings Settings;
    FString Error;
    TestTrue(TEXT("Default settings valid"), Settings.Validate(Error));
    Settings.GameplayRadius = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("Reject NaN"), Settings.Validate(Error));
    Settings = FSABladeTraceSettings();
    Settings.ReachRadiusWorld = Settings.MaxQueryRadiusWorld + 1;
    TestFalse(TEXT("Reject oversized reach sphere"), Settings.Validate(Error));
    Settings = FSABladeTraceSettings();
    Settings.MaxQueriesPerPose = 2;
    Settings.MaxRawHitsPerPose = 3;
    Settings.MaxCandidatesPerPose = 1;
    FSABladeTraceBudget Budget;
    Budget.Reset(Settings);
    TestTrue(TEXT("First query"), Budget.TrySpendQuery());
    TestTrue(TEXT("Last allowed query"), Budget.TrySpendQuery());
    TestFalse(TEXT("Query limit enforced"), Budget.TrySpendQuery());
    TestEqual(TEXT("Failed query does not consume a slot"), Budget.UsedQueries, int64(2));
    TestTrue(TEXT("Raw hits at limit"), Budget.TryAddRawHits(3));
    TestFalse(TEXT("Raw hits over limit"), Budget.TryAddRawHits(1));
    TestTrue(TEXT("First candidate"), Budget.TryAddCandidate());
    TestFalse(TEXT("Candidate limit enforced"), Budget.TryAddCandidate());

    SABladeTraceTests::FScene Scene;
    FSABladeTraceKernel Kernel;
    Settings = FSABladeTraceSettings();
    TestTrue(TEXT("Configure 90 cm blade"), Kernel.Configure(Settings,
        FVector(5, 0, 0), FVector(95, 0, 0), Scene.Source, Scene.Source, {}, Error));
    TestEqual(TEXT("Blade covered by thirteen samples"), Kernel.GetSampleCount(), 13);
    Settings.MaxSamples = 12;
    TestFalse(TEXT("Reject insufficient sampling budget instead of reducing accuracy"),
        Kernel.Configure(Settings, FVector(5, 0, 0), FVector(95, 0, 0),
            Scene.Source, Scene.Source, {}, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSABladeGeometryTest,
    "ShadowAges.MeleeTrace.GeometryAndContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSABladeGeometryTest::RunTest(const FString& Parameters)
{
    SABladeTraceTests::FScene Scene;
    AActor* Target = Scene.AddBox(FVector(50, 0, 0), FVector(2));
    FSABladeTraceSettings Settings;
    FSABladeTraceKernel Kernel;
    FString Error;
    if (!TestTrue(TEXT("Configure"), Kernel.Configure(Settings, FVector(5, 0, 0),
        FVector(95, 0, 0), Scene.Source, Scene.Source, {}, Error))) return false;
    FSABladeTraceBudget Budget;
    TArray<FSABladeContact> Contacts;
    TOptional<FSABladeContact> Wall;
    FSABladeTraceFrame Frame = SABladeTraceTests::Translation();
    Budget.Reset(Settings);
    TestTrue(TEXT("Translation collected"), Kernel.Collect(*Scene.World, Frame,
        Budget, Contacts, Wall) == ESABladeTraceStatus::Collected);
    TestEqual(TEXT("Middle of blade hits target exactly once across samples"), Contacts.Num(), 1);
    if (!Contacts.IsEmpty())
    {
        TestTrue(TEXT("Correct actor"), Contacts[0].Hit.GetActor() == Target);
        TestTrue(TEXT("Swept contact"), Contacts[0].Quality == ESAContactQuality::Swept);
        TestTrue(TEXT("Contact precedes frame midpoint"), Contacts[0].FrameAlpha < 0.5);
        bool bReachable = false;
        TestTrue(TEXT("Reach query succeeds"), Kernel.CheckReach(*Scene.World,
            Contacts[0], Budget, bReachable) == ESABladeTraceStatus::Collected);
        TestTrue(TEXT("Unobstructed contact reachable"), bReachable);
    }
    Frame.ActiveEnd = 0.1;
    Budget.Reset(Settings);
    Kernel.Collect(*Scene.World, Frame, Budget, Contacts, Wall);
    TestTrue(TEXT("Inactive part of motion cannot hit"), Contacts.IsEmpty());

    Budget.Reset(Settings);
    Kernel.CollectSnapshot(*Scene.World, FTransform::Identity, FVector::ZeroVector,
        Budget, Contacts, Wall);
    TestEqual(TEXT("Stationary blade detects overlap"), Contacts.Num(), 1);
    if (!Contacts.IsEmpty())
    {
        TestTrue(TEXT("Snapshot quality is explicit"),
            Contacts[0].Quality == ESAContactQuality::StationaryOverlap);
        TestTrue(TEXT("Snapshot does not invent bone or normal"),
            Contacts[0].Hit.BoneName.IsNone() && Contacts[0].Hit.ImpactNormal.IsZero());
    }

    Frame = SABladeTraceTests::Translation();
    Frame.CurrentPose.SetLocation(FVector(0, 500, 0));
    Budget.Reset(Settings);
    TestTrue(TEXT("Teleport rejected"), Kernel.Collect(*Scene.World, Frame,
        Budget, Contacts, Wall) == ESABladeTraceStatus::Discontinuity);
    TestEqual(TEXT("Teleport does not issue queries"), Budget.UsedQueries, int64(0));
    Frame = SABladeTraceTests::Translation();
    Frame.CurrentPose.SetScale3D(FVector(1, 2, 1));
    TestTrue(TEXT("Nonuniform scale rejected"), Kernel.Collect(*Scene.World, Frame,
        Budget, Contacts, Wall) == ESABladeTraceStatus::InvalidSetup);

    Target->SetActorLocation(FVector(60, 60, 0));
    Frame = FSABladeTraceFrame();
    Frame.CurrentPose.SetRotation(FQuat(FVector::UpVector, UE_PI / 2.0));
    Frame.DeltaSeconds = 0.1;
    Budget.Reset(Settings);
    TestTrue(TEXT("Quarter turn collected"), Kernel.Collect(*Scene.World, Frame,
        Budget, Contacts, Wall) == ESABladeTraceStatus::Collected);
    TestEqual(TEXT("Rotating blade hits along arc"), Contacts.Num(), 1);
    TestEqual(TEXT("Quarter turn uses documented 13 x 6 queries"), Budget.UsedQueries, int64(78));
    Budget.Reset(Settings);
    Budget.MaxQueries = 77;
    TestTrue(TEXT("Insufficient frame budget rejected"), Kernel.Collect(*Scene.World,
        Frame, Budget, Contacts, Wall) == ESABladeTraceStatus::BudgetExceeded);
    TestTrue(TEXT("Failed batch exposes no contacts"), Contacts.IsEmpty() && !Wall.IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSABladeWallTest,
    "ShadowAges.MeleeTrace.WallsAndReach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSABladeWallTest::RunTest(const FString& Parameters)
{
    SABladeTraceTests::FScene Scene;
    AActor* Front = Scene.AddBox(FVector(50, -22, 0), FVector(2));
    Scene.AddBox(FVector(50, 22, 0), FVector(2));
    Scene.AddBox(FVector(50, 0, 0), FVector(100, 2, 100), ECR_Block);
    FSABladeTraceSettings Settings;
    FSABladeTraceKernel Kernel;
    FString Error;
    Kernel.Configure(Settings, FVector(5, 0, 0), FVector(95, 0, 0),
        Scene.Source, Scene.Source, {}, Error);
    FSABladeTraceBudget Budget;
    Budget.Reset(Settings);
    TArray<FSABladeContact> Contacts;
    TOptional<FSABladeContact> Wall;
    Kernel.Collect(*Scene.World, SABladeTraceTests::Translation(), Budget, Contacts, Wall);
    TestTrue(TEXT("World blocker found"), Wall.IsSet());
    TestEqual(TEXT("Only front target is returned"), Contacts.Num(), 1);
    if (Wall.IsSet() && !Contacts.IsEmpty())
    {
        TestTrue(TEXT("Target before wall"), Contacts[0].Hit.GetActor() == Front
            && Contacts[0].FrameAlpha < Wall->FrameAlpha);
    }

    // Blade is already behind the wall: its local sweep misses the wall,
    // but the body-to-hilt reach path must still prevent damage through it.
    Budget.Reset(Settings);
    Kernel.CollectSnapshot(*Scene.World, FTransform(FVector(0, 22, 0)),
        FVector(0, -22, 0), Budget, Contacts, Wall);
    TestFalse(TEXT("Snapshot blade does not intersect wall"), Wall.IsSet());
    TestEqual(TEXT("Behind-wall contact collected geometrically"), Contacts.Num(), 1);
    if (!Contacts.IsEmpty())
    {
        bool bReachable = true;
        TestTrue(TEXT("Reach check completes"), Kernel.CheckReach(*Scene.World,
            Contacts[0], Budget, bReachable) == ESABladeTraceStatus::Collected);
        TestFalse(TEXT("Wall between body and blade denies contact"), bReachable);
    }
    return true;
}

#endif
