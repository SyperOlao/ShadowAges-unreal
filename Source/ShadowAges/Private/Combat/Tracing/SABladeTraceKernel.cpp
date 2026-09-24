// Source/ShadowAges/Private/Combat/Tracing/SABladeTraceKernel.cpp
#include "Combat/Tracing/SABladeTraceKernel.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void FSABladeTraceBudget::Reset(const FSABladeTraceSettings& Settings)
{
    MaxQueries = Settings.MaxQueriesPerPose;
    MaxRawHits = Settings.MaxRawHitsPerPose;
    MaxCandidates = Settings.MaxCandidatesPerPose;
    UsedQueries = 0;
    UsedRawHits = 0;
    UsedCandidates = 0;
}

bool FSABladeTraceBudget::TrySpendQuery()
{
    if (UsedQueries >= MaxQueries)
    {
        return false;
    }
    ++UsedQueries;
    return true;
}

bool FSABladeTraceBudget::TryAddRawHits(int32 Count)
{
    if (Count < 0)
    {
        return false;
    }
    UsedRawHits += int64(Count);
    return UsedRawHits <= MaxRawHits;
}

bool FSABladeTraceBudget::TryAddCandidate()
{
    if (UsedCandidates >= MaxCandidates)
    {
        return false;
    }
    ++UsedCandidates;
    return true;
}

bool FSABladeTraceKernel::Configure(const FSABladeTraceSettings& InSettings,
    const FVector& InLocalBase, const FVector& InLocalTip,
    const AActor* Source, const AActor* Weapon,
    const TArray<AActor*>& ExtraIgnored, FString& OutError)
{
    bConfigured = false;
    LocalSamples.Reset();
    CandidateIndices.Reset();
    if (!InSettings.Validate(OutError))
    {
        return false;
    }
    if (!IsValid(Source) || !IsValid(Weapon) || ExtraIgnored.Num() > 64 ||
        InLocalBase.ContainsNaN() || InLocalTip.ContainsNaN() ||
        InLocalBase.Size() > 1000000.0 || InLocalTip.Size() > 1000000.0)
    {
        OutError = TEXT("Invalid blade actors, local sockets or ignored-actor count.");
        return false;
    }
    const double Length = FVector::Distance(InLocalBase, InLocalTip);
    const double Ratio = Length / InSettings.MaxSpacing;
    if (!FMath::IsFinite(Length) || Length <= UE_SMALL_NUMBER ||
        Length > 10000.0 || !FMath::IsFinite(Ratio) ||
        Ratio > double(InSettings.MaxSamples - 1))
    {
        OutError = TEXT("Blade length or spacing exceeds the sample budget.");
        return false;
    }
    const int32 Segments = FMath::Max(1, FMath::CeilToInt(Ratio));
    if (Segments + 1 > InSettings.MaxQueriesPerPose)
    {
        OutError = TEXT("Even one stationary blade pass exceeds MaxQueriesPerPose.");
        return false;
    }
    const double HalfSpacing = Length / (2.0 * Segments);
    Settings = InSettings;
    LocalBase = InLocalBase;
    LocalTip = InLocalTip;
    LocalBaseRadius = FMath::Sqrt(
        FMath::Square(Settings.GameplayRadius) + FMath::Square(HalfSpacing));
    LocalArcRadius = FMath::Max(LocalBase.Size(), LocalTip.Size());
    LocalSamples.Reserve(Segments + 1);
    for (int32 Index = 0; Index <= Segments; ++Index)
    {
        LocalSamples.Add(FMath::Lerp(LocalBase, LocalTip, double(Index) / Segments));
    }
    QueryParams = FCollisionQueryParams(FName(TEXT("SA_Melee")), false, Source);
    QueryParams.AddIgnoredActor(Weapon);
    for (AActor* Ignored : ExtraIgnored)
    {
        if (IsValid(Ignored))
        {
            QueryParams.AddIgnoredActor(Ignored);
        }
    }
    QueryParams.bFindInitialOverlaps = true;
    QueryParams.bIgnoreTouches = false;
    QueryParams.bIgnoreBlocks = false;
    QueryParams.bReturnPhysicalMaterial = false;
    QueryParams.bReturnFaceIndex = false;
    QueryHits.Reserve(16);
    QueryOverlaps.Reserve(16);
    CandidateIndices.Reserve(Settings.MaxCandidatesPerPose);
    bConfigured = true;
    return true;
}

