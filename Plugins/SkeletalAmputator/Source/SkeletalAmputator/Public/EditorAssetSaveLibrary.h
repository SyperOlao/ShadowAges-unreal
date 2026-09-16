// Copyright 2025 SGSAmputationLab and AO. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EditorAssetSaveLibrary.generated.h"

class UObject;
class USkeletalMesh;
class UStaticMesh;

UCLASS()
class SKELETALAMPUTATOR_API UEditorAssetSaveLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Editor Asset Save", meta = (CallInEditor = "true"))
	static UObject* SaveAssetToContentBrowser(UObject* SourceAsset, const FString& DesiredPackagePath, const FString& DesiredAssetName);

	UFUNCTION(BlueprintCallable, Category = "Editor Asset Save", meta = (CallInEditor = "true"))
	static USkeletalMesh* SaveSkeletalMeshToContentBrowser(USkeletalMesh* SourceMesh, const FString& DesiredPackagePath, const FString& DesiredAssetName);

	UFUNCTION(BlueprintCallable, Category = "Editor Asset Save", meta = (CallInEditor = "true"))
	static UStaticMesh* SaveStaticMeshToContentBrowser(UStaticMesh* SourceMesh, const FString& DesiredPackagePath, const FString& DesiredAssetName);
};
