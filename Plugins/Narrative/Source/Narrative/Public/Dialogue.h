// Copyright Narrative Tools 2022. 

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "LevelSequencePlayer.h"
#include "DialogueSM.h"
#include <MovieSceneSequencePlayer.h>
#include "Dialogue.generated.h"

/**Represents the configuration for a speaker in this dialogue*/
USTRUCT(BlueprintType)
struct FSpeakerInfo
{

	GENERATED_BODY()

	FSpeakerInfo()
	{
		SpeakerID = NAME_None;
		NodeColor = FLinearColor(0.2f, 0.2f, 0.2f);

		DefaultShot = nullptr;
	}

	//The name of this speaker. 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker Details")
	FName SpeakerID;

	//The shot to use when this speaker is talking - overrides any shots added to the dialogue 
	UPROPERTY(EditAnywhere, Instanced, Category = "Camera")
	class UNarrativeDialogueShot* DefaultShot = nullptr;

	//Sequence to use whenever this speaker is talking (can still be overriden by a dialogue line if needed)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequences")
	FDialogueSequence DefaultSequence;

	/**
	Set this to a valid actor class if you want narrative to automatically spawn your speaker avatar in, and destroy it when the dialogue ends. 

	If you leave this empty, narrative will try find an actor in the world with the Speaker ID added as a tag, and use that as the avatar instead.

	If you require different functionality, override the LinkSpeakerAvatar function. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker Details")
	TSubclassOf<class AActor> SpeakerAvatarClass;

	//The transform for the SpeakerActor that gets spawned in, if one is set. 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker Details")
	FTransform SpeakerAvatarTransform;

	//Custom node colour for this NPC in the graph
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speaker Details")
	FLinearColor NodeColor;
};

/**Special speaker type created for the player*/
USTRUCT(BlueprintType)
struct FPlayerSpeakerInfo : public FSpeakerInfo
{
	GENERATED_BODY()

	FPlayerSpeakerInfo()
	{
		SpeakerID = FName("Player");
	}

	//The shot to use when the players replies appear on the screen and the player needs to select one 
	UPROPERTY(EditAnywhere, Instanced, Category = "Camera", meta = (DisplayAfter=DefaultSequence))
	class UNarrativeDialogueShot* SelectingReplyShot = nullptr;

	//Sequence to play when player is selecting their reply, overrides SelectingReplyShot
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequences", meta = (DisplayAfter = SelectingReplyShot))
	FDialogueSequence SelectingReplySequence;
};

//Created at runtime, but also used as a template, similar to UWidgetTrees in UWidgetBlueprints. 
UCLASS(Blueprintable, BlueprintType, meta = (DisplayName="Dialogue"))
class NARRATIVE_API UDialogue : public UObject
{
	GENERATED_BODY()

public:

	UDialogue();

    // Non-blueprint tick for dialogues
    virtual void Tick(const float DeltaTime) {}

	virtual UWorld* GetWorld() const override;
	virtual bool Initialize(class UNarrativeComponent* InitializingComp, class AActor* DefaultNPCAvatar, FName StartFromID);
	virtual void Deinitialize();

	virtual void DuplicateAndInitializeFromDialogue(UDialogue* DialogueTemplate);

	//Dialogue assets/nodes etc have the same name on client and server, so can be referenced over the network 
	//TODO probably don't need this anymore, as it didn't seem to work and we now resolve nodes via their FName IDs instead 
	bool IsNameStableForNetworking() const override { return true; };
	bool IsSupportedForNetworking() const override { return true; };

	#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditChange(FEditPropertyChain& PropertyAboutToChange) override;
	#endif	

	FSpeakerInfo GetSpeaker(const FName& SpeakerID);

	//All the NPC speakers in this dialogue - for the player fill out the PlayerSpeakerInfo below! 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speakers", meta = (TitleProperty="SpeakerID"))
	TArray<FSpeakerInfo> Speakers;

