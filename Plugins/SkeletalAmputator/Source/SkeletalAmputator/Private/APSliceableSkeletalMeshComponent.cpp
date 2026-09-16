// Copyright 2025 SGSAmputationLab and AO. All Rights Reserved.

#include "APSliceableSkeletalMeshComponent.h"

#include "Rendering/SkeletalMeshRenderData.h"
#include "NiagaraFunctionLibrary.h"
#include "APSliceableCharacter.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletalRenderPublic.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicBoneAttribute.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicVertexSkinWeightsAttribute.h"
#include "Operations/MinimalHoleFiller.h"
#include "Operations/PlanarHoleFiller.h"
#include "Operations/SimpleHoleFiller.h"
#include "Operations/SmoothHoleFiller.h"
#include "UObject/ConstructorHelpers.h"
#include "GeomTools.h"
#include "NiagaraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "SkeletalMeshAttributes.h"
#include "Rendering/SkeletalMeshModel.h"
#include "DrawDebugHelpers.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "ReferenceSkeleton.h"
#include "APSkeletalMeshGenerator.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "EditorAssetSaveLibrary.h"
#include "Misc/PackageName.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"

#include <functional>

#define GENERATE_LOD_MODEL_INTERNAL( VertexType, NumUVs, DestMeshParam, SourceMeshParam, VertexIDsParam, IntersectingVerticesParam, SlicePlaneParam, bShouldCapSliceParam, WorldParam, TrimmedSkeletonParam, TrimmedPhysicsAssetParam, LODIdxParam ) \
{\
	switch( NumUVs )\
	{\
	case 1:\
		GenerateLODModel< VertexType<1> >( DestMeshParam, SourceMeshParam, VertexIDsParam, IntersectingVerticesParam, SlicePlaneParam, bShouldCapSliceParam, WorldParam, TrimmedSkeletonParam, TrimmedPhysicsAssetParam, LODIdxParam );\
		break;\
	case 2:\
		GenerateLODModel< VertexType<2> >( DestMeshParam, SourceMeshParam, VertexIDsParam, IntersectingVerticesParam, SlicePlaneParam, bShouldCapSliceParam, WorldParam, TrimmedSkeletonParam, TrimmedPhysicsAssetParam, LODIdxParam );\
		break;\
	case 3:\
		GenerateLODModel< VertexType<3> >( DestMeshParam, SourceMeshParam, VertexIDsParam, IntersectingVerticesParam, SlicePlaneParam, bShouldCapSliceParam, WorldParam, TrimmedSkeletonParam, TrimmedPhysicsAssetParam, LODIdxParam );\
		break;\
	case 4:\
		GenerateLODModel< VertexType<4> >( DestMeshParam, SourceMeshParam, VertexIDsParam, IntersectingVerticesParam, SlicePlaneParam, bShouldCapSliceParam, WorldParam, TrimmedSkeletonParam, TrimmedPhysicsAssetParam, LODIdxParam );\
		break;\
	default:\
		checkf(false, TEXT("Invalid number of UV sets.  Must be between 0 and 4") );\
		break;\
	}\
}

UAPSliceableSkeletalMeshComponent::UAPSliceableSkeletalMeshComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CapMatFinder(
		TEXT("/SkeletalAmputator/Materials/M_SkeletalSliceInterior.M_SkeletalSliceInterior"));
	if (CapMatFinder.Succeeded())
	{
		CapMaterial = CapMatFinder.Object;
	}
	NumVertices = 0;
}

UAPSliceableSkeletalMeshComponent::~UAPSliceableSkeletalMeshComponent()
{
	BaseVertexWeights.Empty();
}

void UAPSliceableSkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();
	Async(EAsyncExecution::ThreadPool, &UAPSliceableSkeletalMeshComponent::WakeUpThreadPool);
	
	ComputeSkinWeightData();
}

void UAPSliceableSkeletalMeshComponent::WakeUpThreadPool()
{
}

void UAPSliceableSkeletalMeshComponent::SpawnSliceEffect(USceneComponent* TargetSocket)
{
	if (!SliceEffectSystem || !TargetSocket)
		return;
	
	FString CompName = FString::Printf(TEXT("SliceEffect_%s"), *TargetSocket->GetName());
	FName CompFName(*CompName);

	FFXSystemSpawnParameters Params;
	Params.SystemTemplate     = SliceEffectSystem;
	Params.AttachToComponent  = TargetSocket;
	Params.AttachPointName    = CompFName;
	Params.Location           = SliceEffectRelativeTransform.GetLocation();
	Params.Rotation           = SliceEffectRelativeTransform.GetRotation().Rotator();
	Params.Scale              = SliceEffectRelativeTransform.GetScale3D();
	Params.LocationType       = EAttachLocation::KeepRelativeOffset;
	Params.bAutoDestroy       = true;
	Params.bAutoActivate      = true;
	Params.PoolingMethod      = ToPSCPoolMethod(ENCPoolMethod::None);
	Params.bPreCullCheck      = false;
	
	UNiagaraComponent* NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAttachedWithParams(Params);
	GetOwner()->AddInstanceComponent(NiagaraComp);
	if (!NiagaraComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Slice] Failed to spawn Niagara effect on socket %s"), *TargetSocket->GetName());
	}
}

void UAPSliceableSkeletalMeshComponent::SpawnSliceParticle(USceneComponent* TargetSocket)
{
	if (!SliceParticleSystem || !TargetSocket)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnSliceParticle: Invalid ParticleSystem or TargetSocket"));
		return;
	}
	
	FString CompName = FString::Printf(TEXT("SliceEffect_%s"), *TargetSocket->GetName());
	FName CompFName(*CompName);
	
	UE_LOG(LogTemp, Log, TEXT("TargetSocket Name: %s"), *TargetSocket->GetName());
	UE_LOG(LogTemp, Log, TEXT("TargetSocket Class: %s"), *TargetSocket->GetClass()->GetName());
	
	UGameplayStatics::SpawnEmitterAttached(
		SliceParticleSystem.Get(),
		TargetSocket,
		CompFName,
		SliceEffectRelativeTransform.GetLocation(),
		SliceEffectRelativeTransform.GetRotation().Rotator(),
		EAttachLocation::SnapToTargetIncludingScale,
		true
	);
	// GetOwner()->AddInstanceComponent(SliceParticleComponent);
	// SliceParticleComponents.Add(SliceParticleComponent);
	// SliceParticleComponent->SetupAttachment(TargetSocket);
	// SliceParticleComponent->SetTemplate(SliceParticleSystem.Get());
	// SliceParticleComponent->AttachToComponent(TargetSocket, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	// SliceParticleComponent->Activate(false);
}

void UAPSliceableSkeletalMeshComponent::FindVerticesForBoneGroup(const FSkeletonBoneGroup& BoneGroup, USkeletalMesh* SourceMesh, const FPlane& SlicePlane, TSet<uint32>& OutGroupVertices, TSet<uint32>& OutGroupIntersectingVertices) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SliceMesh_FindVerticesForBoneGroup);
	OutGroupVertices.Empty();
	OutGroupIntersectingVertices.Empty();

	if (!SourceMesh || BoneGroup.BoneIndices.Num() == 0)
	{
		return;
	}

	FSkeletalMeshRenderData* SourceResource = SourceMesh->GetResourceForRendering();
	if (!SourceResource || !SourceResource->LODRenderData.IsValidIndex(0))
	{
		return;
	}

	const FSkeletalMeshLODRenderData& SourceLODData = SourceResource->LODRenderData[0];
	const FSkinWeightVertexBuffer* SkinWeightBuffer = SourceLODData.GetSkinWeightVertexBuffer();
	if (!SkinWeightBuffer)
	{
		return;
	}

	const int32 NumMeshVertices = SkinWeightBuffer->GetNumVertices();

	FVector GroupCenterSum = FVector::ZeroVector;
	int32 ValidBoneCount = 0;
	const FReferenceSkeleton& RefSkeleton = SourceMesh->GetRefSkeleton();
	for (int32 BoneIndex : BoneGroup.BoneIndices)
	{
		if (!RefSkeleton.IsValidIndex(BoneIndex))
		{
			continue;
		}
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		const FVector BoneLocationComponent = GetBoneLocation(BoneName, EBoneSpaces::ComponentSpace);
		GroupCenterSum += BoneLocationComponent;
		ValidBoneCount++;
	}

	bool bFilterByPlaneSide = (ValidBoneCount > 0);
	bool bGroupOnPositiveSide = true;
	if (bFilterByPlaneSide)
	{
		const FVector GroupCenter = GroupCenterSum / static_cast<float>(ValidBoneCount);
		const float GroupSideDot = SlicePlane.PlaneDot(GroupCenter);
		bGroupOnPositiveSide = (GroupSideDot > 0.0f);
	}

	for (uint32 VertexIndex = 0; VertexIndex < static_cast<uint32>(NumMeshVertices); ++VertexIndex)
	{
		const FVector3f VertexPositionLocalFloat = SourceLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
		const FVector VertexPositionLocal = FVector(VertexPositionLocalFloat);

		const FSkinWeightInfo& Influence = SkinWeightBuffer->GetVertexSkinWeights(VertexIndex);

		const FSkelMeshRenderSection* SourceSection = nullptr;
		for (const FSkelMeshRenderSection& Section : SourceLODData.RenderSections)
		{
			SourceSection = &Section;
			break;
		}
		if (!SourceSection)
		{
			continue;
		}

		if (bFilterByPlaneSide)
		{
			const float VertexSideDot = SlicePlane.PlaneDot(VertexPositionLocal);
			const bool bVertexOnPositiveSide = (VertexSideDot > 0.0f);
			if (bVertexOnPositiveSide != bGroupOnPositiveSide)
			{
				continue;
			}
		}

		float MaxWeight = 0.0f;
		int32 MaxInfluenceIdx = -1;
		for (int32 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES; ++InfluenceIdx)
		{
			if (Influence.InfluenceWeights[InfluenceIdx] > MaxWeight)
			{
				const FBoneIndexType BoneMapIndex = Influence.InfluenceBones[InfluenceIdx];
				if (!SourceSection->BoneMap.IsValidIndex(BoneMapIndex))
				{
					continue;
				}
				const FBoneIndexType SkeletonBoneIndex = SourceSection->BoneMap[BoneMapIndex];
				if (BoneGroup.BoneIndices.Contains(SkeletonBoneIndex))
				{
					MaxWeight = static_cast<float>(Influence.InfluenceWeights[InfluenceIdx]);
					MaxInfluenceIdx = InfluenceIdx;
				}
			}
		}

		if (MaxInfluenceIdx == -1)
		{
			continue;
		}



		OutGroupVertices.Add(VertexIndex);
	}

	TArray<FFinalSkinVertex> SkinnedVertices;
	GetCPUSkinnedVertices(SkinnedVertices, 0);
	if (SkinnedVertices.Num() != NumMeshVertices)
	{
		return;
	}

	TArray<float> Distances;
	Distances.SetNumUninitialized(NumMeshVertices);
	ParallelFor(NumMeshVertices, [&SkinnedVertices, &SlicePlane, &Distances](int32 VertexIndex)
		{
			Distances[VertexIndex] = SlicePlane.PlaneDot(FVector(SkinnedVertices[VertexIndex].Position));
		});

	TArray<uint32> IndexBuffer;
	SourceLODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

	TSet<uint32> IntersectingVertices;
	{
		FCriticalSection IntersectingSetLock;
		const int32 NumTriangles = IndexBuffer.Num() / 3;
		ParallelFor(NumTriangles, [&Distances, &IndexBuffer, &IntersectingVertices, &IntersectingSetLock](int32 TriIndex)
			{
				const int32 BaseIndex = TriIndex * 3;
				const uint32 Index0 = IndexBuffer[BaseIndex + 0];
				const uint32 Index1 = IndexBuffer[BaseIndex + 1];
				const uint32 Index2 = IndexBuffer[BaseIndex + 2];

				if (Distances.IsValidIndex(Index0) && Distances.IsValidIndex(Index1) && Distances.IsValidIndex(Index2))
				{
					const float Distance0 = Distances[Index0];
					const float Distance1 = Distances[Index1];
					const float Distance2 = Distances[Index2];
					const float DistanceMin = FMath::Min3(Distance0, Distance1, Distance2);
					const float DistanceMax = FMath::Max3(Distance0, Distance1, Distance2);

					if (DistanceMin < 0.0f && DistanceMax > 0.0f)
					{
						FScopeLock Lock(&IntersectingSetLock);
						IntersectingVertices.Add(Index0);
						IntersectingVertices.Add(Index1);
						IntersectingVertices.Add(Index2);
					}
				}
			});
	}

	for (uint32 VertexIndex : OutGroupVertices)
	{
		if (IntersectingVertices.Contains(VertexIndex))
		{
			OutGroupIntersectingVertices.Add(VertexIndex);
		}
	}
}

