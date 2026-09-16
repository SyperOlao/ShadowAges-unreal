#include "APSkeletalMeshGenerator.h"
#include "Engine/SkeletalMeshLODSettings.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Kismet/KismetSystemLibrary.h"

bool FAPSkeletalMeshGenerator::GenerateSkeletalMesh(
	USkeletalMesh* SkeletalMesh,
	const TArray<FAPSkeletalMeshSurface>& Surfaces,
	const TArray<UMaterialInterface*>& SurfacesMaterial,
	const bool bNeedCPUAccess,
	const TMap<FName, FTransform>& BoneTransformsOverride)
{
	
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_GenerateMesh);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_FlushRenderingCommands);
		FlushRenderingCommands();
	}

	constexpr int32 LODIndex = 0;

#if WITH_EDITORONLY_DATA
	FSkeletalMeshImportData ImportedModelData;
#endif

	TArray<uint32> SurfaceVertexOffsets;
	TArray<uint32> SurfaceIndexOffsets;
	SurfaceVertexOffsets.SetNum(Surfaces.Num());
	SurfaceIndexOffsets.SetNum(Surfaces.Num());

	bool bUse16BitBoneIndex = false;
	int32 MaxBoneInfluences = 0;
	int32 UVCount = 0;
	if (!Surfaces.IsEmpty() && !Surfaces[0].TextureCoordinates.IsEmpty())
	{
		UVCount = Surfaces[0].TextureCoordinates[0].Num();
	}

	TArray<FStaticMeshBuildVertex> StaticVertices;
	TArray<FVector> Vertices;
	TArray<uint32> Indices;
	TArray<uint32> VertexSurfaceIndex;
	{
		int MaxBoneIndex = 0;
		
		uint32 VerticesCount = 0;
		uint32 IndicesCount = 0;
		for (const FAPSkeletalMeshSurface& Surface : Surfaces)
		{
			VerticesCount += Surface.Vertices.Num();
			IndicesCount += Surface.Indices.Num();

			for (const auto& Influences : Surface.BoneInfluences)
			{
				MaxBoneInfluences = FMath::Max(Influences.Num(), MaxBoneInfluences);
				for (const auto& Influence : Influences)
				{
					MaxBoneIndex = FMath::Max(Influence.BoneIndex, MaxBoneIndex);
				}
			}

#if WITH_EDITOR

			for (const TArray<FVector2D> TextureCoordinates : Surface.TextureCoordinates)
			{
				check(UVCount == TextureCoordinates.Num());
			}
#endif
		}

		bUse16BitBoneIndex = MaxBoneIndex <= MAX_uint16;

		StaticVertices.SetNum(VerticesCount);
		Vertices.SetNum(VerticesCount);
		VertexSurfaceIndex.SetNum(VerticesCount);
		Indices.SetNum(IndicesCount);

		uint32 VerticesOffset = 0;
		uint32 IndicesOffset = 0;
		for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); SurfaceIndex++)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_PackSurface);
		const FAPSkeletalMeshSurface& Surface = Surfaces[SurfaceIndex];

			for (int VertexIndex = 0; VertexIndex < Surface.Vertices.Num(); VertexIndex += 1)
			{
				if (Surface.Colors.Num() > 0)
				{
					StaticVertices[VerticesOffset + VertexIndex].Color = Surface.Colors[VertexIndex];
				}
				StaticVertices[VerticesOffset + VertexIndex].Position = FVector3f(Surface.Vertices[VertexIndex]);
				StaticVertices[VerticesOffset + VertexIndex].TangentX = FVector3f(Surface.Tangents[VertexIndex]);
				StaticVertices[VerticesOffset + VertexIndex].TangentY = FVector3f(FVector::CrossProduct(Surface.Normals[VertexIndex], Surface.Tangents[VertexIndex]) * (1.0 - 2.0 * double(Surface.FlipBinormalSigns[VertexIndex])));
				StaticVertices[VerticesOffset + VertexIndex].TangentZ = FVector3f(Surface.Normals[VertexIndex]);
				for(int32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
				{
					StaticVertices[VerticesOffset + VertexIndex].UVs[UVIndex] = FVector2f(Surface.TextureCoordinates[VertexIndex][UVIndex]);
				}
				VertexSurfaceIndex[VerticesOffset + VertexIndex] = SurfaceIndex;
			}

			FMemory::Memcpy(
				Vertices.GetData() + VerticesOffset,
				Surface.Vertices.GetData(),
				sizeof(FVector) * Surface.Vertices.Num());

			for (int32 IndicesIndex = 0; IndicesIndex < Surface.Indices.Num(); IndicesIndex++)
			{
				Indices[IndicesOffset + IndicesIndex] = Surface.Indices[IndicesIndex] + VerticesOffset;
			}

			SurfaceVertexOffsets[SurfaceIndex] = VerticesOffset;
			VerticesOffset += Surface.Vertices.Num();

			SurfaceIndexOffsets[SurfaceIndex] = IndicesOffset;
			IndicesOffset += Surface.Indices.Num();
		}
	}

