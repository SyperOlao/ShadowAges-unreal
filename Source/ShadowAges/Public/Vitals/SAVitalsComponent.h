#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/Types/SAActionTypes.h"
#include "Core/Types/SACombatTypes.h"
#include "SAVitalsComponent.generated.h"


DECLARE_MULTICAST_DELEGATE(FSAOnVitalsDied);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSAOnVitalsDiedBP);
class USAVitalsComponent;
DECLARE_MULTICAST_DELEGATE_TwoParams(FSAOnManaChanged, float, float);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSAOnManaChangedBP, float, OldMana, float, NewMana);

struct SHADOWAGES_API FSAManaMutation
{
private:
    friend class USAVitalsComponent;
    TWeakObjectPtr<USAVitalsComponent> Issuer;
    FGuid Life;
    uint64 Revision = 0;
    float OldValue = 0.f;
    float NewValue = 0.f;
};

UCLASS(ClassGroup=(ShadowAges), meta=(BlueprintSpawnableComponent))
class SHADOWAGES_API USAVitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USAVitalsComponent();

	UFUNCTION(BlueprintPure, Category="SA|Vitals")
	bool IsAlive() const;

	UFUNCTION(BlueprintPure, Category="SA|Vitals")
	float GetHealth() const;
    bool CanRestoreHealth() const { return IsAlive() && Health < MaxHealth; }
    // Healing has no callbacks; caller publishes its transaction only after commit.
    float RestoreHealthWithoutEvents(float Amount);

	UFUNCTION(BlueprintPure, Category="SA|Vitals")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category="SA|Vitals")
	float GetAvailableStamina() const;

	bool TryReserveStamina(FSAActionHandle Action, int32 Generation,
	                       float Amount, FGuid& OutReservation);
	bool CommitStamina(FGuid Reservation);
	void ReleaseStamina(FGuid Reservation);

	// Amount is final health loss after the future Damage module's calculations.
	UFUNCTION(BlueprintCallable, Category="SA|Vitals")
	float ApplyHealthLoss(float Amount);

    FSADamageResult CommitDamageWithoutEvents(float Amount, const FSAHitContext& Context = FSAHitContext());
    void PublishCommittedDamage(const FSADamageResult& Result);

    UFUNCTION(BlueprintPure, Category="SA|Vitals")
    float GetMana() const { return Mana; }
    UFUNCTION(BlueprintPure, Category="SA|Vitals")
    float GetAvailableMana() const;
    UFUNCTION(BlueprintCallable, Category="SA|Vitals")
    void RestoreMana(float Amount);
    UFUNCTION(BlueprintPure, Category="SA|Vitals")
    FSAHitContext GetLastDamageContext() const { return LastDamageContext; }
    bool TryReserveMana(FSAActionHandle Action, float Amount, FGuid& OutReservation);
    bool CommitManaWithoutEvents(FGuid Reservation, FSAManaMutation& Mutation);
    void PublishManaChange(const FSAManaMutation& Mutation);
    void ReleaseMana(FGuid Reservation);
    FSAOnManaChanged OnManaChanged;
    UPROPERTY(BlueprintAssignable, Category="SA|Vitals")
    FSAOnManaChangedBP OnManaChangedBP;

	UFUNCTION(BlueprintCallable, Category="SA|Vitals")
	void RestoreStamina(float Amount);

	FSAOnVitalsDied OnDied;

	UPROPERTY(BlueprintAssignable, Category="SA|Vitals")
	FSAOnVitalsDiedBP OnDiedBP;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FStaminaReservation
	{
		FSAActionHandle Action;
		int32 Generation = 0;
		float Amount = 0.0f;
	};

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SA|Vitals",
		meta=(AllowPrivateAccess="true", ClampMin="1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SA|Vitals",
		meta=(AllowPrivateAccess="true", ClampMin="0.0"))
	float MaxStamina = 100.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SA|Vitals",
		meta=(AllowPrivateAccess="true"))
	float Health = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SA|Vitals",
		meta=(AllowPrivateAccess="true"))
	float Stamina = 0.0f;

	TMap<FGuid, FStaminaReservation> Reservations;
    bool bDeathPublished = false;
    UPROPERTY(EditDefaultsOnly, Category="SA|Vitals", meta=(ClampMin="0"))
    float MaxMana = 100.f;
    UPROPERTY(EditDefaultsOnly, Category="SA|Vitals", meta=(ClampMin="0"))
    float ManaRegenPerSecond = 0.f;
    UPROPERTY(VisibleInstanceOnly, Category="SA|Vitals")
    float Mana = 0.f;
    UPROPERTY(Transient)
    FSAHitContext LastDamageContext;
    TMap<FGuid, FStaminaReservation> ManaReservations;
    FGuid ManaLife;
    uint64 ManaRevision = 0;
    uint64 PublishedManaRevision = 0;
    TArray<FSAManaMutation> PendingManaChanges;
    bool bPublishingMana = false;
    FSAManaMutation SetManaWithoutEvents(float Value);

	double CalculateAvailableStamina() const;
};
