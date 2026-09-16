// Copyright 2025 SGSAmputationLab and AO. All Rights Reserved.

#include "EditorAssetSaveLibrary.h"

#if WITH_EDITOR
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/SavePackage.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Editor.h"
#include "Misc/MessageDialog.h"
#endif

UObject* UEditorAssetSaveLibrary::SaveAssetToContentBrowser(UObject* SourceAsset, const FString& DesiredPackagePath, const FString& DesiredAssetName)
{
	if (SourceAsset == nullptr)
	{
		return nullptr;
	}

#if WITH_EDITOR
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	FString PackagePath = DesiredPackagePath;
	if (PackagePath.IsEmpty())
	{
		PackagePath = TEXT("/Game/Generated");
	}
	FString BaseName = DesiredAssetName;
	if (BaseName.IsEmpty())
	{
		BaseName = SourceAsset->GetName();
	}

	FString Candidate = PackagePath / BaseName;
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetToolsModule.Get().CreateUniqueAssetName(Candidate, TEXT(""), UniquePackageName, UniqueAssetName);

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (Package == nullptr)
	{
		return nullptr;
	}

	EObjectFlags NewFlags = RF_Public | RF_Standalone;
	UObject* NewAsset = DuplicateObject(SourceAsset, Package, *UniqueAssetName);
	if (NewAsset == nullptr)
	{
		return nullptr;
	}

	NewAsset->SetFlags(NewFlags);
	Package->MarkPackageDirty();

	AssetRegistryModule.Get().AssetCreated(NewAsset);

	FString PackageFileName = FPackageName::LongPackageNameToFilename(UniquePackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = NewFlags;
	SaveArgs.SaveFlags = SAVE_None;
	if (!UPackage::SavePackage(Package, NewAsset, *PackageFileName, SaveArgs))
	{
		return nullptr;
	}

	TArray<FAssetData> AssetsToSync;
	AssetsToSync.Add(FAssetData(NewAsset));
	ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync);

	return NewAsset;
#else
	return nullptr;
#endif
}

USkeletalMesh* UEditorAssetSaveLibrary::SaveSkeletalMeshToContentBrowser(USkeletalMesh* SourceMesh, const FString& DesiredPackagePath, const FString& DesiredAssetName)
{
#if WITH_EDITOR
	return Cast<USkeletalMesh>(SaveAssetToContentBrowser(Cast<UObject>(SourceMesh), DesiredPackagePath, DesiredAssetName));
#else
	return nullptr;
#endif
}

UStaticMesh* UEditorAssetSaveLibrary::SaveStaticMeshToContentBrowser(UStaticMesh* SourceMesh, const FString& DesiredPackagePath, const FString& DesiredAssetName)
{
#if WITH_EDITOR
	return Cast<UStaticMesh>(SaveAssetToContentBrowser(Cast<UObject>(SourceMesh), DesiredPackagePath, DesiredAssetName));
#else
	return nullptr;
#endif
}
