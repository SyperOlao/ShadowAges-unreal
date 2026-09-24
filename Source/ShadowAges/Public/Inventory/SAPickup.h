#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/SAItemDefinition.h"
#include "SAPickup.generated.h"
class USAInventoryComponent;
class USphereComponent;
class UStaticMeshComponent;
UCLASS(Blueprintable)
class SHADOWAGES_API ASAPickup : public AActor
{
    GENERATED_BODY()
public:
    ASAPickup();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USphereComponent> InteractionShape;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USAItemDefinition> Definition;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 InitialCount = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float InteractionDistance = 250;
    UFUNCTION(BlueprintPure) FSAItemInstance GetItem() const { return Item; }
    UFUNCTION(BlueprintPure) bool IsInteractable(USAInventoryComponent* Inventory) const;
    UFUNCTION(BlueprintCallable) int32 TryTransferTo(USAInventoryComponent* Inventory);
    // Native transfer preparation. No events and no interaction before activation.
    bool Prepare(const FSAItemInstance& Value, USAItemDefinition* Def);
    void ActivatePickup();
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY(Transient) FSAItemInstance Item;
    bool bActive = false;
    bool bClaimed = false;
    bool bPrepared = false;
};
