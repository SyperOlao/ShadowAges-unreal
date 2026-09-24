#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SAAnatomyDefinition.generated.h"

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAAnatomyZone
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy")
    FName ZoneId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy")
    FName RootBone;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy", meta=(ClampMin="0.001"))
    float InjuryThreshold = 25.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy", meta=(ClampMin="0.001"))
    float DamageCap = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy", meta=(ClampMin="0.0"))
    float DamageMultiplier = 1.f;
    // Cosmetic corpse slicing only; never causes an additional health transaction.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy")
    bool bAllowCorpseSlice = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy", meta=(ClampMin="0.001"))
    float SeverThreshold = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy")
    FName SeverDamageType = TEXT("Damage.Slash");
    // Reserved for supported authored severing. Injury does not remove abilities.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy")
    TArray<FName> FunctionalBodyTags;
};

UCLASS(BlueprintType)
class SHADOWAGES_API USAAnatomyDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy")
    TArray<FSAAnatomyZone> Zones;

    bool Validate(FString& Error) const;
};