#if WITH_EDITORONLY_DATA
	
	ImportedModelData.Points.Empty();
	ImportedModelData.Points.Append(Vertices);

	ImportedModelData.PointToRawMap.AddUninitialized(ImportedModelData.Points.Num());
	for (int32 ElementIndex = 0; ElementIndex < ImportedModelData.Points.Num(); ElementIndex++)
	{
		ImportedModelData.PointToRawMap[ElementIndex] = ElementIndex;
	}
	check(ImportedModelData.PointToRawMap.Num() == Vertices.Num());

	for (const auto& Surface : Surfaces)
	{
		
		if (Surface.MaterialIndex >= 0 && Surface.MaterialIndex < SurfacesMaterial.Num())
		{
			if (Surface.MaterialIndex >= ImportedModelData.Materials.Num())
			{
				SkeletalMeshImportData::FMaterial& NewMaterial = ImportedModelData.Materials.AddDefaulted_GetRef();
				NewMaterial.Material = SurfacesMaterial[Surface.MaterialIndex];
				NewMaterial.MaterialImportName = SurfacesMaterial[Surface.MaterialIndex]->GetFullName();
			}
		}
	}

	ImportedModelData.Faces.SetNum(Indices.Num() / 3);
	for (int32 FaceIndex = 0; FaceIndex < ImportedModelData.Faces.Num(); FaceIndex += 1)
	{
		SkeletalMeshImportData::FTriangle& Triangle = ImportedModelData.Faces[FaceIndex];

		const int32 VertexIndex0 = Indices[FaceIndex * 3 + 0];
		const int32 VertexIndex1 = Indices[FaceIndex * 3 + 1];
		const int32 VertexIndex2 = Indices[FaceIndex * 3 + 2];
		Triangle.WedgeIndex[0] = FaceIndex * 3 + 0;
		Triangle.WedgeIndex[1] = FaceIndex * 3 + 1;
		Triangle.WedgeIndex[2] = FaceIndex * 3 + 2;

		Triangle.TangentX[0] = StaticVertices[VertexIndex0].TangentX;
		Triangle.TangentY[0] = StaticVertices[VertexIndex0].TangentY;
		Triangle.TangentZ[0] = StaticVertices[VertexIndex0].TangentZ;

		Triangle.TangentX[1] = StaticVertices[VertexIndex1].TangentX;
		Triangle.TangentY[1] = StaticVertices[VertexIndex1].TangentY;
		Triangle.TangentZ[1] = StaticVertices[VertexIndex1].TangentZ;

		Triangle.TangentX[2] = StaticVertices[VertexIndex2].TangentX;
		Triangle.TangentY[2] = StaticVertices[VertexIndex2].TangentY;
		Triangle.TangentZ[2] = StaticVertices[VertexIndex2].TangentZ;

		Triangle.MatIndex = VertexSurfaceIndex[VertexIndex0];
		Triangle.AuxMatIndex = 0;
		Triangle.SmoothingGroups = 1; 
	}

	ImportedModelData.Wedges.SetNum(ImportedModelData.Faces.Num() * 3);
	for (int32 FaceIndex = 0; FaceIndex < ImportedModelData.Faces.Num(); FaceIndex += 1)
	{
		for (int32 ElementIndex = 0; ElementIndex < 3; ElementIndex += 1)
		{
			const int32 WedgeIndex = FaceIndex * 3 + ElementIndex;
			const int32 VertexIndex = Indices[WedgeIndex];

			ImportedModelData.Wedges[WedgeIndex].VertexIndex = VertexIndex;
			for (int32 UVIndex = 0; UVIndex < FMath::Min<int32>(MAX_TEXCOORDS, MAX_STATIC_TEXCOORDS); ++UVIndex)
			{
				ImportedModelData.Wedges[WedgeIndex].UVs[UVIndex] = StaticVertices[VertexIndex].UVs[UVIndex];
			}
			ImportedModelData.Wedges[WedgeIndex].MatIndex = VertexSurfaceIndex[VertexIndex];
			ImportedModelData.Wedges[WedgeIndex].Color = StaticVertices[VertexIndex].Color;
			ImportedModelData.Wedges[WedgeIndex].Reserved = 0;
		}
	}

	{
		const int32 BoneNum = SkeletalMesh->GetSkeleton()->GetReferenceSkeleton().GetRawBoneNum();
		SkeletalMeshImportData::FBone DefaultBone;
		DefaultBone.Name = FString(TEXT(""));
		DefaultBone.Flags = 0;
		DefaultBone.NumChildren = 0;
		DefaultBone.ParentIndex = INDEX_NONE;
		DefaultBone.BonePos.Transform.SetIdentity();
		DefaultBone.BonePos.Length = 0.0;
		DefaultBone.BonePos.XSize = 1.0;
		DefaultBone.BonePos.YSize = 1.0;
		DefaultBone.BonePos.ZSize = 1.0;
		ImportedModelData.RefBonesBinary.Init(DefaultBone, BoneNum);
		for (int32 ElementIndex = 0; ElementIndex < BoneNum; ElementIndex += 1)
		{
			ImportedModelData.RefBonesBinary[ElementIndex].Name = SkeletalMesh->GetSkeleton()->GetReferenceSkeleton().GetBoneName(ElementIndex).ToString();
			ImportedModelData.RefBonesBinary[ElementIndex].ParentIndex = SkeletalMesh->GetSkeleton()->GetReferenceSkeleton().GetParentIndex(ElementIndex);
			if (ImportedModelData.RefBonesBinary[ElementIndex].ParentIndex != INDEX_NONE)
			{
				
				ImportedModelData.RefBonesBinary[ImportedModelData.RefBonesBinary[ElementIndex].ParentIndex].NumChildren += 1;
			}
		}

		for (int32 ElementIndex = 0; ElementIndex < BoneNum; ElementIndex += 1)
		{
			
			const FTransform* TransformOverride = BoneTransformsOverride.Find(SkeletalMesh->GetSkeleton()->GetReferenceSkeleton().GetBoneName(ElementIndex));
			if (TransformOverride != nullptr)
			{
				
				ImportedModelData.RefBonesBinary[ElementIndex].BonePos.Transform = FTransform3f(*TransformOverride);
			}
			else
			{
				ImportedModelData.RefBonesBinary[ElementIndex].BonePos.Transform = FTransform3f(SkeletalMesh->GetSkeleton()->GetReferenceSkeleton().GetRawRefBonePose()[ElementIndex]);
			}
			
			ImportedModelData.RefBonesBinary[ElementIndex].BonePos.Length = ImportedModelData.RefBonesBinary[ElementIndex].BonePos.Transform.GetLocation().Size();
		}
	}
