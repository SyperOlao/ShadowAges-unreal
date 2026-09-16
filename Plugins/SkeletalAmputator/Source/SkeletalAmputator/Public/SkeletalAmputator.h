// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(SGS, Log, All);

#define CALLINFO (FString(__FUNCTION__) + TEXT("(") + FString::FromInt(__LINE__) + TEXT(")"))

#define PRINT_CALLINFO() UE_LOG(SGS, Warning, TEXT("%s"), *CALLINFO)

#define PRINT_LOG(Format, ...) UE_LOG(SGS, Warning, TEXT("%s"), *CALLINFO,\
*FString::Printf(Format, ##__VA_Args__))

class FSkeletalAmputatorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