bool UAPSliceableSkeletalMeshComponent::HasEdgeLoop(const TSet<uint32>& EdgeVertices, USkeletalMesh* SourceMesh) const
{
	if (EdgeVertices.Num() < 3)
	{
		return false;
	}
	
	USkeletalMesh* MeshAsset = SourceMesh ? SourceMesh : GetSkeletalMeshAsset();
	if (!MeshAsset)
	{
		return false;
	}
	
	FSkeletalMeshRenderData* RenderData = MeshAsset->GetResourceForRendering();
	if (!RenderData || RenderData->LODRenderData.Num() == 0)
	{
		return false;
	}
	
	const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
	TArray<uint32> IndexBuffer;
	LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
	
	TMap<uint32, TArray<uint32>> VertexToNeighbors;
	
	for (int32 TriIdx = 0; TriIdx < IndexBuffer.Num(); TriIdx += 3)
	{
		const uint32 V0 = IndexBuffer[TriIdx];
		const uint32 V1 = IndexBuffer[TriIdx + 1];
		const uint32 V2 = IndexBuffer[TriIdx + 2];
		
		const bool bV0Edge = EdgeVertices.Contains(V0);
		const bool bV1Edge = EdgeVertices.Contains(V1);
		const bool bV2Edge = EdgeVertices.Contains(V2);
		
		if (bV0Edge && bV1Edge)
		{
			VertexToNeighbors.FindOrAdd(V0).AddUnique(V1);
			VertexToNeighbors.FindOrAdd(V1).AddUnique(V0);
		}
		if (bV1Edge && bV2Edge)
		{
			VertexToNeighbors.FindOrAdd(V1).AddUnique(V2);
			VertexToNeighbors.FindOrAdd(V2).AddUnique(V1);
		}
		if (bV2Edge && bV0Edge)
		{
			VertexToNeighbors.FindOrAdd(V2).AddUnique(V0);
			VertexToNeighbors.FindOrAdd(V0).AddUnique(V2);
		}
	}
	
	if (VertexToNeighbors.Num() < 3)
	{
		return false;
	}
	
	TSet<uint32> Visited;
	TArray<uint32> Stack;
	
	const uint32 StartVertex = VertexToNeighbors.CreateConstIterator().Key();
	Stack.Push(StartVertex);
	Visited.Add(StartVertex);
	
	while (Stack.Num() > 0)
	{
		const uint32 CurrentVertex = Stack.Pop();
		const TArray<uint32>* NeighborsPtr = VertexToNeighbors.Find(CurrentVertex);
		
		if (!NeighborsPtr)
		{
			continue;
		}
		
		for (uint32 Neighbor : *NeighborsPtr)
		{
			if (Neighbor == StartVertex && Visited.Num() > 2)
			{
				return true;
			}
			
			if (!Visited.Contains(Neighbor))
			{
				Visited.Add(Neighbor);
				Stack.Push(Neighbor);
			}
		}
	}
	
	return Visited.Num() == EdgeVertices.Num() && Visited.Num() >= 3;
}

void UAPSliceableSkeletalMeshComponent::FindConnectedVertexGroups(const TSet<uint32>& Vertices, USkeletalMesh* SourceMesh, TArray<TSet<uint32>>& OutVertexGroups) const
{
	OutVertexGroups.Empty();

	if (Vertices.Num() == 0)
	{
		return;
	}

	USkeletalMesh* MeshAsset = SourceMesh ? SourceMesh : GetSkeletalMeshAsset();
	if (!MeshAsset)
	{
		return;
	}

	FSkeletalMeshRenderData* RenderData = MeshAsset->GetResourceForRendering();
	if (!RenderData || !RenderData->LODRenderData.IsValidIndex(0))
	{
		return;
	}

	const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
	TArray<uint32> IndexBuffer;
	LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

	TMap<uint32, TArray<uint32>> VertexToNeighbors;

	for (int32 TriangleIndex = 0; TriangleIndex < IndexBuffer.Num(); TriangleIndex += 3)
	{
		const uint32 VertexIndex0 = IndexBuffer[TriangleIndex];
		const uint32 VertexIndex1 = IndexBuffer[TriangleIndex + 1];
		const uint32 VertexIndex2 = IndexBuffer[TriangleIndex + 2];

		const bool bVertex0InSet = Vertices.Contains(VertexIndex0);
		const bool bVertex1InSet = Vertices.Contains(VertexIndex1);
		const bool bVertex2InSet = Vertices.Contains(VertexIndex2);

		if (bVertex0InSet && bVertex1InSet)
		{
			VertexToNeighbors.FindOrAdd(VertexIndex0).AddUnique(VertexIndex1);
			VertexToNeighbors.FindOrAdd(VertexIndex1).AddUnique(VertexIndex0);
		}
		if (bVertex1InSet && bVertex2InSet)
		{
			VertexToNeighbors.FindOrAdd(VertexIndex1).AddUnique(VertexIndex2);
			VertexToNeighbors.FindOrAdd(VertexIndex2).AddUnique(VertexIndex1);
		}
		if (bVertex2InSet && bVertex0InSet)
		{
			VertexToNeighbors.FindOrAdd(VertexIndex2).AddUnique(VertexIndex0);
			VertexToNeighbors.FindOrAdd(VertexIndex0).AddUnique(VertexIndex2);
		}
	}

	TSet<uint32> VisitedVertices;

	for (uint32 StartVertexIndex : Vertices)
	{
		if (VisitedVertices.Contains(StartVertexIndex))
		{
			continue;
		}

		TSet<uint32> CurrentGroup;
		TArray<uint32> Queue;
		Queue.Add(StartVertexIndex);
		VisitedVertices.Add(StartVertexIndex);

		while (Queue.Num() > 0)
		{
			const uint32 CurrentVertexIndex = Queue[0];
			Queue.RemoveAt(0);
			CurrentGroup.Add(CurrentVertexIndex);

			const TArray<uint32>* NeighborsPointer = VertexToNeighbors.Find(CurrentVertexIndex);
			if (NeighborsPointer)
			{
				for (uint32 NeighborVertexIndex : *NeighborsPointer)
				{
					if (!VisitedVertices.Contains(NeighborVertexIndex))
					{
						VisitedVertices.Add(NeighborVertexIndex);
						Queue.Add(NeighborVertexIndex);
					}
				}
			}
		}

		if (CurrentGroup.Num() > 0)
		{
			OutVertexGroups.Add(CurrentGroup);
		}
	}
}