#endif

	check(MaxBoneInfluences <= MAX_TOTAL_INFLUENCES);

	check(UVCount <= MAX_STATIC_TEXCOORDS);

	SkeletalMesh->AllocateResourceForRendering();
	FSkeletalMeshRenderData* MeshRenderData = SkeletalMesh->GetResourceForRendering();

	auto LODMeshRenderData = new FSkeletalMeshLODRenderData;
	if (!LODMeshRenderData)
		return false;
	MeshRenderData->LODRenderData.Add(LODMeshRenderData);

	SkeletalMesh->ResetLODInfo();
	FSkeletalMeshLODInfo& MeshLodInfo = SkeletalMesh->AddLODInfo();
	
	MeshLodInfo.LODHysteresis = 0.02f;
	MeshLodInfo.ScreenSize = 1.0;
	MeshLodInfo.bAllowCPUAccess = bNeedCPUAccess;
	if(bNeedCPUAccess)
	{
		MeshLodInfo.SkinCacheUsage = ESkinCacheUsage::Disabled;
		MeshLodInfo.bHasBeenSimplified = true;
	}

	const FBox BoundingBox(Vertices.GetData(), Vertices.Num());
	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(BoundingBox));

#if WITH_EDITORONLY_DATA
	FSkeletalMeshLODModel* SkeletalMeshLODModel = new FSkeletalMeshLODModel();
	SkeletalMesh->GetImportedModel()->LODModels.Add(SkeletalMeshLODModel);

	SkeletalMeshLODModel->NumVertices = Vertices.Num();
	SkeletalMeshLODModel->NumTexCoords = UVCount;

	SkeletalMeshLODModel->Sections.SetNum(Surfaces.Num());
	SkeletalMeshLODModel->MaxImportVertex = Vertices.Num() - 1;

	ImportedModelData.NumTexCoords = UVCount;
	ImportedModelData.MaxMaterialIndex = Surfaces.Num() - 1;
	ImportedModelData.bHasVertexColors = Surfaces[0].Colors.Num() > 0;;
	ImportedModelData.bHasNormals = true;
	ImportedModelData.bHasTangents = true;
