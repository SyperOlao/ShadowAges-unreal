#pragma once

#include "CoreMinimal.h"
#include "Core/Types/SACombatTypes.h"
#include "SAAnatomyTypes.generated.h"

class USAAnatomyComponent;
class USkeletalMesh;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class ESALimbCondition : uint8 { Intact, Injured, Severed };

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSALimbState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FName ZoneId;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    ESALimbCondition Condition = ESALimbCondition::Intact;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    float AccumulatedDamage = 0.f;
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAAnatomyContactSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FName ZoneId;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FSAHitContext Context;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FTransform TargetTransform = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FTransform BoneTransform = FTransform::Identity;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    TWeakObjectPtr<USkeletalMesh> MeshAsset;
    FGuid LifeId;
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSASeverIntent
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FName ZoneId;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FName BoundaryBone;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FVector ContactPointWS = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FVector SuggestedPlaneNormalWS = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    FSAActionHandle SourceAction;
    UPROPERTY(BlueprintReadOnly, Category="Anatomy")
    bool bFatalHit = false;
};

// Only the issuing component can construct a deliverable batch. Copying does not
// duplicate delivery rights. All presentation context belongs to this transaction.
struct SHADOWAGES_API FSALimbChangeBatch
{
private:
    friend class USAAnatomyComponent;
    TWeakObjectPtr<USAAnatomyComponent> Issuer;
    FGuid LifeId, Token;
    FSALimbState State;
    uint64 Revision = 0;
    FSAAnatomyContactSnapshot Snapshot;
    FSASeverIntent Intent;
    bool bRequestSever = false;
    bool bFatal = false;
};