UDynamicMeshComponent* UAPSliceableSkeletalMeshComponent::CreateDynamicMeshFromVertices(const TSet<uint32>& VertexGroup, USkeletalMesh* SourceMesh, const FPlane& SlicePlane, const FName& ComponentName)
{
	if (VertexGroup.Num() < 3)
	{
		return nullptr;
	}

	USkeletalMesh* MeshAsset = SourceMesh ? SourceMesh : GetSkeletalMeshAsset();
	if (!MeshAsset)
	{
		return nullptr;
	}

	FSkeletalMeshRenderData* RenderData = MeshAsset->GetResourceForRendering();
	if (!RenderData || !RenderData->LODRenderData.IsValidIndex(0))
	{
		return nullptr;
	}

	const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
	const FPositionVertexBuffer& PositionBuffer = LODData.StaticVertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& StaticBuffer = LODData.StaticVertexBuffers.StaticMeshVertexBuffer;
	const uint32 VertexCount = PositionBuffer.GetNumVertices();
	const uint32 NumTexCoords = StaticBuffer.GetNumTexCoords();

	TArray<uint32> IndexBuffer;
	LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

	TArray<FFinalSkinVertex> SkinnedVertices;
	GetCPUSkinnedVertices(SkinnedVertices, 0);
	const bool bUseSkinnedPositions = (SkinnedVertices.Num() == static_cast<int32>(VertexCount));

	TMap<uint32, int32> OldToNewVertexIndex;
	TArray<FVector3f> NewVertexPositions;
	TArray<FVector2f> NewVertexUVs;

	for (uint32 OldVertexIndex : VertexGroup)
	{
		if (OldVertexIndex < VertexCount)
		{
			const int32 NewIndex = NewVertexPositions.Num();
			OldToNewVertexIndex.Add(OldVertexIndex, NewIndex);

			if (bUseSkinnedPositions)
			{
				NewVertexPositions.Add(SkinnedVertices[OldVertexIndex].Position);
			}
			else
			{
				NewVertexPositions.Add(PositionBuffer.VertexPosition(OldVertexIndex));
			}

			if (NumTexCoords >= 1)
			{
				NewVertexUVs.Add(StaticBuffer.GetVertexUV(OldVertexIndex, 0));
			}
			else
			{
				NewVertexUVs.Add(FVector2f::ZeroVector);
			}
		}
	}

	TArray<int32> NewTriangleIndices;
	for (int32 TriangleIndex = 0; TriangleIndex < IndexBuffer.Num(); TriangleIndex += 3)
	{
		const uint32 VertexIndex0 = IndexBuffer[TriangleIndex];
		const uint32 VertexIndex1 = IndexBuffer[TriangleIndex + 1];
		const uint32 VertexIndex2 = IndexBuffer[TriangleIndex + 2];

		if (VertexGroup.Contains(VertexIndex0) && VertexGroup.Contains(VertexIndex1) && VertexGroup.Contains(VertexIndex2))
		{
			const int32* NewIndex0Pointer = OldToNewVertexIndex.Find(VertexIndex0);
			const int32* NewIndex1Pointer = OldToNewVertexIndex.Find(VertexIndex1);
			const int32* NewIndex2Pointer = OldToNewVertexIndex.Find(VertexIndex2);

			if (NewIndex0Pointer && NewIndex1Pointer && NewIndex2Pointer)
			{
				NewTriangleIndices.Add(*NewIndex0Pointer);
				NewTriangleIndices.Add(*NewIndex1Pointer);
				NewTriangleIndices.Add(*NewIndex2Pointer);
			}
		}
	}

	if (NewTriangleIndices.Num() < 3)
	{
		return nullptr;
	}

	UE::Geometry::FDynamicMesh3 DynamicMesh;
	DynamicMesh.EnableVertexUVs(FVector2f::ZeroVector);
	DynamicMesh.EnableVertexNormals(FVector3f::UpVector);

	for (int32 VertexIndex = 0; VertexIndex < NewVertexPositions.Num(); ++VertexIndex)
	{
		const FVector3f& Position = NewVertexPositions[VertexIndex];
		const int32 NewVertexId = DynamicMesh.AppendVertex(FVector3d(Position.X, Position.Y, Position.Z));
		
		if (VertexIndex < NewVertexUVs.Num())
		{
			DynamicMesh.SetVertexUV(NewVertexId, FVector2f(NewVertexUVs[VertexIndex].X, NewVertexUVs[VertexIndex].Y));
		}
	}

	for (int32 TriIndex = 0; TriIndex < NewTriangleIndices.Num(); TriIndex += 3)
	{
		DynamicMesh.AppendTriangle(NewTriangleIndices[TriIndex], NewTriangleIndices[TriIndex + 1], NewTriangleIndices[TriIndex + 2]);
	}

	if (DynamicMesh.TriangleCount() == 0)
	{
		return nullptr;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	UDynamicMeshComponent* DynamicMeshComponent = NewObject<UDynamicMeshComponent>(OwnerActor, ComponentName);
	if (!DynamicMeshComponent)
	{
		return nullptr;
	}

	DynamicMeshComponent->SetupAttachment(OwnerActor->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();

	DynamicMeshComponent->GetDynamicMesh()->SetMesh(MoveTemp(DynamicMesh));
	DynamicMeshComponent->SetWorldTransform(GetComponentTransform());

	FBox BoundingBox(ForceInit);
	for (const FVector3f& Position : NewVertexPositions)
	{
		BoundingBox += FVector(Position);
	}

	const FVector BoxExtent = BoundingBox.GetExtent();
	const FVector BoxCenter = BoundingBox.GetCenter();
	const float SphereRadius = BoxExtent.Size();

	UBoxComponent* CollisionBox = NewObject<UBoxComponent>(DynamicMeshComponent, FName(*FString::Format(TEXT("{0}_Collision"), {ComponentName.ToString()})));
	if (CollisionBox)
	{
		CollisionBox->SetupAttachment(DynamicMeshComponent);
		CollisionBox->SetRelativeLocation(BoxCenter);
		CollisionBox->SetBoxExtent(BoxExtent);
		CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CollisionBox->SetCollisionProfileName(TEXT("Ragdoll"));
		CollisionBox->RegisterComponent();
	}

	if (CapMaterial)
	{
		DynamicMeshComponent->SetMaterial(0, CapMaterial);
	}
	else
	{
		const int32 MaterialCount = GetNumMaterials();
		if (MaterialCount > 0)
		{
			DynamicMeshComponent->SetMaterial(0, GetMaterial(0));
		}
	}

	return DynamicMeshComponent;
}

USkeletalMesh* UAPSliceableSkeletalMeshComponent::BuildSkeletalMeshBySkeletalGroup(
	const FSkeletonBoneGroup& BoneGroup,
	const FName& AssetNameTag,
	const FPlane& SlicePlane
)
{
	USkeletalMesh* OriginalMesh = GetSkeletalMeshAsset();
	if (!OriginalMesh)
	{
		return nullptr;
	}

	TSet<uint32> GroupVertices;
	TSet<uint32> GroupIntersectingVertices;
	FindVerticesForBoneGroup(BoneGroup, OriginalMesh, SlicePlane, GroupVertices, GroupIntersectingVertices);
	if (GroupVertices.IsEmpty())
	{
		return nullptr;
	}

	if (!GroupIntersectingVertices.IsEmpty())
	{
		if (bRequireEdgeLoop)
		{
			if (!HasEdgeLoop(GroupIntersectingVertices, OriginalMesh))
			{
				GroupIntersectingVertices.Empty();
			}
		}
	}
	
	FName NewAssetName = MakeUniqueObjectName(this, USkeletalMesh::StaticClass(),
	                                          *FString::Format(TEXT("{0}_{1}"), {OriginalMesh->GetName(), AssetNameTag.ToString()}));
	
	USkeletalMesh* NewSkeletalMesh = NewObject<USkeletalMesh>(this, NewAssetName);

	if (!GroupIntersectingVertices.IsEmpty())
	{
		if (BuildSkeletalMeshInternal(NewSkeletalMesh, OriginalMesh, GroupVertices, GroupIntersectingVertices, SlicePlane, BoneGroup, GetWorld()))
		{
			return NewSkeletalMesh;
		}
	}
	else
	{
		if (BuildSkeletalMeshInternal(NewSkeletalMesh, OriginalMesh, GroupVertices, BoneGroup, GetWorld()))
		{
			return NewSkeletalMesh;
		}
	}
	
	return nullptr;
}


UAPSliceableSkeletalMeshComponent* UAPSliceableSkeletalMeshComponent::BuildSkeletalMeshComponent(USkeletalMesh* SourceMesh, const FName& NewComponentName)
{
	UAPSliceableSkeletalMeshComponent* NewSkeletalMeshComp = NewObject<UAPSliceableSkeletalMeshComponent>(GetOwner(), NewComponentName);
	
	NewSkeletalMeshComp->SetSkeletalMesh(SourceMesh);

	if (GetAttachParent())
	{
		NewSkeletalMeshComp->SetupAttachment(GetAttachParent());
	}
	NewSkeletalMeshComp->RegisterComponent();
	GetOwner()->AddInstanceComponent(NewSkeletalMeshComp);

	NewSkeletalMeshComp->SetRelativeTransform(GetRelativeTransform());

	NewSkeletalMeshComp->SetVisibility(true, true);
	NewSkeletalMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	
	return NewSkeletalMeshComp;
}


void UAPSliceableSkeletalMeshComponent::ComputeSkinWeightData()
{
	USkeletalMesh* MeshAsset = GetSkeletalMeshAsset();
	if (!MeshAsset) return;

	FSkeletalMeshRenderData* RenderData = MeshAsset->GetResourceForRendering();
	if (!RenderData || !RenderData->LODRenderData.IsValidIndex(0)) return;

	const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
	const FSkinWeightVertexBuffer* SkinWeightBuffer = LODData.GetSkinWeightVertexBuffer();
	if (!SkinWeightBuffer) return;

	NumVertices = RenderData->LODRenderData[0].GetNumVertices();
	BaseVertexColors.Init(FColor(255, 255, 255, 255), NumVertices);
	BaseVertexPositions.Init(FVector3f::ZeroVector, NumVertices);
	LODData.SkinWeightVertexBuffer.GetSkinWeights(BaseVertexWeights);
	
	const int32 NumInfluences = SkinWeightBuffer->GetMaxBoneInfluences();
	MaxInfluence = NumInfluences;

	for (uint32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
	{
		BaseVertexPositions[VertexIndex] = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);

		for (int32 InfluenceIndex = 0; InfluenceIndex < NumInfluences; ++InfluenceIndex)
		{
			const uint16 Weight = SkinWeightBuffer->GetBoneWeight(VertexIndex, InfluenceIndex);
			if (Weight > 0)
			{
				const int32 SectionBoneIndex = SkinWeightBuffer->GetBoneIndex(VertexIndex, InfluenceIndex);
				const FSkelMeshRenderSection* Section = nullptr;
				for (const FSkelMeshRenderSection& RenderSection : LODData.RenderSections)
				{
					if (VertexIndex >= RenderSection.BaseVertexIndex && VertexIndex < RenderSection.BaseVertexIndex +
						RenderSection.NumVertices)
					{
						Section = &RenderSection;
						break;
					}
				}

				if (Section)
				{
					BaseVertexWeights[VertexIndex].InfluenceBones[InfluenceIndex] = Section->BoneMap[SectionBoneIndex];
					BaseVertexWeights[VertexIndex].InfluenceWeights[InfluenceIndex] = Weight;
				}
			}
		}
	}
}

FSkeletonAnalysisResult UAPSliceableSkeletalMeshComponent::AnalyzeSkeletonByPlane(const FPlane& SlicePlane) const
{
	FSkeletonAnalysisResult Result;

	USkeletalMesh* MeshAsset = GetSkeletalMeshAsset();
	if (!MeshAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AnalyzeSkeletonByPlane] No skeletal mesh asset"));
		return Result;
	}

	const FReferenceSkeleton& RefSkeleton = MeshAsset->GetRefSkeleton();
	const int32 NumBones = RefSkeleton.GetNum();
	if (NumBones == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AnalyzeSkeletonByPlane] Skeleton has no bones"));
		return Result;
	}

	const FPlane LocalSlicePlane = SlicePlane.TransformBy(GetComponentTransform().Inverse().ToMatrixWithScale());
	const TArray<FTransform>& ComponentSpaceTransforms = GetComponentSpaceTransforms();
	
	if (ComponentSpaceTransforms.Num() != NumBones)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AnalyzeSkeletonByPlane] Component space transforms count mismatch"));
		return Result;
	}

	TArray<bool> BoneOnPositiveSide;
	TArray<TSet<int32>> BoneChildren;
	BoneOnPositiveSide.SetNum(NumBones);
	BoneChildren.SetNum(NumBones);

	constexpr float PlaneTolerance = 0.1f;

	TArray<int32> BoneToNonTwistParent;
	BoneToNonTwistParent.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		BoneToNonTwistParent[BoneIndex] = FindNonTwistParentForAnalysis(RefSkeleton, BoneIndex);
	}

	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		if (IsEndBone(RefSkeleton, BoneIndex))
		{
			continue;
		}

		int32 EffectiveBoneIndex = BoneIndex;
		if (IsTwistBone(RefSkeleton, BoneIndex))
		{
			EffectiveBoneIndex = BoneToNonTwistParent[BoneIndex];
			if (EffectiveBoneIndex == INDEX_NONE)
			{
				continue;
			}
		}

		const FVector BoneLocation = ComponentSpaceTransforms[EffectiveBoneIndex].GetLocation();
		const float DistanceToPlane = LocalSlicePlane.PlaneDot(BoneLocation);
		BoneOnPositiveSide[BoneIndex] = DistanceToPlane > -PlaneTolerance;

		int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		if (ParentIndex != INDEX_NONE)
		{
			int32 EffectiveParentIndex = IsTwistBone(RefSkeleton, ParentIndex) ? BoneToNonTwistParent[ParentIndex] : ParentIndex;
			if (EffectiveParentIndex != INDEX_NONE)
			{
				BoneChildren[EffectiveParentIndex].Add(BoneIndex);
			}
		}
	}

	TSet<int32> VisitedBones;
	
	TArray<FSkeletonBoneGroup> PositiveGroups;
	TArray<FSkeletonBoneGroup> NegativeGroups;
	FindConnectedComponents(true, BoneOnPositiveSide, BoneChildren, RefSkeleton, BoneToNonTwistParent, NumBones, PositiveGroups, VisitedBones);
	FindConnectedComponents(false, BoneOnPositiveSide, BoneChildren, RefSkeleton, BoneToNonTwistParent, NumBones, NegativeGroups, VisitedBones);
	
	Result.Groups.Append(PositiveGroups);
	Result.Groups.Append(NegativeGroups);

