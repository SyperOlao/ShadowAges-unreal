
#include "APSkeletalSliceGeometry.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Arrangement2d.h"
#include "ConstrainedDelaunay2.h"

using namespace UE::Geometry;

static FAPSkeletalSliceVertex InterpolateSliceVertex(const FAPSkeletalSliceVertex& Start, const FAPSkeletalSliceVertex& End, double Fraction)
{
	FAPSkeletalSliceVertex Result;
	Result.Position = FMath::Lerp(Start.Position, End.Position, Fraction);
	Result.Normal = FMath::Lerp(Start.Normal, End.Normal, Fraction).GetSafeNormal();
	Result.Tangent = FMath::Lerp(Start.Tangent, End.Tangent, Fraction).GetSafeNormal();
	Result.Color = FMath::Lerp(Start.Color, End.Color, Fraction);
	for (int32 CoordinateIndex = 0; CoordinateIndex < Start.TextureCoordinates.Num(); ++CoordinateIndex)
	{
		Result.TextureCoordinates.Add(FMath::Lerp(Start.TextureCoordinates[CoordinateIndex], End.TextureCoordinates[CoordinateIndex], Fraction));
	}
	for (const TPair<int32, double>& Influence : Start.BoneWeights)
	{
		Result.BoneWeights.FindOrAdd(Influence.Key) += Influence.Value * (1.0 - Fraction);
	}
	for (const TPair<int32, double>& Influence : End.BoneWeights)
	{
		Result.BoneWeights.FindOrAdd(Influence.Key) += Influence.Value * Fraction;
	}
	return Result;
}

