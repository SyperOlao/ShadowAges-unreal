#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SAWeaponActor.generated.h"
class UStaticMeshComponent;
class USABladeTraceProfile;
// Passive until equipment commits. BP subclasses supply visuals only.
UCLASS(Blueprintable)
class SHADOWAGES_API ASAWeaponActor : public AActor
{
    GENERATED_BODY()
public:
    ASAWeaponActor();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Blade;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TObjectPtr<USABladeTraceProfile> TraceProfile;
    void ActivateEquipmentPresentation();
};
