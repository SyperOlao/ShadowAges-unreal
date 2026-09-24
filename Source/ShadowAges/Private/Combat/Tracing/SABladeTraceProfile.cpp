// Source/ShadowAges/Private/Combat/Tracing/SABladeTraceProfile.cpp
#include "Combat/Tracing/SABladeTraceProfile.h"

bool FSABladeTraceSettings::Validate(FString& OutError) const
{
    OutError.Reset();
    const auto InRange = [](double Value, double Min, double Max)
    {
        return FMath::IsFinite(Value) && Value >= Min && Value <= Max;
    };
    if (!InRange(GameplayRadius, 0.01, 1000.0) ||
        !InRange(MaxSpacing, 0.01, 1000.0) ||
        !InRange(MaxCurveErrorWorld, 0.001, 1000.0) ||
        !InRange(MaxAnglePerSubstepDegrees, 0.1, 180.0) ||
        !InRange(MaxPoseDeltaSeconds, 0.001, 1.0) ||
        !InRange(MaxOriginTravelWorld, 0.01, 10000.0) ||
        !InRange(MaxFrameAngleDegrees, 0.1, 180.0) ||
        !InRange(MaxQueryRadiusWorld, 0.01, 1000.0) ||
        !InRange(ReachRadiusWorld, 0.01, 100.0) ||
        !InRange(MaxReachDistanceWorld, 0.01, 10000.0))
    {
        OutError = TEXT("Blade trace contains a non-finite or out-of-range scalar.");
        return false;
    }
    if (MaxSamples < 2 || MaxSamples > 128 ||
        MaxSubsteps < 1 || MaxSubsteps > 32 ||
        MaxQueriesPerPose < 1 || MaxQueriesPerPose > 4096 ||
        MaxRawHitsPerPose < 1 || MaxRawHitsPerPose > 65536 ||
        MaxCandidatesPerPose < 1 || MaxCandidatesPerPose > 256)
    {
        OutError = TEXT("Blade trace budget is outside the supported limits.");
        return false;
    }
    if (ReachRadiusWorld > MaxQueryRadiusWorld ||
        MaxCandidatesPerPose > MaxRawHitsPerPose)
    {
        OutError = TEXT("Reach radius or candidate budget contradicts its enclosing limit.");
        return false;
    }
    return true;
}

bool USABladeTraceProfile::Validate(FString& OutError) const
{
    OutError.Reset();
    if (ProfileId.IsNone() || BaseSocket.IsNone() || TipSocket.IsNone() ||
        BaseSocket == TipSocket)
    {
        OutError = TEXT("ProfileId and two distinct blade sockets are required.");
        return false;
    }
    return Settings.Validate(OutError);
}
