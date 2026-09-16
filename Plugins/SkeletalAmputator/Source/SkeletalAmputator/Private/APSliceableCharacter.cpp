// Copyright 2025 SGSAmputationLab and AO. All Rights Reserved.

#include "APSliceableCharacter.h"
#include "APSliceableSkeletalMeshComponent.h"

AAPSliceableCharacter::AAPSliceableCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UAPSliceableSkeletalMeshComponent>(ACharacter::MeshComponentName))
{
 	PrimaryActorTick.bCanEverTick = true;
	SliceableMesh = CastChecked<UAPSliceableSkeletalMeshComponent>(GetMesh());
}

void AAPSliceableCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (GetSliceableMesh())
	{
		GetSliceableMesh()->OnSliceMesh.AddDynamic(this, &AAPSliceableCharacter::HandlePostSlice);
		UE_LOG(LogTemp, Warning, TEXT("[SkeletalAmputator] SliceableMesh is set and OnSliceMesh event is bound."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[SkeletalAmputator] SliceableMesh is not set or invalid."));
	}
}

void AAPSliceableCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AAPSliceableCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void AAPSliceableCharacter::HandlePostSlice(const FSliceResult& SliceResult)
{
	for (UAPSliceableSkeletalMeshComponent* Fragment : SliceResult.PositiveFragments)
	{
		SliceableMeshes.AddUnique(Fragment);
	}
	for (UAPSliceableSkeletalMeshComponent* Fragment : SliceResult.NegativeFragments)
	{
		SliceableMeshes.AddUnique(Fragment);
	}
}
