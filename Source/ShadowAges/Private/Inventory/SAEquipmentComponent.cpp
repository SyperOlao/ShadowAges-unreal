#include "Inventory/SAEquipmentComponent.h"
#include "Inventory/SAInventoryComponent.h"
#include "Inventory/SAWeaponActor.h"
#include "Combat/SACombatComponent.h"
#include "Combat/SAWeaponMoveset.h"
#include "Combat/Tracing/SAMeleeTraceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Magic/SASpellDefinition.h"
#include "Magic/SAProjectile.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Magic/SASpellcastingComponent.h"
#include "Vitals/SAVitalsComponent.h"

USAEquipmentComponent::USAEquipmentComponent()
{ PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.bStartWithTickEnabled = false; }
void USAEquipmentComponent::ConfigureMesh(USkeletalMeshComponent* Mesh)
{ if (!bCommitting && !EquippedId.IsValid() && IsValid(Mesh) && Mesh->GetOwner() == GetOwner()) CharacterMesh = Mesh; }
void USAEquipmentComponent::BeginPlay()
{
    Super::BeginPlay();
    if (auto* V = GetOwner()->FindComponentByClass<USAVitalsComponent>())
        DeathSubscription = V->OnDied.AddUObject(this, &USAEquipmentComponent::OwnerDied);
}
void USAEquipmentComponent::CancelPending()
{
    PendingRequest.Invalidate(); PendingItem.Invalidate(); bReady = false;
    bDependenciesRequested = false;
    if (DependencyLoad) DependencyLoad->CancelHandle();
    DependencyLoad.Reset();
    if (PendingLoad) PendingLoad->CancelHandle();
    PendingLoad.Reset(); PendingDefinition = nullptr; SetComponentTickEnabled(false);
}
void USAEquipmentComponent::OwnerDied() { CancelPending(); }
void USAEquipmentComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEnding = true; CancelPending();
    if (auto* V = GetOwner()->FindComponentByClass<USAVitalsComponent>()) V->OnDied.Remove(DeathSubscription);
    if (IsValid(Weapon)) Weapon->Destroy();
    Weapon = nullptr; CurrentLoad.Reset(); CurrentDependencyLoad.Reset();
    Super::EndPlay(Reason);
}
ESAEquipResult USAEquipmentComponent::RequestEquip(FGuid Item)
{
    auto* I = GetOwner()->FindComponentByClass<USAInventoryComponent>();
    auto* V = GetOwner()->FindComponentByClass<USAVitalsComponent>();
    if (!IsInGameThread() || bEnding || bCommitting || !I || !I->CanWrite() || !V || !V->IsAlive()) return ESAEquipResult::Rejected;
    if (EquippedId == Item && Item.IsValid()) { CancelPending(); return ESAEquipResult::AlreadyEquipped; }
    FSAItemInstance Entry;
    if (!I->TryGetItemCopy(Item, Entry) || I->AvailableCount(Item) <= 0) return ESAEquipResult::Rejected;
    auto* Def = I->FindDefinition(Entry.DefinitionId);
    if (!Def || !Def->Validate() || (Def->UseKind != ESAItemUseKind::MeleeWeapon && Def->UseKind != ESAItemUseKind::Spell)) return ESAEquipResult::Rejected;
    if (Def->UseKind == ESAItemUseKind::MeleeWeapon && (!CharacterMesh || !CharacterMesh->DoesSocketExist(Def->AttachmentSocket))) return ESAEquipResult::Rejected;
    CancelPending();
    PendingRequest = FGuid::NewGuid(); PendingItem = Item; PendingDefinition = Def;
    const FGuid Request = PendingRequest;
    TArray<FSoftObjectPath> Paths;
    if (!Def->EquipmentActorClass.IsNull()) Paths.Add(Def->EquipmentActorClass.ToSoftObjectPath());
    if (!Def->Moveset.IsNull()) Paths.Add(Def->Moveset.ToSoftObjectPath());
    if (!Def->BladeMesh.IsNull()) Paths.Add(Def->BladeMesh.ToSoftObjectPath());
    if (!Def->TraceProfile.IsNull()) Paths.Add(Def->TraceProfile.ToSoftObjectPath());
    if (!Def->Spell.IsNull()) Paths.Add(Def->Spell.ToSoftObjectPath());
    auto Load = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
        FStreamableDelegate::CreateWeakLambda(this, [this, Request]()
        {
            if (PendingRequest != Request) return;
            bReady = true; SetComponentTickEnabled(true);
        }));
    if (PendingRequest == Request) { PendingLoad = Load; if (!Load) CancelPending(); }
    return PendingRequest.IsValid() ? ESAEquipResult::Requested : ESAEquipResult::Rejected;
}
void USAEquipmentComponent::TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Tick)
{ Super::TickComponent(DeltaTime, Type, Tick); if (bReady) ContinueEquip(PendingRequest); }
void USAEquipmentComponent::ContinueEquip(FGuid Request)
{
    if (!Request.IsValid() || Request != PendingRequest || bCommitting || bEnding) return;
    auto* I = GetOwner()->FindComponentByClass<USAInventoryComponent>();
    auto* C = GetOwner()->FindComponentByClass<USACombatComponent>();
    auto* V = GetOwner()->FindComponentByClass<USAVitalsComponent>();
    auto* S = GetOwner()->FindComponentByClass<USASpellcastingComponent>();
    auto* T = GetOwner()->FindComponentByClass<USAMeleeTraceComponent>();
    FSAItemInstance Entry;
    if (!I || !C || !V || !V->IsAlive() || !I->TryGetItemCopy(PendingItem, Entry)
        || I->AvailableCount(PendingItem) <= 0 || !PendingDefinition || !PendingDefinition->Validate()
        || Entry.DefinitionId != PendingDefinition->GetPrimaryAssetId()) { CancelPending(); return; }
    // Conservative policy: wait until Combat is idle, including the complete recovery.
    if (C->IsBusy() || !I->CanWrite()) return;
    USAItemDefinition* Def = PendingDefinition;
    const bool Melee = Def->UseKind == ESAItemUseKind::MeleeWeapon;
    if (!Melee && Def->Spell.Get() && !bDependenciesRequested)
    {
        bDependenciesRequested = true;
        TArray<FSoftObjectPath> Dependencies;
        auto* Spell = Def->Spell.Get();
        if (!Spell->ProjectileClass.IsNull()) Dependencies.Add(Spell->ProjectileClass.ToSoftObjectPath());
        if (!Spell->CastMontage.IsNull()) Dependencies.Add(Spell->CastMontage.ToSoftObjectPath());
        if (!Dependencies.IsEmpty())
        {
            bReady = false;
            DependencyLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Dependencies,
                FStreamableDelegate::CreateWeakLambda(this, [this, Request]() { if (PendingRequest == Request) bReady = true; }));
            if (!DependencyLoad) CancelPending();
            return;
        }
    }
    if ((Melee && (!T || !CharacterMesh || !CharacterMesh->DoesSocketExist(Def->AttachmentSocket)
        || !Def->EquipmentActorClass.Get() || !Def->Moveset.Get())) || (!Melee && (!S || !Def->Spell.Get())))
    { CancelPending(); return; }
    FString Error;
    if (Melee)
    {
        if (!Def->Moveset->Validate(Error) || !CharacterMesh->GetSkeletalMeshAsset()) { CancelPending(); return; }
        for (const auto& Step : Def->Moveset->Steps)
            if (Step.Montage->GetSkeleton() != CharacterMesh->GetSkeletalMeshAsset()->GetSkeleton()) { CancelPending(); return; }
    }
    else if (!Def->Spell->Validate(Error) || (!Def->Spell->ProjectileClass.IsNull() && !Def->Spell->ProjectileClass.Get())
        || (!Def->Spell->CastMontage.IsNull() && !Def->Spell->CastMontage.Get())) { CancelPending(); return; }
    FSAActionHandle Action = C->ReserveAction(ESAActionKind::Equip);
    if (!Action.IsValid()) return;
    bool Success = false;
    {
        TGuardValue<bool> EquipGuard(bCommitting, true);
        TGuardValue<bool> InventoryGuard(I->bLocked, true);
        ASAWeaponActor* Prepared = nullptr;
        if (Melee)
        {
            Prepared = GetWorld()->SpawnActorDeferred<ASAWeaponActor>(Def->EquipmentActorClass.Get(), FTransform::Identity,
                GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (Prepared)
            {
                if (!Def->BladeMesh.IsNull()) Prepared->Blade->SetStaticMesh(Def->BladeMesh.Get());
                if (!Def->TraceProfile.IsNull()) Prepared->TraceProfile = Def->TraceProfile.Get();
                Prepared->FinishSpawning(FTransform::Identity);
            }
        }
        // Spawn can run Blueprint code. Revalidate the owner and token before changing any active binding.
        if (Request == PendingRequest && IsValid(GetOwner()) && V->IsAlive() && C->IsReservedAction(Action, ESAActionKind::Equip)
            && (!Melee || (IsValid(Prepared) && Prepared->AttachToComponent(CharacterMesh,
                FAttachmentTransformRules::SnapToTargetNotIncludingScale, Def->AttachmentSocket))))
        {
            if (!Melee || T->ConfigureBlade(Prepared->Blade, Prepared->TraceProfile, Def->AttachmentSocket, {}))
            {
                C->ConfigureCombat(CharacterMesh, Melee ? Def->Moveset.Get() : nullptr);
                if (!Melee && T) T->ClearBlade();
                ASAWeaponActor* Old = Weapon;
                Weapon = Prepared; EquippedId = PendingItem; CurrentDefinition = Def;
                CurrentLoad = PendingLoad; PendingLoad.Reset();
                CurrentDependencyLoad = DependencyLoad; DependencyLoad.Reset(); CancelPending();
                if (S) S->SetPreparedSpell(Melee ? nullptr : Def->Spell.Get());
                if (Weapon) Weapon->ActivateEquipmentPresentation();
                if (IsValid(Old)) Old->Destroy();
                Success = true;
            }
        }
        if (!Success && IsValid(Prepared)) Prepared->Destroy();
    }
    if (!Success) CancelPending();
    // Hold the action through the notification so callbacks cannot start another use mid-commit.
    if (Success) OnEquipmentChanged.Broadcast(EquippedId);
    C->FinishActionIfCurrent(Action, Success ? ESAActionEndReason::Completed : ESAActionEndReason::Failed);
}
bool USAEquipmentComponent::Unequip()
{
    auto* C = GetOwner()->FindComponentByClass<USACombatComponent>();
    if (bEnding || bCommitting || !C || C->IsBusy()) return false;
    CancelPending();
    if (!EquippedId.IsValid()) return true;
    // Callers removing an entry perform this while their inventory write guard is held.
    TGuardValue<bool> Guard(bCommitting, true);
    if (auto* T = GetOwner()->FindComponentByClass<USAMeleeTraceComponent>()) T->ClearBlade();
    C->ConfigureCombat(CharacterMesh, nullptr);
    if (auto* S = GetOwner()->FindComponentByClass<USASpellcastingComponent>()) S->SetPreparedSpell(nullptr);
    ASAWeaponActor* Old = Weapon;
    Weapon = nullptr; EquippedId.Invalidate(); CurrentDefinition = nullptr; CurrentLoad.Reset(); CurrentDependencyLoad.Reset();
    if (IsValid(Old)) Old->Destroy();
    return true;
}
