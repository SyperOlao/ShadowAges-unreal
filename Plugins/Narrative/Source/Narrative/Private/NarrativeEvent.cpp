// Copyright Narrative Tools 2022. 


#include "NarrativeEvent.h"


bool UNarrativeEvent::ExecuteEvent_Implementation(APawn* Pawn, APlayerController* Controller, class UNarrativeComponent* NarrativeComponent)
{
	return true;
}

FString UNarrativeEvent::GetGraphDisplayText_Implementation()
{
	return GetName();
}
