#pragma once
#include "CoreMinimal.h"
#include "Combat/SAWeaponMoveset.h"
#include "SAMeleeRequest.generated.h"

USTRUCT(BlueprintType)
struct SHADOWAGES_API FSAMeleeRequest
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	TObjectPtr<USAWeaponMoveset> Moveset = nullptr;

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	int32 EntryStepOverride = INDEX_NONE;
	
};
