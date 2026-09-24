// Source/ShadowAges/Public/Combat/SAMeleeDamageReceiverComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/Types/SACombatTypes.h"
#include "SAMeleeDamageReceiverComponent.generated.h"

class USAVitalsComponent;
struct FSASpellPayload;

UCLASS(Blueprintable, ClassGroup = (ShadowAges), meta = (BlueprintSpawnableComponent))
class SHADOWAGES_API USAMeleeDamageReceiverComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USAMeleeDamageReceiverComponent();

    bool CanReceiveFrom(const AActor* Source) const;
    FSADamageResult ResolveMeleeHit(const FSAMeleeHitRequest& Request);
    bool CanReceiveSpell(const FSASpellPayload& Payload) const;
    FSADamageResult ResolveSpellHit(const FSASpellPayload& Payload);

    // Shared extension for block/parry/immunity/resistance, including periodic spell damage.
    UFUNCTION(BlueprintNativeEvent, Category="SA|Damage")
    FSAIncomingDamageDecision EvaluateIncomingDamage(const FSAHitContext& Context, float ProposedDamage, bool bIsSpell) const;

    UFUNCTION(BlueprintPure, Category = "SA|Melee")
    FGuid GetSourceCreditId() const;

    UFUNCTION(BlueprintPure, Category = "SA|Melee")
    FName GetFactionId() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    USAVitalsComponent* FindUsableVitals() const;
    FSADamageResult CommitIncomingDamage(const FSAHitContext& Context, float ProposedDamage, bool bIsSpell);

    UPROPERTY(EditAnywhere, Category = "SA|Melee")
    FName FactionId = NAME_None;

    UPROPERTY(EditAnywhere, Category = "SA|Melee")
    bool bCanReceiveMelee = true;
    UPROPERTY(EditAnywhere, Category="SA|Magic")
    bool bCanReceiveSpells = true;

    UPROPERTY(Transient)
    TWeakObjectPtr<USAVitalsComponent> CachedVitals;

    UPROPERTY(VisibleInstanceOnly, Transient, Category = "SA|Melee")
    FGuid SourceCreditId;

    bool bConfigurationValid = false;
    bool bEndingPlay = false;
};
