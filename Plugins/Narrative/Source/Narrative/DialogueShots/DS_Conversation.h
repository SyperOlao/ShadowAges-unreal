//  Copyright Narrative Tools 2022.

#pragma once

#include "CoreMinimal.h"
#include "NarrativeDialogueShot.h"
#include "DS_Conversation.generated.h"

/**
 * A shot that aims itself at the center of a conversation (the average location of all the speakers in the dialogue)
 * 
 * The location can either be a world space, or an offset in the player avatars space, depending on whether use bUsePlayerLocalSpace is checked. 
 */
UCLASS()
class NARRATIVE_API UDS_Conversation : public UNarrativeDialogueShot
{
	GENERATED_BODY()

protected:

	UDS_Conversation();

	/**
	The offset from the player, for example (0, 0, 1000) would set up a birds eye view shot, (0, 500, 0) would be a side on shot etc.

	If bUsePlayerLocalSpace is false, this location will be used as a world location instead of an offset.
	*/
	UPROPERTY(EditAnywhere, Category = "Shot Settings")
	FVector Location;

	/**If true, location is an offset in the players local space, otherwise location will be a world location. */
	UPROPERTY(EditAnywhere, Category = "Shot Settings")
	bool bUsePlayerLocalSpace;

	virtual FTransform GetShotTransform_Implementation(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor) override;


};