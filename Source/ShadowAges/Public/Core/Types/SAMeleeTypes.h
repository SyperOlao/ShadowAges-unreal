#pragma once
#include "CoreMinimal.h"
#include "Core/Types/SAActionTypes.h"
#include "SAMeleeTypes.generated.h"
class UAnimMontage;
class USkeletalMeshComponent;

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAMeleeTimeWindow
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0"))
	float BeginSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0"))
	float EndSeconds = 0.0f;

	bool IsValidFor(const float Length) const
	{
		return FMath::IsFinite(Length) && Length > 0.0f
			&& FMath::IsFinite(BeginSeconds) && FMath::IsFinite(EndSeconds)
			&& BeginSeconds >= 0.0f && BeginSeconds < EndSeconds
			&& EndSeconds <= Length;
	}
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAMeleeHitWindow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0"))
	float BeginSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0.0"))
	float EndSeconds = 0.0f;

	bool IsValidFor(float Length) const
	{
		return FMath::IsFinite(Length) && Length > 0.0f
			&& FMath::IsFinite(BeginSeconds) && FMath::IsFinite(EndSeconds)
			&& BeginSeconds >= 0.0f && BeginSeconds < EndSeconds
			&& EndSeconds <= Length;
	}
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAAttackMovementPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MoveInputScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bAllowLookInput = true;
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAAttackCancelPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cancel")
	bool bAllowDodge = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cancel", meta = (EditCondition = "bAllowDodge"))
	FSAMeleeTimeWindow DodgeWindow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cancel")
	bool bAllowBlock = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cancel", meta = (EditCondition = "bAllowBlock"))
	FSAMeleeTimeWindow BlockWindow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cancel")
	bool bAllowEquip = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cancel", meta = (EditCondition = "bAllowEquip"))
	FSAMeleeTimeWindow EquipWindow;
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAMeleeStep
{
	GENERATED_BODY()

	FSAMeleeStep()
	{
		ComboAccept.BeginSeconds = 0.08f;
		ComboAccept.EndSeconds = 0.57f;
		ComboBranch.BeginSeconds = 0.43f;
		ComboBranch.EndSeconds = 0.57f;
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName StepId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.001"))
	float PlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = "-1"))
	int32 NextStepIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float InputBufferSeconds = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	FSAMeleeTimeWindow ComboAccept;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combo")
	FSAMeleeTimeWindow ComboBranch;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cost", meta = (ClampMin = "0.0"))
	float StaminaCost = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float Damage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float PoiseDamage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	FName TraceProfile = FName(TEXT("DefaultBlade"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	TArray<FSAMeleeHitWindow> HitWindows;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	FSAAttackMovementPolicy Movement;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cancel")
	FSAAttackCancelPolicy CancelPolicy;
};

struct SHADOWAGES_API FSAMeleeWindowSlice
{
	int32 WindowSerial = INDEX_NONE;
	float BeginPosition = 0.0f;
	float EndPosition = 0.0f;
};

struct SHADOWAGES_API FSAMeleePoseFrame
{
	FSAMeleePlaybackKey Key;
	FSAMeleeStep Step;
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	float PreviousPosition = 0.0f;
	float CurrentPosition = 0.0f;
	bool bFirstSample = false;
	TArray<FSAMeleeWindowSlice> HitSlices;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FSAOnMeleePoseAdvanced, const FSAMeleePoseFrame&);
