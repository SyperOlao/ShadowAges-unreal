#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
struct FReferenceSkeleton;

UMaterialInterface* CreateSkeletalSliceMaterial(UObject* Owner, UMaterialInterface* Parent, const FReferenceSkeleton& Skeleton, const TArray<FTransform>& BoneTransforms, const FLinearColor& TissueColor, const FLinearColor& BoneColor, float BoneRadius, float TerminalBoneLength);