bool FSABladeTraceKernel::ReadUniformScale(const FTransform& Pose, double& OutScale)
{
    OutScale = 0.0;
    if (Pose.ContainsNaN() || !Pose.GetRotation().IsNormalized())
    {
        return false;
    }
    const FVector Scale = Pose.GetScale3D();
    if (Scale.X <= UE_SMALL_NUMBER || Scale.Y <= UE_SMALL_NUMBER ||
        Scale.Z <= UE_SMALL_NUMBER || Scale.X > 1000.0)
    {
        return false;
    }
    const double Tolerance = 1.e-6 * double(Scale.X);
    if (FMath::Abs(Scale.X - Scale.Y) > Tolerance ||
        FMath::Abs(Scale.X - Scale.Z) > Tolerance)
    {
        return false;
    }
    OutScale = Scale.X;
    return true;
}

double FSABladeTraceKernel::RotationAngle(const FQuat& A, const FQuat& B)
{
    return 2.0 * FMath::Acos(FMath::Clamp(FMath::Abs(A | B), 0.0, 1.0));
}

FTransform FSABladeTraceKernel::InterpolatePose(const FSABladeTraceFrame& Frame, double Alpha)
{
    return FTransform(
        FQuat::Slerp(Frame.PreviousPose.GetRotation(),
            Frame.CurrentPose.GetRotation(), Alpha).GetNormalized(),
        FMath::Lerp(Frame.PreviousPose.GetLocation(), Frame.CurrentPose.GetLocation(), Alpha),
        Frame.PreviousPose.GetScale3D());
}

ESABladeTraceStatus FSABladeTraceKernel::ValidateFrame(
    const FSABladeTraceFrame& Frame, bool bSnapshot, double& OutScale) const
{
    double PreviousScale = 0.0;
    if (!bConfigured || !ReadUniformScale(Frame.PreviousPose, PreviousScale) ||
        !ReadUniformScale(Frame.CurrentPose, OutScale) ||
        Frame.PreviousReachAnchor.ContainsNaN() || Frame.CurrentReachAnchor.ContainsNaN() ||
        !FMath::IsFinite(Frame.DeltaSeconds) || !FMath::IsFinite(Frame.ActiveBegin) ||
        !FMath::IsFinite(Frame.ActiveEnd) || Frame.ActiveBegin < 0.0 ||
        Frame.ActiveEnd > 1.0 || Frame.ActiveBegin > Frame.ActiveEnd ||
        (!bSnapshot && Frame.DeltaSeconds <= 0.0))
    {
        return ESABladeTraceStatus::InvalidSetup;
    }
    if (FMath::Abs(PreviousScale - OutScale) >
            1.e-6 * FMath::Max(PreviousScale, OutScale) ||
        Frame.DeltaSeconds > Settings.MaxPoseDeltaSeconds ||
        FVector::Distance(Frame.PreviousPose.GetLocation(),
            Frame.CurrentPose.GetLocation()) > Settings.MaxOriginTravelWorld ||
        FVector::Distance(Frame.PreviousReachAnchor,
            Frame.CurrentReachAnchor) > Settings.MaxOriginTravelWorld ||
        RotationAngle(Frame.PreviousPose.GetRotation(), Frame.CurrentPose.GetRotation()) >
            FMath::DegreesToRadians(Settings.MaxFrameAngleDegrees))
    {
        return ESABladeTraceStatus::Discontinuity;
    }
    if (!bSnapshot && Frame.ActiveEnd <= Frame.ActiveBegin)
    {
        return ESABladeTraceStatus::EmptyInterval;
    }
    return ESABladeTraceStatus::Collected;
}

