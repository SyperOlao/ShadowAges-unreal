// Copyright Narrative Tools 2022. 

#include "DialogueEditorStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FDialogueEditorStyle::StyleSet = nullptr;
TSharedPtr<ISlateStyle> FDialogueEditorStyle::Get() { return StyleSet; }

//Helper functions from UE4 forums to easily create box and image brushes
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )

FString FDialogueEditorStyle::RootToContentDir(const ANSICHAR* RelativePath, const TCHAR* Extension)
{
	//Find quest plugin content directory
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("Narrative"))->GetContentDir();
	return (ContentDir / RelativePath) + Extension;
}
FName FDialogueEditorStyle::GetStyleSetName()
{
	static FName DialogueEditorStyleName(TEXT("DialogueEditorStyle"));
	return DialogueEditorStyleName;
}

void FDialogueEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	//Thumbnails and icons
	StyleSet->Set(FName(TEXT("ClassThumbnail.DialogueBlueprint")), new IMAGE_BRUSH("Dialogue", FVector2D(64, 64)));
	StyleSet->Set(FName(TEXT("ClassIcon.DialogueBlueprint")), new IMAGE_BRUSH("Dialogue", FVector2D(16, 16)));

	StyleSet->Set(FName(TEXT("ClassThumbnail.Dialogue")), new IMAGE_BRUSH("Dialogue", FVector2D(64, 64)));
	StyleSet->Set(FName(TEXT("ClassIcon.Dialogue")), new IMAGE_BRUSH("Dialogue", FVector2D(16, 16)));

	//Toolbar buttons
	StyleSet->Set(FName(TEXT("DialogueEditor.Common.ShowDialogueDetails")), new IMAGE_BRUSH("DialogueEditor/Icons/ShowDialogueDetails_40x", FVector2D(40.f, 40.f)));
	StyleSet->Set(FName(TEXT("DialogueEditor.Common.ShowDialogueDetails.Small")), new IMAGE_BRUSH("DialogueEditor/Icons/ShowDialogueDetails_40x", FVector2D(20.f, 20.f)));
	StyleSet->Set(FName(TEXT("DialogueEditor.Common.ViewTutorial")), new IMAGE_BRUSH("DialogueEditor/Icons/ViewTutorial_40x", FVector2D(40.f, 40.f)));
	StyleSet->Set(FName(TEXT("DialogueEditor.Common.ViewTutorial.Small")), new IMAGE_BRUSH("DialogueEditor/Icons/ViewTutorial_40x", FVector2D(20.f, 20.f)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
};

#undef BOX_BRUSH
#undef IMAGE_BRUSH

void FDialogueEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