bool FAPSkeletalSliceGeometry::Split(const TArray<FAPSkeletalSliceTriangle>& SourceTriangles, const FPlane& Plane, int32 CapMaterialIndex, TArray<FAPSkeletalSliceTriangle>& PositiveTriangles, TArray<FAPSkeletalSliceTriangle>& NegativeTriangles, FString& Failure, bool bAllowOpenContours)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_SplitGeometry);
	PositiveTriangles.Reset();
	NegativeTriangles.Reset();
	Failure.Reset();
	const FVector PlaneNormal = Plane.GetNormal();
	if (!PlaneNormal.IsNormalized())
	{
		Failure = TEXT("The slice plane must have a normalized normal.");
		return false;
	}
	constexpr double Tolerance = 0.00001;
	TArray<FAPSkeletalSliceVertex> BoundaryVertices;
	TMap<FIntVector, TArray<int32>> BoundaryBuckets;
	TMap<FIntPoint, int32> BoundaryEdgeCounts;
	TMap<FIntPoint, FIntPoint> BoundaryDirections;
	const auto FindBoundaryVertex = [&](const FAPSkeletalSliceVertex& Vertex)
	{
		const FIntVector Bucket(FMath::FloorToInt(Vertex.Position.X / Tolerance), FMath::FloorToInt(Vertex.Position.Y / Tolerance), FMath::FloorToInt(Vertex.Position.Z / Tolerance));
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
			{
				for (int32 OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
				{
					const TArray<int32>* Candidates = BoundaryBuckets.Find(Bucket + FIntVector(OffsetX, OffsetY, OffsetZ));
					if (Candidates)
					{
						for (int32 Candidate : *Candidates)
						{
							if (BoundaryVertices[Candidate].Position.Equals(Vertex.Position, Tolerance))
							{
								return Candidate;
							}
						}
					}
				}
			}
		}
		const int32 NewIndex = BoundaryVertices.Add(Vertex);
		BoundaryBuckets.FindOrAdd(Bucket).Add(NewIndex);
		return NewIndex;
	};
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_ClipTriangles);
		for (const FAPSkeletalSliceTriangle& SourceTriangle : SourceTriangles)
		{
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				double SideSign = 1.0;
				TArray<FAPSkeletalSliceTriangle>* Output = &PositiveTriangles;
				if (SideIndex == 1)
				{
					SideSign = -1.0;
					Output = &NegativeTriangles;
				}
				TArray<FAPSkeletalSliceVertex> Polygon;
				bool bHasInterior = false;
				for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
				{
					FAPSkeletalSliceVertex Start = SourceTriangle.Vertices[CornerIndex];
					FAPSkeletalSliceVertex End = SourceTriangle.Vertices[(CornerIndex + 1) % 3];
					double StartDistance = Plane.PlaneDot(Start.Position) * SideSign;
					double EndDistance = Plane.PlaneDot(End.Position) * SideSign;
					if (FMath::Abs(StartDistance) <= Tolerance)
					{
						Start.Position -= PlaneNormal * Plane.PlaneDot(Start.Position);
						StartDistance = 0.0;
					}
					if (FMath::Abs(EndDistance) <= Tolerance)
					{
						End.Position -= PlaneNormal * Plane.PlaneDot(End.Position);
						EndDistance = 0.0;
					}
					bHasInterior |= StartDistance > Tolerance;
					if (StartDistance >= 0.0)
					{
						Polygon.Add(Start);
					}
					if ((StartDistance > 0.0 && EndDistance < 0.0) || (StartDistance < 0.0 && EndDistance > 0.0))
					{
						FAPSkeletalSliceVertex Intersection = InterpolateSliceVertex(Start, End, StartDistance / (StartDistance - EndDistance));
						Intersection.Position -= PlaneNormal * Plane.PlaneDot(Intersection.Position);
						Polygon.Add(MoveTemp(Intersection));
					}
				}
				if (!bHasInterior && Polygon.Num() == 3)
				{
					const FVector TriangleNormal = FVector::CrossProduct(Polygon[1].Position - Polygon[0].Position, Polygon[2].Position - Polygon[0].Position);
					bHasInterior = FVector::DotProduct(TriangleNormal, PlaneNormal) * SideSign < 0.0;
				}
				if (!bHasInterior || Polygon.Num() < 3)
				{
					continue;
				}
				for (int32 CornerIndex = 1; CornerIndex + 1 < Polygon.Num(); ++CornerIndex)
				{
					if (FVector::CrossProduct(Polygon[CornerIndex].Position - Polygon[0].Position, Polygon[CornerIndex + 1].Position - Polygon[0].Position).SizeSquared() <= 1.e-20)
					{
						continue;
					}
					FAPSkeletalSliceTriangle& Triangle = Output->Emplace_GetRef();
					Triangle.MaterialIndex = SourceTriangle.MaterialIndex;
					Triangle.SourceTriangleIndex = SourceTriangle.SourceTriangleIndex;
					Triangle.Vertices[0] = Polygon[0];
					Triangle.Vertices[1] = Polygon[CornerIndex];
					Triangle.Vertices[2] = Polygon[CornerIndex + 1];
				}
				if (SideIndex == 0)
				{
					for (int32 CornerIndex = 0; CornerIndex < Polygon.Num(); ++CornerIndex)
					{
						const FAPSkeletalSliceVertex& Start = Polygon[CornerIndex];
						const FAPSkeletalSliceVertex& End = Polygon[(CornerIndex + 1) % Polygon.Num()];
						if (FMath::Abs(Plane.PlaneDot(Start.Position)) <= Tolerance && FMath::Abs(Plane.PlaneDot(End.Position)) <= Tolerance)
						{
							const int32 StartIndex = FindBoundaryVertex(Start);
							const int32 EndIndex = FindBoundaryVertex(End);
							if (StartIndex != EndIndex)
							{
								BoundaryEdgeCounts.FindOrAdd(FIntPoint(FMath::Min(StartIndex, EndIndex), FMath::Max(StartIndex, EndIndex)))++;
								BoundaryDirections.Add(FIntPoint(FMath::Min(StartIndex, EndIndex), FMath::Max(StartIndex, EndIndex)), FIntPoint(StartIndex, EndIndex));
							}
						}
					}
				}
			}
		}
	}
	if (PositiveTriangles.IsEmpty() || NegativeTriangles.IsEmpty())
	{
		Failure = TEXT("The plane does not split the mesh into two nonempty volumes.");
		return false;
	}
	FVector PlaneTangent;
	FVector PlaneBitangent;
	PlaneNormal.FindBestAxisVectors(PlaneTangent, PlaneBitangent);
	if (FVector::DotProduct(FVector::CrossProduct(PlaneTangent, PlaneBitangent), PlaneNormal) < 0.0)
	{
		PlaneBitangent *= -1.0;
	}
	FConstrainedDelaunay2d Triangulation;
	Triangulation.bOrientedEdges = false;
	Triangulation.bOutputCCW = true;
	TArray<int32> BoundaryDegrees;
	BoundaryDegrees.Init(0, BoundaryVertices.Num());
	for (const FAPSkeletalSliceVertex& Vertex : BoundaryVertices)
	{
		Triangulation.Vertices.Add(FVector2d(FVector::DotProduct(Vertex.Position, PlaneTangent), FVector::DotProduct(Vertex.Position, PlaneBitangent)));
	}
	for (const TPair<FIntPoint, int32>& EdgeCount : BoundaryEdgeCounts)
	{
		if (EdgeCount.Value == 1)
		{
			Triangulation.Edges.Add(FIndex2i(EdgeCount.Key.X, EdgeCount.Key.Y));
			BoundaryDegrees[EdgeCount.Key.X]++;
			BoundaryDegrees[EdgeCount.Key.Y]++;
		}
	}
	if (bAllowOpenContours)
	{
		TArray<TArray<int32>> Adjacency;
		Adjacency.SetNum(BoundaryVertices.Num());
		for (const FIndex2i& Edge : Triangulation.Edges)
		{
			Adjacency[Edge.A].Add(Edge.B);
			Adjacency[Edge.B].Add(Edge.A);
		}
		TSet<int32> OpenVertices;
		TArray<int32> PendingVertices;
		for (int32 VertexIndex = 0; VertexIndex < BoundaryDegrees.Num(); ++VertexIndex)
		{
			if (BoundaryDegrees[VertexIndex] != 0 && BoundaryDegrees[VertexIndex] != 2)
			{
				OpenVertices.Add(VertexIndex);
				PendingVertices.Add(VertexIndex);
			}
		}
		for (int32 PendingIndex = 0; PendingIndex < PendingVertices.Num(); ++PendingIndex)
		{
			for (int32 Neighbor : Adjacency[PendingVertices[PendingIndex]])
			{
				if (!OpenVertices.Contains(Neighbor))
				{
					OpenVertices.Add(Neighbor);
					PendingVertices.Add(Neighbor);
				}
			}
		}
		for (int32 VertexIndex : OpenVertices)
		{
			BoundaryDegrees[VertexIndex] = 0;
		}
		Triangulation.Edges.RemoveAll([&OpenVertices](const FIndex2i& Edge)
		{
			return OpenVertices.Contains(Edge.A) || OpenVertices.Contains(Edge.B);
		});
	}
	for (int32 BoundaryIndex = 0; BoundaryIndex < BoundaryDegrees.Num(); ++BoundaryIndex)
	{
		const int32 Degree = BoundaryDegrees[BoundaryIndex];
		if (Degree != 0 && Degree != 2)
		{
			double NearestDistance = TNumericLimits<double>::Max();
			for (int32 OtherIndex = 0; OtherIndex < BoundaryVertices.Num(); ++OtherIndex)
			{
				if (OtherIndex != BoundaryIndex && BoundaryDegrees[OtherIndex] != 0)
				{
					NearestDistance = FMath::Min(NearestDistance, FVector::Distance(BoundaryVertices[OtherIndex].Position, BoundaryVertices[BoundaryIndex].Position));
				}
			}
			Failure = FString::Format(TEXT("The cut boundary is open or nonmanifold: degree={0}, nearest={1}."), {Degree, NearestDistance});
			return false;
		}
	}
	TArray<TArray<int32>> Neighbors;
	Neighbors.SetNum(BoundaryVertices.Num());
	for (const FIndex2i& Edge : Triangulation.Edges)
	{
		Neighbors[Edge.A].Add(Edge.B);
		Neighbors[Edge.B].Add(Edge.A);
	}
	TSet<int32> Visited;
	TArray<TArray<int32>> Loops;
	TArray<FPolygon2d> Polygons;
	for (int32 StartIndex = 0; StartIndex < BoundaryVertices.Num(); ++StartIndex)
	{
		if (Visited.Contains(StartIndex) || Neighbors[StartIndex].IsEmpty())
		{
			continue;
		}
		TArray<int32>& Loop = Loops.Emplace_GetRef();
		FPolygon2d& Polygon = Polygons.Emplace_GetRef();
		int32 Previous = INDEX_NONE;
		int32 Current = StartIndex;
		const int32 FirstNeighbor = Neighbors[StartIndex][0];
		if (BoundaryDirections[FIntPoint(FMath::Min(StartIndex, FirstNeighbor), FMath::Max(StartIndex, FirstNeighbor))].X != StartIndex)
		{
			Swap(Neighbors[StartIndex][0], Neighbors[StartIndex][1]);
		}
		do
		{
			Visited.Add(Current);
			Loop.Add(Current);
			Polygon.AppendVertex(Triangulation.Vertices[Current]);
			int32 Next = Neighbors[Current][0];
			if (Next == Previous)
			{
				Next = Neighbors[Current][1];
			}
			Previous = Current;
			Current = Next;
		}
		while (Current != StartIndex);
	}
	TArray<int32> Parents;
	Parents.Init(INDEX_NONE, Loops.Num());
	for (int32 LoopIndex = 0; LoopIndex < Loops.Num(); ++LoopIndex)
	{
		double ParentArea = TNumericLimits<double>::Max();
		for (int32 Candidate = 0; Candidate < Loops.Num(); ++Candidate)
		{
			const double Area = Polygons[Candidate].Area();
			if (Polygons[Candidate].SignedArea() * Polygons[LoopIndex].SignedArea() < 0.0 && Area > Polygons[LoopIndex].Area() && Area < ParentArea && Polygons[Candidate].Contains(Polygons[LoopIndex]))
			{
				Parents[LoopIndex] = Candidate;
				ParentArea = Area;
			}
		}
	}
	TArray<int32> IntersectionVertices;
	for (int32 LoopIndex = 0; LoopIndex < Loops.Num(); ++LoopIndex)
	{
		int32 Depth = 0;
		for (int32 Parent = Parents[LoopIndex]; Parent != INDEX_NONE; Parent = Parents[Parent])
		{
			++Depth;
		}
		if (Depth % 2 != 0)
		{
			continue;
		}
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_TriangulateContour);
		FConstrainedDelaunay2d ShellTriangulation;
		ShellTriangulation.bOrientedEdges = false;
		ShellTriangulation.bOutputCCW = true;
		TArray<int32> BoundaryMapping;
		for (int32 Candidate = 0; Candidate < Loops.Num(); ++Candidate)
		{
			if (Candidate == LoopIndex || Parents[Candidate] == LoopIndex)
			{
				ShellTriangulation.Add(Polygons[Candidate]);
				BoundaryMapping.Append(Loops[Candidate]);
			}
		}
		if (!ShellTriangulation.Triangulate())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_ResolveContourIntersections);
			FArrangement2d Arrangement(Polygons[LoopIndex].Bounds());
			Arrangement.VertexSnapTol = 1.e-9;
			for (const FIndex2i& Edge : ShellTriangulation.Edges)
			{
				Arrangement.Insert(ShellTriangulation.Vertices[Edge.A], ShellTriangulation.Vertices[Edge.B]);
			}
			FConstrainedDelaunay2d ResolvedTriangulation;
			ResolvedTriangulation.bOrientedEdges = false;
			ResolvedTriangulation.bOutputCCW = true;
			ResolvedTriangulation.Add(Arrangement.Graph);
			if (!ResolvedTriangulation.Triangulate())
			{
				Failure = TEXT("A cut contour could not be triangulated after resolving intersections.");
				return false;
			}
			TArray<int32> ResolvedMapping;
			for (const FVector2d& Position : ResolvedTriangulation.Vertices)
			{
				int32 ExistingIndex = INDEX_NONE;
				for (int32 VertexIndex = 0; VertexIndex < ShellTriangulation.Vertices.Num(); ++VertexIndex)
				{
					if ((Position - ShellTriangulation.Vertices[VertexIndex]).SquaredLength() < 1.e-16)
					{
						ExistingIndex = BoundaryMapping[VertexIndex];
						break;
					}
				}
				if (ExistingIndex != INDEX_NONE)
				{
					ResolvedMapping.Add(ExistingIndex);
					continue;
				}
				FAPSkeletalSliceVertex Intersection;
				int32 ContributingEdges = 0;
				for (const FIndex2i& Edge : ShellTriangulation.Edges)
				{
					const FVector2d Start = ShellTriangulation.Vertices[Edge.A];
					const FVector2d Direction = ShellTriangulation.Vertices[Edge.B] - Start;
					const double Fraction = FMath::Clamp((Position - Start).Dot(Direction) / Direction.SquaredLength(), 0.0, 1.0);
					if ((Position - Start - Direction * Fraction).SquaredLength() > 1.e-14)
					{
						continue;
					}
					const FAPSkeletalSliceVertex EdgeVertex = InterpolateSliceVertex(BoundaryVertices[BoundaryMapping[Edge.A]], BoundaryVertices[BoundaryMapping[Edge.B]], Fraction);
					if (ContributingEdges == 0)
					{
						Intersection = EdgeVertex;
						Intersection.BoneWeights.Reset();
					}
					for (const TPair<int32, double>& Influence : EdgeVertex.BoneWeights)
					{
						Intersection.BoneWeights.FindOrAdd(Influence.Key) += Influence.Value;
					}
					++ContributingEdges;
				}
				if (ContributingEdges == 0)
				{
					Failure = TEXT("A resolved contour vertex has no source boundary edge.");
					return false;
				}
				for (TPair<int32, double>& Influence : Intersection.BoneWeights)
				{
					Influence.Value /= ContributingEdges;
				}
				Intersection.Position = PlaneTangent * Position.X + PlaneBitangent * Position.Y + PlaneNormal * Plane.W;
				const int32 IntersectionIndex = BoundaryVertices.Add(MoveTemp(Intersection));
				ResolvedMapping.Add(IntersectionIndex);
				IntersectionVertices.Add(IntersectionIndex);
			}
			ShellTriangulation = MoveTemp(ResolvedTriangulation);
			BoundaryMapping = MoveTemp(ResolvedMapping);
		}
		for (const FIndex3i& Triangle : ShellTriangulation.Triangles)
		{
			Triangulation.Triangles.Add(FIndex3i(BoundaryMapping[Triangle.A], BoundaryMapping[Triangle.B], BoundaryMapping[Triangle.C]));
		}
	}
	if (Triangulation.Triangles.IsEmpty() && !bAllowOpenContours)
	{
		Failure = TEXT("The cut boundary could not be triangulated.");
		return false;
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_SubdivideIntersectionEdges);
		for (TArray<FAPSkeletalSliceTriangle>* Output : {&PositiveTriangles, &NegativeTriangles})
		{
			for (int32 IntersectionIndex : IntersectionVertices)
			{
				const FAPSkeletalSliceVertex& Intersection = BoundaryVertices[IntersectionIndex];
				const int32 TriangleCount = Output->Num();
				for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
				{
					const FAPSkeletalSliceTriangle Original = (*Output)[TriangleIndex];
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						const FAPSkeletalSliceVertex& Start = Original.Vertices[Corner];
						const FAPSkeletalSliceVertex& End = Original.Vertices[(Corner + 1) % 3];
						if (FMath::Abs(Plane.PlaneDot(Start.Position)) > Tolerance || FMath::Abs(Plane.PlaneDot(End.Position)) > Tolerance)
						{
							continue;
						}
						const FVector Direction = End.Position - Start.Position;
						const double Fraction = FVector::DotProduct(Intersection.Position - Start.Position, Direction) / Direction.SizeSquared();
						if (Fraction <= 1.e-8 || Fraction >= 1.0 - 1.e-8 || (Start.Position + Direction * Fraction - Intersection.Position).SizeSquared() > 1.e-14)
						{
							continue;
						}
						FAPSkeletalSliceVertex BoundaryVertex = InterpolateSliceVertex(Start, End, Fraction);
						BoundaryVertex.Position = Intersection.Position;
						BoundaryVertex.BoneWeights = Intersection.BoneWeights;
						FAPSkeletalSliceTriangle FirstTriangle = Original;
						FirstTriangle.Vertices[Corner] = BoundaryVertex;
						FAPSkeletalSliceTriangle SecondTriangle = Original;
						SecondTriangle.Vertices[(Corner + 1) % 3] = BoundaryVertex;
						(*Output)[TriangleIndex] = MoveTemp(FirstTriangle);
						Output->Add(MoveTemp(SecondTriangle));
						break;
					}
				}
			}
		}
	}
	int32 SliceCapIndex = 0;
	for (const FIndex3i& TriangleIndices : Triangulation.Triangles)
	{
		FAPSkeletalSliceTriangle NegativeCap;
		NegativeCap.MaterialIndex = CapMaterialIndex;
		NegativeCap.SliceCapIndex = SliceCapIndex++;
		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
			FAPSkeletalSliceVertex& Vertex = NegativeCap.Vertices[CornerIndex];
			Vertex = BoundaryVertices[TriangleIndices[CornerIndex]];
			Vertex.Normal = PlaneNormal;
			Vertex.Tangent = PlaneTangent;
			Vertex.TextureCoordinates.SetNum(FMath::Max(3, Vertex.TextureCoordinates.Num()));
			Vertex.TextureCoordinates[0] = FVector2D(FVector::DotProduct(Vertex.Position, PlaneTangent), FVector::DotProduct(Vertex.Position, PlaneBitangent));
			Vertex.TextureCoordinates[1] = FVector2D(Vertex.Position.X, Vertex.Position.Y);
			Vertex.TextureCoordinates[2] = FVector2D(Vertex.Position.Z, 0.0);
		}
		NegativeTriangles.Add(NegativeCap);
		Swap(NegativeCap.Vertices[1], NegativeCap.Vertices[2]);
		for (FAPSkeletalSliceVertex& Vertex : NegativeCap.Vertices)
		{
			Vertex.Normal *= -1.0;
		}
		PositiveTriangles.Add(MoveTemp(NegativeCap));
	}
	return true;
}

