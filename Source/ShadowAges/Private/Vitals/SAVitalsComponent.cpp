#include "Vitals/SAVitalsComponent.h"
#include "GameFramework/Actor.h"

float USAVitalsComponent::RestoreHealthWithoutEvents(float Amount)
{
    if (!IsAlive() || !FMath::IsFinite(Amount) || Amount <= 0) return 0;
    const float Old = Health;
    Health = FMath::Min(MaxHealth, Health + Amount);
    return Health - Old;
}

USAVitalsComponent::USAVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
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
    const FSADamageResult Result = CommitDamageWithoutEvents(Amount);
    PublishCommittedDamage(Result);
    return Result.AppliedHealthDamage;
}

FSADamageResult USAVitalsComponent::CommitDamageWithoutEvents(float Amount, const FSAHitContext& Context)
{
    FSADamageResult Result;
    if (!IsAlive() || !FMath::IsFinite(Amount) || Amount < 0.f) { return Result; }
    Result.bAccepted = true;
    LastDamageContext = Context;
    const float PreviousHealth = Health;
    Health = FMath::Max(0.f, Health - Amount);
    Result.AppliedHealthDamage = PreviousHealth - Health;
    Result.bFatal = Health <= 0.f;
    if (Result.bFatal) { Reservations.Reset(); ManaReservations.Reset(); SetComponentTickEnabled(false); }
    return Result;
}

void USAVitalsComponent::PublishCommittedDamage(const FSADamageResult& Result)
{
    if (!Result.bAccepted || !Result.bFatal || Health > 0.f || bDeathPublished
        || !HasBegunPlay() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed()) { return; }
    bDeathPublished = true;
    OnDied.Broadcast();
    if (IsValid(this) && HasBegunPlay() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed())
    {
        OnDiedBP.Broadcast();
    }
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
    bDeathPublished = false;
    MaxMana = FMath::IsFinite(MaxMana) ? FMath::Clamp(MaxMana, 0.f, 1.e6f) : 100.f;
    ManaRegenPerSecond = FMath::IsFinite(ManaRegenPerSecond) ? FMath::Max(0.f, ManaRegenPerSecond) : 0.f;
    Mana = MaxMana;
    ManaLife = FGuid::NewGuid();
    ManaRevision = PublishedManaRevision = 0;
    PendingManaChanges.Reset();
    ManaReservations.Reset();
	Stamina = MaxStamina;
	Reservations.Reset();
	Super::BeginPlay();
    SetComponentTickEnabled(ManaRegenPerSecond > 0.f);
}

void USAVitalsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Reservations.Reset();
	Super::EndPlay(EndPlayReason);
    ManaReservations.Reset();
    ManaLife.Invalidate();
    PendingManaChanges.Reset();
}

float USAVitalsComponent::GetAvailableMana() const
{
    double Reserved = 0.;
    for (const auto& Pair : ManaReservations) { Reserved += Pair.Value.Amount; }
    return static_cast<float>(FMath::Max(0., double(Mana) - Reserved));
}

bool USAVitalsComponent::TryReserveMana(FSAActionHandle Action, float Amount, FGuid& OutReservation)
{
    OutReservation.Invalidate();
    if (!IsAlive() || !Action.IsValid() || !FMath::IsFinite(Amount) || Amount < 0.f || Amount > GetAvailableMana()) { return false; }
    for (const auto& Pair : ManaReservations) { if (Pair.Value.Action == Action) { return false; } }
    FStaminaReservation Record;
    Record.Action = Action;
    Record.Amount = Amount;
    OutReservation = FGuid::NewGuid();
    ManaReservations.Add(OutReservation, Record);
    return true;
}

FSAManaMutation USAVitalsComponent::SetManaWithoutEvents(float Value)
{
    FSAManaMutation Mutation;
    if (!FMath::IsFinite(Value)) { return Mutation; }
    const float Clamped = FMath::Clamp(Value, 0.f, MaxMana);
    if (Mana == Clamped) { return Mutation; }
    Mutation.Issuer = this;
    Mutation.Life = ManaLife;
    Mutation.Revision = ++ManaRevision;
    Mutation.OldValue = Mana;
    Mutation.NewValue = Clamped;
    Mana = Clamped;
    PendingManaChanges.Add(Mutation);
    return Mutation;
}

bool USAVitalsComponent::CommitManaWithoutEvents(FGuid Reservation, FSAManaMutation& Mutation)
{
    Mutation = {};
    const FStaminaReservation* Record = ManaReservations.Find(Reservation);
    if (!Record || !IsAlive() || Mana < Record->Amount) { return false; }
    const float Amount = Record->Amount;
    ManaReservations.Remove(Reservation);
    Mutation = SetManaWithoutEvents(Mana - Amount);
    return true;
}

void USAVitalsComponent::PublishManaChange(const FSAManaMutation& Mutation)
{
    if (Mutation.Issuer.Get() != this || Mutation.Life != ManaLife || Mutation.Revision <= PublishedManaRevision
        || !HasBegunPlay() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed()) { return; }
    if (bPublishingMana) { return; }
    TGuardValue<bool> Guard(bPublishingMana, true);
    // A released instant effect may restore mana before the cost notification.
    // Flush committed revisions in order rather than dropping the earlier cost.
    while (!PendingManaChanges.IsEmpty() && HasBegunPlay() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed())
    {
        const FSAManaMutation Event = PendingManaChanges[0];
        PendingManaChanges.RemoveAt(0);
        PublishedManaRevision = Event.Revision;
        OnManaChanged.Broadcast(Event.OldValue, Event.NewValue);
        if (IsValid(this) && HasBegunPlay() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed())
        { OnManaChangedBP.Broadcast(Event.OldValue, Event.NewValue); }
    }
}

void USAVitalsComponent::ReleaseMana(FGuid Reservation) { ManaReservations.Remove(Reservation); }

void USAVitalsComponent::RestoreMana(float Amount)
{
    if (!IsAlive() || !FMath::IsFinite(Amount) || Amount <= 0.f) { return; }
    const auto Mutation = SetManaWithoutEvents(static_cast<float>(FMath::Min(double(MaxMana), double(Mana) + Amount)));
    PublishManaChange(Mutation);
}

void USAVitalsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    RestoreMana(DeltaTime * ManaRegenPerSecond);
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
