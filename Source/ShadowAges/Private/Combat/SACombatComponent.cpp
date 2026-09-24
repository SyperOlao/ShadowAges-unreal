#include "Combat/SACombatComponent.h"
#include "AlphaBlend.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Templates/UnrealTemplate.h"
#include "Vitals/SAVitalsComponent.h"

USACombatComponent::USACombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

ESACombatRequestResult USACombatComponent::RequestMeleeInput()
{
	if (State == ESAActionState::Executing)
	{
		return RequestComboInput(ActiveAction)
			       ? ESACombatRequestResult::Buffered
			       : ESACombatRequestResult::WindowClosed;
	}
	if (IsBusy() || bFinishing || bMutatingStep || bEndingPlay)
	{
		return ESACombatRequestResult::Busy;
	}
	if (!EnsureDependencies()) return ESACombatRequestResult::MissingDependencies;
	if (!Vitals->IsAlive()) return ESACombatRequestResult::Dead;
	const FSAActionHandle Handle = ReserveAction(ESAActionKind::Melee);
	if (!Handle.IsValid()) return ESACombatRequestResult::Busy;
	FSAMeleeRequest Request;
	Request.Moveset = Moveset;
	return TryStartMelee(Handle, Request);
}

bool USACombatComponent::ConfigureCombat(USkeletalMeshComponent* Mesh, USAWeaponMoveset* NewMoveset)
{
	if (IsBusy() || bFinishing || bMutatingStep || bEndingPlay
		|| !IsValid(Mesh) || Mesh->GetOwner() != GetOwner())
	{
		return false;
	}
	if (CombatMesh != Mesh)
	{
		if (IsValid(CombatMesh))
		{
			RemoveTickPrerequisiteComponent(CombatMesh);
		}
		CombatMesh = Mesh;
		AddTickPrerequisiteComponent(CombatMesh);
	}
	Moveset = NewMoveset;
	return true;
}

bool USACombatComponent::RequestCancel(ESACombatCancelIntent Intent)
{
	if (bFinishing || bMutatingStep || bEndingPlay) return false;
	if (!IsBusy()) return EnsureDependencies() && Vitals->IsAlive();
	if (State != ESAActionState::Executing || !ActiveKey.IsValid()) return false;
	const FSAAttackCancelPolicy Policy = ActiveStep.CancelPolicy;
	FSAMeleeTimeWindow Window;
	bool bAllowed = false;
	switch (Intent)
	{
	case ESACombatCancelIntent::Dodge: bAllowed = Policy.bAllowDodge; Window = Policy.DodgeWindow; break;
	case ESACombatCancelIntent::Block: bAllowed = Policy.bAllowBlock; Window = Policy.BlockWindow; break;
	case ESACombatCancelIntent::Equip: bAllowed = Policy.bAllowEquip; Window = Policy.EquipWindow; break;
	default: return false;
	}
	UAnimInstance* Anim = OwnedAnim.Get();
	FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(ActiveKey.MontageInstanceID) : nullptr;
	if (!bAllowed || !Instance) return false;
	const float Position = Instance->GetPosition();
	if (Position < Window.BeginSeconds || Position >= Window.EndSeconds) return false;
	const FSAActionHandle Handle = ActiveAction;
	CancelActionIfCurrent(Handle, ESAActionEndReason::Cancelled);
	// A finish listener may already have started another action.
	return !IsBusy() && IsValid(Vitals) && Vitals->IsAlive() && !bEndingPlay;
}

bool USACombatComponent::IsBusy() const
{
	return State != ESAActionState::Idle;
}

FSAActionHandle USACombatComponent::GetCurrentAction() const
{
	return ActiveAction;
}

FSAMeleePlaybackKey USACombatComponent::GetCurrentPlayback() const
{
	return ActiveKey;
}

USAWeaponMoveset* USACombatComponent::GetMoveset() const
{
	return Moveset;
}

FVector USACombatComponent::FilterMoveInput(FVector Input) const
{
	return ActiveKey.IsValid() ? Input * ActiveStep.Movement.MoveInputScale : Input;
}

