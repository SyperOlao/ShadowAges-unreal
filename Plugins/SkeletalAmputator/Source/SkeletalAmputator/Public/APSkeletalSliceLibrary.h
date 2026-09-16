#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "APSliceableSkeletalMeshComponent.h"
#include "APSkeletalSliceLibrary.generated.h"

UCLASS()
class SKELETALAMPUTATOR_API UAPSkeletalSliceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Slice", meta = (WorldContext = "WorldContextObject", DisplayName = "Slice Meshes In Radius"))
	static TArray<FSliceResult> SliceMeshesInRadius(const UObject* WorldContextObject, const FVector& SliceCenter, const FVector& SliceNormal, float SliceRadius = 25.0f, EAPSliceSpace SliceSpace = EAPSliceSpace::World);
};
