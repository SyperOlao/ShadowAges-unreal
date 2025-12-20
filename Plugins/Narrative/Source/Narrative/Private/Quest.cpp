// Copyright Narrative Tools 2022. 

#include "Quest.h"
#include "NarrativeDataTask.h"
#include "QuestSM.h"
#include "Net/UnrealNetwork.h"
#include "QuestBlueprintGeneratedClass.h"
#include "NarrativeEvent.h"
#include "NarrativeComponent.h"
#include "NarrativeFunctionLibrary.h"

UQuest::UQuest()
{
	QuestName = FText::FromString("My New Quest");
	QuestDescription = FText::FromString("Enter a description for your quest here.");
}

class UNarrativeComponent* UQuest::GetOwningNarrativeComponent() const
{
	return OwningComp;
}

class APawn* UQuest::GetPawnOwner() const
{
	return OwningPawn;
}

UWorld* UQuest::GetWorld() const
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}

	if (GetOwningNarrativeComponent())
	{
		return GetOwningNarrativeComponent()->GetWorld();
	}
	
	return nullptr;
}

void UQuest::DuplicateAndInitializeFromQuest(UQuest* QuestTemplate)
{
	if (QuestTemplate)
	{
		//Duplicate the quest template, then steal all its states and branches
		UQuest* NewQuest = Cast<UQuest>(StaticDuplicateObject(QuestTemplate, this, NAME_None, RF_Transactional));
		NewQuest->SetFlags(RF_Transient | RF_DuplicateTransient);
		
		if (NewQuest)
		{
			QuestStartState = NewQuest->QuestStartState;
			States = NewQuest->States;
			Branches = NewQuest->Branches;
		}
	}
}

bool UQuest::Initialize(class UNarrativeComponent* InitializingComp, const FName& QuestStartID /*= NAME_None*/)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		//We need a valid narrative component to make a quest for 
		if (!InitializingComp)
		{
			return false;
		}

		if (UQuestBlueprintGeneratedClass* BGClass = Cast<UQuestBlueprintGeneratedClass>(GetClass()))
		{
			BGClass->InitializeQuest(this);

			//If a quest doesn't have any states or branches, or doesn't have a valid start state something has gone wrong and we should abort
			if (States.Num() == 0 || Branches.Num() == 0 || !QuestStartState)
			{
				return false;
			}

			//At this point, we should have a valid quest assigned to us. Check if we have a valid start state
			if (QuestStartState)
			{
				OwningComp = InitializingComp;

				OwningPawn = OwningComp->GetOwningPawn();
				OwningController = OwningComp->GetOwningController();

				for (auto& Node : GetNodes())
				{
					if (Node)
					{
						Node->OwningQuest = this;
					}
				}

				return true;
			}
		}
	}


	return false;
}

void UQuest::BeginQuest(TArray<FName> StartFromIDs, TArray<FName> BranchToActivate)
{
	QuestCompletion = EQuestCompletion::QC_Started;
	if (StartFromIDs.Num()<=0)
	{
		EnterState(QuestStartState, BranchToActivate);
	}
	else
	{
		for (FName StartFromID : StartFromIDs)
		{
			EnterState(GetState(StartFromID), BranchToActivate);
		}
	}

	BPOnQuestStarted(this);

	if (OwningComp)
	{
		OwningComp->OnQuestStarted.Broadcast(this);
	}
}

void UQuest::TakeBranch(UQuestBranch* Branch)
{
	//We're taking a branch, deactivate it, fire off its bound function and events, and then head to the destination state
	if (Branch)
	{
		Branch->Deactivate();
	}

	OnQuestBranchCompleted(Branch);

	//Client can call this function in order to process delegates and things but server needs to be setting the state, not client 
	if (OwningComp)
	{
		EnterState(Branch->DestinationState, TArray<FName>());
	}
}

void UQuest::CloseAllStateBeforeState(UQuestState* NewState)
{
	if (!NewState)
	{
		return;
	}

	TSet<UQuestState*> VisitedStates;
	TSet<UQuestBranch*> VisitedBranches;

	TFunction<void(UQuestState*)> RecurseState;
	RecurseState = [&](UQuestState* State)
		{
			if (!State)
			{
				return;
			}

			if (VisitedStates.Contains(State))
			{
				return;
			}

			VisitedStates.Add(State);

			for (UQuestBranch* Branch : State->InputBranches)
			{
				if (!Branch)
				{
					continue;
				}

				if (VisitedBranches.Contains(Branch))
				{
					continue;
				}

				VisitedBranches.Add(Branch);

				for (UQuestState* ParentState : Branch->ParentStates)
				{
					if (!ParentState)
					{
						continue;
					}

					RecurseState(ParentState);
				}

				Branch->Deactivate();
			}

			if (State != NewState && CurrentStates.Contains(State))
			{
				State->Deactivate();
				CurrentStates.Remove(State);
			}
		};

	RecurseState(NewState);
}


