// Copyright Narrative Tools 2022. 

#include "NarrativeDialogueShot.h"
#include "Dialogue.h"

UNarrativeDialogueShot::UNarrativeDialogueShot()
{
	//ShotBlendTime = 0.f;
}

FTransform UNarrativeDialogueShot::GetShotTransform_Implementation(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor)
{
	return FTransform();
}
