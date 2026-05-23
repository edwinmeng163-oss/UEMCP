#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpSelfExtensionInternal.h"

namespace
{
	const FString RollbackSafetyRefusalMessage = TEXT("Core rollback is legacy/developer-only. Pass `manifestPath` and `force` explicitly to act on a core manifest.");

	FString RollbackSafetyNormalize(const FString& Path)
	{
		FString Normalized = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Normalized);
		FPaths::CollapseRelativeDirectories(Normalized);
		Normalized.RemoveFromEnd(TEXT("/"));
		return Normalized;
	}

	struct FScopedRollbackSafetyTempRoot
	{
		FString Root;

		FScopedRollbackSafetyTempRoot()
		{
			Root = RollbackSafetyNormalize(FPaths::Combine(
				FPlatformProcess::UserTempDir(),
				FString::Printf(TEXT("UnrealMcpRollbackSafety_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			IFileManager::Get().DeleteDirectory(*Root, false, true);
			IFileManager::Get().MakeDirectory(*Root, true);
		}

		~FScopedRollbackSafetyTempRoot()
		{
			IFileManager::Get().DeleteDirectory(*Root, false, true);
		}
	};

	FString UserToolDir(const FString& ProjectRoot, const FString& ToolId)
	{
		return RollbackSafetyNormalize(FPaths::Combine(ProjectRoot, UnrealMcp::Extension::UserPyToolsRelativeRoot, ToolId));
	}

	bool WriteTextFile(const FString& Path, const FString& Text)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString ReadTextFile(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

	bool JsonStringArrayContains(const TArray<TSharedPtr<FJsonValue>>* Values, const FString& Expected)
	{
		if (Values == nullptr)
		{
			return false;
		}

		const FString NormalizedExpected = RollbackSafetyNormalize(Expected);
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Text;
			if (Value.IsValid() && Value->TryGetString(Text) && RollbackSafetyNormalize(Text).Equals(NormalizedExpected, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	TSharedPtr<FJsonObject> MakeReloadResultObject(const FString& RemovedToolName)
	{
		TSharedPtr<FJsonObject> ReloadObject = MakeShared<FJsonObject>();
		ReloadObject->SetStringField(TEXT("action"), TEXT("mcp_user_registry_reload"));
		TArray<FString> RemovedTools;
		RemovedTools.Add(RemovedToolName);
		ReloadObject->SetArrayField(TEXT("removed"), UnrealMcp::MakeJsonStringArray(RemovedTools));
		return ReloadObject;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpRollbackSafety_UserToolRollbackBoundedTest,
	"UnrealMcp.RollbackSafety.UserToolRollbackBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpRollbackSafety_UserToolRollbackBoundedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FScopedRollbackSafetyTempRoot TempRoot;
	const FString ToolId = TEXT("rollback_temp_tool");
	const FString ToolName = FString::Printf(TEXT("user.%s"), *ToolId);
	const FString ToolDir = UserToolDir(TempRoot.Root, ToolId);
	const FString SiblingDir = UserToolDir(TempRoot.Root, TEXT("sibling_tool"));
	const FString CoreFile = RollbackSafetyNormalize(FPaths::Combine(
		TempRoot.Root,
		TEXT("Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolRegistrar.cpp")));

	TestTrue(TEXT("write tool main.py"), WriteTextFile(FPaths::Combine(ToolDir, TEXT("main.py")), TEXT("def execute(args):\n    return {}\n")));
	TestTrue(TEXT("write tool json"), WriteTextFile(FPaths::Combine(ToolDir, TEXT("tool.json")), TEXT("{}\n")));
	TestTrue(TEXT("write sibling file"), WriteTextFile(FPaths::Combine(SiblingDir, TEXT("main.py")), TEXT("# sibling\n")));
	TestTrue(TEXT("write temp core file"), WriteTextFile(CoreFile, TEXT("core remains\n")));

	bool bDryRunReloadCalled = false;
	FUnrealMcpExecutionResult DryRunResult = UnrealMcp::RollbackUserToolForProjectRoot(
		TempRoot.Root,
		ToolName,
		true,
		[&bDryRunReloadCalled]() -> FUnrealMcpExecutionResult
		{
			bDryRunReloadCalled = true;
			return UnrealMcp::MakeExecutionResult(TEXT("unexpected reload"), MakeShared<FJsonObject>(), false);
		});

	TestFalse(TEXT("dry run succeeds"), DryRunResult.bIsError);
	TestFalse(TEXT("dry run does not reload"), bDryRunReloadCalled);
	TestTrue(TEXT("dry run keeps tool dir"), IFileManager::Get().DirectoryExists(*ToolDir));
	TestTrue(TEXT("dry run keeps core file"), FPaths::FileExists(CoreFile));
	TestTrue(TEXT("dry run structured content"), DryRunResult.StructuredContent.IsValid());
	if (DryRunResult.StructuredContent.IsValid())
	{
		TestEqual(TEXT("dry run boundary dir"), RollbackSafetyNormalize(DryRunResult.StructuredContent->GetStringField(TEXT("deletionBoundaryDir"))), ToolDir);
		const TArray<TSharedPtr<FJsonValue>>* DeletionTargets = nullptr;
		TestTrue(TEXT("deletion targets present"), DryRunResult.StructuredContent->TryGetArrayField(TEXT("deletionTargets"), DeletionTargets));
		TestTrue(TEXT("deletion targets include tool dir"), JsonStringArrayContains(DeletionTargets, ToolDir));
		TestTrue(TEXT("deletion targets include main.py"), JsonStringArrayContains(DeletionTargets, FPaths::Combine(ToolDir, TEXT("main.py"))));
		TestTrue(TEXT("dry run recommends reload"), DryRunResult.StructuredContent->GetBoolField(TEXT("registryReloadRecommended")));
	}

	bool bReloadCalled = false;
	FUnrealMcpExecutionResult ApplyResult = UnrealMcp::RollbackUserToolForProjectRoot(
		TempRoot.Root,
		ToolDir,
		false,
		[&bReloadCalled, &ToolDir, &ToolName]() -> FUnrealMcpExecutionResult
		{
			bReloadCalled = true;
			TSharedPtr<FJsonObject> ReloadObject = MakeReloadResultObject(ToolName);
			ReloadObject->SetBoolField(TEXT("toolDirectoryGoneAtReload"), !IFileManager::Get().DirectoryExists(*ToolDir));
			return UnrealMcp::MakeExecutionResult(TEXT("Reloaded temp registry."), ReloadObject, false);
		});

	TestFalse(TEXT("apply succeeds"), ApplyResult.bIsError);
	TestTrue(TEXT("apply reload called"), bReloadCalled);
	TestFalse(TEXT("tool dir deleted"), IFileManager::Get().DirectoryExists(*ToolDir));
	TestTrue(TEXT("sibling dir remains"), IFileManager::Get().DirectoryExists(*SiblingDir));
	TestEqual(TEXT("core file untouched"), ReadTextFile(CoreFile), FString(TEXT("core remains\n")));
	TestTrue(TEXT("apply structured content"), ApplyResult.StructuredContent.IsValid());
	if (ApplyResult.StructuredContent.IsValid())
	{
		TestTrue(TEXT("apply attempted reload"), ApplyResult.StructuredContent->GetBoolField(TEXT("registryReloadAttempted")));
		const TSharedPtr<FJsonObject>* ReloadObject = nullptr;
		TestTrue(TEXT("reload object present"), ApplyResult.StructuredContent->TryGetObjectField(TEXT("registryReload"), ReloadObject));
		if (ReloadObject != nullptr && ReloadObject->IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* RemovedTools = nullptr;
			TestTrue(TEXT("reload removed array present"), (*ReloadObject)->TryGetArrayField(TEXT("removed"), RemovedTools));
			TestTrue(TEXT("reload drops tool"), JsonStringArrayContains(RemovedTools, ToolName));
			TestTrue(TEXT("reload saw deleted dir"), (*ReloadObject)->GetBoolField(TEXT("toolDirectoryGoneAtReload")));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpRollbackSafety_StaleCoreDefaultRefusalTest,
	"UnrealMcp.RollbackSafety.StaleCoreDefaultRefusal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpRollbackSafety_StaleCoreDefaultRefusalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FScopedRollbackSafetyTempRoot TempRoot;
	const FString PluginSourceRoot = RollbackSafetyNormalize(FPaths::Combine(TempRoot.Root, TEXT("Plugins/UnrealMcp/Source")));
	const FString CoreFile = RollbackSafetyNormalize(FPaths::Combine(PluginSourceRoot, TEXT("UnrealMcp/Private/UnrealMcpEditorTools.cpp")));
	TestTrue(TEXT("write temp core file"), WriteTextFile(CoreFile, TEXT("current core\n")));

	TSharedPtr<FJsonObject> FileObject = MakeShared<FJsonObject>();
	FileObject->SetStringField(TEXT("sourcePath"), CoreFile);
	FileObject->SetStringField(TEXT("backupPath"), FPaths::Combine(TempRoot.Root, TEXT("Saved/Backup/UnrealMcpEditorTools.cpp")));
	FileObject->SetStringField(TEXT("hashAfter"), TEXT("not_the_current_hash"));
	FileObject->SetStringField(TEXT("hashBefore"), TEXT("before_hash"));

	TSharedPtr<FJsonObject> ManifestObject = MakeShared<FJsonObject>();
	ManifestObject->SetStringField(TEXT("toolName"), TEXT("unreal.stale_core_tool"));
	TArray<TSharedPtr<FJsonValue>> ManifestFiles;
	ManifestFiles.Add(MakeShared<FJsonValueObject>(FileObject));
	ManifestObject->SetArrayField(TEXT("files"), ManifestFiles);

	UnrealMcp::FRollbackManifestSafety Safety;
	FString FailureReason;
	TestTrue(
		TEXT("safety evaluation succeeds"),
		UnrealMcp::EvaluateRollbackManifestSafetyForProjectRoot(TempRoot.Root, PluginSourceRoot, ManifestObject, false, false, Safety, FailureReason));
	TestTrue(TEXT("manifest classified core"), Safety.bIsCoreManifest);
	TestTrue(TEXT("core source touched"), Safety.bTouchesCoreFiles);
	TestTrue(TEXT("drift detected"), Safety.bManifestDriftDetected);
	TestTrue(TEXT("default mode refused"), Safety.bRefuseCoreRollback);
	TestEqual(TEXT("refusal message"), Safety.RefusalReason, RollbackSafetyRefusalMessage);
	TestTrue(TEXT("core file still exists"), FPaths::FileExists(CoreFile));
	TestEqual(TEXT("core file not modified"), ReadTextFile(CoreFile), FString(TEXT("current core\n")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpRollbackSafety_UserToolPathEscapeRejectedTest,
	"UnrealMcp.RollbackSafety.UserToolPathEscapeRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpRollbackSafety_UserToolPathEscapeRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FScopedRollbackSafetyTempRoot TempRoot;
	const FString OutsideDir = RollbackSafetyNormalize(FPaths::Combine(
		FPlatformProcess::UserTempDir(),
		FString::Printf(TEXT("UnrealMcpRollbackOutside_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	IFileManager::Get().MakeDirectory(*OutsideDir, true);

	UnrealMcp::FUserToolRollbackPlan Plan;
	FString FailureReason;
	TestFalse(
		TEXT("relative traversal rejected"),
		UnrealMcp::BuildUserToolRollbackPlanForProject(TempRoot.Root, TEXT("../outside_tool"), Plan, FailureReason));
	TestTrue(TEXT("relative traversal failure explains boundary"), FailureReason.Contains(TEXT("must resolve")));

	FailureReason.Reset();
	TestFalse(
		TEXT("absolute outside root rejected"),
		UnrealMcp::BuildUserToolRollbackPlanForProject(TempRoot.Root, OutsideDir, Plan, FailureReason));
	TestTrue(TEXT("absolute failure explains boundary"), FailureReason.Contains(TEXT("must resolve")));

	FailureReason.Reset();
	TestFalse(
		TEXT("user root traversal rejected"),
		UnrealMcp::BuildUserToolRollbackPlanForProject(TempRoot.Root, TEXT("Tools/UnrealMcpPyTools/../escape_tool"), Plan, FailureReason));
	TestTrue(TEXT("user root traversal failure explains boundary"), FailureReason.Contains(TEXT("must resolve")));

	IFileManager::Get().DeleteDirectory(*OutsideDir, false, true);
	return true;
}

#endif
