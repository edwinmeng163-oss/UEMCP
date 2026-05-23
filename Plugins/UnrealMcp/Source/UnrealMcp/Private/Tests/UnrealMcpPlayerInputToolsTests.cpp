#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpEditorTools.h"
#include "UnrealMcpModule.h"

namespace
{
	bool GetPlayerInputStructuredBool(const FUnrealMcpExecutionResult& Result, const FString& FieldName, bool& OutValue)
	{
		return Result.StructuredContent.IsValid()
			&& Result.StructuredContent->TryGetBoolField(FieldName, OutValue);
	}

	const TSharedPtr<FJsonObject>* GetPlayerInputStructuredObject(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetObjectField(FieldName, Object);
		}
		return Object;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetPlayerInputStructuredArray(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetArrayField(FieldName, Values);
		}
		return Values;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpConfigurePlayerInputDryRunDiagnosticsTest,
	"UnrealMcp.PlayerInput.ConfigureDryRunDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpConfigurePlayerInputDryRunDiagnosticsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("inputSystem"), TEXT("legacy"));
	Arguments->SetStringField(TEXT("profile"), TEXT("third_person_basic"));

	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("configure_player_input handled"), UnrealMcp::TryExecuteEditorTool(TEXT("unreal.configure_player_input"), *Arguments, Result));
	TestFalse(TEXT("dry run is not an error"), Result.bIsError);
	TestTrue(TEXT("structured content is present"), Result.StructuredContent.IsValid());
	if (!Result.StructuredContent.IsValid())
	{
		return false;
	}

	bool bDryRun = false;
	TestTrue(TEXT("dryRun field exists"), GetPlayerInputStructuredBool(Result, TEXT("dryRun"), bDryRun));
	TestTrue(TEXT("dryRun defaults true"), bDryRun);

	bool bWouldWrite = false;
	TestTrue(TEXT("wouldWrite field exists"), GetPlayerInputStructuredBool(Result, TEXT("wouldWrite"), bWouldWrite));
	TestTrue(TEXT("capability report exists"), GetPlayerInputStructuredObject(Result, TEXT("capability")) != nullptr);
	TestTrue(TEXT("legacy report exists"), GetPlayerInputStructuredObject(Result, TEXT("legacy")) != nullptr);
	TestTrue(TEXT("postcheck report exists"), GetPlayerInputStructuredObject(Result, TEXT("inputPostcheck")) != nullptr);

	const TArray<TSharedPtr<FJsonValue>>* PlannedMappings = GetPlayerInputStructuredArray(Result, TEXT("plannedMappings"));
	TestTrue(TEXT("third-person profile maps five actions/axes"), PlannedMappings && PlannedMappings->Num() == 5);

	const TSharedPtr<FJsonObject>* Postcheck = GetPlayerInputStructuredObject(Result, TEXT("inputPostcheck"));
	if (Postcheck && Postcheck->IsValid())
	{
		bool bMappingsRequested = false;
		TestTrue(TEXT("postcheck records requested mappings"), (*Postcheck)->TryGetBoolField(TEXT("mappingsRequested"), bMappingsRequested));
		TestTrue(TEXT("mappings were requested"), bMappingsRequested);
	}

	return true;
}

#endif
