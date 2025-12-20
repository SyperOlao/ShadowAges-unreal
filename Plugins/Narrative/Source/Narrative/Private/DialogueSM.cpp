// Copyright Narrative Tools 2022. 

#include "DialogueSM.h"
#include "Dialogue.h"
#include "NarrativeComponent.h"
#include "NarrativeCondition.h"
#include "Animation/AnimInstance.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "NarrativeDialogueSettings.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"

#define LOCTEXT_NAMESPACE "DialogueSM"

UDialogueNode::UDialogueNode()
{



}	

FDialogueLine UDialogueNode::GetRandomLine() const
{
	//Construct the line instead of adding it as a member as to not break dialogues made pre 2.2
	if (!AlternativeLines.Num())
	{
		return Line;
	}
	else
	{
		TArray<FDialogueLine> AllLines = AlternativeLines;
		AllLines.Add(Line);
		return AllLines[FMath::RandRange(0, AllLines.Num() - 1)];
	}
}

TArray<class UDialogueNode_NPC*> UDialogueNode::GetNPCReplies(APlayerController* OwningController, APawn* OwningPawn, class UNarrativeComponent* NarrativeComponent)
{
	TArray<class UDialogueNode_NPC*> ValidReplies;

	for (auto& NPCReply : NPCReplies)
	{
		if (NPCReply->AreConditionsMet(OwningPawn, OwningController, NarrativeComponent))
		{
			ValidReplies.Add(NPCReply);
		}
	}

	return ValidReplies;
}

TArray<class UDialogueNode_Player*> UDialogueNode::GetPlayerReplies(APlayerController* OwningController, APawn* OwningPawn, class UNarrativeComponent* NarrativeComponent)
{
	TArray<class UDialogueNode_Player*> ValidReplies;

	for (auto& PlayerReply : PlayerReplies)
	{
		if(PlayerReply && PlayerReply->AreConditionsMet(OwningPawn, OwningController, NarrativeComponent))
		{
			ValidReplies.Add(PlayerReply);
		}
	}

	//Sort the replies by their Y position in the graph
	ValidReplies.Sort([](const UDialogueNode_Player& NodeA, const UDialogueNode_Player& NodeB) {
		return  NodeA.NodePos.Y < NodeB.NodePos.Y;
		});

	return ValidReplies;
}

UWorld* UDialogueNode::GetWorld() const
{
	return OwningComponent ? OwningComponent->GetWorld() : nullptr;
}

const bool UDialogueNode::IsMissingCues() const
{
	if (!Line.Text.IsEmpty() && !Line.DialogueSound)
	{
		return true;
	}

	if (!AlternativeLines.Num())
	{
		return false;
	}

	for (auto& AltLine : AlternativeLines)
	{
		if (AltLine.Text.IsEmptyOrWhitespace() && !AltLine.DialogueSound)
		{
			return true;
		}
	}

	return false;
}

void UDialogueNode::ConvertLegacyNarrativeProps()
{
	if (!Text.IsEmpty())
	{
		/*A hack since FDialogueLine was added later and previously lines were just a bunch of seperate data members.
		So for any users with old dialogues who are upgrading to 3, we need to auto-add these*/
		Line.DialogueMontage = DialogueMontage;
		Line.DialogueSound = DialogueSound;
		Line.Text = Text;
		Line.Sequence = Sequence;
		Line.Shot = Shot;
		

		Text = FText::GetEmpty();
	}
}

#if WITH_EDITOR

void UDialogueNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty)
	{ 
		//If we changed the ID, make sure it doesn't conflict with any other IDs in the Dialogue
		if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UDialogueNode, Line))
		{
			GenerateIDFromText();
		}
	}
}

void UDialogueNode::EnsureUniqueID()
{
	if (OwningDialogue)
	{
		TArray<UDialogueNode*> DialogueNodes = OwningDialogue->GetNodes();
		TArray<FName> NodeIDs;

		for (auto& Node : DialogueNodes)
		{
			if (Node && Node != this)
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

void UDialogueNode::GenerateIDFromText()
{
	//When the text for this node is entered, give the node a sensible ID: {SpeakerID}_{FirstFourWords}
	FString TextString = Line.Text.ToString();

	TArray<FString> ContentArray;
	FString ContentString = "";
	TextString.ParseIntoArrayWS(ContentArray);
	ContentArray.SetNum(4);

	for (auto& Content : ContentArray)
	{
		if (Content.Len() > 0)
		{
			Content[0] = FChar::ToUpper(Content[0]);
			ContentString += Content;
		}
	}

	FString Prefix = "";

	if (UDialogueNode_NPC* NPCNode = Cast<UDialogueNode_NPC>(this))
	{
		Prefix = NPCNode->SpeakerID.ToString();
	}
	else
	{
		Prefix = "Player";
	}

	//Remove special chars and numeric 
	FString FinalString = "";

	for (TCHAR& Char : ContentString)
	{
		if (FChar::IsAlpha(Char))
		{
			FinalString += Char;
		}
	}

	SetID(FName(Prefix + '_' + FinalString));
}

#endif


TArray<class UDialogueNode_NPC*> UDialogueNode_NPC::GetReplyChain(APlayerController* OwningController, APawn* OwningPawn, class UNarrativeComponent* NarrativeComponent)
{
	TArray<UDialogueNode_NPC*> NPCFollowUpReplies;
	UDialogueNode_NPC* CurrentNode = this;

	NPCFollowUpReplies.Add(CurrentNode);

	while (CurrentNode)
	{
		if (CurrentNode != this)
		{
			NPCFollowUpReplies.Add(CurrentNode);
		}

		TArray<UDialogueNode_NPC*> NPCRepliesToRet = CurrentNode->NPCReplies;

		//Need to process the conditions using higher nodes first 
		NPCRepliesToRet.Sort([](const UDialogueNode_NPC& NodeA, const UDialogueNode_NPC& NodeB) {
			return  NodeA.NodePos.Y < NodeB.NodePos.Y;
			});

		//If we don't find another node after this the loop will exit
		CurrentNode = nullptr;

		//Find the next valid reply. We'll then repeat this cycle until we run out
		for (auto& Reply : NPCRepliesToRet)
		{
			if (Reply != this && Reply->AreConditionsMet(OwningPawn, OwningController, NarrativeComponent))
			{
				CurrentNode = Reply;
				break; // just use the first reply with valid conditions
			}
		}
	}

	return NPCFollowUpReplies;
}

FText UDialogueNode_Player::GetOptionText(class UDialogue* InDialogue) const
{
	FText TextToUse = OptionText.IsEmptyOrWhitespace() ? Line.Text : OptionText;

	if (InDialogue)
	{
		InDialogue->ReplaceStringVariables(TextToUse);
	}

	return TextToUse;
}

#undef LOCTEXT_NAMESPACE