#if ENABLE_LOG_SLICE_BONE_LIST
	UE_LOG(LogTemp, Log, TEXT("[AnalyzeSkeletonByPlane] Analysis complete. Total bones: %d, Groups: %d"), 
		NumBones, Result.Groups.Num());
	
	int32 TotalBoneCount = 0;
	for (int32 GroupIndex = 0; GroupIndex < Result.Groups.Num(); ++GroupIndex)
	{
		const int32 GroupBoneCount = Result.Groups[GroupIndex].BoneIndices.Num();
		TotalBoneCount += GroupBoneCount;
		UE_LOG(LogTemp, Log, TEXT("  Group %d: %d bones"), GroupIndex, GroupBoneCount);
		
		FString BoneNames;
		for (int32 BoneIdx = 0; BoneIdx < Result.Groups[GroupIndex].BoneIndices.Num(); ++BoneIdx)
		{
			const int32 BoneIndex = Result.Groups[GroupIndex].BoneIndices[BoneIdx];
			const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
			if (BoneIdx > 0)
			{
				BoneNames += TEXT(", ");
			}
			BoneNames += BoneName.ToString();
		}
		UE_LOG(LogTemp, Log, TEXT("    Bones: %s"), *BoneNames);
	}
	
	UE_LOG(LogTemp, Log, TEXT("[AnalyzeSkeletonByPlane] Total bones in groups: %d"), TotalBoneCount);
#endif

	return Result;
}
#pragma endregion

bool UAPSliceableSkeletalMeshComponent::BuildSkeletalMeshInternal(USkeletalMesh* DestMesh, USkeletalMesh* SourceMesh, const TSet<uint32>& VertexIDs, const FSkeletonBoneGroup& BoneGroup, UWorld* World)
{
	return BuildSkeletalMeshInternal(DestMesh, SourceMesh, VertexIDs, TSet<uint32>(), FPlane(), BoneGroup, World);
}

bool UAPSliceableSkeletalMeshComponent::BuildSkeletalMeshInternal(USkeletalMesh* DestMesh, USkeletalMesh* SourceMesh, const TSet<uint32>& VertexIDs, const TSet<uint32>& IntersectingVertices, const FPlane& SlicePlane, const FSkeletonBoneGroup& BoneGroup, UWorld* World)
{
	bool Result = true;
	bool bShouldCapSlice = IntersectingVertices.Num() > 0;

	int32 MaxLODs = GetMaxLodFromSourceMesh(SourceMesh);

	ReleaseResources(DestMesh, MaxLODs);

	if (SourceMesh)
	{
		if (SourceMesh->GetHasVertexColors())
		{
			DestMesh->SetHasVertexColors(true);
#if WITH_EDITORONLY_DATA
			DestMesh->SetVertexColorGuid(FGuid::NewGuid());
#endif
		}
	}

	if (Result)
	{
		USkeleton* TrimmedSkeleton = nullptr;
		UPhysicsAsset* TrimmedPhysicsAsset = nullptr;

		if (BoneGroup.BoneIndices.Num() > 0)
		{
			TrimmedSkeleton = GenerateTrimmedSkeleton(BoneGroup, SourceMesh);
			if (TrimmedSkeleton)
			{
				TrimmedPhysicsAsset = GenerateTrimmedPhysicsAsset(TrimmedSkeleton, SourceMesh);
			}
		}
		
		TArray<uint32> PerLODNumUVSets;
		PerLODNumUVSets.AddZeroed(MaxLODs);

		FSkeletalMeshRenderData* SourceResource = SourceMesh->GetResourceForRendering();

		for (int32 LODIdx = 0; LODIdx < MaxLODs; LODIdx++)
		{
			if (SourceResource->LODRenderData.IsValidIndex(LODIdx))
			{
				uint32& NumUVSets = PerLODNumUVSets[LODIdx];
				NumUVSets = FMath::Max(NumUVSets, SourceResource->LODRenderData[LODIdx].GetNumTexCoords());
			}
		}
		
		DestMesh->AllocateResourceForRendering();
		for (int32 LODIdx = 0; LODIdx < MaxLODs; LODIdx++)
		{
			const FSkeletalMeshLODInfo* LODInfoPtr = DestMesh->GetLODInfo(LODIdx);
			bool bUseFullPrecisionUVs = LODInfoPtr ? LODInfoPtr->BuildSettings.bUseFullPrecisionUVs : false;
			if (!bUseFullPrecisionUVs)
			{
				GENERATE_LOD_MODEL_INTERNAL(TGPUSkinVertexFloat16Uvs, PerLODNumUVSets[LODIdx], DestMesh, SourceMesh, VertexIDs, IntersectingVertices, SlicePlane, bShouldCapSlice, World, TrimmedSkeleton, TrimmedPhysicsAsset, LODIdx);
			}
			else
			{
				GENERATE_LOD_MODEL_INTERNAL(TGPUSkinVertexFloat32Uvs, PerLODNumUVSets[LODIdx], DestMesh, SourceMesh, VertexIDs, IntersectingVertices, SlicePlane, bShouldCapSlice, World, TrimmedSkeleton, TrimmedPhysicsAsset, LODIdx);
			}
		}

		if (!ProcessSkeletalMesh(DestMesh, SourceMesh, TrimmedSkeleton, TrimmedPhysicsAsset))
		{
			Result = false;
		}

		if (!GIsEditor)
		{
			DestMesh->NeverStream = false;
#if WITH_EDITORONLY_DATA
			DestMesh->NeverStream = true;
#endif
		}

		DestMesh->InitResources();
	}
	
	return Result;
}


int32 UAPSliceableSkeletalMeshComponent::GetMaxLodFromSourceMesh(USkeletalMesh* SourceMesh) const
{
	return 1;
}

void UAPSliceableSkeletalMeshComponent::ReleaseResources(USkeletalMesh* DestMesh, int32 Slack) const
{
	DestMesh->ReleaseResources();
	DestMesh->ReleaseResourcesFence.Wait();
	
	if (FSkeletalMeshRenderData* Resource = DestMesh->GetResourceForRendering())
	{
		Resource->LODRenderData[0].ReleaseResources();
	}
}

bool UAPSliceableSkeletalMeshComponent::ProcessSkeletalMesh(USkeletalMesh* DestMesh, USkeletalMesh* SourceMesh, USkeleton* TrimmedSkeleton, UPhysicsAsset* TrimmedPhysicsAsset) const
{

	DestMesh->SetImportedBounds(SourceMesh->GetImportedBounds());

	if (TrimmedSkeleton)
	{
		DestMesh->SetSkeleton(TrimmedSkeleton);
		DestMesh->SetRefSkeleton(TrimmedSkeleton->GetReferenceSkeleton());
	}

	if (TrimmedPhysicsAsset)
	{
#if WITH_EDITORONLY_DATA
		TrimmedPhysicsAsset->SetPreviewMesh(DestMesh);
#endif
		DestMesh->SetPhysicsAsset(TrimmedPhysicsAsset);
	}

	//DestMesh->SetBodySetup(SourceMesh->GetBodySetup());

	DestMesh->CalculateInvRefMatrices();

	DestMesh->SetMaterials(SourceMesh->GetMaterials());
	
	return true;
}


USkeleton* UAPSliceableSkeletalMeshComponent::GenerateTrimmedSkeleton(const FSkeletonBoneGroup& BoneGroup, USkeletalMesh* SourceMesh)
{
	if (SourceMesh == nullptr)
	{
		return nullptr;
	}

	const FReferenceSkeleton& SourceRefSkeleton = SourceMesh->GetRefSkeleton();

	USkeleton* NewSkeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient);
	if (NewSkeleton == nullptr)
	{
		return nullptr;
	}

	FReferenceSkeleton& NewRefSkeleton = const_cast<FReferenceSkeleton&>(NewSkeleton->GetReferenceSkeleton());
	NewRefSkeleton = SourceRefSkeleton;

	TSet<int32> OriginalBoneIndicesToKeep;
	OriginalBoneIndicesToKeep.Reserve(BoneGroup.BoneIndices.Num());
	for (int32 OriginalBoneIndex : BoneGroup.BoneIndices)
	{
		OriginalBoneIndicesToKeep.Add(OriginalBoneIndex);
	}

	TArray<int32> OriginalBoneIndicesToRemove;
	OriginalBoneIndicesToRemove.Reserve(SourceRefSkeleton.GetNum());
	for (int32 OriginalBoneIndex = 0; OriginalBoneIndex < SourceRefSkeleton.GetNum(); ++OriginalBoneIndex)
	{
		if (!OriginalBoneIndicesToKeep.Contains(OriginalBoneIndex))
		{
			OriginalBoneIndicesToRemove.Add(OriginalBoneIndex);
		}
	}

	for (int32 OriginalBoneIndex : OriginalBoneIndicesToRemove)
	{
		RemoveBone(NewRefSkeleton, SourceRefSkeleton, OriginalBoneIndex);
	}


	NewRefSkeleton.RebuildRefSkeleton(NewSkeleton, true);

	return NewSkeleton;
}

