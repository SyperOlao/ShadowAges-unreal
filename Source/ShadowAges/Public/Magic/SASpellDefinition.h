#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Magic/SAMagicTypes.h"
#include "SASpellDefinition.generated.h"

class ASAProjectile;
class UAnimMontage;

UCLASS(BlueprintType)
class SHADOWAGES_API USASpellDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spell")
    FGameplayTag SpellTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spell")
    FGameplayTag CooldownGroup;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spell")
    ESASpellDelivery Delivery = ESASpellDelivery::Projectile;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spell")
    ESASpellTargetPolicy TargetPolicy = ESASpellTargetPolicy::Hostile;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spell")
    float ManaCost = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spell")
    float Damage = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spell")
    FName DamageType = TEXT("Damage.Fire");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing")
    float WindupSeconds = .35f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing")
    float RecoverySeconds = .25f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing")
    float CooldownSeconds = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
    float RangeCm = 2000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
    float ProjectileSpeedCmS = 500.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
    float ProjectileRadiusCm = 8.f;
    // Instant only: zero = explicit target (or self), positive = area at aim point.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
    float AreaRadiusCm = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
    int32 MaxAreaTargets = 8;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
    bool bRequireAreaLineOfSight = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
    FName MuzzleSocket = TEXT("Spell_Muzzle");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
    bool bUseComponentOrigin = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Assets")
    TSoftClassPtr<ASAProjectile> ProjectileClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Assets")
    TSoftObjectPtr<UAnimMontage> CastMontage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spell")
    TArray<FSAEffectSpec> ImpactEffects;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast")
    FSACastPhasePolicy WindupPolicy;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cast")
    FSACastPhasePolicy RecoveryPolicy;

    bool Validate(FString& Error) const;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
