// Copyright Narrative Tools 2022. 

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "QuestSM.h"
#include "QuestTask.h"
#include "Quest.generated.h"

class UQuest;
class UQuestState;
class UQuestBranch;
class UQuestBlueprint;
class UNarrativeDataTask;

//Quests
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestBranchCompleted, const UQuest*, Quest, const class UQuestBranch*, Branch);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestNewState, UQuest*, Quest, const UQuestState*, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnQuestTaskProgressChanged, const UQuest*, Quest, const UNarrativeTask*, ProgressedTask, const class UQuestBranch*, Branch, int32, OldProgress, int32, NewProgress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnQuestTaskCompleted, const UQuest*, Quest, const UNarrativeTask*, CompletedTask, const class UQuestBranch*, Branch);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestSucceeded, const UQuest*, Quest, const FText&, QuestSucceededMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuestFailed, const UQuest*, Quest, const FText&, QuestFailedMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestStarted, const UQuest*, Quest);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestForgotten, const UQuest*, Quest);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuestRestarted, const UQuest*, Quest);

// Represents the state of a particular quest
UENUM(BlueprintType)
enum class EQuestCompletion : uint8
{
	QC_NotStarted UMETA(DisplayName="Not Started"),
	QC_Started UMETA(DisplayName = "Started"),
	QC_Succeded UMETA(DisplayName = "Succeeded"),
	QC_Failed  UMETA(DisplayName = "Failed")
};

UCLASS(Blueprintable, BlueprintType)
class NARRATIVE_API UQuest : public UObject
{
	GENERATED_BODY()
	
protected:

	friend class UNarrativeTask;
	friend class UQuestState;
	friend class UQuestBranch;

	UQuest();

	//Dialogue assets/nodes etc have the same name on client and server, so can be referenced over the network 
	bool IsNameStableForNetworking() const override { return true; };
	bool IsSupportedForNetworking() const override { return true; };

	//The current state the player is at in this quest


	virtual UWorld* GetWorld() const override;

public:

	UPROPERTY(BlueprintReadOnly, Category = "Quests")
	TArray<UQuestState*> CurrentStates;

    // Non-blueprint tick for quests
	UFUNCTION(BlueprintImplementableEvent)
	void Recive_Tick(const float DeltaTime);

	virtual void Tick(const float DeltaTime) { Recive_Tick(DeltaTime); }

	UFUNCTION(BlueprintPure, Category = "Narrative")
	class UNarrativeComponent* GetOwningNarrativeComponent() const;
		
	UFUNCTION(BlueprintPure, Category = "Narrative")
	class APawn* GetPawnOwner() const;


	//Initialize this quest from its blueprint generated class. Return true if successful. 
	virtual bool Initialize(class UNarrativeComponent* InitializingComp, const FName& QuestStartID = NAME_None);
	virtual void DuplicateAndInitializeFromQuest(UQuest* QuestTemplate);

	virtual void BeginQuest(TArray<FName> StartFromIDs, TArray<FName> BranchToActivate);

	UFUNCTION(BlueprintCallable, Category = "Quest")
	virtual void TakeBranch(UQuestBranch* Branch);

	UFUNCTION(BlueprintCallable)
	void LeaveState(UQuestState* NewState);

	UFUNCTION(BlueprintCallable)
	void CloseAllStateBeforeState(UQuestState* NewState);

	UFUNCTION(BlueprintCallable, Category = "Quest")
	virtual void EnterState(UQuestState* NewState, TArray<FName> BranchToActivate);

	UFUNCTION(BlueprintCallable, Category = "Quest")
	TArray<UQuestBranch*> GetActiveBranches();

	UFUNCTION(BlueprintPure, Category = "Quest")
	class UQuestState* GetState(FName ID) const;

	UFUNCTION(BlueprintPure, Category = "Quest")
	class UQuestBranch* GetBranch(FName ID) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest Details")
	FText QuestName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest Details", meta = (MultiLine = true))
	FText QuestDescription;

	UFUNCTION(BlueprintCallable, Category = "Quests")
    FText GetQuestName() const;

	UFUNCTION(BlueprintCallable, Category = "Quests")
	FText GetQuestDescription() const;

	//The beginning state of this quest
	UPROPERTY(BlueprintReadOnly, Category = "Quests")
	UQuestState* QuestStartState;

	//Holds all of the states in the quest
	UPROPERTY()
	TArray<UQuestState*> States;

	//Holds all of the branches in the quest
	UPROPERTY()
	TArray<UQuestBranch*> Branches;

	//The branch that was taken to get to the current state
	UPROPERTY(BlueprintReadOnly, Category = "Quests")
	UQuestBranch* PreviousBranch;