void UAPSliceableSkeletalMeshComponent::RemoveBone(FReferenceSkeleton& TargetRefSkeleton, const FReferenceSkeleton& SourceRefSkeleton, int32 TargetOriginalBoneIndex)
{
	const TArray<FMeshBoneInfo>& MeshBoneInfos = TargetRefSkeleton.GetRawRefBoneInfo();
	TArray<FMeshBoneInfo>& DynamicMeshBoneInfos = const_cast<TArray<FMeshBoneInfo>&>(MeshBoneInfos);

	const TArray<FTransform>& RefBonePoses = TargetRefSkeleton.GetRawRefBonePose();
	TArray<FTransform>& DynamicRefBonePoses = const_cast<TArray<FTransform>&>(RefBonePoses);

	const TMap<FName, int32>& NameToIndex = SourceRefSkeleton.GetRawNameToIndexMap();

	int32 CurrentIndex = INDEX_NONE;
	for (int32 Index = 0; Index < DynamicMeshBoneInfos.Num(); ++Index)
	{
		const int32* FoundOriginal = NameToIndex.Find(DynamicMeshBoneInfos[Index].Name);
		if (FoundOriginal && *FoundOriginal == TargetOriginalBoneIndex)
		{
			CurrentIndex = Index;
			break;
		}
	}

	if (CurrentIndex == INDEX_NONE)
	{
		return;
	}

	const int32 RemovedParentIndex = DynamicMeshBoneInfos[CurrentIndex].ParentIndex;

	TArray<int32> ChildIndices;
	for (int32 Index = 0; Index < DynamicMeshBoneInfos.Num(); ++Index)
	{
		if (DynamicMeshBoneInfos[Index].ParentIndex == CurrentIndex)
		{
			ChildIndices.Add(Index);
		}
	}

	const FTransform RemovedBoneTransform = DynamicRefBonePoses[CurrentIndex];

	if (ChildIndices.Num() > 0)
	{
		if (RemovedParentIndex == INDEX_NONE)
		{
			const int32 FirstChildIndex = ChildIndices[0];
			const FTransform FirstChildComponentSpaceTransform = DynamicRefBonePoses[FirstChildIndex] * RemovedBoneTransform;

			for (int32 ChildIndex : ChildIndices)
			{
				if (ChildIndex == FirstChildIndex)
				{
					DynamicRefBonePoses[ChildIndex] = FirstChildComponentSpaceTransform;
					DynamicMeshBoneInfos[ChildIndex].ParentIndex = INDEX_NONE;
				}
				else
				{
					const FTransform ChildComponentSpaceTransform = DynamicRefBonePoses[ChildIndex] * RemovedBoneTransform;
					DynamicRefBonePoses[ChildIndex] = ChildComponentSpaceTransform.GetRelativeTransform(FirstChildComponentSpaceTransform);
					DynamicMeshBoneInfos[ChildIndex].ParentIndex = FirstChildIndex;
				}
			}
		}
		else
		{
			for (int32 ChildIndex : ChildIndices)
			{
				DynamicRefBonePoses[ChildIndex] = DynamicRefBonePoses[ChildIndex] * RemovedBoneTransform;
				DynamicMeshBoneInfos[ChildIndex].ParentIndex = RemovedParentIndex;
			}
		}
	}

	for (int32 Index = 0; Index < DynamicMeshBoneInfos.Num(); ++Index)
	{
		if (Index == CurrentIndex)
		{
			continue;
		}

		const int32 ParentIndex = DynamicMeshBoneInfos[Index].ParentIndex;
		if (ParentIndex != INDEX_NONE && ParentIndex > CurrentIndex)
		{
			DynamicMeshBoneInfos[Index].ParentIndex = ParentIndex - 1;
		}
	}

	DynamicMeshBoneInfos.RemoveAt(CurrentIndex);
	DynamicRefBonePoses.RemoveAt(CurrentIndex);
}



UPhysicsAsset* UAPSliceableSkeletalMeshComponent::GenerateTrimmedPhysicsAsset(USkeleton* InTrimmedSkeleton, USkeletalMesh* SourceMesh)
{
	UPhysicsAsset* SourcePhysicsAsset = SourceMesh->GetPhysicsAsset();
	if (!SourcePhysicsAsset)
	{
		return nullptr;
	}
	
	USkeleton* SourceSkeleton = SourceMesh->GetSkeleton();

	if (!SourceSkeleton)
	{
		return nullptr;
	}
	
	const FReferenceSkeleton& SourceRefSkeleton = SourceSkeleton->GetReferenceSkeleton();
	const FReferenceSkeleton& TrimmedRefSkeleton = InTrimmedSkeleton->GetReferenceSkeleton();
	
	UPhysicsAsset* NewPhysicsAsset = NewObject<UPhysicsAsset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UPhysicsAsset::StaticClass(), FName(*FString::Format(TEXT("{0}_Trimmed"), {SourcePhysicsAsset->GetName()}))));
	if (!NewPhysicsAsset)
	{
		return nullptr;
	}
	
	NewPhysicsAsset->SkeletalBodySetups.Empty();
	NewPhysicsAsset->ConstraintSetup.Empty();
#if WITH_EDITORONLY_DATA
	NewPhysicsAsset->SetPreviewMesh(SourceMesh);
#endif

	for (int32 BodySetupIndex = 0; BodySetupIndex < SourcePhysicsAsset->SkeletalBodySetups.Num(); ++BodySetupIndex)
	{
		USkeletalBodySetup* SourceBodySetup = SourcePhysicsAsset->SkeletalBodySetups[BodySetupIndex];
		if (!SourceBodySetup)
		{
			continue;
		}
		
		const FName SourceBoneName = SourceBodySetup->BoneName;
		const int32 SourceBoneIndex = SourceRefSkeleton.FindBoneIndex(SourceBoneName);
		if (SourceBoneIndex == INDEX_NONE)
		{
			continue;
		}
		
		if (!TrimmedRefSkeleton.GetRawRefBoneNames().Contains(SourceBodySetup->BoneName))
		{
			continue;
		}
		
		USkeletalBodySetup* NewBodySetup = DuplicateObject<USkeletalBodySetup>(SourceBodySetup, NewPhysicsAsset);
		if (!NewBodySetup)
		{
			continue;
		}
		
		NewBodySetup->BoneName = SourceBoneName;
		NewBodySetup->AggGeom = SourceBodySetup->AggGeom;
		NewBodySetup->CollisionTraceFlag = SourceBodySetup->CollisionTraceFlag;
		NewBodySetup->PhysicsType = SourceBodySetup->PhysicsType;
		NewBodySetup->bConsiderForBounds = SourceBodySetup->bConsiderForBounds;
		NewBodySetup->bMeshCollideAll = SourceBodySetup->bMeshCollideAll;
		NewBodySetup->bDoubleSidedGeometry = SourceBodySetup->bDoubleSidedGeometry;
		NewBodySetup->PhysMaterial = SourceBodySetup->PhysMaterial;
		NewBodySetup->CollisionReponse = SourceBodySetup->CollisionReponse;
		NewBodySetup->BodySetupGuid = SourceBodySetup->BodySetupGuid;
		
		NewPhysicsAsset->SkeletalBodySetups.Add(NewBodySetup);
	}
	
	for (int32 ConstraintIndex = 0; ConstraintIndex < SourcePhysicsAsset->ConstraintSetup.Num(); ++ConstraintIndex)
	{
		UPhysicsConstraintTemplate* SourceConstraint = SourcePhysicsAsset->ConstraintSetup[ConstraintIndex];
		if (!SourceConstraint)
		{
			continue;
		}
		
		const FName SourceConstraintBone1Name = SourceConstraint->DefaultInstance.ConstraintBone1;
		const FName SourceConstraintBone2Name = SourceConstraint->DefaultInstance.ConstraintBone2;
		
		const int32 SourceConstraintBone1Index = SourceRefSkeleton.FindBoneIndex(SourceConstraintBone1Name);
		const int32 SourceConstraintBone2Index = SourceRefSkeleton.FindBoneIndex(SourceConstraintBone2Name);
		
		if (SourceConstraintBone1Index == INDEX_NONE || SourceConstraintBone2Index == INDEX_NONE)
		{
			continue;
		}
	
		if (!TrimmedRefSkeleton.GetRawRefBoneNames().Contains(SourceConstraint->DefaultInstance.ConstraintBone1) || !TrimmedRefSkeleton.GetRawRefBoneNames().Contains(SourceConstraint->DefaultInstance.ConstraintBone2))
		{
			continue;
		}
		
		UPhysicsConstraintTemplate* NewConstraint = DuplicateObject<UPhysicsConstraintTemplate>(SourceConstraint, NewPhysicsAsset);
		if (!NewConstraint)
		{
			continue;
		}
		
		NewConstraint->DefaultInstance = SourceConstraint->DefaultInstance;
		
		FRigidBodyIndexPair DisablePair;
		DisablePair.Indices[0] = SourceConstraintBone1Index;
		DisablePair.Indices[1] = SourceConstraintBone2Index;
		NewPhysicsAsset->CollisionDisableTable.Add(DisablePair, false);

		NewPhysicsAsset->ConstraintSetup.Add(NewConstraint);
	}
	
	NewPhysicsAsset->UpdateBodySetupIndexMap();
	NewPhysicsAsset->UpdateBoundsBodiesArray();
	
	return NewPhysicsAsset;
}

template <typename VertexDataType>
void UAPSliceableSkeletalMeshComponent::CopyVertexFromSource(VertexDataType& DestVert, const FSkeletalMeshLODRenderData& SourceLODData, int32 SourceVertIndex)
{
	DestVert.Position = SourceLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(SourceVertIndex);
	DestVert.TangentX = SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(SourceVertIndex);
	DestVert.TangentZ = SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(SourceVertIndex);

	uint32 LODNumTexCoords = SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
	const uint32 ValidLoopCount = FMath::Min(VertexDataType::NumTexCoords, LODNumTexCoords);
	for (uint32 UVIndex = 0; UVIndex < ValidLoopCount; ++UVIndex)
	{
		FVector2D UVs = FVector2D(SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV_Typed<VertexDataType::StaticMeshVertexUVType>(SourceVertIndex, UVIndex));
		DestVert.UVs[UVIndex] = FVector2f(UVs);
	}
	
	for (uint32 UVIndex = ValidLoopCount; UVIndex < VertexDataType::NumTexCoords; ++UVIndex)
	{
		DestVert.UVs[UVIndex] = FVector2f::ZeroVector;
	}
}

