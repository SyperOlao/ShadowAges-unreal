// Source/ShadowAges/Public/Core/Types/SACombatTypes.h
#pragma once

#include "CoreMinimal.h"
#include "Core/Types/SAActionTypes.h"
#include "Engine/HitResult.h"
#include "SACombatTypes.generated.h"

class AActor;
class AController;

UENUM(BlueprintType)
enum class ESAContactQuality : uint8
{
    Swept,
    InitialOverlap,
    StationaryOverlap
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAHitContext
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    FSAMeleePlaybackKey Playback;

    UPROPERTY(BlueprintReadOnly, Category="SA|Melee")
    FName DamageType = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    int32 WindowSerial = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    TWeakObjectPtr<AActor> SourceActor;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    TWeakObjectPtr<AController> SourceController;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    TWeakObjectPtr<AActor> DamageCauser;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    TWeakObjectPtr<AActor> TargetActor;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    FGuid SourceCreditId;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    FHitResult Hit;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    FTransform WeaponTransform = FTransform::Identity;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    FVector BladeDirection = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    FVector BladeVelocity = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    double FrameAlpha = 0.0;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    ESAContactQuality ContactQuality = ESAContactQuality::StationaryOverlap;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    bool bInterpolatedPose = true;
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAMeleeHitRequest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    FSAHitContext Context;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    float ProposedDamage = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    float ProposedPoiseDamage = 0.0f;
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSADamageResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    bool bAccepted = false;

    UPROPERTY(BlueprintReadOnly, Category="SA|Melee")
    bool bBlocked = false;

    UPROPERTY(BlueprintReadOnly, Category="SA|Melee")
    bool bParried = false;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    bool bFatal = false;

    UPROPERTY(BlueprintReadOnly, Category = "SA|Melee")
    float AppliedHealthDamage = 0.0f;
};

// Defense decides before HP is committed. It never applies HP or presentation itself.
USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAIncomingDamageDecision
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SA|Damage")
    bool bAccepted = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SA|Damage")
    bool bBlocked = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SA|Damage")
    bool bParried = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SA|Damage")
    float HealthDamage = 0.f;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(
    FSAOnMeleeContactResolved, const FSAHitContext&, const FSADamageResult&);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FSAOnMeleeContactResolvedBP,
    const FSAHitContext&, Context, const FSADamageResult&, Result);

DECLARE_MULTICAST_DELEGATE_OneParam(
    FSAOnMeleeWorldContact, const FSAHitContext&);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FSAOnMeleeWorldContactBP, const FSAHitContext&, Context);
