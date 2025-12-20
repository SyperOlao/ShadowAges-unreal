// Copyright Narrative Tools 2022. 

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "DialogueEditorSettings.generated.h"

/**
 * 
 */
UCLASS(config=Engine, defaultconfig)
class NARRATIVEDIALOGUEEDITOR_API UDialogueEditorSettings : public UObject
{
	GENERATED_BODY()

public:

	UDialogueEditorSettings();

	UPROPERTY(EditAnywhere, config, Category = "Graph Style")
	FLinearColor RootNodeColor;

	UPROPERTY(EditAnywhere, config, Category = "Graph Style")
	FLinearColor PlayerNodeColor;

	UPROPERTY(EditAnywhere, config, Category = "Graph Style")
	FLinearColor NPCNodeColor;

	UPROPERTY(EditAnywhere, config, Category = "Graph Defaults", noclear, meta = (MetaClass="DialogueNode_NPC"))
	FSoftClassPath DefaultNPCDialogueClass;

	UPROPERTY(EditAnywhere, config, Category = "Graph Defaults", noclear, meta = (MetaClass = "DialogueNode_Player"))
	FSoftClassPath DefaultPlayerDialogueClass;

	UPROPERTY(EditAnywhere, config, Category = "Graph Defaults", noclear, meta = (MetaClass = "Dialogue"))
	FSoftClassPath DefaultDialogueClass;

	UPROPERTY(EditAnywhere, config, Category = "Graph Defaults", noclear, meta = (MetaClass = "DialogueNodeUserWidget"))
	TSoftClassPtr<class UDialogueNodeUserWidget> DefaultDialogueWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Graph Options")
	bool bEnableWarnings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Graph Options")
	bool bWarnMissingSoundCues;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, config, Category = "Graph Options")
	bool bWarnMissingAnims;

};
