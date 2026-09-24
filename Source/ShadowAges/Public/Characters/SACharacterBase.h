#pragma once
#include "CoreMinimal.h"
#include "APSliceableCharacter.h"
#include "SACharacterBase.generated.h"

class USAAnatomyComponent;
class USACorpseSliceAdapterComponent;
class USAVitalsComponent;
class USACombatComponent;
class USAMeleeDamageReceiverComponent;
class USAMeleeTraceComponent;
class USASpellcastingComponent;
class USASpellTargetingComponent;
class USAStatusEffectComponent;
class USAInventoryComponent;
class USAEquipmentComponent;

// Opt-in migration base. Existing Blueprints may instead add the components.
UCLASS()
class SHADOWAGES_API ASACharacterBase : public AAPSliceableCharacter
{
    GENERATED_BODY()
public:
    ASACharacterBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USAAnatomyComponent> Anatomy;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USACorpseSliceAdapterComponent> CorpseSliceAdapter;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USAVitalsComponent> Vitals;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USACombatComponent> Combat;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USAMeleeDamageReceiverComponent> MeleeReceiver;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USAMeleeTraceComponent> MeleeTrace;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USASpellcastingComponent> Spellcasting;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USASpellTargetingComponent> SpellTargeting;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USAStatusEffectComponent> StatusEffects;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USAInventoryComponent> Inventory;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SA")
    TObjectPtr<USAEquipmentComponent> Equipment;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void HandleDeath();
    FDelegateHandle DeathHandle;
};
