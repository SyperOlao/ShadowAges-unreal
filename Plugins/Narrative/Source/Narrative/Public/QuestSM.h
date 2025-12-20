// Copyright Narrative Tools 2022. 

#pragma once

#include "CoreMinimal.h"
#include "NarrativeNodeBase.h"
#include "QuestSM.generated.h"

class UQuest;
class UQuestState;
class UQuestBranch;
class UNarrativeDataTask;
class UQuestBlueprint;

/**
* Used for checking the result of our state machine.
*/
UENUM(BlueprintType)
enum class EStateNodeType : uint8
{
	// Implicit fail - The state is not marked as accept
	Regular,
	// Success - state is an accept state
	Success,
	// Explicit fail - state is marked as failure
	Failure
};

//A quest is a series of state machines, branches are taken by completing all the FNarrativeTasks in that branch.
USTRUCT(BlueprintType)
struct NARRATIVE_API FQuestTask
{
GENERATED_BODY()

public:
	FQuestTask(UNarrativeDataTask* InEvent, const FString& InArgument, const int32 InQuantity, const bool bInHidden, const bool bInOptional) : Task(InEvent), Argument(InArgument), Quantity(InQuantity), bHidden(bInHidden), bOptional(bInOptional)
	{
		CurrentProgress = 0;
	};

	FQuestTask()
	{
		Task = nullptr;
		Argument = "";
		Quantity = 1;
		CurrentProgress = 0;
		bHidden = false;
		bOptional = false;
		bRetroactive = false;
	};

	virtual ~FQuestTask(){};

	/**The event the player needs to do to complete this task*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	UNarrativeDataTask* Task;

	/**The reference to be passed into the action*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	FString Argument;

	/**The amount of times we need to complete this action to move on to the next part of the quest*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task", meta = (ClampMin = 1))
	int32 Quantity;

	/**Should this task be hidden from the player (Great for quests with hidden options)*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	bool bHidden;

	/** SINGLE PLAYER ONLY: Should this task be optional?*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	bool bOptional;

	/** SINGLE PLAYER ONLY: Should it count if the player has already done this task in the past?*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Task")
	bool bRetroactive;

	/**Description for this task. For example "Kill 10 Goblins", "Obtain an Iron Sword", "Find the briefcase", etc... */
	UPROPERTY(EditAnywhere, Category = "Task", BlueprintReadOnly, meta = (MultiLine = true))
	FText TaskDescription;

	UPROPERTY(BlueprintReadOnly, Category = "Quests")
	int32 CurrentProgress;

};

/**Base class for states and branches in the quests state machine*/
 UCLASS()
 class NARRATIVE_API UQuestNode : public UNarrativeNodeBase
 {

	 GENERATED_BODY()

 public:

	 /**Called when the node activates/deactivates*/
	 virtual void Activate();
	 virtual void Deactivate();

	 virtual void Tick(const float DeltaTime);

	 virtual void EnsureUniqueID() override;


	/**Description for this quest node. For example "Kill 10 Goblins", "Find the Gemstone", or "I've found the Gemstone, I need to return to King Edward" */
	UPROPERTY(EditAnywhere, Category = "Details", BlueprintReadOnly, meta = (MultiLine = true, DisplayAfter="ID"))
	FText Description;

	//Get the quest that this quest node belongs to
	UQuest* GetOwningQuest() const;
	class UNarrativeComponent* GetOwningNarrativeComp() const;

	//The title the node should have on the quest node title
	virtual FText GetNodeTitle() const { return FText::FromString("Node"); };

	//TODO can probably remove this. Last time we went through this branch or state. Used for PIE debugging
	UPROPERTY()
	FDateTime LastExecTime;

	//Name of custom event to call when this state/branch is reached/taken
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Details", meta = (AdvancedDisplay=true))
	FName OnEnteredFuncName;

	//The quest object that owns this node. 
	UPROPERTY()
	class UQuest* OwningQuest;
 };

 DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStateReachedEvent);

UCLASS(BlueprintType)
class NARRATIVE_API UQuestState : public UQuestNode
{
	GENERATED_BODY()

public:

	UQuestState();

	/**Called when the node activates/deactivates*/
	virtual void Activate(TArray<FName> BranchToActivate);
	virtual void Deactivate();

	virtual FText GetNodeTitle() const override;

	virtual void Tick(const float DeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Quest State")
	TArray<UQuestBranch*> Branches;

	UPROPERTY(BlueprintReadOnly, Category = "Quest State")
	TArray<UQuestBranch*> InputBranches;

	/**Determines how the state is interpreted by the quest that has reached it .*/
	UPROPERTY(BlueprintReadOnly, Category = "Quest State")
	EStateNodeType StateNodeType;

};

UCLASS(BlueprintType)
class NARRATIVE_API UQuestBranch : public UQuestNode
{

	GENERATED_BODY()

public:

	UQuestBranch();

	/**Called when the node activates/deactivates*/
	virtual void Activate();
	virtual void Deactivate();

	/**Called when one of our quest tasks completes*/
	void OnQuestTaskComplete(class UNarrativeTask* Task);

     // Tick branch and its tasks
	virtual void Tick(const float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	virtual bool CanExecute();

	/**
	*The tasks that need to be completed to take this branch to its destination 
	* 
	* Plenty of quest tasks come with narrative, Please see the the Narrative/Content/DefaultTasks/ folder 
	* for some examples. You can copy these and create custom ones for your game, like ObtainItem, KillEnemy, etc - whatever you need. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Task")
	TArray<class UNarrativeTask*> QuestTasks;

	/**The task the player needs to complete

	In Narrative 3.0 these are still around in the editor for legacy reasons, but you'll need to convert your old quests to new ones.
	Simply add CompleteNarrativeTask task to the new QuestTasks array, and fill the fields in. If you still have any issues try deleting and re-adding the node. */
	UPROPERTY()
	FQuestTask Task;

	/**The task the player needs to complete

	In Narrative 3.0 these are still around in the editor for legacy reasons, but you'll need to convert your old quests to new ones.
	Simply add CompleteNarrativeTask task to the new QuestTasks array, and fill the fields in. If you still have any issues try deleting and re-adding the node. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Task", meta = (AdvancedDisplay=true))
	TArray<FQuestTask> Tasks;

	/**Should this branch be hidden from the player on the narrative demo UI (Great for quests with hidden options that we want to be part
	of the quest logic, but we don't want the UI to show)*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Details")
	bool bHidden;

	/**State where we will go if this branch is taken. Branch will be ignored if this is null*/
	UPROPERTY(BlueprintReadOnly, Category = "Details")
	UQuestState* DestinationState;

	UPROPERTY(BlueprintReadOnly, Category = "Details")
	TArray<UQuestState*> ParentStates;

	UPROPERTY(BlueprintReadOnly, Category = "Details")
	bool bExecute = false;

	virtual FText GetNodeTitle() const override;

protected:

#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

#endif

	virtual bool AreTasksComplete() const;

};

