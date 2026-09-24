#include "Inventory/SAPickup.h"
#include "Inventory/SAInventoryComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Vitals/SAVitalsComponent.h"

ASAPickup::ASAPickup()
{
    InteractionShape = CreateDefaultSubobject<USphereComponent>(TEXT("Interaction"));
    SetRootComponent(InteractionShape); InteractionShape->SetSphereRadius(20);
    InteractionShape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractionShape->SetCollisionResponseToAllChannels(ECR_Ignore);
    InteractionShape->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
    Visual->SetupAttachment(InteractionShape); Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Visual->SetStaticMesh(Sphere.Object); Visual->SetRelativeScale3D(FVector(.3));
    SetActorEnableCollision(false); SetActorHiddenInGame(true);
}
void ASAPickup::BeginPlay()
{
    Super::BeginPlay();
    if (!bPrepared && IsValid(Definition) && Definition->Validate() && InitialCount > 0)
    {
        FSAItemInstance Value; Value.InstanceId = FGuid::NewGuid(); Value.DefinitionId = Definition->GetPrimaryAssetId();
        Value.Count = InitialCount; Value.Durability = Definition->MaxDurability;
        if (Prepare(Value, Definition)) ActivatePickup();
    }
}
bool ASAPickup::Prepare(const FSAItemInstance& Value, USAItemDefinition* Def)
{
    if (bActive || bClaimed || !Def || !Def->Validate() || Value.DefinitionId != Def->GetPrimaryAssetId()
        || !Value.InstanceId.IsValid() || Value.Count <= 0 || !FMath::IsFinite(Value.Durability)
        || Value.Durability < 0 || Value.Durability > Def->MaxDurability
        || ((Def->MaxDurability > 0 || Value.AffixSeed != 0) && Value.Count != 1)) return false;
    Item = Value; Definition = Def; bPrepared = true; return true;
}
void ASAPickup::ActivatePickup()
{
    if (bPrepared && Item.Count > 0)
    { bActive = true; SetActorHiddenInGame(false); SetActorEnableCollision(true); InteractionShape->SetCollisionEnabled(ECollisionEnabled::QueryOnly); }
}
bool ASAPickup::IsInteractable(USAInventoryComponent* Inventory) const
{
    if (!bActive || bClaimed || IsActorBeingDestroyed() || Item.Count <= 0 || !IsValid(Inventory) || !IsValid(Inventory->GetOwner())
        || !FMath::IsFinite(InteractionDistance) || InteractionDistance <= 0) return false;
    const auto* V = Inventory->GetOwner()->FindComponentByClass<USAVitalsComponent>();
    return V && V->IsAlive() && FVector::DistSquared(GetActorLocation(), Inventory->GetOwner()->GetActorLocation()) <= FMath::Square(InteractionDistance);
}
int32 ASAPickup::TryTransferTo(USAInventoryComponent* Inventory)
{
    if (!IsInGameThread() || !IsInteractable(Inventory) || !Inventory->CanWrite() || !Inventory->RegisterDefinition(Definition)) return 0;
    int32 Accepted = 0;
    {
        TGuardValue<bool> Claim(bClaimed, true);
        TGuardValue<bool> DeferPublication(Inventory->bPublishing, true);
        // AddTemplate executes without callbacks while publication is deferred.
        Accepted = Inventory->AddTemplate(Item, false, true);
        Item.Count -= Accepted;
        if (Item.Count == 0) { bActive = false; SetActorEnableCollision(false); }
    }
    Inventory->FlushChanges();
    if (Item.Count == 0) Destroy();
    return Accepted;
}
