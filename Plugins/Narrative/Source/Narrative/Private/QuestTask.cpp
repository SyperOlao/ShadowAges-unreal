//  Copyright Narrative Tools 2022.


#include "QuestTask.h"
#include "Quest.h"
#include "NarrativeComponent.h"
#include <TimerManager.h>


#define LOCTEXT_NAMESPACE "NarrativeQuestTask"

UNarrativeTask::UNarrativeTask()
{
	CurrentProgress = 0;
	RequiredQuantity = 1;
	bOptional = false;
	bHidden = false;
	TickInterval = 0.f;
	bIsActive = false;
}

void UNarrativeTask::BeginTask()
{
	bIsActive = true;

	//Cache all the useful values tasks will want
	if (UQuestBranch* OwningBranch = GetOwningBranch())
	{
		OwningQuest = OwningBranch->GetOwningQuest();

		if (OwningQuest)
		{
			OwningComp = OwningQuest->GetOwningNarrativeComponent();
		
			if (OwningComp)
			{
				OwningPawn = OwningComp->GetOwningPawn();
				OwningController = OwningComp->GetOwningController();
			}
		}
	}

	if (OwningComp && OwningComp->HasAuthority())
	{
		if (TickInterval > 0.f)
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(TimerHandle_TickTask, this, &UNarrativeTask::TickTask, TickInterval, true);
			}
		}

		K2_BeginTask();

		//Fire the first tick off after BeginTask since begin task will usually init things that TickTask may need
		TickTask();
	}
}

void UNarrativeTask::TickTask_Implementation()
{

}

void UNarrativeTask::EndTask()
{
	bIsActive = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimerHandle_TickTask);
	}
}

void UNarrativeTask::SetProgress(const int32 NewProgress)
{
	//if (OwningComp && !OwningComp->HasAuthority())
	//{
		//UE_LOG(LogNarrative, Warning, TEXT("Client task tried setting progress on a task. Tasks are server authoritative, only the server needs to set progress."));
		//return;
	//}

	if (bIsActive && NewProgress >= 0)
	{
		if (NewProgress != CurrentProgress)
		{
			const int32 OldProgress = CurrentProgress;

			CurrentProgress = FMath::Clamp(NewProgress, 0, RequiredQuantity);

			//Dont use IsComplete() because it would check if the task is optional which we don't want 
			if (CurrentProgress >= RequiredQuantity)
			{
				if (UQuestBranch* Branch = GetOwningBranch())
				{
					Branch->OnQuestTaskComplete(this);
				}
			}

			if (OwningQuest)
			{
				OwningQuest->OnQuestTaskProgressChanged(this, GetOwningBranch(), OldProgress, CurrentProgress);
			}
		}
	}
}

void UNarrativeTask::AddProgress(const int32 ProgressToAdd /*= 1*/)
{
	SetProgress(CurrentProgress + ProgressToAdd);
}

bool UNarrativeTask::IsComplete() const
{
	return CurrentProgress >= RequiredQuantity || bOptional;
}

void UNarrativeTask::CompleteTask()
{
	SetProgress(RequiredQuantity);
}

UQuestBranch* UNarrativeTask::GetOwningBranch() const
{
	return Cast<UQuestBranch>(GetOuter());
}

FText UNarrativeTask::GetTaskDescription_Implementation() const
{
	return LOCTEXT("DefaultNarrativeTaskDescription", "Task Description");
}

FText UNarrativeTask::GetTaskNodeDescription_Implementation() const
{
	return GetTaskDescription();
}

#undef LOCTEXT_NAMESPACE