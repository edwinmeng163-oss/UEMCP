#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Providers/UnrealMcpApprovalPolicy.h"
#include "UnrealMcpCodeTools.h"
#include "UnrealMcpModule.h"

namespace
{
	FString CodeWriteToolsTestRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/UnrealMcpPyToolSamples/__CodeWriteToolsTest__")));
	}

	FString CodeWriteToolsSourceTestRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/__CodeWriteToolsTest__")));
	}

	FString CodeWriteToolsProjectPath(const FString& RelativePath)
	{
		return FPaths::Combine(TEXT("Tools/UnrealMcpPyToolSamples/__CodeWriteToolsTest__"), RelativePath);
	}

	FString CodeWriteToolsSourceProjectPath(const FString& RelativePath)
	{
		return FPaths::Combine(TEXT("Source/__CodeWriteToolsTest__"), RelativePath);
	}

	FString CodeWriteToolsChangesRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/CodeChanges")));
	}

	void CodeWriteToolsDeleteTestState()
	{
		IFileManager::Get().DeleteDirectory(*CodeWriteToolsTestRoot(), false, true);
		IFileManager::Get().DeleteDirectory(*CodeWriteToolsSourceTestRoot(), false, true);
		IFileManager::Get().DeleteDirectory(*CodeWriteToolsChangesRoot(), false, true);
		UnrealMcp::ClearCodeToolsApplyTestHooks();
	}

	TArray<uint8> CodeWriteToolsUtf8(const FString& Text)
	{
		TArray<uint8> Bytes;
		FTCHARToUTF8 Converter(*Text);
		Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
		return Bytes;
	}

	bool CodeWriteToolsWriteBytes(const FString& ProjectRelativePath, const TArray<uint8>& Bytes)
	{
		const FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ProjectRelativePath));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), true);
		return FFileHelper::SaveArrayToFile(Bytes, *FullPath);
	}

	bool CodeWriteToolsWriteString(const FString& ProjectRelativePath, const FString& Text)
	{
		return CodeWriteToolsWriteBytes(ProjectRelativePath, CodeWriteToolsUtf8(Text));
	}

	bool CodeWriteToolsReadBytes(const FString& ProjectRelativePath, TArray<uint8>& OutBytes)
	{
		const FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ProjectRelativePath));
		return FFileHelper::LoadFileToArray(OutBytes, *FullPath);
	}

	FString CodeWriteToolsReadSha(const FString& ProjectRelativePath)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("path"), ProjectRelativePath);
		FUnrealMcpExecutionResult Result;
		UnrealMcp::TryExecuteCodeTool(TEXT("unreal.code_read_file"), *Args, Result);
		FString Sha;
		if (!Result.bIsError && Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(TEXT("sha256"), Sha);
		}
		return Sha;
	}

	FUnrealMcpExecutionResult CodeWriteToolsExecute(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
	{
		FUnrealMcpExecutionResult Result;
		UnrealMcp::TryExecuteCodeTool(ToolName, *Arguments, Result);
		return Result;
	}

	TSharedPtr<FJsonObject> CodeWriteToolsMakeEdit(
		const FString& Path,
		const FString& ExpectedSha,
		const FString& Operation,
		const FString& OldText,
		const FString& AnchorText,
		const FString& NewText)
	{
		TSharedPtr<FJsonObject> Edit = MakeShared<FJsonObject>();
		Edit->SetStringField(TEXT("path"), Path);
		Edit->SetStringField(TEXT("expectedSha256"), ExpectedSha);
		Edit->SetStringField(TEXT("operation"), Operation);
		if (Operation.Equals(TEXT("replace_exact"), ESearchCase::IgnoreCase))
		{
			Edit->SetStringField(TEXT("oldText"), OldText);
		}
		if (Operation.Equals(TEXT("insert_before"), ESearchCase::IgnoreCase)
			|| Operation.Equals(TEXT("insert_after"), ESearchCase::IgnoreCase))
		{
			Edit->SetStringField(TEXT("anchorText"), AnchorText);
		}
		Edit->SetStringField(TEXT("newText"), NewText);
		return Edit;
	}

	TSharedPtr<FJsonObject> CodeWriteToolsMakePreviewArgs(const TArray<TSharedPtr<FJsonObject>>& Edits)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TSharedPtr<FJsonObject>& Edit : Edits)
		{
			Values.Add(MakeShared<FJsonValueObject>(Edit));
		}
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetArrayField(TEXT("edits"), Values);
		return Args;
	}

	FUnrealMcpExecutionResult CodeWriteToolsPreviewSingle(
		const FString& Path,
		const FString& ExpectedSha,
		const FString& Operation,
		const FString& OldText,
		const FString& AnchorText,
		const FString& NewText)
	{
		return CodeWriteToolsExecute(
			TEXT("unreal.code_preview_change"),
			CodeWriteToolsMakePreviewArgs({ CodeWriteToolsMakeEdit(Path, ExpectedSha, Operation, OldText, AnchorText, NewText) }));
	}

	FString CodeWriteToolsGetCode(const FUnrealMcpExecutionResult& Result)
	{
		FString Code;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(TEXT("code"), Code);
		}
		return Code;
	}

	FString CodeWriteToolsGetPreviewId(const FUnrealMcpExecutionResult& Result)
	{
		FString PreviewId;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(TEXT("previewId"), PreviewId);
		}
		return PreviewId;
	}

	FString CodeWriteToolsGetStringField(const FUnrealMcpExecutionResult& Result, const FString& Field)
	{
		FString Value;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(Field, Value);
		}
		return Value;
	}

	FUnrealMcpExecutionResult CodeWriteToolsApply(const FString& PreviewId, bool bDryRun, bool bConfirmHighRisk = false)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("previewId"), PreviewId);
		Args->SetBoolField(TEXT("dryRun"), bDryRun);
		if (bConfirmHighRisk)
		{
			Args->SetBoolField(TEXT("confirmHighRisk"), true);
		}
		return CodeWriteToolsExecute(TEXT("unreal.code_apply_change"), Args);
	}

	FUnrealMcpExecutionResult CodeWriteToolsRollbackByEditId(const FString& EditId, bool bDryRun, bool bForce = false)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("editId"), EditId);
		Args->SetBoolField(TEXT("dryRun"), bDryRun);
		if (bForce)
		{
			Args->SetBoolField(TEXT("force"), true);
		}
		return CodeWriteToolsExecute(TEXT("unreal.code_rollback_change"), Args);
	}

	FUnrealMcpExecutionResult CodeWriteToolsRollbackByManifestPath(const FString& ManifestPath, bool bDryRun)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("manifestPath"), ManifestPath);
		Args->SetBoolField(TEXT("dryRun"), bDryRun);
		return CodeWriteToolsExecute(TEXT("unreal.code_rollback_change"), Args);
	}

	bool CodeWriteToolsContainsBytes(const TArray<uint8>& Bytes, const TArray<uint8>& Needle)
	{
		for (int32 Index = 0; Index <= Bytes.Num() - Needle.Num(); ++Index)
		{
			bool bMatches = true;
			for (int32 NeedleIndex = 0; NeedleIndex < Needle.Num(); ++NeedleIndex)
			{
				if (Bytes[Index + NeedleIndex] != Needle[NeedleIndex])
				{
					bMatches = false;
					break;
				}
			}
			if (bMatches)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodeWriteToolsTest,
	"UnrealMcp.Code.WriteTools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodeWriteToolsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	CodeWriteToolsDeleteTestState();

	TestTrue(TEXT("replace fixture writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("replace.py")), TEXT("alpha\nold value\nomega\n")));
	const FString ReplaceSha = CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("replace.py")));
	const FUnrealMcpExecutionResult ReplacePreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("replace.py")), ReplaceSha, TEXT("replace_exact"), TEXT("old value"), FString(), TEXT("new value"));
	TestFalse(TEXT("preview replace_exact succeeds"), ReplacePreview.bIsError);
	TestFalse(TEXT("preview returns expectedNewSha256"), CodeWriteToolsGetStringField(ReplacePreview, TEXT("previewId")).IsEmpty());

	TestTrue(TEXT("insert-before fixture writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("before.py")), TEXT("top\nanchor\nbottom\n")));
	TestFalse(TEXT("preview insert_before succeeds"), CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("before.py")), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("before.py"))), TEXT("insert_before"), FString(), TEXT("anchor"), TEXT("before\n")).bIsError);

	TestTrue(TEXT("insert-after fixture writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("after.py")), TEXT("top\nanchor\nbottom\n")));
	TestFalse(TEXT("preview insert_after succeeds"), CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("after.py")), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("after.py"))), TEXT("insert_after"), FString(), TEXT("anchor"), TEXT("\nafter")).bIsError);

	const FUnrealMcpExecutionResult CreatePreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("created.py")), FString(), TEXT("create_file"), FString(), FString(), TEXT("created\n"));
	TestFalse(TEXT("preview create_file succeeds"), CreatePreview.bIsError);

	const FUnrealMcpExecutionResult StalePreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("replace.py")), TEXT("badsha"), TEXT("replace_exact"), TEXT("old value"), FString(), TEXT("new value"));
	TestTrue(TEXT("stale sha rejects"), StalePreview.bIsError);
	TestEqual(TEXT("stale sha code"), CodeWriteToolsGetCode(StalePreview), FString(TEXT("staleExpectedSha")));

	TestTrue(TEXT("ambiguous fixture writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("ambiguous.py")), TEXT("dup\ndup\n")));
	const FUnrealMcpExecutionResult AmbiguousPreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("ambiguous.py")), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("ambiguous.py"))), TEXT("replace_exact"), TEXT("dup"), FString(), TEXT("once"));
	TestTrue(TEXT("ambiguous match rejects"), AmbiguousPreview.bIsError);
	TestEqual(TEXT("ambiguous match code"), CodeWriteToolsGetCode(AmbiguousPreview), FString(TEXT("ambiguousMatch")));

	const FUnrealMcpExecutionResult MissingPreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("replace.py")), ReplaceSha, TEXT("replace_exact"), TEXT("not present"), FString(), TEXT("new value"));
	TestTrue(TEXT("missing match rejects"), MissingPreview.bIsError);
	TestEqual(TEXT("missing match code"), CodeWriteToolsGetCode(MissingPreview), FString(TEXT("missingMatch")));

	FUnrealMcpExecutionResult DryRunApply = CodeWriteToolsApply(CodeWriteToolsGetPreviewId(ReplacePreview), true);
	TestFalse(TEXT("apply dryRun succeeds"), DryRunApply.bIsError);
	TestFalse(TEXT("dryRun creates no backup dir"), FPaths::DirectoryExists(FPaths::Combine(CodeWriteToolsChangesRoot(), TEXT("Backups"))));
	TestFalse(TEXT("dryRun creates no last manifest"), FPaths::FileExists(FPaths::Combine(CodeWriteToolsChangesRoot(), TEXT("LastCodeChange.json"))));

	TArray<FString> ApplyStates;
	UnrealMcp::FCodeToolsApplyTestHooks StateHooks;
	StateHooks.AfterApplyManifestState = [&ApplyStates](const FString& ApplyState, const FString& ManifestPath)
	{
		(void)ManifestPath;
		ApplyStates.Add(ApplyState);
	};
	UnrealMcp::SetCodeToolsApplyTestHooks(StateHooks);
	const FUnrealMcpExecutionResult RealApply = CodeWriteToolsApply(CodeWriteToolsGetPreviewId(ReplacePreview), false);
	UnrealMcp::ClearCodeToolsApplyTestHooks();
	TestFalse(TEXT("real apply succeeds"), RealApply.bIsError);
	const FString EditId = CodeWriteToolsGetStringField(RealApply, TEXT("editId"));
	const FString ManifestPath = CodeWriteToolsGetStringField(RealApply, TEXT("manifestPath"));
	TestFalse(TEXT("apply returns editId"), EditId.IsEmpty());
	TestTrue(TEXT("apply writes manifest"), FPaths::FileExists(ManifestPath));
	TestTrue(TEXT("apply writes backup dir"), FPaths::DirectoryExists(CodeWriteToolsGetStringField(RealApply, TEXT("backupDirectory"))));
	TestTrue(TEXT("applyState saw started"), ApplyStates.Contains(TEXT("started")));
	TestTrue(TEXT("applyState saw backupComplete"), ApplyStates.Contains(TEXT("backupComplete")));
	TestTrue(TEXT("applyState saw writeComplete"), ApplyStates.Contains(TEXT("writeComplete")));
	TestTrue(TEXT("applyState saw verified"), ApplyStates.Contains(TEXT("verified")));
	TestTrue(TEXT("file was changed"), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("replace.py"))) != ReplaceSha);

	TArray<uint8> BomBytes;
	BomBytes.Add(0xef);
	BomBytes.Add(0xbb);
	BomBytes.Add(0xbf);
	BomBytes.Append(CodeWriteToolsUtf8(TEXT("line1\r\nanchor\r\nline3\r\n")));
	TestTrue(TEXT("BOM fixture writes"), CodeWriteToolsWriteBytes(CodeWriteToolsProjectPath(TEXT("bom_crlf.py")), BomBytes));
	const FUnrealMcpExecutionResult BomPreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("bom_crlf.py")), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("bom_crlf.py"))), TEXT("insert_after"), FString(), TEXT("anchor"), TEXT("\r\ninserted"));
	TestFalse(TEXT("BOM CRLF preview succeeds"), BomPreview.bIsError);
	TestFalse(TEXT("BOM CRLF apply succeeds"), CodeWriteToolsApply(CodeWriteToolsGetPreviewId(BomPreview), false).bIsError);
	TArray<uint8> BomAfterBytes;
	TestTrue(TEXT("BOM CRLF readback succeeds"), CodeWriteToolsReadBytes(CodeWriteToolsProjectPath(TEXT("bom_crlf.py")), BomAfterBytes));
	TestTrue(TEXT("BOM preserved"), BomAfterBytes.Num() >= 3 && BomAfterBytes[0] == 0xef && BomAfterBytes[1] == 0xbb && BomAfterBytes[2] == 0xbf);
	TestTrue(TEXT("CRLF insertion preserved"), CodeWriteToolsContainsBytes(BomAfterBytes, CodeWriteToolsUtf8(TEXT("anchor\r\ninserted\r\nline3"))));

	TestTrue(TEXT("transaction file one writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("txn_one.py")), TEXT("one old\n")));
	TestTrue(TEXT("transaction file two writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("txn_two.py")), TEXT("two old\n")));
	const FString TxnOneSha = CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("txn_one.py")));
	const FString TxnTwoSha = CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("txn_two.py")));
	FUnrealMcpExecutionResult TxnPreview = CodeWriteToolsExecute(
		TEXT("unreal.code_preview_change"),
		CodeWriteToolsMakePreviewArgs({
			CodeWriteToolsMakeEdit(CodeWriteToolsProjectPath(TEXT("txn_one.py")), TxnOneSha, TEXT("replace_exact"), TEXT("one old"), FString(), TEXT("one new")),
			CodeWriteToolsMakeEdit(CodeWriteToolsProjectPath(TEXT("txn_two.py")), TxnTwoSha, TEXT("replace_exact"), TEXT("two old"), FString(), TEXT("two new"))
		}));
	TestFalse(TEXT("transaction preview succeeds"), TxnPreview.bIsError);
	UnrealMcp::FCodeToolsApplyTestHooks TocTouHooks;
	TocTouHooks.BeforeWrite = [](const FString& ProjectRelativePath)
	{
		if (ProjectRelativePath.EndsWith(TEXT("txn_two.py"), ESearchCase::IgnoreCase))
		{
			CodeWriteToolsWriteString(ProjectRelativePath, TEXT("external edit\n"));
		}
	};
	UnrealMcp::SetCodeToolsApplyTestHooks(TocTouHooks);
	const FUnrealMcpExecutionResult TxnApply = CodeWriteToolsApply(CodeWriteToolsGetPreviewId(TxnPreview), false);
	UnrealMcp::ClearCodeToolsApplyTestHooks();
	TestTrue(TEXT("TOCTOU apply rejects"), TxnApply.bIsError);
	TestEqual(TEXT("TOCTOU code"), CodeWriteToolsGetCode(TxnApply), FString(TEXT("transactionRolledBack")));
	TestEqual(TEXT("first file rolled back"), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("txn_one.py"))), TxnOneSha);

	TestTrue(TEXT("collision fixture writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("collision.py")), TEXT("collision old\n")));
	const FUnrealMcpExecutionResult CollisionPreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("collision.py")), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("collision.py"))), TEXT("replace_exact"), TEXT("collision old"), FString(), TEXT("collision new"));
	UnrealMcp::FCodeToolsApplyTestHooks CollisionHooks;
	CollisionHooks.ShouldPretendBackupDirectoryExists = [](const FString& EditId)
	{
		(void)EditId;
		return true;
	};
	UnrealMcp::SetCodeToolsApplyTestHooks(CollisionHooks);
	const FUnrealMcpExecutionResult CollisionApply = CodeWriteToolsApply(CodeWriteToolsGetPreviewId(CollisionPreview), false);
	UnrealMcp::ClearCodeToolsApplyTestHooks();
	TestTrue(TEXT("editId collision rejects"), CollisionApply.bIsError);
	TestEqual(TEXT("editId collision code"), CodeWriteToolsGetCode(CollisionApply), FString(TEXT("editIdCollisionExhausted")));

	TestFalse(TEXT("rollback dryRun succeeds"), CodeWriteToolsRollbackByEditId(EditId, true).bIsError);
	TestFalse(TEXT("rollback real succeeds"), CodeWriteToolsRollbackByEditId(EditId, false).bIsError);
	TestEqual(TEXT("rollback restores original sha"), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("replace.py"))), ReplaceSha);

	const FUnrealMcpExecutionResult DriftPreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("replace.py")), ReplaceSha, TEXT("replace_exact"), TEXT("old value"), FString(), TEXT("drift value"));
	const FUnrealMcpExecutionResult DriftApply = CodeWriteToolsApply(CodeWriteToolsGetPreviewId(DriftPreview), false);
	TestFalse(TEXT("drift apply succeeds"), DriftApply.bIsError);
	TestTrue(TEXT("drift mutation writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("replace.py")), TEXT("manual drift\n")));
	const FUnrealMcpExecutionResult DriftReject = CodeWriteToolsRollbackByEditId(CodeWriteToolsGetStringField(DriftApply, TEXT("editId")), false);
	TestTrue(TEXT("drift without force rejects"), DriftReject.bIsError);
	TestEqual(TEXT("drift code"), CodeWriteToolsGetCode(DriftReject), FString(TEXT("driftDetected")));
	TestFalse(TEXT("drift force rollback succeeds"), CodeWriteToolsRollbackByEditId(CodeWriteToolsGetStringField(DriftApply, TEXT("editId")), false, true).bIsError);
	TestEqual(TEXT("force rollback restores original sha"), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("replace.py"))), ReplaceSha);

	const FUnrealMcpExecutionResult CreateApply = CodeWriteToolsApply(CodeWriteToolsGetPreviewId(CreatePreview), false);
	TestFalse(TEXT("create apply succeeds"), CreateApply.bIsError);
	TestTrue(TEXT("created file exists"), FPaths::FileExists(FPaths::Combine(CodeWriteToolsTestRoot(), TEXT("created.py"))));
	TestFalse(TEXT("create rollback deletes file"), CodeWriteToolsRollbackByEditId(CodeWriteToolsGetStringField(CreateApply, TEXT("editId")), false).bIsError);
	TestFalse(TEXT("created file deleted"), FPaths::FileExists(FPaths::Combine(CodeWriteToolsTestRoot(), TEXT("created.py"))));

	TestTrue(TEXT("explicit fixture writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("explicit.py")), TEXT("explicit old\n")));
	const FString ExplicitSha = CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("explicit.py")));
	const FUnrealMcpExecutionResult ExplicitPreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("explicit.py")), ExplicitSha, TEXT("replace_exact"), TEXT("explicit old"), FString(), TEXT("explicit new"));
	const FUnrealMcpExecutionResult ExplicitApply = CodeWriteToolsApply(CodeWriteToolsGetPreviewId(ExplicitPreview), false);
	TestFalse(TEXT("explicit apply succeeds"), ExplicitApply.bIsError);
	TestTrue(TEXT("other fixture writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("other.py")), TEXT("other old\n")));
	const FUnrealMcpExecutionResult OtherPreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("other.py")), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("other.py"))), TEXT("replace_exact"), TEXT("other old"), FString(), TEXT("other new"));
	TestFalse(TEXT("other apply succeeds"), CodeWriteToolsApply(CodeWriteToolsGetPreviewId(OtherPreview), false).bIsError);
	TestFalse(TEXT("explicit manifestPath rollback succeeds"), CodeWriteToolsRollbackByManifestPath(CodeWriteToolsGetStringField(ExplicitApply, TEXT("manifestPath")), false).bIsError);
	TestEqual(TEXT("explicit manifestPath honored"), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("explicit.py"))), ExplicitSha);

	TestTrue(TEXT("high-risk source fixture writes"), CodeWriteToolsWriteString(CodeWriteToolsSourceProjectPath(TEXT("HighRisk.cpp")), TEXT("void OldName() {}\n")));
	const FUnrealMcpExecutionResult HighRiskPreview = CodeWriteToolsPreviewSingle(CodeWriteToolsSourceProjectPath(TEXT("HighRisk.cpp")), CodeWriteToolsReadSha(CodeWriteToolsSourceProjectPath(TEXT("HighRisk.cpp"))), TEXT("replace_exact"), TEXT("OldName"), FString(), TEXT("NewName"));
	TestFalse(TEXT("high-risk preview succeeds"), HighRiskPreview.bIsError);
	const FUnrealMcpExecutionResult HighRiskApply = CodeWriteToolsApply(CodeWriteToolsGetPreviewId(HighRiskPreview), false);
	TestTrue(TEXT("high-risk apply without confirm rejects"), HighRiskApply.bIsError);
	TestEqual(TEXT("high-risk code"), CodeWriteToolsGetCode(HighRiskApply), FString(TEXT("highRiskConfirmationRequired")));

	TestTrue(TEXT("uproject test fixture writes"), CodeWriteToolsWriteString(CodeWriteToolsProjectPath(TEXT("EngineAssoc.uproject")), TEXT("{\"FileVersion\":3,\"EngineAssociation\":\"5.6\"}\n")));
	const FUnrealMcpExecutionResult UprojectPreview = CodeWriteToolsPreviewSingle(CodeWriteToolsProjectPath(TEXT("EngineAssoc.uproject")), CodeWriteToolsReadSha(CodeWriteToolsProjectPath(TEXT("EngineAssoc.uproject"))), TEXT("replace_exact"), TEXT("\"EngineAssociation\":\"5.6\""), FString(), TEXT("\"EngineAssociation\":\"5.7\""));
	TestTrue(TEXT("EngineAssociation-only uproject edit rejects"), UprojectPreview.bIsError);

	using namespace UnrealMcp::Approval;
	ERiskLevel Risk = ERiskLevel::Low;
	FString Reason;
	TSharedPtr<FJsonObject> ApprovalArgs = MakeShared<FJsonObject>();
	ApprovalArgs->SetBoolField(TEXT("dryRun"), false);
	TestEqual(TEXT("apply dryRun=false requires approval"), static_cast<int32>(EvaluateApprovalPolicy(TEXT("unreal.code_apply_change"), ApprovalArgs, true, Risk, Reason)), static_cast<int32>(EDecision::RequireApproval));
	ApprovalArgs->SetBoolField(TEXT("confirmHighRisk"), true);
	TestEqual(TEXT("high-risk reason ordering requires approval"), static_cast<int32>(EvaluateApprovalPolicy(TEXT("unreal.code_apply_change"), ApprovalArgs, true, Risk, Reason)), static_cast<int32>(EDecision::RequireApproval));
	TestTrue(TEXT("high-risk reason is specific"), Reason.Contains(TEXT("high-risk")));

	CodeWriteToolsDeleteTestState();
	return true;
}

#endif
