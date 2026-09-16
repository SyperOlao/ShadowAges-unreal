#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "APSliceableSkeletalMeshComponent.h"
#include "APSkeletalSliceGeometry.h"
#include "APSkeletalSliceMaterial.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ReferenceSkeleton.h"
#include "SkeletalRenderPublic.h"

static bool HasSliceBodyVolume(const TArray<FVector>& Positions)
{
	if (Positions.Num() < 4)
	{
		return false;
	}
	const FVector Origin = Positions[0];
	FVector Direction = FVector::ZeroVector;
	for (const FVector& Position : Positions)
	{
		if ((Position - Origin).SizeSquared() > Direction.SizeSquared())
		{
			Direction = Position - Origin;
		}
	}
	FVector Normal = FVector::ZeroVector;
	for (const FVector& Position : Positions)
	{
		const FVector Candidate = FVector::CrossProduct(Direction, Position - Origin);
		if (Candidate.SizeSquared() > Normal.SizeSquared())
		{
			Normal = Candidate;
		}
	}
	Normal.Normalize();
	for (const FVector& Position : Positions)
	{
		if (FMath::Abs(FVector::DotProduct(Position - Origin, Normal)) > 0.00001)
		{
			return true;
		}
	}
	return false;
}

static UPhysicsAsset* BuildSlicePhysics(TArray<FAPSkeletalSliceTriangle>& Triangles, const FReferenceSkeleton& Skeleton, const TArray<FTransform>& BoneTransforms, UPhysicsAsset* SourcePhysics, const FReferenceSkeleton& SourceSkeleton, const TArray<FTransform>& SourceTransforms)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_BuildPhysics);
	UPhysicsAsset* Physics = NewObject<UPhysicsAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	TArray<TArray<FVector>> BoneVertices;
	BoneVertices.SetNum(Skeleton.GetNum());
	TArray<int32> PhysicalBoneMapping;
	for (int32 BoneIndex = 0; BoneIndex < Skeleton.GetNum(); ++BoneIndex)
	{
		PhysicalBoneMapping.Add(BoneIndex);
	}
	TSet<int32> SourcePhysicalBones;
	if (SourcePhysics && !SourcePhysics->SkeletalBodySetups.IsEmpty())
	{
		for (int32 BoneIndex = 0; BoneIndex < Skeleton.GetNum(); ++BoneIndex)
		{
			if (SourcePhysics->FindBodyIndex(Skeleton.GetBoneName(BoneIndex)) != INDEX_NONE)
			{
				SourcePhysicalBones.Add(BoneIndex);
			}
		}
	}
	if (!SourcePhysicalBones.IsEmpty())
	{
		for (int32 BoneIndex = 0; BoneIndex < Skeleton.GetNum(); ++BoneIndex)
		{
			int32 PhysicalBone = BoneIndex;
			while (PhysicalBone != INDEX_NONE && !SourcePhysicalBones.Contains(PhysicalBone))
			{
				PhysicalBone = Skeleton.GetParentIndex(PhysicalBone);
			}
			if (PhysicalBone == INDEX_NONE)
			{
				double NearestDistance = TNumericLimits<double>::Max();
				for (int32 Candidate : SourcePhysicalBones)
				{
					const double Distance = FVector::DistSquared(BoneTransforms[BoneIndex].GetLocation(), BoneTransforms[Candidate].GetLocation());
					if (Distance < NearestDistance)
					{
						NearestDistance = Distance;
						PhysicalBone = Candidate;
					}
				}
			}
			PhysicalBoneMapping[BoneIndex] = PhysicalBone;
		}
	}
	for (const FAPSkeletalSliceTriangle& Triangle : Triangles)
	{
		for (const FAPSkeletalSliceVertex& Vertex : Triangle.Vertices)
		{
			for (const TPair<int32, double>& Influence : Vertex.BoneWeights)
			{
				if (Influence.Value > 0.00001)
				{
					BoneVertices[PhysicalBoneMapping[Influence.Key]].Add(Vertex.Position);
				}
			}
		}
	}
	for (int32 BoneIndex = Skeleton.GetNum() - 1; BoneIndex >= 0; --BoneIndex)
	{
		if (PhysicalBoneMapping[BoneIndex] != BoneIndex)
		{
			continue;
		}
		if (!HasSliceBodyVolume(BoneVertices[BoneIndex]))
		{
			int32 ParentIndex = Skeleton.GetParentIndex(BoneIndex);
			while (ParentIndex != INDEX_NONE && !HasSliceBodyVolume(BoneVertices[ParentIndex]))
			{
				ParentIndex = Skeleton.GetParentIndex(ParentIndex);
			}
			if (ParentIndex == INDEX_NONE)
			{
				for (int32 Candidate = 0; Candidate < Skeleton.GetNum(); ++Candidate)
				{
					if (HasSliceBodyVolume(BoneVertices[Candidate]))
					{
						ParentIndex = Candidate;
						break;
					}
				}
			}
			if (ParentIndex != INDEX_NONE)
			{
				BoneVertices[ParentIndex].Append(BoneVertices[BoneIndex]);
				BoneVertices[BoneIndex].Reset();
				PhysicalBoneMapping[BoneIndex] = ParentIndex;
			}
		}
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_RemapPhysicsWeights);
		for (FAPSkeletalSliceTriangle& Triangle : Triangles)
		{
			for (FAPSkeletalSliceVertex& Vertex : Triangle.Vertices)
			{
				TMap<int32, double> NewWeights;
				for (const TPair<int32, double>& Influence : Vertex.BoneWeights)
				{
					int32 BoneIndex = Influence.Key;
					while (PhysicalBoneMapping[BoneIndex] != BoneIndex)
					{
						BoneIndex = PhysicalBoneMapping[BoneIndex];
					}
					NewWeights.FindOrAdd(BoneIndex) += Influence.Value;
				}
				Vertex.BoneWeights = MoveTemp(NewWeights);
			}
		}
	}
	for (int32 BoneIndex = 0; BoneIndex < Skeleton.GetNum(); ++BoneIndex)
	{
		if (!HasSliceBodyVolume(BoneVertices[BoneIndex]))
		{
			continue;
		}
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_BuildConvexBody);
		USkeletalBodySetup* Body = NewObject<USkeletalBodySetup>(Physics);
		Body->BoneName = Skeleton.GetBoneName(BoneIndex);
		Body->PhysicsType = PhysType_Default;
		Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		Body->DefaultInstance.SetCollisionProfileName(TEXT("Ragdoll"));
		Body->DefaultInstance.SetOverrideIterationCounts(true);
		Body->DefaultInstance.PositionSolverIterationCount = 16;
		Body->DefaultInstance.VelocitySolverIterationCount = 4;
		Body->DefaultInstance.LinearDamping = 0.1f;
		Body->DefaultInstance.AngularDamping = 0.5f;
		FKConvexElem& Convex = Body->AggGeom.ConvexElems.Emplace_GetRef();
		for (const FVector& Position : BoneVertices[BoneIndex])
		{
			Convex.VertexData.Add(BoneTransforms[BoneIndex].InverseTransformPosition(Position));
		}
		Convex.UpdateElemBox();
		Body->InvalidatePhysicsData();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_CookConvexBody);
			Body->CreatePhysicsMeshes();
		}
		Physics->SkeletalBodySetups.Add(Body);
	}
	Physics->UpdateBodySetupIndexMap();
	Physics->UpdateBoundsBodiesArray();
	for (const FAPSkeletalSliceTriangle& Triangle : Triangles)
	{
		for (const FAPSkeletalSliceVertex& Vertex : Triangle.Vertices)
		{
			for (const TPair<int32, double>& FirstInfluence : Vertex.BoneWeights)
			{
				for (const TPair<int32, double>& SecondInfluence : Vertex.BoneWeights)
				{
					if (FirstInfluence.Key >= SecondInfluence.Key)
					{
						continue;
					}
					const int32 FirstBody = Physics->FindBodyIndex(Skeleton.GetBoneName(FirstInfluence.Key));
					const int32 SecondBody = Physics->FindBodyIndex(Skeleton.GetBoneName(SecondInfluence.Key));
					if (FirstBody != INDEX_NONE && SecondBody != INDEX_NONE)
					{
						Physics->DisableCollision(FirstBody, SecondBody);
					}
				}
			}
		}
	}
	if (SourcePhysics)
	{
		TSet<uint64> ConnectedBodies;
		for (const UPhysicsConstraintTemplate* SourceConstraint : SourcePhysics->ConstraintSetup)
		{
			const FName FirstName = SourceConstraint->DefaultInstance.ConstraintBone1;
			const FName SecondName = SourceConstraint->DefaultInstance.ConstraintBone2;
			int32 FirstBone = Skeleton.FindBoneIndex(FirstName);
			int32 SecondBone = Skeleton.FindBoneIndex(SecondName);
			if (FirstBone == INDEX_NONE || SecondBone == INDEX_NONE)
			{
				continue;
			}
			while (PhysicalBoneMapping[FirstBone] != FirstBone)
			{
				FirstBone = PhysicalBoneMapping[FirstBone];
			}
			while (PhysicalBoneMapping[SecondBone] != SecondBone)
			{
				SecondBone = PhysicalBoneMapping[SecondBone];
			}
			const int32 FirstBody = Physics->FindBodyIndex(Skeleton.GetBoneName(FirstBone));
			const int32 SecondBody = Physics->FindBodyIndex(Skeleton.GetBoneName(SecondBone));
			if (FirstBody == INDEX_NONE || SecondBody == INDEX_NONE || FirstBody == SecondBody)
			{
				continue;
			}
			const uint64 BodyPair = (uint64(FMath::Min(FirstBody, SecondBody)) << 32) | uint32(FMath::Max(FirstBody, SecondBody));
			if (ConnectedBodies.Contains(BodyPair))
			{
				continue;
			}
			const int32 SourceFirstBone = SourceSkeleton.FindBoneIndex(FirstName);
			const int32 SourceSecondBone = SourceSkeleton.FindBoneIndex(SecondName);
			if (SourceFirstBone == INDEX_NONE || SourceSecondBone == INDEX_NONE)
			{
				continue;
			}
			ConnectedBodies.Add(BodyPair);
			UPhysicsConstraintTemplate* Constraint = DuplicateObject<UPhysicsConstraintTemplate>(SourceConstraint, Physics);
			Constraint->DefaultInstance.ConstraintBone1 = Skeleton.GetBoneName(FirstBone);
			Constraint->DefaultInstance.ConstraintBone2 = Skeleton.GetBoneName(SecondBone);
			const FTransform JointTransform = Constraint->DefaultInstance.GetRefFrame(EConstraintFrame::Frame1) * SourceTransforms[SourceFirstBone];
			Constraint->DefaultInstance.SetRefFrame(EConstraintFrame::Frame1, JointTransform.GetRelativeTransform(BoneTransforms[FirstBone]));
			Constraint->DefaultInstance.SetRefFrame(EConstraintFrame::Frame2, JointTransform.GetRelativeTransform(BoneTransforms[SecondBone]));
			Physics->ConstraintSetup.Add(Constraint);
			Physics->DisableCollision(FirstBody, SecondBody);
		}
		for (const TPair<FRigidBodyIndexPair, bool>& DisabledPair : SourcePhysics->CollisionDisableTable)
		{
			if (!SourcePhysics->SkeletalBodySetups.IsValidIndex(DisabledPair.Key.Indices[0]) || !SourcePhysics->SkeletalBodySetups.IsValidIndex(DisabledPair.Key.Indices[1]))
			{
				continue;
			}
			const int32 FirstBody = Physics->FindBodyIndex(SourcePhysics->SkeletalBodySetups[DisabledPair.Key.Indices[0]]->BoneName);
			const int32 SecondBody = Physics->FindBodyIndex(SourcePhysics->SkeletalBodySetups[DisabledPair.Key.Indices[1]]->BoneName);
			if (FirstBody != INDEX_NONE && SecondBody != INDEX_NONE && FirstBody != SecondBody)
			{
				Physics->DisableCollision(FirstBody, SecondBody);
			}
		}
	}
	return Physics;
}

