// Copyright Narrative Tools 2022. 


#include "NarrativeTestingWidget.h"
#include "NarrativeComponent.h"
#include <NarrativeFunctionLibrary.h>

void UNarrativeTestingWidget::NativeConstruct()
{
	Init();
	Super::NativeConstruct();
}

void UNarrativeTestingWidget::NativeDestruct()
{
	Super::NativeDestruct();
}

void UNarrativeTestingWidget::Init()
{
	//Bind local quest updates 
	if (NarrativeComponent)
	{
		NarrativeComponent->OnQuestStarted.AddDynamic(this, &UNarrativeTestingWidget::OnQuestStarted);
		NarrativeComponent->OnQuestFailed.AddDynamic(this, &UNarrativeTestingWidget::OnQuestFailed);
		NarrativeComponent->OnQuestSucceeded.AddDynamic(this, &UNarrativeTestingWidget::OnQuestSucceeded);
		NarrativeComponent->OnQuestBranchCompleted.AddDynamic(this, &UNarrativeTestingWidget::OnQuestStepCompleted);
		NarrativeComponent->OnQuestNewState.AddDynamic(this, &UNarrativeTestingWidget::OnQuestNewState);
		NarrativeComponent->OnQuestTaskProgressChanged.AddDynamic(this, &UNarrativeTestingWidget::OnQuestTaskProgressMade);
		NarrativeComponent->OnQuestTaskCompleted.AddDynamic(this, &UNarrativeTestingWidget::OnQuestTaskCompleted);
		NarrativeComponent->OnBeginSave.AddDynamic(this, &UNarrativeTestingWidget::OnBeginSave);
		NarrativeComponent->OnBeginLoad.AddDynamic(this, &UNarrativeTestingWidget::OnBeginLoad);
		NarrativeComponent->OnSaveComplete.AddDynamic(this, &UNarrativeTestingWidget::OnSaveComplete);
		NarrativeComponent->OnLoadComplete.AddDynamic(this, &UNarrativeTestingWidget::OnLoadComplete);
	}

	//Bind any shared quest updates 
	if (NarrativeComponent && NarrativeComponent->SharedNarrativeComp)
	{
		NarrativeComponent->SharedNarrativeComp->OnQuestStarted.AddDynamic(this, &UNarrativeTestingWidget::OnQuestStarted);
		NarrativeComponent->SharedNarrativeComp->OnQuestFailed.AddDynamic(this, &UNarrativeTestingWidget::OnQuestFailed);
		NarrativeComponent->SharedNarrativeComp->OnQuestSucceeded.AddDynamic(this, &UNarrativeTestingWidget::OnQuestSucceeded);
		NarrativeComponent->SharedNarrativeComp->OnQuestBranchCompleted.AddDynamic(this, &UNarrativeTestingWidget::OnQuestStepCompleted);
		NarrativeComponent->SharedNarrativeComp->OnQuestNewState.AddDynamic(this, &UNarrativeTestingWidget::OnQuestNewState);
		NarrativeComponent->SharedNarrativeComp->OnQuestTaskProgressChanged.AddDynamic(this, &UNarrativeTestingWidget::OnQuestTaskProgressMade);
		NarrativeComponent->SharedNarrativeComp->OnQuestTaskCompleted.AddDynamic(this, &UNarrativeTestingWidget::OnQuestTaskCompleted);
		NarrativeComponent->SharedNarrativeComp->OnBeginSave.AddDynamic(this, &UNarrativeTestingWidget::OnBeginSave);
		NarrativeComponent->SharedNarrativeComp->OnBeginLoad.AddDynamic(this, &UNarrativeTestingWidget::OnBeginLoad);
		NarrativeComponent->SharedNarrativeComp->OnSaveComplete.AddDynamic(this, &UNarrativeTestingWidget::OnSaveComplete);
		NarrativeComponent->SharedNarrativeComp->OnLoadComplete.AddDynamic(this, &UNarrativeTestingWidget::OnLoadComplete);
	}

	//Bind the dialogue. Don't bind shared component, dialogues should always be local
	if (NarrativeComponent)
	{
		NarrativeComponent->OnDialogueFinished.AddDynamic(this, &UNarrativeTestingWidget::OnDialogueFinished);
		NarrativeComponent->OnDialogueBegan.AddDynamic(this, &UNarrativeTestingWidget::OnDialogueBegan);
		NarrativeComponent->OnDialogueRepliesAvailable.AddDynamic(this, &UNarrativeTestingWidget::OnDialogueRepliesAvailable);
		NarrativeComponent->OnNPCDialogueLineStarted.AddDynamic(this, &UNarrativeTestingWidget::OnNPCDialogueLineStarted);
		NarrativeComponent->OnNPCDialogueLineFinished.AddDynamic(this, &UNarrativeTestingWidget::OnNPCDialogueLineFinished);
		NarrativeComponent->OnPlayerDialogueLineStarted.AddDynamic(this, &UNarrativeTestingWidget::OnPlayerDialogueLineStarted);
		NarrativeComponent->OnPlayerDialogueLineFinished.AddDynamic(this, &UNarrativeTestingWidget::OnPlayerDialogueLineFinished);

	}
}

