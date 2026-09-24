#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Anatomy/SAAnatomyTypes.h"
#include "SACorpseSliceAdapterComponent.generated.h"

class USceneComponent;
UCLASS(ClassGroup=(ShadowAges), meta=(BlueprintSpawnableComponent))
class SHADOWAGES_API USACorpseSliceAdapterComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USACorpseSliceAdapterComponent();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SA|Corpse")
    bool bEnableCorpseSlicing = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SA|Corpse", meta=(ClampMin="1.0"))
    float SliceRadius = 25.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SA|Corpse", meta=(ClampMin="1.0"))
    float FragmentLifetime = 15.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SA|Corpse", meta=(ClampMin="0", ClampMax="16"))
    int32 MaxDetachedParts = 4;
    bool TrySliceCorpse(const FSASeverIntent& Intent, const FSAAnatomyContactSnapshot& Snapshot);
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void CleanupFragments();
    bool bAttempted = false;
    TArray<TWeakObjectPtr<USceneComponent>> Fragments;
    FTimerHandle CleanupTimer;
};