static TArray<TArray<FAPSkeletalSliceTriangle>> SeparateSliceFragments(TArray<FAPSkeletalSliceTriangle> Triangles, const FPlane& Plane, double SideSign, const FReferenceSkeleton& Skeleton, const TArray<FTransform>& BoneTransforms, bool bUseAnatomicalNeighbours = false)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_SeparateFragments);
	TArray<int32> Parents;
	Parents.Reserve(Triangles.Num());
	for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
	{
		Parents.Add(TriangleIndex);
	}
	const auto FindRoot = [&Parents](int32 TriangleIndex)
	{
		while (Parents[TriangleIndex] != TriangleIndex)
		{
			Parents[TriangleIndex] = Parents[Parents[TriangleIndex]];
			TriangleIndex = Parents[TriangleIndex];
		}
		return TriangleIndex;
	};
	const auto Join = [&Parents, &FindRoot](int32 FirstTriangle, int32 SecondTriangle)
	{
		const int32 FirstRoot = FindRoot(FirstTriangle);
		const int32 SecondRoot = FindRoot(SecondTriangle);
		Parents[FMath::Max(FirstRoot, SecondRoot)] = FMath::Min(FirstRoot, SecondRoot);
	};
	constexpr double PositionTolerance = 0.00001;
	TMap<FIntVector, TArray<TPair<FVector, int32>>> VertexBuckets;
	for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
	{
		for (const FAPSkeletalSliceVertex& Vertex : Triangles[TriangleIndex].Vertices)
		{
			const FIntVector Bucket(FMath::FloorToInt(Vertex.Position.X / PositionTolerance), FMath::FloorToInt(Vertex.Position.Y / PositionTolerance), FMath::FloorToInt(Vertex.Position.Z / PositionTolerance));
			bool bFoundVertex = false;
			for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
			{
				for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
				{
					for (int32 OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
					{
						const TArray<TPair<FVector, int32>>* Candidates = VertexBuckets.Find(Bucket + FIntVector(OffsetX, OffsetY, OffsetZ));
						if (Candidates)
						{
							for (const TPair<FVector, int32>& Candidate : *Candidates)
							{
								if (Candidate.Key.Equals(Vertex.Position, PositionTolerance))
								{
									Join(TriangleIndex, Candidate.Value);
									bFoundVertex = true;
								}
							}
						}
					}
				}
			}
			if (!bFoundVertex)
			{
				VertexBuckets.FindOrAdd(Bucket).Emplace(Vertex.Position, TriangleIndex);
			}
		}
	}
	TArray<int32> BoneGroups;
	BoneGroups.Init(INDEX_NONE, Skeleton.GetNum());
	for (int32 BoneIndex = 0; BoneIndex < Skeleton.GetNum(); ++BoneIndex)
	{
		if (Plane.PlaneDot(BoneTransforms[BoneIndex].GetLocation()) * SideSign < -PositionTolerance)
		{
			continue;
		}
		BoneGroups[BoneIndex] = BoneIndex;
		const int32 ParentIndex = Skeleton.GetParentIndex(BoneIndex);
		if (ParentIndex != INDEX_NONE && BoneGroups[ParentIndex] != INDEX_NONE)
		{
			BoneGroups[BoneIndex] = BoneGroups[ParentIndex];
		}
	}
	TArray<int32> GeometricRoots;
	TSet<int32> WeightedGeometricRoots;
	for (int32 TriangleIndex = 0; bUseAnatomicalNeighbours && TriangleIndex < Triangles.Num(); ++TriangleIndex)
	{
		const int32 GeometricRoot = FindRoot(TriangleIndex);
		GeometricRoots.Add(GeometricRoot);
		for (const FAPSkeletalSliceVertex& Vertex : Triangles[TriangleIndex].Vertices)
		{
			for (const TPair<int32, double>& Influence : Vertex.BoneWeights)
			{
				if (Influence.Value > 0.00001 && BoneGroups[Influence.Key] != INDEX_NONE)
				{
					WeightedGeometricRoots.Add(GeometricRoot);
				}
			}
		}
	}
	TArray<TArray<int32>> RetainedNeighbours;
	RetainedNeighbours.SetNum(Skeleton.GetNum());
	for (int32 BoneIndex = 0; bUseAnatomicalNeighbours && BoneIndex < Skeleton.GetNum(); ++BoneIndex)
	{
		if (BoneGroups[BoneIndex] != INDEX_NONE)
		{
			continue;
		}
		TArray<int32> PendingBones = {BoneIndex};
		TSet<int32> VisitedBones = {BoneIndex};
		for (int32 PendingIndex = 0; PendingIndex < PendingBones.Num(); ++PendingIndex)
		{
			const int32 CurrentBone = PendingBones[PendingIndex];
			for (int32 Candidate = 0; Candidate < Skeleton.GetNum(); ++Candidate)
			{
				if (VisitedBones.Contains(Candidate) || (Skeleton.GetParentIndex(Candidate) != CurrentBone && Skeleton.GetParentIndex(CurrentBone) != Candidate))
				{
					continue;
				}
				VisitedBones.Add(Candidate);
				if (BoneGroups[Candidate] != INDEX_NONE)
				{
					RetainedNeighbours[BoneIndex].Add(Candidate);
				}
				else
				{
					PendingBones.Add(Candidate);
				}
			}
		}
	}
	TMap<int32, int32> BoneGroupTriangles;
	for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
	{
		for (const FAPSkeletalSliceVertex& Vertex : Triangles[TriangleIndex].Vertices)
		{
			for (const TPair<int32, double>& Influence : Vertex.BoneWeights)
			{
				if (Influence.Value <= 0.00001)
				{
					continue;
				}
				int32 BoneGroup = BoneGroups[Influence.Key];
				if (BoneGroup == INDEX_NONE)
				{
					if (bUseAnatomicalNeighbours && WeightedGeometricRoots.Contains(GeometricRoots[TriangleIndex]))
					{
						continue;
					}
					double ClosestDistance = TNumericLimits<double>::Max();
					for (int32 BoneIndex = 0; BoneIndex < BoneGroups.Num(); ++BoneIndex)
					{
						if (BoneGroups[BoneIndex] == INDEX_NONE || (bUseAnatomicalNeighbours && !RetainedNeighbours[Influence.Key].Contains(BoneIndex)))
						{
							continue;
						}
						const double Distance = FVector::DistSquared(Vertex.Position, BoneTransforms[BoneIndex].GetLocation());
						if (Distance < ClosestDistance)
						{
							ClosestDistance = Distance;
							BoneGroup = BoneGroups[BoneIndex];
						}
					}
				}
				if (BoneGroup == INDEX_NONE)
				{
					continue;
				}
				if (const int32* ExistingTriangle = BoneGroupTriangles.Find(BoneGroup))
				{
					Join(TriangleIndex, *ExistingTriangle);
				}
				else
				{
					BoneGroupTriangles.Add(BoneGroup, TriangleIndex);
				}
			}
		}
	}
	TMap<int32, int32> FragmentIndices;
	TArray<TArray<FAPSkeletalSliceTriangle>> Fragments;
	for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
	{
		const int32 RootIndex = FindRoot(TriangleIndex);
		if (!FragmentIndices.Contains(RootIndex))
		{
			FragmentIndices.Add(RootIndex, Fragments.Num());
			Fragments.AddDefaulted();
		}
		Fragments[FragmentIndices[RootIndex]].Add(MoveTemp(Triangles[TriangleIndex]));
	}
	Fragments.StableSort([](const TArray<FAPSkeletalSliceTriangle>& First, const TArray<FAPSkeletalSliceTriangle>& Second)
	{
		return First.Num() > Second.Num();
	});
	return Fragments;
}

