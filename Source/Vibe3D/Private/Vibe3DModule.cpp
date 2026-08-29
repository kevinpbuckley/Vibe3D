// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "IPythonScriptPlugin.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogVibe3D, Log, All);

namespace
{
	/** Every non-abstract UToolsetDefinition subclass defined in this module that exposes at least one AICallable tool. */
	void GatherVibe3DToolsetClasses(TArray<UClass*>& OutClasses)
	{
		TArray<UClass*> Derived;
		GetDerivedClasses(UToolsetDefinition::StaticClass(), Derived, /*bRecursive*/ true);
		const UPackage* ModulePackage = FindPackage(nullptr, TEXT("/Script/Vibe3D"));
		for (UClass* Class : Derived)
		{
			if (!Class || Class->HasAnyClassFlags(CLASS_Abstract) || Class->GetOutermost() != ModulePackage)
			{
				continue;
			}
			bool bHasAICallable = false;
			for (TFieldIterator<UFunction> It(Class); It && !bHasAICallable; ++It)
			{
				const TValueOrError<bool, FString> Result = UToolsetDefinition::IsFunctionAICallable(*It);
				bHasAICallable = Result.HasValue() && Result.GetValue();
			}
			if (bHasAICallable)
			{
				OutClasses.Add(Class);
			}
		}
	}

	FString PluginContentDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Vibe3D"));
		return Plugin.IsValid() ? Plugin->GetContentDir() : FString();
	}
}

/**
 * Vibe3D: procedural modeling as an AI Editor Toolset. The module's only job is to hand the
 * AICallable service classes (UModelingService) to UE 5.8's ToolsetRegistry so they appear on the
 * native MCP endpoint, and to expose a console command that re-reads the markdown skills.
 */
class FVibe3DModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (IsRunningCommandlet())
		{
			return;
		}
		if (GEditor)
		{
			RegisterToolsets();
		}
		else
		{
			FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FVibe3DModule::RegisterToolsets);
		}
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
		if (UToolsetRegistry::IsAvailable())
		{
			for (UClass* Class : RegisteredClasses)
			{
				UToolsetRegistry::UnregisterToolsetClass(Class);
			}
		}
		RegisteredClasses.Reset();
	}

private:
	void RegisterToolsets()
	{
		if (!UToolsetRegistry::IsAvailable())
		{
			UE_LOG(LogVibe3D, Warning, TEXT("Vibe3D: ToolsetRegistry not available; toolsets not registered."));
			return;
		}
		TArray<UClass*> Classes;
		GatherVibe3DToolsetClasses(Classes);
		for (UClass* Class : Classes)
		{
			UToolsetRegistry::RegisterToolsetClass(Class);
			RegisteredClasses.Add(Class);
		}
		UE_LOG(LogVibe3D, Display, TEXT("Vibe3D: registered %d toolset(s) with ToolsetRegistry."), Classes.Num());
	}

	TArray<UClass*> RegisteredClasses;
};

// Re-run Content/Python/init_unreal.py so edited SKILL.md files are served without an editor restart.
static FAutoConsoleCommand Vibe3DReloadSkillsCommand(
	TEXT("Vibe3D.ReloadSkills"),
	TEXT("Re-read Vibe3D's Content/Skills markdown into the registered AgentSkills (no editor restart needed)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		const FString ContentDir = PluginContentDir();
		if (!Python || !Python->IsPythonAvailable() || ContentDir.IsEmpty())
		{
			UE_LOG(LogVibe3D, Warning, TEXT("Vibe3D.ReloadSkills: Python or the plugin content directory is not available."));
			return;
		}
		Python->ExecPythonCommand(*(ContentDir / TEXT("Python") / TEXT("init_unreal.py")));
	})
);

IMPLEMENT_MODULE(FVibe3DModule, Vibe3D)