	UPROPERTY(BlueprintReadOnly)
	class AActor* Instigator;

	//The speaker info for our player
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speakers")
	FPlayerSpeakerInfo PlayerSpeakerInfo;

	//If true, narrative UI won't show mouse cursor and set input mode to UI - you'll still be able to control your player.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
	bool bFreeMovement;

	//If false, default UI will disallow closing of the dialogue with ESC, and you'll need to wait for it to end 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
	bool bCanBeExited;

	//If enabled, narrative will automatically rotate the speakers to face whoever is currently talking.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
	bool bAutoRotateSpeakers;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
	bool bPlayOnlyOnce = false;

	/*
	* If selected, automatically stop the characters movement when the dialogue begins. This is useful because
	* a camera shot will automatically play but then if the speaker is still moving, the shot won't be lined up correctly 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Configuration")
	bool bAutoStopMovement;

	//whether or not to spawn a dialogue camera 
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	bool bSpawnDialogueCamera;

	//The amount of seconds it should take to blend from the players camera into/out of the dialogue camera.  
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin=0))
	float DialogueCameraBlendTime;

	//Camera class to spawn and use during the dialogue
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	TSubclassOf<class ACameraActor> DialogueCameraClass;

	//Camera shake the dialogue camera will play. Defaults to a handheld camera shake by default.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	TSubclassOf<class UCameraShakeBase> DialogueCameraShake;

	//Any shots added to this array will randomly be cycled through by narratives dialogue cinecam.
	UPROPERTY(EditAnywhere, Instanced, Category = "Camera")
	TArray<class UNarrativeDialogueShot*> DialogueShots;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	class UNarrativeComponent* OwningComp;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	class APawn* OwningPawn;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	class APlayerController* OwningController;

	//The NPCActor that we're talking to. This can be null 
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	class AActor* NPCActor;

	//The first thing the NPC says in the dialog
	UPROPERTY()
	class UDialogueNode_NPC* RootDialogue;

	//Holds all of the npc replies in the dialogue
	UPROPERTY()
	TArray<class UDialogueNode_NPC*> NPCReplies;

	//Holds all of the player replies in the dialogue
	UPROPERTY()
	TArray<class UDialogueNode_Player*> PlayerReplies;

	//Skips the current dialogue line 
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	virtual void SkipCurrentLine();

public: 

	FORCEINLINE bool IsPlaying() const {return CurrentNode != nullptr; }

	//Server has been given a dialogue option from the player
	void SelectDialogueOption(UDialogueNode_Player* PlayerNode);

	//Used to check at any time on client or server if we have a valid chunk, meaning we can call play() and begin the dialogue
	bool HasValidChunk() const;

	//After we select a dialogue option, this function generates the next chunk of dialogue,
	// a "chunk" being a chain of NPC replies, followed by the players available responses to that chain. 
	bool GenerateDialogueChunk(UDialogueNode_NPC* NPCNode);

	//Called by the client when they have received the next dialogue chunk from the server
	void ClientReceiveDialogueChunk(const TArray<FName>& NPCReplies, const TArray<FName>& PlayerReplies);
	
	//Plays the current chunk of dialogue, then broadcasts the players available reponses. 
	void Play();

	//Called once we've played through all the NPC dialogues and the players reponses have been sent to the UI 
	void NPCFinishedTalking();

	//The NPC replies in the current chunk 
	UPROPERTY()
	TArray<class UDialogueNode_NPC*> NPCReplyChain;

	//The player responses once NPC has finished talking the current chunk 
	UPROPERTY()
	TArray<class UDialogueNode_Player*> AvailableResponses;

	//We need to reference dialogue nodes by their IDs as dialogue objects cant be replicated over the network - helper functions to easily do this: 

	//Get a node via its ID
	UDialogueNode_NPC* GetNPCReplyByID(const FName& ID) const;
	UDialogueNode_Player* GetPlayerReplyByID(const FName& ID) const;

	//Get multiple nodes via their IDs
	TArray<UDialogueNode_NPC*> GetNPCRepliesByIDs(const TArray<FName>& IDs) const;
	TArray <UDialogueNode_Player*> GetPlayerRepliesByIDs(const TArray<FName>& IDs) const;

	//Convert an array of nodes into an array of IDs
	TArray<FName> MakeIDsFromNPCNodes(const TArray<UDialogueNode_NPC*> Nodes) const;
	TArray<FName> MakeIDsFromPlayerNodes(const TArray<UDialogueNode_Player*> Nodes) const;

	UFUNCTION(BlueprintPure, Category = "Dialogue")
	TArray<UDialogueNode*> GetNodes() const;

	//Replace any {MyVar} style variables in a dialogue line with their value 
	virtual void ReplaceStringVariables(FText& Line);

	//* Sometimes our actual pawn shows up in dialogues, other times we use a special avatar actor that is spawned in. Return whichever one is being used. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	AActor* GetPlayerAvatar() const;

	/* Grab the avatar for a given speaker, if one exists. */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	AActor* GetSpeakerAvatar(const FName& SpeakerID) const;