static bool RecombineLimitedSlice(const TArray<FAPSkeletalSliceTriangle>& SourceTriangles, TArray<TArray<FAPSkeletalSliceTriangle>>& PositiveFragments, TArray<TArray<FAPSkeletalSliceTriangle>>& NegativeFragments, TArray<int32>& BoneGroups, const TArray<FTransform>& BoneTransforms, const FPlane& Plane, TArray<int32>& FragmentGroups, FString& Failure)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_RecombineLimitedSlice);
	TArray<int32> MergedGroups;
	for (int32 BoneIndex = 0; BoneIndex < BoneGroups.Num(); ++BoneIndex)
	{
		MergedGroups.Add(BoneIndex);
	}
	const auto FindGroup = [&MergedGroups](int32 Group)
	{
		while (MergedGroups[Group] != Group)
		{
			MergedGroups[Group] = MergedGroups[MergedGroups[Group]];
			Group = MergedGroups[Group];
		}
		return Group;
	};
	TMap<int32, TArray<FAPSkeletalSliceTriangle>> ClassifiedTriangles;
	TMap<int32, FIntPoint> CapGroups;
	TArray<int32> SourceGroups;
	SourceGroups.Init(INDEX_NONE, SourceTriangles.Num());
	TArray<bool> SharedTriangles;
	SharedTriangles.Init(false, SourceTriangles.Num());
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		TArray<TArray<FAPSkeletalSliceTriangle>>* Fragments = &PositiveFragments;
		double SideSign = 1.0;
		if (SideIndex == 1)
		{
			Fragments = &NegativeFragments;
			SideSign = -1.0;
		}
		for (TArray<FAPSkeletalSliceTriangle>& Fragment : *Fragments)
		{
			TMap<int32, double> GroupWeights;
			for (int32 PassIndex = 0; PassIndex < 2 && GroupWeights.IsEmpty(); ++PassIndex)
			{
				for (const FAPSkeletalSliceTriangle& Triangle : Fragment)
				{
					for (const FAPSkeletalSliceVertex& Vertex : Triangle.Vertices)
					{
						for (const TPair<int32, double>& Influence : Vertex.BoneWeights)
						{
							if (Influence.Value > 0.00001 && (PassIndex == 1 || Plane.PlaneDot(BoneTransforms[Influence.Key].GetLocation()) * SideSign >= -0.00001))
							{
								GroupWeights.FindOrAdd(BoneGroups[Influence.Key]) += Influence.Value;
							}
						}
					}
				}
			}
			if (GroupWeights.IsEmpty())
			{
				Failure = TEXT("A region has no bone weights.");
				return false;
			}
			int32 Group = FindGroup(GroupWeights.CreateConstIterator().Key());
			for (const TPair<int32, double>& GroupWeight : GroupWeights)
			{
				const int32 OtherGroup = FindGroup(GroupWeight.Key);
				MergedGroups[FMath::Max(Group, OtherGroup)] = FMath::Min(Group, OtherGroup);
				Group = FindGroup(Group);
			}
			for (FAPSkeletalSliceTriangle& Triangle : Fragment)
			{
				if (Triangle.SourceTriangleIndex != INDEX_NONE)
				{
					int32& SourceGroup = SourceGroups[Triangle.SourceTriangleIndex];
					SharedTriangles[Triangle.SourceTriangleIndex] = SharedTriangles[Triangle.SourceTriangleIndex] || (SourceGroup != INDEX_NONE && SourceGroup != Group);
					SourceGroup = Group;
				}
				else
				{
					if (!CapGroups.Contains(Triangle.SliceCapIndex))
					{
						CapGroups.Add(Triangle.SliceCapIndex, FIntPoint(INDEX_NONE, INDEX_NONE));
					}
					CapGroups[Triangle.SliceCapIndex][SideIndex] = Group;
				}
				ClassifiedTriangles.FindOrAdd(Group).Add(MoveTemp(Triangle));
			}
		}
	}
	for (int32& BoneGroup : BoneGroups)
	{
		BoneGroup = FindGroup(BoneGroup);
	}
	for (TPair<int32, FIntPoint>& CapGroup : CapGroups)
	{
		CapGroup.Value.X = FindGroup(CapGroup.Value.X);
		CapGroup.Value.Y = FindGroup(CapGroup.Value.Y);
	}
	TMap<int32, TArray<FAPSkeletalSliceTriangle>> NormalizedTriangles;
	SourceGroups.Init(INDEX_NONE, SourceTriangles.Num());
	SharedTriangles.Init(false, SourceTriangles.Num());
	for (TPair<int32, TArray<FAPSkeletalSliceTriangle>>& ClassifiedGroup : ClassifiedTriangles)
	{
		const int32 Group = FindGroup(ClassifiedGroup.Key);
		for (FAPSkeletalSliceTriangle& Triangle : ClassifiedGroup.Value)
		{
			if (Triangle.SourceTriangleIndex != INDEX_NONE)
			{
				int32& SourceGroup = SourceGroups[Triangle.SourceTriangleIndex];
				SharedTriangles[Triangle.SourceTriangleIndex] = SharedTriangles[Triangle.SourceTriangleIndex] || (SourceGroup != INDEX_NONE && SourceGroup != Group);
				SourceGroup = Group;
			}
			NormalizedTriangles.FindOrAdd(Group).Add(MoveTemp(Triangle));
		}
	}
	ClassifiedTriangles = MoveTemp(NormalizedTriangles);
	if (ClassifiedTriangles.Num() < 2)
	{
		Failure = TEXT("Selected bone groups remain connected through the mesh.");
		return false;
	}
	TMap<int32, TArray<FAPSkeletalSliceTriangle>> RecombinedTriangles;
	for (int32 TriangleIndex = 0; TriangleIndex < SourceTriangles.Num(); ++TriangleIndex)
	{
		if (!SharedTriangles[TriangleIndex])
		{
			if (SourceGroups[TriangleIndex] == INDEX_NONE)
			{
				Failure = TEXT("An original triangle was not classified.");
				return false;
			}
			RecombinedTriangles.FindOrAdd(SourceGroups[TriangleIndex]).Add(SourceTriangles[TriangleIndex]);
		}
	}
	bool bHasCut = false;
	for (TPair<int32, TArray<FAPSkeletalSliceTriangle>>& Group : ClassifiedTriangles)
	{
		for (FAPSkeletalSliceTriangle& Triangle : Group.Value)
		{
			if (Triangle.SourceTriangleIndex != INDEX_NONE)
			{
				if (SharedTriangles[Triangle.SourceTriangleIndex])
				{
					RecombinedTriangles.FindOrAdd(Group.Key).Add(MoveTemp(Triangle));
				}
			}
			else
			{
				const FIntPoint Owners = CapGroups[Triangle.SliceCapIndex];
				if (Owners.X != Owners.Y)
				{
					RecombinedTriangles.FindOrAdd(Group.Key).Add(MoveTemp(Triangle));
					bHasCut = true;
				}
			}
		}
	}
	if (!bHasCut)
	{
		Failure = TEXT("No selected cut surface separates bone groups.");
		return false;
	}
	for (const TPair<int32, TArray<FAPSkeletalSliceTriangle>>& Group : RecombinedTriangles)
	{
		TArray<FVector> BoundaryPositions;
		TMap<FIntPoint, int32> BoundaryEdgeCounts;
		const auto FindPosition = [&BoundaryPositions](const FVector& Position)
		{
			int32 PositionIndex = BoundaryPositions.IndexOfByPredicate([&Position](const FVector& Existing)
			{
				return Existing.Equals(Position, 0.00001);
			});
			if (PositionIndex == INDEX_NONE)
			{
				PositionIndex = BoundaryPositions.Add(Position);
			}
			return PositionIndex;
		};
		for (const FAPSkeletalSliceTriangle& Triangle : Group.Value)
		{
			if (Triangle.SourceTriangleIndex != INDEX_NONE && !SharedTriangles[Triangle.SourceTriangleIndex])
			{
				continue;
			}
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FVector Start = Triangle.Vertices[Corner].Position;
				const FVector End = Triangle.Vertices[(Corner + 1) % 3].Position;
				if (FMath::Abs(Plane.PlaneDot(Start)) > 0.00001 || FMath::Abs(Plane.PlaneDot(End)) > 0.00001)
				{
					continue;
				}
				const int32 StartIndex = FindPosition(Start);
				const int32 EndIndex = FindPosition(End);
				if (StartIndex != EndIndex)
				{
					BoundaryEdgeCounts.FindOrAdd(FIntPoint(FMath::Min(StartIndex, EndIndex), FMath::Max(StartIndex, EndIndex)))++;
				}
			}
		}
		for (const TPair<FIntPoint, int32>& Edge : BoundaryEdgeCounts)
		{
			if (Edge.Value != 2)
			{
				Failure = TEXT("A selected cut surface has unmatched boundary edges.");
				return false;
			}
		}
	}
	RecombinedTriangles.GetKeys(FragmentGroups);
	FragmentGroups.Sort();
	PositiveFragments.Reset();
	NegativeFragments.Reset();
	PositiveFragments.Add(MoveTemp(RecombinedTriangles[FragmentGroups[0]]));
	for (int32 FragmentIndex = 1; FragmentIndex < FragmentGroups.Num(); ++FragmentIndex)
	{
		NegativeFragments.Add(MoveTemp(RecombinedTriangles[FragmentGroups[FragmentIndex]]));
	}
	return true;
}

