#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/DataAsset.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "UnrealMcpAutomationTools.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpPieSmokeTools.h"
#include "UObject/Package.h"

namespace
{
	FString MakePieSmokeTestRunId(const FString& Suffix)
	{
		return FString::Printf(TEXT("20260101T010000Z-%s"), *Suffix);
	}

	FString GetPieSmokeStructuredString(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		FString Value;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	UObject* CreateNonWorldAssetForPieSmokeValidation()
	{
		const FString PackageName = TEXT("/Game/__UEvolvePieSmokeTests/PieSmokeNotWorld");
		const FString AssetName = TEXT("PieSmokeNotWorld");
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}

		UObject* Asset = NewObject<UDataAsset>(Package, UDataAsset::StaticClass(), *AssetName, RF_Public | RF_Standalone);
		if (Asset)
		{
			FAssetRegistryModule::AssetCreated(Asset);
			Package->SetDirtyFlag(false);
		}
		return Asset;
	}

	void DestroyNonWorldAssetForPieSmokeValidation(UObject* Asset)
	{
		if (!Asset)
		{
			return;
		}
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().AssetDeleted(Asset);
		Asset->ClearFlags(RF_Standalone);
		if (UPackage* Package = Asset->GetOutermost())
		{
			Package->SetDirtyFlag(false);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpPieSmokeArgumentParsingTest,
	"UnrealMcp.PieSmoke.ArgumentParsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpPieSmokeArgumentParsingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetNumberField(TEXT("timeoutSeconds"), 999);
	Arguments->SetNumberField(TEXT("aliveWindowSeconds"), 30);

	int32 TimeoutSeconds = 0;
	int32 AliveWindowSeconds = 0;
	FString ErrorKind;
	TestTrue(TEXT("max values parse"), UnrealMcp::ParsePieSmokeArgumentsForTests(*Arguments, TimeoutSeconds, AliveWindowSeconds, ErrorKind));
	TestEqual(TEXT("timeout clamps to 300"), TimeoutSeconds, 300);
	TestEqual(TEXT("alive window clamps to 30"), AliveWindowSeconds, 30);

	Arguments->SetNumberField(TEXT("timeoutSeconds"), -5);
	Arguments->SetNumberField(TEXT("aliveWindowSeconds"), -1);
	TestTrue(TEXT("min values parse"), UnrealMcp::ParsePieSmokeArgumentsForTests(*Arguments, TimeoutSeconds, AliveWindowSeconds, ErrorKind));
	TestEqual(TEXT("timeout clamps to 10"), TimeoutSeconds, 10);
	TestEqual(TEXT("alive window clamps to 1"), AliveWindowSeconds, 1);

	Arguments->SetNumberField(TEXT("timeoutSeconds"), 10);
	Arguments->SetNumberField(TEXT("aliveWindowSeconds"), 30);
	TestFalse(TEXT("alive must be less than timeout"), UnrealMcp::ParsePieSmokeArgumentsForTests(*Arguments, TimeoutSeconds, AliveWindowSeconds, ErrorKind));
	TestEqual(TEXT("invalid arguments kind"), ErrorKind, TEXT("InvalidArguments"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpPieSmokeMapValidationTest,
	"UnrealMcp.PieSmoke.MapValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpPieSmokeMapValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FString MatchedMap;
	FString ErrorKind;
	FString Message;
	TestFalse(
		TEXT("non /Game path rejected"),
		UnrealMcp::ValidatePieSmokeMapPathForTests(TEXT("/Engine/Maps/Entry"), MatchedMap, ErrorKind, Message));
	TestEqual(TEXT("non /Game error kind"), ErrorKind, TEXT("InvalidMapPath"));

	TestFalse(
		TEXT("missing /Game map rejected"),
		UnrealMcp::ValidatePieSmokeMapPathForTests(TEXT("/Game/__UEvolvePieSmokeTests/MissingMap"), MatchedMap, ErrorKind, Message));
	TestEqual(TEXT("missing map error kind"), ErrorKind, TEXT("MapNotFound"));

	UObject* NonWorldAsset = CreateNonWorldAssetForPieSmokeValidation();
	TestNotNull(TEXT("non-world asset created"), NonWorldAsset);
	if (NonWorldAsset)
	{
		TestFalse(
			TEXT("non-UWorld asset rejected"),
			UnrealMcp::ValidatePieSmokeMapPathForTests(TEXT("/Game/__UEvolvePieSmokeTests/PieSmokeNotWorld.PieSmokeNotWorld"), MatchedMap, ErrorKind, Message));
		TestEqual(TEXT("non-UWorld error kind"), ErrorKind, TEXT("InvalidMapPath"));
	}
	DestroyNonWorldAssetForPieSmokeValidation(NonWorldAsset);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpPieSmokeLockSymmetryTest,
	"UnrealMcp.PieSmoke.LockSymmetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpPieSmokeLockSymmetryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::ResetPieSmokeToolStateForTests();
	UnrealMcp::ResetAutomationToolStateForTests();

	const FString AutomationRunId = MakePieSmokeTestRunId(TEXT("aaa111"));
	UnrealMcp::SetActiveAutomationRunForTests(
		AutomationRunId,
		UnrealMcp::EUnrealMcpAutomationRunType::Automation,
		TEXT("running"),
		FDateTime::UtcNow(),
		120);

	TSharedPtr<FJsonObject> PieArguments = MakeShared<FJsonObject>();
	PieArguments->SetNumberField(TEXT("timeoutSeconds"), 60);
	PieArguments->SetNumberField(TEXT("aliveWindowSeconds"), 5);
	FUnrealMcpExecutionResult PieResult;
	TestTrue(TEXT("pie_smoke handled"), UnrealMcp::TryExecutePieSmokeTool(TEXT("unreal.pie_smoke"), *PieArguments, PieResult));
	TestTrue(TEXT("pie_smoke rejects automation lock"), PieResult.bIsError);
	TestEqual(TEXT("pie activeRunType"), GetPieSmokeStructuredString(PieResult, TEXT("activeRunType")), TEXT("automation"));
	UnrealMcp::DeleteAutomationRunStateForTests(AutomationRunId);
	UnrealMcp::ResetAutomationToolStateForTests();

	const FString PieRunId = MakePieSmokeTestRunId(TEXT("bbb222"));
	UnrealMcp::SetActiveAutomationRunForTests(
		PieRunId,
		UnrealMcp::EUnrealMcpAutomationRunType::PieSmoke,
		TEXT("running"),
		FDateTime::UtcNow(),
		120);

	TSharedPtr<FJsonObject> AutomationArguments = MakeShared<FJsonObject>();
	AutomationArguments->SetStringField(TEXT("fullName"), TEXT("UnrealMcp.AutomationTools.DoesNotMatter"));
	FUnrealMcpExecutionResult AutomationResult;
	TestTrue(TEXT("automation_run handled"), UnrealMcp::TryExecuteAutomationTool(TEXT("unreal.automation_run"), *AutomationArguments, AutomationResult));
	TestTrue(TEXT("automation_run rejects pie lock"), AutomationResult.bIsError);
	TestEqual(TEXT("automation activeRunType"), GetPieSmokeStructuredString(AutomationResult, TEXT("activeRunType")), TEXT("pie_smoke"));

	UnrealMcp::DeleteAutomationRunStateForTests(PieRunId);
	UnrealMcp::ResetAutomationToolStateForTests();
	UnrealMcp::ResetPieSmokeToolStateForTests();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpPieSmokeDelegateCleanupStaleTest,
	"UnrealMcp.PieSmoke.DelegateCleanupStale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpPieSmokeDelegateCleanupStaleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::ResetPieSmokeToolStateForTests();
	UnrealMcp::ResetAutomationToolStateForTests();

	const FString RunId = MakePieSmokeTestRunId(TEXT("ccc333"));
	UnrealMcp::CreateActivePieSmokeRunWithDelegatesForTests(RunId);
	TestTrue(TEXT("delegates registered"), UnrealMcp::ArePieSmokeDelegatesRegisteredForTests());
	TestTrue(TEXT("stale cleanup handled"), UnrealMcp::MarkActivePieSmokeStaleForTests(RunId, TEXT("hard_timeout")));
	TestFalse(TEXT("delegates unregistered after stale cleanup"), UnrealMcp::ArePieSmokeDelegatesRegisteredForTests());

	UnrealMcp::DeleteAutomationRunStateForTests(RunId);
	UnrealMcp::ResetAutomationToolStateForTests();
	UnrealMcp::ResetPieSmokeToolStateForTests();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpPieSmokeLegacyRunTypeReportTest,
	"UnrealMcp.PieSmoke.LegacyRunTypeReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpPieSmokeLegacyRunTypeReportTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::ResetAutomationToolStateForTests();
	const FString RunId = MakePieSmokeTestRunId(TEXT("ddd444"));
	UnrealMcp::WriteLegacyAutomationRunStateForTests(
		RunId,
		TEXT("completed"),
		TEXT("UnrealMcp.AutomationTools.Legacy"),
		TEXT("UnrealMcp.AutomationTools.Legacy"),
		FDateTime::UtcNow() - FTimespan::FromSeconds(5),
		120);

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("runId"), RunId);
	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("automation_report handled"), UnrealMcp::TryExecuteAutomationTool(TEXT("unreal.automation_report"), *Arguments, Result));
	TestFalse(TEXT("legacy report is readable"), Result.bIsError);
	TestEqual(TEXT("legacy runType defaults automation"), GetPieSmokeStructuredString(Result, TEXT("runType")), TEXT("automation"));

	UnrealMcp::DeleteAutomationRunStateForTests(RunId);
	UnrealMcp::ResetAutomationToolStateForTests();
	return true;
}

#endif