	/*
	* Return the location of the actors head - basically the location the dialogue camera will aim itself at. 
	* 
	* By default, narrative will check for a skeletal mesh with a bone/socket named "head", and if this cannot be found, it will 
	* fall back to using GetActorEyesViewPoint. 
	* 
	* However, you can override this function if you need more complex functionality
	*/
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dialogue")
	FVector GetSpeakerHeadLocation(class AActor* Actor);
	virtual FVector GetSpeakerHeadLocation_Implementation(class AActor* Actor);

	/**
	* Useful when dialogues need to do something every tick
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void TickDialogue(const float DeltaTime);
	virtual void TickDialogue_Implementation(const float DeltaTime);

protected:


	/*
	* Narrative needs to know which speaker ID links to which Speaker Avatar, so it knows where to point the camera, who to play
	* animations on, and much more. This function links each Speaker to its Speaker Avatar. It does this by spawning the actor 
	* if an AvatarActorClass is set, or by finding an actor in the world with the SpeakerID added as a tag.
	* 
	* Finally, we still weren't able to find an Avatar to link to the speaker, we'll fall back to the DefaultNPCAvatar for NPCs or 
	* the players Pawn for Player nodes. 
	* 
	* If that doesn't work for your game, you can override this function
	*/
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dialogue")
	AActor* LinkSpeakerAvatar(const FSpeakerInfo& Info);
	virtual AActor* LinkSpeakerAvatar_Implementation(const FSpeakerInfo& Info);

	/*
	* Clean up a given actor from the world
	*/
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dialogue")
	void DestroySpeakerAvatar(const FSpeakerInfo& Info, AActor* SpeakerAvatar);
	virtual void DestroySpeakerAvatar_Implementation(const FSpeakerInfo& Info, AActor* SpeakerAvatar);

	/*
	* Play a dialogue animation. Override this if you want to change how narrative plays animations
	*
	* Default implementation just plays the supplied anim montage on the NPC actor you gave it.
	* 
	* Speaker and Listener are the avatar actors for the speaker and listener that were spawned by narrative, or OwningPawn/NPCActor
	*/
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dialogue")
	void PlayDialogueAnimation(class UDialogueNode* Node, const FDialogueLine& Line, class AActor* Speaker, class AActor* Listener);
	virtual void PlayDialogueAnimation_Implementation(class UDialogueNode* Node, const FDialogueLine& Line, class AActor* Speaker, class AActor* Listener);

	/*
	* Play a dialogue sound. Override this if you want to change how narrative plays sounds.
	*
	* Default implementation just plays the sound at the location of the NPC, or in 2D if no NPC was supplied. 
	*/
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dialogue")
	void PlayDialogueSound(const FDialogueLine& Line, class AActor* Speaker, class AActor* Listener);
	virtual void PlayDialogueSound_Implementation(const FDialogueLine& Line, class AActor* Speaker, class AActor* Listener);

