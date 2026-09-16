#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshRenderData.h"

struct FAPSkeletalBoneInfluence
{
	int32 VertexIndex;
	int32 BoneIndex;
	float Weight;

	explicit FAPSkeletalBoneInfluence(int32 InVertexIndex = -1, int32 InBoneIndex = -1, float InWeight = 0.0f)
		: VertexIndex(InVertexIndex), BoneIndex(InBoneIndex), Weight(InWeight)
	{
	}
};

struct FAPSkeletalMeshSurface
{
	int32 MaterialIndex = 0;
	TArray<uint32> Indices;
	TArray<FVector> Vertices;
	TArray<FVector> Tangents;
	TArray<FVector> Normals;
	TArray<TArray<FVector2D>> TextureCoordinates;
	TArray<FColor> Colors;
	TArray<bool> FlipBinormalSigns;
	TArray<TArray<FAPSkeletalBoneInfluence>> BoneInfluences;
};

class FAPSkeletalMeshGenerator
{
public:
	static bool GenerateSkeletalMesh(
		USkeletalMesh* SkeletalMesh,
		const TArray<FAPSkeletalMeshSurface>& Surfaces,
		const TArray<UMaterialInterface*>& SurfacesMaterial,
		bool bNeedCPUAccess = false,
		const TMap<FName, FTransform>& BoneTransformsOverride = TMap<FName, FTransform>());

	static bool DecomposeSkeletalMesh(
		const USkeletalMesh* SkeletalMesh,
		TArray<FAPSkeletalMeshSurface>& OutSurfaces,
		TArray<int32>& OutSurfacesVertexOffsets,
		TArray<int32>& OutSurfacesIndexOffsets,
		TArray<UMaterialInterface*>& OutSurfacesMaterial);
};
