// Copyright Narrative Tools 2022. 

#include "Dialogue.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "NarrativeComponent.h"
#include "DialogueBlueprintGeneratedClass.h"
#include "DialogueSM.h"
#include "Animation/AnimInstance.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "NarrativeDialogueSettings.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraShakeBase.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "NarrativeDefaultCinecam.h"
#include "NarrativeDialogueShot.h"
#include "../DialogueShots/DS_Speaker.h"
#include "Sound/SoundBase.h"
#include <Camera/CameraShakeBase.h>
#include <EngineUtils.h>

static const FName NAME_PlayerSpeakerID("Player");
static const FString NAME_PlayDialogueNodeTask("PlayDialogueNode");

UDialogue::UDialogue()
{
	bSpawnDialogueCamera = true;
	DialogueCameraClass = ANarrativeDefaultCinecam::StaticClass();

	auto ShakeFinder = ConstructorHelpers::FClassFinder<UCameraShakeBase>(TEXT("Blueprint'/Narrative/Misc/BP_HandheldCameraShake.BP_HandheldCameraShake_C'"));
	if (ShakeFinder.Succeeded())
	{
		DialogueCameraShake = ShakeFinder.Class;
	}
	
	//Add a speaker shot by default so dialogues people make are pretty :) 
	DialogueShots.Add(CreateDefaultSubobject<UDS_Speaker>(TEXT("DefaultShot")));

	bAutoRotateSpeakers = false;
	bAutoStopMovement = true;
	bCanBeExited = true;
	bFreeMovement = false;
	bDeinitialized = false;

	//Not sure if this is needed but really want to avoid RF_Public assert issues people were having with UE5
	SetFlags(RF_Public);
}

UWorld* UDialogue::GetWorld() const
{

	if (OwningComp)
	{
		return OwningComp->GetWorld();
	}

	return nullptr;
}

bool UDialogue::Initialize(class UNarrativeComponent* InitializingComp, class AActor* NPC, FName StartFromID)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		//We need a valid narrative component to make a quest for 
		if (!InitializingComp)
		{
			return false;
		}

		if (UDialogueBlueprintGeneratedClass* BGClass = Cast<UDialogueBlueprintGeneratedClass>(GetClass()))
		{
			BGClass->InitializeDialogue(this);

			//If a dialogue doesn't have any npc replies, or doesn't have a valid root dialogue something has gone wrong 
			if (NPCReplies.Num() == 0 || !RootDialogue)
			{
				UE_LOG(LogNarrative, Warning, TEXT("UDialogue::Initialize was given a dialogue with an invalid RootDialogue, or no NPC replies."));
				return false;
			}

			UDialogueNode_NPC* StartDialogue = StartFromID.IsNone() ? RootDialogue : GetNPCReplyByID(StartFromID);

			if (!StartDialogue && !StartFromID.IsNone())
			{
				UE_LOG(LogNarrative, Warning, TEXT("UDialogue::Initialize could not find Start node with StartFromID: %s. Falling back to root node."), *StartFromID.ToString());
				StartDialogue = RootDialogue;
			}

			//Initialize all the data required to begin the dialogue 
			if (StartDialogue)
			{
				NPCActor = NPC;
				OwningComp = InitializingComp;
				OwningController = OwningComp->GetOwningController();
				OwningPawn = OwningComp->GetOwningPawn();

				if (RootDialogue)
				{
					RootDialogue->OwningDialogue = this;
				}

				for (auto& Reply : NPCReplies)
				{
					if (Reply)
					{
						Reply->OwningDialogue = this;
						Reply->OwningComponent = OwningComp;
					}
				}

				for (auto& Reply : PlayerReplies)
				{
					if (Reply)
					{
						Reply->OwningDialogue = this;
						Reply->OwningComponent = OwningComp;
					}
				}

				//Generate the first chunk of dialogue 
				if (OwningComp->HasAuthority())
				{
					const bool bHasValidDialogue = GenerateDialogueChunk(StartDialogue);

					if (!bHasValidDialogue)
					{
						UE_LOG(LogNarrative, Verbose, TEXT("UDialogue::Initialize attempted to begin the dialogue, but there was no valid dialogue to play. "));
						return false;
					}
				}

				OnBeginDialogue();
				return true;
			}
		}
	}

	return false;
}

void UDialogue::Deinitialize()
{
	bDeinitialized = true;

	OnEndDialogue();

	StopDialogueSequence();

	if (DialogueAudio)
	{
		DialogueAudio->Stop();
		DialogueAudio->DestroyComponent();
	}

	if (DialogueSequencePlayer)
	{
		DialogueSequencePlayer->Destroy();
	}

	OwningComp = nullptr; 

	NPCActor = nullptr;

	NPCReplies.Empty();
	PlayerReplies.Empty();

	if (RootDialogue)
	{
		RootDialogue->OwningComponent = nullptr;
	}

	for (auto& Reply : NPCReplies)
	{
		if (Reply)
		{
			Reply->OwningComponent = nullptr;
		}
	}

	for (auto& Reply : PlayerReplies)
	{
		if (Reply)
		{
			Reply->OwningComponent = nullptr;
		}
	}
}

void UDialogue::DuplicateAndInitializeFromDialogue(UDialogue* DialogueTemplate)
{
	if (DialogueTemplate)
	{
		//Duplicate the quest template, then steal all its states and branches - TODO this seems unreliable, what if we add new fields to UDialogue? Look into swapping object entirely instead of stealing fields
		UDialogue* NewDialogue = Cast<UDialogue>(StaticDuplicateObject(DialogueTemplate, this, NAME_None, RF_Transactional));
		NewDialogue->SetFlags(RF_Transient | RF_DuplicateTransient);

		RootDialogue = NewDialogue->RootDialogue;
		NPCReplies = NewDialogue->NPCReplies;
		PlayerReplies = NewDialogue->PlayerReplies;
	}
}