	/**Current quest progress*/
	UPROPERTY()
	EQuestCompletion QuestCompletion;

	/**All input for this quest*/
	UPROPERTY()
	TArray<FString> QuestActivities;

	UPROPERTY(BlueprintReadOnly, Category = "Quests")
	class UNarrativeComponent* OwningComp;

	UPROPERTY(BlueprintReadOnly, Category = "Quests")
	class APawn* OwningPawn;

	UPROPERTY(BlueprintReadOnly, Category = "Quests")
	class APlayerController* OwningController;
	
	UPROPERTY(EditAnywhere, Category = "Quests")
	bool bPlayOnlyStartNodeEvent = true;

	UPROPERTY(EditAnywhere, Category = "Quests")
	bool bSkipEventsAndFunctionCallWhileSaveLoading = false;

protected:

	/**Called when a quest objective has been completed.*/
	UPROPERTY(BlueprintAssignable, Category = "Quests")
	FOnQuestBranchCompleted QuestBranchCompleted;

	/**Called when a quest objective is updated and we've received a new objective*/
	UPROPERTY(BlueprintAssignable, Category = "Quests")
		FOnQuestNewState QuestNewState;

	/**Called when a quest task in a branch has made progress. ie 6 out of 10 coins found*/
	UPROPERTY(BlueprintAssignable, Category = "Quests")
		FOnQuestTaskProgressChanged QuestTaskProgressChanged;

	/**Called when a quest task on a branch is completed ie 10 out of 10 coins found*/
	UPROPERTY(BlueprintAssignable, Category = "Quests")
		FOnQuestTaskCompleted QuestTaskCompleted;

	/**Called when a quest is completed.*/
	UPROPERTY(BlueprintAssignable, Category = "Quests")
		FOnQuestSucceeded QuestSucceeded;

	/**Called when a quest is failed.*/
	UPROPERTY(BlueprintAssignable, Category = "Quests")
		FOnQuestFailed QuestFailed;

	/**Called when a quest is started.*/
	UPROPERTY(BlueprintAssignable, Category = "Quests")
		FOnQuestStarted QuestStarted;

	/**Called when a quest is forgotten.*/
	UPROPERTY(BlueprintAssignable, Category = "Quests")
		FOnQuestForgotten QuestForgotten;

	/**Called when a quest is restarted.*/
	UPROPERTY(BlueprintAssignable, Category = "Quests")
		FOnQuestRestarted QuestRestarted;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName="On Quest Started"))
		void BPOnQuestStarted(const UQuest* Quest);

	UFUNCTION(BlueprintCallable, Category = "Quest")
		void FailQuest(FText QuestFailedMessage);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Quest Failed"))
		void BPOnQuestFailed(const UQuest* Quest, const FText& QuestFailedMessage);

	/**Manually set the quest as succeeded. You'll need to provide some text for the UI as theres no node  the quest, you're manually succeeding it.*/
	UFUNCTION(BlueprintCallable, Category = "Quest")
		void SucceedQuest(FText QuestSucceededMessage);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Quest Succeeded"))
		void BPOnQuestSucceeded(const UQuest* Quest, const FText& QuestSucceededMessage);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Quest New State"))
		void BPOnQuestNewState(UQuest* Quest, const UQuestState* NewState);

	UFUNCTION()
		void OnQuestTaskProgressChanged(const UNarrativeTask* Task, const class UQuestBranch* Step, int32 CurrentProgress, int32 RequiredProgress);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Quest Objective Progress Made"))
	void BPOnQuestTaskProgressChanged(const UQuest* Quest, const UNarrativeTask* Task, const class UQuestBranch* Step, int32 CurrentProgress, int32 RequiredProgress);

	UFUNCTION()
		void OnQuestTaskCompleted(const UNarrativeTask* Task, const class UQuestBranch* Branch);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Quest Task Completed"))
		void BPOnQuestTaskCompleted(const UQuest* Quest, const UNarrativeTask* Task, const class UQuestBranch* Step);

	UFUNCTION()
		void OnQuestBranchCompleted(const class UQuestBranch* Branch);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Branch Taken"))
		void BPOnQuestBranchCompleted(const UQuest* Quest, const class UQuestBranch* Branch);

public:

	UFUNCTION(BlueprintPure, Category = "Quests")
	TArray<UQuestNode*> GetNodes() const;

	//Grab the completion of the quest 
	UFUNCTION(BlueprintPure, Category = "Quests")
	FORCEINLINE EQuestCompletion GetQuestCompletion() const { return QuestCompletion; };

};