void UQuest::LeaveState(UQuestState* NewState)
{
	if (CurrentStates.Contains(NewState))
	{
		NewState->Deactivate();
		CurrentStates.Remove(NewState);
	}
}

void UQuest::EnterState(UQuestState* NewState, TArray<FName> BranchToActivate)
{
	if (CurrentStates.Contains(NewState))
	{
		return;
	}

	if (NewState && OwningComp)
	{
		CurrentStates.Add(NewState);

		//Update the quests completion

		if (NewState->StateNodeType == EStateNodeType::Success)
		{
			SucceedQuest(NewState->Description);
		}
		else if (NewState->StateNodeType == EStateNodeType::Failure)
		{
			FailQuest(NewState->Description);
		}

		//Finally, activate our new state, therefore activating its branches allowing us to take one to progress through the quest 
		NewState->Activate(BranchToActivate);

		//If we're loading quests back in off disk we don't want to broadcast any progress or anything
		if (OwningComp->bIsLoading)
		{
			return;
		}

		//Fire off delegates 
		BPOnQuestNewState(this, NewState);

		if (OwningComp)
		{
			OwningComp->OnQuestNewState.Broadcast(this, NewState);
		}

		QuestNewState.Broadcast(this, NewState);
	}
}

TArray<UQuestBranch*> UQuest::GetActiveBranches()
{
	TArray<UQuestBranch*> ResultQuestBranches;

	for (UQuestBranch* Branch : Branches)
	{
		if (Branch->bExecute)
		{
			ResultQuestBranches.Add(Branch);
		}
	}

	return ResultQuestBranches;
}

class UQuestState* UQuest::GetState(FName ID) const
{
	for (auto& State : States)
	{
		if (State->GetID() == ID)
		{
			return State;
		}
	}
	return nullptr;
}

class UQuestBranch* UQuest::GetBranch(FName ID) const
{
	for (auto& Branch : Branches)
	{
		if (Branch->GetID() == ID)
		{
			return Branch;
		}
	}
	return nullptr;
}

FText UQuest::GetQuestName() const
{
	return QuestName;
}

FText UQuest::GetQuestDescription() const
{
	return QuestDescription;
}

void UQuest::FailQuest(FText QuestFailedMessage)
{
	QuestCompletion = EQuestCompletion::QC_Failed;

	BPOnQuestFailed(this, QuestFailedMessage);

	QuestFailed.Broadcast(this, QuestFailedMessage);

	if (OwningComp)
	{
		OwningComp->OnQuestFailed.Broadcast(this, QuestFailedMessage);
	}
}

void UQuest::SucceedQuest(FText QuestSucceededMessage)
{
	QuestCompletion = EQuestCompletion::QC_Succeded;

	BPOnQuestSucceeded(this, QuestSucceededMessage);

	QuestSucceeded.Broadcast(this, QuestSucceededMessage);

	if (OwningComp)
	{
		OwningComp->OnQuestSucceeded.Broadcast(this, QuestSucceededMessage);
	}
}

void UQuest::OnQuestTaskProgressChanged(const UNarrativeTask* Task, const class UQuestBranch* Step, int32 CurrentProgress, int32 RequiredProgress)
{
	BPOnQuestTaskProgressChanged(this, Task, Step, CurrentProgress, RequiredProgress);

	QuestTaskProgressChanged.Broadcast(this, Task, Step, CurrentProgress, RequiredProgress);

	if (OwningComp)
	{
		OwningComp->OnQuestTaskProgressChanged.Broadcast(this, Task, Step, CurrentProgress, RequiredProgress);
	}
}

void UQuest::OnQuestTaskCompleted(const UNarrativeTask* Task, const class UQuestBranch* Branch)
{
	BPOnQuestTaskCompleted(this, Task, Branch);

	QuestTaskCompleted.Broadcast(this, Task, Branch);

	if (OwningComp)
	{
		OwningComp->OnQuestTaskCompleted.Broadcast(this, Task, Branch);
	}
}

void UQuest::OnQuestBranchCompleted(const class UQuestBranch* Step)
{
	QuestBranchCompleted.Broadcast(this, Step);

	BPOnQuestBranchCompleted(this, Step);

	if (OwningComp)
	{
		OwningComp->OnQuestBranchCompleted.Broadcast(this, Step);
	}
}

TArray<UQuestNode*> UQuest::GetNodes() const
{
	TArray<UQuestNode*> Ret;

	for (auto& State : States)
	{
		Ret.Add(State);
	}

	for (auto& Branch : Branches)
	{
		Ret.Add(Branch);
	}

	return Ret;
}
