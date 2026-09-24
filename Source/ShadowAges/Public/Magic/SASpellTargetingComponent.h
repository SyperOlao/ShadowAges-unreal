#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Magic/SAMagicTypes.h"
#include "SASpellTargetingComponent.generated.h"

class USceneComponent;
class USASpellDefinition;
struct SHADOWAGES_API FSAPreparedSpellAim
{
    FVector Muzzle = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    FVector AimPoint = FVector::ZeroVector;
    TArray<FHitResult> Hits;
};

UCLASS(ClassGroup=(ShadowAges), meta=(BlueprintSpawnableComponent))
class SHADOWAGES_API USASpellTargetingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USASpellTargetingComponent();
    UFUNCTION(BlueprintCallable, Category="SA|Magic")
    bool SetMuzzleComponent(USceneComponent* Component);
    UFUNCTION(BlueprintPure, Category="SA|Magic")
    static FSASpellAim AimFromController(AController* Controller);
    bool HasValidMuzzle(const USASpellDefinition& Spell) const;
    ESACastFailure PrepareAim(const USASpellDefinition& Spell, const FSASpellAim& Aim,
        const FSASpellPayload& Source, FSAPreparedSpellAim& Out) const;
private:
    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> MuzzleComponent;
};
