#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
		const TArray<FString>& CriticalPath,
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

	TArray<FString> CriticalPath;
	CriticalPath.Add(TEXT("unreal.editor_status"));
	CriticalPath.Add(TEXT("unreal.list_level_actors"));
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
		CriticalPath,
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
	TestEqual(TEXT("two critical path steps"), StepTools.Num(), 2);
	TestTrue(TEXT("main.py calls editor_status"), MainPy.Contains(TEXT("call_tool(\"unreal.editor_status\"")));
	TestTrue(TEXT("main.py calls list_level_actors"), MainPy.Contains(TEXT("call_tool(\"unreal.list_level_actors\"")));
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
	TestTrue(TEXT("smokeArgs exists"), ToolJson->HasTypedField<EJson::Object>(TEXT("smokeArgs")));
	const TSharedPtr<FJsonObject>* SmokeArgsObject = nullptr;
	if (ToolJson->TryGetObjectField(TEXT("smokeArgs"), SmokeArgsObject) && SmokeArgsObject && (*SmokeArgsObject).IsValid())
	{
		TestTrue(TEXT("smoke step0_args"), (*SmokeArgsObject)->HasTypedField<EJson::Object>(TEXT("step0_args")));
		TestTrue(TEXT("smoke step1_args"), (*SmokeArgsObject)->HasTypedField<EJson::Object>(TEXT("step1_args")));
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
						StepObject->TryGetStringField(TEXT("policy"), Policy);
						TestEqual(TEXT("step policy allow"), Policy, TEXT("allow"));
					}
				}
			}
		}
	}

	DeleteCompositeTool();
	ReloadUserTools(true);
	return true;
}

#endif
