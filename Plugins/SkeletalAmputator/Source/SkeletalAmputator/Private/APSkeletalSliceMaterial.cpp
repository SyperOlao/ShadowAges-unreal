
#include "APSkeletalSliceMaterial.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ReferenceSkeleton.h"


UMaterialInterface* CreateSkeletalSliceMaterial(UObject* Owner, UMaterialInterface* Parent, const FReferenceSkeleton& Skeleton, const TArray<FTransform>& BoneTransforms, const FLinearColor& TissueColor, const FLinearColor& BoneColor, float BoneRadius, float TerminalBoneLength)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_CreateInteriorMaterial);
	if (!Parent || Parent->GetPathName() == TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"))
	{
		Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/SkeletalAmputator/Materials/M_SkeletalSliceInterior.M_SkeletalSliceInterior"), nullptr, LOAD_NoWarn);
	}
	if (!Parent)
	{
		Parent = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	TArray<FVector4f> Segments;
	for (int32 BoneIndex = 0; BoneIndex < Skeleton.GetNum(); ++BoneIndex)
	{
		const int32 ParentIndex = Skeleton.GetParentIndex(BoneIndex);
		if (ParentIndex != INDEX_NONE)
		{
			Segments.Add(FVector4f(FVector3f(BoneTransforms[ParentIndex].GetLocation()), 0.0f));
			Segments.Add(FVector4f(FVector3f(BoneTransforms[BoneIndex].GetLocation()), 0.0f));
		}
	}
	if (Segments.IsEmpty())
	{
		Segments.Add(FVector4f(FVector3f(BoneTransforms[0].GetLocation()), 0.0f));
		Segments.Add(FVector4f(FVector3f(BoneTransforms[0].TransformPosition(FVector(TerminalBoneLength, 0.0, 0.0))), 0.0f));
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(2, Segments.Num() / 2, PF_A32B32G32R32F);
	Texture->SRGB = false;
	Texture->Filter = TF_Nearest;
	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Segments.GetData(), Segments.Num() * sizeof(FVector4f));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Owner);
	Material->SetTextureParameterValue(TEXT("BoneSegments"), Texture);
	Material->SetScalarParameterValue(TEXT("BoneCount"), Segments.Num() / 2);
	Material->SetScalarParameterValue(TEXT("BoneRadius"), BoneRadius);
	Material->SetVectorParameterValue(TEXT("TissueColor"), TissueColor);
	Material->SetVectorParameterValue(TEXT("BoneColor"), BoneColor);
	return Material;
}