#if WITH_EDITOR
void UDialogue::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{	
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PlayerSpeakerInfo.SpeakerID != NAME_PlayerSpeakerID)
	{
		PlayerSpeakerInfo.SpeakerID = NAME_PlayerSpeakerID;
	}

	//If a designer clears the speakers always ensure at least one is added 
	if (Speakers.Num() == 0)
	{
		FSpeakerInfo DefaultSpeaker;
		DefaultSpeaker.SpeakerID = GetFName();
		Speakers.Add(DefaultSpeaker);
	}

	//If any NPC replies don't have a valid speaker set to the first speaker
	for (auto& Node : NPCReplies)
	{
		if (Node)
		{
			bool bSpeakerNotFound = true;

			for (auto& Speaker : Speakers)
			{
				if (Speaker.SpeakerID == Node->SpeakerID)
				{
					bSpeakerNotFound = false;
				}
			}

			if (bSpeakerNotFound)
			{
				Node->SpeakerID = Speakers[0].SpeakerID;
			}
		}
	}
}

void UDialogue::PreEditChange(FEditPropertyChain& PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);


}

#endif

FSpeakerInfo UDialogue::GetSpeaker(const FName& SpeakerID)
{
	for (auto& Speaker : Speakers)
	{
		if (Speaker.SpeakerID == SpeakerID)
		{
			return Speaker;
		}
	}

	//Nodes created before the release of Speakers won't have their speaker set. Therefore, just return the first speaker.
	if (Speakers.Num() && Speakers.IsValidIndex(0))
	{
		return Speakers[0];
	}

	return FSpeakerInfo();
}

void UDialogue::SkipCurrentLine()
{
	//Only allow skipping lines in standalone 
	if (OwningComp && OwningComp->GetNetMode() == NM_Standalone)
	{
		//Skip whatever dialogue line is currently playing
		if (CurrentNode)
		{
			if (CurrentNode->IsA<UDialogueNode_NPC>())
			{
				GetWorld()->GetTimerManager().ClearTimer(TimerHandle_NPCReplyFinished);
				FinishNPCDialogue();
			}
			else
			{
				GetWorld()->GetTimerManager().ClearTimer(TimerHandle_PlayerReplyFinished);
				FinishPlayerDialogue();
			}
		}
	}
}

void UDialogue::SelectDialogueOption(UDialogueNode_Player* Option)
{
	//Validate that the option that was selected is actually one of the available options
	if (AvailableResponses.Contains(Option))// GenerateDialogueChunk() already did this && Option->AreConditionsMet(OwningPawn, OwningController, OwningComp))
	{
		PlayPlayerDialogueNode(Option);
	}
}

bool UDialogue::HasValidChunk() const
{
	//Validate that the reply chain isn't just full of empty routing nodes and actually has some content to play
	bool bReplyChainHasContent = false;

	for (auto& Reply : NPCReplyChain)
	{
		if (Reply && !Reply->IsRoutingNode())
		{
			bReplyChainHasContent = true;
			break;
		}
	}

	//Chunk is valid if there is something for the player to say, or something for the NPC to say that isn't empty (empty nodes can be used for routing)
	//At this point we have a valid chunk and Play() can be called on this dialogue to play it
	if (AvailableResponses.Num() || bReplyChainHasContent)
	{
		return true;
	}

	return false;
}

bool UDialogue::GenerateDialogueChunk(UDialogueNode_NPC* NPCNode)
{
	if (NPCNode && OwningComp && OwningComp->HasAuthority())
	{
		//Generate the NPC reply chain
		NPCReplyChain = NPCNode->GetReplyChain(OwningController, OwningPawn, OwningComp);

		//Grab all the players responses to the last thing the NPC had to say
		if (NPCReplyChain.Num() && NPCReplyChain.IsValidIndex(NPCReplyChain.Num() - 1))
		{
			if (UDialogueNode_NPC* LastNPCNode = NPCReplyChain.Last())
			{
				AvailableResponses = LastNPCNode->GetPlayerReplies(OwningController, OwningPawn, OwningComp);
			}
		}

		//Did we generate a valid chunk?
		if (HasValidChunk())
		{
			return true;
		}
	}

	return false;
}

void UDialogue::ClientReceiveDialogueChunk(const TArray<FName>& NPCReplyIDs, const TArray<FName>& PlayerReplyIDs)
{	
	if (OwningComp && !OwningComp->HasAuthority())
	{
		//Resolve the nodes the server sent us 
		NPCReplyChain = GetNPCRepliesByIDs(NPCReplyIDs);
		AvailableResponses = GetPlayerRepliesByIDs(PlayerReplyIDs);

		//Server ensures chunks are valid before sending them to us. If they aren't something has gone very wrong
		check(HasValidChunk());

		Play();
	}
}

void UDialogue::Play()
{
	//FString RoleString = OwningComp && OwningComp->HasAuthority() ? "Server" : "Client";
	//UE_LOG(LogNarrative, Warning, TEXT("Playing %d nodes on %s"), NPCReplyChain.Num(), *RoleString);
	//Start playing through the NPCs replies until we run out
	if (NPCReplyChain.Num())
	{
		PlayNextNPCReply();
	}
}

void UDialogue::ExitDialogue()
{
	if (OwningComp)
	{
		OwningComp->ExitDialogue();
	}
}

void UDialogue::TickDialogue_Implementation(const float DeltaTime)
{
	if (DialogueCamera)
	{

	}
}

