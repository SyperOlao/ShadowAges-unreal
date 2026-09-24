#include "Inventory/SAInventoryComponent.h"
#include "Inventory/SAEquipmentComponent.h"
#include "Inventory/SAPickup.h"
#include "Combat/SACombatComponent.h"
#include "Magic/SASpellcastingComponent.h"
#include "Vitals/SAVitalsComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

USAInventoryComponent::USAInventoryComponent()
{ PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.bStartWithTickEnabled = false; DropClass = ASAPickup::StaticClass(); }
bool USAInventoryComponent::CanWrite() const
{ return IsInGameThread() && !bLocked && !RestoreRequest.IsValid() && !bEnding && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed(); }
bool USAInventoryComponent::RegisterDefinition(USAItemDefinition* Def)
{
    if (!CanWrite() || !IsValid(Def) || !Def->Validate()) return false;
    auto* Existing = FindDefinition(Def->GetPrimaryAssetId());
    if (Existing && Existing != Def) return false;
    Definitions.Add(Def->GetPrimaryAssetId(), Def); return true;
}
USAItemDefinition* USAInventoryComponent::FindDefinition(FPrimaryAssetId Id) const
{
    if (const auto* Found = Definitions.Find(Id)) return Found->Get();
    return Cast<USAItemDefinition>(UAssetManager::Get().GetPrimaryAssetObject(Id));
}
bool USAInventoryComponent::ValidateItem(const FSAItemInstance& Item) const
{
    const auto* Def = FindDefinition(Item.DefinitionId);
    return Def && Def->GetPrimaryAssetId() == Item.DefinitionId && Def->Validate() && Item.InstanceId.IsValid() && Item.Count > 0 && Item.Count <= Def->MaxStack
        && FMath::IsFinite(Item.Durability) && Item.Durability >= 0 && Item.Durability <= Def->MaxDurability
        && (Item.AffixSeed == 0 || Def->MaxStack == 1);
}
bool USAInventoryComponent::TryGetItemCopy(FGuid Id, FSAItemInstance& Item) const
{
    for (const auto& E : Entries) if (E.InstanceId == Id) { Item = E; return true; }
    Item = {}; return false;
}
int32 USAInventoryComponent::AvailableCount(FGuid Id) const
{
    FSAItemInstance Item;
    if (!TryGetItemCopy(Id, Item)) return 0;
    for (const auto& Pair : Reservations) if (Pair.Value.Item == Id) Item.Count -= Pair.Value.Count;
    return FMath::Max(0, Item.Count);
}
int32 USAInventoryComponent::TryAdd(USAItemDefinition* Def, int32 Count, bool AllOrNothing)
{
    if (!RegisterDefinition(Def) || Count <= 0) return 0;
    FSAItemInstance Item;
    Item.DefinitionId = Def->GetPrimaryAssetId(); Item.Count = Count; Item.Durability = Def->MaxDurability;
    return AddTemplate(Item, AllOrNothing);
}
int32 USAInventoryComponent::AddTemplate(const FSAItemInstance& Item, bool AllOrNothing, bool PreserveIdentity)
{
    if (!CanWrite() || Item.Count <= 0 || MaxSlots < 0 || MaxSlots > 4096) return 0;
    FSAItemInstance Probe = Item; Probe.InstanceId = FGuid::NewGuid(); Probe.Count = 1;
    if (!ValidateItem(Probe)) return 0;
    FSAItemInstance Existing;
    if (PreserveIdentity && (!Item.InstanceId.IsValid() || TryGetItemCopy(Item.InstanceId, Existing))) return 0;
    const auto* Def = FindDefinition(Item.DefinitionId);
    TArray<FSAItemInstance> After = Entries;
    int32 Remaining = Item.Count;
    for (auto& E : After)
    {
        if (Remaining == 0) break;
        if (AvailableCount(E.InstanceId) != E.Count) continue;
        if (Def->MaxStack > 1 && Def->MaxDurability == 0 && E.AffixSeed == 0 && Item.AffixSeed == 0
            && E.DefinitionId == Item.DefinitionId && E.Durability == Item.Durability && E.InstanceTags == Item.InstanceTags)
        {
            const int32 Added = FMath::Min(Def->MaxStack - E.Count, Remaining);
            E.Count += Added; Remaining -= Added;
        }
    }
    bool UsedIdentity = false;
    while (Remaining > 0 && After.Num() < MaxSlots)
    {
        FSAItemInstance E = Item;
        E.Count = FMath::Min(Def->MaxStack, Remaining);
        E.InstanceId = PreserveIdentity && !UsedIdentity && Remaining == Item.Count && E.Count == Item.Count
            ? Item.InstanceId : FGuid::NewGuid();
        UsedIdentity = true; Remaining -= E.Count; After.Add(E);
    }
    if ((AllOrNothing && Remaining > 0) || Remaining == Item.Count) return 0;
    Entries = MoveTemp(After); Changed(); return Item.Count - Remaining;
}
bool USAInventoryComponent::TryRemove(FGuid Id, int32 Count)
{
    if (!CanWrite() || Count <= 0 || AvailableCount(Id) < Count) return false;
    const int32 Index = Entries.IndexOfByPredicate([Id](const auto& E) { return E.InstanceId == Id; });
    if (Index == INDEX_NONE) return false;
    {
        TGuardValue<bool> Guard(bLocked, true);
        if (Entries[Index].Count == Count)
        {
            auto* E = GetOwner()->FindComponentByClass<USAEquipmentComponent>();
            if (E && E->GetEquippedItemId() == Id && !E->Unequip()) return false;
            Entries.RemoveAt(Index);
            for (auto& Q : Quickbar) if (Q == Id) Q.Invalidate();
        }
        else Entries[Index].Count -= Count;
    }
    Changed(); return true;
}
bool USAInventoryComponent::TryReserve(FSAActionHandle Action, FGuid Id, int32 Count, FGuid& Reservation)
{
    Reservation.Invalidate();
    if (!CanWrite() || !Action.IsValid() || Count <= 0 || AvailableCount(Id) < Count) return false;
    Reservation = FGuid::NewGuid(); Reservations.Add(Reservation, {Action, Id, Count}); return true;
}
bool USAInventoryComponent::CommitReservation(FSAActionHandle Action, FGuid Reservation)
{
    if (!CanWrite()) return false;
    const auto* Found = Reservations.Find(Reservation);
    if (!Found || Found->Action != Action) return false;
    const FReservation R = *Found;
    // Remove only this exact reserve. Failed removal restores it; duplicate commit finds no token.
    Reservations.Remove(Reservation);
    if (TryRemove(R.Item, R.Count)) return true;
    Reservations.Add(Reservation, R); return false;
}
void USAInventoryComponent::ReleaseReservation(FSAActionHandle Action, FGuid Reservation)
{
    if (const auto* R = Reservations.Find(Reservation); R && R->Action == Action) Reservations.Remove(Reservation);
}
void USAInventoryComponent::Changed()
{
    FSAInventoryChangeSet Change; Change.Revision = ++Revision; Change.Items = Entries; Change.Quickbar = Quickbar;
    if (const auto* E = GetOwner()->FindComponentByClass<USAEquipmentComponent>()) Change.EquippedItemId = E->GetEquippedItemId();
    PendingChanges.Add(MoveTemp(Change)); FlushChanges();
}
void USAInventoryComponent::FlushChanges()
{
    if (bLocked || bPublishing) return;
    TGuardValue<bool> Guard(bPublishing, true);
    int32 Budget = 64;
    while (!PendingChanges.IsEmpty() && !bEnding && Budget-- > 0)
    {
        const FSAInventoryChangeSet Change = PendingChanges[0]; PendingChanges.RemoveAt(0);
        OnItemsChanged.Broadcast(Change); if (!bEnding) OnItemsChangedBP.Broadcast(Change);
    }
    SetComponentTickEnabled(ConsumeAction.IsValid() || !PendingChanges.IsEmpty());
}
bool USAInventoryComponent::SetQuickbar(int32 Slot, FGuid Id)
{
    FSAItemInstance Item;
    if (!CanWrite() || Slot < 0 || Slot >= 10 || (Id.IsValid() && !TryGetItemCopy(Id, Item))) return false;
    if (Quickbar.Num() <= Slot) Quickbar.SetNum(Slot + 1);
    Quickbar[Slot] = Id; Changed(); return true;
}
FSAInventorySave USAInventoryComponent::MakeSnapshot() const
{
    if (!IsInGameThread() || bLocked || bEnding) { FSAInventorySave Invalid; Invalid.SchemaVersion = 0; return Invalid; }
    FSAInventorySave Save; Save.Items = Entries; Save.QuickbarItemIds = Quickbar;
    if (const auto* E = GetOwner()->FindComponentByClass<USAEquipmentComponent>()) Save.EquippedItemId = E->GetEquippedItemId();
    return Save;
}
bool USAInventoryComponent::RestoreSnapshot(const FSAInventorySave& Save)
{
    if (!CanWrite() || Save.SchemaVersion != 1 || Save.Items.Num() > MaxSlots || Save.QuickbarItemIds.Num() > 10) return false;
    auto* C = GetOwner()->FindComponentByClass<USACombatComponent>();
    if ((C && C->IsBusy()) || !Reservations.IsEmpty()) return false;
    TSet<FGuid> Ids;
    for (const auto& E : Save.Items)
    {
        if (!ValidateItem(E) || Ids.Contains(E.InstanceId)) return false;
        Ids.Add(E.InstanceId);
    }
    if (Save.EquippedItemId.IsValid() && !Ids.Contains(Save.EquippedItemId)) return false;
    if (Save.EquippedItemId.IsValid())
    {
        const auto* Equipped = Save.Items.FindByPredicate([&](const auto& Item) { return Item.InstanceId == Save.EquippedItemId; });
        const auto* Def = FindDefinition(Equipped->DefinitionId);
        if (Def->UseKind != ESAItemUseKind::MeleeWeapon && Def->UseKind != ESAItemUseKind::Spell) return false;
    }
    for (auto Q : Save.QuickbarItemIds) if (Q.IsValid() && !Ids.Contains(Q)) return false;
    auto* Equipment = GetOwner()->FindComponentByClass<USAEquipmentComponent>();
    if (Save.EquippedItemId.IsValid() && !Equipment) return false;
    {
        TGuardValue<bool> Guard(bLocked, true);
        if (Equipment && !Equipment->Unequip()) return false;
        Entries = Save.Items; Quickbar = Save.QuickbarItemIds;
    }
    if (Equipment && Save.EquippedItemId.IsValid()) Equipment->RequestEquip(Save.EquippedItemId);
    Changed(); return true;
}
bool USAInventoryComponent::RestoreSnapshotAsync(const FSAInventorySave& Save)
{
    if (!CanWrite() || Save.SchemaVersion != 1 || Save.Items.Num() > MaxSlots) return false;
    auto* C = GetOwner()->FindComponentByClass<USACombatComponent>();
    if ((C && C->IsBusy()) || !Reservations.IsEmpty()) return false;
    TArray<FSoftObjectPath> Paths;
    for (const auto& Item : Save.Items)
    {
        if (FindDefinition(Item.DefinitionId)) continue;
        const auto Path = UAssetManager::Get().GetPrimaryAssetPath(Item.DefinitionId);
        if (!Path.IsValid()) return false;
        Paths.AddUnique(Path);
    }
    if (Paths.IsEmpty()) { const bool Success = RestoreSnapshot(Save); OnRestoreFinished.Broadcast(Success); return Success; }
    RestoreRequest = FGuid::NewGuid();
    const FGuid Request = RestoreRequest;
    RestoreLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
        FStreamableDelegate::CreateWeakLambda(this, [this, Request, Save]()
        {
            if (Request != RestoreRequest || bEnding) return;
            // Release the member before delegates: a listener may start another restore.
            const auto CompletedLoad = MoveTemp(RestoreLoad);
            RestoreRequest.Invalidate();
            for (const auto& Item : Save.Items) if (auto* Def = FindDefinition(Item.DefinitionId)) Definitions.Add(Item.DefinitionId, Def);
            const bool Success = RestoreSnapshot(Save);
            if (!bEnding) OnRestoreFinished.Broadcast(Success);
        }));
    if (!RestoreLoad) { RestoreRequest.Invalidate(); return false; }
    return true;
}
ESACombatRequestResult USAInventoryComponent::RequestUse(FGuid Id, const FSASpellAim& Aim, FSAActionHandle& Action)
{
    Action = {};
    FSAItemInstance Item;
    if (!CanWrite() || !TryGetItemCopy(Id, Item) || AvailableCount(Id) < 1) return ESACombatRequestResult::InvalidData;
    auto* Def = FindDefinition(Item.DefinitionId);
    auto* C = GetOwner()->FindComponentByClass<USACombatComponent>();
    auto* V = GetOwner()->FindComponentByClass<USAVitalsComponent>();
    if (!Def || !C || !V) return ESACombatRequestResult::MissingDependencies;
    if (!V->IsAlive()) return ESACombatRequestResult::Dead;
    if (Def->UseKind == ESAItemUseKind::MeleeWeapon || Def->UseKind == ESAItemUseKind::Spell)
    {
        auto* E = GetOwner()->FindComponentByClass<USAEquipmentComponent>();
        if (!E || E->GetEquippedItemId() != Id) return ESACombatRequestResult::InvalidData;
        if (Def->UseKind == ESAItemUseKind::MeleeWeapon)
        {
            const auto Result = C->RequestMeleeInput();
            if (Result == ESACombatRequestResult::Started || Result == ESACombatRequestResult::Buffered) Action = C->GetCurrentAction();
            return Result;
        }
        auto* S = GetOwner()->FindComponentByClass<USASpellcastingComponent>();
        if (!S) return ESACombatRequestResult::MissingDependencies;
        return S->RequestCast(Aim, Action) == ESACastFailure::None ? ESACombatRequestResult::Started : ESACombatRequestResult::Failed;
    }
    if (Def->UseKind != ESAItemUseKind::Consumable || !Def->Validate() || !V->CanRestoreHealth()) return ESACombatRequestResult::InvalidData;
    if (C->IsBusy()) return ESACombatRequestResult::Busy;
    Action = C->ReserveAction(ESAActionKind::Consumable);
    FGuid Reservation;
    if (!Action.IsValid()) return ESACombatRequestResult::Busy;
    if (!TryReserve(Action, Id, 1, Reservation) || !C->BeginReservedAction(Action))
    {
        ReleaseReservation(Action, Reservation); C->FinishActionIfCurrent(Action, ESAActionEndReason::Failed);
        Action = {}; return ESACombatRequestResult::Failed;
    }
    ConsumeAction = Action; ConsumeReservation = Reservation; ConsumeHealing = Def->Healing;
    ConsumeAt = GetWorld()->GetTimeSeconds() + Def->ConsumeDelay;
    SetComponentTickEnabled(true); return ESACombatRequestResult::Started;
}
bool USAInventoryComponent::CommitConsumable(FSAActionHandle Action)
{
    auto* C = GetOwner()->FindComponentByClass<USACombatComponent>();
    auto* V = GetOwner()->FindComponentByClass<USAVitalsComponent>();
    if (!CanWrite() || !Action.IsValid() || ConsumeAction != Action || !C || !C->IsCurrentAction(Action) || !V) return false;
    if (!V->CanRestoreHealth()) { C->FinishActionIfCurrent(Action, ESAActionEndReason::Failed); return false; }
    // Suppress publication until both the expense and healing are committed.
    bool Success;
    {
        TGuardValue<bool> PublishGuard(bPublishing, true);
        const float Healing = ConsumeHealing;
        Success = CommitReservation(Action, ConsumeReservation);
        if (Success) { ConsumeAction = {}; ConsumeReservation.Invalidate(); V->RestoreHealthWithoutEvents(Healing); }
    }
    FlushChanges();
    C->FinishActionIfCurrent(Action, Success ? ESAActionEndReason::Completed : ESAActionEndReason::Failed);
    return Success;
}
void USAInventoryComponent::ActionFinished(FSAActionHandle Action, ESAActionEndReason Reason)
{
    for (auto It = Reservations.CreateIterator(); It; ++It) if (It.Value().Action == Action) It.RemoveCurrent();
    if (ConsumeAction == Action) { ConsumeAction = {}; ConsumeReservation.Invalidate(); }
}
void USAInventoryComponent::OwnerDied()
{
    Reservations.Reset(); ConsumeAction = {}; ConsumeReservation.Invalidate();
    RestoreRequest.Invalidate(); if (RestoreLoad) RestoreLoad->CancelHandle(); RestoreLoad.Reset();
}
void USAInventoryComponent::BeginPlay()
{
    Super::BeginPlay();
    if (auto* C = GetOwner()->FindComponentByClass<USACombatComponent>()) FinishSubscription = C->OnActionFinished.AddUObject(this, &USAInventoryComponent::ActionFinished);
    if (auto* V = GetOwner()->FindComponentByClass<USAVitalsComponent>()) DeathSubscription = V->OnDied.AddUObject(this, &USAInventoryComponent::OwnerDied);
}
void USAInventoryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEnding = true;
    if (auto* C = GetOwner()->FindComponentByClass<USACombatComponent>())
    { C->OnActionFinished.Remove(FinishSubscription); C->CancelActionIfCurrent(ConsumeAction, ESAActionEndReason::OwnerEnded); }
    if (auto* V = GetOwner()->FindComponentByClass<USAVitalsComponent>()) V->OnDied.Remove(DeathSubscription);
    OwnerDied(); Super::EndPlay(Reason);
}
void USAInventoryComponent::TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime, Type, Tick);
    if (ConsumeAction.IsValid() && GetWorld()->GetTimeSeconds() >= ConsumeAt) CommitConsumable(ConsumeAction);
    FlushChanges();
}