	/*
	* Play a dialogue node. Narrative plays some audio, a montage, and updates the camera by default
	* however if you want modify this behavior or add extra behavior you can override this function!
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void PlayDialogueNode(class UDialogueNode* Node, const FDialogueLine& Line, const FSpeakerInfo& Speaker, class AActor* SpeakerActor, class AActor* ListenerActor);
	virtual void PlayDialogueNode_Implementation(class UDialogueNode* Node, const FDialogueLine& Line, const FSpeakerInfo& Speaker, class AActor* SpeakerActor, class AActor* ListenerActor);

	/*
	* Allows you to override how an NPC dialogue node is played. Narrative plays some audio, a montage, and plays a cinematic shot by default,
	* however if you want to do even more you can override this function
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void PlayNPCDialogue(class UDialogueNode_NPC* NPCReply, const FDialogueLine& Line, const FSpeakerInfo& Speaker);
	virtual void PlayNPCDialogue_Implementation(class UDialogueNode_NPC* NPCReply, const FDialogueLine& Line, const FSpeakerInfo& Speaker);

	/*
	* Allows you to override how an Player dialogue node is played. Narrative plays some audio, a montage, and plays a cinematic shot by default,
	* however if you want modify this behavior or add extra behavior you can override this function
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void PlayPlayerDialogue(class UDialogueNode_Player* PlayerReply, const FDialogueLine& Line);
	virtual void PlayPlayerDialogue_Implementation(class UDialogueNode_Player* PlayerReply, const FDialogueLine& Line);

	/*Returns how long narrative should wait before moving onto the next line.
	*
	* By default, we wait the length of the dialogue audio, or if no audio is supplied
	* we wait at a rate of 25 letters per second to give the reader time to finish reading.
	*
	* However if you want to change how long narrative waits for a dialogue line you can override this function. 
	* 
	* This words per minute can be configured in the Narrative settings.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	float GetLineDuration(class UDialogueNode* Node, const FDialogueLine& Line);
	virtual float GetLineDuration_Implementation(class UDialogueNode* Node, const FDialogueLine& Line);

	/**
	* Allows you to use variables in your dialogue, ie Hello {PlayerName}.
	*
	* Simply override this function, then check the value of variable name and return whatever value you like!
	*
	* ie if(VariableName == "PlayerName") {return OwningPawn->GetUsername();}; - then Hello {PlayerName}! will automatically become Hello xXNoobPwner420Xx! etc 
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	FString GetStringVariable(class UDialogueNode* Node, const FDialogueLine& Line, const FString& VariableName);
	virtual FString GetStringVariable_Implementation(class UDialogueNode* Node, const FDialogueLine& Line, const FString& VariableName);

	/**
	* Called when an NPC Dialogue line starts
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void OnNPCDialogueLineStarted(class UDialogueNode_NPC* Node, const FDialogueLine& DialogueLine, const FSpeakerInfo& Speaker);
	void OnNPCDialogueLineStarted_Implementation(class UDialogueNode_NPC* Node, const FDialogueLine& DialogueLine, const FSpeakerInfo& Speaker);

	/**
	* Called when an NPC Dialogue line is finished
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void OnNPCDialogueLineFinished(class UDialogueNode_NPC* Node, const FDialogueLine& DialogueLine, const FSpeakerInfo& Speaker);
	void OnNPCDialogueLineFinished_Implementation(class UDialogueNode_NPC* Node, const FDialogueLine& DialogueLine, const FSpeakerInfo& Speaker);

	/**
	* Called when a player dialogue line has started
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void OnPlayerDialogueLineStarted(class UDialogueNode_Player* Node, const FDialogueLine& DialogueLine);
	virtual void OnPlayerDialogueLineStarted_Implementation(class UDialogueNode_Player* Node, const FDialogueLine& DialogueLine);

	/**
	* Called when a player dialogue line has started
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue")
	void OnPlayerDialogueLineFinished(class UDialogueNode_Player* Node, const FDialogueLine& DialogueLine);
	virtual void OnPlayerDialogueLineFinished_Implementation( class UDialogueNode_Player* Node, const FDialogueLine& DialogueLine);

	/**
	* Called when the dialogue Begins.
	*/
	UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue", meta = (DisplayName = "OnBeginDialogue", ScriptName = "OnBeginDialogue"))
	void K2_OnBeginDialogue();

