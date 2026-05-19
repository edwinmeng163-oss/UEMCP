#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpAutomationTools.h"
#include "UnrealMcpModule.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAutomationToolsInternalPassTest,
	"UnrealMcp.AutomationTools.InternalPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAutomationToolsInternalPassTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	return true;
}

namespace
{
	const FString InternalPassTestName = TEXT("UnrealMcp.AutomationTools.InternalPass");

	FString MakeTestRunId(const FString& Suffix)
	{
		return FString::Printf(TEXT("20260101T000000Z-%s"), *Suffix);
	}

	FString GetStructuredString(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		FString Value;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(FieldName, Value);
		}
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAutomationRunRejectsActiveTest,
	"UnrealMcp.AutomationTools.RunRejectsActive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAutomationRunRejectsActiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::ResetAutomationToolStateForTests();
	const FString ActiveRunId = MakeTestRunId(TEXT("aaa001"));
	UnrealMcp::WriteAutomationRunStateForTests(
		ActiveRunId,
		TEXT("running"),
		InternalPassTestName,
		InternalPassTestName,
		FDateTime::UtcNow(),
		120);

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("fullName"), TEXT("UnrealMcp.AutomationTools.DoesNotMatter"));

	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("automation_run handled"), UnrealMcp::TryExecuteAutomationTool(TEXT("unreal.automation_run"), *Arguments, Result));
	TestTrue(TEXT("active run rejection is error"), Result.bIsError);
	TestEqual(TEXT("errorKind"), GetStructuredString(Result, TEXT("errorKind")), TEXT("RunAlreadyActive"));
	TestEqual(TEXT("activeRunId"), GetStructuredString(Result, TEXT("activeRunId")), ActiveRunId);

	UnrealMcp::DeleteAutomationRunStateForTests(ActiveRunId);
	UnrealMcp::ResetAutomationToolStateForTests();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAutomationRunRecoversStaleTest,
	"UnrealMcp.AutomationTools.RunRecoversStale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAutomationRunRecoversStaleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::ResetAutomationToolStateForTests();
	const FString StaleRunId = MakeTestRunId(TEXT("bbb002"));
	UnrealMcp::WriteAutomationRunStateForTests(
		StaleRunId,
		TEXT("running"),
		InternalPassTestName,
		InternalPassTestName,
		FDateTime::UtcNow() - FTimespan::FromSeconds(300),
		1);

	UnrealMcp::SetAutomationFrameworkStartSuppressedForTests(true);
	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("fullName"), InternalPassTestName);
	Arguments->SetNumberField(TEXT("timeoutSeconds"), 120);

	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("automation_run handled"), UnrealMcp::TryExecuteAutomationTool(TEXT("unreal.automation_run"), *Arguments, Result));
	TestFalse(TEXT("stale recovery accepts new run"), Result.bIsError);
	const FString NewRunId = GetStructuredString(Result, TEXT("runId"));
	TestTrue(TEXT("new runId assigned"), !NewRunId.IsEmpty() && NewRunId != StaleRunId);

	TSharedPtr<FJsonObject> ReportArguments = MakeShared<FJsonObject>();
	ReportArguments->SetStringField(TEXT("runId"), StaleRunId);
	FUnrealMcpExecutionResult ReportResult;
	TestTrue(TEXT("automation_report handled"), UnrealMcp::TryExecuteAutomationTool(TEXT("unreal.automation_report"), *ReportArguments, ReportResult));
	TestFalse(TEXT("stale report is readable"), ReportResult.bIsError);
	TestEqual(TEXT("stale status"), GetStructuredString(ReportResult, TEXT("status")), TEXT("stale"));

	UnrealMcp::DeleteAutomationRunStateForTests(StaleRunId);
	UnrealMcp::DeleteAutomationRunStateForTests(NewRunId);
	UnrealMcp::ResetAutomationToolStateForTests();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAutomationRunRejectsUnknownFullNameTest,
	"UnrealMcp.AutomationTools.RunRejectsUnknownFullName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAutomationRunRejectsUnknownFullNameTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::ResetAutomationToolStateForTests();

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("fullName"), TEXT("UnrealMcp.AutomationTools.NotRegistered"));

	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("automation_run handled"), UnrealMcp::TryExecuteAutomationTool(TEXT("unreal.automation_run"), *Arguments, Result));
	TestTrue(TEXT("unknown fullName is error"), Result.bIsError);
	TestEqual(TEXT("errorKind"), GetStructuredString(Result, TEXT("errorKind")), TEXT("TestNotFound"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAutomationReportRejectsUnknownRunIdTest,
	"UnrealMcp.AutomationTools.ReportRejectsUnknownRunId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAutomationReportRejectsUnknownRunIdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::ResetAutomationToolStateForTests();
	const FString MissingRunId = MakeTestRunId(TEXT("ccc003"));
	UnrealMcp::DeleteAutomationRunStateForTests(MissingRunId);

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("runId"), MissingRunId);

	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("automation_report handled"), UnrealMcp::TryExecuteAutomationTool(TEXT("unreal.automation_report"), *Arguments, Result));
	TestTrue(TEXT("unknown runId is error"), Result.bIsError);
	TestEqual(TEXT("errorKind"), GetStructuredString(Result, TEXT("errorKind")), TEXT("RunNotFound"));
	return true;
}

#endif
