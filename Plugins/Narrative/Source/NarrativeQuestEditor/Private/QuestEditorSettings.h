// Copyright Narrative Tools 2022. 

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "QuestEditorSettings.generated.h"

/**
 * 
 */
UCLASS(config=Engine, defaultconfig)
class NARRATIVEQUESTEDITOR_API UQuestEditorSettings : public UObject
{
	GENERATED_BODY()

public:

	UQuestEditorSettings();

	UPROPERTY(EditAnywhere, config, Category = "Graph Style")
	FLinearColor FailedNodeColor;

	UPROPERTY(EditAnywhere, config, Category = "Graph Style")
	FLinearColor SuccessNodeColor;

	UPROPERTY(EditAnywhere, config, Category = "Graph Style")
	FLinearColor StateNodeColor;

	UPROPERTY(EditAnywhere, config, Category = "Graph Style")
	FLinearColor RootNodeColor;

	UPROPERTY(EditAnywhere, config, Category = "Graph Style")
	FLinearColor TaskNodeColor;

	UPROPERTY(EditAnywhere, config, Category = "Graph Style")
	FLinearColor PersistentTasksNodeColor;

	UPROPERTY(EditAnywhere, config, Category = "Graph Defaults", noclear, meta = (MetaClass = "QuestBranch"))
	FSoftClassPath DefaultQuestBranch;

	UPROPERTY(EditAnywhere, config, Category = "Graph Defaults", noclear, meta = (MetaClass = "QuestState"))
	FSoftClassPath DefaultQuestState;

	UPROPERTY(EditAnywhere, config, Category = "Graph Defaults", noclear, meta = (MetaClass = "QuestNodeUserWidget"))
	TSoftClassPtr<class UQuestNodeUserWidget> DefaultQuestWidgetClass;
};
