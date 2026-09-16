// Copyright 2025 SGSAmputationLab and AO. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"
#include "BoneIndices.h"
#include "Templates/Function.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/SkeletalMesh.h"

struct FSkinWeightInfo;
struct FSoftSkinVertex;


struct FRawRenderSectionInfo
{
	// source lod section index
	int32 OriginalSectionIndex;
	
	uint32 MaterialIndex = 0;
	uint32 NumVertices = 0;
	uint32 BaseIndex = 0;
	uint32 BaseVertexIndex = 0;
	uint32 NumTriangles = 0;
	uint32 MaxBoneInfluences = 0;

	// calc bonemap
	TArray<FBoneIndexType> BoneMap;
};

struct FRawLODData
{
	TArray<FSoftSkinVertex> VertexBuffer;
	TArray<uint32> IndexBuffer;

	TArray<FRawRenderSectionInfo> RenderSections;

	TMap<uint32, uint32> SourceToNewIndexMap;

	TArray<FBoneIndexType> ActiveBoneIndices;
	TArray<FBoneIndexType> RequiredBoneIndices;

	uint32 NumTexCoords = 0;
	uint32 SourceMaxBoneInfluences = 0;
	
	bool bSourceUse16BitBoneIndex= false;
	bool bHasVertexColors = false;
};

/**
 * 
 */
class SKELETALAMPUTATOR_API FAPSkeletalMeshBuilderTask : public FNonAbandonableTask
{
public:
	FAPSkeletalMeshBuilderTask(USkeletalMesh* InSourceMesh, const TSet<uint32>& InVertexIDs, TUniquePtr<FRawLODData> InResult, TUniquePtr<FSkeletalMeshLODRenderData> InRenderData, TFunction<void(TUniquePtr<FRawLODData>, TUniquePtr<FSkeletalMeshLODRenderData>)> InCompletionCallback);
	
	~FAPSkeletalMeshBuilderTask();

	void DoWork();

	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FAPSkeletalMeshBuilderTask, STATGROUP_ThreadPoolAsyncTasks);
	}

private:
	USkeletalMesh* SourceMesh;
	const TSet<uint32>& VertexIDs;

	TUniquePtr<FRawLODData> Result;
	TUniquePtr<FSkeletalMeshLODRenderData> LODRenderData;
	TFunction<void(TUniquePtr<FRawLODData>, TUniquePtr<FSkeletalMeshLODRenderData>)> CompletionCallback;
};
