#include "Inventory/SAInventoryComponent.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Inventory/SAEquipmentComponent.h"
#include "Inventory/SAPickup.h"
#include "Inventory/SAWeaponActor.h"
#include "Combat/SACombatComponent.h"
#include "Combat/SAWeaponMoveset.h"
#include "Combat/SAMeleeDamageReceiverComponent.h"
#include "Combat/Tracing/SAMeleeTraceComponent.h"
#include "Magic/SASpellcastingComponent.h"
#include "Magic/SASpellDefinition.h"
#include "Vitals/SAVitalsComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/AssetManager.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace SAInventoryTests
{
struct FScene
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AActor* Actor;
    USAInventoryComponent* Inventory;
    USAEquipmentComponent* Equipment;
    USACombatComponent* Combat;
    USAVitalsComponent* Vitals;
    template<class T> T* Add() { auto* C = NewObject<T>(Actor); Actor->AddInstanceComponent(C); C->RegisterComponent(); return C; }
    FScene()
    {
        Actor = World->SpawnActor<AActor>(); Actor->SetRootComponent(Add<USceneComponent>());
        Vitals = Add<USAVitalsComponent>(); Combat = Add<USACombatComponent>();
        Inventory = Add<USAInventoryComponent>(); Equipment = Add<USAEquipmentComponent>(); Add<USASpellcastingComponent>();
        Actor->DispatchBeginPlay();
    }
    ~FScene() { World->DestroyWorld(false); }
    USAItemDefinition* Definition(FName Key = TEXT("Test.Arrow"), int32 Stack = 10)
    {
        auto* D = NewObject<USAItemDefinition>(Actor); D->DefinitionKey = Key; D->MaxStack = Stack;
        Inventory->RegisterDefinition(D); return D;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAInventoryStacks, "ShadowAges.Inventory.StacksAndIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAInventoryStacks::RunTest(const FString&)
{
    SAInventoryTests::FScene S; auto* I = S.Inventory; auto* D = S.Definition(); I->MaxSlots = 2;
    TestEqual(TEXT("Initial add"), I->TryAdd(D, 8), 8); const FGuid Id = I->GetItems()[0].InstanceId;
    TestEqual(TEXT("Atomic overflow rejected"), I->TryAdd(D, 13), 0);
    TestEqual(TEXT("Overflow preserved count"), I->GetItems()[0].Count, 8);
    TestEqual(TEXT("Partial fill exact"), I->TryAdd(D, 13, false), 12);
    TestEqual(TEXT("Merge preserves receiving GUID"), I->GetItems()[0].InstanceId, Id);
    TestFalse(TEXT("Over-removal rejected"), I->TryRemove(Id, 11));
    I->SetQuickbar(0, I->GetItems()[1].InstanceId); const auto Other = I->GetItems()[1].InstanceId;
    TestTrue(TEXT("Remove first"), I->TryRemove(Id, 10));
    TestEqual(TEXT("Quickbar does not use array index"), I->MakeSnapshot().QuickbarItemIds[0], Other);
    FSAItemInstance Unique; auto* Sword = S.Definition(TEXT("Test.Sword"), 1); Sword->MaxDurability = 100;
    Unique.DefinitionId = Sword->GetPrimaryAssetId(); Unique.Count = 1; Unique.Durability = 80; Unique.InstanceId = FGuid::NewGuid();
    TestEqual(TEXT("Transfer unique"), I->AddTemplate(Unique, true, true), 1);
    TestEqual(TEXT("Unique GUID preserved"), I->GetItems()[1].InstanceId, Unique.InstanceId);
    TestEqual(TEXT("Duplicate GUID rejected"), I->AddTemplate(Unique, true, true), 0);
    I->MaxSlots = 3; Unique.InstanceId = FGuid::NewGuid(); Unique.Durability = 20;
    TestEqual(TEXT("Different durability separate entry"), I->AddTemplate(Unique, true, true), 1);
    TestEqual(TEXT("Durability not merged"), I->GetItems().Num(), 3);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAInventoryConsumables, "ShadowAges.Inventory.ConsumableTransactions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAInventoryConsumables::RunTest(const FString&)
{
    SAInventoryTests::FScene S; auto* I = S.Inventory; auto* D = S.Definition(TEXT("Test.Potion"));
    D->UseKind = ESAItemUseKind::Consumable; D->Healing = 25;
    I->TryAdd(D, 1); auto Id = I->GetItems()[0].InstanceId;
    FSAActionHandle A, B;
    TestEqual(TEXT("Full health rejects"), I->RequestUse(Id, {}, A), ESACombatRequestResult::InvalidData);
    S.Vitals->ApplyHealthLoss(50);
    TestEqual(TEXT("Consume starts"), I->RequestUse(Id, {}, A), ESACombatRequestResult::Started);
    TestEqual(TEXT("Reserve hides last potion"), I->AvailableCount(Id), 0);
    TestTrue(TEXT("Second use rejected"), I->RequestUse(Id, {}, B) != ESACombatRequestResult::Started);
    TestFalse(TEXT("Cannot remove reserve"), I->TryRemove(Id, 1));
    S.Combat->CancelActionIfCurrent(A, ESAActionEndReason::Interrupted);
    TestEqual(TEXT("Cancel refunds uncommitted reserve"), I->AvailableCount(Id), 1);
    TestFalse(TEXT("Stale marker rejected"), I->CommitConsumable(A));
    I->RequestUse(Id, {}, B);
    int32 Events = 0;
    I->OnItemsChanged.AddLambda([&](const FSAInventoryChangeSet& Change)
    { ++Events; TestEqual(TEXT("Healing precedes expense notification"), S.Vitals->GetHealth(), 75.f); TestEqual(TEXT("No item at commit"), Change.Items.Num(), 0); });
    TestTrue(TEXT("Commit once"), I->CommitConsumable(B));
    TestFalse(TEXT("Duplicate marker rejected"), I->CommitConsumable(B));
    TestEqual(TEXT("One change set"), Events, 1);
    TestFalse(TEXT("Late cancellation does not refund"), S.Combat->CancelActionIfCurrent(B, ESAActionEndReason::Interrupted));
    TestEqual(TEXT("Spent item stays spent"), I->GetItems().Num(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAInventoryEvents, "ShadowAges.Inventory.ReservationsAndReentrantEvents", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAInventoryEvents::RunTest(const FString&)
{
    SAInventoryTests::FScene S; auto* I = S.Inventory; auto* D = S.Definition(); I->TryAdd(D, 5);
    auto Id = I->GetItems()[0].InstanceId; FSAActionHandle A; A.Id = FGuid::NewGuid(); FGuid R;
    TestTrue(TEXT("Reserve exact quantity"), I->TryReserve(A, Id, 3, R));
    TestTrue(TEXT("Unreserved portion removable"), I->TryRemove(Id, 2));
    TestFalse(TEXT("Reserved portion protected"), I->TryRemove(Id, 1));
    FSAActionHandle Wrong; Wrong.Id = FGuid::NewGuid();
    TestFalse(TEXT("Wrong action cannot commit"), I->CommitReservation(Wrong, R));
    TestTrue(TEXT("Correct action commits"), I->CommitReservation(A, R));
    TestFalse(TEXT("Commit idempotent"), I->CommitReservation(A, R));
    int32 Depth = 0, MaxDepth = 0, Events = 0; int64 LastRevision = 0;
    I->OnItemsChanged.AddLambda([&](const FSAInventoryChangeSet& Change)
    {
        ++Depth; MaxDepth = FMath::Max(MaxDepth, Depth); ++Events;
        TestTrue(TEXT("Ordered revisions"), Change.Revision > LastRevision); LastRevision = Change.Revision;
        if (Events == 1) I->TryAdd(D, 1);
        --Depth;
    });
    I->TryAdd(D, 1);
    TestEqual(TEXT("Nested mutation publishes after first event"), MaxDepth, 1);
    TestEqual(TEXT("Both commits published"), Events, 2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAInventoryTransfers, "ShadowAges.Inventory.PickupDropConservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAInventoryTransfers::RunTest(const FString&)
{
    SAInventoryTests::FScene S; auto* I = S.Inventory; auto* D = S.Definition(); I->MaxSlots = 1; I->TryAdd(D, 7);
    auto* P = S.World->SpawnActor<ASAPickup>(); FSAItemInstance Item;
    Item.InstanceId = FGuid::NewGuid(); Item.DefinitionId = D->GetPrimaryAssetId(); Item.Count = 12;
    P->Prepare(Item, D); P->ActivatePickup();
    TestEqual(TEXT("Partial pickup accepts three"), P->TryTransferTo(I), 3);
    TestEqual(TEXT("Nine remain"), P->GetItem().Count, 9);
    TestEqual(TEXT("Repeated request cannot duplicate"), P->TryTransferTo(I), 0);
    P->SetActorLocation(FVector(0,200,0));
    auto Id = I->GetItems()[0].InstanceId;
    auto* Drop = I->TryDrop(Id, 4);
    TestNotNull(TEXT("Drop prepared"), Drop);
    if (Drop)
    {
        TestEqual(TEXT("Drop exact quantity"), Drop->GetItem().Count, 4);
        TestTrue(TEXT("Split new GUID"), Drop->GetItem().InstanceId != Id);
        TestEqual(TEXT("Remainder keeps GUID"), I->GetItems()[0].InstanceId, Id);
        TestEqual(TEXT("Re-pick conserves count"), Drop->TryTransferTo(I), 4);
    }
    D->bCanBeThrown = false;
    TestNull(TEXT("Throw prohibition enforced"), I->TryDrop(Id, 1));
    D->bCanBeThrown = true;
    AActor* Wall = S.World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box); Wall->AddInstanceComponent(Box); Box->SetBoxExtent(FVector(30));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
    Wall->SetActorLocation(FVector(80,0,0));
    TestNull(TEXT("Blocked placement rejects drop"), I->TryDrop(Id, 1));
    TestEqual(TEXT("Failed drop preserves inventory"), I->GetItems()[0].Count, 10);
    Wall->Destroy(); I->TryRemove(Id, 10);
    auto* Unique = S.Definition(TEXT("Test.Unique"), 1); Unique->MaxDurability = 100;
    Item.InstanceId = FGuid::NewGuid(); Item.DefinitionId = Unique->GetPrimaryAssetId(); Item.Count = 1; Item.Durability = 37; Item.AffixSeed = 123;
    I->AddTemplate(Item, true, true); Drop = I->TryDrop(Item.InstanceId, 1);
    TestNotNull(TEXT("Unique dropped"), Drop);
    if (Drop)
    {
        TestEqual(TEXT("Unique drop GUID"), Drop->GetItem().InstanceId, Item.InstanceId);
        Drop->TryTransferTo(I);
        FSAItemInstance Restored; TestTrue(TEXT("Original identity recovered"), I->TryGetItemCopy(Item.InstanceId, Restored));
        TestEqual(TEXT("Durability retained"), Restored.Durability, 37.f); TestEqual(TEXT("Affix retained"), Restored.AffixSeed, 123);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAInventorySaveTest, "ShadowAges.Inventory.SaveRoundTripAndValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAInventorySaveTest::RunTest(const FString&)
{
    SAInventoryTests::FScene S; auto* I = S.Inventory; auto* D = S.Definition(); I->TryAdd(D, 6);
    auto Id = I->GetItems()[0].InstanceId; I->SetQuickbar(2, Id);
    auto* Save = NewObject<USAInventorySaveGame>(); Save->Inventory = I->MakeSnapshot();
    TArray<uint8> Bytes; TestTrue(TEXT("Serialize snapshot"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
    auto* Read = Cast<USAInventorySaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Read snapshot"), Read)) return false;
    I->TryRemove(Id, 6); TestTrue(TEXT("Restore complete snapshot"), I->RestoreSnapshot(Read->Inventory));
    TestEqual(TEXT("Count survives disk format"), I->GetItems()[0].Count, 6);
    TestEqual(TEXT("Quickbar GUID survives"), I->MakeSnapshot().QuickbarItemIds[2], Id);
    auto Bad = Read->Inventory; const FSAItemInstance Duplicate = Bad.Items[0]; Bad.Items.Add(Duplicate);
    TestFalse(TEXT("Duplicate saved GUID rejected"), I->RestoreSnapshot(Bad));
    Bad = Read->Inventory; Bad.Items[0].DefinitionId = FPrimaryAssetId(TEXT("SAItem"), TEXT("Missing"));
    TestFalse(TEXT("Unknown definition refuses whole save"), I->RestoreSnapshot(Bad));
    TestEqual(TEXT("Invalid restore does not alter items"), I->GetItems()[0].Count, 6);
    Bad = Read->Inventory; Bad.SchemaVersion = 2; TestFalse(TEXT("Future schema refused"), I->RestoreSnapshot(Bad));
    return true;
}

class FSAEquipmentLifecycleCommand : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TUniquePtr<SAInventoryTests::FScene> S;
    FGuid First, Last;
    int32 Stage = 0, Frames = 0;
public:
    explicit FSAEquipmentLifecycleCommand(FAutomationTestBase* InTest) : Test(InTest), S(MakeUnique<SAInventoryTests::FScene>())
    {
        for (int32 Index = 0; Index < 3; ++Index)
        {
            auto* Spell = NewObject<USASpellDefinition>(S->Actor);
            Spell->Delivery = ESASpellDelivery::Instant; Spell->SpellTag = FGameplayTag::RequestGameplayTag(TEXT("Spell.RestoreMana"));
            Spell->CooldownGroup = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Spell.RestoreMana"));
            Spell->TargetPolicy = ESASpellTargetPolicy::Self; Spell->Damage = 0;
            auto* Def = S->Definition(FName(*FString::Printf(TEXT("Test.Spell%d"), Index)), 1);
            Def->UseKind = ESAItemUseKind::Spell; Def->Spell = Spell;
            S->Inventory->TryAdd(Def, 1);
        }
        First = S->Inventory->GetItems()[0].InstanceId; Last = S->Inventory->GetItems()[2].InstanceId;
        for (const auto& Item : S->Inventory->GetItems()) S->Equipment->RequestEquip(Item.InstanceId);
    }
    virtual bool Update() override
    {
        if (++Frames > 200) { Test->AddError(TEXT("Equipment async request timed out")); return true; }
        static_cast<UActorComponent*>(S->Equipment)->TickComponent(.016f, LEVELTICK_All, nullptr);
        if (Stage == 0)
        {
            if (S->Equipment->IsPreparing()) return false;
            Test->TestEqual(TEXT("Latest asynchronous request wins"), S->Equipment->GetEquippedItemId(), Last);
            Test->TestEqual(TEXT("Repeated GUID is already equipped"), S->Equipment->RequestEquip(Last), ESAEquipResult::AlreadyEquipped);
            auto Action = S->Combat->ReserveAction(ESAActionKind::Consumable); S->Combat->BeginReservedAction(Action);
            S->Equipment->RequestEquip(First); Stage = 1; return false;
        }
        if (Stage == 1)
        {
            if (Frames < 8) return false;
            Test->TestEqual(TEXT("Busy combat retains previous equipment"), S->Equipment->GetEquippedItemId(), Last);
            S->Vitals->ApplyHealthLoss(1000); Stage = 2; return false;
        }
        Test->TestFalse(TEXT("Death cancels pending load"), S->Equipment->IsPreparing());
        Test->TestEqual(TEXT("Dead callback cannot replace equipment"), S->Equipment->GetEquippedItemId(), Last);
        return true;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAEquipmentLifecycle, "ShadowAges.Inventory.EquipmentAsyncLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAEquipmentLifecycle::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FSAEquipmentLifecycleCommand(this)); return true; }

class FSAWeaponIdentityCommand : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TUniquePtr<SAInventoryTests::FScene> S;
    FGuid Sword, Broken;
    TWeakObjectPtr<ASAWeaponActor> Original;
    int32 Stage = 0, Frames = 0;
public:
    explicit FSAWeaponIdentityCommand(FAutomationTestBase* T) : Test(T), S(MakeUnique<SAInventoryTests::FScene>())
    {
        auto* Mesh = S->Add<USkeletalMeshComponent>(); Mesh->AttachToComponent(S->Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        Mesh->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
        Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
        S->Add<USAMeleeDamageReceiverComponent>(); S->Add<USAMeleeTraceComponent>();
        S->Equipment->ConfigureMesh(Mesh);
        auto* Sequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01"));
        auto* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, TEXT("DefaultSlot"));
        if (!Montage) { Test->AddError(TEXT("Test attack animation missing")); return; }
        Montage->bEnableAutoBlendOut = false;
        auto* Moveset = NewObject<USAWeaponMoveset>(S->Actor);
        FSAMeleeStep Step; Step.StepId = TEXT("TestSwing"); Step.Montage = Montage; Step.StaminaCost = 0;
        FSAMeleeHitWindow Window; Window.BeginSeconds = .1f; Window.EndSeconds = .2f; Step.HitWindows.Add(Window);
        Moveset->Steps.Add(Step);
        auto* Blade = DuplicateObject<UStaticMesh>(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")), S->Actor);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto* Socket = NewObject<UStaticMeshSocket>(Blade); Socket->SocketName = Index ? TEXT("BladeTip") : TEXT("BladeBase");
            Socket->RelativeLocation = FVector(Index * 100, 0, 0); Blade->AddSocket(Socket);
        }
        auto* Profile = NewObject<USABladeTraceProfile>(S->Actor);
        auto* D = S->Definition(TEXT("Test.NativeSword"), 1); D->UseKind = ESAItemUseKind::MeleeWeapon;
        D->EquipmentActorClass = ASAWeaponActor::StaticClass(); D->Moveset = Moveset; D->BladeMesh = Blade; D->TraceProfile = Profile;
        S->Inventory->TryAdd(D, 1); Sword = S->Inventory->GetItems()[0].InstanceId;
        auto* Bad = S->Definition(TEXT("Test.BrokenSword"), 1); Bad->UseKind = ESAItemUseKind::MeleeWeapon;
        Bad->EquipmentActorClass = ASAWeaponActor::StaticClass(); Bad->Moveset = Moveset;
        S->Inventory->TryAdd(Bad, 1); Broken = S->Inventory->GetItems()[1].InstanceId;
        S->Equipment->RequestEquip(Sword);
    }
    virtual bool Update() override
    {
        if (++Frames > 200) { Test->AddError(TEXT("Weapon test load timeout")); return true; }
        static_cast<UActorComponent*>(S->Equipment)->TickComponent(.016f, LEVELTICK_All, nullptr);
        if (S->Equipment->IsPreparing()) return false;
        if (Stage == 0)
        {
            Original = S->Equipment->GetWeaponActor();
            if (!Test->TestNotNull(TEXT("Native weapon equipped"), Original.Get())) return true;
            for (int32 Index = 0; Index < 20; ++Index)
            {
                FSAActionHandle Action;
                Test->TestEqual(TEXT("Use starts attack"), S->Inventory->RequestUse(Sword, {}, Action), ESACombatRequestResult::Started);
                Test->TestEqual(TEXT("Use retains actor identity"), S->Equipment->GetWeaponActor(), Original.Get());
                S->Combat->FinishActionIfCurrent(Action, ESAActionEndReason::Completed);
            }
            Test->TestEqual(TEXT("Re-equip doesn't spawn"), S->Equipment->RequestEquip(Sword), ESAEquipResult::AlreadyEquipped);
            S->Equipment->RequestEquip(Broken); Stage = 1; return false;
        }
        Test->TestEqual(TEXT("Failed preparation preserves old actor"), S->Equipment->GetWeaponActor(), Original.Get());
        Test->TestEqual(TEXT("Failed preparation preserves old GUID"), S->Equipment->GetEquippedItemId(), Sword);
        return true;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAWeaponIdentity, "ShadowAges.Inventory.WeaponStableAcrossTwentyUses", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAWeaponIdentity::RunTest(const FString&)
{ ADD_LATENT_AUTOMATION_COMMAND(FSAWeaponIdentityCommand(this)); return true; }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSAItemExamples, "ShadowAges.Inventory.ExampleDefinitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSAItemExamples::RunTest(const FString&)
{
    TSet<FPrimaryAssetId> Ids;
    for (const TCHAR* Name : {TEXT("DA_Item_HealingPotion"), TEXT("DA_Item_Arrow"), TEXT("DA_Item_FireSpell")})
    {
        auto* D = LoadObject<USAItemDefinition>(nullptr, *FString::Printf(TEXT("/Game/Combat/Items/%s.%s"), Name, Name));
        if (!TestNotNull(TEXT("Example exists"), D)) continue;
        TestTrue(TEXT("Definition validates"), D->Validate());
        TestFalse(TEXT("Unique designer key"), Ids.Contains(D->GetPrimaryAssetId())); Ids.Add(D->GetPrimaryAssetId());
        TestTrue(TEXT("AssetManager resolves saved ID"), UAssetManager::Get().GetPrimaryAssetPath(D->GetPrimaryAssetId()).IsValid());
    }
    return true;
}
#endif
