#pragma once
#include "CoreMinimal.h"
#include "SAActionTypes.generated.h"

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAActionHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Combat")
	FGuid Id;

	bool IsValid() const { return Id.IsValid(); }

	friend bool operator==(const FSAActionHandle& A, const FSAActionHandle& B)
	{
		return A.Id == B.Id;
	}

	friend bool operator!=(const FSAActionHandle& A, const FSAActionHandle& B)
	{
		return A.Id != B.Id;
	}
};

UENUM(BlueprintType)
enum class ESAActionKind : uint8
{
	Melee,
	Cast,
	Equip,
	Reaction
};

UENUM(BlueprintType)
enum class ESAActionType : uint8
{
	Idle,
	Reserved,
	Executing
};

UENUM(BlueprintType)
enum class ESAActionEndReason : uint8
{
	Completed,
	Cancelled,
	Interrupted,
	OwnerDied,
	Failed,
	TimeOut,
	OwnerEnded
};

UENUM(BlueprintType)
enum class ESACombatRequestResult : uint8
{
	Started,
	Buffered,
	Busy,
	Dead,
	MissingDependencies,
	InvalidData,
	InvalidHandle,
	UnsupportedAction,
	InsufficientStamina,
	WindowClosed,
	Failed
};

UENUM(BlueprintType)
enum class ESACombatCancelIntent : uint8
{
	Dodge,
	Block,
	Equip
};

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAMeleePlaybackKey
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FSAActionHandle Action;
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 StepIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 Generation = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 MontageInstanceID = INDEX_NONE;

	bool IsValid() const
	{
		return Action.IsValid() && StepIndex >= 0 && Generation > 0 && MontageInstanceID != INDEX_NONE;
	}

	friend bool operator==(const FSAMeleePlaybackKey& A, const FSAMeleePlaybackKey& B)
	{
		return A.Action == B.Action && A.StepIndex == B.StepIndex
			&& A.Generation == B.Generation
			&& A.MontageInstanceID == B.MontageInstanceID;
	}
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FSAOnActionFinished, FSAActionHandle, ESAActionEndReason);
DECLARE_MULTICAST_DELEGATE_OneParam(FSAOnMeleeStepEvent, FSAMeleePlaybackKey);
DECLARE_MULTICAST_DELEGATE_OneParam(FSAOnComboAcceptOpened, FSAMeleePlaybackKey);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSAOnActionFinishedBP, FSAActionHandle, Action, ESAActionEndReason, Reason);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSAOnMeleeStepEventBP, FSAMeleePlaybackKey, Key);
