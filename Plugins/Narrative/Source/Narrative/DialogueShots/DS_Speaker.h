//  Copyright Narrative Tools 2022.

#pragma once

#include "CoreMinimal.h"
#include "NarrativeDialogueShot.h"
#include "DS_Speaker.generated.h"

/**
* Focal point is the person speaking,  with a customizable offset and camera tilt.

* Automatically handles the 180 degree rule and the tilt automatically tilts in different directions depending on who is speaking,
* keeping each speaker on their own side of the screen.
 */
UCLASS()
class NARRATIVE_API UDS_Speaker : public UNarrativeDialogueShot
{
	GENERATED_BODY()
	
protected:
	
	UDS_Speaker();

	//The camera offset from the speaker, in the speakers space
	UPROPERTY(EditAnywhere, Category = "Shot Settings")
	FVector SpeakerOffset;

	//The tilt in degrees, automatically applied in different directions for each speaker (so each character stays on their own side of the screen)
	UPROPERTY(EditAnywhere, Category = "Shot Settings")
	float TiltYawDegrees;

	virtual FTransform GetShotTransform_Implementation(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor) override;
};
