#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

namespace UnrealMcp
{
	namespace UnrealMcpUserRegistryReloadTool
	{
		FUnrealMcpExecutionResult Execute(const FJsonObject& Arguments);
	}

	namespace UnrealMcpUserToolSmokeTool
	{
		FUnrealMcpExecutionResult Execute(const FJsonObject& Arguments);
	}
}

namespace
{
	const FString UserRegistryTestPrefix = TEXT("codex_job_b_");
	const FString UserRegistryPassPy = TEXT("def execute(args):\n    return {\"ok\": True, \"dryRun\": args.get(\"dryRun\", False)}\n");
	const FString UserRegistryPassSha = TEXT("c0b99179da60b01efa870ef4bfb9e391ae22a1234bf079fb9fc4e7f28c903de0");
	const FString UserRegistryUpdate1Py = TEXT("def execute(args):\n    return {\"version\": 1, \"dryRun\": args.get(\"dryRun\", False)}\n");
	const FString UserRegistryUpdate1Sha = TEXT("27bc005aeef4baccbbe3d52c14a68a7365dd6ad54fd10c90f9fbe5ffca99e3ae");
	const FString UserRegistryUpdate2Py = TEXT("def execute(args):\n    return {\"version\": 2, \"dryRun\": args.get(\"dryRun\", False)}\n");
	const FString UserRegistryUpdate2Sha = TEXT("a314325928ccfbb2f627e73a83e2fbef399dcca78836fcd237df3f9fd3c23655");
	const FString UserRegistryRaisePy = TEXT("def execute(args):\n    raise RuntimeError(\"smoke boom\")\n");
	const FString UserRegistryRaiseSha = TEXT("48da9d7f1fb4fc7397376439be5ba61b1a8c0f5e1279be0e6c333a3c60e26dcd");

	FString UserRegistryTestRoot()
	{
		UnrealMcp::UserRegistry::InitializeUserToolRegistry();
		return UnrealMcp::UserRegistry::GetUserToolsRootDir();
	}

	FString UserRegistryToolDir(const FString& ToolId)
	{
		return FPaths::Combine(UserRegistryTestRoot(), ToolId);
	}

	FString UserRegistryToolName(const FString& ToolId)
	{
		return FString::Printf(TEXT("user.%s"), *ToolId);
	}

	void UserRegistryDeleteToolDir(const FString& ToolId)
	{
		IFileManager::Get().DeleteDirectory(*UserRegistryToolDir(ToolId), false, true);
	}

	void UserRegistryDeleteTestDirs()
	{
		const FString Root = UserRegistryTestRoot();
		TArray<FString> DirectoryNames;
		IFileManager::Get().FindFiles(DirectoryNames, *FPaths::Combine(Root, TEXT("*")), false, true);
		for (const FString& DirectoryName : DirectoryNames)
		{
			if (DirectoryName.StartsWith(UserRegistryTestPrefix, ESearchCase::CaseSensitive))
			{
				IFileManager::Get().DeleteDirectory(*FPaths::Combine(Root, DirectoryName), false, true);
			}
		}
	}

	void UserRegistryReloadClean()
	{
		UnrealMcp::UserToolLock::FExclusiveGuard Guard;
		UnrealMcp::UserRegistry::ReloadUserToolRegistry(true);
	}

