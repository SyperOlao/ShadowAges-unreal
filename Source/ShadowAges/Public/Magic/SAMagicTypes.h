#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/Types/SACombatTypes.h"
#include "Core/Types/SAMeleeTypes.h"
#include "SAMagicTypes.generated.h"

UENUM(BlueprintType)
enum class ESASpellDelivery : uint8 { Projectile, Instant, Ray };
UENUM(BlueprintType)
enum class ESACastPhase : uint8 { None, Windup, PreparingRelease, Recovery };
UENUM(BlueprintType)
enum class ESACastFailure : uint8
{
    None, Busy, Dead, Silenced, NotEnoughMana, Cooldown, AssetsNotReady,
    MuzzleBlocked, InvalidAim, UnsupportedDelivery, InvalidData, MissingDependencies,
    InvalidHandle, SpawnFailed, Interrupted
};
UENUM(BlueprintType)
enum class ESASpellTargetPolicy : uint8 { Hostile, Friendly, Any, Self };
UENUM(BlueprintType)
enum class ESAEffectLifetime : uint8 { Instant, Duration };
UENUM(BlueprintType)
enum class ESAEffectStackRule : uint8 { Refresh, Replace, AddStacks, Ignore };
UENUM(BlueprintType)
enum class ESAEffectKind : uint8 { Damage, RestoreMana, Slow, Silence, Disarm };
UENUM(BlueprintType)
enum class ESAEffectRemovalReason : uint8 { Expired, Cleansed, Replaced, OwnerDied, EndPlay };

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAEffectSpec
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect")
    FGameplayTag EffectTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect")
    ESAEffectKind Kind = ESAEffectKind::Damage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect")
    ESAEffectLifetime Lifetime = ESAEffectLifetime::Instant;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect")
    ESAEffectStackRule StackRule = ESAEffectStackRule::Refresh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect", meta=(ClampMin="0"))
    float Magnitude = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect", meta=(ClampMin="0"))
    float Duration = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect", meta=(ClampMin="0"))
    float Period = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect", meta=(ClampMin="0", ClampMax="1"))
    float Chance = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect", meta=(ClampMin="1", ClampMax="32"))
    int32 MaxStacks = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect")
    bool bGroupBySource = true;
    bool Validate(FString& Error) const;
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSASpellAim
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim")
    FVector ViewOrigin = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim")
    FVector ViewDirection = FVector::ForwardVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim")
    TWeakObjectPtr<AActor> Target;
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSACastPhasePolicy
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast")
    FSAAttackMovementPolicy Movement;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast")
    bool bAllowDodge = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast")
    bool bAllowEquip = true;
};

// Immutable release/effect snapshot. Weak source pointers never keep a dead caster alive.
USTRUCT(BlueprintType)
struct SHADOWAGES_API FSASpellPayload
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Spell")
    FSAHitContext Context;
    UPROPERTY(BlueprintReadOnly, Category="Spell")
    FName SourceFaction;
    UPROPERTY(BlueprintReadOnly, Category="Spell")
    FGameplayTag SpellTag;
    UPROPERTY(BlueprintReadOnly, Category="Spell")
    ESASpellTargetPolicy TargetPolicy = ESASpellTargetPolicy::Hostile;
    UPROPERTY(BlueprintReadOnly, Category="Spell")
    float Damage = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Spell")
    TArray<FSAEffectSpec> Effects;
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAActiveEffect
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Effect")
    FGuid Id;
    UPROPERTY(BlueprintReadOnly, Category="Effect")
    FSAEffectSpec Spec;
    UPROPERTY(BlueprintReadOnly, Category="Effect")
    FSASpellPayload Source;
    UPROPERTY(BlueprintReadOnly, Category="Effect")
    int32 Stacks = 1;
    UPROPERTY(BlueprintReadOnly, Category="Effect")
    double AppliedAt = 0.;
    UPROPERTY(BlueprintReadOnly, Category="Effect")
    double ExpiresAt = 0.;
    int64 NextTickIndex = 1;
    double NextTickAt() const { return AppliedAt + double(NextTickIndex) * Spec.Period; }
};
