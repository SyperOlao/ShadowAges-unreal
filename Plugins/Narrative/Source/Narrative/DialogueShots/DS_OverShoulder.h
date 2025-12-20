//  Copyright Narrative Tools 2022.

#pragma once

#include "CoreMinimal.h"
#include "NarrativeDialogueShot.h"
#include "DS_OverShoulder.generated.h"

/**
 * A shot centered on the middle of the conversation, but offset from the listeners shoulder. Automatically adheres to the 180 degree rule
 */
UCLASS()
class NARRATIVE_API UDS_OverShoulder : public UNarrativeDialogueShot
{
	GENERATED_BODY()

protected:

	UDS_OverShoulder();

	/**
	The cameras offset from the listeners head. 
	The Y (right) offset is flipped when the player is speaking, to ensure the camera always stays on the same side of the conversation. (180 degree rule)
	*/
	UPROPERTY(EditAnywhere, Category = "Shot Settings")
	FVector SpeakerOffset;

	//The tilt in degrees, automatically applied in different directions for each speaker (so each character stays on their own side of the screen)
	UPROPERTY(EditAnywhere, Category = "Shot Settings")
	float TiltYawDegrees;

	virtual FTransform GetShotTransform_Implementation(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor) override;

	
};
