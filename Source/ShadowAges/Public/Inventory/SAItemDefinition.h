#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "SAItemDefinition.generated.h"

class ASAWeaponActor;
class USAWeaponMoveset;
class USASpellDefinition;
class UTexture2D;
class UStaticMesh;
class USABladeTraceProfile;
UENUM(BlueprintType)
enum class ESAItemUseKind : uint8 { None, MeleeWeapon, Spell, Consumable };

UCLASS(BlueprintType)
class SHADOWAGES_API USAItemDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName DefinitionKey;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) ESAItemUseKind UseKind = ESAItemUseKind::None;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) int32 MaxStack = 1;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float MaxDurability = 0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) bool bCanBeThrown = true;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(AssetBundles="UI")) TSoftObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(AssetBundles="Equipped")) TSoftClassPtr<ASAWeaponActor> EquipmentActorClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(AssetBundles="Equipped")) TSoftObjectPtr<USAWeaponMoveset> Moveset;
    // Optional overrides allow the passive native weapon class to be used directly.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(AssetBundles="Equipped")) TSoftObjectPtr<UStaticMesh> BladeMesh;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(AssetBundles="Equipped")) TSoftObjectPtr<USABladeTraceProfile> TraceProfile;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(AssetBundles="Equipped")) TSoftObjectPtr<USASpellDefinition> Spell;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName AttachmentSocket = TEXT("hand_r");
    // First consumable policy: self healing; full health rejects without spending.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float Healing = 25;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float ConsumeDelay = .35f;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override;
    bool Validate() const;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAItemInstance
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly) FGuid InstanceId;
    UPROPERTY(SaveGame, BlueprintReadOnly) FPrimaryAssetId DefinitionId;
    UPROPERTY(SaveGame, BlueprintReadOnly) int32 Count = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly) float Durability = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly) int32 AffixSeed = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly) FGameplayTagContainer InstanceTags;
};