void FAPSkeletalSliceGeometry::MakeSurfaces(const TArray<FAPSkeletalSliceTriangle>& Triangles, TArray<FAPSkeletalMeshSurface>& Surfaces)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_MakeSurfaces);
	Surfaces.Reset();
	int32 CoordinateCount = 3;
	for (const FAPSkeletalSliceTriangle& Triangle : Triangles)
	{
		for (const FAPSkeletalSliceVertex& Vertex : Triangle.Vertices)
		{
			CoordinateCount = FMath::Max(CoordinateCount, Vertex.TextureCoordinates.Num());
		}
	}
	TMap<int32, int32> MaterialToSurface;
	for (const FAPSkeletalSliceTriangle& Triangle : Triangles)
	{
		if (!MaterialToSurface.Contains(Triangle.MaterialIndex))
		{
			MaterialToSurface.Add(Triangle.MaterialIndex, Surfaces.Num());
			Surfaces.Emplace_GetRef().MaterialIndex = Triangle.MaterialIndex;
		}
		FAPSkeletalMeshSurface& Surface = Surfaces[MaterialToSurface[Triangle.MaterialIndex]];
		for (const FAPSkeletalSliceVertex& Vertex : Triangle.Vertices)
		{
			const int32 VertexIndex = Surface.Vertices.Add(Vertex.Position);
			Surface.Indices.Add(VertexIndex);
			Surface.Normals.Add(Vertex.Normal);
			Surface.Tangents.Add(Vertex.Tangent);
			Surface.Colors.Add(Vertex.Color.ToFColor(false));
			Surface.FlipBinormalSigns.Add(false);
			TArray<FVector2D> Coordinates = Vertex.TextureCoordinates;
			Coordinates.SetNumZeroed(CoordinateCount);
			Surface.TextureCoordinates.Add(MoveTemp(Coordinates));
			TArray<FAPSkeletalBoneInfluence>& Influences = Surface.BoneInfluences.Emplace_GetRef();
			for (const TPair<int32, double>& Influence : Vertex.BoneWeights)
			{
				if (Influence.Value > 0.0)
				{
					Influences.Emplace(VertexIndex, Influence.Key, Influence.Value);
				}
			}
			Influences.Sort([](const FAPSkeletalBoneInfluence& Left, const FAPSkeletalBoneInfluence& Right)
			{
				if (Left.Weight == Right.Weight)
				{
					return Left.BoneIndex < Right.BoneIndex;
				}
				return Left.Weight > Right.Weight;
			});
			if (Influences.Num() > MAX_TOTAL_INFLUENCES)
			{
				Influences.SetNum(MAX_TOTAL_INFLUENCES);
			}
			double TotalWeight = 0.0;
			for (const FAPSkeletalBoneInfluence& Influence : Influences)
			{
				TotalWeight += Influence.Weight;
			}
			for (FAPSkeletalBoneInfluence& Influence : Influences)
			{
				Influence.Weight /= TotalWeight;
			}
		}
		Swap(Surface.Indices[Surface.Indices.Num() - 2], Surface.Indices.Last());
	}
}
