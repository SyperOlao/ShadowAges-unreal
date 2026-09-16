#pragma once

#include "CoreMinimal.h"
#include "APSkeletalMeshGenerator.h"

struct FAPSkeletalSliceVertex
{
	FVector Position = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	FVector Tangent = FVector::ForwardVector;
	TArray<FVector2D> TextureCoordinates;
	FLinearColor Color = FLinearColor::White;
	TMap<int32, double> BoneWeights;
};

struct FAPSkeletalSliceTriangle
{
	FAPSkeletalSliceVertex Vertices[3];
	int32 MaterialIndex = 0;
	int32 SourceTriangleIndex = INDEX_NONE;
	int32 SliceCapIndex = INDEX_NONE;
};

struct FAPSkeletalSliceGeometry
{
	static bool Split(const TArray<FAPSkeletalSliceTriangle>& SourceTriangles, const FPlane& Plane, int32 CapMaterialIndex, TArray<FAPSkeletalSliceTriangle>& PositiveTriangles, TArray<FAPSkeletalSliceTriangle>& NegativeTriangles, FString& Failure, bool bAllowOpenContours = false);
	static void MakeSurfaces(const TArray<FAPSkeletalSliceTriangle>& Triangles, TArray<FAPSkeletalMeshSurface>& Surfaces);
};