void UDialogue::OnBeginDialogue()
{
	K2_OnBeginDialogue();


	/*It doesnt really make sense for the conversation participants to not face each other, so do this automagically (can be turned off) */
	if (NPCActor && OwningPawn)
	{
		//bAutoRotate speakers may rotate our pawn/npcactor so cache these so we can reset when dialogue ends
		OldNPCRot = NPCActor->GetActorRotation();
		OldPlayerRot = OwningPawn->GetActorRotation();

		//Keep bAutoRotateSpeakers legacy functionality, where bAutoRotateSpeakers makes OwningPawn and NPCActor face each other 
		if (bAutoRotateSpeakers)
		{
			FVector Offset = (NPCActor->GetActorLocation() - OwningPawn->GetActorLocation()).GetSafeNormal();

			//Generally characters need to be kept upright, so just effect yaw
			Offset.Z = 0.f;

			NPCActor->SetActorRotation((-Offset).Rotation());
		}

		if (bAutoStopMovement)
		{
			//The camera will line up its shot, but if the character/AI is still moving the shot won't be lined up, so stop the characters from moving
			if (ACharacter* NPCChar = Cast<ACharacter>(NPCActor))
			{
				NPCChar->GetCharacterMovement()->StopMovementImmediately();
			}

			if (ACharacter* PlayerChar = Cast<ACharacter>(OwningPawn))
			{
				PlayerChar->GetCharacterMovement()->StopMovementImmediately();
			}
		}
	}

	if (OwningController && OwningController->IsLocalPlayerController())
	{
		InitSpeakerAvatars();

		if (bSpawnDialogueCamera && DialogueCameraClass)
		{
			DialogueCamera = GetWorld()->SpawnActor<ACameraActor>(DialogueCameraClass, FTransform());

			OldViewTarget = OwningController->GetViewTarget();

			OwningController->SetViewTargetWithBlend(DialogueCamera, DialogueCameraBlendTime, VTBlend_Cubic);

			if (DialogueCameraShake)
			{
				OwningController->ClientStartCameraShake(DialogueCameraShake);
			}
		}
	}
}

void UDialogue::InitSpeakerAvatars()
{
	//Spawn any speaker avatars in - just spawn these locally, never seems like we'd want the server
	//to spawn replicated speaker actors for something that is just a visual for a dialogue 
	for (auto& Speaker : Speakers)
	{
		if (AActor* SpeakerActor = LinkSpeakerAvatar(Speaker))
		{
			//Track spawned avatars
			SpeakerAvatars.Add(Speaker.SpeakerID, SpeakerActor);
			Speaker.SpeakerAvatarTransform = SpeakerActor->GetActorTransform(); 
		}
		else if(NPCActor)// fall back to the NPC avatar if FindSpeakerAvatar failed 
		{
			SpeakerAvatars.Add(Speaker.SpeakerID, NPCActor);
			Speaker.SpeakerAvatarTransform = NPCActor->GetActorTransform();
		}
		else
		{
			UE_LOG(LogNarrative, Warning, 
			TEXT("Narrative wasn't able to find the avatar for %s, as a SpeakerAvatarClass wasn't set, no actors with tag %s were found, and DefaultNPCAvatar was invalid."),
			*Speaker.SpeakerID.ToString(), *Speaker.SpeakerID.ToString());
		}
	}

	//Spawn the players speaker avatar in, or just use the players pawn as their avatar if one isn't set
	if (AActor* SpeakerActor = LinkSpeakerAvatar(PlayerSpeakerInfo))
	{
		SpeakerAvatars.Add(PlayerSpeakerInfo.SpeakerID, SpeakerActor);
		PlayerSpeakerInfo.SpeakerAvatarTransform = SpeakerActor->GetActorTransform();

		//By default if the player has a speaker avatar in the world we'll hide their pawn
		if (OwningPawn)
		{
			OwningPawn->SetActorHiddenInGame(true);
		}
	}
	else if(OwningPawn)
	{
		SpeakerAvatars.Add(PlayerSpeakerInfo.SpeakerID, OwningPawn);
		PlayerSpeakerInfo.SpeakerAvatarTransform = OwningPawn->GetActorTransform();
	}
	else
	{
		UE_LOG(LogNarrative, Warning, TEXT("Narrative wasn't able to find the avatar for the player, as a SpeakerAvatarClass wasn't set, no actors with tag 'Player' were found, and OwningPawn was invalid."));
	}

}

