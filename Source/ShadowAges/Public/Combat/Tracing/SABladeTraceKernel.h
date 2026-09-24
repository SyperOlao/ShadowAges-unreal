// Source/ShadowAges/Public/Combat/Tracing/SABladeTraceKernel.h
#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "Combat/Tracing/SABladeTraceProfile.h"
#include "Core/Types/SACombatTypes.h"

class AActor;
class UWorld;

struct SHADOWAGES_API FSABladeTraceFrame
{
    FTransform PreviousPose = FTransform::Identity;
    FTransform CurrentPose = FTransform::Identity;
    FVector PreviousReachAnchor = FVector::ZeroVector;
    FVector CurrentReachAnchor = FVector::ZeroVector;
    double DeltaSeconds = 0.0;
    double ActiveBegin = 0.0;
    double ActiveEnd = 1.0;
};

struct SHADOWAGES_API FSABladeContact
{
    FHitResult Hit;
    FTransform WeaponPose = FTransform::Identity;
    FVector BladeVelocity = FVector::ZeroVector;
    FVector ReachAnchor = FVector::ZeroVector;
    double FrameAlpha = 0.0;
    ESAContactQuality Quality = ESAContactQuality::StationaryOverlap;
    bool bInterpolatedPose = true;
};

struct SHADOWAGES_API FSABladeTraceBudget
{
    int32 MaxQueries = 0;
    int32 MaxRawHits = 0;
    int32 MaxCandidates = 0;
    int64 UsedQueries = 0;
    int64 UsedRawHits = 0;
    int64 UsedCandidates = 0;

    void Reset(const FSABladeTraceSettings& Settings);
    bool TrySpendQuery();
    bool TryAddRawHits(int32 Count);
    bool TryAddCandidate();
};

enum class ESABladeTraceStatus : uint8
{
    Collected,
    EmptyInterval,
    InvalidSetup,
    Discontinuity,
    BudgetExceeded
};

class SHADOWAGES_API FSABladeTraceKernel
{
public:
    bool Configure(const FSABladeTraceSettings& InSettings,
        const FVector& InLocalBase, const FVector& InLocalTip,
        const AActor* Source, const AActor* Weapon,
        const TArray<AActor*>& ExtraIgnored, FString& OutError);

    ESABladeTraceStatus Collect(UWorld& World,
        const FSABladeTraceFrame& Frame, FSABladeTraceBudget& Budget,
        TArray<FSABladeContact>& OutContacts,
        TOptional<FSABladeContact>& OutFirstWorldBlock,
        bool bDrawDebug = false);

    ESABladeTraceStatus CollectSnapshot(UWorld& World,
        const FTransform& CurrentPose, const FVector& Anchor,
        FSABladeTraceBudget& Budget, TArray<FSABladeContact>& OutContacts,
        TOptional<FSABladeContact>& OutFirstWorldBlock,
        bool bDrawDebug = false);

    ESABladeTraceStatus CheckReach(UWorld& World,
        const FSABladeContact& Contact, FSABladeTraceBudget& Budget,
        bool& OutReachable);

    int32 GetSampleCount() const;

private:
    static bool ReadUniformScale(const FTransform& Pose, double& OutScale);
    static double RotationAngle(const FQuat& A, const FQuat& B);
    static FTransform InterpolatePose(const FSABladeTraceFrame& Frame, double Alpha);

    ESABladeTraceStatus CollectInternal(UWorld& World,
        const FSABladeTraceFrame& Frame, bool bSnapshot,
        FSABladeTraceBudget& Budget, TArray<FSABladeContact>& OutContacts,
        TOptional<FSABladeContact>& OutFirstWorldBlock, bool bDrawDebug);

    ESABladeTraceStatus ValidateFrame(const FSABladeTraceFrame& Frame,
        bool bSnapshot, double& OutScale) const;

    ESABladeTraceStatus PlanInterval(const FSABladeTraceFrame& Frame,
        double UniformScale, int32& OutSubsteps, double& OutRadius) const;

    bool QuerySample(UWorld& World, const FSABladeTraceFrame& Frame,
        const FVector& Start, const FVector& Finish,
        double A0, double A1, double Radius,
        FSABladeTraceBudget& Budget, TArray<FSABladeContact>& OutContacts,
        TOptional<FSABladeContact>& OutFirstWorldBlock, bool bDrawDebug);

    bool Accumulate(const FSABladeTraceFrame& Frame, const FHitResult& Hit,
        ESAContactQuality Quality, double Alpha, const FVector& Velocity,
        FSABladeTraceBudget& Budget, TArray<FSABladeContact>& OutContacts,
        TOptional<FSABladeContact>& OutFirstWorldBlock);

    FSABladeTraceSettings Settings;
    FVector LocalBase = FVector::ZeroVector;
    FVector LocalTip = FVector::ZeroVector;
    TArray<FVector> LocalSamples;
    TArray<FHitResult> QueryHits;
    TArray<FOverlapResult> QueryOverlaps;
    TMap<TWeakObjectPtr<AActor>, int32> CandidateIndices;
    FCollisionQueryParams QueryParams;
    double LocalBaseRadius = 0.0;
    double LocalArcRadius = 0.0;
    bool bConfigured = false;
};
