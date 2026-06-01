#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpTaskAtlasService.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <unistd.h>
#endif

namespace UnrealMcp::TaskAtlasService
{
	void SetSavedRootDirForTests(const FString& RootDir);
	void ClearSavedRootDirForTests();
	void CorruptNextMakeCompositeStagingShaForTests();
	void RejectNextMakeCompositeReloadForTests();
}

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

	FString JsonToPrettyString(const TSharedPtr<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	TSharedPtr<FJsonValue> MakeTaskStepRef(int32 Ordinal, const FString& ToolName, const FString& CaptureStatus)
	{
		TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetNumberField(TEXT("ordinal"), Ordinal);
		Step->SetStringField(TEXT("eventId"), FString::Printf(TEXT("event-%d"), Ordinal));
		Step->SetStringField(TEXT("tool"), ToolName);
		Step->SetStringField(TEXT("captureStatus"), CaptureStatus);
		return MakeShared<FJsonValueObject>(Step);
	}

	bool WriteTaskFixture(
		const FString& SavedRoot,
		const FString& TaskId,
		const FString& Label,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs)
	{
		TSharedPtr<FJsonObject> Task = MakeShared<FJsonObject>();
		Task->SetNumberField(TEXT("schemaVersion"), 2.0);
		Task->SetStringField(TEXT("taskId"), TaskId);
		Task->SetStringField(TEXT("label"), Label);
		Task->SetStringField(TEXT("rating"), TEXT("unrated"));
		Task->SetBoolField(TEXT("pinned"), false);
		Task->SetStringField(TEXT("aiSummaryText"), TEXT("Task Atlas service test fixture."));
		Task->SetArrayField(TEXT("stepRefs"), StepRefs);

		TArray<TSharedPtr<FJsonValue>> CriticalPath;
		for (const TSharedPtr<FJsonValue>& StepValue : StepRefs)
		{
			const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
			FString ToolName;
			if (StepObject.IsValid() && StepObject->TryGetStringField(TEXT("tool"), ToolName))
			{
				CriticalPath.Add(MakeShared<FJsonValueString>(ToolName));
			}
		}
		Task->SetArrayField(TEXT("criticalPath"), CriticalPath);

		return WriteTextFile(FPaths::Combine(SavedRoot, TEXT("Tasks"), TaskId + TEXT(".json")), JsonToPrettyString(Task));
	}

	struct FMakeCompositeFixture
	{
		FString Root;
		FString SavedRoot;
		FString PyToolsRoot;
	};

	FMakeCompositeFixture SetupMakeCompositeFixture(const FString& Leaf)
	{
		FMakeCompositeFixture Fixture;
		Fixture.Root = TestRoot(FPaths::Combine(TEXT("Chunk3"), Leaf));
		Fixture.SavedRoot = FPaths::Combine(Fixture.Root, TEXT("Saved/UnrealMcp"));
		Fixture.PyToolsRoot = FPaths::Combine(Fixture.Root, TEXT("Tools/UnrealMcpPyTools"));
		ResetDirectory(Fixture.Root);
		IFileManager::Get().MakeDirectory(*Fixture.SavedRoot, true);
		IFileManager::Get().MakeDirectory(*Fixture.PyToolsRoot, true);
		UnrealMcp::TaskAtlasService::SetSavedRootDirForTests(Fixture.SavedRoot);
		UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Fixture.PyToolsRoot);
		return Fixture;
	}

	void ClearMakeCompositeFixture(const FMakeCompositeFixture& Fixture)
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		UnrealMcp::TaskAtlasService::ClearSavedRootDirForTests();
		IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceMakeCompositeBlockedTest,
	"UnrealMcp.TaskAtlasService.MakeComposite.Blocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceMakeCompositeBlockedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FMakeCompositeFixture Fixture = SetupMakeCompositeFixture(TEXT("Blocked"));
	ON_SCOPE_EXIT
	{
		ClearMakeCompositeFixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	StepRefs.Add(MakeTaskStepRef(1, TEXT("unreal.execute_python"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("blocked-task"), TEXT("Blocked Task"), StepRefs));

	UnrealMcp::TaskAtlasService::FMakeCompositeRequest Request;
	Request.TaskId = TEXT("blocked-task");
	const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Request);
	TestTrue(TEXT("blocked outcome"), Result.Outcome == UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::Blocked);
	TestTrue(TEXT("document path exists"), FPaths::FileExists(Result.DocumentPath));
	TestTrue(TEXT("generated dir empty"), Result.GeneratedDir.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceMakeCompositePreviewReadyTest,
	"UnrealMcp.TaskAtlasService.MakeComposite.PreviewReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceMakeCompositePreviewReadyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FMakeCompositeFixture Fixture = SetupMakeCompositeFixture(TEXT("PreviewReady"));
	ON_SCOPE_EXIT
	{
		ClearMakeCompositeFixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	StepRefs.Add(MakeTaskStepRef(1, TEXT("unreal.list_maps"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("preview-task"), TEXT("Preview Ready Tool"), StepRefs));

	UnrealMcp::TaskAtlasService::FMakeCompositeRequest Request;
	Request.TaskId = TEXT("preview-task");
	const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Request);
	TestTrue(TEXT("composite written"), Result.Outcome == UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::CompositeWritten);
	TestEqual(TEXT("tool name"), Result.ToolName, TEXT("user.atlas_preview_ready_tool"));
	TestTrue(TEXT("generated dir exists"), IFileManager::Get().DirectoryExists(*Result.GeneratedDir));
	TestTrue(TEXT("target tool.json exists"), FPaths::FileExists(FPaths::Combine(Result.GeneratedDir, TEXT("tool.json"))));
	TestTrue(TEXT("target main.py exists"), FPaths::FileExists(FPaths::Combine(Result.GeneratedDir, TEXT("main.py"))));
	TestTrue(TEXT("target README exists"), FPaths::FileExists(FPaths::Combine(Result.GeneratedDir, TEXT("README.md"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceMakeCompositeStagingFailedTest,
	"UnrealMcp.TaskAtlasService.MakeComposite.StagingFailed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceMakeCompositeStagingFailedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FMakeCompositeFixture Fixture = SetupMakeCompositeFixture(TEXT("StagingFailed"));
	ON_SCOPE_EXIT
	{
		ClearMakeCompositeFixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("staging-task"), TEXT("Staging Failure Tool"), StepRefs));
	UnrealMcp::TaskAtlasService::CorruptNextMakeCompositeStagingShaForTests();

	UnrealMcp::TaskAtlasService::FMakeCompositeRequest Request;
	Request.TaskId = TEXT("staging-task");
	const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Request);
	TestTrue(TEXT("staging failed"), Result.Outcome == UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::StagingFailed);
	TestEqual(TEXT("staging sha mismatch"), Result.ErrorCode, TEXT("staging_sha_mismatch"));
	TestFalse(TEXT("target absent"), IFileManager::Get().DirectoryExists(*FPaths::Combine(Fixture.PyToolsRoot, TEXT("atlas_staging_failure_tool"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceMakeCompositeReloadRejectedTest,
	"UnrealMcp.TaskAtlasService.MakeComposite.ReloadRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceMakeCompositeReloadRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FMakeCompositeFixture Fixture = SetupMakeCompositeFixture(TEXT("ReloadRejected"));
	ON_SCOPE_EXIT
	{
		ClearMakeCompositeFixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("reload-task"), TEXT("Reload Reject Tool"), StepRefs));
	UnrealMcp::TaskAtlasService::RejectNextMakeCompositeReloadForTests();

	UnrealMcp::TaskAtlasService::FMakeCompositeRequest Request;
	Request.TaskId = TEXT("reload-task");
	const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Request);
	TestTrue(TEXT("reload rejected"), Result.Outcome == UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::ReloadRejected);
	TestEqual(TEXT("reload error code"), Result.ErrorCode, TEXT("reload_rejected"));
	TestFalse(TEXT("target rolled back"), IFileManager::Get().DirectoryExists(*Result.GeneratedDir));
	TestTrue(TEXT("diagnostic exists"), FPaths::FileExists(Result.FailureDiagnosticPath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceMakeCompositeCollisionTest,
	"UnrealMcp.TaskAtlasService.MakeComposite.Collision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceMakeCompositeCollisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FMakeCompositeFixture Fixture = SetupMakeCompositeFixture(TEXT("Collision"));
	ON_SCOPE_EXIT
	{
		ClearMakeCompositeFixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("collision-task"), TEXT("Collision Tool"), StepRefs));
	const FString ExistingTarget = FPaths::Combine(Fixture.PyToolsRoot, TEXT("atlas_collision_tool"));
	TestTrue(TEXT("existing marker writes"), WriteTextFile(FPaths::Combine(ExistingTarget, TEXT("marker.txt")), TEXT("keep")));

	UnrealMcp::TaskAtlasService::FMakeCompositeRequest Request;
	Request.TaskId = TEXT("collision-task");
	const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Request);
	TestTrue(TEXT("collision skipped"), Result.Outcome == UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::Skipped);
	TestEqual(TEXT("collision error code"), Result.ErrorCode, TEXT("collision_existing_target"));
	TestTrue(TEXT("existing marker preserved"), FPaths::FileExists(FPaths::Combine(ExistingTarget, TEXT("marker.txt"))));
	return true;
}

#endif
