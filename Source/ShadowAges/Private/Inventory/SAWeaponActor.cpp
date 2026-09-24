#include "Inventory/SAWeaponActor.h"
#include "Components/StaticMeshComponent.h"
ASAWeaponActor::ASAWeaponActor()
{
    PrimaryActorTick.bCanEverTick = false;
    Blade = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Blade"));
    SetRootComponent(Blade);
    Blade->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetActorEnableCollision(false);
    SetActorHiddenInGame(true);
}
void ASAWeaponActor::ActivateEquipmentPresentation()
{ SetActorHiddenInGame(false); }
