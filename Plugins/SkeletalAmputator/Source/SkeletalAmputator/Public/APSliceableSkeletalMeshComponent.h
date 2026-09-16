// Copyright 2025 SGSAmputationLab and AO. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Components/SkeletalMeshComponent.h"
#include "APSliceableSkeletalMeshComponent.generated.h"

#define ENABLE_LOG_SLICE_BONE_LIST 1
#define ENABLE_VISUALIZE_FIND_BONE 1
#define ENABLE_VISUALIZE_PLANE 1
#define ENABLE_LOG_MERGE_SLICESECTION 0
using namespace UE::Geometry;

struct FSkinWeightInfo;
struct FVertexMappingInfo;
struct FRawLODData;
struct FAPSkeletalBoneInfluence;

class UAPSliceableSkeletalMeshComponent;
class UNiagaraSystem;
class UDynamicMeshComponent;

UENUM(BlueprintType)
enum class EAPSliceSpace : uint8
{
	World,
	Local
};

USTRUCT(BlueprintType)
struct FSliceResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Slice Result")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Slice Result")
	TObjectPtr<UAPSliceableSkeletalMeshComponent> SourceComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Slice Result")
	UAPSliceableSkeletalMeshComponent* PSideSkeletalMeshComp = nullptr;
	
	UPROPERTY(BlueprintReadOnly, Category = "Slice Result")
	UAPSliceableSkeletalMeshComponent* NSideSkeletalMeshComp = nullptr;
	  
	UPROPERTY(BlueprintReadOnly, Category = "Slice Result")
	TArray<USceneComponent*> CapSockets;

	UPROPERTY(BlueprintReadOnly, Category = "Slice Result")
	TArray<UAPSliceableSkeletalMeshComponent*> PositiveFragments;

	UPROPERTY(BlueprintReadOnly, Category = "Slice Result")
	TArray<UAPSliceableSkeletalMeshComponent*> NegativeFragments;

	UPROPERTY(BlueprintReadOnly, Category = "Slice Result")
	TArray<UDynamicMeshComponent*> DetachedMeshComponents;
};

USTRUCT(BlueprintType)
struct FSkeletonBoneGroup
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Skeleton Bone Group")
	TArray<int32> BoneIndices;

	FSkeletonBoneGroup()
	{
		BoneIndices.Empty();
	}

	FSkeletonBoneGroup(const TArray<int32>& InBoneIndices)
		: BoneIndices(InBoneIndices)
	{
	}
};

USTRUCT(BlueprintType)
struct FSkeletonAnalysisResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Skeleton Analysis")
	TArray<FSkeletonBoneGroup> Groups;

	FSkeletonAnalysisResult()
	{
		Groups.Empty();
	}
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SKELETALAMPUTATOR_API UAPSliceableSkeletalMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()
	
public:

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnSliceMesh,
	const FSliceResult&, SliceResult
	);

	UPROPERTY(BlueprintAssignable, Category = "Slice||Events")
	FOnSliceMesh OnSliceMesh;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bRequireEdgeLoop = false;
	
public:
	UAPSliceableSkeletalMeshComponent();
	virtual ~UAPSliceableSkeletalMeshComponent() override;
	
protected:
	virtual void BeginPlay() override;

