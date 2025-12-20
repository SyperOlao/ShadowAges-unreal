//  Copyright Narrative Tools 2022.


#include "DS_Conversation.h"
#include "Dialogue.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

UDS_Conversation::UDS_Conversation()
{
	Location = FVector(0.f, 500.f, 0.f);
	bUsePlayerLocalSpace = true;
 }

FTransform UDS_Conversation::GetShotTransform_Implementation(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor)
{
	FTransform ShotTransform;

	if (SpeakingActor && ListeningActor && Dialogue)
	{
		AActor* const PlayerAvatar = Dialogue->GetPlayerAvatar();

		const FVector SpeakerEyesLoc = Dialogue->GetSpeakerHeadLocation(SpeakingActor);
		const FVector ListenerEyesLoc = Dialogue->GetSpeakerHeadLocation(ListeningActor);

		//Figure out where the middle of the conversation is 
		TArray<AActor*> SpeakerActors;

		SpeakerActors.Add(PlayerAvatar);

		for (auto& Speaker : Dialogue->Speakers)
		{
			if (AActor* SpeakerAvatar = Dialogue->GetSpeakerAvatar(Speaker.SpeakerID))
			{
				SpeakerActors.AddUnique(SpeakerAvatar);
			}
		}

		//If there is only 1 speaker actor, and thats the player, add the NPCActor too, since this conversations NPCs dont use speaker avatars
		if (SpeakerActors.Num() == 1)
		{
			SpeakerActors.Add(Dialogue->NPCActor);
		}

		FVector LocationSum(0, 0, 0); // sum of locations
		int32 ActorCount = 0; // num actors
		// iterate over actors
		for (int32 ActorIdx = 0; ActorIdx < SpeakerActors.Num(); ActorIdx++)
		{
			AActor* A = SpeakerActors[ActorIdx];
			// Check actor is non-null, not deleted, and has a root component
			if (IsValid(A) && A->GetRootComponent())
			{
				LocationSum += Dialogue->GetSpeakerHeadLocation(A);
				ActorCount++;
			}
		}

		// Find average
		FVector Average(0, 0, 0);
		if (ActorCount > 0)
		{
			Average = LocationSum / ((float)ActorCount);
		}

		const FVector FocalPoint = Average;
	
		const FVector ShotOffset = 
			(Dialogue->OwningPawn->GetActorForwardVector() * Location.X) + 
			(Dialogue->OwningPawn->GetActorRightVector() * Location.Y) + 
			(Dialogue->OwningPawn->GetActorUpVector() * Location.Z);

		const FVector FinalLoc = bUsePlayerLocalSpace ? FocalPoint + ShotOffset : Location;
		const FRotator FinalRot = FRotationMatrix::MakeFromX(FocalPoint - FinalLoc).Rotator();

		ShotTransform.SetLocation(FinalLoc);
		ShotTransform.SetRotation(FinalRot.Quaternion());
	}

	return ShotTransform;
}