void UDialogue::MakeSpeakersFaceTarget(const AActor* Target)
{
	for (auto& SpeakerActorKVP : SpeakerAvatars)
	{
		if (AActor* Avatar = SpeakerActorKVP.Value)
		{
			if (Avatar != Target)
			{
				FVector Offset = (Avatar->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal();

				//Generally characters need to be kept upright, so just effect yaw
				Offset.Z = 0.f;

				Avatar->SetActorRotation((-Offset).Rotation());
			}
		}
	}

}

void UDialogue::ProcessNodeEvents(class UDialogueNode* Node, bool bStartEvents)
{
	if (Node)
	{
		if (bPlayOnlyStartNodeEvent && !bStartEvents)
		{
			return;
		}

		struct SOnPlayedStruct
		{
			UDialogueNode* Node;
			bool bStarted;
		};

		SOnPlayedStruct Parms;
		Parms.Node = Node;
		Parms.bStarted = bStartEvents;

		if (UFunction* Func = FindFunction(Node->OnPlayNodeFuncName))
		{
			ProcessEvent(Func, &Parms);
		}

		Node->ProcessEvents(OwningPawn, OwningController, OwningComp, bStartEvents ? EEventRuntime::Start : EEventRuntime::End);
	}
}


void UDialogue::OnEndDialogue()
{
	K2_OnEndDialogue();

	if (NPCActor && OwningPawn)
	{
		if (bAutoRotateSpeakers)
		{
			NPCActor->SetActorRotation(OldNPCRot);
			OwningPawn->SetActorRotation(OldPlayerRot);
		}

		NPCActor->SetActorHiddenInGame(false);
		OwningPawn->SetActorHiddenInGame(false);
	}

	//Clean up any spawned actors
	for (auto& Speaker : Speakers)
	{
		if (AActor* SpeakerAvatar = GetSpeakerAvatar(Speaker.SpeakerID))
		{
			DestroySpeakerAvatar(Speaker, SpeakerAvatar);
		}
	}

	if (AActor* PlayerAvatar = GetPlayerAvatar())
	{
		DestroySpeakerAvatar(PlayerSpeakerInfo, PlayerAvatar);
	}

	SpeakerAvatars.Empty();

	if (DialogueCameraShake && OwningController)
	{
		OwningController->ClientStopCameraShake(DialogueCameraShake);
	}

	if (DialogueCamera)
	{
		//dont destroy dialogue camera immediately, SetViewTargetWithBlend needs it alive to blend back to the player camera nicely.
		if (DialogueCameraBlendTime > 0.f)
		{
			DialogueCamera->SetLifeSpan(DialogueCameraBlendTime);
		}
		else
		{
			DialogueCamera->Destroy();
		}
	}

	if (OldViewTarget)
	{
		if (OwningController)
		{
			OwningController->SetViewTargetWithBlend(OldViewTarget, DialogueCameraBlendTime, VTBlend_Cubic);
		}
	}
	else
	{
		UE_LOG(LogNarrative, Warning, TEXT("Narrative tried setting the camera back to your old view target, but your view target is no longer valid. (Did your player die during dialogue?)"));
	}
}

void UDialogue::NPCFinishedTalking()
{
	//Ensure that a narrative event etc hasn't started a new dialogue
	if (IsInitialized() && AvailableResponses.Num() && OwningComp && OwningComp->CurrentDialogue == this)
	{
		//If a response is autoselect, select it and early out 
		for (auto& AvailableResponse : AvailableResponses)
		{
			if(AvailableResponse && AvailableResponse->IsAutoSelect())
			{
				OwningComp->SelectDialogueOption(AvailableResponse);
				return;
			}
		}

		AActor* const PlayerAvatar = GetPlayerAvatar();
		AActor* ListeningActor = NPCActor;

		/**
		* Things like Over The Shoulder shots require a listener and a speaker to line up the shot, but since we're selecting a reply, the 
		* listener will depend on 
		*/
		if (SpeakerAvatars.Contains(CurrentSpeaker.SpeakerID))
		{
			ListeningActor = SpeakerAvatars[CurrentSpeaker.SpeakerID];
		}

		/*
		* Order of precendence for what camera shot to choose :
		*
		* 1. Player speaker info has a sequence defined
		* 2. Player speaker info has a shot defined
		* 3. Dialogue has any shots added to its DialogueShots
		*/
		if (PlayerSpeakerInfo.SelectingReplySequence.SequenceAsset)
		{
			PlayDialogueSequence(PlayerSpeakerInfo.SelectingReplySequence);
		}
		else if (PlayerSpeakerInfo.SelectingReplyShot && DialogueCamera)
		{
			PlayDialogueShot(PlayerSpeakerInfo.SelectingReplyShot, PlayerAvatar, ListeningActor);
		}
		else if (DialogueShots.Num())
		{
			PlayDialogueShot(DialogueShots[FMath::RandRange(0, DialogueShots.Num() - 1)], PlayerAvatar, ListeningActor);
		}

		//Make speakers face the player
		if (bAutoRotateSpeakers)
		{
			MakeSpeakersFaceTarget(PlayerAvatar);
		}

		//NPC has finished talking. Let UI know it can show the player replies.
		OwningComp->OnDialogueRepliesAvailable.Broadcast(this, AvailableResponses);

		//Also make sure we stop playing any dialogue audio that was previously playing
		if (DialogueAudio)
		{
			DialogueAudio->Stop();
			DialogueAudio->DestroyComponent();
		}
	}
	else
	{
		//There were no replies for the player, end the dialogue 
		ExitDialogue();
	}
}

void UDialogue::PlayNPCDialogueNode(class UDialogueNode_NPC* NPCReply)
{
	check(OwningComp && NPCReply);

	//FString RoleString = OwningComp && OwningComp->HasAuthority() ? "Server" : "Client";
	//UE_LOG(LogNarrative, Warning, TEXT("PlayNPCDialogueNode called on %s with node %s"), *RoleString, *GetNameSafe(NPCReply));

	if (NPCReply)
	{
		CurrentNode = NPCReply;
		CurrentLine = NPCReply->GetRandomLine();
		ReplaceStringVariables(CurrentLine.Text);

		CurrentSpeaker = GetSpeaker(NPCReply->SpeakerID);

		ProcessNodeEvents(NPCReply, true);

		//If a node has no text, just finish it, firing its events 
		if (NPCReply->Line.Text.IsEmptyOrWhitespace())
		{
			FinishNPCDialogue();
			return;
		}

		//Actual playing of the node is inside a BlueprintNativeEvent so designers can override how NPC dialogues are played 
		PlayNPCDialogue(NPCReply, CurrentLine, CurrentSpeaker);

		if (OwningComp)
		{
			//Call delegates and BPNativeEvents
			OwningComp->OnNPCDialogueLineStarted.Broadcast(this, NPCReply, CurrentLine, CurrentSpeaker);
		}

		OnNPCDialogueLineStarted(NPCReply, CurrentLine, CurrentSpeaker);

		const float Duration = GetLineDuration(CurrentNode, CurrentLine);

		if (Duration > 0.01f && GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(TimerHandle_NPCReplyFinished);
			//Give the reply time to play, then play the next one! 
			GetWorld()->GetTimerManager().SetTimer(TimerHandle_NPCReplyFinished, this, &UDialogue::FinishNPCDialogue, Duration, false);
		}
		else
		{
			FinishNPCDialogue();
		}
	}
	else 
	{
		//Somehow we were given a null NPC reply to play, just try play the next one
		PlayNextNPCReply();
	}
}

void UDialogue::PlayPlayerDialogueNode(class UDialogueNode_Player* PlayerReply)
{
	//NPC replies should be fully gone before we play a player response
	check(!NPCReplyChain.Num());
	check(OwningComp && PlayerReply);

	if (PlayerReply)
	{
		CurrentNode = PlayerReply;
		
		ProcessNodeEvents(PlayerReply, true);

		//If a node has no text, just process events then go to the next line 
		if (PlayerReply->Line.Text.IsEmptyOrWhitespace())
		{
			FinishPlayerDialogue();
			return;
		}

		CurrentLine = PlayerReply->GetRandomLine();
		ReplaceStringVariables(CurrentLine.Text);

		if (OwningComp)
		{
			//Call delegates and BPNativeEvents
			OwningComp->OnPlayerDialogueLineStarted.Broadcast(this, PlayerReply, CurrentLine);
		}

		OnPlayerDialogueLineStarted(PlayerReply, CurrentLine);

		//Actual playing of the node is inside a BlueprintNativeEvent so designers can override how NPC dialogues are played 
		PlayPlayerDialogue(PlayerReply, CurrentLine);

		CurrentSpeaker = PlayerSpeakerInfo;

		const float Duration = GetLineDuration(CurrentNode, CurrentLine);
		if (Duration > 0.01f && GetWorld())
		{
			//Give the reply time to play, then play the next one! 
			GetWorld()->GetTimerManager().SetTimer(TimerHandle_PlayerReplyFinished, this, &UDialogue::FinishPlayerDialogue, Duration, false);
		}
		else
		{
			FinishPlayerDialogue();
		}
	}
}

void UDialogue::ReplaceStringVariables(FText& Line)
{
	//Replace variables in dialogue line
	FString LineString = Line.ToString();

	int32 OpenBraceIdx = -1;
	int32 CloseBraceIdx = -1;
	bool bFoundOpenBrace = LineString.FindChar('{', OpenBraceIdx);
	bool bFoundCloseBrace = LineString.FindChar('}', CloseBraceIdx);
	uint32 Iters = 0; // More than 50 wildcard replaces and something has probably gone wrong, so safeguard against that

	while (bFoundOpenBrace && bFoundCloseBrace && OpenBraceIdx < CloseBraceIdx && Iters < 50)
	{
		const FString VariableName = LineString.Mid(OpenBraceIdx + 1, CloseBraceIdx - OpenBraceIdx - 1);
		const FString VariableVal = GetStringVariable(CurrentNode, CurrentLine, VariableName);

		if (!VariableVal.IsEmpty())
		{
			LineString.RemoveAt(OpenBraceIdx, CloseBraceIdx - OpenBraceIdx + 1);
			LineString.InsertAt(OpenBraceIdx, VariableVal);
		}

		bFoundOpenBrace = LineString.FindChar('{', OpenBraceIdx);
		bFoundCloseBrace = LineString.FindChar('}', CloseBraceIdx);

		Iters++;
	}

	if (Iters > 0)
	{
		Line = FText::FromString(LineString);
	}
}

AActor* UDialogue::GetPlayerAvatar() const
{
	if (SpeakerAvatars.Contains(PlayerSpeakerInfo.SpeakerID))
	{
		return SpeakerAvatars[PlayerSpeakerInfo.SpeakerID];
	}
	else
	{
		return OwningPawn;
	}
}

AActor* UDialogue::GetSpeakerAvatar(const FName& SpeakerID) const
{
	if (SpeakerAvatars.Contains(SpeakerID))
	{
		return SpeakerAvatars[SpeakerID];
	}

	return nullptr;
}

FVector UDialogue::GetSpeakerHeadLocation_Implementation(class AActor* Actor)
{
	if (!Actor)
	{
		return FVector::ZeroVector;
	}
	
	FVector EyesLoc;
	FRotator EyesRot;
	Actor->GetActorEyesViewPoint(EyesLoc, EyesRot);

	const bool bIsChar = Actor->IsA<ACharacter>();

	/*Chars should use GetActorEyesViewPoint, because grabbing the location of the head bone is problematic and should be avoided
	if possible. This is because:
	
	1. A lot of character skeletons might not have a bone named "head", it might be called something else (confusing for users)
	2. The head bone moves each frame with the animation, meaning the shot will be placed slightly differently every time, 
	whereas GetActorEyesViewPoint doesn't have this problem. 
	*/
	if (!bIsChar)
	{
		//If we absolutely have to use the head bone, just use the Z, that way at least the shot won't move as much each time
		if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Actor->GetComponentByClass(USkeletalMeshComponent::StaticClass())))
		{
			EyesLoc.Z = SkelMesh->GetBoneLocation("head").Z;
		}
	}

	return EyesLoc;
}

