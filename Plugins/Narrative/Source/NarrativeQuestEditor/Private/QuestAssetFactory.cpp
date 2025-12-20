// Copyright Narrative Tools 2022. 

#include "QuestAssetFactory.h"
#include "Editor/ClassViewer/Public/ClassViewerModule.h"
#include "Editor/ClassViewer/Public/ClassViewerFilter.h"
#include "QuestBlueprint.h"
#include "Kismet2/SClassPickerDialog.h"
#include "QuestBlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Quest.h"
#include "BlueprintEditorSettings.h"
#include "QuestGraph.h"
#include "QuestGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "QuestAssetFactory"

class FQuestAssetFilterViewer : public IClassViewerFilter
{
public:

	TSet<const UClass*> AllowedChildrenOfClasses;
	uint32 DisallowedClassFlags;

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<class FClassViewerFilterFuncs> InFilterFuncs)
	{
		return InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InClass) != EFilterReturn::Failed;
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InUnloadedClassData) != EFilterReturn::Failed;
	}
};

UQuestAssetFactory::UQuestAssetFactory()
{
	SupportedClass = UQuestBlueprint::StaticClass();
	ParentClass = UQuest::StaticClass();

	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UQuestAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	check(Class->IsChildOf(UQuestBlueprint::StaticClass()));

	UQuestBlueprint* QuestBP = nullptr;

	//For some reason this is failing the nullcheck so set manually for now 

	QuestBP = CastChecked<UQuestBlueprint>(FKismetEditorUtilities::CreateBlueprint(UQuest::StaticClass(), InParent, Name, BPTYPE_Normal, UQuestBlueprint::StaticClass(), UQuestBlueprintGeneratedClass::StaticClass(), CallingContext));

	//QuestBP->QuestTemplate = NewObject<UQuest>(QuestBP, UQuest::StaticClass(), NAME_None, RF_Transactional);

	if (UQuest* QuestCDO = Cast<UQuest>(QuestBP->GeneratedClass->GetDefaultObject()))
	{
		FString NameString = QuestBP->GetFName().ToString();

		//Add the Default Speaker to the dialogue 
		int32 UnderscoreIndex = -1;

		if (NameString.FindChar(TCHAR('_'), UnderscoreIndex))
		{
			//remove D_SpeakerName prefix 
			QuestCDO->QuestName = FText::FromString(NameString.RightChop(UnderscoreIndex + 1));
		}
		else
		{
			QuestCDO->QuestName = FText::FromString(NameString);
		}
	}

	return QuestBP;
}

UObject* UQuestAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return FactoryCreateNew(Class, InParent, Name, Flags, Context, Warn, NAME_None);
}

bool UQuestAssetFactory::ConfigureProperties()
{
	return true;

	//FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	//FClassViewerInitializationOptions Options;
	//Options.Mode = EClassViewerMode::ClassPicker;

	//TSharedPtr< FQuestAssetFilterViewer> Filter = MakeShareable<FQuestAssetFilterViewer> (new FQuestAssetFilterViewer);
	//Options.ClassFilter = Filter;

	//Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated;
	//Filter->AllowedChildrenOfClasses.Add(UQuest::StaticClass());

	//const FText TitleText = LOCTEXT("CreateAssetOptions", "Pick Quest Asset Class");
	//UClass* ChosenClass = nullptr;
	//const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, UQuestBlueprint::StaticClass());

	//if (bPressedOk)
	//{
	//	ParentClass = ChosenClass;
	//}

	//return bPressedOk;

}

bool UQuestAssetFactory::CanCreateNew() const
{
	return true;
}

FString UQuestAssetFactory::GetDefaultNewAssetName() const
{
	return FString(TEXT("NewQuest"));
}

FText UQuestAssetFactory::GetDisplayName() const
{
	return LOCTEXT("QuestText", "Quest");
}

#undef LOCTEXT_NAMESPACE