static USkeletalMesh* BuildSliceFragment(TArray<FAPSkeletalSliceTriangle> Triangles, const FPlane& Plane, double SideSign, USkeletalMesh* SourceMesh, const TArray<FTransform>& SourceTransforms, const TArray<UMaterialInterface*>& Materials, const TArray<int32>* LimitedBoneGroups = nullptr, int32 FragmentBoneGroup = INDEX_NONE)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_BuildFragment);
	const FReferenceSkeleton& SourceSkeleton = SourceMesh->GetRefSkeleton();
	TSet<int32> UsedBones;
	for (const FAPSkeletalSliceTriangle& Triangle : Triangles)
	{
		for (const FAPSkeletalSliceVertex& Vertex : Triangle.Vertices)
		{
			for (const TPair<int32, double>& Influence : Vertex.BoneWeights)
			{
				bool bRetainedBone = Plane.PlaneDot(SourceTransforms[Influence.Key].GetLocation()) * SideSign >= -0.00001;
				if (LimitedBoneGroups)
				{
					bRetainedBone = (*LimitedBoneGroups)[Influence.Key] == FragmentBoneGroup;
				}
				if (Influence.Value > 0.00001 && bRetainedBone)
				{
					UsedBones.Add(Influence.Key);
				}
			}
		}
	}
	if (UsedBones.IsEmpty())
	{
		int32 ClosestBone = 0;
		double ClosestDistance = TNumericLimits<double>::Max();
		for (int32 BoneIndex = 0; BoneIndex < SourceSkeleton.GetNum(); ++BoneIndex)
		{
			if (LimitedBoneGroups && (*LimitedBoneGroups)[BoneIndex] != FragmentBoneGroup)
			{
				continue;
			}
			const double Distance = FVector::DistSquared(SourceTransforms[BoneIndex].GetLocation(), Triangles[0].Vertices[0].Position);
			if (Distance < ClosestDistance)
			{
				ClosestDistance = Distance;
				ClosestBone = BoneIndex;
			}
		}
		UsedBones.Add(ClosestBone);
	}
	const TArray<int32> WeightedBones = UsedBones.Array();
	for (int32 BoneIndex : WeightedBones)
	{
		int32 ParentIndex = SourceSkeleton.GetParentIndex(BoneIndex);
		while (ParentIndex != INDEX_NONE)
		{
			bool bRetainedParent = Plane.PlaneDot(SourceTransforms[ParentIndex].GetLocation()) * SideSign >= -0.00001;
			if (LimitedBoneGroups)
			{
				bRetainedParent = (*LimitedBoneGroups)[ParentIndex] == FragmentBoneGroup;
			}
			if (!bRetainedParent)
			{
				break;
			}
			UsedBones.Add(ParentIndex);
			ParentIndex = SourceSkeleton.GetParentIndex(ParentIndex);
		}
	}
	TArray<FTransform> AdjustedTransforms = SourceTransforms;
	for (int32 BoneIndex = 1; BoneIndex < SourceSkeleton.GetNum(); ++BoneIndex)
	{
		const int32 ParentIndex = SourceSkeleton.GetParentIndex(BoneIndex);
		const FVector Start = SourceTransforms[ParentIndex].GetLocation();
		const FVector End = SourceTransforms[BoneIndex].GetLocation();
		const double StartDistance = Plane.PlaneDot(Start) * SideSign;
		const double EndDistance = Plane.PlaneDot(End) * SideSign;
		if (LimitedBoneGroups)
		{
			if ((*LimitedBoneGroups)[BoneIndex] != (*LimitedBoneGroups)[ParentIndex] && StartDistance * EndDistance < 0.0)
			{
				const FVector Intersection = FMath::Lerp(Start, End, StartDistance / (StartDistance - EndDistance));
				if ((*LimitedBoneGroups)[ParentIndex] == FragmentBoneGroup)
				{
					UsedBones.Add(BoneIndex);
					AdjustedTransforms[BoneIndex].SetLocation(Intersection);
				}
				else if ((*LimitedBoneGroups)[BoneIndex] == FragmentBoneGroup)
				{
					UsedBones.Add(ParentIndex);
					AdjustedTransforms[ParentIndex].SetLocation(Intersection);
				}
			}
			continue;
		}
		if (StartDistance * EndDistance < 0.0 && (UsedBones.Contains(BoneIndex) || UsedBones.Contains(ParentIndex)))
		{
			const FVector Intersection = FMath::Lerp(Start, End, StartDistance / (StartDistance - EndDistance));
			if (StartDistance < 0.0)
			{
				UsedBones.Add(ParentIndex);
				AdjustedTransforms[ParentIndex].SetLocation(Intersection);
			}
			else
			{
				UsedBones.Add(BoneIndex);
				AdjustedTransforms[BoneIndex].SetLocation(Intersection);
			}
		}
	}
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient);
	FReferenceSkeleton& NewSkeleton = const_cast<FReferenceSkeleton&>(Skeleton->GetReferenceSkeleton());
	TMap<int32, int32> BoneMapping;
	TArray<FTransform> BoneTransforms;
	{
		FReferenceSkeletonModifier Modifier(NewSkeleton, Skeleton);
		for (int32 BoneIndex = 0; BoneIndex < SourceSkeleton.GetNum(); ++BoneIndex)
		{
			if (!UsedBones.Contains(BoneIndex))
			{
				continue;
			}
			int32 SourceParent = SourceSkeleton.GetParentIndex(BoneIndex);
			while (SourceParent != INDEX_NONE && !BoneMapping.Contains(SourceParent))
			{
				SourceParent = SourceSkeleton.GetParentIndex(SourceParent);
			}
			int32 ParentIndex = INDEX_NONE;
			if (SourceParent != INDEX_NONE)
			{
				ParentIndex = BoneMapping[SourceParent];
			}
			else if (!BoneTransforms.IsEmpty())
			{
				ParentIndex = 0;
			}
			FTransform LocalTransform = AdjustedTransforms[BoneIndex];
			if (ParentIndex != INDEX_NONE)
			{
				LocalTransform = LocalTransform.GetRelativeTransform(BoneTransforms[ParentIndex]);
			}
			Modifier.Add(FMeshBoneInfo(SourceSkeleton.GetBoneName(BoneIndex), SourceSkeleton.GetBoneName(BoneIndex).ToString(), ParentIndex), LocalTransform);
			BoneMapping.Add(BoneIndex, BoneTransforms.Add(AdjustedTransforms[BoneIndex]));
		}
	}
	for (FAPSkeletalSliceTriangle& Triangle : Triangles)
	{
		for (FAPSkeletalSliceVertex& Vertex : Triangle.Vertices)
		{
			TMap<int32, double> NewWeights;
			for (const TPair<int32, double>& Influence : Vertex.BoneWeights)
			{
				int32 TargetBone = 0;
				if (BoneMapping.Contains(Influence.Key))
				{
					TargetBone = BoneMapping[Influence.Key];
				}
				else
				{
					double ClosestDistance = TNumericLimits<double>::Max();
					for (int32 Candidate = 0; Candidate < BoneTransforms.Num(); ++Candidate)
					{
						const double Distance = FVector::DistSquared(Vertex.Position, BoneTransforms[Candidate].GetLocation());
						if (Distance < ClosestDistance)
						{
							ClosestDistance = Distance;
							TargetBone = Candidate;
						}
					}
				}
				NewWeights.FindOrAdd(TargetBone) += Influence.Value;
			}
			Vertex.BoneWeights = MoveTemp(NewWeights);
		}
	}
	UPhysicsAsset* Physics = BuildSlicePhysics(Triangles, NewSkeleton, BoneTransforms, SourceMesh->GetPhysicsAsset(), SourceSkeleton, SourceTransforms);
	TArray<FAPSkeletalMeshSurface> Surfaces;
	FAPSkeletalSliceGeometry::MakeSurfaces(Triangles, Surfaces);
	TArray<UMaterialInterface*> UsedMaterials;
	for (FAPSkeletalMeshSurface& Surface : Surfaces)
	{
		UMaterialInterface* Material = Materials[Surface.MaterialIndex];
		Surface.MaterialIndex = UsedMaterials.Add(Material);
	}
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	Mesh->SetSkeleton(Skeleton);
	Mesh->SetRefSkeleton(NewSkeleton);
	Mesh->SetHasVertexColors(true);
	if (!FAPSkeletalMeshGenerator::GenerateSkeletalMesh(Mesh, Surfaces, UsedMaterials, true))
	{
		return nullptr;
	}
	Mesh->CalculateInvRefMatrices();
	Mesh->SetPhysicsAsset(Physics);
	return Mesh;
}