bool USACombatComponent::CanApplyLookInput() const
{
	
	return !ActiveKey.IsValid() || ActiveStep.Movement.bAllowLookInput;
}

FString USACombatComponent::GetDebugString() const
{
	return FString::Printf(TEXT("State=%d Action=%s Step=%d Gen=%d Instance=%d Buffer=%d Pos=%.3f Error=%s"),
	static_cast<int32>(State), *ActiveAction.Id.ToString(), ActiveKey.StepIndex,
	ActiveKey.Generation, ActiveKey.MontageInstanceID, static_cast<int32>(Combo.GetState()),
	PreviousPosition, *LastError);
}


FSAActionHandle USACombatComponent::ReserveAction(ESAActionKind Kind)
{
	if (IsBusy() || bFinishing || bMutatingStep || bEndingPlay)
	{
		LastError = TEXT("Combat is busy.");
		return {};
	}
	if (Kind != ESAActionKind::Melee)
	{
		LastError = TEXT("This chapter implements only Melee.");
		return {};
	}
	if (!EnsureDependencies() || !Vitals->IsAlive())
	{
		return {};
	}
	ActiveAction.Id = FGuid::NewGuid();
	LastError.Reset();
	State = ESAActionState::Reserved;
	ActionKind = Kind;
	ReservationDeadline = GetWorld()->GetTimeSeconds() + ReservationTimeoutSeconds;
	SetComponentTickEnabled(true);
	return ActiveAction;
}

bool USACombatComponent::BeginReservedAction(FSAActionHandle Handle)
{
	if (!IsCurrentAction(Handle) || State != ESAActionState::Reserved
		|| ActionKind != ESAActionKind::Melee || !ActiveMoveset
		|| !IsValid(Vitals) || !Vitals->IsAlive())
	{
		return false;
	}
	State = ESAActionState::Executing;
	return true;
}

ESACombatRequestResult USACombatComponent::TryStartMelee(FSAActionHandle Handle, const FSAMeleeRequest& Request)
{
	if (!IsCurrentAction(Handle) || State != ESAActionState::Reserved)
	{
		return ESACombatRequestResult::InvalidHandle;
	}
	if (!EnsureDependencies())
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
		return ESACombatRequestResult::MissingDependencies;
	}
	if (!Vitals->IsAlive())
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::OwnerDied);
		return ESACombatRequestResult::Dead;
	}
	if (!ValidateForMesh(Request.Moveset, LastError))
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
		return ESACombatRequestResult::InvalidData;
	}
	const int32 Entry = Request.EntryStepOverride == INDEX_NONE
		                    ? Request.Moveset->EntryStepIndex
		                    : Request.EntryStepOverride;
	if (!Request.Moveset->Steps.IsValidIndex(Entry))
	{
		LastError = TEXT("EntryStepOverride is outside Steps.");
		FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
		return ESACombatRequestResult::InvalidData;
	}
	ActiveMoveset = Request.Moveset;
	double Budget = TimeoutGraceSeconds;
	for (int32 Index = Entry; Index != INDEX_NONE; Index = ActiveMoveset->Steps[Index].NextStepIndex)
	{
		const FSAMeleeStep& Step = ActiveMoveset->Steps[Index];
		Budget += Step.Montage->GetPlayLength() / (Step.PlayRate * Step.Montage->RateScale)
			+ TimeoutGraceSeconds;
	}
	ActionDeadline = GetWorld()->GetTimeSeconds() + Budget;
	if (!BeginReservedAction(Handle))
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
		return ESACombatRequestResult::Failed;
	}
	const ESACombatRequestResult Result = StartStep(Handle, Entry);
	if (Result != ESACombatRequestResult::Started)
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
	}
	return Result;
}