void UDialogue::PlayNextNPCReply()
{
	//Keep going through the NPC replies until we run out
	if (NPCReplyChain.IsValidIndex(0) && IsInitialized())
	{
		UDialogueNode_NPC* NPCNode = NPCReplyChain[0];
		NPCReplyChain.Remove(NPCNode);
		PlayNPCDialogueNode(NPCNode);
	}
	else //NPC has nothing left to say 
	{
		NPCFinishedTalking();
	}
}

void UDialogue::FinishNPCDialogue()
{
	//FString RoleString = OwningComp && OwningComp->HasAuthority() ? "Server" : "Client";
	//UE_LOG(LogNarrative, Warning, TEXT("FinishNPCDialogue called on %s with node %s"), *RoleString, *GetNameSafe(CurrentNode));

	if (UDialogueNode_NPC* NPCNode = Cast<UDialogueNode_NPC>(CurrentNode))
	{
		if (OwningComp)
		{
			OwningComp->CompleteNarrativeDataTask(NAME_PlayDialogueNodeTask, NPCNode->GetID().ToString());

			ProcessNodeEvents(NPCNode, false);

			//We need to re-check OwningComp validity, as ProcessEvents may have ended this dialogue
			if (OwningComp)
			{
				//Call delegates and BPNativeEvents
				OwningComp->OnNPCDialogueLineFinished.Broadcast(this, NPCNode, CurrentLine, CurrentSpeaker);
				OnNPCDialogueLineFinished(NPCNode, CurrentLine, CurrentSpeaker);

				PlayNextNPCReply();
			}
		}
	}
}

