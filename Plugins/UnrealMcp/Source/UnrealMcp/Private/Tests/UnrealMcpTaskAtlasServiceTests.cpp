#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpTaskAtlasService.h"
#include "UnrealMcpUserToolListVersion.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <unistd.h>
#endif

namespace UnrealMcp::TaskAtlasService
{
	void SetSavedRootDirForTests(const FString& RootDir);
	void ClearSavedRootDirForTests();
	void CorruptNextMakeCompositeStagingShaForTests();
	void RejectNextMakeCompositeReloadForTests();
	void FailNextPromoteRefreshForTests();
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

	const FString Chunk4PassPy = TEXT("def execute(args):\n    return {\"ok\": True, \"dryRun\": args.get(\"dryRun\", False)}\n");
	const FString Chunk4RaisePy = TEXT("def execute(args):\n    raise RuntimeError(\"chunk4 smoke boom\")\n");
	const FString Chunk4ToolPrefix = TEXT("atlas_chunk4_");

	struct FChunk4Fixture
	{
		FString Root;
		FString SavedRoot;
		FString PyToolsRoot;
	};

	FChunk4Fixture SetupChunk4Fixture(const FString& Leaf)
	{
		FChunk4Fixture Fixture;
		Fixture.Root = TestRoot(FPaths::Combine(TEXT("Chunk4"), Leaf));
		Fixture.SavedRoot = FPaths::Combine(Fixture.Root, TEXT("Saved/UnrealMcp"));
		Fixture.PyToolsRoot = FPaths::Combine(Fixture.Root, TEXT("Tools/UnrealMcpPyTools"));
		ResetDirectory(Fixture.Root);
		IFileManager::Get().MakeDirectory(*Fixture.SavedRoot, true);
		IFileManager::Get().MakeDirectory(*Fixture.PyToolsRoot, true);
		UnrealMcp::TaskAtlasService::SetSavedRootDirForTests(Fixture.SavedRoot);
		UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Fixture.PyToolsRoot);
		return Fixture;
	}

	void ClearChunk4Fixture(const FChunk4Fixture& Fixture)
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		UnrealMcp::TaskAtlasService::ClearSavedRootDirForTests();
		IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	}

	FString RegistryUserToolsRoot()
	{
		UnrealMcp::UserRegistry::InitializeUserToolRegistry();
		return UnrealMcp::UserRegistry::GetUserToolsRootDir();
	}

	void ReloadUserRegistryForTests()
	{
		UnrealMcp::UserToolLock::FExclusiveGuard Guard;
		UnrealMcp::UserRegistry::ReloadUserToolRegistry(true);
	}

	bool RegistryHasTool(const FString& ToolName)
	{
		UnrealMcp::UserToolLock::FSharedGuard Guard;
		return UnrealMcp::UserRegistry::FindUserTool(ToolName) != nullptr;
	}

	void DeleteChunk4RegistryTools()
	{
		const FString Root = RegistryUserToolsRoot();
		TArray<FString> DirectoryNames;
		IFileManager::Get().FindFiles(DirectoryNames, *FPaths::Combine(Root, TEXT("*")), false, true);
		for (const FString& DirectoryName : DirectoryNames)
		{
			if (DirectoryName.StartsWith(Chunk4ToolPrefix, ESearchCase::CaseSensitive))
			{
				IFileManager::Get().DeleteDirectory(*FPaths::Combine(Root, DirectoryName), false, true);
			}
		}
	}

	bool WriteGeneratedToolWithPython(
		const FString& Root,
		const FString& ToolId,
		const FString& MainPy,
		const FString& Generator = TEXT("task_atlas_make_composite"),
		const FString& ReplayStatus = TEXT("preview_ready"),
		const FString& SourceTaskId = TEXT("task-fixture"))
	{
		const FString ToolDir = FPaths::Combine(Root, ToolId);
		const FString MainPyPath = FPaths::Combine(ToolDir, TEXT("main.py"));
		const FString ToolJsonPath = FPaths::Combine(ToolDir, TEXT("tool.json"));
		const FString Sha = UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(MainPy).ToLower();

		TSharedPtr<FJsonObject> ToolJson = MakeShared<FJsonObject>();
		ToolJson->SetStringField(TEXT("name"), FString::Printf(TEXT("user.%s"), *ToolId));
		ToolJson->SetStringField(TEXT("title"), TEXT("Chunk4 Generated Tool"));
		ToolJson->SetStringField(TEXT("description"), TEXT("Task Atlas service chunk4 fixture."));
		ToolJson->SetStringField(TEXT("generator"), Generator);
		ToolJson->SetStringField(TEXT("compositeKind"), TEXT("preview"));
		ToolJson->SetStringField(TEXT("replayStatus"), ReplayStatus);
		ToolJson->SetStringField(TEXT("replayEligibility"), TEXT("preview_ready"));
		ToolJson->SetStringField(TEXT("createdUtc"), TEXT("2026-05-31T00:00:00Z"));
		ToolJson->SetStringField(TEXT("sourceTaskId"), SourceTaskId);
		ToolJson->SetStringField(TEXT("pythonHandlerSha256"), Sha);
		ToolJson->SetArrayField(TEXT("importAllowlist"), TArray<TSharedPtr<FJsonValue>>());

		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		InputSchema->SetBoolField(TEXT("additionalProperties"), true);
		ToolJson->SetObjectField(TEXT("inputSchema"), InputSchema);

		TSharedPtr<FJsonObject> SmokeArgs = MakeShared<FJsonObject>();
		SmokeArgs->SetStringField(TEXT("fixture"), ToolId);
		ToolJson->SetObjectField(TEXT("smokeArgs"), SmokeArgs);

		return WriteTextFile(MainPyPath, MainPy)
			&& WriteTextFile(ToolJsonPath, JsonToPrettyString(ToolJson));
	}

	bool ReadToolJsonField(const FString& Root, const FString& ToolId, const FString& FieldName, FString& OutValue)
	{
		OutValue.Reset();
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *FPaths::Combine(Root, ToolId, TEXT("tool.json"))))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid() && Object->TryGetStringField(FieldName, OutValue);
	}

	TArray<UnrealMcp::TaskAtlasService::FUserToolView> Chunk4IntrospectionViews()
	{
		TArray<UnrealMcp::TaskAtlasService::FUserToolView> Filtered;
		for (const UnrealMcp::TaskAtlasService::FUserToolView& View : UnrealMcp::TaskAtlasService::IntrospectUserRegistry())
		{
			if (View.ToolName.StartsWith(FString(TEXT("user.")) + Chunk4ToolPrefix, ESearchCase::CaseSensitive))
			{
				Filtered.Add(View);
			}
		}
		return Filtered;
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

	FUnrealMcpExecutionResult ExecuteMcpTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
	{
		FUnrealMcpModule& Module = FModuleManager::LoadModuleChecked<FUnrealMcpModule>(TEXT("UnrealMcp"));
		return Module.ExecuteToolFromEditorUI(ToolName, *Arguments);
	}

	FString StructuredString(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		FString Value;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	int32 StructuredInt(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		double Value = 0.0;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetNumberField(FieldName, Value);
		}
		return static_cast<int32>(Value);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionInitialNonZeroTest,
	"UnrealMcp.UserToolListVersion.InitialNonZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionInitialNonZeroTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("initial version is non-zero"), UnrealMcp::GetUserToolListVersion() >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionBumpMonotonicTest,
	"UnrealMcp.UserToolListVersion.BumpIncreasesMonotonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionBumpMonotonicTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const uint64 Before = UnrealMcp::GetUserToolListVersion();
	const uint64 Bumped = UnrealMcp::BumpUserToolListVersion();
	TestEqual(TEXT("bump return value"), Bumped, Before + 1);
	TestEqual(TEXT("stored bumped version"), UnrealMcp::GetUserToolListVersion(), Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionReloadDryRunTest,
	"UnrealMcp.UserToolListVersion.ReloadDryRunDoesNotBump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionReloadDryRunTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetBoolField(TEXT("dryRun"), true);
	const uint64 Before = UnrealMcp::GetUserToolListVersion();
	const FUnrealMcpExecutionResult Result = ExecuteMcpTool(TEXT("unreal.mcp_user_registry_reload"), Arguments);
	TestFalse(TEXT("reload dry-run succeeds"), Result.bIsError);
	TestEqual(TEXT("dry-run keeps version"), UnrealMcp::GetUserToolListVersion(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionReloadApplyTest,
	"UnrealMcp.UserToolListVersion.ReloadApplyBumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionReloadApplyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetBoolField(TEXT("dryRun"), false);
	Arguments->SetBoolField(TEXT("acceptChangedHashes"), true);
	const uint64 Before = UnrealMcp::GetUserToolListVersion();
	const FUnrealMcpExecutionResult Result = ExecuteMcpTool(TEXT("unreal.mcp_user_registry_reload"), Arguments);
	TestFalse(TEXT("reload apply succeeds"), Result.bIsError);
	TestEqual(TEXT("apply bumps version once"), UnrealMcp::GetUserToolListVersion(), Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionMakeCompositePreviewReadyTest,
	"UnrealMcp.UserToolListVersion.MakeCompositePreviewReadyBumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionMakeCompositePreviewReadyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FMakeCompositeFixture Fixture = SetupMakeCompositeFixture(TEXT("Version/PreviewReady"));
	ON_SCOPE_EXIT
	{
		ClearMakeCompositeFixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	StepRefs.Add(MakeTaskStepRef(1, TEXT("unreal.list_maps"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("version-preview-task"), TEXT("Version Preview Tool"), StepRefs));

	UnrealMcp::TaskAtlasService::FMakeCompositeRequest Request;
	Request.TaskId = TEXT("version-preview-task");
	const uint64 Before = UnrealMcp::GetUserToolListVersion();
	const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Request);
	TestTrue(TEXT("composite written"), Result.Outcome == UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::CompositeWritten);
	TestEqual(TEXT("make composite bumps version once"), UnrealMcp::GetUserToolListVersion(), Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionMakeCompositeBlockedTest,
	"UnrealMcp.UserToolListVersion.MakeCompositeBlockedDoesNotBump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionMakeCompositeBlockedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FMakeCompositeFixture Fixture = SetupMakeCompositeFixture(TEXT("Version/Blocked"));
	ON_SCOPE_EXIT
	{
		ClearMakeCompositeFixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.execute_python"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("version-blocked-task"), TEXT("Version Blocked Tool"), StepRefs));

	UnrealMcp::TaskAtlasService::FMakeCompositeRequest Request;
	Request.TaskId = TEXT("version-blocked-task");
	const uint64 Before = UnrealMcp::GetUserToolListVersion();
	const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Request);
	TestTrue(TEXT("blocked outcome"), Result.Outcome == UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::Blocked);
	TestEqual(TEXT("blocked keeps version"), UnrealMcp::GetUserToolListVersion(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionDeleteBumpsTest,
	"UnrealMcp.UserToolListVersion.DeleteBumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionDeleteBumpsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("Version/Delete"));
	ON_SCOPE_EXIT
	{
		ClearChunk4Fixture(Fixture);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("version_delete");
	const FString ToolName = FString(TEXT("user.")) + ToolId;
	TestTrue(TEXT("delete fixture writes"), WriteGeneratedToolWithPython(Fixture.PyToolsRoot, ToolId, Chunk4PassPy));
	const uint64 Before = UnrealMcp::GetUserToolListVersion();
	const UnrealMcp::TaskAtlasService::FDeleteMadeToolResult Result = UnrealMcp::TaskAtlasService::DeleteMadeTool(ToolName);
	TestTrue(TEXT("delete outcome"), Result.Outcome == UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::Deleted);
	TestEqual(TEXT("delete bumps version once"), UnrealMcp::GetUserToolListVersion(), Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionSmokeDryRunTest,
	"UnrealMcp.UserToolListVersion.SmokeDryRunDoesNotBump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionSmokeDryRunTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("Version/SmokeDryRun"));
	ON_SCOPE_EXIT
	{
		ClearChunk4Fixture(Fixture);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("version_smoke_dry_run");
	TestTrue(TEXT("smoke dry-run fixture writes"), WriteGeneratedToolWithPython(Fixture.PyToolsRoot, ToolId, Chunk4PassPy));
	UnrealMcp::TaskAtlasService::FSmokeRequest Request;
	Request.ToolName = FString(TEXT("user.")) + ToolId;
	Request.bDryRun = true;
	const uint64 Before = UnrealMcp::GetUserToolListVersion();
	const UnrealMcp::TaskAtlasService::FSmokeResult Result = UnrealMcp::TaskAtlasService::SmokeMadeTool(Request);
	TestTrue(TEXT("dry-run outcome"), Result.Outcome == UnrealMcp::TaskAtlasService::ESmokeOutcome::DryRun);
	TestEqual(TEXT("smoke dry-run keeps version"), UnrealMcp::GetUserToolListVersion(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionSmokeFailedTest,
	"UnrealMcp.UserToolListVersion.SmokeFailedBumps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionSmokeFailedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("Version/SmokeFailed"));
	ON_SCOPE_EXIT
	{
		ClearChunk4Fixture(Fixture);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("version_smoke_failed");
	TestTrue(TEXT("smoke fail fixture writes"), WriteGeneratedToolWithPython(Fixture.PyToolsRoot, ToolId, Chunk4RaisePy));
	const uint64 Before = UnrealMcp::GetUserToolListVersion();
	const UnrealMcp::TaskAtlasService::FSmokeResult Result = UnrealMcp::TaskAtlasService::SmokeMadeTool(FString(TEXT("user.")) + ToolId);
	TestTrue(TEXT("smoke failed"), Result.Outcome == UnrealMcp::TaskAtlasService::ESmokeOutcome::Failed);
	TestEqual(TEXT("smoke failure bumps version once"), UnrealMcp::GetUserToolListVersion(), Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolListVersionSmokePassedTest,
	"UnrealMcp.UserToolListVersion.SmokePassedDoesNotBump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolListVersionSmokePassedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("Version/SmokePassed"));
	const FString PyRoot = RegistryUserToolsRoot();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(PyRoot);
	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		UnrealMcp::TaskAtlasService::ClearSavedRootDirForTests();
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
		IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("version_smoke_passed");
	TestTrue(TEXT("smoke pass fixture writes"), WriteGeneratedToolWithPython(PyRoot, ToolId, Chunk4PassPy));
	ReloadUserRegistryForTests();
	const uint64 Before = UnrealMcp::GetUserToolListVersion();
	const UnrealMcp::TaskAtlasService::FSmokeResult Result = UnrealMcp::TaskAtlasService::SmokeMadeTool(FString(TEXT("user.")) + ToolId);
	TestTrue(TEXT("smoke passed"), Result.Outcome == UnrealMcp::TaskAtlasService::ESmokeOutcome::Passed);
	TestEqual(TEXT("smoke pass keeps version"), UnrealMcp::GetUserToolListVersion(), Before);
	return true;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceDeleteSuccessGeneratedTest,
	"UnrealMcp.TaskAtlasService.Delete.SuccessGenerated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceDeleteSuccessGeneratedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("DeleteSuccessGenerated"));
	const FString PyRoot = RegistryUserToolsRoot();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(PyRoot);
	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		UnrealMcp::TaskAtlasService::ClearSavedRootDirForTests();
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
		IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("delete_success");
	const FString ToolName = FString(TEXT("user.")) + ToolId;
	TestTrue(TEXT("generated registry tool writes"), WriteGeneratedToolWithPython(PyRoot, ToolId, Chunk4PassPy));
	ReloadUserRegistryForTests();
	TestTrue(TEXT("registry loaded before delete"), RegistryHasTool(ToolName));

	const UnrealMcp::TaskAtlasService::FDeleteMadeToolResult Result = UnrealMcp::TaskAtlasService::DeleteMadeTool(ToolName);
	TestTrue(
		TEXT("delete outcome completed"),
		Result.Outcome == UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::Deleted
			|| Result.Outcome == UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::ReloadRejected);
	TestFalse(TEXT("directory removed"), IFileManager::Get().DirectoryExists(*FPaths::Combine(PyRoot, ToolId)));
	TestFalse(TEXT("registry unloaded after delete"), RegistryHasTool(ToolName));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceDeleteRefuseNonGeneratedTest,
	"UnrealMcp.TaskAtlasService.Delete.RefuseNonGenerated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceDeleteRefuseNonGeneratedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("DeleteRefuseNonGenerated"));
	ON_SCOPE_EXIT
	{
		ClearChunk4Fixture(Fixture);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("manual");
	const FString ToolDir = FPaths::Combine(Fixture.PyToolsRoot, ToolId);
	TestTrue(TEXT("manual tool writes"), WriteGeneratedToolWithPython(Fixture.PyToolsRoot, ToolId, Chunk4PassPy, TEXT("manual_fixture")));
	const UnrealMcp::TaskAtlasService::FDeleteMadeToolResult Result = UnrealMcp::TaskAtlasService::DeleteMadeTool(FString(TEXT("user.")) + ToolId);
	TestTrue(TEXT("refused outcome"), Result.Outcome == UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::Refused);
	TestEqual(TEXT("refused error code"), Result.ErrorCode, TEXT("not_task_atlas_generated"));
	TestTrue(TEXT("manual dir preserved"), IFileManager::Get().DirectoryExists(*ToolDir));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceDeleteRefusePathUnsafeTest,
	"UnrealMcp.TaskAtlasService.Delete.RefusePathUnsafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceDeleteRefusePathUnsafeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("DeleteRefusePathUnsafe"));
	const FString OutsideRoot = TestRoot(TEXT("Chunk4/DeleteRefusePathUnsafeOutside"));
	ResetDirectory(OutsideRoot);
	ON_SCOPE_EXIT
	{
		ClearChunk4Fixture(Fixture);
		IFileManager::Get().DeleteDirectory(*OutsideRoot, false, true);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("escape");
	TestTrue(TEXT("outside generated tool writes"), WriteGeneratedToolWithPython(OutsideRoot, ToolId, Chunk4PassPy));
#if PLATFORM_MAC || PLATFORM_LINUX
	TestTrue(TEXT("escape symlink creates"), CreateDirectorySymlink(FPaths::Combine(OutsideRoot, ToolId), FPaths::Combine(Fixture.PyToolsRoot, ToolId)));
	const UnrealMcp::TaskAtlasService::FDeleteMadeToolResult Result = UnrealMcp::TaskAtlasService::DeleteMadeTool(FString(TEXT("user.")) + ToolId);
	TestTrue(TEXT("path refused"), Result.Outcome == UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::Refused);
	TestEqual(TEXT("path error code"), Result.ErrorCode, TEXT("path_unsafe"));
	TestTrue(TEXT("outside dir preserved"), IFileManager::Get().DirectoryExists(*FPaths::Combine(OutsideRoot, ToolId)));
#else
	AddInfo(TEXT("Path-unsafe symlink test skipped on this platform."));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceDeleteStaleEntryTest,
	"UnrealMcp.TaskAtlasService.Delete.StaleEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceDeleteStaleEntryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("DeleteStaleEntry"));
	const FString PyRoot = RegistryUserToolsRoot();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(PyRoot);
	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		UnrealMcp::TaskAtlasService::ClearSavedRootDirForTests();
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
		IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("stale");
	const FString ToolName = FString(TEXT("user.")) + ToolId;
	TestTrue(TEXT("stale registry fixture writes"), WriteGeneratedToolWithPython(PyRoot, ToolId, Chunk4PassPy));
	ReloadUserRegistryForTests();
	TestTrue(TEXT("registry loaded before stale delete"), RegistryHasTool(ToolName));
	IFileManager::Get().DeleteDirectory(*FPaths::Combine(PyRoot, ToolId), false, true);

	const UnrealMcp::TaskAtlasService::FDeleteMadeToolResult Result = UnrealMcp::TaskAtlasService::DeleteMadeTool(ToolName);
	TestTrue(TEXT("stale flag"), Result.bWasStaleEntry);
	TestTrue(
		TEXT("stale outcome"),
		Result.Outcome == UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::NotFound
			|| Result.Outcome == UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::ReloadRejected);
	TestFalse(TEXT("registry removed stale entry"), RegistryHasTool(ToolName));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceSmokeDryRunTest,
	"UnrealMcp.TaskAtlasService.Smoke.DryRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceSmokeDryRunTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("SmokeDryRun"));
	ON_SCOPE_EXIT
	{
		ClearChunk4Fixture(Fixture);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("smoke_dry_run");
	TestTrue(TEXT("dry-run smoke fixture writes"), WriteGeneratedToolWithPython(Fixture.PyToolsRoot, ToolId, Chunk4PassPy));
	UnrealMcp::TaskAtlasService::FSmokeRequest Request;
	Request.ToolName = FString(TEXT("user.")) + ToolId;
	Request.bDryRun = true;
	const UnrealMcp::TaskAtlasService::FSmokeResult Result = UnrealMcp::TaskAtlasService::SmokeMadeTool(Request);
	TestTrue(TEXT("dry-run outcome"), Result.Outcome == UnrealMcp::TaskAtlasService::ESmokeOutcome::DryRun);
	TestTrue(TEXT("dry-run verdict placeholder"), Result.bVerdictMatchesEligibility);
	TestFalse(TEXT("no marker written"), FPaths::FileExists(FPaths::Combine(Fixture.PyToolsRoot, ToolId, TEXT("_failure.marker"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceSmokeRealPassTest,
	"UnrealMcp.TaskAtlasService.Smoke.RealPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceSmokeRealPassTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("SmokeRealPass"));
	const FString PyRoot = RegistryUserToolsRoot();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(PyRoot);
	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		UnrealMcp::TaskAtlasService::ClearSavedRootDirForTests();
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
		IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("smoke_pass");
	TestTrue(TEXT("real-pass smoke fixture writes"), WriteGeneratedToolWithPython(PyRoot, ToolId, Chunk4PassPy));
	ReloadUserRegistryForTests();
	const UnrealMcp::TaskAtlasService::FSmokeResult Result = UnrealMcp::TaskAtlasService::SmokeMadeTool(FString(TEXT("user.")) + ToolId);
	TestTrue(TEXT("smoke passed"), Result.Outcome == UnrealMcp::TaskAtlasService::ESmokeOutcome::Passed);
	TestEqual(TEXT("status unchanged"), Result.ReplayStatusAfter, TEXT("preview_ready"));
	TestTrue(TEXT("verdict matches"), Result.bVerdictMatchesEligibility);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceSmokeRealFailTest,
	"UnrealMcp.TaskAtlasService.Smoke.RealFail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceSmokeRealFailTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("SmokeRealFail"));
	const FString PyRoot = RegistryUserToolsRoot();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(PyRoot);
	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		UnrealMcp::TaskAtlasService::ClearSavedRootDirForTests();
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
		IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("smoke_fail");
	TestTrue(TEXT("real-fail smoke fixture writes"), WriteGeneratedToolWithPython(PyRoot, ToolId, Chunk4RaisePy));
	ReloadUserRegistryForTests();
	const UnrealMcp::TaskAtlasService::FSmokeResult Result = UnrealMcp::TaskAtlasService::SmokeMadeTool(FString(TEXT("user.")) + ToolId);
	TestTrue(TEXT("smoke failed"), Result.Outcome == UnrealMcp::TaskAtlasService::ESmokeOutcome::Failed);
	TestTrue(TEXT("marker written"), FPaths::FileExists(FPaths::Combine(PyRoot, ToolId, TEXT("_failure.marker"))));
	TestTrue(TEXT("diagnostic written"), FPaths::FileExists(Result.FailureDiagnosticPath));
	FString ReplayStatus;
	TestTrue(TEXT("replay status reads"), ReadToolJsonField(PyRoot, ToolId, TEXT("replayStatus"), ReplayStatus));
	TestEqual(TEXT("replay status failed"), ReplayStatus, TEXT("generated_smoke_failed"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServicePromoteDryRunTest,
	"UnrealMcp.TaskAtlasService.Promote.DryRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServicePromoteDryRunTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("PromoteDryRun"));
	ON_SCOPE_EXIT
	{
		ClearChunk4Fixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("promote-dry-run"), TEXT("Promote Dry Run"), StepRefs));
	UnrealMcp::TaskAtlasService::FPromoteToRagRequest Request;
	Request.TaskId = TEXT("promote-dry-run");
	Request.bDryRun = true;
	const UnrealMcp::TaskAtlasService::FPromoteToRagResult Result = UnrealMcp::TaskAtlasService::PromoteToRag(Request);
	TestTrue(TEXT("dry-run outcome"), Result.Outcome == UnrealMcp::TaskAtlasService::EPromoteToRagOutcome::DryRun);
	TestTrue(TEXT("markdown length reported"), Result.MarkdownLength > 0);
	TestFalse(TEXT("dry-run did not write markdown"), FPaths::FileExists(Result.KnowledgeSourcePath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServicePromoteWriteSuccessTest,
	"UnrealMcp.TaskAtlasService.Promote.WriteSuccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServicePromoteWriteSuccessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("PromoteWriteSuccess"));
	ON_SCOPE_EXIT
	{
		ClearChunk4Fixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("promote-write-success"), TEXT("Promote Write Success"), StepRefs));
	const UnrealMcp::TaskAtlasService::FPromoteToRagResult Result = UnrealMcp::TaskAtlasService::PromoteToRag(TEXT("promote-write-success"));
	TestTrue(TEXT("markdown written"), FPaths::FileExists(Result.KnowledgeSourcePath));
	TestTrue(
		TEXT("promote refresh completed or preserved source for manual refresh"),
		Result.Outcome == UnrealMcp::TaskAtlasService::EPromoteToRagOutcome::Promoted
			|| Result.Outcome == UnrealMcp::TaskAtlasService::EPromoteToRagOutcome::RefreshFailed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServicePromoteRefreshFailedTest,
	"UnrealMcp.TaskAtlasService.Promote.RefreshFailed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServicePromoteRefreshFailedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("PromoteRefreshFailed"));
	ON_SCOPE_EXIT
	{
		ClearChunk4Fixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("promote-refresh-failed"), TEXT("Promote Refresh Failed"), StepRefs));
	UnrealMcp::TaskAtlasService::FailNextPromoteRefreshForTests();
	const UnrealMcp::TaskAtlasService::FPromoteToRagResult Result = UnrealMcp::TaskAtlasService::PromoteToRag(TEXT("promote-refresh-failed"));
	TestTrue(TEXT("refresh failed outcome"), Result.Outcome == UnrealMcp::TaskAtlasService::EPromoteToRagOutcome::RefreshFailed);
	TestTrue(TEXT("markdown preserved"), FPaths::FileExists(Result.KnowledgeSourcePath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceIntrospectMultipleToolsTest,
	"UnrealMcp.TaskAtlasService.Introspect.MultipleTools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceIntrospectMultipleToolsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
	};

	const FString PyRoot = RegistryUserToolsRoot();
	TestTrue(TEXT("tool b writes"), WriteGeneratedToolWithPython(PyRoot, Chunk4ToolPrefix + TEXT("b"), Chunk4PassPy));
	TestTrue(TEXT("tool a writes"), WriteGeneratedToolWithPython(PyRoot, Chunk4ToolPrefix + TEXT("a"), Chunk4PassPy));
	ReloadUserRegistryForTests();
	const TArray<UnrealMcp::TaskAtlasService::FUserToolView> Views = Chunk4IntrospectionViews();
	TestEqual(TEXT("two chunk4 tools"), Views.Num(), 2);
	if (Views.Num() == 2)
	{
		TestEqual(TEXT("sorted first"), Views[0].ToolName, FString(TEXT("user.")) + Chunk4ToolPrefix + TEXT("a"));
		TestEqual(TEXT("sorted second"), Views[1].ToolName, FString(TEXT("user.")) + Chunk4ToolPrefix + TEXT("b"));
		TestTrue(TEXT("loaded first"), Views[0].bLoaded);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceIntrospectSanitizedTest,
	"UnrealMcp.TaskAtlasService.Introspect.Sanitized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceIntrospectSanitizedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
	};

	const FString PyRoot = RegistryUserToolsRoot();
	const FString ToolId = Chunk4ToolPrefix + TEXT("sanitized");
	TestTrue(TEXT("sanitized tool writes"), WriteGeneratedToolWithPython(PyRoot, ToolId, Chunk4PassPy));
	ReloadUserRegistryForTests();
	const TArray<UnrealMcp::TaskAtlasService::FUserToolView> Views = Chunk4IntrospectionViews();
	TestEqual(TEXT("one chunk4 tool"), Views.Num(), 1);
	if (Views.Num() == 1)
	{
		const FString HomeDir = FPlatformProcess::UserDir();
		TestFalse(TEXT("scaffold omits home"), !HomeDir.IsEmpty() && Views[0].ScaffoldDir.Contains(HomeDir, ESearchCase::IgnoreCase));
		TestFalse(TEXT("python path omits home"), !HomeDir.IsEmpty() && Views[0].PythonPath.Contains(HomeDir, ESearchCase::IgnoreCase));
		TestFalse(TEXT("no raw args in source kind"), Views[0].SourceKind.Contains(TEXT("fixture"), ESearchCase::IgnoreCase));
		TestEqual(TEXT("source task id"), Views[0].SourceTaskId, TEXT("task-fixture"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasServiceIntrospectEmptyRegistryTest,
	"UnrealMcp.TaskAtlasService.Introspect.EmptyRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasServiceIntrospectEmptyRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	const TArray<UnrealMcp::TaskAtlasService::FUserToolView> Views = Chunk4IntrospectionViews();
	TestEqual(TEXT("no chunk4 tools"), Views.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasMcpMakeDocumentOnlyTest,
	"UnrealMcp.TaskAtlasMcp.MakeViaMcp.DocumentOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasMcpMakeDocumentOnlyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FMakeCompositeFixture Fixture = SetupMakeCompositeFixture(TEXT("Chunk5/MakeDocumentOnly"));
	ON_SCOPE_EXIT
	{
		ClearMakeCompositeFixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	StepRefs.Add(MakeTaskStepRef(1, TEXT("unreal.list_maps"), TEXT("missing")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("mcp-document-only"), TEXT("MCP Document Only"), StepRefs));

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("taskId"), TEXT("mcp-document-only"));
	const FUnrealMcpExecutionResult Result = ExecuteMcpTool(TEXT("unreal.task_atlas_make_composite"), Args);
	TestFalse(TEXT("DocumentOnly is not bIsError"), Result.bIsError);
	TestTrue(TEXT("structured content"), Result.StructuredContent.IsValid());
	TestEqual(TEXT("outcome"), StructuredString(Result, TEXT("outcome")), TEXT("DocumentOnly"));
	TestFalse(TEXT("document path exists"), StructuredString(Result, TEXT("documentPath")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasMcpMakePreviewReadyTest,
	"UnrealMcp.TaskAtlasMcp.MakeViaMcp.PreviewReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasMcpMakePreviewReadyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FMakeCompositeFixture Fixture = SetupMakeCompositeFixture(TEXT("Chunk5/MakePreviewReady"));
	ON_SCOPE_EXIT
	{
		ClearMakeCompositeFixture(Fixture);
	};

	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeTaskStepRef(0, TEXT("unreal.editor_status"), TEXT("captured")));
	StepRefs.Add(MakeTaskStepRef(1, TEXT("unreal.list_maps"), TEXT("captured")));
	TestTrue(TEXT("task fixture writes"), WriteTaskFixture(Fixture.SavedRoot, TEXT("mcp-preview-ready"), TEXT("MCP Preview Ready"), StepRefs));

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("taskId"), TEXT("mcp-preview-ready"));
	const FUnrealMcpExecutionResult Result = ExecuteMcpTool(TEXT("unreal.task_atlas_make_composite"), Args);
	TestFalse(TEXT("CompositeWritten is not bIsError"), Result.bIsError);
	TestTrue(TEXT("structured content"), Result.StructuredContent.IsValid());
	TestEqual(TEXT("outcome"), StructuredString(Result, TEXT("outcome")), TEXT("CompositeWritten"));
	TestFalse(TEXT("generated dir non-empty"), StructuredString(Result, TEXT("generatedDir")).IsEmpty());
	TestTrue(TEXT("generated dir exists"), IFileManager::Get().DirectoryExists(*StructuredString(Result, TEXT("generatedDir"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasMcpDeleteConfirmRequiredTest,
	"UnrealMcp.TaskAtlasMcp.DeleteViaMcp.ConfirmRequired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasMcpDeleteConfirmRequiredTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FChunk4Fixture Fixture = SetupChunk4Fixture(TEXT("Chunk5/DeleteConfirmRequired"));
	const FString PyRoot = RegistryUserToolsRoot();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(PyRoot);
	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		UnrealMcp::TaskAtlasService::ClearSavedRootDirForTests();
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
		IFileManager::Get().DeleteDirectory(*Fixture.Root, false, true);
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("mcp_delete");
	const FString ToolName = FString(TEXT("user.")) + ToolId;
	TestTrue(TEXT("generated registry tool writes"), WriteGeneratedToolWithPython(PyRoot, ToolId, Chunk4PassPy));
	ReloadUserRegistryForTests();
	TestTrue(TEXT("registry loaded before delete"), RegistryHasTool(ToolName));

	TSharedPtr<FJsonObject> RefuseArgs = MakeShared<FJsonObject>();
	RefuseArgs->SetStringField(TEXT("toolName"), ToolName);
	RefuseArgs->SetBoolField(TEXT("confirm"), false);
	const FUnrealMcpExecutionResult Refused = ExecuteMcpTool(TEXT("unreal.task_atlas_delete_made_tool"), RefuseArgs);
	TestFalse(TEXT("confirm refusal is product outcome"), Refused.bIsError);
	TestEqual(TEXT("refused outcome"), StructuredString(Refused, TEXT("outcome")), TEXT("Refused"));
	TestEqual(TEXT("confirm error code"), StructuredString(Refused, TEXT("errorCode")), TEXT("confirm_required"));
	TestTrue(TEXT("directory preserved after refused delete"), IFileManager::Get().DirectoryExists(*FPaths::Combine(PyRoot, ToolId)));

	TSharedPtr<FJsonObject> DeleteArgs = MakeShared<FJsonObject>();
	DeleteArgs->SetStringField(TEXT("toolName"), ToolName);
	DeleteArgs->SetBoolField(TEXT("confirm"), true);
	const FUnrealMcpExecutionResult Deleted = ExecuteMcpTool(TEXT("unreal.task_atlas_delete_made_tool"), DeleteArgs);
	TestFalse(TEXT("delete completes without MCP wrapper error"), Deleted.bIsError);
	TestEqual(TEXT("deleted outcome"), StructuredString(Deleted, TEXT("outcome")), TEXT("Deleted"));
	TestFalse(TEXT("directory removed"), IFileManager::Get().DirectoryExists(*FPaths::Combine(PyRoot, ToolId)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasMcpListIntrospectReadOnlyTest,
	"UnrealMcp.TaskAtlasMcp.ListIntrospectViaMcp.ReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasMcpListIntrospectReadOnlyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FString PyRoot = RegistryUserToolsRoot();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(PyRoot);
	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("mcp_read_only");
	const FString ToolName = FString(TEXT("user.")) + ToolId;
	TestTrue(TEXT("generated registry tool writes"), WriteGeneratedToolWithPython(PyRoot, ToolId, Chunk4PassPy));
	ReloadUserRegistryForTests();
	const FString ToolDir = FPaths::Combine(PyRoot, ToolId);
	TestTrue(TEXT("directory exists before reads"), IFileManager::Get().DirectoryExists(*ToolDir));

	TSharedPtr<FJsonObject> ListArgs = MakeShared<FJsonObject>();
	ListArgs->SetBoolField(TEXT("includeStale"), true);
	ListArgs->SetBoolField(TEXT("includeFailureMarkers"), true);
	const FUnrealMcpExecutionResult Listed = ExecuteMcpTool(TEXT("unreal.task_atlas_list_made_tools"), ListArgs);
	TestFalse(TEXT("list read-only result"), Listed.bIsError);
	TestEqual(TEXT("list action"), StructuredString(Listed, TEXT("action")), TEXT("task_atlas_list_made_tools"));
	TestTrue(TEXT("list includes at least one made tool"), StructuredInt(Listed, TEXT("count")) >= 1);
	TestTrue(TEXT("list reports toolsListVersion"), StructuredInt(Listed, TEXT("toolsListVersion")) >= 1);

	TSharedPtr<FJsonObject> IntrospectArgs = MakeShared<FJsonObject>();
	IntrospectArgs->SetStringField(TEXT("toolName"), ToolName);
	const FUnrealMcpExecutionResult Introspected = ExecuteMcpTool(TEXT("unreal.user_registry_introspect"), IntrospectArgs);
	TestFalse(TEXT("introspect read-only result"), Introspected.bIsError);
	TestEqual(TEXT("introspect action"), StructuredString(Introspected, TEXT("action")), TEXT("user_registry_introspect"));
	TestEqual(TEXT("introspect count"), StructuredInt(Introspected, TEXT("count")), 1);
	TestTrue(TEXT("introspect reports toolsListVersion"), StructuredInt(Introspected, TEXT("toolsListVersion")) >= 1);
	TestTrue(TEXT("directory still exists after reads"), IFileManager::Get().DirectoryExists(*ToolDir));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasMcpSmokeDryRunTest,
	"UnrealMcp.TaskAtlasMcp.SmokeOrPromoteDryRunViaMcp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasMcpSmokeDryRunTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasServiceTests;

	const FString PyRoot = RegistryUserToolsRoot();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(PyRoot);
	DeleteChunk4RegistryTools();
	ReloadUserRegistryForTests();
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		DeleteChunk4RegistryTools();
		ReloadUserRegistryForTests();
	};

	const FString ToolId = Chunk4ToolPrefix + TEXT("mcp_smoke_dry_run");
	const FString ToolName = FString(TEXT("user.")) + ToolId;
	TestTrue(TEXT("generated registry tool writes"), WriteGeneratedToolWithPython(PyRoot, ToolId, Chunk4PassPy));
	ReloadUserRegistryForTests();

	FString ReplayBefore;
	TestTrue(TEXT("read replay before"), ReadToolJsonField(PyRoot, ToolId, TEXT("replayStatus"), ReplayBefore));

	TSharedPtr<FJsonObject> SmokeArgs = MakeShared<FJsonObject>();
	SmokeArgs->SetStringField(TEXT("toolName"), ToolName);
	SmokeArgs->SetBoolField(TEXT("dryRun"), true);
	const FUnrealMcpExecutionResult Smoked = ExecuteMcpTool(TEXT("unreal.task_atlas_smoke_made_tool"), SmokeArgs);
	TestFalse(TEXT("smoke dry-run is not error"), Smoked.bIsError);
	TestEqual(TEXT("dry-run outcome"), StructuredString(Smoked, TEXT("outcome")), TEXT("DryRun"));

	FString ReplayAfter;
	TestTrue(TEXT("read replay after"), ReadToolJsonField(PyRoot, ToolId, TEXT("replayStatus"), ReplayAfter));
	TestEqual(TEXT("dry-run keeps replay status"), ReplayAfter, ReplayBefore);
	return true;
}

#endif