bool USACombatComponent::FinishActionIfCurrent(FSAActionHandle Handle, ESAActionEndReason Reason)
{
	if (!IsCurrentAction(Handle) || bFinishing) return false;
	// Death wins even if another OnDied listener aborts before our listener.
	if (IsValid(Vitals) && Vitals->HasBegunPlay() && Vitals->GetHealth() <= 0.0f)
	{
		Reason = ESAActionEndReason::OwnerDied;
	}
	const FSAMeleePlaybackKey FinishedKey = ActiveKey;
	const TWeakObjectPtr<UAnimInstance> FinishedAnim = OwnedAnim;
	const FGuid Reservation = PendingStaminaReservation;
	{
		TGuardValue<bool> Guard(bFinishing, true);
		ActiveAction = {};
		ActiveKey = {};
		State = ESAActionState::Idle;
		ActiveMoveset = nullptr;
		ActiveStep = {};
		OwnedAnim.Reset();
		Combo.Reset();
		PendingStaminaReservation.Invalidate();
		SetComponentTickEnabled(false);
		if (IsValid(Vitals)) Vitals->ReleaseStamina(Reservation);
		if (FinishedKey.IsValid()) OnMeleeStepClosed.Broadcast(FinishedKey);
		StopPlayback(FinishedAnim, FinishedKey.MontageInstanceID, StopBlendOutSeconds);
		FTerminalEvent Event;
		Event.Action = Handle;
		Event.Reason = Reason;
		PendingTerminalEvents.Add(Event);
	}
	FlushTerminalEvents();
	return true;
}

bool USACombatComponent::CancelActionIfCurrent(FSAActionHandle Handle, ESAActionEndReason Reason)
{
	if (Reason == ESAActionEndReason::Completed) return false;
	return FinishActionIfCurrent(Handle, Reason);
}

bool USACombatComponent::IsCurrentAction(FSAActionHandle Handle) const
{
	return Handle.IsValid() && State != ESAActionState::Idle && Handle == ActiveAction;
}

bool USACombatComponent::IsCurrentPlayback(FSAMeleePlaybackKey Key) const
{
	return Key.IsValid() && State == ESAActionState::Executing
	&& IsCurrentAction(Key.Action) && ActiveKey == Key;
}

double USACombatComponent::GetActionDeadline(FSAActionHandle Handle) const
{
	
	if (!IsCurrentAction(Handle)) return 0.0;
	return State == ESAActionState::Reserved ? ReservationDeadline : ActionDeadline;
}

bool USACombatComponent::RequestComboInput(FSAActionHandle Handle)
{
	if (!IsCurrentAction(Handle) || State != ESAActionState::Executing
		|| !ActiveKey.IsValid() || bBranchClosed || bFinishing
		|| ActiveStep.NextStepIndex == INDEX_NONE || !IsValid(Vitals) || !Vitals->IsAlive())
	{
		return false;
	}
	UAnimInstance* Anim = OwnedAnim.Get();
	FAnimMontageInstance* Instance = Anim ? Anim->GetMontageInstanceForID(ActiveKey.MontageInstanceID) : nullptr;
	if (!Instance || Instance->IsStopped()) return false;
	const float Position = Instance->GetPosition();
	const bool bTimelineDecision = AcceptDispatchKey.IsValid() && AcceptDispatchKey == ActiveKey;
	if (!bTimelineDecision && Position >= ActiveStep.ComboBranch.EndSeconds) return false;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Combo.Submit(Handle, ActiveKey.Generation, Now, ActiveStep.InputBufferSeconds)) return false;
	if (bTimelineDecision || (Position >= ActiveStep.ComboAccept.BeginSeconds
		&& Position < ActiveStep.ComboAccept.EndSeconds))
	{
		Combo.TryReserve(Handle, ActiveKey.Generation, Now);
	}
	return true;
}

void USACombatComponent::BeginPlay()
{
	Super::BeginPlay();
	ReservationTimeoutSeconds = FMath::IsFinite(ReservationTimeoutSeconds)
		                            ? FMath::Max(0.01f, ReservationTimeoutSeconds)
		                            : 1.0f;
	TimeoutGraceSeconds = FMath::IsFinite(TimeoutGraceSeconds)
		                      ? FMath::Max(0.1f, TimeoutGraceSeconds)
		                      : 2.0f;
	StopBlendOutSeconds = FMath::IsFinite(StopBlendOutSeconds)
		                      ? FMath::Max(0.0f, StopBlendOutSeconds)
		                      : 0.1f;
	if (!CombatMesh)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			CombatMesh = Character->GetMesh();
		}
	}
	if (CombatMesh)
	{
		AddTickPrerequisiteComponent(CombatMesh);
	}
}

void USACombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	FinishActionIfCurrent(ActiveAction, ESAActionEndReason::OwnerEnded);
	FlushTerminalEvents();
	if (IsValid(Vitals))
	{
		Vitals->OnDied.Remove(DeathSubscription);
	}
	DeathSubscription.Reset();
	if (IsValid(CombatMesh))
	{
		RemoveTickPrerequisiteComponent(CombatMesh);
	}
	SetComponentTickEnabled(false);
	Super::EndPlay(EndPlayReason);
}

void USACombatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                       FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	FlushTerminalEvents();
	if (State == ESAActionState::Idle)
	{
		if (PendingTerminalEvents.IsEmpty()) SetComponentTickEnabled(false);
		return;
	}
	const FSAActionHandle Handle = ActiveAction;
	if (!IsValid(Vitals) || !Vitals->IsAlive())
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::OwnerDied);
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (State == ESAActionState::Reserved)
	{
		if (Now >= ReservationDeadline) FinishActionIfCurrent(Handle, ESAActionEndReason::TimeOut);
		return;
	}
	UAnimInstance* Anim = OwnedAnim.Get();
	if (!IsValid(CombatMesh) || !Anim || CombatMesh->GetAnimInstance() != Anim)
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::Interrupted);
		return;
	}
	FAnimMontageInstance* Instance = Anim->GetMontageInstanceForID(ActiveKey.MontageInstanceID);
	if (!Instance || Instance->IsStopped())
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::Interrupted);
		return;
	}
	const float Length = ActiveStep.Montage->GetPlayLength();
	float Position = Instance->GetPosition();
	if (!FMath::IsFinite(Position) || Position + KINDA_SMALL_NUMBER < PreviousPosition)
	{
		LastError = TEXT("Montage moved backwards; loops and jumps are unsupported.");
		FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
		return;
	}
	if (Position >= Length - KINDA_SMALL_NUMBER) Position = Length;
	const FSAMeleePlaybackKey Key = ActiveKey;
	AdvanceTimeline(FMath::Clamp(Position, PreviousPosition, Length), Now);
	if (!IsCurrentPlayback(Key)) return;
	if (Now > StepDeadline || Now > ActionDeadline)
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::TimeOut);
	}
}

bool USACombatComponent::EnsureDependencies()
{
	AActor* Owner = GetOwner();
	if (!Owner || bEndingPlay || !GetWorld() || !HasBegunPlay())
	{
		LastError = TEXT("Combat has not begun play or owner is ending play.");
		return false;
	}
	if (!IsValid(CombatMesh))
	{
		if (ACharacter* Character = Cast<ACharacter>(Owner))
		{
			CombatMesh = Character->GetMesh();
			if (CombatMesh)
			{
				AddTickPrerequisiteComponent(CombatMesh);
			}
		}
	}
	TArray<USAVitalsComponent*> Found;
	Owner->GetComponents<USAVitalsComponent>(Found);
	if (Found.Num() != 1 || !IsValid(Found[0]) || !Found[0]->HasBegunPlay()
		|| !IsValid(CombatMesh) || CombatMesh->GetOwner() != Owner
		|| !IsValid(CombatMesh->GetAnimInstance()))
	{
		LastError = TEXT("Need one initialized Vitals and an owned skeletal mesh with AnimInstance.");
		return false;
	}
	if (Vitals != Found[0])
	{
		if (IsValid(Vitals))
		{
			Vitals->OnDied.Remove(DeathSubscription);
		}
		Vitals = Found[0];
		DeathSubscription = Vitals->OnDied.AddUObject(this, &USACombatComponent::HandleOwnerDied);
	}
	return true;
}