#endif

	LODMeshRenderData->RenderSections.SetNum(Surfaces.Num());

	for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); SurfaceIndex++)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_PackSurface);
		const FAPSkeletalMeshSurface& Surface = Surfaces[SurfaceIndex];
		FSkelMeshRenderSection& RenderSection = LODMeshRenderData->RenderSections[SurfaceIndex];

		RenderSection.bDisabled = false;
		RenderSection.BaseVertexIndex = SurfaceVertexOffsets[SurfaceIndex];
		RenderSection.NumVertices = Surface.Vertices.Num();
		RenderSection.BaseIndex = SurfaceIndexOffsets[SurfaceIndex];
		RenderSection.NumTriangles = Surface.Indices.Num() / 3;
		RenderSection.MaterialIndex = Surfaces[SurfaceIndex].MaterialIndex;
		RenderSection.bCastShadow = true;
		RenderSection.bRecomputeTangent = false;
		RenderSection.MaxBoneInfluences = MaxBoneInfluences;

#if WITH_EDITOR
		FSkelMeshSection& MeshSection = SkeletalMeshLODModel->Sections[SurfaceIndex];
		MeshSection.bDisabled = RenderSection.bDisabled;
		MeshSection.bRecomputeTangent = RenderSection.bRecomputeTangent;
		MeshSection.bCastShadow = RenderSection.bCastShadow;
		MeshSection.BaseVertexIndex = RenderSection.BaseVertexIndex;
		MeshSection.BaseIndex = RenderSection.BaseIndex;
		MeshSection.MaterialIndex = RenderSection.MaterialIndex;
		MeshSection.NumVertices = RenderSection.NumVertices;
		MeshSection.NumTriangles = RenderSection.NumTriangles;
		MeshSection.MaxBoneInfluences = RenderSection.MaxBoneInfluences;
		MeshSection.bUse16BitBoneIndex = bUse16BitBoneIndex;
		MeshSection.OriginalDataSectionIndex = SurfaceIndex; 

		MeshSection.SoftVertices.SetNum(Surface.Vertices.Num());
		for (int32 VertexIndex = 0; VertexIndex < Surface.Vertices.Num(); VertexIndex += 1)
		{
			MeshSection.SoftVertices[VertexIndex].Position = FVector3f(Surface.Vertices[VertexIndex]);
			MeshSection.SoftVertices[VertexIndex].TangentX = FVector3f(Surface.Tangents[VertexIndex]);
			MeshSection.SoftVertices[VertexIndex].TangentY = FVector3f(FVector::CrossProduct(Surface.Normals[VertexIndex], Surface.Tangents[VertexIndex]) * (1.0 - 2.0 * double(Surface.FlipBinormalSigns[VertexIndex])));
			MeshSection.SoftVertices[VertexIndex].TangentZ = FVector3f(Surface.Normals[VertexIndex]);
			for (int32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
			{
				MeshSection.SoftVertices[VertexIndex].UVs[UVIndex] = FVector2f(Surface.TextureCoordinates[VertexIndex][UVIndex]);
			}
			if (Surface.Colors.Num() > VertexIndex)
			{
				MeshSection.SoftVertices[VertexIndex].Color = Surface.Colors[VertexIndex];
			}

			const TArray<FAPSkeletalBoneInfluence>& VertInfluences = Surface.BoneInfluences[VertexIndex];
			
			FMemory::Memset(MeshSection.SoftVertices[VertexIndex].InfluenceWeights, 0, sizeof(MeshSection.SoftVertices[VertexIndex].InfluenceWeights));
			FMemory::Memset(MeshSection.SoftVertices[VertexIndex].InfluenceBones, 0, sizeof(MeshSection.SoftVertices[VertexIndex].InfluenceBones));

			int MaxVertInfluencesNum = FMath::Min(VertInfluences.Num(), MAX_TOTAL_INFLUENCES);
			for (int InfluenceIndex = 0; InfluenceIndex < MaxVertInfluencesNum; InfluenceIndex += 1)
			{
				const FAPSkeletalBoneInfluence& VertInfluence = VertInfluences[InfluenceIndex];
				
				check(VertexIndex == VertInfluence.VertexIndex);

				const uint16 EncodedWeight = FMath::Clamp(VertInfluence.Weight, 0., 1.) * 65535.;

				MeshSection.SoftVertices[VertexIndex].InfluenceWeights[InfluenceIndex] = EncodedWeight;
				MeshSection.SoftVertices[VertexIndex].InfluenceBones[InfluenceIndex] = 0;
					if (EncodedWeight != 0)
					{
						MeshSection.SoftVertices[VertexIndex].InfluenceBones[InfluenceIndex] = VertInfluence.BoneIndex;
					}
			}
		}

		{

			FSkelMeshSourceSectionUserData& UserSectionData = SkeletalMeshLODModel->UserSectionsData.FindOrAdd(SurfaceIndex);
			UserSectionData.bDisabled = MeshSection.bDisabled;
			UserSectionData.bCastShadow = MeshSection.bCastShadow;
			UserSectionData.bRecomputeTangent = MeshSection.bRecomputeTangent;
			UserSectionData.RecomputeTangentsVertexMaskChannel = MeshSection.RecomputeTangentsVertexMaskChannel;
			UserSectionData.GenerateUpToLodIndex = MeshSection.GenerateUpToLodIndex;

			const bool IsRenderDataInSync =
				UserSectionData.bDisabled == RenderSection.bDisabled &&
				UserSectionData.bCastShadow == RenderSection.bCastShadow &&
				UserSectionData.bRecomputeTangent == RenderSection.bRecomputeTangent &&
				UserSectionData.RecomputeTangentsVertexMaskChannel == RenderSection.RecomputeTangentsVertexMaskChannel &&
				UserSectionData.CorrespondClothAssetIndex == RenderSection.CorrespondClothAssetIndex &&
				UserSectionData.ClothingData.AssetGuid == RenderSection.ClothingData.AssetGuid &&
				UserSectionData.ClothingData.AssetLodIndex == RenderSection.ClothingData.AssetLodIndex;

			check(IsRenderDataInSync); 
		}
#endif

		{
			RenderSection.DuplicatedVerticesBuffer.DupVertData.ResizeBuffer(1);
			uint8* VertData = RenderSection.DuplicatedVerticesBuffer.DupVertData.GetDataPointer();
			FMemory::Memzero(VertData, sizeof(uint32) * RenderSection.DuplicatedVerticesBuffer.DupVertData.Num());

			RenderSection.DuplicatedVerticesBuffer.DupVertIndexData.ResizeBuffer(RenderSection.NumVertices);
			uint8* IndexData = RenderSection.DuplicatedVerticesBuffer.DupVertIndexData.GetDataPointer();
			FMemory::Memzero(IndexData, RenderSection.NumVertices * sizeof(FIndexLengthPair));
		}
	}

	{
#if WITH_EDITOR
		SkeletalMeshLODModel->IndexBuffer = Indices;
#endif

		uint8 IndexElementSize = sizeof(uint32);
	if (Indices.Num() < MAX_uint16)
	{
		IndexElementSize = sizeof(uint16);
	}
	LODMeshRenderData->MultiSizeIndexContainer.RebuildIndexBuffer(
			
			IndexElementSize,
			Indices);

		TArray<uint32> ActualIndexBuffer;
		LODMeshRenderData->MultiSizeIndexContainer.GetIndexBuffer(ActualIndexBuffer);
		check(ActualIndexBuffer.Num() == Indices.Num());
	}

	LODMeshRenderData->StaticVertexBuffers.PositionVertexBuffer.Init(
		StaticVertices,
		bNeedCPUAccess);
	LODMeshRenderData->StaticVertexBuffers.ColorVertexBuffer.Init(
		StaticVertices,
		bNeedCPUAccess);
	FConstMeshBuildVertexView VertexView = MakeConstMeshBuildVertexView(StaticVertices);
	VertexView.UVs.SetNum(UVCount);
	LODMeshRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
	LODMeshRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.Init(VertexView, bNeedCPUAccess);

	LODMeshRenderData->SkinWeightVertexBuffer.SetMaxBoneInfluences(MaxBoneInfluences);
	LODMeshRenderData->SkinWeightVertexBuffer.SetUse16BitBoneIndex(bUse16BitBoneIndex);

	TArray<FSkinWeightInfo> Weights;
	Weights.SetNum(Vertices.Num());

	for (int WeightIndex = 0; WeightIndex < Weights.Num(); WeightIndex++)
	{
		for (int InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
		{
			Weights[WeightIndex].InfluenceBones[InfluenceIndex] = 0;
			Weights[WeightIndex].InfluenceWeights[InfluenceIndex] = 0;
		}
	}

	for (int32 SurfacesIndex = 0; SurfacesIndex < Surfaces.Num(); SurfacesIndex++)
	{
		const FAPSkeletalMeshSurface& Surface = Surfaces[SurfacesIndex];

		for (int32 LocalVertexIndex = 0; LocalVertexIndex < Surface.BoneInfluences.Num(); LocalVertexIndex += 1)
		{
			const TArray<FAPSkeletalBoneInfluence>& VertInfluences = Surface.BoneInfluences[LocalVertexIndex];
			const int32 VertexIndex = SurfaceVertexOffsets[SurfacesIndex] + LocalVertexIndex;
			FSkinWeightInfo& Weight = Weights[VertexIndex];

			for (int InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; InfluenceIndex++)
			{
				if (InfluenceIndex >= VertInfluences.Num())
				{

					Weight.InfluenceWeights[InfluenceIndex] = 0;
					Weight.InfluenceBones[InfluenceIndex] = 0;
				}
				else
				{
					const FAPSkeletalBoneInfluence& VertInfluence = VertInfluences[InfluenceIndex];
					
					check(LocalVertexIndex == VertInfluence.VertexIndex);

					if (!SkeletalMesh->GetSkeleton()->GetReferenceSkeleton().IsValidIndex(VertInfluence.BoneIndex))
					{
						
						UKismetSystemLibrary::PrintString(nullptr, FString::Format(TEXT("SkeletalAmputator: bone {0} is absent from the generated skeleton."), {VertInfluence.BoneIndex}));
						continue;
					}

					const uint16 EncodedWeight = FMath::Clamp(VertInfluence.Weight, 0., 1.) * 65535;
					Weight.InfluenceWeights[InfluenceIndex] = EncodedWeight;
					Weight.InfluenceBones[InfluenceIndex] = 0;
					if (EncodedWeight != 0)
					{
						Weight.InfluenceBones[InfluenceIndex] = VertInfluence.BoneIndex;
					}

#if WITH_EDITORONLY_DATA
					if (Weight.InfluenceBones[InfluenceIndex] != INDEX_NONE)
					{
						SkeletalMeshImportData::FRawBoneInfluence& Influence = ImportedModelData.Influences.AddDefaulted_GetRef();
						Influence.Weight = static_cast<float>(FMath::Clamp(Weight.InfluenceWeights[InfluenceIndex] / 65535.0, 0.0, 1.0));
						Influence.BoneIndex = Weight.InfluenceBones[InfluenceIndex];
						Influence.VertexIndex = VertexIndex;
					}
#endif
				}
				
			}
			
		}
	}

#if WITH_EDITOR
	SkeletalMeshLODModel->ActiveBoneIndices.Empty();
	SkeletalMeshLODModel->RequiredBones.Empty();
#endif
	LODMeshRenderData->RequiredBones.Empty();
	LODMeshRenderData->ActiveBoneIndices.Empty();

	for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); SurfaceIndex++)
	{
		FSkelMeshRenderSection& RenderSection = LODMeshRenderData->RenderSections[SurfaceIndex];
		RenderSection.BoneMap.Empty();
#if WITH_EDITOR
		FSkelMeshSection& MeshSection = SkeletalMeshLODModel->Sections[SurfaceIndex];
		MeshSection.BoneMap.Empty();
#endif
	}

	const int32 BoneNum = SkeletalMesh->GetSkeleton()->GetReferenceSkeleton().GetRawBoneNum();
	for (int32 BoneIndex = 0; BoneIndex < BoneNum; BoneIndex++)
	{
#if WITH_EDITOR
		SkeletalMeshLODModel->ActiveBoneIndices.AddUnique(BoneIndex);
		SkeletalMeshLODModel->RequiredBones.AddUnique(BoneIndex);
#endif
		LODMeshRenderData->RequiredBones.AddUnique(BoneIndex);
		LODMeshRenderData->ActiveBoneIndices.AddUnique(BoneIndex);

		for (int32 SurfaceIndex = 0; SurfaceIndex < Surfaces.Num(); SurfaceIndex++)
		{
			FSkelMeshRenderSection& RenderSection = LODMeshRenderData->RenderSections[SurfaceIndex];
			RenderSection.BoneMap.AddUnique(BoneIndex);
#if WITH_EDITOR
			FSkelMeshSection& MeshSection = SkeletalMeshLODModel->Sections[SurfaceIndex];
			MeshSection.BoneMap.AddUnique(BoneIndex);
#endif
		}
	}

	LODMeshRenderData->SkinWeightVertexBuffer.SetNeedsCPUAccess(bNeedCPUAccess);
	LODMeshRenderData->SkinWeightVertexBuffer = Weights;

	SkeletalMesh->GetMaterials().Reserve(SurfacesMaterial.Num());
	for (auto& Material : SurfacesMaterial)
	{
		SkeletalMesh->GetMaterials().Emplace(Material);
	}

	SkeletalMesh->GetRefBasesInvMatrix().Empty();
	SkeletalMesh->CalculateInvRefMatrices(); 
	MeshRenderData->bReadyForStreaming = false;

	if (!GIsEditor)
	{
		SkeletalMesh->NeverStream = false;
	}
	if(bNeedCPUAccess)
	{
		SkeletalMesh->NeverStream = true;
	}