ESABladeTraceStatus FSABladeTraceKernel::PlanInterval(
    const FSABladeTraceFrame& Frame, double UniformScale,
    int32& OutSubsteps, double& OutRadius) const
{
    OutSubsteps = 1;
    OutRadius = LocalBaseRadius * UniformScale;
    if (!FMath::IsFinite(OutRadius) || OutRadius > Settings.MaxQueryRadiusWorld)
    {
        return ESABladeTraceStatus::InvalidSetup;
    }
    const double Angle = RotationAngle(Frame.PreviousPose.GetRotation(),
        Frame.CurrentPose.GetRotation()) * (Frame.ActiveEnd - Frame.ActiveBegin);
    const double ArcRadius = LocalArcRadius * UniformScale;
    if (Angle > UE_SMALL_NUMBER && ArcRadius > UE_SMALL_NUMBER)
    {
        const double CurveLimit = 2.0 * FMath::Acos(FMath::Clamp(
            1.0 - Settings.MaxCurveErrorWorld / ArcRadius, -1.0, 1.0));
        const double AngleLimit = FMath::Min(CurveLimit,
            FMath::DegreesToRadians(Settings.MaxAnglePerSubstepDegrees));
        if (AngleLimit <= 0.0)
        {
            return ESABladeTraceStatus::BudgetExceeded;
        }
        const double Ratio = Angle / AngleLimit;
        if (!FMath::IsFinite(Ratio) || Ratio > Settings.MaxSubsteps)
        {
            return ESABladeTraceStatus::BudgetExceeded;
        }
        OutSubsteps = FMath::Max(1, FMath::CeilToInt(Ratio));
        OutRadius += ArcRadius * (1.0 - FMath::Cos(Angle / (2.0 * OutSubsteps)));
    }
    return OutRadius <= Settings.MaxQueryRadiusWorld
        ? ESABladeTraceStatus::Collected : ESABladeTraceStatus::BudgetExceeded;
}

ESABladeTraceStatus FSABladeTraceKernel::Collect(UWorld& World,
    const FSABladeTraceFrame& Frame, FSABladeTraceBudget& Budget,
    TArray<FSABladeContact>& OutContacts,
    TOptional<FSABladeContact>& OutFirstWorldBlock, bool bDrawDebug)
{
    return CollectInternal(World, Frame, false, Budget,
        OutContacts, OutFirstWorldBlock, bDrawDebug);
}

ESABladeTraceStatus FSABladeTraceKernel::CollectSnapshot(UWorld& World,
    const FTransform& CurrentPose, const FVector& Anchor,
    FSABladeTraceBudget& Budget, TArray<FSABladeContact>& OutContacts,
    TOptional<FSABladeContact>& OutFirstWorldBlock, bool bDrawDebug)
{
    FSABladeTraceFrame Frame;
    Frame.PreviousPose = CurrentPose;
    Frame.CurrentPose = CurrentPose;
    Frame.PreviousReachAnchor = Anchor;
    Frame.CurrentReachAnchor = Anchor;
    Frame.ActiveBegin = 1.0;
    Frame.ActiveEnd = 1.0;
    return CollectInternal(World, Frame, true, Budget,
        OutContacts, OutFirstWorldBlock, bDrawDebug);
}

ESABladeTraceStatus FSABladeTraceKernel::CollectInternal(UWorld& World,
    const FSABladeTraceFrame& Frame, bool bSnapshot,
    FSABladeTraceBudget& Budget, TArray<FSABladeContact>& OutContacts,
    TOptional<FSABladeContact>& OutFirstWorldBlock, bool bDrawDebug)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SA_MeleeTrace_Collect);
    OutContacts.Reset();
    OutFirstWorldBlock.Reset();
    CandidateIndices.Reset();
    double Scale = 1.0;
    ESABladeTraceStatus Status = ValidateFrame(Frame, bSnapshot, Scale);
    if (Status != ESABladeTraceStatus::Collected)
    {
        return Status;
    }
    int32 Substeps = 1;
    double Radius = 0.0;
    Status = PlanInterval(Frame, Scale, Substeps, Radius);
    if (Status != ESABladeTraceStatus::Collected)
    {
        return Status;
    }
    const int64 NeededQueries = int64(LocalSamples.Num()) * Substeps;
    if (NeededQueries > int64(Budget.MaxQueries) - Budget.UsedQueries)
    {
        return ESABladeTraceStatus::BudgetExceeded;
    }
    for (int32 Step = 0; Step < Substeps; ++Step)
    {
        const double A0 = FMath::Lerp(Frame.ActiveBegin, Frame.ActiveEnd,
            double(Step) / Substeps);
        const double A1 = FMath::Lerp(Frame.ActiveBegin, Frame.ActiveEnd,
            double(Step + 1) / Substeps);
        const FTransform Pose0 = InterpolatePose(Frame, A0);
        const FTransform Pose1 = InterpolatePose(Frame, A1);
        for (const FVector& Sample : LocalSamples)
        {
            if (!QuerySample(World, Frame, Pose0.TransformPosition(Sample),
                Pose1.TransformPosition(Sample), A0, A1, Radius, Budget,
                OutContacts, OutFirstWorldBlock, bDrawDebug))
            {
                OutContacts.Reset();
                OutFirstWorldBlock.Reset();
                return ESABladeTraceStatus::BudgetExceeded;
            }
        }
    }
    OutContacts.Sort([](const FSABladeContact& A, const FSABladeContact& B)
    {
        return A.FrameAlpha < B.FrameAlpha;
    });
    return ESABladeTraceStatus::Collected;
}

