#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMcpCapturedArgsStore.h"
#include "UnrealMcpCaptureRedaction.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpPythonToolBridge.h"
#include "UnrealMcpToolHandlerRegistry.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

namespace UnrealMcp::TaskAtlasComposite
{
	bool BuildCompositeUserToolFiles(
		const FString& ToolId,
		const FString& Title,
		const FString& Description,
		const FString& TaskId,
		const FString& ReplayEligibility,
		const FString& ReplayUnavailableReason,
		const TArray<FString>& CriticalPath,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs,
		const TSet<FString>& VisibleCoreToolNames,
		FString& OutToolName,
		FString& OutMainPy,
		FString& OutMainPySha256,
		FString& OutToolJson,
		TSharedPtr<FJsonObject>& OutSmokeArgs,
		TArray<FString>& OutStepTools,
		FString& OutFailureReason);
	bool WriteCompositeUserToolFiles(
		const FString& ToolId,
		const FString& MainPy,
		const FString& ToolJson,
		bool bOverwrite,
		FString& OutDirectory,
		FString& OutFailureReason);
}

namespace UnrealMcpTaskAtlasCompositeTests
{
	const FString CompositeToolId = TEXT("codex_task_atlas_composite");
	const FString CompositeToolName = TEXT("user.") + CompositeToolId;

	FString CompositeToolDir()
	{
		UnrealMcp::UserRegistry::InitializeUserToolRegistry();
		return FPaths::Combine(UnrealMcp::UserRegistry::GetUserToolsRootDir(), CompositeToolId);
	}

	void DeleteCompositeTool()
	{
		IFileManager::Get().DeleteDirectory(*CompositeToolDir(), false, true);
	}

	void ReloadUserTools(bool bAcceptChangedHashes)
	{
		UnrealMcp::UserToolLock::FExclusiveGuard Guard;
		UnrealMcp::UserRegistry::ReloadUserToolRegistry(bAcceptChangedHashes);
	}

	bool ParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	TSharedPtr<FJsonObject> MakeObject()
	{
		return MakeShared<FJsonObject>();
	}

	FString MakeTestId(const FString& Prefix)
	{
		return FString::Printf(TEXT("%s-%s"), *Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToLower());
	}

	void DeleteCaptureSession(const FString& SessionId)
	{
		IFileManager::Get().DeleteDirectory(
			*FPaths::ConvertRelativePathToFull(FPaths::Combine(UnrealMcp::CapturedArgsStore::GetCapturedArgsRoot(), SessionId)),
			false,
			true);
	}

	bool WriteCapturedArgsFixture(
		const FString& SessionId,
		const FString& EventId,
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Args,
		FString& OutCaptureRef)
	{
		const UnrealMcp::CaptureRedaction::FRedactionResult Redacted =
			UnrealMcp::CaptureRedaction::SanitizeToolArguments_Pure(ToolName, Args, 4096, 65536);
		FString CaptureSha256;
		return UnrealMcp::CapturedArgsStore::WriteCapturedArgs(
			SessionId,
			EventId,
			ToolName,
			TEXT("2026-05-31T00:00:00Z"),
			Redacted,
			OutCaptureRef,
			CaptureSha256);
	}

	TSharedPtr<FJsonValue> MakeStepRef(
		int32 Ordinal,
		const FString& EventId,
		const FString& ToolName,
		const FString& CaptureStatus,
		const FString& CaptureRef,
		const FString& PolicyClassAtCapture)
	{
		TSharedPtr<FJsonObject> StepRef = MakeShared<FJsonObject>();
		StepRef->SetNumberField(TEXT("ordinal"), Ordinal);
		StepRef->SetStringField(TEXT("eventId"), EventId);
		StepRef->SetStringField(TEXT("sessionId"), TEXT("task-atlas-composite-test"));
		StepRef->SetStringField(TEXT("tool"), ToolName);
		StepRef->SetStringField(TEXT("ts"), TEXT("2026-05-31T00:00:00Z"));
		StepRef->SetBoolField(TEXT("isError"), false);
		StepRef->SetStringField(TEXT("captureStatus"), CaptureStatus);
		if (!CaptureRef.IsEmpty())
		{
			StepRef->SetStringField(TEXT("captureRef"), CaptureRef);
		}
		StepRef->SetStringField(TEXT("policyClassAtCapture"), PolicyClassAtCapture);
		return MakeShared<FJsonValueObject>(StepRef);
	}