void UAPSliceableSkeletalMeshComponent::SliceMesh(const FPlane& SlicePlane, EAPSliceSpace SliceSpace)
{
	SliceMeshInternal(SlicePlane, nullptr, 0.0f, SliceSpace);
}

FSliceResult UAPSliceableSkeletalMeshComponent::SliceMeshInRadius(const FVector& SliceCenter, const FVector& SliceNormal, float SliceRadius, EAPSliceSpace SliceSpace)
{
	if (SliceRadius <= 0.0f || !FMath::IsFinite(SliceRadius) || SliceCenter.ContainsNaN() || SliceNormal.ContainsNaN() || SliceNormal.IsNearlyZero())
	{
		return FSliceResult();
	}
	return SliceMeshInternal(FPlane(SliceCenter, SliceNormal.GetSafeNormal()), &SliceCenter, SliceRadius, SliceSpace);
}

FSliceResult UAPSliceableSkeletalMeshComponent::SliceMeshInternal(const FPlane& SlicePlane, const FVector* SliceCenter, float SliceRadius, EAPSliceSpace SliceSpace)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_SliceMesh);
	USkeletalMesh* SourceMesh = GetSkeletalMeshAsset();
	if (!SourceMesh || !GetOwner() || SlicePlane.GetNormal().IsNearlyZero())
	{
		return FSliceResult();
	}
	const bool bUsePhysicsPose = IsSimulatingPhysics();
	if (!bUsePhysicsPose)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_RefreshPose);
		RefreshBoneTransforms();
	}
	const FTransform OriginalTransform = GetComponentTransform();
	FPlane LocalPlane = SlicePlane;
	if (SliceSpace == EAPSliceSpace::World)
	{
		LocalPlane = SlicePlane.TransformBy(OriginalTransform.ToInverseMatrixWithScale());
	}
	LocalPlane.Normalize();
	const TArray<FTransform> SourceTransforms = GetComponentSpaceTransforms();
	TArray<int32> LimitedBoneGroups;
	if (SliceCenter)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_SelectBonesInRadius);
		const FReferenceSkeleton& Skeleton = SourceMesh->GetRefSkeleton();
		LimitedBoneGroups.Init(0, Skeleton.GetNum());
		bool bIntersectsBone = false;
		for (int32 BoneIndex = 1; BoneIndex < Skeleton.GetNum(); ++BoneIndex)
		{
			const int32 ParentIndex = Skeleton.GetParentIndex(BoneIndex);
			LimitedBoneGroups[BoneIndex] = LimitedBoneGroups[ParentIndex];
			const FVector Start = SourceTransforms[ParentIndex].GetLocation();
			const FVector End = SourceTransforms[BoneIndex].GetLocation();
			const double StartDistance = LocalPlane.PlaneDot(Start);
			const double EndDistance = LocalPlane.PlaneDot(End);
			if (StartDistance * EndDistance >= -1.e-10)
			{
				continue;
			}
			FVector Intersection = FMath::Lerp(Start, End, StartDistance / (StartDistance - EndDistance));
			if (SliceSpace == EAPSliceSpace::World)
			{
				Intersection = OriginalTransform.TransformPosition(Intersection);
			}
			if (FVector::DistSquared(Intersection, *SliceCenter) <= FMath::Square(double(SliceRadius)))
			{
				LimitedBoneGroups[BoneIndex] = BoneIndex;
				bIntersectsBone = true;
			}
		}
		if (!bIntersectsBone)
		{
			return FSliceResult();
		}
	}
	const FSkeletalMeshLODRenderData& RenderData = SourceMesh->GetResourceForRendering()->LODRenderData[0];
	const uint32 TextureCoordinateCount = RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
	if (TextureCoordinateCount < 1 || TextureCoordinateCount > 4)
	{
		UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("SkeletalAmputator: unsupported texture coordinate count {0}."), {TextureCoordinateCount}));
		return FSliceResult();
	}
	TArray<FFinalSkinVertex> SkinnedVertices;
	TArray<FMatrix44f> BoneMatrices;
	if (bUsePhysicsPose)
	{
		GetCurrentRefToLocalMatrices(BoneMatrices, 0);
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_GetCPUSkinnedVertices);
		GetCPUSkinnedVertices(SkinnedVertices, 0);
	}
	if (!bUsePhysicsPose && SkinnedVertices.Num() != RenderData.GetNumVertices())
	{
		UKismetSystemLibrary::PrintString(this, TEXT("SkeletalAmputator: CPU skinned vertices are unavailable."));
		return FSliceResult();
	}
	TArray<FAPSkeletalSliceTriangle> SourceTriangles;
	const FRawStaticIndexBuffer16or32Interface* Indices = RenderData.MultiSizeIndexContainer.GetIndexBuffer();
	const FSkinWeightVertexBuffer* Weights = RenderData.GetSkinWeightVertexBuffer();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_ExtractPosedGeometry);
		for (const FSkelMeshRenderSection& Section : RenderData.RenderSections)
		{
			for (uint32 TriangleIndex = 0; TriangleIndex < Section.NumTriangles; ++TriangleIndex)
			{
				FAPSkeletalSliceTriangle& Triangle = SourceTriangles.Emplace_GetRef();
				Triangle.MaterialIndex = Section.MaterialIndex;
				Triangle.SourceTriangleIndex = SourceTriangles.Num() - 1;
				for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
				{
					const uint32 VertexIndex = Indices->Get(Section.BaseIndex + TriangleIndex * 3 + CornerIndex);
					FAPSkeletalSliceVertex& Vertex = Triangle.Vertices[CornerIndex];
					if (bUsePhysicsPose)
					{
						Vertex.Position = FVector::ZeroVector;
						Vertex.Normal = FVector::ZeroVector;
						Vertex.Tangent = FVector::ZeroVector;
					}
					else
					{
						Vertex.Position = FVector(SkinnedVertices[VertexIndex].Position);
						Vertex.Normal = FVector(SkinnedVertices[VertexIndex].TangentZ.ToFVector3f());
						Vertex.Tangent = FVector(SkinnedVertices[VertexIndex].TangentX.ToFVector3f());
					}
					for (uint32 CoordinateIndex = 0; CoordinateIndex < RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords(); ++CoordinateIndex)
					{
						Vertex.TextureCoordinates.Add(FVector2D(RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, CoordinateIndex)));
					}
					if (RenderData.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() > VertexIndex)
					{
						Vertex.Color = FLinearColor::FromSRGBColor(RenderData.StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertexIndex));
					}
					for (uint32 InfluenceIndex = 0; InfluenceIndex < Weights->GetMaxBoneInfluences(); ++InfluenceIndex)
					{
						const double Weight = Weights->GetBoneWeight(VertexIndex, InfluenceIndex) / 65535.0;
						if (Weight > 0.0)
						{
							const int32 BoneIndex = Section.BoneMap[Weights->GetBoneIndex(VertexIndex, InfluenceIndex)];
							Vertex.BoneWeights.Add(BoneIndex, Weight);
							if (bUsePhysicsPose)
							{
								Vertex.Position += FVector(BoneMatrices[BoneIndex].TransformPosition(RenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex))) * Weight;
								Vertex.Normal += FVector(BoneMatrices[BoneIndex].TransformVector(RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex))) * Weight;
								Vertex.Tangent += FVector(BoneMatrices[BoneIndex].TransformVector(RenderData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex))) * Weight;
							}
						}
					}
					Vertex.Normal.Normalize();
					Vertex.Tangent.Normalize();
				}
				Swap(Triangle.Vertices[1], Triangle.Vertices[2]);
			}
		}
	}
	TArray<UMaterialInterface*> Materials;
	for (int32 MaterialIndex = 0; MaterialIndex < SourceMesh->GetMaterials().Num(); ++MaterialIndex)
	{
		UMaterialInterface* Material = GetMaterial(MaterialIndex);
		if (!Material)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		Materials.Add(Material);
	}
	UMaterialInterface* InteriorMaterial = CreateSkeletalSliceMaterial(this, CapMaterial, SourceMesh->GetRefSkeleton(), SourceTransforms, TissueColor, BoneColor, BoneRadius, TerminalBoneLength);
	const int32 CapMaterialIndex = Materials.Add(InteriorMaterial);
	TArray<FAPSkeletalSliceTriangle> Positive;
	TArray<FAPSkeletalSliceTriangle> Negative;
	FString Failure;
	if (!FAPSkeletalSliceGeometry::Split(SourceTriangles, LocalPlane, CapMaterialIndex, Positive, Negative, Failure, SliceCenter != nullptr))
	{
		UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("SkeletalAmputator: {0}"), {Failure}));
		return FSliceResult();
	}
	TArray<TArray<FAPSkeletalSliceTriangle>> PositiveFragments = SeparateSliceFragments(MoveTemp(Positive), LocalPlane, 1.0, SourceMesh->GetRefSkeleton(), SourceTransforms, SliceCenter != nullptr);
	TArray<TArray<FAPSkeletalSliceTriangle>> NegativeFragments = SeparateSliceFragments(MoveTemp(Negative), LocalPlane, -1.0, SourceMesh->GetRefSkeleton(), SourceTransforms, SliceCenter != nullptr);
	TArray<int32> LimitedFragmentGroups;
	if (SliceCenter && !RecombineLimitedSlice(SourceTriangles, PositiveFragments, NegativeFragments, LimitedBoneGroups, SourceTransforms, LocalPlane, LimitedFragmentGroups, Failure))
	{
		UKismetSystemLibrary::PrintString(this, FString::Format(TEXT("SkeletalAmputator: {0} The source mesh was preserved."), {Failure}));
		return FSliceResult();
	}
	TArray<USkeletalMesh*> FragmentMeshes;
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		TArray<TArray<FAPSkeletalSliceTriangle>>* Fragments = &PositiveFragments;
		double SideSign = 1.0;
		if (SideIndex == 1)
		{
			Fragments = &NegativeFragments;
			SideSign = -1.0;
		}
		for (TArray<FAPSkeletalSliceTriangle>& Fragment : *Fragments)
		{
			const TArray<int32>* FragmentBoneGroups = nullptr;
			int32 FragmentBoneGroup = INDEX_NONE;
			if (SliceCenter)
			{
				FragmentBoneGroups = &LimitedBoneGroups;
				FragmentBoneGroup = LimitedFragmentGroups[FragmentMeshes.Num()];
			}
			USkeletalMesh* FragmentMesh = BuildSliceFragment(MoveTemp(Fragment), LocalPlane, SideSign, SourceMesh, SourceTransforms, Materials, FragmentBoneGroups, FragmentBoneGroup);
			if (!FragmentMesh || FragmentMesh->GetPhysicsAsset()->SkeletalBodySetups.IsEmpty())
			{
				UKismetSystemLibrary::PrintString(this, TEXT("SkeletalAmputator: fragment construction failed; the source mesh was preserved."));
				return FSliceResult();
			}
			FragmentMeshes.Add(FragmentMesh);
		}
	}
	FSliceResult Result;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_InstallFragmentsAndStartPhysics);
		const FVector LinearVelocity = GetPhysicsLinearVelocity();
		const FVector AngularVelocity = GetPhysicsAngularVelocityInRadians();
		for (int32 FragmentIndex = 0; FragmentIndex < FragmentMeshes.Num(); ++FragmentIndex)
		{
			UAPSliceableSkeletalMeshComponent* Component = this;
			if (FragmentIndex > 0)
			{
				Component = NewObject<UAPSliceableSkeletalMeshComponent>(GetOwner());
				GetOwner()->AddInstanceComponent(Component);
				Component->CapMaterial = CapMaterial;
				Component->TissueColor = TissueColor;
				Component->BoneColor = BoneColor;
				Component->BoneRadius = BoneRadius;
				Component->TerminalBoneLength = TerminalBoneLength;
				Component->OnSliceMesh = OnSliceMesh;
			}
			else
			{
				SetSimulatePhysics(false);
				SetAnimInstanceClass(nullptr);
				SetLeaderPoseComponent(nullptr);
				EmptyOverrideMaterials();
			}
			Component->SetSkeletalMesh(FragmentMeshes[FragmentIndex]);
			Component->SetWorldTransform(OriginalTransform);
			Component->SetCollisionProfileName(TEXT("Ragdoll"));
			Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Component->SetEnableGravity(true);
			Component->SetForcedLOD(1);
			if (FragmentIndex > 0)
			{
				Component->RegisterComponent();
			}
			Component->SetPhysicsAsset(FragmentMeshes[FragmentIndex]->GetPhysicsAsset(), true);
			Component->RefreshBoneTransforms();
			Component->ComputeSkinWeightData();
			Component->SetSimulatePhysics(true);
			Component->SetAllPhysicsLinearVelocity(LinearVelocity);
			Component->SetAllPhysicsAngularVelocityInRadians(AngularVelocity);
			Component->WakeAllRigidBodies();
			if (FragmentIndex < PositiveFragments.Num())
			{
				Result.PositiveFragments.Add(Component);
			}
			else
			{
				Result.NegativeFragments.Add(Component);
			}
		}
	}
	Result.bSuccess = true;
	Result.SourceComponent = this;
	Result.PSideSkeletalMeshComp = Result.PositiveFragments[0];
	Result.NSideSkeletalMeshComp = Result.NegativeFragments[0];
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_NotifySliceListeners);
		OnSliceMesh.Broadcast(Result);
	}
	return Result;
}