bool FSABladeTraceKernel::QuerySample(UWorld& World,
    const FSABladeTraceFrame& Frame, const FVector& Start, const FVector& Finish,
    double A0, double A1, double Radius, FSABladeTraceBudget& Budget,
    TArray<FSABladeContact>& OutContacts,
    TOptional<FSABladeContact>& OutFirstWorldBlock, bool bDrawDebug)
{
    if (!Budget.TrySpendQuery())
    {
        return false;
    }
    const FCollisionShape Shape = FCollisionShape::MakeSphere(float(Radius));
    const double SegmentSeconds = Frame.DeltaSeconds * (A1 - A0);
    const FVector Velocity = SegmentSeconds > UE_SMALL_NUMBER
        ? (Finish - Start) / SegmentSeconds : FVector::ZeroVector;
    if (bDrawDebug)
    {
        DrawDebugLine(&World, Start, Finish, FColor::Cyan, false, 0.0f);
        DrawDebugSphere(&World, Start, float(Radius), 8, FColor::Cyan, false, 0.0f);
        DrawDebugSphere(&World, Finish, float(Radius), 8, FColor::Cyan, false, 0.0f);
    }
    if (Start.Equals(Finish, 1.e-6))
    {
        QueryOverlaps.Reset();
        World.OverlapMultiByChannel(QueryOverlaps, Start, FQuat::Identity,
            ECC_GameTraceChannel2, Shape, QueryParams);
        if (!Budget.TryAddRawHits(QueryOverlaps.Num()))
        {
            return false;
        }
        for (const FOverlapResult& Overlap : QueryOverlaps)
        {
            FHitResult Hit(Overlap.GetActor(), Overlap.GetComponent(), Start, FVector::ZeroVector);
            Hit.Location = Start;
            Hit.ImpactPoint = Start;
            Hit.Normal = FVector::ZeroVector;
            Hit.ImpactNormal = FVector::ZeroVector;
            Hit.TraceStart = Start;
            Hit.TraceEnd = Finish;
            Hit.Time = 0.0f;
            Hit.BoneName = NAME_None;
            Hit.bBlockingHit = Overlap.bBlockingHit;
            Hit.bStartPenetrating = false;
            if (!Accumulate(Frame, Hit, ESAContactQuality::StationaryOverlap,
                A0, Velocity, Budget, OutContacts, OutFirstWorldBlock))
            {
                return false;
            }
        }
        return true;
    }
    QueryHits.Reset();
    World.SweepMultiByChannel(QueryHits, Start, Finish, FQuat::Identity,
        ECC_GameTraceChannel2, Shape, QueryParams);
    if (!Budget.TryAddRawHits(QueryHits.Num()))
    {
        return false;
    }
    for (const FHitResult& Hit : QueryHits)
    {
        if (!FMath::IsFinite(Hit.Time))
        {
            continue;
        }
        const double Alpha = FMath::Lerp(A0, A1, FMath::Clamp(double(Hit.Time), 0.0, 1.0));
        const ESAContactQuality Quality = Hit.bStartPenetrating || Hit.Time <= 1.e-6f
            ? ESAContactQuality::InitialOverlap : ESAContactQuality::Swept;
        if (!Accumulate(Frame, Hit, Quality, Alpha, Velocity,
            Budget, OutContacts, OutFirstWorldBlock))
        {
            return false;
        }
    }
    return true;
}

