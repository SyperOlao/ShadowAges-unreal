// Copyright 2025 SGSAmputationLab and AO. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "APSliceableCharacter.generated.h"

class UNiagaraSystem;
class UAPSliceableSkeletalMeshComponent;
struct FSliceResult;

UCLASS()
class SKELETALAMPUTATOR_API AAPSliceableCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAPSliceableCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
	UFUNCTION(Blueprintable, Category = "Slice")
	UAPSliceableSkeletalMeshComponent* GetSliceableMesh() const { return SliceableMesh; }

	UFUNCTION(BlueprintCallable, Category = "Slice")
	void HandlePostSlice(const FSliceResult& SliceResult);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slice")
	TObjectPtr<UAPSliceableSkeletalMeshComponent> SliceableMesh;
	
	UPROPERTY(VisibleAnywhere, Category = "Slice")
	TArray<UAPSliceableSkeletalMeshComponent*> SliceableMeshes;

	UPROPERTY(VisibleAnywhere, Category = "Slice")
	TArray<USceneComponent*> CapSockets;

	UPROPERTY(VisibleAnywhere, Category = "Slice")
	TArray<UNiagaraSystem*> SliceNiagaraEffects;

	UPROPERTY(VisibleAnywhere, Category = "Slice")
	TArray<UParticleSystem*> SliceParticleEffects;
};
