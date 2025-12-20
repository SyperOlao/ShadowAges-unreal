// Copyright Narrative Tools 2022. 

#pragma once

#include "CoreMinimal.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "QuestBlueprintGeneratedClass.generated.h"

class UQuestState;
class UQuest;

/**
 * Blueprint generated class for quest blueprints. The quest compiler compiles the quest and stores it in
 * the QuestTemplate object ready for use at runtime. Good explanation at https://heapcleaner.wordpress.com/2016/06/12/inside-of-unreal-engine-blueprint/
 */
UCLASS()
class NARRATIVE_API UQuestBlueprintGeneratedClass : public UBlueprintGeneratedClass
{
	GENERATED_BODY()
	
public:

	virtual void InitializeQuest(class UQuest* Quest);


	// UStruct interface
	virtual void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	// End of UStruct interface
	
	virtual void PostLoad() override;

	// UClass interface
	virtual void PurgeClass(bool bRecompilingOnLoad) override;
	// End of UClass interface

	UQuest* GetQuestTemplate() const {return QuestTemplate;}
	void SetQuestTemplate(UQuest* InQuestTemplate);

private:

	//The quest template to be created 
	UPROPERTY()
	class UQuest* QuestTemplate;


};