	bool UserRegistryWriteFile(const FString& Path, const FString& Content)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool UserRegistryWriteTool(
		const FString& ToolId,
		const FString& MainPy,
		const FString& Sha256,
		const FString& OverrideToolName = FString(),
		const FString& ExtraJsonFields = FString())
	{
		const FString ToolDir = UserRegistryToolDir(ToolId);
		IFileManager::Get().MakeDirectory(*ToolDir, true);
		const FString ToolName = OverrideToolName.IsEmpty() ? UserRegistryToolName(ToolId) : OverrideToolName;
		const FString ToolJson = FString::Printf(
			TEXT("{\n")
			TEXT("  \"name\": \"%s\",\n")
			TEXT("  \"title\": \"Test User Tool\",\n")
			TEXT("  \"description\": \"Test user tool.\",\n")
			TEXT("  \"pythonHandlerSha256\": \"%s\",\n")
			TEXT("  \"importAllowlist\": [],\n")
			TEXT("  \"inputSchema\": {\"type\":\"object\",\"additionalProperties\":true}%s\n")
			TEXT("}\n"),
			*ToolName,
			*Sha256,
			ExtraJsonFields.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(",\n%s"), *ExtraJsonFields));
		return UserRegistryWriteFile(FPaths::Combine(ToolDir, TEXT("main.py")), MainPy)
			&& UserRegistryWriteFile(FPaths::Combine(ToolDir, TEXT("tool.json")), ToolJson);
	}

	UnrealMcp::UserRegistry::FReloadResult UserRegistryReload(bool bAcceptChangedHashes = false)
	{
		UnrealMcp::UserToolLock::FExclusiveGuard Guard;
		return UnrealMcp::UserRegistry::ReloadUserToolRegistry(bAcceptChangedHashes);
	}

	bool UserRegistryHasLoadedTool(const FString& ToolName)
	{
		UnrealMcp::UserToolLock::FSharedGuard Guard;
		return UnrealMcp::UserRegistry::FindUserTool(ToolName) != nullptr;
	}

	FString UserRegistryLoadedToolSha(const FString& ToolName)
	{
		UnrealMcp::UserToolLock::FSharedGuard Guard;
		const UnrealMcp::UserRegistry::FUserToolEntry* Entry = UnrealMcp::UserRegistry::FindUserTool(ToolName);
		return Entry ? Entry->PythonHandlerSha256 : FString();
	}

	FString UserRegistryFirstRejectionReason(const UnrealMcp::UserRegistry::FReloadResult& Result)
	{
		return Result.RejectedTools.Num() > 0 ? Result.RejectedTools[0].Reason : FString();
	}

	bool UserRegistryHasRejection(const UnrealMcp::UserRegistry::FReloadResult& Result, const FString& ToolName, const FString& ReasonPart)
	{
		for (const UnrealMcp::UserRegistry::FReloadResult::FRejection& Rejection : Result.RejectedTools)
		{
			if (Rejection.ToolName == ToolName && Rejection.Reason.Contains(ReasonPart))
			{
				return true;
			}
		}
		return false;
	}

	FString UserRegistryStructuredString(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
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
	FUnrealMcpUserRegistry_SubdirPathRejectionTest,
	"UnrealMcp.UserRegistry.SubdirPathRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_SubdirPathRejectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	TestTrue(TEXT("valid user tool writes"), UserRegistryWriteTool(UserRegistryTestPrefix + TEXT("valid_plain"), UserRegistryPassPy, UserRegistryPassSha, FString(), TEXT("  \"pythonHandlerPath\": \"valid/relative/main.py\"")));
	TestTrue(TEXT("cache dir tool writes"), UserRegistryWriteTool(UserRegistryTestPrefix + TEXT("cache_ok"), UserRegistryPassPy, UserRegistryPassSha));
	TestTrue(TEXT("__pycache__ file writes"), UserRegistryWriteFile(FPaths::Combine(UserRegistryToolDir(UserRegistryTestPrefix + TEXT("cache_ok")), TEXT("__pycache__/main.cpython-311.pyc")), TEXT("cache\n")));
	TestTrue(TEXT(".pytest_cache file writes"), UserRegistryWriteFile(FPaths::Combine(UserRegistryToolDir(UserRegistryTestPrefix + TEXT("cache_ok")), TEXT(".pytest_cache/CACHEDIR.TAG")), TEXT("cache\n")));
	TestTrue(TEXT(".mypy_cache file writes"), UserRegistryWriteFile(FPaths::Combine(UserRegistryToolDir(UserRegistryTestPrefix + TEXT("cache_ok")), TEXT(".mypy_cache/meta.json")), TEXT("{}\n")));
	TestTrue(TEXT("nested subpackage dir writes"), UserRegistryWriteTool(UserRegistryTestPrefix + TEXT("nested"), UserRegistryPassPy, UserRegistryPassSha));
	TestTrue(TEXT("nested subpackage file writes"), UserRegistryWriteFile(FPaths::Combine(UserRegistryToolDir(UserRegistryTestPrefix + TEXT("nested")), TEXT("subpkg/__init__.py")), TEXT("VALUE = 1\n")));
	TestTrue(TEXT("extra py tool writes"), UserRegistryWriteTool(UserRegistryTestPrefix + TEXT("extra_py"), UserRegistryPassPy, UserRegistryPassSha));
	TestTrue(TEXT("extra helper writes"), UserRegistryWriteFile(FPaths::Combine(UserRegistryToolDir(UserRegistryTestPrefix + TEXT("extra_py")), TEXT("helpers.py")), TEXT("VALUE = 1\n")));

	const UnrealMcp::UserRegistry::FReloadResult Result = UserRegistryReload();
	TestTrue(TEXT("valid plain tool accepted"), UserRegistryHasLoadedTool(UserRegistryToolName(UserRegistryTestPrefix + TEXT("valid_plain"))));
	TestTrue(TEXT("cache dir tool accepted"), UserRegistryHasLoadedTool(UserRegistryToolName(UserRegistryTestPrefix + TEXT("cache_ok"))));
	TestEqual(TEXT("cache dirs do not affect handler sha"), UserRegistryLoadedToolSha(UserRegistryToolName(UserRegistryTestPrefix + TEXT("cache_ok"))), UserRegistryPassSha);
	TestTrue(TEXT("arbitrary subdir still rejected"), UserRegistryHasRejection(Result, UserRegistryTestPrefix + TEXT("nested"), TEXT("subdirectories are not allowed")));
	TestTrue(TEXT("invalid fixtures rejected"), Result.RejectedTools.Num() >= 2);

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserRegistry_HashValidationTest,
	"UnrealMcp.UserRegistry.HashValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_HashValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	const FString ToolId = UserRegistryTestPrefix + TEXT("hash");
	TestTrue(TEXT("matching hash writes"), UserRegistryWriteTool(ToolId, UserRegistryPassPy, UserRegistryPassSha));
	TestTrue(TEXT("matching hash accepted"), UserRegistryReload().AddedTools.Contains(UserRegistryToolName(ToolId)));

	TestTrue(TEXT("mismatch writes"), UserRegistryWriteTool(ToolId, UserRegistryUpdate1Py, UserRegistryPassSha));
	const UnrealMcp::UserRegistry::FReloadResult Rejected = UserRegistryReload(false);
	TestTrue(TEXT("mismatch rejected"), UserRegistryFirstRejectionReason(Rejected).Contains(TEXT("python_sha_mismatch")));

	const UnrealMcp::UserRegistry::FReloadResult Accepted = UserRegistryReload(true);
	TestTrue(TEXT("mismatch accepted when requested"), UserRegistryHasLoadedTool(UserRegistryToolName(ToolId)));
	TestTrue(TEXT("accepted mismatch categorized updated"), Accepted.UpdatedTools.Contains(UserRegistryToolName(ToolId)));

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserRegistry_HotAddTest,
	"UnrealMcp.UserRegistry.HotAdd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_HotAddTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	const FString ToolId = UserRegistryTestPrefix + TEXT("hot_add");
	TestTrue(TEXT("hot add writes"), UserRegistryWriteTool(ToolId, UserRegistryPassPy, UserRegistryPassSha));
	const UnrealMcp::UserRegistry::FReloadResult Result = UserRegistryReload();
	TestTrue(TEXT("added contains tool"), Result.AddedTools.Contains(UserRegistryToolName(ToolId)));
	TestTrue(TEXT("tool appears in registry"), UserRegistryHasLoadedTool(UserRegistryToolName(ToolId)));

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserRegistry_HotUpdateTest,
	"UnrealMcp.UserRegistry.HotUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_HotUpdateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	const FString ToolId = UserRegistryTestPrefix + TEXT("hot_update");
	TestTrue(TEXT("initial writes"), UserRegistryWriteTool(ToolId, UserRegistryUpdate1Py, UserRegistryUpdate1Sha));
	UserRegistryReload();
	TestTrue(TEXT("updated writes"), UserRegistryWriteTool(ToolId, UserRegistryUpdate2Py, UserRegistryUpdate2Sha));
	const UnrealMcp::UserRegistry::FReloadResult Result = UserRegistryReload();
	TestTrue(TEXT("updated contains tool"), Result.UpdatedTools.Contains(UserRegistryToolName(ToolId)));
	TestTrue(TEXT("updated tool still loaded"), UserRegistryHasLoadedTool(UserRegistryToolName(ToolId)));

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserRegistry_HotRemoveTest,
	"UnrealMcp.UserRegistry.HotRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_HotRemoveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	const FString ToolId = UserRegistryTestPrefix + TEXT("hot_remove");
	TestTrue(TEXT("remove fixture writes"), UserRegistryWriteTool(ToolId, UserRegistryPassPy, UserRegistryPassSha));
	UserRegistryReload();
	IFileManager::Get().Delete(*FPaths::Combine(UserRegistryToolDir(ToolId), TEXT("tool.json")));
	const UnrealMcp::UserRegistry::FReloadResult Result = UserRegistryReload();
	TestTrue(TEXT("removed contains tool"), Result.RemovedTools.Contains(UserRegistryToolName(ToolId)));
	TestFalse(TEXT("removed tool not loaded"), UserRegistryHasLoadedTool(UserRegistryToolName(ToolId)));

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserRegistry_DuplicateCoreNameTest,
	"UnrealMcp.UserRegistry.DuplicateCoreName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_DuplicateCoreNameTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	TestTrue(TEXT("core collision fixture writes"), UserRegistryWriteTool(UserRegistryTestPrefix + TEXT("core_collision"), UserRegistryPassPy, UserRegistryPassSha, TEXT("unreal.mcp_tool_audit")));
	const UnrealMcp::UserRegistry::FReloadResult Result = UserRegistryReload();
	TestTrue(TEXT("core collision rejected"), UserRegistryFirstRejectionReason(Result).Contains(TEXT("cannot shadow")));

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolLock_ReloadWhileExecuteTest,
	"UnrealMcp.UserRegistry.ReloadWhileExecute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolLock_ReloadWhileExecuteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::UserToolLock::AcquireShared();
	TestTrue(TEXT("current thread sees shared lock"), UnrealMcp::UserToolLock::IsSharedHeldByCurrentThread());
	TestTrue(TEXT("current thread sees any lock"), UnrealMcp::UserToolLock::IsHeldByCurrentThread());
	TestFalse(TEXT("exclusive blocked by shared"), UnrealMcp::UserToolLock::TryAcquireExclusive(0.05));
	UnrealMcp::UserToolLock::ReleaseShared();
	TestFalse(TEXT("current thread shared released"), UnrealMcp::UserToolLock::IsSharedHeldByCurrentThread());
	TestTrue(TEXT("exclusive succeeds after shared release"), UnrealMcp::UserToolLock::TryAcquireExclusive(0.5));
	TestTrue(TEXT("current thread sees exclusive lock"), UnrealMcp::UserToolLock::IsExclusiveHeldByCurrentThread());
	UnrealMcp::UserToolLock::ReleaseExclusive();
	TestFalse(TEXT("current thread exclusive released"), UnrealMcp::UserToolLock::IsExclusiveHeldByCurrentThread());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolLock_ExecuteWhileReloadTest,
	"UnrealMcp.UserRegistry.ExecuteWhileReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolLock_ExecuteWhileReloadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::UserToolLock::AcquireExclusive();
	TestFalse(TEXT("shared blocked by exclusive"), UnrealMcp::UserToolLock::TryAcquireShared(0.05));
	UnrealMcp::UserToolLock::ReleaseExclusive();
	TestTrue(TEXT("shared succeeds after exclusive release"), UnrealMcp::UserToolLock::TryAcquireShared(0.5));
	UnrealMcp::UserToolLock::ReleaseShared();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolLock_SameToolSerializeTest,
	"UnrealMcp.UserRegistry.SameToolSerialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolLock_SameToolSerializeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("first same-tool lock succeeds"), UnrealMcp::UserToolLock::SerializeSameToolExecution(TEXT("user.foo"), 0.5));
	TestFalse(TEXT("second same-tool lock times out"), UnrealMcp::UserToolLock::SerializeSameToolExecution(TEXT("user.foo"), 0.05));
	TestTrue(TEXT("different tool lock succeeds"), UnrealMcp::UserToolLock::SerializeSameToolExecution(TEXT("user.bar"), 0.05));
	UnrealMcp::UserToolLock::ReleaseSameToolExecution(TEXT("user.foo"));
	TestTrue(TEXT("same-tool retry succeeds after release"), UnrealMcp::UserToolLock::SerializeSameToolExecution(TEXT("user.foo"), 0.5));
	UnrealMcp::UserToolLock::ReleaseSameToolExecution(TEXT("user.foo"));
	UnrealMcp::UserToolLock::ReleaseSameToolExecution(TEXT("user.bar"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserRegistry_CountSeparationTest,
	"UnrealMcp.UserRegistry.CountSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_CountSeparationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	TestEqual(TEXT("core count remains canonical"), UnrealMcp::GetCoreToolCount(), 181);
	TestEqual(TEXT("user count initially zero"), UnrealMcp::GetUserToolCount(), 0);

	TestTrue(TEXT("tool a writes"), UserRegistryWriteTool(UserRegistryTestPrefix + TEXT("count_a"), UserRegistryPassPy, UserRegistryPassSha));
	TestTrue(TEXT("tool b writes"), UserRegistryWriteTool(UserRegistryTestPrefix + TEXT("count_b"), UserRegistryUpdate1Py, UserRegistryUpdate1Sha));
	TestTrue(TEXT("tool c writes"), UserRegistryWriteTool(UserRegistryTestPrefix + TEXT("count_c"), UserRegistryUpdate2Py, UserRegistryUpdate2Sha));
	UserRegistryReload();
	TestEqual(TEXT("user count after load"), UnrealMcp::GetUserToolCount(), 3);
	TestEqual(TEXT("core count still canonical"), UnrealMcp::GetCoreToolCount(), 181);

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserRegistry_ReloadToolStructuredContentTest,
	"UnrealMcp.UserRegistry.ReloadToolStructuredContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_ReloadToolStructuredContentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	FJsonObject Arguments;
	FUnrealMcpExecutionResult Result = UnrealMcp::UnrealMcpUserRegistryReloadTool::Execute(Arguments);
	TestFalse(TEXT("reload tool succeeds"), Result.bIsError);
	TestEqual(TEXT("hot reload mode"), UserRegistryStructuredString(Result, TEXT("hotReloadMode")), TEXT("python_main_reimport"));
	TestTrue(TEXT("before count present"), Result.StructuredContent.IsValid() && Result.StructuredContent->HasField(TEXT("beforeCount")));
	TestTrue(TEXT("after count present"), Result.StructuredContent.IsValid() && Result.StructuredContent->HasField(TEXT("afterCount")));
	const TSharedPtr<FJsonObject>* Lifecycle = nullptr;
	TestTrue(TEXT("lifecycle present"), Result.StructuredContent.IsValid() && Result.StructuredContent->TryGetObjectField(TEXT("lifecycle"), Lifecycle) && Lifecycle && (*Lifecycle).IsValid());
	FString State;
	if (Lifecycle && (*Lifecycle).IsValid())
	{
		(*Lifecycle)->TryGetStringField(TEXT("state"), State);
	}
	TestEqual(TEXT("lifecycle state"), State, TEXT("loaded_user_python_hot"));

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserRegistry_ReloadDryRunInsideSharedLockTest,
	"UnrealMcp.UserRegistry.ReloadDryRunInsideSharedLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_ReloadDryRunInsideSharedLockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	FJsonObject Arguments;
	Arguments.SetBoolField(TEXT("dryRun"), true);
	FUnrealMcpExecutionResult Result;
	{
		UnrealMcp::UserToolLock::FSharedGuard ExistingExecutionGuard;
		Result = UnrealMcp::UnrealMcpUserRegistryReloadTool::Execute(Arguments);
	}

	TestFalse(TEXT("dryRun reload succeeds inside shared lock"), Result.bIsError);
	TestEqual(TEXT("dryRun action"), UserRegistryStructuredString(Result, TEXT("action")), TEXT("mcp_user_registry_reload_dry_run"));
	bool bDryRun = false;
	if (Result.StructuredContent.IsValid())
	{
		Result.StructuredContent->TryGetBoolField(TEXT("dryRun"), bDryRun);
	}
	TestTrue(TEXT("dryRun flag"), bDryRun);

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserRegistry_ReloadApplyInsideSharedLockDeniedTest,
	"UnrealMcp.UserRegistry.ReloadApplyInsideSharedLockDenied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserRegistry_ReloadApplyInsideSharedLockDeniedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	FJsonObject Arguments;
	FUnrealMcpExecutionResult Result;
	{
		UnrealMcp::UserToolLock::FSharedGuard ExistingExecutionGuard;
		Result = UnrealMcp::UnrealMcpUserRegistryReloadTool::Execute(Arguments);
	}

	TestTrue(TEXT("apply reload denied inside shared lock"), Result.bIsError);
	TestEqual(
		TEXT("reentrant error code"),
		UserRegistryStructuredString(Result, TEXT("errorCode")),
		TEXT("user_registry_reload_reentrant_apply_denied"));

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpUserToolSmoke_PassFailTest,
	"UnrealMcp.UserRegistry.SmokePassFail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpUserToolSmoke_PassFailTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();

	const FString ToolId = UserRegistryTestPrefix + TEXT("smoke");
	const FString ToolName = UserRegistryToolName(ToolId);
	TestTrue(TEXT("smoke pass fixture writes"), UserRegistryWriteTool(ToolId, UserRegistryPassPy, UserRegistryPassSha));
	UserRegistryReload();

	FJsonObject SmokeArgs;
	SmokeArgs.SetStringField(TEXT("toolName"), ToolName);
	SmokeArgs.SetNumberField(TEXT("timeoutSeconds"), 10.0);
	FUnrealMcpExecutionResult PassResult = UnrealMcp::UnrealMcpUserToolSmokeTool::Execute(SmokeArgs);
	TestFalse(TEXT("smoke pass succeeds"), PassResult.bIsError);
	TestFalse(TEXT("smoke pass releases current thread lock"), UnrealMcp::UserToolLock::IsHeldByCurrentThread());

	FJsonObject ReloadArgs;
	FUnrealMcpExecutionResult ReloadAfterSmokeResult = UnrealMcp::UnrealMcpUserRegistryReloadTool::Execute(ReloadArgs);
	TestFalse(TEXT("reload after smoke is not denied by stale current-thread lock"), ReloadAfterSmokeResult.bIsError);
	TestNotEqual(
		TEXT("reload after smoke does not report reentrant denial"),
		UserRegistryStructuredString(ReloadAfterSmokeResult, TEXT("errorCode")),
		TEXT("user_registry_reload_reentrant_apply_denied"));

	TestTrue(TEXT("smoke fail fixture writes"), UserRegistryWriteTool(ToolId, UserRegistryRaisePy, UserRegistryRaiseSha));
	UserRegistryReload();
	FUnrealMcpExecutionResult FailResult = UnrealMcp::UnrealMcpUserToolSmokeTool::Execute(SmokeArgs);
	TestTrue(TEXT("smoke failure is error"), FailResult.bIsError);
	TestFalse(TEXT("smoke failure releases current thread lock"), UnrealMcp::UserToolLock::IsHeldByCurrentThread());
	TestTrue(TEXT("executionError populated"), FailResult.StructuredContent.IsValid() && FailResult.StructuredContent->HasField(TEXT("executionError")));

	UserRegistryDeleteTestDirs();
	UserRegistryReloadClean();
	return true;
}

#endif
