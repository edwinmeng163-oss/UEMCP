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
#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpScaffoldGenerator.h"
#include "UnrealMcpSelfExtensionInternal.h"

namespace UnrealMcp
{
	bool TryExecuteScaffoldTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);
}

namespace UnrealMcpScaffoldGeneratorTests
{
	void CleanupScaffoldDir(const FString& ToolId)
	{
		const FString Dir = FPaths::Combine(
			FPaths::ProjectDir(),
			::UnrealMcp::Extension::UserPyToolsRelativeRoot,
			ToolId);
		IFileManager::Get().DeleteDirectory(*Dir, false, true);
	}

	FString CppTrackTestRoot()
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/UnrealMcp/ScaffoldGeneratorTests"));
	}

	void CleanupCppTrackTestRoot()
	{
		IFileManager::Get().DeleteDirectory(*CppTrackTestRoot(), false, true);
	}

	TSharedPtr<FJsonObject> LoadJsonObject(const FString& Path)
	{
		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *Path))
		{
			return nullptr;
		}

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	bool IsLowerHexSha256(const FString& Value)
	{
		if (Value.Len() != 64)
		{
			return false;
		}

		for (const TCHAR Character : Value)
		{
			const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
			const bool bLowerHex = Character >= TEXT('a') && Character <= TEXT('f');
			if (!bDigit && !bLowerHex)
			{
				return false;
			}
		}
		return true;
	}

	bool HasReadinessIssueCode(const TSharedPtr<FJsonObject>& Object, const FString& Code)
	{
		const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("readinessIssues"), Issues) || !Issues)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& IssueValue : *Issues)
		{
			if (IssueValue.IsValid() && IssueValue->Type == EJson::Object && IssueValue->AsObject().IsValid())
			{
				FString IssueCode;
				IssueValue->AsObject()->TryGetStringField(TEXT("code"), IssueCode);
				if (IssueCode == Code)
				{
					return true;
				}
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpScaffoldGenerator_PythonGoldenOutputTest,
	"UnrealMcp.ScaffoldGenerator.PythonGoldenOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpScaffoldGenerator_PythonGoldenOutputTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpScaffoldGeneratorTests;

	const FString ToolId = TEXT("test_python_tool");
	CleanupScaffoldDir(ToolId);

	FJsonObject Arguments;
	Arguments.SetStringField(TEXT("description"), TEXT("Test tool"));
	const UnrealMcp::Scaffold::FPythonScaffoldResult Result =
		UnrealMcp::Scaffold::GeneratePythonScaffoldFiles(ToolId, Arguments);

	TestTrue(TEXT("scaffold generation succeeds"), Result.bSuccess);
	TestTrue(TEXT("scaffold dir exists"), IFileManager::Get().DirectoryExists(*Result.ScaffoldDir));

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Result.ScaffoldDir, TEXT("*")), true, false);
	TestEqual(TEXT("exactly 3 files generated"), Files.Num(), 3);
	TestTrue(TEXT("main.py exists"), FPaths::FileExists(Result.MainPyPath));
	TestTrue(TEXT("tool.json exists"), FPaths::FileExists(Result.ToolJsonPath));
	TestTrue(TEXT("README.md exists"), FPaths::FileExists(Result.ReadmePath));

	TArray<FString> PatchFiles;
	IFileManager::Get().FindFiles(PatchFiles, *FPaths::Combine(Result.ScaffoldDir, TEXT("*.patch.cpp")), true, false);
	TestEqual(TEXT("no patch.cpp files generated"), PatchFiles.Num(), 0);

	FString MainPy;
	FFileHelper::LoadFileToString(MainPy, *Result.MainPyPath);
	TestTrue(TEXT("main.py contains execute entry point"), MainPy.Contains(TEXT("def execute(args)")));
	TestFalse(TEXT("main.py substituted TOOL_ID"), MainPy.Contains(TEXT("{{TOOL_ID}}")));
	TestTrue(TEXT("main.py contains substituted tool id"), MainPy.Contains(ToolId));

	const TSharedPtr<FJsonObject> ToolJson = LoadJsonObject(Result.ToolJsonPath);
	TestTrue(TEXT("tool.json parses"), ToolJson.IsValid());
	if (ToolJson.IsValid())
	{
		TestEqual(TEXT("tool.json name"), ToolJson->GetStringField(TEXT("name")), TEXT("user.test_python_tool"));
		TestEqual(TEXT("tool.json implementationTrack"), ToolJson->GetStringField(TEXT("implementationTrack")), TEXT("python"));
		TestTrue(TEXT("tool.json sha256 lower hex"), IsLowerHexSha256(ToolJson->GetStringField(TEXT("pythonHandlerSha256"))));
	}

	FString Readme;
	FFileHelper::LoadFileToString(Readme, *Result.ReadmePath);
	TestTrue(TEXT("README contains user tool name"), Readme.Contains(TEXT("user.test_python_tool")));

	CleanupScaffoldDir(ToolId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpScaffoldGenerator_ToolIdValidationTest,
	"UnrealMcp.ScaffoldGenerator.ToolIdValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpScaffoldGenerator_ToolIdValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpScaffoldGeneratorTests;

	FJsonObject Arguments;
	const TArray<FString> RejectedIds = {
		TEXT("../escape"),
		TEXT("tool/with/slash"),
		TEXT("tool\\with\\backslash"),
		TEXT("C:tool"),
		TEXT(""),
		FString::ChrN(65, TEXT('a')),
		TEXT("tool-with-dash"),
		TEXT("tool with space")
	};

	for (const FString& ToolId : RejectedIds)
	{
		const UnrealMcp::Scaffold::FPythonScaffoldResult Result =
			UnrealMcp::Scaffold::GeneratePythonScaffoldFiles(ToolId, Arguments);
		TestFalse(FString::Printf(TEXT("reject invalid ToolId '%s'"), *ToolId), Result.bSuccess);
	}

	const TArray<FString> AcceptedIds = {
		TEXT("valid_tool_name"),
		TEXT("tool123")
	};
	for (const FString& ToolId : AcceptedIds)
	{
		CleanupScaffoldDir(ToolId);
		const UnrealMcp::Scaffold::FPythonScaffoldResult Result =
			UnrealMcp::Scaffold::GeneratePythonScaffoldFiles(ToolId, Arguments);
		TestTrue(FString::Printf(TEXT("accept valid ToolId '%s'"), *ToolId), Result.bSuccess);
		CleanupScaffoldDir(ToolId);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpScaffoldGenerator_LifecycleStateTest,
	"UnrealMcp.ScaffoldGenerator.LifecycleState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpScaffoldGenerator_LifecycleStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpScaffoldGeneratorTests;

	const FString ToolId = TEXT("test_lifecycle");
	CleanupScaffoldDir(ToolId);

	FJsonObject Arguments;
	const UnrealMcp::Scaffold::FPythonScaffoldResult Scaffold =
		UnrealMcp::Scaffold::GeneratePythonScaffoldFiles(ToolId, Arguments);
	const FUnrealMcpExecutionResult Result = UnrealMcp::Scaffold::BuildScaffoldExecutionResult(Scaffold);

	TestFalse(TEXT("execution result not error"), Result.bIsError);
	TestTrue(TEXT("structured content valid"), Result.StructuredContent.IsValid());
	if (Result.StructuredContent.IsValid())
	{
		const TSharedPtr<FJsonObject>* LifecyclePtr = nullptr;
		TestTrue(TEXT("lifecycle object present"), Result.StructuredContent->TryGetObjectField(TEXT("lifecycle"), LifecyclePtr));
		if (LifecyclePtr && LifecyclePtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& Lifecycle = *LifecyclePtr;
			TestEqual(TEXT("lifecycle.state"), Lifecycle->GetStringField(TEXT("state")), TEXT("draft_scaffolded"));
			TestFalse(TEXT("lifecycle.callableNow"), Lifecycle->GetBoolField(TEXT("callableNow")));
			TestEqual(TEXT("lifecycle.nextRequiredAction"), Lifecycle->GetStringField(TEXT("nextRequiredAction")), TEXT("unreal.mcp_user_registry_reload"));
			TestEqual(TEXT("lifecycle.extensionScope"), Lifecycle->GetStringField(TEXT("extensionScope")), TEXT("user"));
			TestEqual(TEXT("lifecycle.implementationTrack"), Lifecycle->GetStringField(TEXT("implementationTrack")), TEXT("python"));
			TestEqual(TEXT("lifecycle.handlerKind"), Lifecycle->GetStringField(TEXT("handlerKind")), TEXT("python_bridge"));
			TestEqual(TEXT("lifecycle.sourceKind"), Lifecycle->GetStringField(TEXT("sourceKind")), TEXT("user_registry"));

			const TSharedPtr<FJsonObject>* PathsPtr = nullptr;
			TestTrue(TEXT("lifecycle paths present"), Lifecycle->TryGetObjectField(TEXT("paths"), PathsPtr));
			if (PathsPtr && PathsPtr->IsValid())
			{
				TestFalse(TEXT("paths.scaffoldDir non-empty"), (*PathsPtr)->GetStringField(TEXT("scaffoldDir")).IsEmpty());
			}
		}
	}

	CleanupScaffoldDir(ToolId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpScaffoldGenerator_NoOverwriteTest,
	"UnrealMcp.ScaffoldGenerator.NoOverwrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpScaffoldGenerator_NoOverwriteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpScaffoldGeneratorTests;

	const FString ToolId = TEXT("test_no_overwrite");
	CleanupScaffoldDir(ToolId);

	FJsonObject Arguments;
	const UnrealMcp::Scaffold::FPythonScaffoldResult First =
		UnrealMcp::Scaffold::GeneratePythonScaffoldFiles(ToolId, Arguments);
	const UnrealMcp::Scaffold::FPythonScaffoldResult Second =
		UnrealMcp::Scaffold::GeneratePythonScaffoldFiles(ToolId, Arguments);

	TestTrue(TEXT("first generation succeeds"), First.bSuccess);
	TestFalse(TEXT("second generation fails"), Second.bSuccess);
	TestTrue(TEXT("second error mentions exists"), Second.Error.Contains(TEXT("exists")));

	CleanupScaffoldDir(ToolId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpScaffoldGenerator_PythonInspectReadinessTest,
	"UnrealMcp.ScaffoldGenerator.PythonInspectReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpScaffoldGenerator_PythonInspectReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpScaffoldGeneratorTests;

	const FString ToolId = TEXT("test_python_inspect");
	CleanupScaffoldDir(ToolId);

	FJsonObject ScaffoldArgs;
	ScaffoldArgs.SetStringField(TEXT("description"), TEXT("Inspect readiness test tool."));
	const UnrealMcp::Scaffold::FPythonScaffoldResult Scaffold =
		UnrealMcp::Scaffold::GeneratePythonScaffoldFiles(ToolId, ScaffoldArgs);
	TestTrue(TEXT("python scaffold generation succeeds"), Scaffold.bSuccess);

	FJsonObject InspectArgs;
	InspectArgs.SetStringField(TEXT("scaffoldDir"), FPaths::Combine(::UnrealMcp::Extension::UserPyToolsRelativeRoot, ToolId));
	const FUnrealMcpExecutionResult InspectResult = UnrealMcp::InspectMcpScaffold(InspectArgs, TArray<TSharedPtr<FJsonValue>>());
	TestFalse(TEXT("inspect succeeds"), InspectResult.bIsError);
	TestTrue(TEXT("inspect structured content valid"), InspectResult.StructuredContent.IsValid());
	if (InspectResult.StructuredContent.IsValid())
	{
		TestEqual(TEXT("implementationTrack"), InspectResult.StructuredContent->GetStringField(TEXT("implementationTrack")), TEXT("python"));
		TestEqual(TEXT("toolName from tool.json"), InspectResult.StructuredContent->GetStringField(TEXT("toolName")), TEXT("user.test_python_inspect"));
		TestTrue(TEXT("readyForReload true"), InspectResult.StructuredContent->GetBoolField(TEXT("readyForReload")));
		TestFalse(TEXT("readyForApply false"), InspectResult.StructuredContent->GetBoolField(TEXT("readyForApply")));
		TestTrue(TEXT("next action reload"), InspectResult.StructuredContent->GetStringField(TEXT("nextAction")).Contains(TEXT("unreal.mcp_user_registry_reload")));
		TestTrue(TEXT("next action smoke"), InspectResult.StructuredContent->GetStringField(TEXT("nextAction")).Contains(TEXT("unreal.mcp_user_tool_smoke")));
		TestFalse(TEXT("missing patch not reported"), HasReadinessIssueCode(InspectResult.StructuredContent, TEXT("missing_patch_file")));
		const TSharedPtr<FJsonObject>* GroupedIssues = nullptr;
		TestTrue(TEXT("grouped readiness issues present"), InspectResult.StructuredContent->TryGetObjectField(TEXT("readinessIssuesByTrack"), GroupedIssues) && GroupedIssues && (*GroupedIssues).IsValid());
	}

	CleanupScaffoldDir(ToolId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpScaffoldGenerator_CppTrackUnchangedTest,
	"UnrealMcp.ScaffoldGenerator.CppTrackUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpScaffoldGenerator_CppTrackUnchangedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpScaffoldGeneratorTests;

	CleanupCppTrackTestRoot();

	FJsonObject Arguments;
	Arguments.SetStringField(TEXT("implementationTrack"), TEXT("cpp"));
	Arguments.SetStringField(TEXT("toolName"), TEXT("unreal.test_cpp_track"));
	Arguments.SetStringField(TEXT("title"), TEXT("Test Cpp Track"));
	Arguments.SetStringField(TEXT("description"), TEXT("Legacy C++ scaffold track test."));
	Arguments.SetStringField(TEXT("outputRoot"), TEXT("Saved/UnrealMcp/ScaffoldGeneratorTests"));
	Arguments.SetBoolField(TEXT("includeChatCommandSnippet"), false);

	FUnrealMcpExecutionResult Result;
	TestTrue(TEXT("scaffold tool handled"), UnrealMcp::TryExecuteScaffoldTool(TEXT("unreal.scaffold_mcp_tool"), Arguments, Result));
	TestFalse(TEXT("cpp track result not error"), Result.bIsError);

	const FString ToolDir = FPaths::Combine(CppTrackTestRoot(), TEXT("test_cpp_track"));
	TestTrue(TEXT("legacy ToolRegistrar patch exists"), FPaths::FileExists(FPaths::Combine(ToolDir, TEXT("ToolRegistrar.patch.cpp"))));
	TestTrue(TEXT("legacy ToolRegistrarCall patch exists"), FPaths::FileExists(FPaths::Combine(ToolDir, TEXT("ToolRegistrarCall.patch.cpp"))));
	TestTrue(TEXT("legacy CategoryHandlerFunction patch exists"), FPaths::FileExists(FPaths::Combine(ToolDir, TEXT("CategoryHandlerFunction.patch.cpp"))));
	TestTrue(TEXT("legacy CategoryDispatcherBranch patch exists"), FPaths::FileExists(FPaths::Combine(ToolDir, TEXT("CategoryDispatcherBranch.patch.cpp"))));

	CleanupCppTrackTestRoot();
	return true;
}

#endif
