#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/Types/SAMeleeTypes.h"
#include "SAWeaponMoveset.generated.h"

UCLASS(BlueprintType)
class SHADOWAGES_API USAWeaponMoveset : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Moveset", meta = (ClampMin = "0"))
	int32 EntryStepIndex = 0;
	
	static constexpr int32 Max_Steps = 64;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Moveset")
	TArray<FSAMeleeStep> Steps;

	bool Validate(FString& OutError) const;
	
};


