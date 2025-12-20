//  Copyright Narrative Tools 2022.

#include "DS_OverShoulder.h"
#include "Dialogue.h"

UDS_OverShoulder::UDS_OverShoulder()
{
	SpeakerOffset = FVector(-150.f, -100.f, 20.f);
	TiltYawDegrees = 0.f; //OTS already enforces same side of screen, tilt should be off by default 
}

FTransform UDS_OverShoulder::GetShotTransform_Implementation(class UDialogue* Dialogue, class AActor* SpeakingActor, class AActor* ListeningActor)
{
	FTransform ShotTransform;

	if (SpeakingActor && ListeningActor && Dialogue)
	{
		AActor* const PlayerAvatar = Dialogue->GetPlayerAvatar();

		const FVector SpeakerEyesLoc = Dialogue->GetSpeakerHeadLocation(SpeakingActor);
		const FVector ListenerEyesLoc = Dialogue->GetSpeakerHeadLocation(ListeningActor);

		const FVector FocalPoint = (SpeakerEyesLoc + ListenerEyesLoc) * 0.5f;

		const FVector ConversationDir = (SpeakerEyesLoc - ListenerEyesLoc).GetSafeNormal2D();
		FVector RightDir = FVector::CrossProduct(ConversationDir, FVector::UpVector);

		//Flip the right direction when the player is speaking, to ensure the 180 degree rule is enforced
		if (PlayerAvatar == SpeakingActor)
		{
			RightDir = -RightDir;
		}

		const FVector FwdOffset = ConversationDir * SpeakerOffset.X;
		const FVector RightOffset = RightDir * SpeakerOffset.Y;
		const FVector UpOffset = FVector::UpVector * SpeakerOffset.Z;

		const FVector FinalLoc = ListenerEyesLoc + FwdOffset + RightOffset + UpOffset;
		FRotator FinalRot = FRotationMatrix::MakeFromX(FocalPoint - FinalLoc).Rotator();

		/*
		* If the player is speaking, tilt the camera in the opposite direction - this ensures that the player and other characters appear
		* on different sides of the screen (a rule in cinematography)
		*/
		const float Direction = SpeakingActor == PlayerAvatar ? 1.f : -1.f;
		FinalRot.Yaw += TiltYawDegrees * Direction;

		ShotTransform.SetLocation(FinalLoc);
		ShotTransform.SetRotation(FinalRot.Quaternion());
	}

	return ShotTransform;
}
