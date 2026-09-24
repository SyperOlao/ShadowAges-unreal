#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "Anatomy/SAAnatomyTypes.h"
#include "SAAnatomyAIController.generated.h"

class USAAnatomyComponent;

// Optional bridge for existing BTs: records injury without imposing a locomotion
// or permanent ability penalty before authored visual support exists.
UCLASS()
class SHADOWAGES_API ASAAnatomyAIController : public AAIController
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SA|Anatomy")
    FName LastInjuredZone;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SA|Anatomy")
    FName InjuredZoneBlackboardKey = NAME_None;
    UFUNCTION(BlueprintImplementableEvent, Category="SA|Anatomy")
    void OnAnatomyInjury(const FSALimbState& State, const FSAAnatomyContactSnapshot& Contact);
protected:
    virtual void OnPossess(APawn* Pawn) override;
    virtual void OnUnPossess() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void UnbindAnatomy();
    void HandleLimbChanged(const FSALimbState& State, const FSAAnatomyContactSnapshot& Contact, bool bAlive);
    TWeakObjectPtr<USAAnatomyComponent> Anatomy;
    FDelegateHandle InjuryHandle;
};
