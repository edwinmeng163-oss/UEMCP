#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpDiagnosticsTools.h"
#include "UnrealMcpModule.h"

namespace
{
	FDateTime GetDiagnosticsTestBaseTime()
	{
		return FDateTime(2026, 5, 19, 12, 0, 0);
	}

	TSharedPtr<FJsonObject> MakeDiagnosticsArguments(
		const TArray<FString>& Classes = TArray<FString>(),
		const FString& Since = FString(),
		int32 Limit = 200)
	{
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		if (!Since.IsEmpty())
		{
			Arguments->SetStringField(TEXT("since"), Since);
		}
		if (!Classes.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> ClassValues;
			for (const FString& ClassName : Classes)
			{
				ClassValues.Add(MakeShared<FJsonValueString>(ClassName));
			}
			Arguments->SetArrayField(TEXT("classes"), ClassValues);
		}
		Arguments->SetNumberField(TEXT("limit"), Limit);
		return Arguments;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetEntries(const FUnrealMcpExecutionResult& Result)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetArrayField(TEXT("entries"), Entries);
		}
		return Entries;
	}

	FString GetEntryString(const FUnrealMcpExecutionResult& Result, int32 EntryIndex, const FString& FieldName)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = GetEntries(Result);
		if (Entries == nullptr || !Entries->IsValidIndex(EntryIndex))
		{
			return FString();
		}

		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!(*Entries)[EntryIndex].IsValid() || !(*Entries)[EntryIndex]->TryGetObject(EntryObject) || EntryObject == nullptr || !EntryObject->IsValid())
		{
			return FString();
		}

		FString Value;
		(*EntryObject)->TryGetStringField(FieldName, Value);
		return Value;
	}

	double GetStructuredNumber(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		double Value = 0.0;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetNumberField(FieldName, Value);
		}
		return Value;
	}

	bool GetStructuredBool(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		bool bValue = false;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetBoolField(FieldName, bValue);
		}
		return bValue;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpDiagnosticsClassifiesKnownCategoryTest,
	"UnrealMcp.DiagnosticsTools.ClassifiesKnownCategory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpDiagnosticsClassifiesKnownCategoryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FDateTime BaseTime = GetDiagnosticsTestBaseTime();
	UnrealMcp::ResetDiagnosticsStateForTests(BaseTime);
	UnrealMcp::AppendDiagnosticLogLineForTests(
		TEXT("Blueprint compiler warning"),
		ELogVerbosity::Warning,
		FName(TEXT("LogKismetCompiler")),
		BaseTime);

	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("editor_diagnostics handled"), UnrealMcp::TryExecuteDiagnosticsTool(TEXT("unreal.editor_diagnostics"), *MakeDiagnosticsArguments({ TEXT("compile") }), Result));
	TestFalse(TEXT("known category query succeeds"), Result.bIsError);
	TestEqual(TEXT("one entry returned"), GetEntries(Result) ? GetEntries(Result)->Num() : 0, 1);
	TestEqual(TEXT("class"), GetEntryString(Result, 0, TEXT("class")), TEXT("compile"));
	TestEqual(TEXT("source"), GetEntryString(Result, 0, TEXT("source")), TEXT("LogKismetCompiler"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpDiagnosticsSeverityFallbackTest,
	"UnrealMcp.DiagnosticsTools.SeverityFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpDiagnosticsSeverityFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FDateTime BaseTime = GetDiagnosticsTestBaseTime();
	UnrealMcp::ResetDiagnosticsStateForTests(BaseTime);
	UnrealMcp::AppendDiagnosticLogLineForTests(
		TEXT("Unknown warning"),
		ELogVerbosity::Warning,
		FName(TEXT("LogUnmappedForDiagnosticsTest")),
		BaseTime);

	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("editor_diagnostics handled"), UnrealMcp::TryExecuteDiagnosticsTool(TEXT("unreal.editor_diagnostics"), *MakeDiagnosticsArguments(), Result));
	TestFalse(TEXT("fallback query succeeds"), Result.bIsError);
	TestEqual(TEXT("severity"), GetEntryString(Result, 0, TEXT("severity")), TEXT("warning"));
	TestEqual(TEXT("class"), GetEntryString(Result, 0, TEXT("class")), TEXT("log_warning"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpDiagnosticsSuggestedMatcherTest,
	"UnrealMcp.DiagnosticsTools.SuggestedMatcher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpDiagnosticsSuggestedMatcherTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FDateTime BaseTime = GetDiagnosticsTestBaseTime();
	UnrealMcp::ResetDiagnosticsStateForTests(BaseTime);
	UnrealMcp::AppendDiagnosticLogLineForTests(
		TEXT("Asset has Package Version: 0 and needs refresh"),
		ELogVerbosity::Error,
		FName(TEXT("LogLinker")),
		BaseTime);

	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("editor_diagnostics handled"), UnrealMcp::TryExecuteDiagnosticsTool(TEXT("unreal.editor_diagnostics"), *MakeDiagnosticsArguments({ TEXT("content") }), Result));
	TestFalse(TEXT("suggested query succeeds"), Result.bIsError);
	TestEqual(
		TEXT("suggested"),
		GetEntryString(Result, 0, TEXT("suggested")),
		TEXT("Open the asset in editor and Save to refresh CustomVersion stamps."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpDiagnosticsFilterSemanticsTest,
	"UnrealMcp.DiagnosticsTools.FilterSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpDiagnosticsFilterSemanticsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FDateTime BaseTime = GetDiagnosticsTestBaseTime();
	UnrealMcp::ResetDiagnosticsStateForTests(BaseTime);
	UnrealMcp::AppendDiagnosticLogLineForTests(TEXT("Old compile warning"), ELogVerbosity::Warning, FName(TEXT("LogKismet")), BaseTime);
	UnrealMcp::AppendDiagnosticLogLineForTests(TEXT("Map warning"), ELogVerbosity::Warning, FName(TEXT("LogMapCheck")), BaseTime + FTimespan::FromMinutes(1));
	UnrealMcp::AppendDiagnosticLogLineForTests(TEXT("Unknown error"), ELogVerbosity::Error, FName(TEXT("LogUnmappedForDiagnosticsTest")), BaseTime + FTimespan::FromMinutes(2));
	UnrealMcp::AppendDiagnosticLogLineForTests(TEXT("New compile warning"), ELogVerbosity::Warning, FName(TEXT("LogCompile")), BaseTime + FTimespan::FromMinutes(3));

	FUnrealMcpExecutionResult Result;
	TestTrue(
		TEXT("editor_diagnostics handled"),
		UnrealMcp::TryExecuteDiagnosticsTool(
			TEXT("unreal.editor_diagnostics"),
			*MakeDiagnosticsArguments({ TEXT("compile"), TEXT("log_error") }, (BaseTime + FTimespan::FromMinutes(1)).ToIso8601(), 1),
			Result));
	TestFalse(TEXT("filtered query succeeds"), Result.bIsError);
	TestEqual(TEXT("matching totalCount"), GetStructuredNumber(Result, TEXT("totalCount")), 2.0);
	TestTrue(TEXT("limit truncates"), GetStructuredBool(Result, TEXT("truncated")));
	TestEqual(TEXT("one entry returned after limit"), GetEntries(Result) ? GetEntries(Result)->Num() : 0, 1);
	TestEqual(TEXT("latest matching class returned"), GetEntryString(Result, 0, TEXT("class")), TEXT("compile"));
	TestEqual(TEXT("latest matching message returned"), GetEntryString(Result, 0, TEXT("message")), TEXT("New compile warning"));
	return true;
}

#endif