void UNarrativeTestingWidget::OnQuestStarted(const UQuest* Quest)
{
	BPOnQuestStarted(Quest);
}

void UNarrativeTestingWidget::OnQuestFailed(const UQuest* Quest, const FText& QuestFailedMessage)
{
	BPOnQuestFailed(Quest, QuestFailedMessage);
}

void UNarrativeTestingWidget::OnQuestSucceeded(const UQuest* Quest, const FText& QuestSucceededMessage)
{
	BPOnQuestSucceeded(Quest, QuestSucceededMessage);
}

void UNarrativeTestingWidget::OnQuestNewState(UQuest* Quest, const UQuestState* NewState)
{
	BPOnQuestNewState(Quest, NewState);
}

void UNarrativeTestingWidget::OnQuestTaskProgressMade(const UQuest* Quest, const UNarrativeTask* Task, const class UQuestBranch* Step, int32 CurrentProgress, int32 RequiredProgress)
{
	BPOnQuestTaskProgressMade(Quest, Task, Step, CurrentProgress, RequiredProgress);
}

void UNarrativeTestingWidget::OnQuestTaskCompleted(const UQuest* Quest, const UNarrativeTask* Task, const class UQuestBranch* Step)
{
	BPOnQuestTaskCompleted(Quest, Task, Step);
}

void UNarrativeTestingWidget::OnQuestStepCompleted(const UQuest* Quest, const class UQuestBranch* Step)
{
	BPOnQuestStepCompleted(Quest, Step);
}

void UNarrativeTestingWidget::OnBeginSave(FString SaveName)
{
	BPOnBeginSave(SaveName);
}

void UNarrativeTestingWidget::OnBeginLoad(FString SaveName)
{
	BPOnBeginLoad(SaveName);
}

void UNarrativeTestingWidget::OnSaveComplete(FString SaveName)
{
	BPOnSaveComplete(SaveName);
}

void UNarrativeTestingWidget::OnLoadComplete(FString SaveName)
{
	BPOnLoadComplete(SaveName);
}

void UNarrativeTestingWidget::OnDialogueUpdated(class UDialogue* Dialogue, const TArray<UDialogueNode_NPC*>& NPCReplies, const TArray<UDialogueNode_Player*>& PlayerReplies)
{
	BPOnDialogueUpdated(Dialogue, NPCReplies, PlayerReplies);
}

void UNarrativeTestingWidget::OnDialogueBegan(class UDialogue* Dialogue)
{
	BPOnDialogueBegan(Dialogue);
}

void UNarrativeTestingWidget::OnDialogueFinished(class UDialogue* Dialogue)
{
	BPOnDialogueFinished(Dialogue);
}

void UNarrativeTestingWidget::OnDialogueRepliesAvailable(class UDialogue* Dialogue, const TArray<UDialogueNode_Player*>& PlayerReplies)
{
	BPOnDialogueRepliesAvailable(Dialogue, PlayerReplies);
}

void UNarrativeTestingWidget::OnNPCDialogueLineStarted(class UDialogue* Dialogue, class UDialogueNode_NPC* Node, const FDialogueLine& DialogueLine, const FSpeakerInfo& Speaker)
{
	BPOnNPCDialogueLineStarted(Dialogue, Node, DialogueLine, Speaker);
}

void UNarrativeTestingWidget::OnNPCDialogueLineFinished(class UDialogue* Dialogue, class UDialogueNode_NPC* Node, const FDialogueLine& DialogueLine, const FSpeakerInfo& Speaker)
{
	BPOnNPCDialogueLineFinished(Dialogue, Node, DialogueLine, Speaker);
}

void UNarrativeTestingWidget::OnPlayerDialogueLineStarted(class UDialogue* Dialogue, class UDialogueNode_Player* Node, const FDialogueLine& DialogueLine)
{
	BPOnPlayerDialogueLineStarted(Dialogue, Node, DialogueLine);
}

void UNarrativeTestingWidget::OnPlayerDialogueLineFinished(class UDialogue* Dialogue, class UDialogueNode_Player* Node, const FDialogueLine& DialogueLine)
{
	BPOnPlayerDialogueLineFinished(Dialogue, Node, DialogueLine);
}