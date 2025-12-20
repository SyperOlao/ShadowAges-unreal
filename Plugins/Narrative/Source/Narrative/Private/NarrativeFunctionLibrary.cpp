// Copyright Narrative Tools 2022. 


#include "NarrativeFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "NarrativeComponent.h"
#include "NarrativeTaskManager.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h"

class UNarrativeComponent* UNarrativeFunctionLibrary::GetNarrativeComponent(const UObject* WorldContextObject)
{
	return GetNarrativeComponentFromTarget(UGameplayStatics::GetPlayerController(WorldContextObject, 0));
}

class UNarrativeComponent* UNarrativeFunctionLibrary::GetNarrativeComponentFromTarget(AActor* Target)
{
	if (!Target)
	{
		return nullptr;
	}

	if (UNarrativeComponent* NarrativeComp = Target->FindComponentByClass<UNarrativeComponent>())
	{
		return NarrativeComp;
	}

	//Narrative comp may be on the controllers pawn or pawns controller
	if (APlayerController* OwningController = Cast<APlayerController>(Target))
	{
		if (OwningController->GetPawn())
		{
			return OwningController->GetPawn()->FindComponentByClass<UNarrativeComponent>();
		}
	}

	if (APawn* OwningPawn = Cast<APawn>(Target))
	{
		if (OwningPawn->GetController())
		{
			return OwningPawn->GetController()->FindComponentByClass<UNarrativeComponent>();
		}
	}

	return nullptr;
}


class UNarrativeComponent* UNarrativeFunctionLibrary::GetSharedNarrativeComponentFromTarget(AActor* Target)
{

	if (UNarrativeComponent* NarrativeComp = GetNarrativeComponentFromTarget(Target))
	{
		return NarrativeComp->SharedNarrativeComp;
	}

	return nullptr;
}

bool UNarrativeFunctionLibrary::CompleteNarrativeDataTask(class UNarrativeComponent* Target, const UNarrativeDataTask* Task, const FString& Argument, const int32 Quantity)
{
	if (Target)
	{
		return Target->CompleteNarrativeDataTask(Task, Argument, Quantity);
	}
	return false;
}

static FString DefaultString("LooseTask");

bool UNarrativeFunctionLibrary::CompleteLooseNarrativeDataTask(class UNarrativeComponent* Target, const FString& Argument, const int32 Quantity /*= 1*/)
{
	if (Target)
	{
		return Target->CompleteNarrativeDataTask(DefaultString, Argument, Quantity);
	}
	return false;
}

class UNarrativeDataTask* UNarrativeFunctionLibrary::GetTaskByName(const UObject* WorldContextObject, const FString& EventName)
{
	if (UGameInstance* GI = UGameplayStatics::GetGameInstance(WorldContextObject))
	{
		if (UNarrativeTaskManager* EventManager = GI->GetSubsystem<UNarrativeTaskManager>())
		{
			return EventManager->GetTask(EventName);
		}
	}

	return nullptr;
}

FString UNarrativeFunctionLibrary::MakeDisplayString(const FString& String)
{
	return FName::NameToDisplayString(String, false);
}

void UNarrativeFunctionLibrary::PreDuplicateMarkedObjects(UObject* SourceObject, UObject* DestOuter, TMap<UObject*, UObject*>& DuplicationSeed)
{
    if (!SourceObject) return;


    for (TFieldIterator<FProperty> PropIt(SourceObject->GetClass()); PropIt; ++PropIt)
    {
        FProperty* Property = *PropIt;

        if (FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Property))
        {
#if WITH_EDITOR
            if (!ObjectProp->HasMetaData(TEXT("DeepDuplicate")))
            {
                continue;
            }
#endif

            void* ValuePtr = ObjectProp->ContainerPtrToValuePtr<void>(SourceObject);
            UObject* Referenced = ObjectProp->GetObjectPropertyValue(ValuePtr);

            if (Referenced && !DuplicationSeed.Contains(Referenced))
            {
                PreDuplicateMarkedObjects(Referenced, DestOuter, DuplicationSeed);

                FName NewName = MakeUniqueObjectName(DestOuter, Referenced->GetClass(), Referenced->GetFName());
                UObject* NewCopy = StaticDuplicateObject(Referenced, DestOuter, NewName);
                DuplicationSeed.Add(Referenced, NewCopy);
            }
        }

        else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
        {
#if WITH_EDITOR
            if (!ArrayProp->HasMetaData(TEXT("DeepDuplicate")))
            {
                continue;
            }
#endif

            if (FObjectPropertyBase* InnerObjectProp = CastField<FObjectPropertyBase>(ArrayProp->Inner))
            {
                FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(SourceObject));
                for (int32 Index = 0; Index < Helper.Num(); ++Index)
                {
                    void* ElemPtr = Helper.GetRawPtr(Index);
                    UObject* Referenced = InnerObjectProp->GetObjectPropertyValue(ElemPtr);

                    if (Referenced && !DuplicationSeed.Contains(Referenced))
                    {
                        PreDuplicateMarkedObjects(Referenced, DestOuter, DuplicationSeed);
                        FName NewName = MakeUniqueObjectName(DestOuter, Referenced->GetClass(), Referenced->GetFName());
                        UObject* NewCopy = StaticDuplicateObject(Referenced, DestOuter, NewName);
                        DuplicationSeed.Add(Referenced, NewCopy);
                    }
                }
            }
        }

        // ѕримечание: дл€ Map/Set/Struct/SoftPtr/WekPtr/AssetRefs нужны дополнительные ветки.
    }
}

UObject* UNarrativeFunctionLibrary::DeepDuplicateWithMeta(UObject* Root, UObject* DestOuter, FName DestName)
{
	return nullptr;

}