template <typename VertexDataType>
void UAPSliceableSkeletalMeshComponent::GenerateLODModel(USkeletalMesh* DestMesh, USkeletalMesh* SourceMesh, const TSet<uint32>& VertexIDs, const TSet<uint32>& IntersectingVertices, const FPlane& SlicePlane, bool bShouldCapSlice, UWorld* World, USkeleton* TrimmedSkeleton, UPhysicsAsset* TrimmedPhysicsAsset, int32 LODIdx)
{
	if (!SourceMesh || !DestMesh)
	{
		return;
	}

	USkeleton* TargetSkeleton = TrimmedSkeleton ? TrimmedSkeleton : SourceMesh->GetSkeleton();
	if (!TargetSkeleton)
	{
		return;
	}

	const FReferenceSkeleton& TargetRefSkeleton = TargetSkeleton->GetReferenceSkeleton();
	const FReferenceSkeleton& SourceRefSkeleton = SourceMesh->GetRefSkeleton();

	int32 TargetRootBoneIndex = INDEX_NONE;
	for (int32 TargetBoneIndex = 0; TargetBoneIndex < TargetRefSkeleton.GetNum(); ++TargetBoneIndex)
	{
		if (TargetRefSkeleton.GetParentIndex(TargetBoneIndex) == INDEX_NONE)
		{
			TargetRootBoneIndex = TargetBoneIndex;
			break;
		}
	}
	if (TargetRootBoneIndex == INDEX_NONE)
	{
		TargetRootBoneIndex = 0;
	}
	
	TMap<int32, int32> OldToNewBoneIndexMap;
	if (TrimmedSkeleton)
	{
		for (int32 NewBoneIndex = 0; NewBoneIndex < TargetRefSkeleton.GetNum(); ++NewBoneIndex)
		{
			const FName BoneName = TargetRefSkeleton.GetBoneName(NewBoneIndex);
			const int32 OldBoneIndex = SourceRefSkeleton.FindBoneIndex(BoneName);
			if (OldBoneIndex != INDEX_NONE)
			{
				OldToNewBoneIndexMap.Add(OldBoneIndex, NewBoneIndex);
			}
		}
	}
	else
	{
		for (int32 BoneIndex = 0; BoneIndex < SourceRefSkeleton.GetNum(); ++BoneIndex)
		{
			OldToNewBoneIndexMap.Add(BoneIndex, BoneIndex);
		}
	}

	FSkeletalMeshRenderData* SourceResource = SourceMesh->GetResourceForRendering();
	if (!SourceResource || !SourceResource->LODRenderData.IsValidIndex(LODIdx))
	{
		return;
	}

	const FSkeletalMeshLODRenderData& SourceLODData = SourceResource->LODRenderData[LODIdx];
	const FRawStaticIndexBuffer16or32Interface* SourceIndexBuffer = SourceLODData.MultiSizeIndexContainer.GetIndexBuffer();
	const FSkinWeightVertexBuffer* SkinWeightBuffer = SourceLODData.GetSkinWeightVertexBuffer();

	if (!SourceIndexBuffer || !SkinWeightBuffer)
	{
		return;
	}

	TArray<FFinalSkinVertex> SkinnedVertices;
	GetCPUSkinnedVertices(SkinnedVertices, LODIdx);
	const bool bHasValidSkinnedVertices = SkinnedVertices.Num() == static_cast<int32>(SkinWeightBuffer->GetNumVertices());

	TArray<FAPSkeletalMeshSurface> Surfaces;
	TArray<UMaterialInterface*> SurfacesMaterial;
	
	uint32 TotalNumUVs = SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
	const int32 MaxColorIdx = SourceLODData.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices();

	TArray<FVector> Vectors;
	for (uint32 VertexIndex = 0; VertexIndex < static_cast<uint32>(SkinWeightBuffer->GetNumVertices()); ++VertexIndex)
	{
		const FVector3f VertexPositionLocalFloat = SourceLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
		const FVector VertexPositionLocal = FVector(VertexPositionLocalFloat);

		Vectors.Add(VertexPositionLocal);
	}

	auto ResolveTargetBoneIndexFromSourceBoneIndex = [&](int32 SourceSkeletonBoneIndex)->int32
		{
			if (!TrimmedSkeleton)
			{
				return SourceSkeletonBoneIndex;
			}

			int32 CurrentSourceSkeletonBoneIndex = SourceSkeletonBoneIndex;
			while (CurrentSourceSkeletonBoneIndex != INDEX_NONE)
			{
				const int32* FoundTargetBoneIndex = OldToNewBoneIndexMap.Find(CurrentSourceSkeletonBoneIndex);
				if (FoundTargetBoneIndex != nullptr)
				{
					return *FoundTargetBoneIndex;
				}
				CurrentSourceSkeletonBoneIndex = SourceRefSkeleton.GetParentIndex(CurrentSourceSkeletonBoneIndex);
			}

			return TargetRootBoneIndex;
		};

	for (int32 SectionIdx = 0; SectionIdx < SourceLODData.RenderSections.Num(); SectionIdx++)
	{
		const FSkelMeshRenderSection& Section = SourceLODData.RenderSections[SectionIdx];
		
		bool bSectionHasValidVertices = false;
		for (uint32 VertexIndexInSection = 0; VertexIndexInSection < Section.NumVertices; ++VertexIndexInSection)
		{
			const uint32 GlobalVertexIndex = Section.BaseVertexIndex + VertexIndexInSection;
			if (VertexIDs.Contains(GlobalVertexIndex))
			{
				bSectionHasValidVertices = true;
				break;
			}
		}

		if (!bSectionHasValidVertices)
		{
			continue;
		}

		FAPSkeletalMeshSurface& Surface = Surfaces.Emplace_GetRef();
		Surface.MaterialIndex = Surfaces.Num() - 1;
		
		TMap<int32, int32> SectionSourceToLocalIndexMap;
		const int32 MaxVertIdx = FMath::Min<int32>(
			Section.BaseVertexIndex + Section.NumVertices,
			SourceLODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices()
		);
		
		for (int32 VertIndex = Section.BaseVertexIndex; VertIndex < MaxVertIdx; VertIndex++)
		{
			if (!VertexIDs.Contains(VertIndex))
			{
				continue;
			}
			
			const int32 NewVertexIndex = Surface.Vertices.Num();
			SectionSourceToLocalIndexMap.Add(VertIndex, NewVertexIndex);
			
			VertexDataType TempVertex;
			CopyVertexFromSource(TempVertex, SourceLODData, VertIndex);
			
			if (bHasValidSkinnedVertices)
			{
				Surface.Vertices.Add(FVector(SkinnedVertices[VertIndex].Position));
			}
			else
			{
				Surface.Vertices.Add(FVector(TempVertex.Position));
			}
			Surface.Tangents.Add(TempVertex.TangentX.ToFVector());
			Surface.Normals.Add(TempVertex.TangentZ.ToFVector());
			
			TArray<FVector2D>& VertexUVs = Surface.TextureCoordinates.Emplace_GetRef();
			VertexUVs.SetNum(TotalNumUVs);
			for (uint32 UVIndex = 0; UVIndex < TotalNumUVs; ++UVIndex)
			{
				if (UVIndex < VertexDataType::NumTexCoords)
				{
					VertexUVs[UVIndex] = FVector2D(TempVertex.UVs[UVIndex]);
				}
				else
				{
					VertexUVs[UVIndex] = FVector2D::ZeroVector;
				}
			}
			
			if (SourceMesh->GetHasVertexColors())
			{
				if (VertIndex < MaxColorIdx)
				{
					const FColor& SourceColor = SourceLODData.StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertIndex);
					Surface.Colors.Add(SourceColor);
				}
				else
				{
					Surface.Colors.Add(FColor(255, 255, 255, 255));
				}
			}
			
			
			const FVector TangentY = FVector::CrossProduct(Surface.Normals[NewVertexIndex], Surface.Tangents[NewVertexIndex]);
			const FVector ActualBinormal = FVector(SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertIndex));
			Surface.FlipBinormalSigns.Add(FVector::DotProduct(ActualBinormal, TangentY) < 0.99f);
			
			
			const FSkinWeightInfo& SourceSkinWeight = SkinWeightBuffer->GetVertexSkinWeights(VertIndex);
			TArray<FAPSkeletalBoneInfluence>& VertexInfluences = Surface.BoneInfluences.Emplace_GetRef();

			TMap<int32, float> CombinedBoneWeights;
			
			for (int32 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES; ++InfluenceIdx)
			{
				if (SourceSkinWeight.InfluenceWeights[InfluenceIdx] > 0)
				{
					const FBoneIndexType SourceBoneMapIndex = SourceSkinWeight.InfluenceBones[InfluenceIdx];
					if (Section.BoneMap.IsValidIndex(SourceBoneMapIndex))
					{
						const FBoneIndexType OldSkeletonBoneIndex = Section.BoneMap[SourceBoneMapIndex];
						const int32 NewBoneIndex = ResolveTargetBoneIndexFromSourceBoneIndex(OldSkeletonBoneIndex);
						if (NewBoneIndex != INDEX_NONE)
						{
							const float Weight = static_cast<float>(SourceSkinWeight.InfluenceWeights[InfluenceIdx]) / 65535.0f;
							float& CombinedWeight = CombinedBoneWeights.FindOrAdd(NewBoneIndex, 0.0f);
							CombinedWeight += Weight;
						}
					}
				}
			}

			for (const TPair<int32, float>& CombinedBoneWeight : CombinedBoneWeights)
			{
				VertexInfluences.Add(FAPSkeletalBoneInfluence(NewVertexIndex, CombinedBoneWeight.Key, CombinedBoneWeight.Value));
			}

			if (VertexInfluences.Num() == 0)
			{
				VertexInfluences.Add(FAPSkeletalBoneInfluence(NewVertexIndex, TargetRootBoneIndex, 1.0f));
			}
			
			UAPSliceableSkeletalMeshComponent::RenormalizeBoneInfluenceWeights(VertexInfluences);
			VertexInfluences.Sort(&UAPSliceableSkeletalMeshComponent::CompareBoneInfluences);
			

			/*
			const FBoneIndexType RootSkeletonBoneIndex = 0;
			const int32* NewRootBoneIndex = OldToNewBoneIndexMap.Find(RootSkeletonBoneIndex);
			if (NewRootBoneIndex)
			{
				VertexInfluences.Add(FAPSkeletalBoneInfluence(NewVertexIndex, *NewRootBoneIndex, 1.0f));
			}
			*/
			
		}
		
		for (uint32 TriangleIdx = 0; TriangleIdx < Section.NumTriangles; ++TriangleIdx)
		{
			const uint32 IndexBufferOffset = Section.BaseIndex + TriangleIdx * 3;
			const uint32 V0_Old = SourceIndexBuffer->Get(IndexBufferOffset + 0);
			const uint32 V1_Old = SourceIndexBuffer->Get(IndexBufferOffset + 1);
			const uint32 V2_Old = SourceIndexBuffer->Get(IndexBufferOffset + 2);
			
			if (SectionSourceToLocalIndexMap.Contains(V0_Old) && SectionSourceToLocalIndexMap.Contains(V1_Old) && SectionSourceToLocalIndexMap.Contains(V2_Old))
			{
				Surface.Indices.Add(SectionSourceToLocalIndexMap[V0_Old]);
				Surface.Indices.Add(SectionSourceToLocalIndexMap[V1_Old]);
				Surface.Indices.Add(SectionSourceToLocalIndexMap[V2_Old]);
			}
		}
		
		if (SourceMesh->GetMaterials().IsValidIndex(SectionIdx))
		{
			SurfacesMaterial.Add(SourceMesh->GetMaterials()[SectionIdx].MaterialInterface);
		}
		else
		{
			SurfacesMaterial.Add(nullptr);
		}
	}

	if (Surfaces.Num() == 0)
	{
		return;
	}

	
	if (bShouldCapSlice && IntersectingVertices.Num() > 0)
	{
		TSet<uint32> CapVertices;
		for (uint32 VertexIndex : IntersectingVertices)
		{
			if (VertexIDs.Contains(VertexIndex))
			{
				CapVertices.Add(VertexIndex);
			}
		}
		
		if (CapVertices.Num() >= 3)
		{
			FAPSkeletalMeshSurface& LastSurface = Surfaces.Last();
			
			const FVector PlaneNormal = SlicePlane.GetNormal();
			FVector PlaneTangent = FVector::CrossProduct(PlaneNormal, FVector::UpVector).GetSafeNormal();
			if (PlaneTangent.IsNearlyZero())
			{
				PlaneTangent = FVector::CrossProduct(PlaneNormal, FVector::RightVector).GetSafeNormal();
			}
			const FVector PlaneBitangent = FVector::CrossProduct(PlaneNormal, PlaneTangent).GetSafeNormal();
			
			FVector CenterPosition = FVector::ZeroVector;
			TArray<uint32> UnorderedVertexIndices = CapVertices.Array();
			for (uint32 VertexIndex : UnorderedVertexIndices)
			{
				const FVector VertexPosition = bHasValidSkinnedVertices ? FVector(SkinnedVertices[VertexIndex].Position) : FVector(SourceLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
				CenterPosition += VertexPosition;
			}
			CenterPosition /= UnorderedVertexIndices.Num();
			const FVector ProjectedCenter = CenterPosition - SlicePlane.PlaneDot(CenterPosition) * PlaneNormal;

			float SignedDistanceSum = 0.0f;
			for (uint32 VertexIndex : UnorderedVertexIndices)
			{
				const FVector VertexPosition = bHasValidSkinnedVertices ? FVector(SkinnedVertices[VertexIndex].Position) : FVector(SourceLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
				SignedDistanceSum += SlicePlane.PlaneDot(VertexPosition);
			}
			const bool bMeshOnPositiveSide = SignedDistanceSum > 0.0f;
			const FVector CapNormal = bMeshOnPositiveSide ? -PlaneNormal : PlaneNormal;
			const bool bReverseWinding = FVector::DotProduct(CapNormal, PlaneNormal) < 0.0f;

			struct FCapVertexSortData
			{
				uint32 VertexIndex = 0;
				float Angle = 0.0f;
			};

			TArray<FCapVertexSortData> SortedCapVertices;
			SortedCapVertices.Reserve(UnorderedVertexIndices.Num());
			for (uint32 VertexIndex : UnorderedVertexIndices)
			{
				const FVector VertexPosition = bHasValidSkinnedVertices ? FVector(SkinnedVertices[VertexIndex].Position) : FVector(SourceLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
				const FVector ProjectedPosition = VertexPosition - SlicePlane.PlaneDot(VertexPosition) * PlaneNormal;
				const FVector Delta = ProjectedPosition - ProjectedCenter;

				const float CoordinateAlongPlaneTangent = FVector::DotProduct(Delta, PlaneTangent);
				const float CoordinateAlongPlaneBitangent = FVector::DotProduct(Delta, PlaneBitangent);
				const float Angle = FMath::Atan2(CoordinateAlongPlaneBitangent, CoordinateAlongPlaneTangent);

				FCapVertexSortData& NewItem = SortedCapVertices.Emplace_GetRef();
				NewItem.VertexIndex = VertexIndex;
				NewItem.Angle = Angle;
			}
			SortedCapVertices.Sort([](const FCapVertexSortData& Left, const FCapVertexSortData& Right)
				{
					return Left.Angle < Right.Angle;
				});
			
			const int32 CenterVertexIndex = LastSurface.Vertices.Num();
			LastSurface.Vertices.Add(ProjectedCenter);
			LastSurface.Tangents.Add(PlaneTangent);
			LastSurface.Normals.Add(CapNormal);
			
			TArray<FVector2D>& CenterUVs = LastSurface.TextureCoordinates.Emplace_GetRef();
			CenterUVs.SetNum(TotalNumUVs);
			for (uint32 UVIdx = 0; UVIdx < TotalNumUVs; ++UVIdx)
			{
				CenterUVs[UVIdx] = FVector2D::ZeroVector;
			}
			
			if (DestMesh->GetHasVertexColors())
			{
				LastSurface.Colors.Add(FColor(255, 255, 255, 255));
			}
			
			LastSurface.FlipBinormalSigns.Add(false);
			
			TMap<int32, float> CombinedBoneWeights;
			for (const FCapVertexSortData& CapVertexData : SortedCapVertices)
			{
				const uint32 VertexIndex = CapVertexData.VertexIndex;
				const FSkinWeightInfo& SourceSkinWeight = SkinWeightBuffer->GetVertexSkinWeights(VertexIndex);
				for (int32 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES; ++InfluenceIdx)
				{
					if (SourceSkinWeight.InfluenceWeights[InfluenceIdx] > 0)
					{
						const FBoneIndexType SourceBoneMapIndex = SourceSkinWeight.InfluenceBones[InfluenceIdx];
						const FSkelMeshRenderSection* SourceSection = nullptr;
						for (const FSkelMeshRenderSection& Section : SourceLODData.RenderSections)
						{
							if (Section.BaseVertexIndex <= VertexIndex && VertexIndex < Section.BaseVertexIndex + Section.NumVertices)
							{
								SourceSection = &Section;
								break;
							}
						}
						if (SourceSection && SourceSection->BoneMap.IsValidIndex(SourceBoneMapIndex))
						{
							const FBoneIndexType OldSkeletonBoneIndex = SourceSection->BoneMap[SourceBoneMapIndex];
							const int32 NewBoneIndex = ResolveTargetBoneIndexFromSourceBoneIndex(OldSkeletonBoneIndex);
							if (NewBoneIndex != INDEX_NONE)
							{
								const float Weight = static_cast<float>(SourceSkinWeight.InfluenceWeights[InfluenceIdx]) / 65535.0f;
								float& ExistingWeight = CombinedBoneWeights.FindOrAdd(NewBoneIndex, 0.0f);
								ExistingWeight += Weight;
							}
						}
					}
				}
			}
			
			TArray<FAPSkeletalBoneInfluence>& CenterInfluences = LastSurface.BoneInfluences.Emplace_GetRef();
			for (const auto& BoneWeight : CombinedBoneWeights)
			{
				CenterInfluences.Add(FAPSkeletalBoneInfluence(CenterVertexIndex, BoneWeight.Key, BoneWeight.Value));
			}
			if (CenterInfluences.Num() == 0)
			{
				CenterInfluences.Add(FAPSkeletalBoneInfluence(CenterVertexIndex, TargetRootBoneIndex, 1.0f));
			}
			UAPSliceableSkeletalMeshComponent::RenormalizeBoneInfluenceWeights(CenterInfluences);
			CenterInfluences.Sort(&UAPSliceableSkeletalMeshComponent::CompareBoneInfluences);
			
			for (const FCapVertexSortData& CapVertexData : SortedCapVertices)
			{
				const uint32 VertexIndex = CapVertexData.VertexIndex;
				const FVector VertexPosition = bHasValidSkinnedVertices ? FVector(SkinnedVertices[VertexIndex].Position) : FVector(SourceLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
				const FVector ProjectedPos = VertexPosition - SlicePlane.PlaneDot(VertexPosition) * PlaneNormal;
				
				const int32 NewVertexIndex = LastSurface.Vertices.Num();
				LastSurface.Vertices.Add(ProjectedPos);
				LastSurface.Tangents.Add(PlaneTangent);
				LastSurface.Normals.Add(CapNormal);
				
				TArray<FVector2D>& VertexUVs = LastSurface.TextureCoordinates.Emplace_GetRef();
				VertexUVs.SetNum(TotalNumUVs);
				for (uint32 UVIdx = 0; UVIdx < TotalNumUVs; ++UVIdx)
				{
					VertexUVs[UVIdx] = FVector2D::ZeroVector;
				}
				
				if (DestMesh->GetHasVertexColors())
				{
					LastSurface.Colors.Add(FColor(255, 255, 255, 255));
				}
				
				LastSurface.FlipBinormalSigns.Add(false);
				
				const FSkinWeightInfo& SourceSkinWeight = SkinWeightBuffer->GetVertexSkinWeights(VertexIndex);
				TArray<FAPSkeletalBoneInfluence>& VertexInfluences = LastSurface.BoneInfluences.Emplace_GetRef();

				TMap<int32, float> CapVertexCombinedBoneWeights;
				for (int32 InfluenceIdx = 0; InfluenceIdx < MAX_TOTAL_INFLUENCES; ++InfluenceIdx)
				{
					if (SourceSkinWeight.InfluenceWeights[InfluenceIdx] > 0)
					{
						const FBoneIndexType SourceBoneMapIndex = SourceSkinWeight.InfluenceBones[InfluenceIdx];
						const FSkelMeshRenderSection* SourceSection = nullptr;
						for (const FSkelMeshRenderSection& Section : SourceLODData.RenderSections)
						{
							if (Section.BaseVertexIndex <= VertexIndex && VertexIndex < Section.BaseVertexIndex + Section.NumVertices)
							{
								SourceSection = &Section;
								break;
							}
						}
						if (SourceSection && SourceSection->BoneMap.IsValidIndex(SourceBoneMapIndex))
						{
							const FBoneIndexType OldSkeletonBoneIndex = SourceSection->BoneMap[SourceBoneMapIndex];
							const int32 NewBoneIndex = ResolveTargetBoneIndexFromSourceBoneIndex(OldSkeletonBoneIndex);
							if (NewBoneIndex != INDEX_NONE)
							{
								const float Weight = static_cast<float>(SourceSkinWeight.InfluenceWeights[InfluenceIdx]) / 65535.0f;
								float& ExistingWeight = CapVertexCombinedBoneWeights.FindOrAdd(NewBoneIndex, 0.0f);
								ExistingWeight += Weight;
							}
						}
					}
				}

				for (const TPair<int32, float>& BoneWeight : CapVertexCombinedBoneWeights)
				{
					VertexInfluences.Add(FAPSkeletalBoneInfluence(NewVertexIndex, BoneWeight.Key, BoneWeight.Value));
				}
				if (VertexInfluences.Num() == 0)
				{
					VertexInfluences.Add(FAPSkeletalBoneInfluence(NewVertexIndex, TargetRootBoneIndex, 1.0f));
				}
				
				UAPSliceableSkeletalMeshComponent::RenormalizeBoneInfluenceWeights(VertexInfluences);
				VertexInfluences.Sort(&UAPSliceableSkeletalMeshComponent::CompareBoneInfluences);
			}
			
			if (SortedCapVertices.Num() >= 3)
			{
				for (int32 VertexLoopIndex = 0; VertexLoopIndex < SortedCapVertices.Num(); ++VertexLoopIndex)
				{
					const int32 NextLoopIndex = (VertexLoopIndex + 1) % SortedCapVertices.Num();
					const int32 VertexIndexCurrent = CenterVertexIndex + 1 + VertexLoopIndex;
					const int32 VertexIndexNext = CenterVertexIndex + 1 + NextLoopIndex;
					
					LastSurface.Indices.Add(CenterVertexIndex);
					if (bReverseWinding)
					{
						LastSurface.Indices.Add(VertexIndexNext);
						LastSurface.Indices.Add(VertexIndexCurrent);
					}
					else
					{
						LastSurface.Indices.Add(VertexIndexCurrent);
						LastSurface.Indices.Add(VertexIndexNext);
					}
					
				}
			}
		}
	}

	DestMesh->SetSkeleton(TargetSkeleton);
	DestMesh->SetRefSkeleton(TargetRefSkeleton);
	
	FAPSkeletalMeshGenerator::GenerateSkeletalMesh(
		DestMesh,
		Surfaces,
		SurfacesMaterial,
		false,
		TMap<FName, FTransform>()
	);

	DestMesh->CalculateInvRefMatrices();
}

bool UAPSliceableSkeletalMeshComponent::IsTwistBoneForVertex(const FReferenceSkeleton* RefSkeleton, int32 BoneIndex) const
{
	if (!RefSkeleton || BoneIndex == INDEX_NONE)
	{
		return false;
	}
	const FName BoneName = RefSkeleton->GetBoneName(BoneIndex);
	const FString BoneNameString = BoneName.ToString();
	return BoneNameString.Contains(TEXT("twist"), ESearchCase::IgnoreCase);
}

int32 UAPSliceableSkeletalMeshComponent::FindNonTwistParent(const FReferenceSkeleton* RefSkeleton, int32 BoneIndex) const
{
	if (!RefSkeleton || BoneIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	int32 CurrentBoneIndex = BoneIndex;
	while (CurrentBoneIndex != INDEX_NONE)
	{
		if (!IsTwistBoneForVertex(RefSkeleton, CurrentBoneIndex))
		{
			return CurrentBoneIndex;
		}
		CurrentBoneIndex = RefSkeleton->GetParentIndex(CurrentBoneIndex);
	}

	return INDEX_NONE;
}

bool UAPSliceableSkeletalMeshComponent::GetBoneSide(const TArray<FTransform>& ComponentSpaceTransforms, const FPlane& LocalSlicePlane, float PlaneTolerance, int32 BoneIndex) const
{
	if (!ComponentSpaceTransforms.IsValidIndex(BoneIndex))
	{
		return false;
	}
	const FVector BoneLocation = ComponentSpaceTransforms[BoneIndex].GetLocation();
	const float DistanceToPlane = LocalSlicePlane.PlaneDot(BoneLocation);
	return DistanceToPlane > -PlaneTolerance;
}

int32 UAPSliceableSkeletalMeshComponent::FindChildBoneOnSide(const FReferenceSkeleton* RefSkeleton, int32 ParentBoneIndex, bool bTargetSide, const FSkinWeightInfo& SkinWeight, int32 InfluenceCount, const TArray<FTransform>& ComponentSpaceTransforms, const FPlane& LocalSlicePlane, float PlaneTolerance) const
{
	if (!RefSkeleton || ParentBoneIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	
	TMap<int32, uint16> BoneWeights;
	for (int32 InfluenceIndex = 0; InfluenceIndex < static_cast<int32>(InfluenceCount); ++InfluenceIndex)
	{
		const int32 BoneIndex = SkinWeight.InfluenceBones[InfluenceIndex];
		if (BoneIndex != INDEX_NONE)
		{
			BoneWeights.Add(BoneIndex, SkinWeight.InfluenceWeights[InfluenceIndex]);
		}
	}
	
	int32 BestChildBoneIndex = INDEX_NONE;
	uint16 BestWeight = 0;
	
	TArray<int32> BonesToCheck;
	for (int32 ChildIndex = 0; ChildIndex < RefSkeleton->GetNum(); ++ChildIndex)
	{
		if (RefSkeleton->GetParentIndex(ChildIndex) == ParentBoneIndex)
		{
			BonesToCheck.Add(ChildIndex);
		}
	}
	
	while (BonesToCheck.Num() > 0)
	{
		const int32 CurrentBoneIndex = BonesToCheck.Pop();
		
		if (IsTwistBoneForVertex(RefSkeleton, CurrentBoneIndex))
		{
			for (int32 ChildIndex = 0; ChildIndex < RefSkeleton->GetNum(); ++ChildIndex)
			{
				if (RefSkeleton->GetParentIndex(ChildIndex) == CurrentBoneIndex)
				{
					BonesToCheck.Add(ChildIndex);
				}
			}
			continue;
		}
		
		const bool bCurrentBoneSide = GetBoneSide(ComponentSpaceTransforms, LocalSlicePlane, PlaneTolerance, CurrentBoneIndex);
		if (bCurrentBoneSide == bTargetSide)
		{
			const uint16* WeightPtr = BoneWeights.Find(CurrentBoneIndex);
			if (WeightPtr && *WeightPtr > BestWeight)
			{
				BestWeight = *WeightPtr;
				BestChildBoneIndex = CurrentBoneIndex;
			}
		}
		
		for (int32 ChildIndex = 0; ChildIndex < RefSkeleton->GetNum(); ++ChildIndex)
		{
			if (RefSkeleton->GetParentIndex(ChildIndex) == CurrentBoneIndex)
			{
				BonesToCheck.Add(ChildIndex);
			}
		}
	}
	
	return BestChildBoneIndex;
}

int32 UAPSliceableSkeletalMeshComponent::GetDominantBoneForVertex(uint32 VertexIndex, bool bVertexOnPositiveSide, const FReferenceSkeleton* RefSkeleton, const TArray<FTransform>& ComponentSpaceTransforms, const FPlane& LocalSlicePlane, float PlaneTolerance) const
{
	if (!BaseVertexWeights.IsValidIndex(VertexIndex))
	{
		return INDEX_NONE;
	}

	const FSkinWeightInfo& SkinWeight = BaseVertexWeights[VertexIndex];
	int32 DominantBoneIndex = INDEX_NONE;
	uint16 MaxWeight = 0;

	for (int32 InfluenceIndex = 0; InfluenceIndex < static_cast<int32>(MaxInfluence); ++InfluenceIndex)
	{
		const int32 BoneIndex = SkinWeight.InfluenceBones[InfluenceIndex];
		
		if (SkinWeight.InfluenceWeights[InfluenceIndex] > MaxWeight)
		{
			MaxWeight = SkinWeight.InfluenceWeights[InfluenceIndex];
			DominantBoneIndex = BoneIndex;
		}
	}

	if (IsTwistBoneForVertex(RefSkeleton, DominantBoneIndex))
	{
		DominantBoneIndex = FindNonTwistParent(RefSkeleton, DominantBoneIndex);
	}
	
	if (DominantBoneIndex != INDEX_NONE)
	{
		const bool bBoneSide = GetBoneSide(ComponentSpaceTransforms, LocalSlicePlane, PlaneTolerance, DominantBoneIndex);
		if (bBoneSide != bVertexOnPositiveSide)
		{
			const int32 ChildBoneOnSide = FindChildBoneOnSide(RefSkeleton, DominantBoneIndex, bVertexOnPositiveSide, SkinWeight, MaxInfluence, ComponentSpaceTransforms, LocalSlicePlane, PlaneTolerance);
			if (ChildBoneOnSide != INDEX_NONE)
			{
				DominantBoneIndex = ChildBoneOnSide;
			}
		}
	}

	return DominantBoneIndex;
}


bool UAPSliceableSkeletalMeshComponent::IsTwistBone(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex) const
{
	const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
	const FString BoneNameString = BoneName.ToString();
	return BoneNameString.Contains(TEXT("twist"), ESearchCase::IgnoreCase);
}

bool UAPSliceableSkeletalMeshComponent::IsEndBone(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex) const
{
	if (BoneIndex == INDEX_NONE || !RefSkeleton.IsValidIndex(BoneIndex))
	{
		return false;
	}

	const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
	const FString BoneNameString = BoneName.ToString();
	const bool bHasEndPrefix = BoneNameString.Contains(TEXT("end"), ESearchCase::IgnoreCase) ||
		BoneNameString.Contains(TEXT("_end"), ESearchCase::IgnoreCase) ||
		BoneNameString.EndsWith(TEXT("End"), ESearchCase::CaseSensitive) ||
		BoneNameString.EndsWith(TEXT("_end"), ESearchCase::IgnoreCase);

	if (!bHasEndPrefix)
	{
		return false;
	}

	bool bHasChildren = false;
	const int32 NumBones = RefSkeleton.GetNum();
	for (int32 ChildIndex = 0; ChildIndex < NumBones; ++ChildIndex)
	{
		if (RefSkeleton.GetParentIndex(ChildIndex) == BoneIndex)
		{
			bHasChildren = true;
			break;
		}
	}

	return !bHasChildren;
}

int32 UAPSliceableSkeletalMeshComponent::FindNonTwistParentForAnalysis(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex) const
{
	if (BoneIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	int32 CurrentBoneIndex = BoneIndex;
	while (CurrentBoneIndex != INDEX_NONE)
	{
		if (!IsTwistBone(RefSkeleton, CurrentBoneIndex))
		{
			return CurrentBoneIndex;
		}
		CurrentBoneIndex = RefSkeleton.GetParentIndex(CurrentBoneIndex);
	}

	return INDEX_NONE;
}

void UAPSliceableSkeletalMeshComponent::FindConnectedComponents(bool bPositiveSide, const TArray<bool>& BoneOnPositiveSide, const TArray<TSet<int32>>& BoneChildren, const FReferenceSkeleton& RefSkeleton, const TArray<int32>& BoneToNonTwistParent, int32 NumBones, TArray<FSkeletonBoneGroup>& OutGroups, TSet<int32>& VisitedBones) const
{
	VisitedBones.Empty();
	OutGroups.Empty();

	for (int32 StartBoneIndex = 0; StartBoneIndex < NumBones; ++StartBoneIndex)
	{
		if (IsTwistBone(RefSkeleton, StartBoneIndex) || IsEndBone(RefSkeleton, StartBoneIndex))
		{
			continue;
		}

		if (BoneOnPositiveSide[StartBoneIndex] != bPositiveSide || VisitedBones.Contains(StartBoneIndex))
		{
			continue;
		}

		TArray<int32> CurrentGroup;
		TArray<int32> Queue;
		Queue.Add(StartBoneIndex);
		VisitedBones.Add(StartBoneIndex);

		while (Queue.Num() > 0)
		{
			const int32 CurrentBoneIndex = Queue[0];
			Queue.RemoveAt(0);
			
			if (!IsTwistBone(RefSkeleton, CurrentBoneIndex) && !IsEndBone(RefSkeleton, CurrentBoneIndex))
			{
				CurrentGroup.Add(CurrentBoneIndex);
			}

			int32 ParentIndex = RefSkeleton.GetParentIndex(CurrentBoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				int32 EffectiveParentIndex = IsTwistBone(RefSkeleton, ParentIndex) ? BoneToNonTwistParent[ParentIndex] : ParentIndex;
				if (EffectiveParentIndex != INDEX_NONE)
				{
					if (BoneOnPositiveSide[EffectiveParentIndex] == bPositiveSide && !VisitedBones.Contains(EffectiveParentIndex))
					{
						Queue.Add(EffectiveParentIndex);
						VisitedBones.Add(EffectiveParentIndex);
					}
				}
			}

			for (int32 ChildIndex : BoneChildren[CurrentBoneIndex])
			{
				if (BoneOnPositiveSide[ChildIndex] == bPositiveSide && !VisitedBones.Contains(ChildIndex))
				{
					Queue.Add(ChildIndex);
					VisitedBones.Add(ChildIndex);
				}
			}
		}

		if (CurrentGroup.Num() > 0)
		{
			OutGroups.Add(FSkeletonBoneGroup(CurrentGroup));
		}
	}
}

bool UAPSliceableSkeletalMeshComponent::CompareBoneWeights(const TPair<FBoneIndexType, uint16>& A, const TPair<FBoneIndexType, uint16>& B)
{
	return A.Value > B.Value;
}

bool UAPSliceableSkeletalMeshComponent::CompareBoneInfluences(const FAPSkeletalBoneInfluence& A, const FAPSkeletalBoneInfluence& B)
{
	return A.Weight > B.Weight;
}

void UAPSliceableSkeletalMeshComponent::RenormalizeBoneInfluenceWeights(TArray<FAPSkeletalBoneInfluence>& Influences)
{
	if (Influences.Num() == 0)
	{
		return;
	}
	float SumWeight = 0.0f;
	for (const FAPSkeletalBoneInfluence& Influence : Influences)
	{
		SumWeight += Influence.Weight;
	}
	constexpr float Epsilon = 1.e-6f;
	if (SumWeight <= Epsilon)
	{
		return;
	}
	const float InvSum = 1.0f / SumWeight;
	for (FAPSkeletalBoneInfluence& Influence : Influences)
	{
		Influence.Weight *= InvSum;
	}
}
