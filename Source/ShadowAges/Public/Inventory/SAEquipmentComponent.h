#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SAEquipmentComponent.generated.h"
class ASAWeaponActor;
class USAItemDefinition;
class USkeletalMeshComponent;
struct FStreamableHandle;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSAEquipmentChanged, FGuid, EquippedItem);
UENUM(BlueprintType)
enum class ESAEquipResult : uint8 { Requested, AlreadyEquipped, Rejected };
UCLASS(ClassGroup=(ShadowAges), meta=(BlueprintSpawnableComponent))
class SHADOWAGES_API USAEquipmentComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USAEquipmentComponent();
    UFUNCTION(BlueprintCallable) void ConfigureMesh(USkeletalMeshComponent* Mesh);
    UFUNCTION(BlueprintCallable) ESAEquipResult RequestEquip(FGuid Item);
    UFUNCTION(BlueprintCallable) bool Unequip();
    UFUNCTION(BlueprintPure) FGuid GetEquippedItemId() const { return EquippedId; }
    UFUNCTION(BlueprintPure) ASAWeaponActor* GetWeaponActor() const { return Weapon; }
    UFUNCTION(BlueprintPure) bool IsPreparing() const { return PendingRequest.IsValid(); }
    UPROPERTY(BlueprintAssignable) FSAEquipmentChanged OnEquipmentChanged;
    void CancelPending();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Tick) override;
private:
    void ContinueEquip(FGuid Request);
    void OwnerDied();
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> CharacterMesh;
    UPROPERTY(Transient) TObjectPtr<ASAWeaponActor> Weapon;
    UPROPERTY(Transient) TObjectPtr<USAItemDefinition> CurrentDefinition;
    UPROPERTY(Transient) TObjectPtr<USAItemDefinition> PendingDefinition;
    FGuid EquippedId, PendingItem, PendingRequest;
    TSharedPtr<FStreamableHandle> PendingLoad, CurrentLoad;
    bool bCommitting = false;
    bool bEnding = false;
    bool bReady = false;
    bool bDependenciesRequested = false;
    TSharedPtr<FStreamableHandle> DependencyLoad, CurrentDependencyLoad;
    FDelegateHandle DeathSubscription;
};
