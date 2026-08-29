// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
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

// Write the Vibe3D agent guide into the project's agent memory files from the bundled sample —
// the same contract as VibeUE.GenerateAgentConfig: the guide lands inside a marked block so re-runs
// refresh only that block and never touch the user's own content.
//
// Only Claude Code (CLAUDE.md) and Gemini CLI (GEMINI.md) resolve `@path` imports, so the default
// COPIES the guide in; pass "import" to write a one-line `@<sample path>` for those two instead.
static void GenerateVibe3DAgentConfig(const TArray<FString>& Args, FOutputDevice& Ar)
{
	const FString Client = (Args.Num() > 0) ? Args[0].ToLower() : TEXT("all");
	bool bImportRequested = false;
	for (int32 i = 1; i < Args.Num(); ++i)
	{
		const FString A = Args[i].ToLower();
		if (A == TEXT("import") || A == TEXT("link") || A == TEXT("-import") || A == TEXT("--import"))
		{
			bImportRequested = true;
		}
	}

	const FString ContentDir = PluginContentDir();
	if (ContentDir.IsEmpty())
	{
		Ar.Log(TEXT("Vibe3D.GenerateAgentConfig: ERROR - could not locate the Vibe3D plugin."));
		return;
	}
	const FString SamplePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(ContentDir, TEXT("samples"), TEXT("AGENTS.md.sample")));
	FString SampleContent;
	if (!FFileHelper::LoadFileToString(SampleContent, *SamplePath))
	{
		Ar.Logf(TEXT("Vibe3D.GenerateAgentConfig: ERROR - could not read sample at %s"), *SamplePath);
		return;
	}

	// Target file per agent, and whether that agent resolves @-imports.
	TArray<TPair<FString, bool>> Targets;
	if (Client == TEXT("claude") || Client == TEXT("claudecode"))
	{
		Targets.Add(TPair<FString, bool>(TEXT("CLAUDE.md"), true));
	}
	else if (Client == TEXT("gemini"))
	{
		Targets.Add(TPair<FString, bool>(TEXT("GEMINI.md"), true));
	}
	else if (Client == TEXT("copilot"))
	{
		Targets.Add(TPair<FString, bool>(TEXT(".github/copilot-instructions.md"), false));
	}
	else if (Client == TEXT("codex") || Client == TEXT("hermes") || Client == TEXT("cursor") || Client == TEXT("agents") || Client == TEXT("agent"))
	{
		Targets.Add(TPair<FString, bool>(TEXT("AGENTS.md"), false));
	}
	else if (Client == TEXT("all"))
	{
		Targets.Add(TPair<FString, bool>(TEXT("CLAUDE.md"), true));
		Targets.Add(TPair<FString, bool>(TEXT("GEMINI.md"), true));
		Targets.Add(TPair<FString, bool>(TEXT("AGENTS.md"), false));
	}
	else
	{
		Ar.Logf(TEXT("Vibe3D.GenerateAgentConfig: unknown client '%s'. Use: ClaudeCode | Gemini | Codex | Hermes | Cursor | Copilot | All."), *Client);
		return;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Vibe3D"));
	const FString Version = Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : TEXT("?");
	const FString BeginMarker = FString::Printf(TEXT("<!-- BEGIN Vibe3D (v%s) - generated by Vibe3D.GenerateAgentConfig; re-run to refresh -->"), *Version);
	const FString EndMarker = TEXT("<!-- END Vibe3D -->");
	const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	for (const TPair<FString, bool>& Target : Targets)
	{
		const FString& FileName = Target.Key;
		const bool bUseImport = bImportRequested && Target.Value;
		if (bImportRequested && !Target.Value)
		{
			Ar.Logf(TEXT("Vibe3D.GenerateAgentConfig: %s does not resolve @-imports - copying the guide in instead."), *FileName);
		}
		FString Body = bUseImport ? FString::Printf(TEXT("@%s\n"), *SamplePath) : SampleContent;
		if (!Body.EndsWith(TEXT("\n")))
		{
			Body += TEXT("\n");
		}
		const FString Block = BeginMarker + TEXT("\n") + Body + EndMarker + TEXT("\n");
		const FString FullPath = FPaths::Combine(ProjectRoot, FileName);

		FString NewContent;
		FString Existing;
		if (FFileHelper::LoadFileToString(Existing, *FullPath))
		{
			const int32 BeginIdx = Existing.Find(TEXT("<!-- BEGIN Vibe3D"), ESearchCase::IgnoreCase, ESearchDir::FromStart);
			if (BeginIdx != INDEX_NONE)
			{
				int32 EndIdx = Existing.Find(*EndMarker, ESearchCase::IgnoreCase, ESearchDir::FromStart, BeginIdx);
				if (EndIdx != INDEX_NONE)
				{
					EndIdx += EndMarker.Len();
					if (EndIdx < Existing.Len() && Existing[EndIdx] == TEXT('\n'))
					{
						++EndIdx;   // swallow one trailing newline so repeated runs don't accrete blank lines
					}
					NewContent = Existing.Left(BeginIdx) + Block + Existing.RightChop(EndIdx);
				}
				else
				{
					NewContent = Existing.Left(BeginIdx) + Block;   // begin marker without end marker: replace to EOF
				}
				Ar.Logf(TEXT("Vibe3D.GenerateAgentConfig: refreshed Vibe3D block in %s"), *FullPath);
			}
			else
			{
				NewContent = Existing;
				if (!NewContent.EndsWith(TEXT("\n")))
				{
					NewContent += TEXT("\n");
				}
				NewContent += TEXT("\n") + Block;
				Ar.Logf(TEXT("Vibe3D.GenerateAgentConfig: appended Vibe3D block to existing %s"), *FullPath);
			}
		}
		else
		{
			NewContent = Block;
			Ar.Logf(TEXT("Vibe3D.GenerateAgentConfig: created %s (%s)"), *FullPath, bUseImport ? TEXT("import") : TEXT("copy"));
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), /*Tree=*/true);
		if (!FFileHelper::SaveStringToFile(NewContent, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Ar.Logf(TEXT("Vibe3D.GenerateAgentConfig: ERROR - failed to write %s"), *FullPath);
		}
	}
	Ar.Logf(TEXT("Vibe3D.GenerateAgentConfig: done (source: %s)."), *SamplePath);
}

static FAutoConsoleCommandWithArgsAndOutputDevice Vibe3DGenerateAgentConfigCommand(
	TEXT("Vibe3D.GenerateAgentConfig"),
	TEXT("Write the Vibe3D agent guide into the project's agent files from the bundled sample. ")
	TEXT("Usage: Vibe3D.GenerateAgentConfig [ClaudeCode|Gemini|Codex|Hermes|Cursor|Copilot|All] [import]. ")
	TEXT("Default All -> CLAUDE.md + GEMINI.md + AGENTS.md. 'import' writes a one-line @import for Claude/Gemini (others copy)."),
	FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(GenerateVibe3DAgentConfig)
);

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