public:
	UFUNCTION(BlueprintCallable, Category="Slice")
	void SpawnSliceEffect(USceneComponent* TargetSocket);

	UFUNCTION(BlueprintCallable, Category="Slice")
	void SpawnSliceParticle(USceneComponent* TargetSocket);
	
	UFUNCTION(BlueprintCallable, Category = "Slice")
	void SliceMesh(const FPlane& SlicePlane, EAPSliceSpace SliceSpace = EAPSliceSpace::World);

	UFUNCTION(BlueprintCallable, Category = "Slice", meta = (ClampMin = "0.0", Units = "cm"))
	FSliceResult SliceMeshInRadius(const FVector& SliceCenter, const FVector& SliceNormal, float SliceRadius = 25.0f, EAPSliceSpace SliceSpace = EAPSliceSpace::World);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slice|Interior")
	FLinearColor TissueColor = FLinearColor(0.5f, 0.01f, 0.01f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slice|Interior")
	FLinearColor BoneColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slice|Interior", meta = (ClampMin = "0.01"))
	float BoneRadius = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slice|Interior", meta = (ClampMin = "0.0"))
	float TerminalBoneLength = 5.0f;

	UFUNCTION(BlueprintCallable, Category = "Slice")
	FSkeletonAnalysisResult AnalyzeSkeletonByPlane(const FPlane& SlicePlane) const;
	
private:
	FSliceResult SliceMeshInternal(const FPlane& SlicePlane, const FVector* SliceCenter, float SliceRadius, EAPSliceSpace SliceSpace);
	USkeletalMesh* BuildSkeletalMeshBySkeletalGroup(
		const FSkeletonBoneGroup& BoneGroup,
		const FName& AssetNameTag,
		const FPlane& SlicePlane
	);

	void FindVerticesForBoneGroup(const FSkeletonBoneGroup& BoneGroup, USkeletalMesh* SourceMesh, const FPlane& SlicePlane, TSet<uint32>& OutGroupVertices, TSet<uint32>& OutGroupIntersectingVertices) const;
	
	bool HasEdgeLoop(const TSet<uint32>& EdgeVertices, USkeletalMesh* SourceMesh) const;

	void FindConnectedVertexGroups(const TSet<uint32>& Vertices, USkeletalMesh* SourceMesh, TArray<TSet<uint32>>& OutVertexGroups) const;

	UDynamicMeshComponent* CreateDynamicMeshFromVertices(const TSet<uint32>& VertexGroup, USkeletalMesh* SourceMesh, const FPlane& SlicePlane, const FName& ComponentName);

	UAPSliceableSkeletalMeshComponent* BuildSkeletalMeshComponent(USkeletalMesh* SourceMesh, const FName& NewComponentName);

	void ComputeSkinWeightData();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slice")
	TArray<UParticleSystemComponent*> SliceParticleComponents;
	
protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slice")
	TObjectPtr<UMaterialInterface> CapMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Slice")
	FTransform SliceEffectRelativeTransform = FTransform::Identity;

	// TODO: Deprecated, Use in contents logic
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slice")
	TObjectPtr<UNiagaraSystem> SliceEffectSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slice")
	TObjectPtr<UParticleSystem> SliceParticleSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slice")
	bool bUseSliceEffect = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slice")
	bool bUseSliceParticle = false;
	
private:
	UPROPERTY()
	uint32 NumVertices;

	UPROPERTY()
	uint32 MaxInfluence;
	
	UPROPERTY()
	TArray<FColor> BaseVertexColors;

	UPROPERTY()
	TArray<FVector3f> BaseVertexPositions;

	TArray<FSkinWeightInfo> BaseVertexWeights;

	bool BuildSkeletalMeshInternal(USkeletalMesh* DestMesh, USkeletalMesh* SourceMesh, const TSet<uint32>& VertexIDs, const FSkeletonBoneGroup& BoneGroup, UWorld* World);

	bool BuildSkeletalMeshInternal(USkeletalMesh* DestMesh, USkeletalMesh* SourceMesh, const TSet<uint32>& VertexIDs, const TSet<uint32>& IntersectingVertices, const FPlane& SlicePlane, const FSkeletonBoneGroup& BoneGroup, UWorld* World);

	int32 GetMaxLodFromSourceMesh(USkeletalMesh* SourceMesh) const;

	void ReleaseResources(USkeletalMesh* DestMesh, int32 Slack) const;

	template<typename VertexDataType>
	void GenerateLODModel(USkeletalMesh* DestMesh, USkeletalMesh* SourceMesh, const TSet<uint32>& VertexIDs, const TSet<uint32>& IntersectingVertices, const FPlane& SlicePlane, bool bShouldCapSlice, UWorld* World, USkeleton* TrimmedSkeleton, UPhysicsAsset* TrimmedPhysicsAsset, int32 LODIdx);
	
	bool ProcessSkeletalMesh(USkeletalMesh* DestMesh, USkeletalMesh* SourceMesh, USkeleton* TrimmedSkeleton, UPhysicsAsset* TrimmedPhysicsAsset) const;
	
	USkeleton* GenerateTrimmedSkeleton(const FSkeletonBoneGroup& BoneGroup, USkeletalMesh* SourceMesh);

	void RemoveBone(FReferenceSkeleton& TargetRefSkeleton, const FReferenceSkeleton& SourceRefSkeleton, int32 TargetOriginalBoneIndex);

	UPhysicsAsset* GenerateTrimmedPhysicsAsset(USkeleton* InTrimmedSkeleton, USkeletalMesh* SourceMesh);

	template<typename VertexDataType>
	void CopyVertexFromSource(VertexDataType& DestVert, const FSkeletalMeshLODRenderData& SourceLODData, int32 SourceVertIndex);

private:
	static void WakeUpThreadPool();
	
	bool IsTwistBoneForVertex(const FReferenceSkeleton* RefSkeleton, int32 BoneIndex) const;
	
	int32 FindNonTwistParent(const FReferenceSkeleton* RefSkeleton, int32 BoneIndex) const;
	
	bool GetBoneSide(const TArray<FTransform>& ComponentSpaceTransforms, const FPlane& LocalSlicePlane, float PlaneTolerance, int32 BoneIndex) const;
	
	int32 FindChildBoneOnSide(const FReferenceSkeleton* RefSkeleton, int32 ParentBoneIndex, bool bTargetSide, const FSkinWeightInfo& SkinWeight, int32 InfluenceCount, const TArray<FTransform>& ComponentSpaceTransforms, const FPlane& LocalSlicePlane, float PlaneTolerance) const;
	
	int32 GetDominantBoneForVertex(uint32 VertexIndex, bool bVertexOnPositiveSide, const FReferenceSkeleton* RefSkeleton, const TArray<FTransform>& ComponentSpaceTransforms, const FPlane& LocalSlicePlane, float PlaneTolerance) const;
	
	bool IsTwistBone(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex) const;

	bool IsEndBone(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex) const;
	
	int32 FindNonTwistParentForAnalysis(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex) const;
	
	void FindConnectedComponents(bool bPositiveSide, const TArray<bool>& BoneOnPositiveSide, const TArray<TSet<int32>>& BoneChildren, const FReferenceSkeleton& RefSkeleton, const TArray<int32>& BoneToNonTwistParent, int32 NumBones, TArray<FSkeletonBoneGroup>& OutGroups, TSet<int32>& VisitedBones) const;
	
	static bool CompareBoneWeights(const TPair<FBoneIndexType, uint16>& A, const TPair<FBoneIndexType, uint16>& B);
	
	static bool CompareBoneInfluences(const FAPSkeletalBoneInfluence& A, const FAPSkeletalBoneInfluence& B);
	
	static void RenormalizeBoneInfluenceWeights(TArray<FAPSkeletalBoneInfluence>& Influences);
	
public:
	FORCEINLINE UMaterialInterface* GetCapMaterial() const { return CapMaterial; }

	FORCEINLINE uint32 GetMaxInfluence() const { return MaxInfluence; }
};
