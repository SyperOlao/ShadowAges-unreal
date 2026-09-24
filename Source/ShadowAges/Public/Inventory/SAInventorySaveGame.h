#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Inventory/SAItemDefinition.h"
#include "SAInventorySaveGame.generated.h"
USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAInventorySave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadWrite) int32 SchemaVersion = 1;
    UPROPERTY(SaveGame, BlueprintReadWrite) TArray<FSAItemInstance> Items;
    UPROPERTY(SaveGame, BlueprintReadWrite) FGuid EquippedItemId;
    UPROPERTY(SaveGame, BlueprintReadWrite) TArray<FGuid> QuickbarItemIds;
};
UCLASS()
class SHADOWAGES_API USAInventorySaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame, BlueprintReadWrite) FSAInventorySave Inventory;
};
