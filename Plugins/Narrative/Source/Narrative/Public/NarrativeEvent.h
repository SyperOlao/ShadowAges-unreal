// Copyright Narrative Tools 2022. 

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "NarrativeEvent.generated.h"

/**
* Used for picking when an event should run
*/
UENUM(BlueprintType)
enum class EEventRuntime : uint8
{
	/* Execute the event at the start.

	For dialogue, this is when the dialogue line starts playing.
	For Quest states, this is when the state is entered
	For Quest branches, this is when the branch becomes active
	*/
	Start,
	/* Execute the event at the end. 

	For dialogue, this is when the dialogue line finishes playing.
	For Quest states, this is when the state is exited
	For Quest branches, this is when the branch gets taken to a new state and gets deactivated. 
	*/
	End,
	// Run the event at both start and end. This means your event will be executed twice. 
	Both
};

/**
 * Narrative Events allow you to write a little bit of code that fires when a given quest or dialogue node is reached. 
 *
 * For example, you could make a Narrative Event "Give item" that adds an item to the players inventory. Then, you could 
 * add that event to a dialogue node, for example if an NPC said "Heres my sword, I want you to take it." you could add your
 * event to that dialogue node and actually add the sword item to the players inventory. 
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, AutoExpandCategories = ("Default"))
class NARRATIVE_API UNarrativeEvent : public UObject
{
	GENERATED_BODY()

public:

    // Simple non-blueprint virtual tick for events
    virtual void Tick(const float DeltaTime) {}

	// Allows the Object to get a valid UWorld from it's outer.
	virtual UWorld* GetWorld() const override
	{
		if (HasAllFlags(RF_ClassDefaultObject))
		{
			// If we are a CDO, we must return nullptr instead of calling Outer->GetWorld() to fool UObject::ImplementsGetWorld.
			return nullptr;
		}

		UObject* Outer = GetOuter();

		while (Outer)
		{
			UWorld* World = Outer->GetWorld();
			if (World)
			{
				return World;
			}

			Outer = Outer->GetOuter();
		}

		return nullptr;
	}


	/**
	If true, run this event on the players shared narrative comp instead of their local one
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	bool bUseSharedComponent = false;

	/**
	Defines when the event should be executed 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	EEventRuntime EventRuntime;

	UFUNCTION(BlueprintNativeEvent, Category = "Event")
	bool ExecuteEvent(APawn* Pawn, APlayerController* Controller, class UNarrativeComponent* NarrativeComponent);
	virtual bool ExecuteEvent_Implementation(APawn* Pawn, APlayerController* Controller, class UNarrativeComponent* NarrativeComponent);

	/**Define the text that will show up on a node if this event is added to it */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Event")
	FString GetGraphDisplayText();
	virtual FString GetGraphDisplayText_Implementation();
};