ASAPickup* USAInventoryComponent::TryDrop(FGuid Id, int32 Count)
{
    FSAItemInstance Item;
    if (!CanWrite() || Count <= 0 || AvailableCount(Id) < Count || !TryGetItemCopy(Id, Item)) return nullptr;
    auto* Def = FindDefinition(Item.DefinitionId);
    auto* V = GetOwner()->FindComponentByClass<USAVitalsComponent>();
    if (!Def || !Def->bCanBeThrown || !V || !V->IsAlive() || !DropClass) return nullptr;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(SADrop), false, GetOwner());
    TArray<AActor*> Attached; GetOwner()->GetAttachedActors(Attached); Params.AddIgnoredActors(Attached);
    const FVector Start = GetOwner()->GetActorLocation();
    const FVector End = Start + GetOwner()->GetActorForwardVector() * 100;
    FHitResult Hit;
    const FCollisionShape Shape = FCollisionShape::MakeSphere(20);
    if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, Shape, Params)
        || GetWorld()->OverlapBlockingTestByChannel(End, FQuat::Identity, ECC_Visibility, Shape, Params)) return nullptr;
    FSAActionHandle Transfer; Transfer.Id = FGuid::NewGuid(); FGuid Reservation;
    if (!TryReserve(Transfer, Id, Count, Reservation)) return nullptr;
    FSAItemInstance Dropped = Item; Dropped.Count = Count;
    if (Count < Item.Count) Dropped.InstanceId = FGuid::NewGuid();
    ASAPickup* Pickup = nullptr;
    {
        TGuardValue<bool> Guard(bLocked, true);
        Pickup = GetWorld()->SpawnActorDeferred<ASAPickup>(DropClass, FTransform(End),
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
        if (Pickup)
        {
            if (!Pickup->Prepare(Dropped, Def)) { Pickup->Destroy(); Pickup = nullptr; }
            else Pickup->FinishSpawning(FTransform(End));
        }
    }
    bool Committed = false;
    {
        TGuardValue<bool> DeferPublication(bPublishing, true);
        if (IsValid(Pickup) && V->IsAlive() && CanWrite()) Committed = CommitReservation(Transfer, Reservation);
        if (Committed) Pickup->ActivatePickup();
    }
    if (!Committed)
    { ReleaseReservation(Transfer, Reservation); if (IsValid(Pickup)) Pickup->Destroy(); Pickup = nullptr; }
    FlushChanges(); return Pickup;
}
