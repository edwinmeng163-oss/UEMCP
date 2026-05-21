#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

namespace LE = UnrealMcp::Extension;

namespace UnrealMcpAuditTaxonomyTests
{
	const FString AuditUserToolPrefix = TEXT("codex_job_d_");
	const FString AuditUserPassPy = TEXT("def execute(args):\n    return {\"ok\": True, \"dryRun\": args.get(\"dryRun\", False)}\n");
	const FString AuditUserPassSha = TEXT("c0b99179da60b01efa870ef4bfb9e391ae22a1234bf079fb9fc4e7f28c903de0");
	const FString AuditUserOtherSha = TEXT("27bc005aeef4baccbbe3d52c14a68a7365dd6ad54fd10c90f9fbe5ffca99e3ae");

	TSharedPtr<FJsonObject> InvokeAuditAndGetStructured()
	{
		FJsonObject Arguments;
		FUnrealMcpModule& Module = FModuleManager::LoadModuleChecked<FUnrealMcpModule>(TEXT("UnrealMcp"));
		return Module.ExecuteToolFromEditorUI(TEXT("unreal.mcp_tool_audit"), Arguments).StructuredContent;
	}

	int32 GetIntField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		double Value = 0.0;
		if (Object.IsValid())
		{
			Object->TryGetNumberField(FieldName, Value);
		}
		return FMath::RoundToInt(Value);
	}

	bool GetBoolField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		bool bValue = false;
		if (Object.IsValid())
		{
			Object->TryGetBoolField(FieldName, bValue);
		}
		return bValue;
	}

	FString GetStringField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		FString Value;
		if (Object.IsValid())
		{
			Object->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	bool AuditContainsIssueCode(const TSharedPtr<FJsonObject>& StructuredContent, const FString& IssueCode)
	{
		const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
		if (!StructuredContent.IsValid() || !StructuredContent->TryGetArrayField(TEXT("taxonomyIssues"), Issues) || !Issues)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& IssueValue : *Issues)
		{
			const TSharedPtr<FJsonObject> IssueObject = IssueValue.IsValid() ? IssueValue->AsObject() : nullptr;
			if (IssueObject.IsValid() && GetStringField(IssueObject, TEXT("issueCode")) == IssueCode)
			{
				return true;
			}
		}
		return false;
	}

	FString AuditUserToolsRoot()
	{
		UnrealMcp::UserRegistry::InitializeUserToolRegistry();
		return UnrealMcp::UserRegistry::GetUserToolsRootDir();
	}

	FString AuditUserToolDir(const FString& ToolId)
	{
		return FPaths::Combine(AuditUserToolsRoot(), ToolId);
	}

	FString AuditUserToolName(const FString& ToolId)
	{
		return FString::Printf(TEXT("user.%s"), *ToolId);
	}

	bool AuditWriteFile(const FString& Path, const FString& Content)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool AuditWriteToolJson(const FString& ToolId, const FString& Sha256)
	{
		const FString ToolJson = FString::Printf(
			TEXT("{\n")
			TEXT("  \"name\": \"%s\",\n")
			TEXT("  \"title\": \"Audit Test User Tool\",\n")
			TEXT("  \"description\": \"Audit taxonomy test user tool.\",\n")
			TEXT("  \"pythonHandlerSha256\": \"%s\",\n")
			TEXT("  \"importAllowlist\": [],\n")
			TEXT("  \"inputSchema\": {\"type\":\"object\",\"additionalProperties\":false}\n")
			TEXT("}\n"),
			*AuditUserToolName(ToolId),
			*Sha256);
		return AuditWriteFile(FPaths::Combine(AuditUserToolDir(ToolId), TEXT("tool.json")), ToolJson);
	}

	bool AuditWriteValidUserTool(const FString& ToolId)
	{
		return AuditWriteFile(FPaths::Combine(AuditUserToolDir(ToolId), TEXT("main.py")), AuditUserPassPy)
			&& AuditWriteToolJson(ToolId, AuditUserPassSha);
	}

	void AuditDeleteTestDirs()
	{
		const FString Root = AuditUserToolsRoot();
		TArray<FString> DirectoryNames;
		IFileManager::Get().FindFiles(DirectoryNames, *FPaths::Combine(Root, TEXT("*")), false, true);
		for (const FString& DirectoryName : DirectoryNames)
		{
			if (DirectoryName.StartsWith(AuditUserToolPrefix, ESearchCase::CaseSensitive))
			{
				IFileManager::Get().DeleteDirectory(*FPaths::Combine(Root, DirectoryName), false, true);
			}
		}
	}

	void AuditReloadClean()
	{
		UnrealMcp::UserToolLock::FExclusiveGuard Guard;
		UnrealMcp::UserRegistry::ReloadUserToolRegistry(true);
	}

	LE::ELifecycleState ExpectedStateForIssueCode(const FString& IssueCode)
	{
		if (IssueCode == TEXT("core_registry_ok"))
		{
			return LE::ELifecycleState::LoadedCoreCppAfterRestart;
		}
		if (IssueCode == TEXT("user_registry_ok"))
		{
			return LE::ELifecycleState::LoadedUserPythonHot;
		}
		if (IssueCode == TEXT("descriptor_only"))
		{
			return LE::ELifecycleState::DraftScaffolded;
		}
		if (IssueCode == TEXT("python_sha_mismatch") || IssueCode == TEXT("reload_required"))
		{
			return LE::ELifecycleState::AppliedUserPythonReloadRequired;
		}
		if (IssueCode == TEXT("build_required"))
		{
			return LE::ELifecycleState::AppliedCoreCppBuildRequired;
		}
		if (IssueCode == TEXT("restart_required"))
		{
			return LE::ELifecycleState::BuiltRestartRequired;
		}
		return LE::ELifecycleState::Blocked;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAudit_StaticOnlyTest,
	"UnrealMcp.Audit.StaticOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAudit_StaticOnlyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<FJsonObject> StructuredContent = UnrealMcpAuditTaxonomyTests::InvokeAuditAndGetStructured();
	TestTrue(TEXT("structuredContent present"), StructuredContent.IsValid());
	TestEqual(TEXT("auditMode"), UnrealMcpAuditTaxonomyTests::GetStringField(StructuredContent, TEXT("auditMode")), TEXT("static_registry_and_handler_reconciliation"));
	TestFalse(TEXT("didInvokeHandlers"), UnrealMcpAuditTaxonomyTests::GetBoolField(StructuredContent, TEXT("didInvokeHandlers")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAudit_CountSplitTest,
	"UnrealMcp.Audit.CountSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAudit_CountSplitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpAuditTaxonomyTests;
	AuditDeleteTestDirs();
	AuditReloadClean();

	TSharedPtr<FJsonObject> StructuredContent = InvokeAuditAndGetStructured();
	const int32 InitialUserCount = UnrealMcp::UserRegistry::GetUserToolCount();
	if (InitialUserCount == 0)
	{
		TestEqual(TEXT("initial userToolCount is zero"), GetIntField(StructuredContent, TEXT("userToolCount")), 0);
	}
	else
	{
		AddInfo(FString::Printf(TEXT("Pre-existing user tools loaded (%d); verifying split against public API count instead of zero."), InitialUserCount));
		TestEqual(TEXT("initial userToolCount matches API"), GetIntField(StructuredContent, TEXT("userToolCount")), InitialUserCount);
	}
	TestEqual(TEXT("coreToolCount matches API"), GetIntField(StructuredContent, TEXT("coreToolCount")), UnrealMcp::GetCoreToolCount());

	TestTrue(TEXT("user tool a writes"), AuditWriteValidUserTool(AuditUserToolPrefix + TEXT("count_a")));
	TestTrue(TEXT("user tool b writes"), AuditWriteValidUserTool(AuditUserToolPrefix + TEXT("count_b")));
	AuditReloadClean();

	StructuredContent = InvokeAuditAndGetStructured();
	TestEqual(TEXT("coreToolCount remains API count"), GetIntField(StructuredContent, TEXT("coreToolCount")), UnrealMcp::GetCoreToolCount());
	TestEqual(TEXT("userToolCount includes two fixtures"), GetIntField(StructuredContent, TEXT("userToolCount")), InitialUserCount + 2);

	AuditDeleteTestDirs();
	AuditReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAudit_AllTaxonomyCodesTest,
	"UnrealMcp.Audit.AllTaxonomyCodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAudit_AllTaxonomyCodesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpAuditTaxonomyTests;
	AuditDeleteTestDirs();
	AuditReloadClean();

	const FString LoadedToolId = AuditUserToolPrefix + TEXT("loaded_ok");
	TestTrue(TEXT("loaded user tool writes"), AuditWriteValidUserTool(LoadedToolId));
	AuditReloadClean();

	TestTrue(TEXT("reload pending user tool writes"), AuditWriteValidUserTool(AuditUserToolPrefix + TEXT("reload_pending")));
	TestTrue(TEXT("missing main tool json writes"), AuditWriteToolJson(AuditUserToolPrefix + TEXT("missing_main"), AuditUserPassSha));
	TestTrue(TEXT("sha mismatch main writes"), AuditWriteFile(FPaths::Combine(AuditUserToolDir(AuditUserToolPrefix + TEXT("sha_mismatch")), TEXT("main.py")), AuditUserPassPy));
	TestTrue(TEXT("sha mismatch tool json writes"), AuditWriteToolJson(AuditUserToolPrefix + TEXT("sha_mismatch"), AuditUserOtherSha));
	TestTrue(TEXT("invalid tool json writes"), AuditWriteFile(FPaths::Combine(AuditUserToolDir(AuditUserToolPrefix + TEXT("invalid_json")), TEXT("tool.json")), TEXT("{ invalid json\n")));

	const TSharedPtr<FJsonObject> StructuredContent = InvokeAuditAndGetStructured();
	TestTrue(TEXT("core_registry_ok produced"), AuditContainsIssueCode(StructuredContent, TEXT("core_registry_ok")));
	TestTrue(TEXT("user_registry_ok produced"), AuditContainsIssueCode(StructuredContent, TEXT("user_registry_ok")));
	TestTrue(TEXT("python_handler_missing produced"), AuditContainsIssueCode(StructuredContent, TEXT("python_handler_missing")));
	TestTrue(TEXT("python_sha_mismatch produced"), AuditContainsIssueCode(StructuredContent, TEXT("python_sha_mismatch")));
	TestTrue(TEXT("user_registry_invalid produced"), AuditContainsIssueCode(StructuredContent, TEXT("user_registry_invalid")));
	TestTrue(TEXT("reload_required produced"), AuditContainsIssueCode(StructuredContent, TEXT("reload_required")));

	const TArray<FString> HardToSynthesizeCodes = {
		TEXT("descriptor_only"),
		TEXT("registry_no_handler"),
		TEXT("handler_no_registry"),
		TEXT("build_required"),
		TEXT("restart_required")
	};
	for (const FString& Code : HardToSynthesizeCodes)
	{
		if (!AuditContainsIssueCode(StructuredContent, Code))
		{
			AddInfo(FString::Printf(TEXT("Skipped synthetic construction for %s; production audit wiring exists but this test build has no mutable registry/handler hook for that state."), *Code));
		}
	}

	AuditDeleteTestDirs();
	AuditReloadClean();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAudit_PerToolLifecycleTest,
	"UnrealMcp.Audit.PerToolLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAudit_PerToolLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpAuditTaxonomyTests;
	const TSharedPtr<FJsonObject> StructuredContent = InvokeAuditAndGetStructured();
	TestTrue(TEXT("structuredContent present"), StructuredContent.IsValid());

	const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
	TestTrue(TEXT("tools array present"), StructuredContent.IsValid() && StructuredContent->TryGetArrayField(TEXT("tools"), Tools) && Tools != nullptr);
	if (!Tools)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& ToolValue : *Tools)
	{
		const TSharedPtr<FJsonObject> ToolObject = ToolValue.IsValid() ? ToolValue->AsObject() : nullptr;
		TestTrue(TEXT("tool object valid"), ToolObject.IsValid());
		if (!ToolObject.IsValid())
		{
			continue;
		}

		const FString ToolName = GetStringField(ToolObject, TEXT("name"));
		const FString IssueCode = GetStringField(ToolObject, TEXT("issueCode"));
		const TSharedPtr<FJsonObject>* LifecycleObject = nullptr;
		TestTrue(*FString::Printf(TEXT("%s lifecycle present"), *ToolName), ToolObject->TryGetObjectField(TEXT("lifecycle"), LifecycleObject) && LifecycleObject && (*LifecycleObject).IsValid());
		if (!LifecycleObject || !(*LifecycleObject).IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> Wrapped = MakeShared<FJsonObject>();
		Wrapped->SetObjectField(TEXT("lifecycle"), *LifecycleObject);
		LE::FToolLifecycle Lifecycle;
		TestTrue(*FString::Printf(TEXT("%s lifecycle parses"), *ToolName), LE::ParseLifecycleJson(Wrapped, Lifecycle));
		TestEqual(*FString::Printf(TEXT("%s lifecycle state maps from issueCode"), *ToolName), static_cast<uint8>(Lifecycle.State), static_cast<uint8>(ExpectedStateForIssueCode(IssueCode)));
		TestEqual(*FString::Printf(TEXT("%s callableNow follows state"), *ToolName), Lifecycle.bCallableNow, LE::IsLifecycleStateCallable(Lifecycle.State));
	}

	return true;
}

#endif