bool USACombatComponent::ValidateForMesh(const USAWeaponMoveset* Candidate, FString& Error) const
{
	if (!IsValid(Candidate) || !Candidate->Validate(Error))
	{
		if (!Candidate) Error = TEXT("Moveset is null.");
		return false;
	}
	USkeletalMesh* SkeletalMesh = CombatMesh ? CombatMesh->GetSkeletalMeshAsset() : nullptr;
	if (!SkeletalMesh || !SkeletalMesh->GetSkeleton())
	{
		Error = TEXT("Combat mesh has no skeleton.");
		return false;
	}
	for (const FSAMeleeStep& Step : Candidate->Steps)
	{
		if (Step.Montage->GetSkeleton() != SkeletalMesh->GetSkeleton())
		{
			Error = FString::Printf(TEXT("Step %s uses another skeleton."), *Step.StepId.ToString());
			return false;
		}
	}
	return true;
}

ESACombatRequestResult USACombatComponent::StartStep(FSAActionHandle Handle, int32 NewStepIndex)
{
	if (bMutatingStep) return ESACombatRequestResult::Busy;
	ESACombatRequestResult Result;
	{
		TGuardValue Guard(bMutatingStep, true);
		Result = StartStepInternal(Handle, NewStepIndex);
	}
	FlushTerminalEvents();
	return Result;
}

ESACombatRequestResult USACombatComponent::StartStepInternal(FSAActionHandle Handle, int32 NewStepIndex)
{
	if (!IsCurrentAction(Handle) || State != ESAActionState::Executing
		|| !ActiveMoveset || !ActiveMoveset->Steps.IsValidIndex(NewStepIndex)
		|| !IsValid(CombatMesh) || !IsValid(Vitals) || !Vitals->IsAlive())
	{
		return ESACombatRequestResult::Failed;
	}
	if (GenerationCounter == MAX_int32)
	{
		LastError = TEXT("Playback generation exhausted; recreate the actor.");
		return ESACombatRequestResult::Failed;
	}
	const FSAMeleeStep NewStep = ActiveMoveset->Steps[NewStepIndex];
	const int32 NewGeneration = ++GenerationCounter;
	FGuid Reservation;
	if (!Vitals->TryReserveStamina(Handle, NewGeneration, NewStep.StaminaCost, Reservation))
	{
		return ESACombatRequestResult::InsufficientStamina;
	}
	PendingStaminaReservation = Reservation;
	const FSAMeleePlaybackKey OldKey = ActiveKey;
	const TWeakObjectPtr<UAnimInstance> OldAnim = OwnedAnim;
	ActiveKey = {};
	Combo.Reset();
	if (OldKey.IsValid())
	{
		OnMeleeStepClosed.Broadcast(OldKey);
		StopPlayback(OldAnim, OldKey.MontageInstanceID, StopBlendOutSeconds);
	}
	if (!IsCurrentAction(Handle)) return ESACombatRequestResult::Failed;

	UAnimInstance* Anim = CombatMesh->GetAnimInstance();
	if (!IsValid(Anim))
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
		return ESACombatRequestResult::MissingDependencies;
	}
	OwnedAnim = Anim;
	ActiveStep = NewStep;
	ActiveKey.Action = Handle;
	ActiveKey.StepIndex = NewStepIndex;
	ActiveKey.Generation = NewGeneration;
	ActiveKey.MontageInstanceID = INDEX_NONE;
	bAcceptNotified = false;
	bBranchClosed = false;
	bFirstPoseSample = true;
	PreviousPosition = 0.0f;
	PreviousWorldTime = GetWorld()->GetTimeSeconds();

	const float PlayedLength = Anim->Montage_Play(NewStep.Montage, NewStep.PlayRate,
	                                              EMontagePlayReturnType::MontageLength, 0.0f, false);
	FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(NewStep.Montage);
	const int32 NewInstanceID = Instance ? Instance->GetInstanceID() : INDEX_NONE;
	if (!IsCurrentAction(Handle) || ActiveKey.Generation != NewGeneration)
	{
		StopPlayback(Anim, NewInstanceID, StopBlendOutSeconds);
		return ESACombatRequestResult::Failed;
	}
	if (PlayedLength <= 0.0f || !Instance || Instance->GetNextSectionID(0) != INDEX_NONE)
	{
		ActiveKey.MontageInstanceID = NewInstanceID;
		FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
		return ESACombatRequestResult::Failed;
	}
	ActiveKey.MontageInstanceID = NewInstanceID;
	const FSAMeleePlaybackKey StartedKey = ActiveKey;

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &USACombatComponent::HandleMontageEnded, StartedKey);
	Anim->Montage_SetEndDelegate(EndDelegate, NewStep.Montage);
	FOnMontageBlendingOutStarted BlendDelegate;
	BlendDelegate.BindUObject(this, &USACombatComponent::HandleMontageBlendingOut, StartedKey);
	Anim->Montage_SetBlendingOutDelegate(BlendDelegate, NewStep.Montage);

	if (!Vitals->CommitStamina(Reservation))
	{
		FinishActionIfCurrent(Handle, ESAActionEndReason::Failed);
		return ESACombatRequestResult::Failed;
	}
	PendingStaminaReservation.Invalidate();
	StepDeadline = PreviousWorldTime + NewStep.Montage->GetPlayLength()
		/ (NewStep.PlayRate * NewStep.Montage->RateScale) + TimeoutGraceSeconds;
	OnMeleeStepStarted.Broadcast(StartedKey);
	if (!IsCurrentPlayback(StartedKey)) return ESACombatRequestResult::Failed;
	OnMeleeStepStartedBP.Broadcast(StartedKey);
	return IsCurrentPlayback(StartedKey)
		       ? ESACombatRequestResult::Started
		       : ESACombatRequestResult::Failed;
}

void USACombatComponent::AdvanceTimeline(float Position, double Now)
{
	const FSAMeleePlaybackKey Key = ActiveKey;
	const FSAMeleeStep Step = ActiveStep;
	const float Previous = PreviousPosition;
	const double PreviousTime = PreviousWorldTime;
	FSAMeleePoseFrame Frame;
	Frame.Key = Key;
	Frame.Step = Step;
	Frame.Mesh = CombatMesh.Get();
	Frame.PreviousPosition = Previous;
	Frame.CurrentPosition = Position;
	Frame.bFirstSample = bFirstPoseSample;
	for (int32 Index = 0; Index < Step.HitWindows.Num(); ++Index)
	{
		const FSAMeleeHitWindow& Window = Step.HitWindows[Index];
		const float Begin = FMath::Max(Previous, Window.BeginSeconds);
		const float End = FMath::Min(Position, Window.EndSeconds);
		if (End > Begin)
		{
			FSAMeleeWindowSlice Slice;
			Slice.WindowSerial = Index;
			Slice.BeginPosition = Begin;
			Slice.EndPosition = End;
			Frame.HitSlices.Add(Slice);
		}
	}
	PreviousPosition = Position;
	PreviousWorldTime = Now;
	bFirstPoseSample = false;
	OnMeleePoseAdvanced.Broadcast(Frame);
	if (!IsCurrentPlayback(Key)) return;

	if (Step.NextStepIndex != INDEX_NONE && !bBranchClosed)
	{
		if (Previous <= Step.ComboAccept.BeginSeconds && Position >= Step.ComboAccept.BeginSeconds)
		{
			const double Alpha = Position > Previous
				                     ? (Step.ComboAccept.BeginSeconds - Previous) / (Position - Previous)
				                     : 1.0;
			const double BoundaryTime = PreviousTime + FMath::Clamp(Alpha, 0.0, 1.0) * (Now - PreviousTime);
			Combo.TryReserve(Key.Action, Key.Generation, BoundaryTime);
		}
		const bool bAcceptOpenNow = Position >= Step.ComboAccept.BeginSeconds
			&& Position < Step.ComboAccept.EndSeconds
			&& Position < Step.ComboBranch.EndSeconds;
		if (bAcceptOpenNow) Combo.TryReserve(Key.Action, Key.Generation, Now);
		const bool bReachedAccept = !bAcceptNotified
			&& Previous < FMath::Min(Step.ComboAccept.EndSeconds, Step.ComboBranch.EndSeconds)
			&& Position >= Step.ComboAccept.BeginSeconds;
		if (bReachedAccept)
		{
			bAcceptNotified = true;
			TGuardValue AcceptGuard(AcceptDispatchKey, Key);
			OnComboAcceptOpened.Broadcast(Key);
			if (!IsCurrentPlayback(Key)) return;
			OnComboAcceptOpenedBP.Broadcast(Key);
			if (!IsCurrentPlayback(Key)) return;
		}
		const bool bPassedBranch = Position >= Step.ComboBranch.BeginSeconds
			&& Previous < Step.ComboBranch.EndSeconds;
		if (bPassedBranch && Combo.Consume(Key.Action, Key.Generation))
		{
			const ESACombatRequestResult Result = StartStep(Key.Action, Step.NextStepIndex);
			if (Result == ESACombatRequestResult::Started || !IsCurrentPlayback(Key)) return;
			if (Result != ESACombatRequestResult::InsufficientStamina)
			{
				FinishActionIfCurrent(Key.Action, ESAActionEndReason::Failed);
				return;
			}
			bBranchClosed = true;
		}
		if (Position >= Step.ComboBranch.EndSeconds)
		{
			bBranchClosed = true;
			Combo.Reset();
		}
	}
	Combo.Expire(Now);
	if (Position >= Step.Montage->GetPlayLength())
	{
		FinishActionIfCurrent(Key.Action, ESAActionEndReason::Completed);
	}
}

