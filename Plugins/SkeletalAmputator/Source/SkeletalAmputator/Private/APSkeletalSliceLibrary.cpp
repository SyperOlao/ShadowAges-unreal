#include "APSkeletalSliceLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

TArray<FSliceResult> UAPSkeletalSliceLibrary::SliceMeshesInRadius(const UObject* WorldContextObject, const FVector& SliceCenter, const FVector& SliceNormal, float SliceRadius, EAPSliceSpace SliceSpace)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SkeletalAmputator_SliceMeshesInRadius);
	TArray<FSliceResult> Results;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World || SliceRadius <= 0.0f || !FMath::IsFinite(SliceRadius) || SliceCenter.ContainsNaN() || SliceNormal.ContainsNaN() || SliceNormal.IsNearlyZero())
	{
		return Results;
	}
	TArray<UAPSliceableSkeletalMeshComponent*> Candidates;
	for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator)
	{
		TInlineComponentArray<UAPSliceableSkeletalMeshComponent*> Components;
		ActorIterator->GetComponents(Components);
		for (UAPSliceableSkeletalMeshComponent* Component : Components)
		{
			if (Component->IsRegistered())
			{
				Candidates.Add(Component);
			}
		}
	}
	for (UAPSliceableSkeletalMeshComponent* Component : Candidates)
	{
		if (!IsValid(Component) || !Component->IsRegistered() || Component->GetWorld() != World)
		{
			continue;
		}
		FSliceResult Result = Component->SliceMeshInRadius(SliceCenter, SliceNormal, SliceRadius, SliceSpace);
		if (Result.bSuccess)
		{
			Results.Add(MoveTemp(Result));
		}
	}
	return Results;
}
