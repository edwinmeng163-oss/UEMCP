#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UnrealMcpTaskAtlasService.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <unistd.h>
#endif

namespace UnrealMcpTaskAtlasServiceTests
{
	const FString ValidSha = TEXT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

	UnrealMcp::FTaskAtlasStepRef MakeStepRef(const FString& ToolName, const FString& CaptureStatus)
	{
		UnrealMcp::FTaskAtlasStepRef Step;
		Step.ToolName = ToolName;
		Step.EventId = FString::Printf(TEXT("event-%s"), *ToolName.Replace(TEXT("."), TEXT("_")));
		Step.CaptureStatus = CaptureStatus;
		return Step;
	}

	FString TestRoot(const FString& Leaf)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMcpTaskAtlasServiceTests"), Leaf));
	}

	void ResetDirectory(const FString& Root)
	{
		IFileManager::Get().DeleteDirectory(*Root, false, true);
		IFileManager::Get().MakeDirectory(*Root, true);
	}

	bool WriteTextFile(const FString& Path, const FString& Content)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool WriteGeneratedTool(
		const FString& Root,
		const FString& ToolId,
		const FString& CompositeKind = TEXT("preview"),
		const FString& ReplayStatus = TEXT("preview_ready"),
		const FString& CreatedUtc = TEXT("2026-05-31T00:00:00Z"))
	{
		const FString ToolName = FString::Printf(TEXT("user.%s"), *ToolId);
		const FString ToolJson = FString::Printf(
			TEXT("{\n")
			TEXT("  \"name\": \"%s\",\n")
			TEXT("  \"generator\": \"task_atlas_make_composite\",\n")
			TEXT("  \"compositeKind\": \"%s\",\n")
			TEXT("  \"replayStatus\": \"%s\",\n")
			TEXT("  \"createdUtc\": \"%s\",\n")
			TEXT("  \"sourceTaskId\": \"task-fixture\",\n")
			TEXT("  \"pythonHandlerSha256\": \"%s\"\n")
			TEXT("}\n"),
			*ToolName,
			*CompositeKind,
			*ReplayStatus,
			*CreatedUtc,
			*ValidSha);
		return WriteTextFile(FPaths::Combine(Root, ToolId, TEXT("tool.json")), ToolJson);
	}

	bool CreateDirectorySymlink(const FString& Target, const FString& Link)
	{
#if PLATFORM_MAC || PLATFORM_LINUX
		FTCHARToUTF8 TargetUtf8(*Target);
		FTCHARToUTF8 LinkUtf8(*Link);
		return symlink(TargetUtf8.Get(), LinkUtf8.Get()) == 0;
#else
		(void)Target;
		(void)Link;
		return false;
#endif
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceClassifyPreviewReadyTest,
	"UnrealMcp.TaskAtlasService.Classify.PreviewReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceClassifyPreviewReadyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	UnrealMcp::FTaskAtlasModel Task;
	Task.StepRefs.Add(MakeStepRef(TEXT("unreal.editor_status"), TEXT("captured")));
	Task.StepRefs.Add(MakeStepRef(TEXT("unreal.list_maps"), TEXT("captured")));
	Task.StepRefs.Add(MakeStepRef(TEXT("unreal.list_selected_assets"), TEXT("redacted")));

	const UnrealMcp::TaskAtlasService::FEligibilityResult Result = UnrealMcp::TaskAtlasService::ClassifyTask(Task);
	TestTrue(TEXT("preview ready eligibility"), Result.Eligibility == UnrealMcp::TaskAtlasService::EEligibility::PreviewReady);
	TestEqual(TEXT("allow count"), Result.AllowCount, 3);
	TestEqual(TEXT("captured count"), Result.CapturedArgsCount, 3);
	TestEqual(TEXT("blocked first step"), Result.BlockedFirstStep, -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceClassifyPartialTest,
	"UnrealMcp.TaskAtlasService.Classify.Partial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceClassifyPartialTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	UnrealMcp::FTaskAtlasModel Task;
	Task.StepRefs.Add(MakeStepRef(TEXT("unreal.editor_status"), TEXT("captured")));
	Task.StepRefs.Add(MakeStepRef(TEXT("unreal.list_maps"), TEXT("")));
	Task.StepRefs.Add(MakeStepRef(TEXT("unreal.list_selected_assets"), TEXT("redacted")));

	const UnrealMcp::TaskAtlasService::FEligibilityResult Result = UnrealMcp::TaskAtlasService::ClassifyTask(Task);
	TestTrue(TEXT("partial eligibility"), Result.Eligibility == UnrealMcp::TaskAtlasService::EEligibility::Partial);
	TestEqual(TEXT("total count"), Result.TotalStepCount, 3);
	TestEqual(TEXT("captured count"), Result.CapturedArgsCount, 2);
	TestEqual(TEXT("deny count"), Result.DenyCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceClassifySkeletonPreCaptureTest,
	"UnrealMcp.TaskAtlasService.Classify.SkeletonPreCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceClassifySkeletonPreCaptureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UnrealMcp::FTaskAtlasModel Task;
	Task.CriticalPath.Add(TEXT("unreal.editor_status"));
	Task.CriticalPath.Add(TEXT("unreal.list_maps"));
	Task.CriticalPath.Add(TEXT("unreal.list_selected_assets"));

	const UnrealMcp::TaskAtlasService::FEligibilityResult Result = UnrealMcp::TaskAtlasService::ClassifyTask(Task);
	TestTrue(TEXT("skeleton eligibility"), Result.Eligibility == UnrealMcp::TaskAtlasService::EEligibility::SkeletonPreCapture);
	TestEqual(TEXT("total count"), Result.TotalStepCount, 3);
	TestEqual(TEXT("captured count"), Result.CapturedArgsCount, 0);
	TestEqual(TEXT("step one ordinal"), Result.Steps[1].Ordinal, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceClassifyBlockedTest,
	"UnrealMcp.TaskAtlasService.Classify.Blocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceClassifyBlockedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	UnrealMcp::FTaskAtlasModel Task;
	Task.StepRefs.Add(MakeStepRef(TEXT("unreal.editor_status"), TEXT("captured")));
	Task.StepRefs.Add(MakeStepRef(TEXT("unreal.execute_python"), TEXT("captured")));
	Task.StepRefs.Add(MakeStepRef(TEXT("unreal.list_maps"), TEXT("captured")));

	const UnrealMcp::TaskAtlasService::FEligibilityResult Result = UnrealMcp::TaskAtlasService::ClassifyTask(Task);
	TestTrue(TEXT("blocked eligibility"), Result.Eligibility == UnrealMcp::TaskAtlasService::EEligibility::Blocked);
	TestEqual(TEXT("blocked first step"), Result.BlockedFirstStep, 1);
	TestEqual(TEXT("blocked reason"), Result.BlockedFirstReason, TEXT("dangerous_no_dryrun"));
	TestEqual(TEXT("deny count"), Result.DenyCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceListMadeToolsSafeEntryTest,
	"UnrealMcp.TaskAtlasService.ListMadeTools.SafeEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceListMadeToolsSafeEntryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FString Root = TestRoot(TEXT("SafeEntry"));
	ResetDirectory(Root);
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root);
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		IFileManager::Get().DeleteDirectory(*Root, false, true);
	};

	TestTrue(TEXT("fixture writes"), WriteGeneratedTool(Root, TEXT("atlas_test")));
	const TArray<UnrealMcp::TaskAtlasService::FMadeToolEntry> Entries = UnrealMcp::TaskAtlasService::ListMadeTools();
	TestEqual(TEXT("one made tool"), Entries.Num(), 1);
	if (Entries.Num() == 1)
	{
		TestEqual(TEXT("tool name"), Entries[0].ToolName, TEXT("user.atlas_test"));
		TestEqual(TEXT("generator"), Entries[0].Generator, TEXT("task_atlas_make_composite"));
		TestEqual(TEXT("composite kind"), Entries[0].CompositeKind, TEXT("preview"));
		TestEqual(TEXT("replay status"), Entries[0].ReplayStatus, TEXT("preview_ready"));
		TestEqual(TEXT("source task"), Entries[0].SourceTaskId, TEXT("task-fixture"));
		TestFalse(TEXT("not loaded in real user registry"), Entries[0].bLoadedInUserRegistry);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceListMadeToolsStaleEntryTest,
	"UnrealMcp.TaskAtlasService.ListMadeTools.StaleEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceListMadeToolsStaleEntryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FString Root = TestRoot(TEXT("StaleEntry"));
	ResetDirectory(Root);
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root);
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		IFileManager::Get().DeleteDirectory(*Root, false, true);
	};

	const FString StaleJson =
		TEXT("{\"name\":\"user.atlas_stale\",\"generator\":\"task_atlas_make_composite\"}\n");
	TestTrue(TEXT("stale fixture writes"), WriteTextFile(FPaths::Combine(Root, TEXT("atlas_stale/tool.json")), StaleJson));
	const TArray<UnrealMcp::TaskAtlasService::FMadeToolEntry> Entries = UnrealMcp::TaskAtlasService::ListMadeTools();
	TestEqual(TEXT("stale entry skipped"), Entries.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceListMadeToolsUnsafeRejectedTest,
	"UnrealMcp.TaskAtlasService.ListMadeTools.UnsafeRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceListMadeToolsUnsafeRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FString Root = TestRoot(TEXT("UnsafeRejected"));
	const FString OutsideRoot = TestRoot(TEXT("UnsafeRejectedOutside"));
	ResetDirectory(Root);
	ResetDirectory(OutsideRoot);
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root);
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		IFileManager::Get().DeleteDirectory(*Root, false, true);
		IFileManager::Get().DeleteDirectory(*OutsideRoot, false, true);
	};

	TestTrue(TEXT("outside fixture writes"), WriteGeneratedTool(OutsideRoot, TEXT("atlas_escape")));
#if PLATFORM_MAC || PLATFORM_LINUX
	TestTrue(TEXT("symlink fixture creates"), CreateDirectorySymlink(FPaths::Combine(OutsideRoot, TEXT("atlas_escape")), FPaths::Combine(Root, TEXT("atlas_escape"))));
#else
	AddInfo(TEXT("Skipped symlink creation on this platform; scanner still runs against an empty isolated root."));
#endif
	const TArray<UnrealMcp::TaskAtlasService::FMadeToolEntry> Entries = UnrealMcp::TaskAtlasService::ListMadeTools();
	TestEqual(TEXT("unsafe symlink entry skipped"), Entries.Num(), 0);
	return true;
}

#endif