#if WITH_EDITOR
	if (SkeletalMesh->GetLODSettings() != nullptr)
	{
		
		SkeletalMesh->GetLODSettings()->SetLODSettingsFromMesh(SkeletalMesh);

		checkf(SkeletalMesh->GetLODSettings() != nullptr, TEXT("At this point the LODSetings are supposed to be set."));

		const int32 NumSettings = FMath::Min(SkeletalMesh->GetLODSettings()->GetNumberOfSettings(), SkeletalMesh->GetLODNum());
		checkf(LODIndex < NumSettings, TEXT("Make sure the LODSettings are set for the LODIndex 0."));

		const FSkeletalMeshLODGroupSettings* SkeletalMeshLODGroupSettings = &SkeletalMesh->GetLODSettings()->GetSettingsForLODLevel(LODIndex);
		MeshLodInfo.BuildGUID = MeshLodInfo.ComputeDeriveDataCacheKey(SkeletalMeshLODGroupSettings);
	}

	const FString BuildStringID = SkeletalMesh->GetImportedModel()->LODModels[0].GetLODModelDeriveDataKey();
	SkeletalMesh->GetImportedModel()->LODModels[0].BuildStringID = BuildStringID;

	SkeletalMesh->SetLODImportedDataVersions(0, ESkeletalMeshGeoImportVersions::LatestVersion, ESkeletalMeshSkinningImportVersions::LatestVersion);
	SkeletalMesh->SaveLODImportedData(0, ImportedModelData);
	SkeletalMesh->InvalidateDeriveDataCacheGUID();
