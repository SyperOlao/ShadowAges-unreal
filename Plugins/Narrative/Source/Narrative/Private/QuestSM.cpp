// Copyright Narrative Tools 2022. 

#include "QuestSM.h"
#include "Quest.h"
#include "NarrativeEvent.h"
#include "NarrativeDataTask.h"
#include "NarrativeComponent.h"
#include "NarrativeQuestSettings.h"
#include "QuestTask.h"
#include "NarrativeCondition.h"

#define LOCTEXT_NAMESPACE "StateMachine"

UQuestState::UQuestState()
{
	//Description = LOCTEXT("QuestStateDescription", "Write an update to appear in the players quest journal here.");
}

void UQuestState::Activate(TArray<FName> BranchToActivate)
{
	//Once the state activates, activate all the branches it has 
	

	for (auto& Branch : Branches)
	{
		if (BranchToActivate.IsEmpty() || BranchToActivate.Contains(Branch->GetID()))
		{
			if (Branch)
			{
				Branch->Activate();
			}
		}
	}

	UQuestNode::Activate();
}

void UQuestState::Deactivate()
{
	//Once the state deactivates, deactivate all the branches it has 
	for (auto& Branch : Branches)
	{
		if (Branch)
		{
			Branch->Deactivate();
		}
	}

	UQuestNode::Deactivate();
}

UQuestBranch::UQuestBranch()
{
	//Description = LOCTEXT("QuestBranchDescription", "Describe what the player needs to do to complete this task.");

	Tasks.SetNum(1);
}

void UQuestBranch::Activate()
{
	bExecute = true;
	for (UNarrativeTask* QuestTask : QuestTasks)
	{
		if (QuestTask)
		{
			if (CanExecute())
			{
				QuestTask->BeginTask();
			}
		}
	}
	
	UQuestNode::Activate();
}

void UQuestBranch::Deactivate()
{
	bExecute = false;
	for (UNarrativeTask* QuestTask : QuestTasks)
	{
		if (QuestTask)
		{
			if (!CanExecute())
			{
				QuestTask->EndTask();
			}
		}
	}

	UQuestNode::Deactivate();
}

void UQuestBranch::OnQuestTaskComplete(class UNarrativeTask* UpdatedTask)
{
	if (OwningQuest)
	{
		OwningQuest->OnQuestTaskCompleted(UpdatedTask, this);

		if (AreTasksComplete())
		{
			OwningQuest->TakeBranch(this);
		}
	}
}

void UQuestBranch::Tick(const float DeltaTime)
{
	if (bExecute)
	{
		if (CanExecute())
		{
			for (UNarrativeTask* QuestTask : QuestTasks)
			{
				if (QuestTask && !QuestTask->bIsActive)
				{
					QuestTask->BeginTask();
				}
			}
		}
		else
		{
			for (UNarrativeTask* QuestTask : QuestTasks)
			{
				if (QuestTask && QuestTask->bIsActive)
				{
					QuestTask->EndTask();
				}
			}
		}
	}
	for (UNarrativeTask* LTask : QuestTasks) 
	{ 
		if (LTask)
		{
			LTask->Tick(DeltaTime);
		} 
	}
}

bool UQuestBranch::CanExecute()
{
	for (UNarrativeCondition* Condition : Conditions)
	{
		if (!Condition->CheckCondition(OwningQuest->OwningPawn, OwningQuest->OwningController, OwningQuest->OwningComp))
		{
			return false;
		}
	}
	return true;
}

FText UQuestBranch::GetNodeTitle() const
{
	FString Title = "";
	int32 Idx = 0;

	Title += "Tasks: ";
	Title += LINE_TERMINATOR;

	for (UNarrativeTask* QuestTask : QuestTasks)
	{
		if (!QuestTask)
		{
			continue;
		}

		if (Idx > 0)
		{
			Title += LINE_TERMINATOR;
		}

		//For custom tasks, just display "Task"
		const FString TaskName = QuestTask->GetTaskNodeDescription().ToString();

		++Idx;
	}
	
	return FText::FromString(Title);
}

