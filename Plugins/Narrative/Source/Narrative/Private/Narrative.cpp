// Copyright Narrative Tools 2022. 

#include "Narrative.h"

DEFINE_LOG_CATEGORY(LogNarrativeRuntime);

#define LOCTEXT_NAMESPACE "FNarrativeModule"

void FNarrativeModule::StartupModule()
{
	UE_LOG(LogNarrativeRuntime, Warning, TEXT("Narrative Runtime loaded."));
}

void FNarrativeModule::ShutdownModule()
{
	UE_LOG(LogNarrativeRuntime, Warning, TEXT("Narrative Runtime unloaded."));
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FNarrativeModule, Narrative)