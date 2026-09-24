#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/SAInventorySaveGame.h"
#include "Core/Types/SAActionTypes.h"
#include "Magic/SAMagicTypes.h"
#include "SAInventoryComponent.generated.h"
class ASAPickup;
struct FStreamableHandle;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSAInventoryRestored, bool, Success);
USTRUCT(BlueprintType)
struct FSAInventoryChangeSet
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int64 Revision = 0;
    UPROPERTY(BlueprintReadOnly) TArray<FSAItemInstance> Items;
    UPROPERTY(BlueprintReadOnly) TArray<FGuid> Quickbar;
    UPROPERTY(BlueprintReadOnly) FGuid EquippedItemId;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSAInventoryChangedBP, FSAInventoryChangeSet, Change);
DECLARE_MULTICAST_DELEGATE_OneParam(FSAInventoryChanged, const FSAInventoryChangeSet&);
UCLASS(ClassGroup=(ShadowAges), meta=(BlueprintSpawnableComponent))
class SHADOWAGES_API USAInventoryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USAInventoryComponent();
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxSlots = 32;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<ASAPickup> DropClass;
    UPROPERTY(BlueprintAssignable) FSAInventoryChangedBP OnItemsChangedBP;
    FSAInventoryChanged OnItemsChanged;
    UFUNCTION(BlueprintCallable) bool RegisterDefinition(USAItemDefinition* Definition);
    USAItemDefinition* FindDefinition(FPrimaryAssetId Id) const;
    UFUNCTION(BlueprintPure) TArray<FSAItemInstance> GetItems() const { return Entries; }
    UFUNCTION(BlueprintPure) bool TryGetItemCopy(FGuid Id, FSAItemInstance& Item) const;
    UFUNCTION(BlueprintPure) int32 AvailableCount(FGuid Id) const;
    UFUNCTION(BlueprintCallable) int32 TryAdd(USAItemDefinition* Definition, int32 Count, bool AllOrNothing = true);
    int32 AddTemplate(const FSAItemInstance& Item, bool AllOrNothing, bool PreserveIdentity = false);
    UFUNCTION(BlueprintCallable) bool TryRemove(FGuid Id, int32 Count);
    bool TryReserve(FSAActionHandle Action, FGuid Id, int32 Count, FGuid& Reservation);
    bool CommitReservation(FSAActionHandle Action, FGuid Reservation);
    void ReleaseReservation(FSAActionHandle Action, FGuid Reservation);
    UFUNCTION(BlueprintCallable) ESACombatRequestResult RequestUse(FGuid Id, const FSASpellAim& Aim, FSAActionHandle& Action);
    UFUNCTION(BlueprintCallable) bool CommitConsumable(FSAActionHandle Action);
    UFUNCTION(BlueprintCallable) bool SetQuickbar(int32 Slot, FGuid Id);
    UFUNCTION(BlueprintPure) FSAInventorySave MakeSnapshot() const;
    UFUNCTION(BlueprintCallable) bool RestoreSnapshot(const FSAInventorySave& Snapshot);
    // Resolves saved definition IDs without blocking the game thread. Unknown IDs fail the entire restore.
    UFUNCTION(BlueprintCallable) bool RestoreSnapshotAsync(const FSAInventorySave& Snapshot);
    UPROPERTY(BlueprintAssignable) FSAInventoryRestored OnRestoreFinished;
    UFUNCTION(BlueprintCallable) ASAPickup* TryDrop(FGuid Id, int32 Count);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Tick) override;
private:
    friend class ASAPickup;
    friend class USAEquipmentComponent;
    struct FReservation { FSAActionHandle Action; FGuid Item; int32 Count; };
    bool ValidateItem(const FSAItemInstance& Item) const;
    bool CanWrite() const;
    void Changed();
    void FlushChanges();
    void ActionFinished(FSAActionHandle Action, ESAActionEndReason Reason);
    void OwnerDied();
    UPROPERTY(Transient) TArray<FSAItemInstance> Entries;
    UPROPERTY(Transient) TMap<FPrimaryAssetId, TObjectPtr<USAItemDefinition>> Definitions;
    UPROPERTY(Transient) TArray<FGuid> Quickbar;
    TMap<FGuid, FReservation> Reservations;
    TArray<FSAInventoryChangeSet> PendingChanges;
    int64 Revision = 0;
    bool bLocked = false;
    bool bPublishing = false;
    bool bEnding = false;
    FSAActionHandle ConsumeAction;
    FGuid ConsumeReservation;
    float ConsumeHealing = 0;
    double ConsumeAt = 0;
    FDelegateHandle FinishSubscription, DeathSubscription;
    TSharedPtr<FStreamableHandle> RestoreLoad;
    FGuid RestoreRequest;
};
