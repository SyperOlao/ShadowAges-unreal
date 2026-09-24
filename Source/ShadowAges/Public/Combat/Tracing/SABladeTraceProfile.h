// Source/ShadowAges/Public/Combat/Tracing/SABladeTraceProfile.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SABladeTraceProfile.generated.h"

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSABladeTraceSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = "0.01", Units = "cm"))
    double GameplayRadius = 4.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = "0.01", Units = "cm"))
    double MaxSpacing = 8.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0.001", Units = "cm"))
    double MaxCurveErrorWorld = 1.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accuracy", meta = (ClampMin = "0.1", ClampMax = "180.0"))
    double MaxAnglePerSubstepDegrees = 20.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Continuity", meta = (ClampMin = "0.001", ClampMax = "1.0"))
    double MaxPoseDeltaSeconds = 0.12;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Continuity", meta = (ClampMin = "0.01", Units = "cm"))
    double MaxOriginTravelWorld = 150.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Continuity", meta = (ClampMin = "0.1", ClampMax = "180.0"))
    double MaxFrameAngleDegrees = 100.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "2", ClampMax = "128"))
    int32 MaxSamples = 24;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "32"))
    int32 MaxSubsteps = 8;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "4096"))
    int32 MaxQueriesPerPose = 384;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "65536"))
    int32 MaxRawHitsPerPose = 2048;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "1", ClampMax = "256"))
    int32 MaxCandidatesPerPose = 64;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "0.01", Units = "cm"))
    double MaxQueryRadiusWorld = 16.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reach", meta = (ClampMin = "0.01", Units = "cm"))
    double ReachRadiusWorld = 0.5;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reach", meta = (ClampMin = "0.01", Units = "cm"))
    double MaxReachDistanceWorld = 250.0;

    bool Validate(FString& OutError) const;
};

UCLASS(BlueprintType)
class SHADOWAGES_API USABladeTraceProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blade")
    FName ProfileId = TEXT("DefaultBlade");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blade")
    FName BaseSocket = TEXT("BladeBase");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blade")
    FName TipSocket = TEXT("BladeTip");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blade")
    FSABladeTraceSettings Settings;

    bool Validate(FString& OutError) const;
};
