//  Copyright Narrative Tools 2022.


#include "DS_WorldTransform.h"

FTransform UDS_WorldTransform::GetShotTransform_Implementation(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor)
{
	return CameraTransform;
}
