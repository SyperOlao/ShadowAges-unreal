#include "Vitals/SAVitalsComponent.h"

USAVitalsComponent::USAVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool USAVitalsComponent::IsAlive() const
{
	return HasBegunPlay() && Health > 0.0f;
}

float USAVitalsComponent::GetHealth() const
{
	return Health;
}

float USAVitalsComponent::GetStamina() const
{
	return Stamina;
}

float USAVitalsComponent::GetAvailableStamina() const
{
	return static_cast<float>(CalculateAvailableStamina());
}

bool USAVitalsComponent::TryReserveStamina(FSAActionHandle Action, int32 Generation, float Amount,
                                           FGuid& OutReservation)
{
	OutReservation.Invalidate();
	if (!IsAlive() || !Action.IsValid() || Generation <= 0 || !FMath::IsFinite(Amount) || Amount < 0.0f)
	{
		return false;
	}
	for (const TPair<FGuid, FStaminaReservation>& Entry : Reservations)
	{
		if (Entry.Value.Action == Action && Entry.Value.Generation == Generation)
		{
			return false;
		}
	}
	if (static_cast<double>(Amount) > CalculateAvailableStamina())
	{
		return false;
	}
	FGuid Token;
	do
	{
		Token = FGuid::NewGuid();
	}
	while (!Token.IsValid() || Reservations.Contains(Token));
	FStaminaReservation Record;
	Record.Action = Action;
	Record.Generation = Generation;
	Record.Amount = Amount;
	Reservations.Add(Token, Record);
	OutReservation = Token;
	return true;
}

bool USAVitalsComponent::CommitStamina(FGuid Reservation)
{
	if (!Reservation.IsValid() || !IsAlive())
	{
		return false;
	}
	const FStaminaReservation* Record = Reservations.Find(Reservation);
	if (!Record)
	{
		return false;
	}

	const float Amount = Record->Amount;
	Reservations.Remove(Reservation);
	Stamina = FMath::Max(0.0f, Stamina - Amount);
	return true;
}

void USAVitalsComponent::ReleaseStamina(FGuid Reservation)
{
	if (Reservation.IsValid())
	{
		Reservations.Remove(Reservation);
	}
}

float USAVitalsComponent::ApplyHealthLoss(float Amount)
{
	if (!IsAlive() || !FMath::IsFinite(Amount) || Amount <= 0.0f)
	{
		return 0.0f;
	}

	const float PreviousHealth = Health;
	Health = FMath::Max(0.0f, Health - Amount);
	const float Applied = PreviousHealth - Health;
	if (Health <= 0.0f)
	{
		Reservations.Reset();
		OnDied.Broadcast();
		OnDiedBP.Broadcast();
	}
	return Applied;
}

void USAVitalsComponent::RestoreStamina(float Amount)
{
	if (!IsAlive() || !FMath::IsFinite(Amount) || Amount <= 0.0f)
	{
		return;
	}
	const double Restored = static_cast<double>(Stamina)
		+ static_cast<double>(Amount);
	Stamina = static_cast<float>(FMath::Min(static_cast<double>(MaxStamina), Restored));
}

void USAVitalsComponent::BeginPlay()
{
	if (!ensureMsgf(FMath::IsFinite(MaxHealth) && MaxHealth > 0.0f,
	                TEXT("MaxHealth must be finite and positive.")))
	{
		MaxHealth = 100.0f;
	}
	if (!ensureMsgf(FMath::IsFinite(MaxStamina) && MaxStamina >= 0.0f,
	                TEXT("MaxStamina must be finite and nonnegative.")))
	{
		MaxStamina = 100.0f;
	}

	Health = MaxHealth;
	Stamina = MaxStamina;
	Reservations.Reset();
	Super::BeginPlay();
}

void USAVitalsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Reservations.Reset();
	Super::EndPlay(EndPlayReason);
}

double USAVitalsComponent::CalculateAvailableStamina() const
{
	double Reserved = 0.0;
	for (const TPair<FGuid, FStaminaReservation>& Entry : Reservations)
	{
		Reserved += static_cast<double>(Entry.Value.Amount);
	}
	return FMath::Max(0.0, static_cast<double>(Stamina) - Reserved);
}