bool FSABladeTraceKernel::Accumulate(const FSABladeTraceFrame& Frame,
    const FHitResult& Hit, ESAContactQuality Quality, double Alpha,
    const FVector& Velocity, FSABladeTraceBudget& Budget,
    TArray<FSABladeContact>& OutContacts,
    TOptional<FSABladeContact>& OutFirstWorldBlock)
{
    FSABladeContact Contact;
    Contact.Hit = Hit;
    Contact.Quality = Quality;
    Contact.FrameAlpha = Alpha;
    Contact.bInterpolatedPose = Frame.ActiveEnd > Frame.ActiveBegin;
    Contact.WeaponPose = InterpolatePose(Frame, Alpha);
    Contact.BladeVelocity = Velocity;
    Contact.ReachAnchor = FMath::Lerp(Frame.PreviousReachAnchor, Frame.CurrentReachAnchor, Alpha);
    if (Hit.bBlockingHit)
    {
        if (!OutFirstWorldBlock.IsSet() || Alpha < OutFirstWorldBlock->FrameAlpha)
        {
            OutFirstWorldBlock = Contact;
        }
        return true;
    }
    AActor* Target = Hit.GetActor();
    if (!IsValid(Target))
    {
        return true;
    }
    const TWeakObjectPtr<AActor> Key(Target);
    if (const int32* ExistingIndex = CandidateIndices.Find(Key))
    {
        if (Alpha < OutContacts[*ExistingIndex].FrameAlpha)
        {
            OutContacts[*ExistingIndex] = Contact;
        }
        return true;
    }
    if (!Budget.TryAddCandidate())
    {
        return false;
    }
    CandidateIndices.Add(Key, OutContacts.Add(Contact));
    return true;
}

ESABladeTraceStatus FSABladeTraceKernel::CheckReach(UWorld& World,
    const FSABladeContact& Contact, FSABladeTraceBudget& Budget, bool& OutReachable)
{
    OutReachable = false;
    double Scale = 1.0;
    if (!bConfigured || !ReadUniformScale(Contact.WeaponPose, Scale) ||
        Contact.ReachAnchor.ContainsNaN())
    {
        return ESABladeTraceStatus::InvalidSetup;
    }
    const FVector Hilt = Contact.WeaponPose.TransformPosition(LocalBase);
    const FVector Point = Contact.Quality == ESAContactQuality::Swept
        ? FVector(Contact.Hit.ImpactPoint) : FVector(Contact.Hit.Location);
    const double PathLength = FVector::Distance(Contact.ReachAnchor, Hilt) +
        FVector::Distance(Hilt, Point);
    if (Hilt.ContainsNaN() || Point.ContainsNaN() || !FMath::IsFinite(PathLength))
    {
        return ESABladeTraceStatus::InvalidSetup;
    }
    if (PathLength > Settings.MaxReachDistanceWorld)
    {
        return ESABladeTraceStatus::Collected;
    }
    const FVector Points[] = { Contact.ReachAnchor, Hilt, Point };
    const FCollisionShape Shape = FCollisionShape::MakeSphere(float(Settings.ReachRadiusWorld));
    for (int32 Segment = 0; Segment < 2; ++Segment)
    {
        if (!Budget.TrySpendQuery())
        {
            return ESABladeTraceStatus::BudgetExceeded;
        }
        bool bBlocked = false;
        if (Points[Segment].Equals(Points[Segment + 1], 1.e-6))
        {
            bBlocked = World.OverlapBlockingTestByChannel(Points[Segment], FQuat::Identity,
                ECC_GameTraceChannel2, Shape, QueryParams);
        }
        else
        {
            FHitResult Obstruction;
            bBlocked = World.SweepSingleByChannel(Obstruction, Points[Segment],
                Points[Segment + 1], FQuat::Identity, ECC_GameTraceChannel2, Shape, QueryParams);
        }
        if (!Budget.TryAddRawHits(bBlocked ? 1 : 0))
        {
            return ESABladeTraceStatus::BudgetExceeded;
        }
        if (bBlocked)
        {
            return ESABladeTraceStatus::Collected;
        }
    }
    OutReachable = true;
    return ESABladeTraceStatus::Collected;
}

int32 FSABladeTraceKernel::GetSampleCount() const
{
    return LocalSamples.Num();
}