void UDialogue::FinishPlayerDialogue()
{
	//FString RoleString = OwningComp && OwningComp->HasAuthority() ? "Server" : "Client";
	//UE_LOG(LogNarrative, Warning, TEXT("FinishPlayerDialogue called on %s with node %s"), *RoleString, *GetNameSafe(CurrentNode));
	//Players dialogue node has finished, generate the next chunk of dialogue! 
	if (UDialogueNode_Player* PlayerNode = Cast<UDialogueNode_Player>(CurrentNode))
	{
		if (!OwningComp || !PlayerNode)
		{
			UE_LOG(LogNarrative, Verbose, TEXT("UDialogue::PlayerDialogueNodeFinished was called but had a null OwningComp or PlayerNode. "));
			return;
		}

		//Call delegates and BPNativeEvents
		OwningComp->OnPlayerDialogueLineFinished.Broadcast(this, PlayerNode, CurrentLine);
		OnPlayerDialogueLineFinished(PlayerNode, CurrentLine);

		//No need, generate dialogue chunk already did this: if (PlayerNode->AreConditionsMet(OwningPawn, OwningController, OwningComp))
		{
			//Both auth and local need to run the events
			ProcessNodeEvents(PlayerNode, false);

			if (OwningComp && OwningComp->HasAuthority())
			{
				OwningComp->CompleteNarrativeDataTask(NAME_PlayDialogueNodeTask, PlayerNode->GetID().ToString());

				//Player selected a reply with nothing leading off it, dialogue has ended 
				if (PlayerNode->NPCReplies.Num() <= 0)
				{
					ExitDialogue();
					return;
				}

				//Find the first valid NPC reply after the option we selected. TODO: Use NPC replies Y-pos in the graph to prioritize order of check
				UDialogueNode_NPC* NextReply = nullptr;

				for (auto& NextNPCReply : PlayerNode->NPCReplies)
				{
					if (NextNPCReply->AreConditionsMet(OwningPawn, OwningController, OwningComp))
					{
						NextReply = NextNPCReply;
						break;
					}
				}

				//If we can generate more dialogue from the reply that was selected, do so, otherwise exit dialogue 
				if (GenerateDialogueChunk(NextReply))
				{
					//RPC the dialogue chunk to the client so it can play it
					OwningComp->ClientRecieveDialogueChunk(MakeIDsFromNPCNodes(NPCReplyChain), MakeIDsFromPlayerNodes(AvailableResponses));

					Play();
				}
				else
				{
					UE_LOG(LogNarrative, Warning, TEXT("No more chunks generated from response. Ending dialogue! "));
					ExitDialogue();
				}
			}
		}

	}
}

UDialogueNode_NPC* UDialogue::GetNPCReplyByID(const FName& ID) const
{
	for (auto& NPCReply : NPCReplies)
	{
		if (NPCReply->GetID() == ID)
		{
			return NPCReply;
		}
	}
	return nullptr;
}

UDialogueNode_Player* UDialogue::GetPlayerReplyByID(const FName& ID) const
{
	for (auto& PlayerReply : PlayerReplies)
	{
		if (PlayerReply->GetID() == ID)
		{
			return PlayerReply;
		}
	}
	return nullptr;
}

TArray<UDialogueNode_NPC*> UDialogue::GetNPCRepliesByIDs(const TArray<FName>& IDs) const
{
	TArray<UDialogueNode_NPC*> Replies;

	for (auto& ID : IDs)
	{
		for (auto& Reply : NPCReplies)
		{
			if (Reply && Reply->GetID() == ID)
			{
				Replies.Add(Reply);
				break;
			}
		}
	}

	return Replies;
}

TArray <UDialogueNode_Player*> UDialogue::GetPlayerRepliesByIDs(const TArray<FName>& IDs) const
{
	TArray<UDialogueNode_Player*> Replies;

	for (auto& ID : IDs)
	{
		for (auto& Reply : PlayerReplies)
		{
			if (Reply && Reply->GetID() == ID)
			{
				Replies.Add(Reply);
				break;
			}
		}
	}

	return Replies;
}

TArray<FName> UDialogue::MakeIDsFromNPCNodes(const TArray<UDialogueNode_NPC*> Nodes) const
{
	TArray<FName> IDs;

	for (auto& Node : Nodes)
	{
		IDs.Add(Node->GetID());
	}

	return IDs;
}

TArray<FName> UDialogue::MakeIDsFromPlayerNodes(const TArray<UDialogueNode_Player*> Nodes) const
{
	TArray<FName> IDs;

	for (auto& Node : Nodes)
	{
		IDs.Add(Node->GetID());
	}

	return IDs;
}

TArray<UDialogueNode*> UDialogue::GetNodes() const
{
	TArray<UDialogueNode*> Ret;

	for (auto& NPCReply : NPCReplies)
	{
		Ret.Add(NPCReply);
	}

	for (auto& PlayerReply : PlayerReplies)
	{
		Ret.Add(PlayerReply);
	}

	return Ret;
}

void UDialogue::PlayDialogueShot(class UNarrativeDialogueShot* Shot, class AActor* Speaker, class AActor* Listener)
{
	//A shot starting should override any sequence that had started playing as a result of another line
	StopDialogueSequence();

	if (DialogueCamera && Shot)
	{
		const FTransform Transform = Shot->GetShotTransform(this, Speaker, Listener);

		//TODO implement shot blending 
		//if (Shot->ShotBlendTime <= SMALL_NUMBER)
		{
			DialogueCamera->SetActorTransform(Transform);
		}

		//If the dialogue camera is a cinecam, focus the camera on the speakers face and generate a nice blur in the bg/fg.
		//TODO: This seems to not work for all shots, so UNarrativeDialogueShot should probably control this instead of it being generic
		if (ACineCameraActor* CineCam = Cast<ACineCameraActor>(DialogueCamera))
		{
			CineCam->GetCineCameraComponent()->FocusSettings.ManualFocusDistance = FVector::Dist(CineCam->GetActorLocation(), GetSpeakerHeadLocation(Speaker));
		}
	}
}