	TSet<FString> VisibleCoreToolsForFixture()
	{
		TSet<FString> VisibleTools;
		for (const UnrealMcp::FToolRegistryEntry& Entry : UnrealMcp::GetToolRegistryEntries())
		{
			if (Entry.Exposure == UnrealMcp::EToolExposure::Visible && Entry.Name.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive))
			{
				VisibleTools.Add(Entry.Name);
			}
		}
		return VisibleTools;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasCompositeGenerationTest,
	"UnrealMcp.TaskAtlas.CompositeGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasCompositeGenerationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasCompositeTests;

	DeleteCompositeTool();
	ReloadUserTools(true);
	const FString CaptureSessionId = MakeTestId(TEXT("composite-preview"));
	ON_SCOPE_EXIT
	{
		DeleteCompositeTool();
		DeleteCaptureSession(CaptureSessionId);
		ReloadUserTools(true);
	};

	FString EditorStatusCaptureRef;
	TSharedPtr<FJsonObject> EditorStatusArgs = MakeObject();
	EditorStatusArgs->SetStringField(TEXT("note"), TEXT("TRIPLE_SINGLE ''' TRIPLE_DOUBLE \"\"\"\n# HASH_MARKER"));
	TestTrue(
		TEXT("write editor_status captured args"),
		WriteCapturedArgsFixture(
			CaptureSessionId,
			TEXT("event-step-0"),
			TEXT("unreal.editor_status"),
			EditorStatusArgs,
			EditorStatusCaptureRef));

	FString ReloadCaptureRef;
	TSharedPtr<FJsonObject> ReloadArgs = MakeObject();
	ReloadArgs->SetBoolField(TEXT("acceptChangedHashes"), true);
	TestTrue(
		TEXT("write reload captured args"),
		WriteCapturedArgsFixture(
			CaptureSessionId,
			TEXT("event-step-1"),
			TEXT("unreal.mcp_user_registry_reload"),
			ReloadArgs,
			ReloadCaptureRef));

