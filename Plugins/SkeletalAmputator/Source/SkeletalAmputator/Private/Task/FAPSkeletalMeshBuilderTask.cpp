// Copyright 2025 SGSAmputationLab and AO. All Rights Reserved.


#include "Task/FAPSkeletalMeshBuilderTask.h"



FAPSkeletalMeshBuilderTask::FAPSkeletalMeshBuilderTask(USkeletalMesh* InSourceMesh, const TSet<uint32>& InVertexIDs, TUniquePtr<FRawLODData> InResult, TUniquePtr<FSkeletalMeshLODRenderData> InRenderData, TFunction<void(TUniquePtr<FRawLODData>, TUniquePtr<FSkeletalMeshLODRenderData>)> InCompletionCallback)
	: SourceMesh(InSourceMesh)
	, VertexIDs(InVertexIDs)
	, Result(MoveTemp(InResult))
	, LODRenderData(MoveTemp(InRenderData))
	, CompletionCallback(InCompletionCallback)
{
}

FAPSkeletalMeshBuilderTask::~FAPSkeletalMeshBuilderTask()
{
}

void FAPSkeletalMeshBuilderTask::DoWork()
{
    // worker thread.
    
    FSkeletalMeshRenderData* SourceRenderData = SourceMesh->GetResourceForRendering();
    if (!SourceRenderData || !SourceRenderData->LODRenderData.IsValidIndex(0))
    {
        return;
    }

    FSkeletalMeshLODRenderData& SourceLODData = SourceRenderData->LODRenderData[0];
    Result->RequiredBoneIndices = SourceLODData.RequiredBones;
    
    Result->bHasVertexColors = SourceMesh->GetHasVertexColors();
    Result->NumTexCoords = SourceLODData.GetNumTexCoords();

    Result->SourceMaxBoneInfluences = SourceLODData.GetSkinWeightVertexBuffer()->GetMaxBoneInfluences();
    Result->bSourceUse16BitBoneIndex = SourceLODData.GetSkinWeightVertexBuffer()->Use16BitBoneIndex();
    
    Result->VertexBuffer.Reserve(VertexIDs.Num());
    Result->SourceToNewIndexMap.Reserve(VertexIDs.Num());

    for (int32 SectionIdx = 0; SectionIdx < SourceLODData.RenderSections.Num(); ++SectionIdx)
    {
        FSkelMeshRenderSection& SourceSection = SourceLODData.RenderSections[SectionIdx];
        
        FRawRenderSectionInfo& NewSectionInfo = Result->RenderSections.Emplace_GetRef();
        NewSectionInfo.OriginalSectionIndex = SectionIdx;
        NewSectionInfo.MaterialIndex = SourceSection.MaterialIndex;
        NewSectionInfo.MaxBoneInfluences = SourceSection.MaxBoneInfluences;
        NewSectionInfo.BaseVertexIndex = Result->VertexBuffer.Num();
        NewSectionInfo.BaseIndex = Result->IndexBuffer.Num();

        TSet<uint32> VerticesInThisSection;
        TSet<FBoneIndexType> UsedBoneIndices;
        
        for (uint32 i = 0; i < SourceSection.NumVertices; ++i)
        {
            const uint32 SourceGlobalIndex = SourceSection.BaseVertexIndex + i;
            if (VertexIDs.Contains(SourceGlobalIndex))
            {
                VerticesInThisSection.Add(SourceGlobalIndex);

                const FSkinWeightInfo& Influence = SourceLODData.GetSkinWeightVertexBuffer()->GetVertexSkinWeights(SourceGlobalIndex);
                for (int32 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES; ++InfluenceIdx)
                {
                    if (Influence.InfluenceWeights[InfluenceIdx] > 0)
                    {
                        const FBoneIndexType BoneMapIndex = Influence.InfluenceBones[InfluenceIdx];
                        if (SourceSection.BoneMap.IsValidIndex(BoneMapIndex))
                        {
                            UsedBoneIndices.Add(SourceSection.BoneMap[BoneMapIndex]);
                        }
                    }
                }
            }
        }

        NewSectionInfo.BoneMap = UsedBoneIndices.Array();
        NewSectionInfo.BoneMap.Sort();

        for (const uint32 SourceGlobalIndex : VerticesInThisSection)
        {
            const int32 NewIndex = Result->VertexBuffer.Num();
            Result->SourceToNewIndexMap.Add(SourceGlobalIndex, NewIndex);
            
            FSoftSkinVertex& Vtx = Result->VertexBuffer.Emplace_GetRef();
            
            Vtx.Position = SourceLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(SourceGlobalIndex);
            Vtx.TangentX = SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(SourceGlobalIndex);
            Vtx.TangentY = SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(SourceGlobalIndex);
            Vtx.TangentZ = SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(SourceGlobalIndex);
            
            for (uint32 UVIdx = 0; UVIdx < Result->NumTexCoords; ++UVIdx)
            {
                Vtx.UVs[UVIdx] = SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(SourceGlobalIndex, UVIdx);
            }
            if (Result->bHasVertexColors && SourceLODData.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() > SourceGlobalIndex)
            {
                Vtx.Color = SourceLODData.StaticVertexBuffers.ColorVertexBuffer.VertexColor(SourceGlobalIndex);
            }
            else
            {
                Vtx.Color = FColor::White;
            }

            const FSkinWeightInfo& SkinInfo = SourceLODData.GetSkinWeightVertexBuffer()->GetVertexSkinWeights(SourceGlobalIndex);

            FMemory::Memzero(Vtx.InfluenceBones, sizeof(Vtx.InfluenceBones));
            FMemory::Memzero(Vtx.InfluenceWeights, sizeof(Vtx.InfluenceWeights));
    
            for (int32 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES; ++InfluenceIdx)
            {
                if (SkinInfo.InfluenceWeights[InfluenceIdx] > 0)
                {
                    const FBoneIndexType SourceBoneMapIndex = SkinInfo.InfluenceBones[InfluenceIdx];
                    const FBoneIndexType SkeletonBoneIndex = SourceSection.BoneMap[SourceBoneMapIndex];

                    const int32 NewBoneMapIndex = NewSectionInfo.BoneMap.Find(SkeletonBoneIndex);

                    if (NewBoneMapIndex != INDEX_NONE)
                    {
                        Vtx.InfluenceBones[InfluenceIdx] = NewBoneMapIndex;
                        Vtx.InfluenceWeights[InfluenceIdx] = SkinInfo.InfluenceWeights[InfluenceIdx];
                    }
                }
            }
        }

        const FRawStaticIndexBuffer16or32Interface* SourceIndexBuffer = SourceLODData.MultiSizeIndexContainer.GetIndexBuffer();
        uint32 TrianglesInSection = 0;
        for (uint32 i = 0; i < SourceSection.NumTriangles; ++i)
        {
            const uint32 I0 = SourceIndexBuffer->Get(SourceSection.BaseIndex + i * 3 + 0);
            const uint32 I1 = SourceIndexBuffer->Get(SourceSection.BaseIndex + i * 3 + 1);
            const uint32 I2 = SourceIndexBuffer->Get(SourceSection.BaseIndex + i * 3 + 2);

            if (VertexIDs.Contains(I0) && VertexIDs.Contains(I1) && VertexIDs.Contains(I2))
            {
                Result->IndexBuffer.Add(Result->SourceToNewIndexMap.FindChecked(I0));
                Result->IndexBuffer.Add(Result->SourceToNewIndexMap.FindChecked(I1));
                Result->IndexBuffer.Add(Result->SourceToNewIndexMap.FindChecked(I2));
                TrianglesInSection++;
            }
        }
        NewSectionInfo.NumVertices = VerticesInThisSection.Num();
        NewSectionInfo.NumTriangles = TrianglesInSection;
    }

    // TUniquePtr<FSkeletalMeshLODRenderData>& LODData = LODRenderData;
    LODRenderData.Get()->ActiveBoneIndices.Empty();
    LODRenderData.Get()->RequiredBones.Empty();

    for ( int32 CreateIdx = 0; CreateIdx < Result->RenderSections.Num(); CreateIdx++ )
    {
    	const FRawRenderSectionInfo& SectionInfo = Result->RenderSections[CreateIdx];
        FSkelMeshRenderSection& NewSection = *new(LODRenderData.Get()->RenderSections) FSkelMeshRenderSection;
    	
        NewSection.MaterialIndex = SectionInfo.MaterialIndex;
        NewSection.BaseIndex = SectionInfo.BaseIndex;
        NewSection.NumTriangles = SectionInfo.NumTriangles;
        NewSection.BaseVertexIndex = SectionInfo.BaseVertexIndex;
        NewSection.NumVertices = SectionInfo.NumVertices;
    	NewSection.BoneMap = SectionInfo.BoneMap;
    	NewSection.MaxBoneInfluences = Result->SourceMaxBoneInfluences;

    	for ( int32 Idx = 0; Idx < SectionInfo.BoneMap.Num(); Idx++ )
    	{
    		FBoneIndexType BoneIndex = SectionInfo.BoneMap[Idx];
    		LODRenderData.Get()->ActiveBoneIndices.AddUnique(BoneIndex);
    	}
    	

    	if (SourceLODData.RenderSections[CreateIdx].DuplicatedVerticesBuffer.bHasOverlappingVertices)
    	{
    		NewSection.DuplicatedVerticesBuffer.bHasOverlappingVertices = true;
    	}
    	else
    	{
    		NewSection.DuplicatedVerticesBuffer.bHasOverlappingVertices = false;
    	}
    }

	
    const int32 NumVertices = Result->VertexBuffer.Num();
    LODRenderData.Get()->StaticVertexBuffers.PositionVertexBuffer.Init(NumVertices);
    LODRenderData.Get()->StaticVertexBuffers.StaticMeshVertexBuffer.Init(NumVertices, Result->NumTexCoords);

    TArray<FColor> Colors;
    TArray<FSkinWeightInfo> SkinWeights;
    if (Result->bHasVertexColors) Colors.Reserve(NumVertices);
    SkinWeights.SetNum(NumVertices);

    for (int32 i = 0; i < NumVertices; ++i)
    {
        const FSoftSkinVertex& SourceVertex = Result->VertexBuffer[i];
        LODRenderData.Get()->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(i) = SourceVertex.Position;
    	const FVector3f TangentY = FVector3f::CrossProduct(SourceVertex.TangentZ, SourceVertex.TangentX).GetSafeNormal() * SourceVertex.TangentZ.W;
    	LODRenderData.Get()->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(i, SourceVertex.TangentX, TangentY, SourceVertex.TangentZ);

        for (uint32 j = 0; j < Result->NumTexCoords; ++j)
        {
            LODRenderData.Get()->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(i, j, SourceVertex.UVs[j]);
        }
        
        if (Result->bHasVertexColors) Colors.Add(SourceVertex.Color);
        
        FSkinWeightInfo& Info = SkinWeights[i];
    	FMemory::Memzero(&Info, sizeof(FSkinWeightInfo));
        FMemory::Memcpy(Info.InfluenceBones, SourceVertex.InfluenceBones, sizeof(Info.InfluenceBones));
        FMemory::Memcpy(Info.InfluenceWeights, SourceVertex.InfluenceWeights, sizeof(Info.InfluenceWeights));
    }
	

    if (Result->bHasVertexColors) LODRenderData.Get()->StaticVertexBuffers.ColorVertexBuffer.InitFromColorArray(Colors);
	LODRenderData.Get()->SkinWeightVertexBuffer.SetMaxBoneInfluences(Result->SourceMaxBoneInfluences);
	LODRenderData.Get()->SkinWeightVertexBuffer.SetUse16BitBoneIndex(Result->bSourceUse16BitBoneIndex);

	LODRenderData.Get()->SkinWeightVertexBuffer = SkinWeights;
	
    const uint8 DataTypeSize = SourceMesh->GetResourceForRendering()->LODRenderData[0].MultiSizeIndexContainer.GetDataTypeSize();
    LODRenderData.Get()->MultiSizeIndexContainer.RebuildIndexBuffer(DataTypeSize, Result->IndexBuffer);

	LODRenderData.Get()->ActiveBoneIndices.Sort();
	LODRenderData.Get()->RequiredBones = SourceLODData.RequiredBones;
	LODRenderData.Get()->RequiredBones.Sort();

    AsyncTask(ENamedThreads::GameThread, [Callback = CompletionCallback, Res = MoveTemp(Result), ResultLODData = MoveTemp(LODRenderData)]() mutable
    {
        Callback(MoveTemp(Res), MoveTemp(ResultLODData));
    });
}