#if WITH_EDITOR

void UQuestBranch::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UQuestBranch, QuestTasks))
	{
		const int32 Idx = PropertyChangedEvent.GetArrayIndex(PropertyChangedEvent.GetPropertyName().ToString());

		if (QuestTasks.IsValidIndex(Idx))
		{
			
		}
	}
}

void UQuestBranch::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

#endif

bool UQuestBranch::AreTasksComplete() const
{
	bool bCompletedAllTasks = true;

	for (auto& MyTask : QuestTasks)
	{
		if (!MyTask->IsComplete())
		{
			bCompletedAllTasks = false;
			break;
		}
	}

	return bCompletedAllTasks;
}

FText UQuestState::GetNodeTitle() const
{
	if (ID == NAME_None)
	{
		return FText::GetEmpty();
	}

	FString ReadableString;
	ReadableString = FName::NameToDisplayString(ID.ToString(), false);
	return FText::FromString(ReadableString);
}

void UQuestState::Tick(const float DeltaTime)
{
	for (UQuestBranch* Branch : Branches)
	{
		if (Branch)
		{
			Branch->Tick(DeltaTime);
		}
	}
}


void UQuestNode::Activate()
{
	if (OwningQuest)
	{
		struct SOnEnteredStruct
		{
			UQuestNode* Node;
			bool bActivated;
		};

		SOnEnteredStruct Parms;
		Parms.Node = this;
		Parms.bActivated = true;

		if (OwningQuest->GetOwningNarrativeComponent()->bIsLoading)
		{
			if (OwningQuest->bSkipEventsAndFunctionCallWhileSaveLoading)
			{
				return;
			}
		}

		if (UFunction* Func = OwningQuest->FindFunction(OnEnteredFuncName))
		{
			OwningQuest->ProcessEvent(Func, &Parms);
		}

		ProcessEvents(OwningQuest->OwningPawn, OwningQuest->OwningController, OwningQuest->OwningComp, EEventRuntime::Start);
	}
}

void UQuestNode::Deactivate()
{
	if (OwningQuest)
	{
		struct SOnEnteredStruct
		{
			UQuestNode* Node;
			bool bActivated;
		};

		if (!OwningQuest->bPlayOnlyStartNodeEvent)
		{
			SOnEnteredStruct Parms;
			Parms.Node = this;
			Parms.bActivated = false;

			if (UFunction* Func = OwningQuest->FindFunction(OnEnteredFuncName))
			{
				OwningQuest->ProcessEvent(Func, &Parms);
			}
		}

		ProcessEvents(OwningQuest->OwningPawn, OwningQuest->OwningController, OwningQuest->OwningComp, EEventRuntime::End);
	}
}

void UQuestNode::Tick(const float DeltaTime)
{
}

void UQuestNode::EnsureUniqueID()
{
	if (UQuest* Quest = GetOwningQuest())
	{
		TArray<UQuestNode*> QuestNodes = Quest->GetNodes();
		TArray<FName> NodeIDs;

		for (auto& Node : QuestNodes)
		{
			if (Node != this)
			{
				NodeIDs.Add(Node->ID);
			}
		}

		int32 Suffix = 1;
		FName NewID = ID;

		if (!NodeIDs.Contains(NewID))
		{
			return;
		}

		// Check if the new ID already exists in the array
		while (NodeIDs.Contains(NewID))
		{
			// If it does, add a numeric suffix and try again
			NewID = FName(*FString::Printf(TEXT("%s%d"), *ID.ToString(), Suffix));
			Suffix++;
		}

		ID = NewID;
	}
}

UQuest* UQuestNode::GetOwningQuest() const
{
	//In the editor, outer will be the quest. At runtime, quest object is manually set
	return OwningQuest ? OwningQuest : Cast<UQuest>(GetOuter());
}

class UNarrativeComponent* UQuestNode::GetOwningNarrativeComp() const
{
	if (UQuest* Quest = GetOwningQuest())
	{
		return Quest->GetOwningNarrativeComponent();
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE