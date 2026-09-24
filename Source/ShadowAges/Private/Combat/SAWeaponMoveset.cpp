#include "Combat/SAWeaponMoveset.h"

bool USAWeaponMoveset::Validate(FString& OutError) const
{
	OutError.Reset();

	const auto Fail = [&OutError](const FString& ErrorMessage)
	{
		OutError = ErrorMessage;
		return false;
	};

	if (Steps.IsEmpty() || Steps.Num() > Max_Steps)
	{
		return Fail(FString::Printf(TEXT("Moveset must contain 1..%d steps."), Max_Steps));
	}

	if (!Steps.IsValidIndex(EntryStepIndex))
	{
		return Fail(TEXT("EntryStepIndex is outside Steps."));
	}
	TSet<FName> StepIds;
	FName ExpectedGroup = NAME_None;

	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const FSAMeleeStep& Step = Steps[Index];
		const FString Prefix = FString::Printf(TEXT("Step[%d]: "), Index);

		if (Step.StepId.IsNone() || StepIds.Contains(Step.StepId))
		{
			return Fail(Prefix + TEXT("StepId must be nonempty and unique."));
		}
		StepIds.Add(Step.StepId);

		UAnimMontage* Montage = Step.Montage.Get();
		if (!IsValid(Montage) || !IsValid(Montage->GetSkeleton()))
		{
			return Fail(Prefix + TEXT("Montage and its Skeleton must be assigned."));
		}
		const float Length = Montage->GetPlayLength();
		const float EffectiveRate = Step.PlayRate * Montage->RateScale;
		if (!FMath::IsFinite(Length) || Length <= 0.0f
			|| !FMath::IsFinite(Step.PlayRate) || Step.PlayRate <= 0.0f
			|| !FMath::IsFinite(Montage->RateScale) || Montage->RateScale <= 0.0f
			|| !FMath::IsFinite(EffectiveRate) || EffectiveRate <= 0.0f
			|| !FMath::IsFinite(Length / EffectiveRate))
		{
			return Fail(Prefix + TEXT("Montage length and playback rates must be finite and positive."));
		}
		if (Montage->bEnableAutoBlendOut)
		{
			return Fail(Prefix + TEXT("Disable Enable Auto Blend Out: the combat executor owns completion."));
		}
		if (Montage->CompositeSections.Num() != 1 || Montage->SlotAnimTracks.Num() != 1)
		{
			return Fail(Prefix + TEXT("Montage must have exactly one section and one slot track."));
		}
		float SectionStart = 0.0f;
		float SectionEnd = 0.0f;
		Montage->GetSectionStartAndEndTime(0, SectionStart, SectionEnd);
		if (!FMath::IsFinite(SectionStart) || !FMath::IsFinite(SectionEnd)
			|| !FMath::IsNearlyZero(SectionStart)
			|| !FMath::IsNearlyEqual(SectionEnd, Length)
			|| !Montage->CompositeSections[0].NextSectionName.IsNone())
		{
			return Fail(Prefix + TEXT("The only section must cover the whole montage and have no next section."));
		}
		const FName Group = Montage->GetGroupName();
		if (Group.IsNone() || Montage->SlotAnimTracks[0].SlotName.IsNone())
		{
			return Fail(Prefix + TEXT("Montage slot and group must be configured."));
		}
		if (Index == 0)
		{
			ExpectedGroup = Group;
		}
		else if (Group != ExpectedGroup)
		{
			return Fail(Prefix + TEXT("All steps must use the same montage group."));
		}
		if (Step.NextStepIndex != INDEX_NONE && !Steps.IsValidIndex(Step.NextStepIndex))
		{
			return Fail(Prefix + TEXT("NextStepIndex must be -1 or a valid step index."));
		}
		if (!FMath::IsFinite(Step.InputBufferSeconds)
			|| Step.InputBufferSeconds <= 0.0f || Step.InputBufferSeconds > 1.0f)
		{
			return Fail(Prefix + TEXT("InputBufferSeconds must be in (0, 1]."));
		}

		if (!FMath::IsFinite(Step.StaminaCost) || Step.StaminaCost < 0.0f
			|| !FMath::IsFinite(Step.Damage) || Step.Damage < 0.0f
			|| !FMath::IsFinite(Step.PoiseDamage) || Step.PoiseDamage < 0.0f)
		{
			return Fail(Prefix + TEXT("StaminaCost, Damage and PoiseDamage must be finite and nonnegative."));
		}

		if (!FMath::IsFinite(Step.Movement.MoveInputScale)
			|| Step.Movement.MoveInputScale < 0.0f || Step.Movement.MoveInputScale > 1.0f)
		{
			return Fail(Prefix + TEXT("MoveInputScale must be in [0, 1]."));
		}

		if (Step.TraceProfile.IsNone())
		{
			return Fail(Prefix + TEXT("TraceProfile must be assigned."));
		}

		if (Step.NextStepIndex != INDEX_NONE)
		{
			if (!Step.ComboAccept.IsValidFor(Length) || !Step.ComboBranch.IsValidFor(Length)
				|| Step.ComboAccept.BeginSeconds > Step.ComboBranch.BeginSeconds
				|| Step.ComboBranch.EndSeconds > Step.ComboAccept.EndSeconds)
			{
				return Fail(Prefix + TEXT("ComboBranch must be a nonempty window inside ComboAccept and the montage."));
			}
		}

		float PreviousHitEnd = 0.0f;
		for (int32 WindowIndex = 0; WindowIndex < Step.HitWindows.Num(); ++WindowIndex)
		{
			const FSAMeleeHitWindow& Window = Step.HitWindows[WindowIndex];
			if (!Window.IsValidFor(Length) || Window.BeginSeconds < PreviousHitEnd)
			{
				return Fail(Prefix + FString::Printf(
					TEXT("HitWindows[%d] must be in range, sorted and nonoverlapping."), WindowIndex));
			}
			if (Step.NextStepIndex != INDEX_NONE && Window.EndSeconds > Step.ComboBranch.BeginSeconds)
			{
				return Fail(Prefix + TEXT("All hit windows must end before or at ComboBranch.BeginSeconds."));
			}
			PreviousHitEnd = Window.EndSeconds;
		}

		const FSAAttackCancelPolicy& Cancel = Step.CancelPolicy;
		if ((Cancel.bAllowDodge && !Cancel.DodgeWindow.IsValidFor(Length))
			|| (Cancel.bAllowBlock && !Cancel.BlockWindow.IsValidFor(Length))
			|| (Cancel.bAllowEquip && !Cancel.EquipWindow.IsValidFor(Length)))
		{
			return Fail(Prefix + TEXT("Each enabled cancel requires a valid time window within the montage."));
		}
	}

	for (int32 Start = 0; Start < Steps.Num(); ++Start)
	{
		TSet<int32> Visited;
		int32 Current = Start;
		while (Current != INDEX_NONE)
		{
			if (Visited.Contains(Current))
			{
				return Fail(FString::Printf(TEXT("Combo chain starting at Step[%d] contains a cycle."), Start));
			}
			Visited.Add(Current);
			Current = Steps[Current].NextStepIndex;
		}
	}

	return true;
}