AActor* UDialogue::LinkSpeakerAvatar_Implementation(const FSpeakerInfo& Info)
{
	AActor* SpawnedActor = nullptr;

	if (!Info.SpeakerID.IsNone())
	{
		if (!SpeakerAvatars.Contains(Info.SpeakerID) && IsValid(Info.SpeakerAvatarClass))
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.bNoFail = true;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.Owner = OwningController;

			if (UWorld* World = GetWorld())
			{
				SpawnedActor = World->SpawnActor(Info.SpeakerAvatarClass, &Info.SpeakerAvatarTransform, SpawnParams);
			}
		}
		else
		{
			//If the user doesn't want narrative to spawn their dialogue avatar, as a fallback search the world an actor tagged with the Speakers ID
			TArray<AActor*> FoundActors;

			for (FActorIterator It(GetWorld()); It; ++It)
			{
				AActor* Actor = *It;

				if (Actor->ActorHasTag(Info.SpeakerID))
				{
					FoundActors.Add(Actor);
				}
			}

			if (FoundActors.Num() > 1)
			{
				UE_LOG(LogNarrative, Warning, TEXT("UDialogue::SpawnSpeakerAvatar_Implementation tried find an actor tagged with the speakers ID but found multiple. Reverting to closest..."));

				float Dist;

				SpawnedActor = UGameplayStatics::FindNearestActor(OwningPawn->GetActorLocation(), FoundActors, Dist);
			}
			else if (FoundActors.IsValidIndex(0))
			{
				SpawnedActor = FoundActors[0];
			}
		}
	}

	return SpawnedActor;
}

void UDialogue::DestroySpeakerAvatar_Implementation(const FSpeakerInfo& Info, AActor* const SpeakerAvatar)
{
	//In general, the NPCActor and OwningPawn shouldn't be cleaned up automatically, since these weren't spawned in for the dialogue - they were already around
	//Also, don't remove any speaker avatars unless narrative spawned them 
	if (SpeakerAvatar != NPCActor && SpeakerAvatar != OwningPawn && IsValid(Info.SpeakerAvatarClass))
	{
		SpeakerAvatar->Destroy();
	}
}

void UDialogue::PlayDialogueAnimation_Implementation(class UDialogueNode* Node, const FDialogueLine& Line, class AActor* Speaker, class AActor* Listener)
{
	//Are we playing an animation on the player, or the NPC? 
	const bool bIsNPCAnim = Node->IsA<UDialogueNode_NPC>();

	AActor* ActorToUse = Speaker;

	if (Line.DialogueMontage && ActorToUse)
	{
		//Use characters anim montage player otherwise use generic
		if (ACharacter* Char = Cast<ACharacter>(ActorToUse))
		{
			if (Char)
			{
				Char->PlayAnimMontage(Line.DialogueMontage);
			}
		}
		else if (USkeletalMeshComponent* SkelMeshComp = Cast<USkeletalMeshComponent>(ActorToUse->GetComponentByClass(USkeletalMeshComponent::StaticClass())))
		{
			if (SkelMeshComp->GetAnimInstance())
			{
				SkelMeshComp->GetAnimInstance()->Montage_Play(Line.DialogueMontage);
			}
		}

	}
}

void UDialogue::PlayDialogueSound_Implementation(const FDialogueLine& Line, class AActor* Speaker, class AActor* Listener)
{
	//Stop the existing audio regardless of whether the new line has audio
	if (DialogueAudio)
	{
		DialogueAudio->Stop();
		DialogueAudio->DestroyComponent();
	}

	if (Line.DialogueSound)
	{
		if (Speaker)
		{
			DialogueAudio = UGameplayStatics::SpawnSoundAtLocation(OwningComp, Line.DialogueSound, Speaker->GetActorLocation(), Speaker->GetActorForwardVector().Rotation());
		}
		else //Else just play 2D audio 
		{
			DialogueAudio = UGameplayStatics::SpawnSound2D(OwningComp, Line.DialogueSound);
		}
	}
}

void UDialogue::PlayDialogueNode_Implementation(class UDialogueNode* Node, const FDialogueLine& Line, const FSpeakerInfo& Speaker, class AActor* SpeakerActor, class AActor* ListenerActor)
{
	if (Node)
	{
		//If autorotate speakers is enabled, make all the speakers face the person currently talking
		if (bAutoRotateSpeakers)
		{
			MakeSpeakersFaceTarget(SpeakerActor);

			// Speaker may have just been facing someone, we need to put him back
			SpeakerActor->SetActorTransform(Speaker.SpeakerAvatarTransform);
		}

		PlayDialogueSound(Line, SpeakerActor, ListenerActor);
		PlayDialogueAnimation(Node, Line, SpeakerActor, ListenerActor);

		/*Order of precedence for camera shot :
		* Dialogue line has a sequence set
		* Dialogue line has a shot set
		* speaker has a sequence set
		* speaker has a shot(s) set
		* Dialogue has dialogue shots added
		* If none are set, stop any currently running shots
		* */
		if (Line.Sequence.SequenceAsset)
		{
			PlayDialogueSequence(Line.Sequence);
		}
		else if (Line.Shot)
		{
			PlayDialogueShot(Line.Shot, SpeakerActor, ListenerActor);
		}
		else if (Speaker.DefaultSequence.SequenceAsset)
		{
			PlayDialogueSequence(Speaker.DefaultSequence);
		}
		else if (Speaker. DefaultShot)
		{
			PlayDialogueShot(Speaker.DefaultShot, SpeakerActor, ListenerActor);
		}
		else if (DialogueShots.Num())
		{
			PlayDialogueShot(DialogueShots[FMath::RandRange(0, DialogueShots.Num() - 1)], SpeakerActor, ListenerActor);
		}
	}
}