	/**
	* Called when the dialogue ends.
	*/
	UFUNCTION(BlueprintImplementableEvent, Category = "Dialogue" ,meta = (DisplayName = "OnEndDialogue", ScriptName = "OnEndDialogue"))
	void K2_OnEndDialogue();

	virtual void OnBeginDialogue();
	virtual void OnEndDialogue();

	//Tell the dialogue sequence player to start or stop playing a dialogue shot.
	virtual void PlayDialogueSequence(const FDialogueSequence& Sequence);
	virtual void StopDialogueSequence();

	//Play a dialogue shot
	virtual void PlayDialogueShot(class UNarrativeDialogueShot* Shot, class AActor* Speaker, class AActor* Listener);

	UFUNCTION()
	virtual void PlayNextNPCReply();

	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	virtual void FinishNPCDialogue();

	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	virtual void FinishPlayerDialogue();

	virtual void PlayNPCDialogueNode(class UDialogueNode_NPC* NPCReply);
	virtual void PlayPlayerDialogueNode(class UDialogueNode_Player* PlayerReply);

	//Tells the narrative component this dialogue is finished and clears the dialogue. 
	virtual void ExitDialogue();

	virtual void InitSpeakerAvatars();

	/*Turn everyone (except the target obviously) to look at the target*/
	void MakeSpeakersFaceTarget(const AActor* Target);

	//Process all the events on a given node
	virtual void ProcessNodeEvents(class UDialogueNode* Node, bool bStartEvents);

	UPROPERTY(EditAnywhere, Category = "Dialogue")
	bool bPlayOnlyStartNodeEvent = true;

	//The current dialogue node narrative is playing
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	class UDialogueNode* CurrentNode;

	//The current speaker that is talking
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FSpeakerInfo CurrentSpeaker;

	//The current line that is being played 
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDialogueLine CurrentLine;

	//Sequence actor responsible for playing any cinematic shots during the dialogue
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	class ALevelSequenceActor* DialogueSequencePlayer;

	//Audio component responsible for playing any audio during the dialogue
	UPROPERTY()
	class UAudioComponent* DialogueAudio;

	//Camera responsible for pointing itself at the speaker
	UPROPERTY(BlueprintReadOnly, Category = "Blend")
	class ACameraActor* DialogueCamera;

	UPROPERTY(BlueprintReadOnly, Category = "Blend")
	FTransform CameraDesiredTransform;

	//All spawned speaker actors, with the speaker ID mapping to that speakers avatar
	UPROPERTY()
	TMap<FName, class AActor*> SpeakerAvatars;

	//We cache the players old view target so we can set the view target back to it after dialogue ends 
	UPROPERTY()
	class AActor* OldViewTarget;

	//Our players rot prior to beginning dialogue, so we can put him back where he was after making him face the conversation
	UPROPERTY()
	FRotator OldPlayerRot;

	//Our players rot prior to beginning dialogue, so we can put him back where he was after making him face the conversation
	UPROPERTY()
	FRotator OldNPCRot;

	UPROPERTY()
	FTimerHandle TimerHandle_NPCReplyFinished;

	UPROPERTY()
	FTimerHandle TimerHandle_PlayerReplyFinished;

	//Deintialize has been called and the dialogue should not play anymore
	bool bDeinitialized;

	FORCEINLINE bool IsInitialized() const {return !bDeinitialized;};

};
