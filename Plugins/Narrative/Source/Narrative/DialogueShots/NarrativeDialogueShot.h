// Copyright Narrative Tools 2022. 
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "NarrativeDialogueShot.generated.h"

/**
 * Defines a cinematic shot for the camera to use in a dialogue, by providing a method that tells the camera where to position itself.
 * 
 * Narrative provides many shots out of the box, but if you to create a new shot type you can subclass this. 
 * 
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, AutoExpandCategories = ("Default"))
class NARRATIVE_API UNarrativeDialogueShot : public UObject
{
	GENERATED_BODY()
	
public:

	UNarrativeDialogueShot(); 

	/**
	Returns the position the camera be at when the given actor is speaking - by default narrative just places it near the actors eyes view point.
	For most games this will be fine - the camera will look at the head of whoever is speaking, however you may wish to override this if you need the camera elsewhere.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Dialogue Camera")
	FTransform GetShotTransform(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor);
	virtual FTransform GetShotTransform_Implementation(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor);

	//TODO implement blend times - commened as don't have time to get stable for now
	//The time it should take for the camera to blend into this shot 
	//UPROPERTY(EditAnywhere, Category = "Shot Settings")
	//float ShotBlendTime;
};
