#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Anatomy/SAAnatomyDefinition.h"
#include "Anatomy/SAAnatomyTypes.h"
#include "SAAnatomyComponent.generated.h"

struct FReferenceSkeleton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSAOnLimbChanged, const FSALimbState&, State,
    const FSAAnatomyContactSnapshot&, Contact, bool, bOwnerAlive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSAOnLimbLost, const FSALimbState&, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSAOnSeverRequested, const FSASeverIntent&, Intent);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FSAOnLimbChangedNative, const FSALimbState&,
    const FSAAnatomyContactSnapshot&, bool);

UCLASS(ClassGroup=(ShadowAges), meta=(BlueprintSpawnableComponent))
class SHADOWAGES_API USAAnatomyComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    USAAnatomyComponent();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SA|Anatomy")
    TObjectPtr<USAAnatomyDefinition> Definition;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SA|Anatomy")
    bool bDrawDebug = false;
    UPROPERTY(BlueprintAssignable, Category="SA|Anatomy")
    FSAOnLimbChanged OnLimbChanged;
    UPROPERTY(BlueprintAssignable, Category="SA|Anatomy")
    FSAOnLimbLost OnLimbLost;
    UPROPERTY(BlueprintAssignable, Category="SA|Anatomy")
    FSAOnSeverRequested OnSeverRequested;
    FSAOnLimbChangedNative OnLimbChangedNative;

    UFUNCTION(BlueprintCallable, Category="SA|Anatomy")
    bool InitializeAnatomy(USAAnatomyDefinition* Profile, USkeletalMeshComponent* BodyMesh);
    UFUNCTION(BlueprintPure, Category="SA|Anatomy")
    bool GetLimbState(FName ZoneId, FSALimbState& State) const;
    UFUNCTION(BlueprintPure, Category="SA|Anatomy")
    bool HasFunctionalBodyTags(const TArray<FName>& RequiredTags) const;
    UFUNCTION(BlueprintCallable, Category="SA|Anatomy")
    FName ResolveBone(FName Bone);
    // Explicit rejection: no visual implementation for live severing in phase 4.
    UFUNCTION(BlueprintCallable, Category="SA|Anatomy")
    bool RequestLiveSever(const FSASeverIntent& Intent) { return false; }

    FSAAnatomyContactSnapshot CaptureContact(const FSAHitContext& Context);
    FSALimbChangeBatch ConsumeResolvedDamageStateOnly(const FSADamageResult& Result,
        const FSAAnatomyContactSnapshot& Snapshot);
    void DeliverLimbChanges(FSALimbChangeBatch Batch);
    static FName FindZoneByAncestry(const FReferenceSkeleton& Skeleton, FName Bone,
        const TMap<FName, FName>& Roots);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool RebuildBoneCache();
    bool IsUsable() const;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    TWeakObjectPtr<USkeletalMesh> CachedMeshAsset;
    TMap<FName, FName> BoneToZone;
    TMap<FName, FSAAnatomyZone> Rules;
    TMap<FName, FSALimbState> Limbs;
    TMap<FName, uint64> Revisions;
    TSet<FGuid> DeliveryTokens;
    FGuid LifeId;
};
