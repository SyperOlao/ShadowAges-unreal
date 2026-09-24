#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/Types/SAActionTypes.h"
#include "SAVitalsComponent.generated.h"


DECLARE_MULTICAST_DELEGATE(FSAOnVitalsDied);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSAOnVitalsDiedBP);

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

	UFUNCTION(BlueprintCallable, Category="SA|Vitals")
	void RestoreStamina(float Amount);

	FSAOnVitalsDied OnDied;

	UPROPERTY(BlueprintAssignable, Category="SA|Vitals")
	FSAOnVitalsDiedBP OnDiedBP;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	double CalculateAvailableStamina() const;
};