void USACombatComponent::StopPlayback(TWeakObjectPtr<UAnimInstance> Anim, int32 InstanceID, float BlendSeconds)
{
	UAnimInstance* InstanceOwner = Anim.Get();
	FAnimMontageInstance* Instance = InstanceOwner && InstanceID != INDEX_NONE
		                                 ? InstanceOwner->GetMontageInstanceForID(InstanceID)
		                                 : nullptr;
	if (!Instance) return;
	Instance->OnMontageEnded.Unbind();
	Instance->OnMontageBlendingOutStarted.Unbind();
	Instance->Stop(FAlphaBlend(BlendSeconds), true);
}

void USACombatComponent::FlushTerminalEvents()
{
	if (bMutatingStep || bFinishing || bDispatchingTerminalEvents || PendingTerminalEvents.IsEmpty()) return;
	TGuardValue<bool> Guard(bDispatchingTerminalEvents, true);
	do
	{
		TArray<FTerminalEvent> Events = MoveTemp(PendingTerminalEvents);
		PendingTerminalEvents.Reset();
		for (const FTerminalEvent& Event : Events)
		{
			OnActionFinished.Broadcast(Event.Action, Event.Reason);
			OnActionFinishedBP.Broadcast(Event.Action, Event.Reason);
		}
	}
	while (bEndingPlay && !PendingTerminalEvents.IsEmpty());
	if (!PendingTerminalEvents.IsEmpty() && !bEndingPlay) SetComponentTickEnabled(true);
}

void USACombatComponent::HandleOwnerDied()
{
	FinishActionIfCurrent(ActiveAction, ESAActionEndReason::OwnerDied);
}

void USACombatComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted, FSAMeleePlaybackKey Expected)
{
	if (!IsCurrentPlayback(Expected) || Montage != ActiveStep.Montage) return;
	FinishActionIfCurrent(Expected.Action,
	                      bInterrupted ? ESAActionEndReason::Interrupted : ESAActionEndReason::Failed);
}

void USACombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted,
	FSAMeleePlaybackKey Expected)
{
	if (!IsCurrentPlayback(Expected) || Montage != ActiveStep.Montage) return;
	FinishActionIfCurrent(Expected.Action, ESAActionEndReason::Interrupted);
}