void UDialogue::PlayNPCDialogue_Implementation(class UDialogueNode_NPC* NPCReply, const FDialogueLine& LineToPlay, const FSpeakerInfo& SpeakerInfo)
{
	//Figure out what actor is saying this line
	AActor* SpeakingActor = NPCActor;
	AActor* ListeningActor = GetSpeakerAvatar(NPCReply->DirectedAtSpeakerID); // For now, all NPC lines are considered to be spoken at the player (should probably refactor)

	//If the shot isn't directed at someone, direct the line at the player by default
	if (!ListeningActor)
	{
		ListeningActor = GetPlayerAvatar();
	}

	if (SpeakerAvatars.Contains(SpeakerInfo.SpeakerID))
	{
		SpeakingActor = *SpeakerAvatars.Find(SpeakerInfo.SpeakerID);
	}

	PlayDialogueNode(NPCReply, LineToPlay, SpeakerInfo, SpeakingActor, ListeningActor);
}

void UDialogue::PlayPlayerDialogue_Implementation(class UDialogueNode_Player* PlayerReply, const FDialogueLine& Line)
{
	//Figure out what actor is saying this line
	AActor* const SpeakingActor = GetPlayerAvatar();
	AActor* ListeningActor = GetSpeakerAvatar(PlayerReply->DirectedAtSpeakerID);

	if (!ListeningActor)
	{
		ListeningActor = GetSpeakerAvatar(CurrentSpeaker.SpeakerID);
	}

	PlayDialogueNode(PlayerReply, Line, PlayerSpeakerInfo, SpeakingActor, ListeningActor);
}

float UDialogue::GetLineDuration_Implementation(class UDialogueNode* Node, const FDialogueLine& Line)
{
	/*
	* By default, we wait until the Dialogue Audio is finished, or if no audio is supplied
	* we wait at a rate of 25 letters per second (configurable in .ini) to give the reader time to finish reading the dialogue line.
	*/
	if (Line.DialogueSound)
	{	
		float DialogueLineAudioSilence = 0.5f;

		if (const UNarrativeDialogueSettings* DialogueSettings = GetDefault<UNarrativeDialogueSettings>())
		{
			DialogueLineAudioSilence = DialogueSettings->DialogueLineAudioSilence;
		}

		//Use the length of a line and add in a short buff ere
		return Line.DialogueSound->GetDuration() + DialogueLineAudioSilence;
	}
	else
	{
		float LettersPerSecondLineDuration = 25.f;
		float MinDialogueTextDisplayTime = 2.f;

		if (const UNarrativeDialogueSettings* DialogueSettings = GetDefault<UNarrativeDialogueSettings>())
		{
			LettersPerSecondLineDuration = DialogueSettings->LettersPerSecondLineDuration;
			MinDialogueTextDisplayTime = DialogueSettings->MinDialogueTextDisplayTime;
		}

		return FMath::Max(Line.Text.ToString().Len() / LettersPerSecondLineDuration, MinDialogueTextDisplayTime);
	}
}

FString UDialogue::GetStringVariable_Implementation(class UDialogueNode* Node, const FDialogueLine& Line, const FString& VariableName)
{
	return VariableName;
}

void UDialogue::OnNPCDialogueLineStarted_Implementation(class UDialogueNode_NPC* Node, const FDialogueLine& DialogueLine, const FSpeakerInfo& Speaker)
{

}

void UDialogue::OnNPCDialogueLineFinished_Implementation(class UDialogueNode_NPC* Node, const FDialogueLine& DialogueLine, const FSpeakerInfo& Speaker)
{

}

void UDialogue::OnPlayerDialogueLineStarted_Implementation(class UDialogueNode_Player* Node, const FDialogueLine& DialogueLine)
{

}

void UDialogue::OnPlayerDialogueLineFinished_Implementation(class UDialogueNode_Player* Node, const FDialogueLine& DialogueLine)
{

}

void UDialogue::PlayDialogueSequence(const FDialogueSequence& Sequence)
{
	if (Sequence.SequenceAsset && OwningController && OwningController->IsLocalPlayerController())
	{
		//We're trying to play a dialogue shot that is already playing, check if the settings don't allow this
		if (DialogueSequencePlayer && DialogueSequencePlayer->GetSequence() == Sequence.SequenceAsset)
		{
			if (!Sequence.bShouldRestart)
			{
				return;
			}
		}

		//Stop any currently running sequence 
		StopDialogueSequence();

		//Narrative needs to initialize its cutscene player 
		if (!DialogueSequencePlayer)
		{
			ULevelSequencePlayer::CreateLevelSequencePlayer(GetWorld(), Sequence.SequenceAsset, Sequence.ToSequenceSettings(), DialogueSequencePlayer);
		}
		else if (DialogueSequencePlayer && DialogueSequencePlayer->GetSequencePlayer())
		{
			DialogueSequencePlayer->PlaybackSettings = Sequence.ToSequenceSettings();
			DialogueSequencePlayer->SetSequence(Sequence.SequenceAsset);
		}

		if (DialogueSequencePlayer)
		{
			if (DialogueSequencePlayer && DialogueSequencePlayer->GetSequencePlayer())
			{
				DialogueSequencePlayer->GetSequencePlayer()->Play();
			}
		}

	}
}
void UDialogue::StopDialogueSequence()
{
	if (OwningController && OwningController->IsLocalPlayerController())
	{
		if (DialogueSequencePlayer && DialogueSequencePlayer->GetSequencePlayer())
		{
			DialogueSequencePlayer->GetSequencePlayer()->Stop();
		}
	}
}
