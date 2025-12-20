// Copyright Narrative Tools 2022. 

#pragma once

#include "CoreMinimal.h"
#include "NarrativeNodeBase.h"
#include "MovieSceneSequencePlayer.h"
#include "DialogueSM.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDialogueNodeFinishedPlaying);

/**Convinience struct with a details customization that allows the speaker ID to be selected from a combobox
rather than inputted as an FName */
USTRUCT(BlueprintType)
struct FSpeakerSelector
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Speakers")
	FName SpeakerID;

	FSpeakerSelector()
	{}

	FSpeakerSelector(const FName& InSpeakerID) :
		SpeakerID(InSpeakerID)
	{}
};

/**Specialized dialogue sequence settings that cull some of the members from the other struct - also
fixes a problem with UE5.1 where the constructor cant be found despite its module being included*/
USTRUCT(BlueprintType)
struct FDialogueSequence
{
	GENERATED_BODY()

		FDialogueSequence()
	{
		bPauseAtEnd = false;
		LoopCount = FMovieSceneSequenceLoopCount();
		PlayRate = 1.f;
		StartTime = 0.f;
		bRandomStartTime = false;
		bHidePlayer = true;
		bHideHud = true;
		bDisableCameraCuts = false;
		bShouldRestart = true;
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sequences")
	class ULevelSequence* SequenceAsset = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sequences")
	bool bPauseAtEnd = true;

	/** Number of times to loop playback. -1 for infinite, else the number of times to loop before stopping */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", meta = (UIMin = 1, DisplayName = "Loop"))
	FMovieSceneSequenceLoopCount LoopCount;

	/** The rate at which to playback the animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", meta = (Units = Multiplier))
		float PlayRate = 1.f;

	/** Start playback at the specified offset from the start of the sequence's playback range */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback", DisplayName = "Start Offset", meta = (Units = s, EditCondition = "!bRandomStartTime"))
		float StartTime = 0.f;

	/** Start playback at a random time */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Playback")
		uint32 bRandomStartTime : 1;

	/** Hide Player Pawn during play */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cinematic")
		uint32 bHidePlayer : 1;

	/** Hide HUD during play */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cinematic")
		uint32 bHideHud : 1;

	/** Disable camera cuts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cinematic")
		uint32 bDisableCameraCuts : 1;

	/** If narrative tries playing this sequence but it already started playing it from an earlier node, should we restart the shot or just let it keep going? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cinematic")
	uint32 bShouldRestart : 1;

	FMovieSceneSequencePlaybackSettings ToSequenceSettings() const
	{
		FMovieSceneSequencePlaybackSettings Settings;

		Settings.bPauseAtEnd = bPauseAtEnd;
		Settings.LoopCount = LoopCount;
		Settings.PlayRate = PlayRate;
		Settings.StartTime = StartTime;
		Settings.bRandomStartTime = bRandomStartTime;
		Settings.bHidePlayer = bHidePlayer;
		Settings.bHideHud = bHideHud;
		Settings.bDisableCameraCuts = bDisableCameraCuts;

		return Settings;
	};

};

USTRUCT(BlueprintType)
struct FDialogueLine
{
	GENERATED_BODY()

public:

	FDialogueLine()
	{
		Text = FText::GetEmpty();
		DialogueSound = nullptr;
		DialogueMontage = nullptr;
		Shot = nullptr;
	}

	/**
	The text for this dialogue node. Narrative will automatically display this on the NarrativeDefaultUI if you're using that, otherwise you can simply grab this
	yourself if you're making your own dialogue UI - it is readable from Blueprints.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Line", meta = (MultiLine = true))
	FText Text;

	/**
	* If a dialogue sound is selected, narrative will automatically play the sound for you in 3D space, at the location of the speaker.  
	* If narrative can't find a speaker actor (for example if you were getting a phone call where there isn't an physical speaker) it will be played in 2D. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Line")
	class USoundBase* DialogueSound;

	/**
	* If a montage is selected, narrative will automatically play this montage on the speaker, provided they have a SkeletalMeshComponent to play it on.
	*
	* If you need more fine tuned control, for example if you've added facial animations and need narrative to play the animation on a specific face mesh,
	* override the PlayDialogueMontage function in your DialogueBlueprint! From there you can override exactly what mesh gets the montage played on it, or
	* even have narrative play multiple different animations, whatever you want to do.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Line")
	class UAnimMontage* DialogueMontage;

	/**
	* Camera shot to play for this dialogue line. Narratives cinematic dialogue camera will use this shot to define how to position itself. 
	*/
	UPROPERTY(EditAnywhere, Instanced, Category = "Dialogue Line")
	class UNarrativeDialogueShot* Shot;

	/**
	* If selected, Narrative will play this sequence whilst this line is being played. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue Line")
	FDialogueSequence Sequence;

};

/**Base class for states and branches in the Dialogues state machine*/
 UCLASS(BlueprintType, Blueprintable)
 class NARRATIVE_API UDialogueNode : public UNarrativeNodeBase
 {

	 GENERATED_BODY()

 public:

	UDialogueNode();

	 //The dialogue line associated with this node
	 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Details - Dialogue Node", meta = (ShowOnlyInnerProperties))
	 FDialogueLine Line;

	 /**
	 deprecated, kept around for legacy for a few more releases then will be removed.
	 */
	 UPROPERTY()
	 FText Text;

	 /**
	 deprecated, kept around for legacy for a few more releases then will be removed.
	 */
	 UPROPERTY()
		 class USoundBase* DialogueSound;

	 /**
	 deprecated, kept around for legacy for a few more releases then will be removed.
	 */
	 UPROPERTY()
		 class UAnimMontage* DialogueMontage;

	 /**
	 deprecated, kept around for legacy for a few more releases then will be removed.
	 */
	 UPROPERTY()
		 class UNarrativeDialogueShot* Shot;

	 /**
	 deprecated, kept around for legacy for a few more releases then will be removed.
	 */
	 UPROPERTY()
		 FDialogueSequence Sequence;

	 /** If alternative lines are added in here, narrative will randomly select either the main line or one of the alternatives.
	 
	 This can make dialogues more random and believable. 
	 */
	 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue Line", meta=(AdvancedDisplay))
	 TArray<FDialogueLine> AlternativeLines;

	 virtual FDialogueLine GetRandomLine() const;

	UPROPERTY(BlueprintAssignable, Category = "Dialogue")
	FOnDialogueNodeFinishedPlaying OnDialogueFinished;

	//The last line the dialogue node played.
	UPROPERTY(BlueprintReadOnly, Category = "Details")
	FDialogueLine PlayedLine;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug", meta = (AdvancedDisplay))
	TArray<class UDialogueNode_NPC*> NPCReplies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug", meta = (AdvancedDisplay))
	TArray<class UDialogueNode_Player*> PlayerReplies;
	
	UPROPERTY()
	class UDialogue* OwningDialogue;

	UPROPERTY()
	class UNarrativeComponent* OwningComponent;

	//Name of custom event to call when this is reached 
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Details - Dialogue Node", meta = (AdvancedDisplay))
	FName OnPlayNodeFuncName;

	/**The ID of the speaker we are saying this line to. Can be left empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Details - Dialogue Node")
	FName DirectedAtSpeakerID;
	
#if WITH_EDITORONLY_DATA

	/**If true, the dialogue editor will style this node in a compact form*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Details - Dialogue Editor")
	bool bCompactView;

#endif

	TArray<class UDialogueNode_NPC*> GetNPCReplies(APlayerController* OwningController, APawn* OwningPawn, class UNarrativeComponent* NarrativeComponent);
	TArray<class UDialogueNode_Player*> GetPlayerReplies(APlayerController* OwningController, APawn* OwningPawn, class UNarrativeComponent* NarrativeComponent);

	virtual UWorld* GetWorld() const;

	//The text this dialogue should display on its Graph Node
	const bool IsMissingCues() const;

	void ConvertLegacyNarrativeProps();

private:

#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

public:
	virtual void EnsureUniqueID();
	void GenerateIDFromText();

#endif //WITH_EDITOR


private:

	//Legacy annoyingness, we need to move old data members into new struct that was added - in a couple of releases remove old members completely
	UPROPERTY()
	bool bHasConvertedLegacyNarrativeProperties;

 };

UCLASS(BlueprintType)
class NARRATIVE_API UDialogueNode_NPC : public UDialogueNode
{
	GENERATED_BODY()

public:

	/**The ID of the speaker for this node */
	UPROPERTY(BlueprintReadWrite, Category = "Details - NPC Dialogue Node")
	FName SpeakerID;

	/**Grab this NPC node, appending all follow up responses to that node. Since multiple NPC replies can be linked together, 
	we need to grab the chain of replies the NPC has to say. */
	TArray<class UDialogueNode_NPC*> GetReplyChain(APlayerController* OwningController, APawn* OwningPawn, class UNarrativeComponent* NarrativeComponent);

	//Node is just used for routing and doesn't contain any dialogue 
	FORCEINLINE bool IsRoutingNode() const {return Line.Text.IsEmptyOrWhitespace() && Events.Num() <= 0; }

};

UCLASS(BlueprintType)
class NARRATIVE_API UDialogueNode_Player : public UDialogueNode
{
	GENERATED_BODY()

public:

	//Have to pass dialogue in because OwningDialogue is null for some reason - TODO look into why this is
	UFUNCTION(BlueprintPure, Category = "Details")
	virtual FText GetOptionText(class UDialogue* InDialogue) const;

	FORCEINLINE bool IsAutoSelect() const {return bAutoSelect || Line.Text.IsEmpty(); };

protected:

	/**The shortened text to display for dialogue option when it shows up in the list of available responses. If left empty narrative will just use the main text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Details - Player Dialogue Node")
	FText OptionText;

	/**If true, this dialogue option will be automatically selected instead of the player having to select it from the UI*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Details - Player Dialogue Node")
	bool bAutoSelect = false;
};