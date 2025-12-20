//  Copyright Narrative Tools 2022.

#pragma once

#include "CoreMinimal.h"
#include "NarrativeDialogueShot.h"
#include "DS_WorldTransform.generated.h"

/**
 * Moves the camera to a predefined world transform. 
 */
UCLASS()
class NARRATIVE_API UDS_WorldTransform : public UNarrativeDialogueShot
{
	GENERATED_BODY()
	
protected:

	virtual FTransform GetShotTransform_Implementation(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor) override;

	//The camera offset from the speaker, in the speakers space
	UPROPERTY(EditAnywhere, Category = "Shot Settings")
	FTransform CameraTransform;

};