	TArray<FString> CriticalPath;
	CriticalPath.Add(TEXT("unreal.editor_status"));
	CriticalPath.Add(TEXT("unreal.list_level_actors"));
	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeStepRef(0, TEXT("event-step-0"), TEXT("unreal.editor_status"), TEXT("captured"), EditorStatusCaptureRef, TEXT("allow")));
	StepRefs.Add(MakeStepRef(1, TEXT("event-step-1"), TEXT("unreal.mcp_user_registry_reload"), TEXT("captured"), ReloadCaptureRef, TEXT("force_dry_run")));
	FString ToolName;
	FString MainPy;
	FString MainPySha256;
	FString ToolJsonText;
	TSharedPtr<FJsonObject> SmokeArgs;
	TArray<FString> StepTools;
	FString FailureReason;
	const bool bBuilt = UnrealMcp::TaskAtlasComposite::BuildCompositeUserToolFiles(
		CompositeToolId,
		TEXT("Task Atlas Composite Test"),
		TEXT("Composite skeleton generated from Task Atlas automation fixture."),
		TEXT("task-atlas-composite-test"),
		TEXT("preview_ready"),
		FString(),
		CriticalPath,
		StepRefs,
		VisibleCoreToolsForFixture(),
		ToolName,
		MainPy,
		MainPySha256,
		ToolJsonText,
		SmokeArgs,
		StepTools,
		FailureReason);
	TestTrue(TEXT("composite files build"), bBuilt);
	if (!FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}
	if (!bBuilt)
	{
		DeleteCompositeTool();
		ReloadUserTools(true);
		return false;
	}

	TestEqual(TEXT("user tool name"), ToolName, CompositeToolName);
	TestEqual(TEXT("two stepRef steps"), StepTools.Num(), 2);
	if (StepTools.Num() == 2)
	{
		TestEqual(TEXT("stepRefs preserve first tool"), StepTools[0], TEXT("unreal.editor_status"));
		TestEqual(TEXT("stepRefs preserve second tool"), StepTools[1], TEXT("unreal.mcp_user_registry_reload"));
	}
	TestTrue(TEXT("main.py calls editor_status"), MainPy.Contains(TEXT("call_tool_raw(\"unreal.editor_status\"")));
	TestTrue(TEXT("main.py calls reload"), MainPy.Contains(TEXT("call_tool_raw(\"unreal.mcp_user_registry_reload\"")));
	TestFalse(TEXT("main.py ignores fallback criticalPath when stepRefs exist"), MainPy.Contains(TEXT("call_tool_raw(\"unreal.list_level_actors\"")));
	TestTrue(TEXT("captured defaults are JSON data"), MainPy.Contains(TEXT("_CAPTURED_JSON = ")));
	TestTrue(TEXT("captured defaults parsed by json.loads"), MainPy.Contains(TEXT("_CAPTURED = json.loads(_CAPTURED_JSON)")));
	TestFalse(TEXT("captured data is not raw triple-quoted"), MainPy.Contains(TEXT("json.loads(r'''")) || MainPy.Contains(TEXT("json.loads(r\"\"\"")));
	const int32 MarkerIndex = MainPy.Find(TEXT("TRIPLE_SINGLE"));
	const int32 ExecuteIndex = MainPy.Find(TEXT("def execute"));
	TestTrue(TEXT("injection marker is in the data constant"), MarkerIndex != INDEX_NONE && ExecuteIndex != INDEX_NONE && MarkerIndex < ExecuteIndex);
	if (ExecuteIndex != INDEX_NONE)
	{
		TestFalse(TEXT("injection marker is not in generated logic"), MainPy.Mid(ExecuteIndex).Contains(TEXT("TRIPLE_SINGLE")));
	}
	TestEqual(TEXT("returned sha matches main.py"), MainPySha256, UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(MainPy));

	TSharedPtr<FJsonObject> ToolJson;
	TestTrue(TEXT("tool.json parses"), ParseJsonObject(ToolJsonText, ToolJson));
	if (!ToolJson.IsValid())
	{
		DeleteCompositeTool();
		ReloadUserTools(true);
		return false;
	}

	FString DeclaredSha;
	ToolJson->TryGetStringField(TEXT("pythonHandlerSha256"), DeclaredSha);
	TestEqual(TEXT("tool.json sha matches main.py"), DeclaredSha, MainPySha256);
	FString CompositeKind;
	ToolJson->TryGetStringField(TEXT("compositeKind"), CompositeKind);
	TestEqual(TEXT("preview-ready composite kind"), CompositeKind, TEXT("preview"));
	FString ReplayStatus;
	ToolJson->TryGetStringField(TEXT("replayStatus"), ReplayStatus);
	TestEqual(TEXT("preview-ready replay status"), ReplayStatus, TEXT("preview_only"));
	TestTrue(TEXT("smokeArgs exists"), ToolJson->HasTypedField<EJson::Object>(TEXT("smokeArgs")));
	const TSharedPtr<FJsonObject>* SmokeArgsObject = nullptr;
	if (ToolJson->TryGetObjectField(TEXT("smokeArgs"), SmokeArgsObject) && SmokeArgsObject && (*SmokeArgsObject).IsValid())
	{
		TestFalse(TEXT("preview smoke omits step0 override so captured args are used"), (*SmokeArgsObject)->HasTypedField<EJson::Object>(TEXT("step0_args")));
		TestFalse(TEXT("preview smoke omits step1 override so captured args are used"), (*SmokeArgsObject)->HasTypedField<EJson::Object>(TEXT("step1_args")));
	}
	const TArray<TSharedPtr<FJsonValue>>* ToolJsonStepRefs = nullptr;
	TestTrue(TEXT("tool.json stepRefs present"), ToolJson->TryGetArrayField(TEXT("stepRefs"), ToolJsonStepRefs) && ToolJsonStepRefs);
	if (ToolJsonStepRefs)
	{
		TestEqual(TEXT("tool.json stepRefs count"), ToolJsonStepRefs->Num(), 2);
	}

	const TSharedPtr<FJsonObject>* InputSchema = nullptr;
	TestTrue(TEXT("inputSchema exists"), ToolJson->TryGetObjectField(TEXT("inputSchema"), InputSchema) && InputSchema && (*InputSchema).IsValid());
	if (InputSchema && (*InputSchema).IsValid())
	{
		bool bAdditionalProperties = true;
		(*InputSchema)->TryGetBoolField(TEXT("additionalProperties"), bAdditionalProperties);
		TestFalse(TEXT("inputSchema closed"), bAdditionalProperties);

		const TSharedPtr<FJsonObject>* Properties = nullptr;
		TestTrue(TEXT("properties exists"), (*InputSchema)->TryGetObjectField(TEXT("properties"), Properties) && Properties && (*Properties).IsValid());
		if (Properties && (*Properties).IsValid())
		{
			TestTrue(TEXT("step0_args property"), (*Properties)->HasTypedField<EJson::Object>(TEXT("step0_args")));
			TestTrue(TEXT("step1_args property"), (*Properties)->HasTypedField<EJson::Object>(TEXT("step1_args")));
		}
	}

	FString Directory;
	const bool bWritten = UnrealMcp::TaskAtlasComposite::WriteCompositeUserToolFiles(
		CompositeToolId,
		MainPy,
		ToolJsonText,
		false,
		Directory,
		FailureReason);
	TestTrue(TEXT("composite files write"), bWritten);
	if (!FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}
	if (!bWritten)
	{
		DeleteCompositeTool();
		ReloadUserTools(true);
		return false;
	}

	TArray<uint8> MainPyBytes;
	TestTrue(TEXT("main.py reloads from disk"), FFileHelper::LoadFileToArray(MainPyBytes, *FPaths::Combine(Directory, TEXT("main.py"))));
	TestEqual(TEXT("tool.json sha matches actual main.py bytes"), DeclaredSha, UnrealMcp::HashUtils::Sha256LowerHex(MainPyBytes));

	ReloadUserTools(true);
	const UnrealMcp::FToolHandlerRegistryEntry* HandlerEntry = UnrealMcp::FindToolHandlerRegistryEntry(CompositeToolName);
	TestNotNull(TEXT("composite handler registered"), HandlerEntry);
	if (!HandlerEntry)
	{
		DeleteCompositeTool();
		ReloadUserTools(true);
		return false;
	}

	FJsonObject Arguments;
	if (SmokeArgs.IsValid())
	{
		Arguments.Values = SmokeArgs->Values;
	}
	Arguments.SetBoolField(TEXT("dryRun"), true);

	const FUnrealMcpExecutionResult Result = UnrealMcp::UnrealMcpPythonToolBridge::ExecutePythonRegisteredTool(*HandlerEntry, Arguments);
	if (Result.bIsError
		&& (Result.Text.Contains(TEXT("PythonScriptPlugin"))
			|| Result.Text.Contains(TEXT("Python support is not available"))
			|| Result.Text.Contains(TEXT("Python is not initialized"))))
	{
		AddInfo(TEXT("Skipped smoke: PythonScriptPlugin is unavailable in this automation environment."));
		DeleteCompositeTool();
		ReloadUserTools(true);
		return true;
	}

	TestFalse(TEXT("composite smoke succeeds"), Result.bIsError);
	TestTrue(TEXT("composite structured content present"), Result.StructuredContent.IsValid());
	if (Result.StructuredContent.IsValid())
	{
		const TSharedPtr<FJsonObject>* InnerContent = nullptr;
		TestTrue(
			TEXT("composite structuredContent object"),
			Result.StructuredContent->TryGetObjectField(TEXT("structuredContent"), InnerContent) && InnerContent && (*InnerContent).IsValid());
		if (InnerContent && (*InnerContent).IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
			TestTrue(TEXT("composite steps array"), (*InnerContent)->TryGetArrayField(TEXT("steps"), Steps) && Steps);
			if (Steps)
			{
				TestEqual(TEXT("composite step count"), Steps->Num(), 2);
				for (const TSharedPtr<FJsonValue>& StepValue : *Steps)
				{
					const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
					TestTrue(TEXT("step object valid"), StepObject.IsValid());
					if (StepObject.IsValid())
					{
						FString Policy;
						StepObject->TryGetStringField(TEXT("policyDecision"), Policy);
						FString StepTool;
						StepObject->TryGetStringField(TEXT("tool"), StepTool);
						if (StepTool == TEXT("unreal.editor_status"))
						{
							TestEqual(TEXT("editor_status policy allow"), Policy, TEXT("allow"));
						}
						else if (StepTool == TEXT("unreal.mcp_user_registry_reload"))
						{
							TestEqual(TEXT("reload policy force dry run"), Policy, TEXT("force_dry_run"));
							bool bForcedDryRun = false;
							StepObject->TryGetBoolField(TEXT("forcedDryRun"), bForcedDryRun);
							TestTrue(TEXT("reload step marks forcedDryRun"), bForcedDryRun);
							const TSharedPtr<FJsonObject>* EffectiveArgsDiff = nullptr;
							TestTrue(
								TEXT("reload step reports effective args diff"),
								StepObject->TryGetObjectField(TEXT("effectiveArgsDiff"), EffectiveArgsDiff) && EffectiveArgsDiff && (*EffectiveArgsDiff).IsValid());
							if (EffectiveArgsDiff && (*EffectiveArgsDiff).IsValid())
							{
								const TSharedPtr<FJsonObject>* DryRunDiff = nullptr;
								TestTrue(TEXT("dryRun diff present"), (*EffectiveArgsDiff)->TryGetObjectField(TEXT("dryRun"), DryRunDiff) && DryRunDiff && (*DryRunDiff).IsValid());
							}
						}
					}
				}
			}
		}
	}

	DeleteCompositeTool();
	ReloadUserTools(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasCompositeSkeletonWithoutCaptureTest,
	"UnrealMcp.TaskAtlas.CompositeSkeletonWithoutCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasCompositeSkeletonWithoutCaptureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpTaskAtlasCompositeTests;

	TArray<FString> CriticalPath;
	CriticalPath.Add(TEXT("unreal.list_level_actors"));
	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeStepRef(0, TEXT("event-step-0"), TEXT("unreal.editor_status"), TEXT("skipped"), FString(), TEXT("allow")));
	StepRefs.Add(MakeStepRef(1, TEXT("event-step-1"), TEXT("unreal.mcp_user_registry_reload"), TEXT("skipped"), FString(), TEXT("force_dry_run")));

	FString ToolName;
	FString MainPy;
	FString MainPySha256;
	FString ToolJsonText;
	TSharedPtr<FJsonObject> SmokeArgs;
	TArray<FString> StepTools;
	FString FailureReason;
	const bool bBuilt = UnrealMcp::TaskAtlasComposite::BuildCompositeUserToolFiles(
		CompositeToolId,
		TEXT("Task Atlas Skeleton Test"),
		TEXT("Composite skeleton generated from Task Atlas automation fixture."),
		TEXT("task-atlas-composite-skeleton-test"),
		TEXT("partial"),
		TEXT("step 0 is missing captureRef"),
		CriticalPath,
		StepRefs,
		VisibleCoreToolsForFixture(),
		ToolName,
		MainPy,
		MainPySha256,
		ToolJsonText,
		SmokeArgs,
		StepTools,
		FailureReason);
	TestTrue(TEXT("skeleton files build"), bBuilt);
	if (!FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}
	if (!bBuilt)
	{
		return false;
	}

	TestEqual(TEXT("stepRefs still drive skeleton step order"), StepTools.Num(), 2);
	if (StepTools.Num() == 2)
	{
		TestEqual(TEXT("skeleton first step"), StepTools[0], TEXT("unreal.editor_status"));
		TestEqual(TEXT("skeleton second step"), StepTools[1], TEXT("unreal.mcp_user_registry_reload"));
	}
	TestTrue(TEXT("skeleton uses empty captured data object"), MainPy.Contains(TEXT("_CAPTURED_JSON = \"{}\"")));

	TSharedPtr<FJsonObject> ToolJson;
	TestTrue(TEXT("tool.json parses"), ParseJsonObject(ToolJsonText, ToolJson));
	if (!ToolJson.IsValid())
	{
		return false;
	}

	FString CompositeKind;
	ToolJson->TryGetStringField(TEXT("compositeKind"), CompositeKind);
	TestEqual(TEXT("no captured args keeps skeleton kind"), CompositeKind, TEXT("skeleton"));
	FString ReplayStatus;
	ToolJson->TryGetStringField(TEXT("replayStatus"), ReplayStatus);
	TestEqual(TEXT("partial replay status retained"), ReplayStatus, TEXT("partial"));
	const TSharedPtr<FJsonObject>* SmokeArgsObject = nullptr;
	if (ToolJson->TryGetObjectField(TEXT("smokeArgs"), SmokeArgsObject) && SmokeArgsObject && (*SmokeArgsObject).IsValid())
	{
		TestTrue(TEXT("skeleton smoke step0_args"), (*SmokeArgsObject)->HasTypedField<EJson::Object>(TEXT("step0_args")));
		TestTrue(TEXT("skeleton smoke step1_args"), (*SmokeArgsObject)->HasTypedField<EJson::Object>(TEXT("step1_args")));
	}
	return true;
}

#endif