#endif

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_InitializeMeshResources);
		SkeletalMesh->PostLoad();
	}

#if WITH_EDITOR

#endif
	return true;
}

bool FAPSkeletalMeshGenerator::DecomposeSkeletalMesh(
	
	const USkeletalMesh* SkeletalMesh,
	
	TArray<FAPSkeletalMeshSurface>& OutSurfaces,
	
	TArray<int32>& OutSurfacesVertexOffsets,
	
	TArray<int32>& OutSurfacesIndexOffsets,
	
	TArray<UMaterialInterface*>& OutSurfacesMaterial)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_DecomposeMesh);
	OutSurfaces.Empty();
	OutSurfacesVertexOffsets.Empty();
	OutSurfacesIndexOffsets.Empty();
	OutSurfacesMaterial.Empty();

	constexpr int32 LODIndex = 0;

	const int32 RenderSectionsNum = SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex].RenderSections.Num();;

	OutSurfaces.SetNum(RenderSectionsNum);
	OutSurfacesVertexOffsets.SetNum(RenderSectionsNum);
	OutSurfacesIndexOffsets.SetNum(RenderSectionsNum);

	const FSkeletalMeshLODRenderData& RenderData = SkeletalMesh->GetResourceForRendering()->LODRenderData[LODIndex];

	TArray<uint32> IndexBuffer;
	RenderData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

	for (int32 SectionIndex = 0; SectionIndex < RenderSectionsNum; SectionIndex += 1)
	{
		FAPSkeletalMeshSurface& Surface = OutSurfaces[SectionIndex];

		const FSkelMeshRenderSection& RenderSection = RenderData.RenderSections[SectionIndex];
		Surface.MaterialIndex = RenderSection.MaterialIndex;

		const uint32 VertexIndexOffset = RenderSection.BaseVertexIndex;
		const uint32 VertexNum = RenderSection.NumVertices;

		OutSurfacesVertexOffsets[SectionIndex] = VertexIndexOffset;

		Surface.Vertices.SetNum(VertexNum);
		Surface.Normals.SetNum(VertexNum);
		Surface.Tangents.SetNum(VertexNum);
		Surface.FlipBinormalSigns.SetNum(VertexNum);
		Surface.TextureCoordinates.SetNum(VertexNum);
		Surface.Colors.SetNum(VertexNum);
		Surface.BoneInfluences.SetNum(VertexNum);

		for (uint32 ElementIndex = 0; ElementIndex < VertexNum; ElementIndex += 1)
		{
			const uint32 VertexIndex = VertexIndexOffset + ElementIndex;

			Surface.Vertices[ElementIndex] = FVector(RenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));

			Surface.Normals[ElementIndex] = FVector(RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex));
			Surface.Tangents[ElementIndex] = FVector(RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex));
			
			const FVector& ActualBinormal = FVector(RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex));
			const FVector CalculatedBinormal = FVector::CrossProduct(Surface.Normals[ElementIndex], Surface.Tangents[ElementIndex]);

			Surface.FlipBinormalSigns[ElementIndex] = FVector::DotProduct(ActualBinormal, CalculatedBinormal) < 0.99;

			Surface.TextureCoordinates[ElementIndex].SetNum(RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());
			for (uint32 UVIndex = 0; UVIndex < RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords(); UVIndex += 1)
			{
				Surface.TextureCoordinates[ElementIndex][UVIndex] = FVector2d(RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, UVIndex));
			}

			if (VertexIndex < RenderData.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices())
			{
				
				Surface.Colors[ElementIndex] = RenderData.StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertexIndex);
			}

			checkf(static_cast<int32>(RenderData.SkinWeightVertexBuffer.GetMaxBoneInfluences()) >= RenderSection.MaxBoneInfluences, TEXT("These two MUST be the same."));

			Surface.BoneInfluences[ElementIndex].SetNum(RenderSection.MaxBoneInfluences);
			for (
				int32 BoneInfluenceIndex = 0;
				BoneInfluenceIndex < RenderSection.MaxBoneInfluences;
				BoneInfluenceIndex += 1)
			{
				Surface.BoneInfluences[ElementIndex][BoneInfluenceIndex].VertexIndex = ElementIndex;
				Surface.BoneInfluences[ElementIndex][BoneInfluenceIndex].BoneIndex =
					RenderSection.BoneMap[RenderData.SkinWeightVertexBuffer.GetBoneIndex(VertexIndex, BoneInfluenceIndex)];
				Surface.BoneInfluences[ElementIndex][BoneInfluenceIndex].Weight =
					static_cast<float>(FMath::Clamp(RenderData.SkinWeightVertexBuffer.GetBoneWeight(VertexIndex, BoneInfluenceIndex) / 65535.0, 0.0, 1.0));
			}
		}

		const uint32 IndexIndexOffset = RenderSection.BaseIndex;
		const uint32 IndexCount = RenderSection.NumTriangles * 3;

		OutSurfacesIndexOffsets[SectionIndex] = IndexIndexOffset;
		Surface.Indices.SetNum(IndexCount);

		for (uint32 ElementIndex = 0; ElementIndex < IndexCount; ElementIndex += 1)
		{
			const uint32 Index = ElementIndex + IndexIndexOffset;

			Surface.Indices[ElementIndex] = IndexBuffer[Index] - VertexIndexOffset;
		}
	}

	OutSurfacesMaterial.Reserve(SkeletalMesh->GetMaterials().Num());
	for (const auto& Material : SkeletalMesh->GetMaterials())
	{
		OutSurfacesMaterial.Emplace(Material.MaterialInterface);
	}

	return true;
}

