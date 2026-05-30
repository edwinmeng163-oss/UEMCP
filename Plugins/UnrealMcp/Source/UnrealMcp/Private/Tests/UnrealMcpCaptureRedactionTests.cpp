#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpActivityLog.h"
#include "UnrealMcpCaptureRedaction.h"

namespace UnrealMcpCaptureRedactionTests
{
	TSharedPtr<FJsonObject> MakeObject()
	{
		return MakeShared<FJsonObject>();
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

	FString SerializeObject(const TSharedPtr<FJsonObject>& Object)
	{
		FString Json;
		if (!Object.IsValid())
		{
			return Json;
		}
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Json;
	}

	TSharedPtr<FJsonObject> ParseJsonLine(const FString& Line)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		FJsonSerializer::Deserialize(Reader, Object);
		return Object;
	}

	FString MakeActivityLogPath(const FString& SessionId)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/ActivityLog"), SessionId + TEXT(".jsonl")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCaptureRedactionMatrixTest,
	"UnrealMcp.Capture.RedactionMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCaptureRedactionMatrixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace UnrealMcp::CaptureRedaction;

	{
		TSharedPtr<FJsonObject> Args = UnrealMcpCaptureRedactionTests::MakeObject();
		Args->SetStringField(TEXT("token"), TEXT("secret-token-value"));
		Args->SetStringField(TEXT("api_key"), TEXT("secret-api-key-value"));
		Args->SetStringField(TEXT("password"), TEXT("secret-password-value"));
		Args->SetStringField(TEXT("secret"), TEXT("secret-field-value"));
		Args->SetStringField(TEXT("authorization"), TEXT("Bearer secret-auth-value"));
		TSharedPtr<FJsonObject> Nested = UnrealMcpCaptureRedactionTests::MakeObject();
		Nested->SetStringField(TEXT("cookie"), TEXT("secret-cookie-value"));
		Args->SetObjectField(TEXT("nested"), Nested);

		const FRedactionResult Result = SanitizeToolArguments_Pure(TEXT("unreal.editor_status"), Args, 4096, 65536);
		TestEqual(TEXT("Secret field redaction reports redacted status."), Result.CaptureStatus, FString(TEXT("redacted")));
		TestTrue(TEXT("Secret redaction keeps sanitized args."), Result.SanitizedArguments.IsValid());
		TestEqual(TEXT("Token is redacted."), Result.SanitizedArguments->GetStringField(TEXT("token")), FString(TEXT("<redacted:secret>")));
		TestEqual(TEXT("API key is redacted."), Result.SanitizedArguments->GetStringField(TEXT("api_key")), FString(TEXT("<redacted:secret>")));
		TestEqual(TEXT("Password is redacted."), Result.SanitizedArguments->GetStringField(TEXT("password")), FString(TEXT("<redacted:secret>")));
		TestEqual(TEXT("Secret is redacted."), Result.SanitizedArguments->GetStringField(TEXT("secret")), FString(TEXT("<redacted:secret>")));
		TestEqual(TEXT("Authorization is redacted."), Result.SanitizedArguments->GetStringField(TEXT("authorization")), FString(TEXT("<redacted:secret>")));
		const TSharedPtr<FJsonObject>* SanitizedNested = nullptr;
		TestTrue(TEXT("Nested object remains present."), Result.SanitizedArguments->TryGetObjectField(TEXT("nested"), SanitizedNested) && SanitizedNested && SanitizedNested->IsValid());
		if (SanitizedNested && SanitizedNested->IsValid())
		{
			TestEqual(TEXT("Nested cookie is redacted."), (*SanitizedNested)->GetStringField(TEXT("cookie")), FString(TEXT("<redacted:secret>")));
		}
		TestEqual(TEXT("Secret redaction summary count."), UnrealMcpCaptureRedactionTests::GetIntField(Result.RedactionSummaryPublic, TEXT("redactedSecretFields")), 6);
		const FString SummaryJson = UnrealMcpCaptureRedactionTests::SerializeObject(Result.RedactionSummaryPublic);
		TestFalse(TEXT("Public summary excludes token value."), SummaryJson.Contains(TEXT("secret-token-value")));
		TestFalse(TEXT("Public summary excludes cookie value."), SummaryJson.Contains(TEXT("secret-cookie-value")));
	}

	{
		TSharedPtr<FJsonObject> Args = UnrealMcpCaptureRedactionTests::MakeObject();
		const FString HomePath = FPaths::Combine(FPlatformProcess::UserDir(), TEXT("capture-redaction-home.txt"));
		const FString ProjectDir = FPaths::ProjectDir();
		const FString ProjectDirFull = FPaths::ConvertRelativePathToFull(ProjectDir);
		const FString ProjectPath = FPaths::Combine(ProjectDir, TEXT("Plugins/UnrealMcp/Source/Example.cpp"));
		const FString ProjectPathFull = FPaths::ConvertRelativePathToFull(ProjectPath);
		AddInfo(FString::Printf(
			TEXT("CaptureRedaction paths: ProjectDir='%s'; ProjectDirFull='%s'; ProjectPath='%s'; ProjectPathFull='%s'."),
			*ProjectDir,
			*ProjectDirFull,
			*ProjectPath,
			*ProjectPathFull));
		Args->SetStringField(TEXT("homePath"), HomePath);
		Args->SetStringField(TEXT("projectPath"), ProjectPath);

		const FRedactionResult Result = SanitizeToolArguments_Pure(TEXT("unreal.code_read_file"), Args, 4096, 65536);
		TestEqual(TEXT("Path redaction reports redacted status."), Result.CaptureStatus, FString(TEXT("redacted")));
		TestTrue(TEXT("Home path uses placeholder."), Result.SanitizedArguments->GetStringField(TEXT("homePath")).StartsWith(TEXT("<home>/")));
		TestTrue(TEXT("Project path uses placeholder."), Result.SanitizedArguments->GetStringField(TEXT("projectPath")).StartsWith(TEXT("<project>/")));
		TestEqual(TEXT("Path redaction summary count."), UnrealMcpCaptureRedactionTests::GetIntField(Result.RedactionSummaryPublic, TEXT("redactedPaths")), 2);
	}

	{
		TSharedPtr<FJsonObject> Args = UnrealMcpCaptureRedactionTests::MakeObject();
		const FString ProjectPathFull = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins/UnrealMcp/Source/Example.cpp")));
		FString ProjectPathFullBackslashes = ProjectPathFull;
		ProjectPathFullBackslashes.ReplaceInline(TEXT("/"), TEXT("\\"));
		Args->SetStringField(TEXT("projectPathFull"), ProjectPathFull);
		Args->SetStringField(TEXT("projectPathFullBackslashes"), ProjectPathFullBackslashes);

		const FRedactionResult Result = SanitizeToolArguments_Pure(TEXT("unreal.code_read_file"), Args, 4096, 65536);
		TestEqual(TEXT("Absolute project path redaction reports redacted status."), Result.CaptureStatus, FString(TEXT("redacted")));
		TestTrue(TEXT("Absolute project path uses placeholder."), Result.SanitizedArguments->GetStringField(TEXT("projectPathFull")).StartsWith(TEXT("<project>/")));
		TestTrue(TEXT("Backslash project path uses placeholder."), Result.SanitizedArguments->GetStringField(TEXT("projectPathFullBackslashes")).StartsWith(TEXT("<project>/")));
		TestEqual(TEXT("Absolute project path redaction summary count."), UnrealMcpCaptureRedactionTests::GetIntField(Result.RedactionSummaryPublic, TEXT("redactedPaths")), 2);
	}

	{
		const TArray<FString> SkippedTools = {
			TEXT("unreal.execute_python"),
			TEXT("unreal.execute_python_file"),
			TEXT("unreal.execute_console_command"),
			TEXT("unreal.code_preview_change"),
			TEXT("unreal.mcp_patch_scaffold_patch"),
			TEXT("unreal.mcp_validate_cpp_patch")
		};

		for (const FString& ToolName : SkippedTools)
		{
			TSharedPtr<FJsonObject> Args = UnrealMcpCaptureRedactionTests::MakeObject();
			Args->SetStringField(TEXT("command"), TEXT("print('private')"));
			Args->SetStringField(TEXT("scriptPath"), TEXT("/private/script.py"));
			Args->SetStringField(TEXT("args"), TEXT("--private"));
			Args->SetStringField(TEXT("edits"), TEXT("file patch content"));
			Args->SetStringField(TEXT("newText"), TEXT("new patch content"));
			Args->SetStringField(TEXT("findText"), TEXT("old patch content"));
			Args->SetStringField(TEXT("patchText"), TEXT("C++ patch content"));
			const FRedactionResult Result = SanitizeToolArguments_Pure(ToolName, Args, 4096, 65536);
			TestEqual(FString::Printf(TEXT("%s is skipped."), *ToolName), Result.CaptureStatus, FString(TEXT("skipped")));
			TestFalse(FString::Printf(TEXT("%s has no sanitized args."), *ToolName), Result.SanitizedArguments.IsValid());
			TestEqual(FString::Printf(TEXT("%s summary marks skip."), *ToolName), UnrealMcpCaptureRedactionTests::GetIntField(Result.RedactionSummaryPublic, TEXT("skippedToolDenylist")), 1);
		}

		TSharedPtr<FJsonObject> ApplyArgs = UnrealMcpCaptureRedactionTests::MakeObject();
		ApplyArgs->SetStringField(TEXT("previewId"), TEXT("preview-123"));
		ApplyArgs->SetBoolField(TEXT("dryRun"), true);
		const FRedactionResult ApplyResult = SanitizeToolArguments_Pure(TEXT("unreal.code_apply_change"), ApplyArgs, 4096, 65536);
		TestEqual(TEXT("code_apply_change remains capturable."), ApplyResult.CaptureStatus, FString(TEXT("captured")));
		TestTrue(TEXT("code_apply_change returns sanitized args."), ApplyResult.SanitizedArguments.IsValid());
	}

	{
		TSharedPtr<FJsonObject> Args = UnrealMcpCaptureRedactionTests::MakeObject();
		Args->SetStringField(TEXT("description"), TEXT("0123456789"));
		const FRedactionResult Result = SanitizeToolArguments_Pure(TEXT("unreal.editor_status"), Args, 5, 65536);
		TestEqual(TEXT("Oversized value redacts in place."), Result.CaptureStatus, FString(TEXT("redacted")));
		TestEqual(TEXT("Oversized value marker includes byte count."), Result.SanitizedArguments->GetStringField(TEXT("description")), FString(TEXT("<omitted:oversized:10 bytes>")));
		TestEqual(TEXT("Oversized value summary count."), UnrealMcpCaptureRedactionTests::GetIntField(Result.RedactionSummaryPublic, TEXT("omittedOversized")), 1);
	}

	{
		TSharedPtr<FJsonObject> Args = UnrealMcpCaptureRedactionTests::MakeObject();
		Args->SetStringField(TEXT("description"), TEXT("small"));
		const FRedactionResult Result = SanitizeToolArguments_Pure(TEXT("unreal.editor_status"), Args, 4096, 5);
		TestEqual(TEXT("Oversized total skips capture."), Result.CaptureStatus, FString(TEXT("oversized")));
		TestFalse(TEXT("Oversized total has no sanitized args."), Result.SanitizedArguments.IsValid());
		TestEqual(TEXT("Oversized total summary count."), UnrealMcpCaptureRedactionTests::GetIntField(Result.RedactionSummaryPublic, TEXT("oversizedTotal")), 1);
	}

	{
		TSharedPtr<FJsonObject> Args = UnrealMcpCaptureRedactionTests::MakeObject();
		Args->SetStringField(TEXT("filter"), TEXT("StaticMesh"));
		Args->SetBoolField(TEXT("includeHidden"), false);
		const FRedactionResult Result = SanitizeToolArguments_Pure(TEXT("unreal.list_level_actors"), Args, 4096, 65536);
		TestEqual(TEXT("Clean args are captured."), Result.CaptureStatus, FString(TEXT("captured")));
		TestTrue(TEXT("Clean args return sanitized copy."), Result.SanitizedArguments.IsValid());
		TestEqual(TEXT("Clean args secret count."), UnrealMcpCaptureRedactionTests::GetIntField(Result.RedactionSummaryPublic, TEXT("redactedSecretFields")), 0);
		TestEqual(TEXT("Clean args path count."), UnrealMcpCaptureRedactionTests::GetIntField(Result.RedactionSummaryPublic, TEXT("redactedPaths")), 0);
		TestEqual(TEXT("Clean args oversized count."), UnrealMcpCaptureRedactionTests::GetIntField(Result.RedactionSummaryPublic, TEXT("omittedOversized")), 0);
	}

	{
		TSharedPtr<FJsonObject> Args = UnrealMcpCaptureRedactionTests::MakeObject();
		Args->SetStringField(TEXT("password"), TEXT("secret-payload-value"));
		TSharedPtr<FJsonObject> Payload = UnrealMcpCaptureRedactionTests::MakeObject();
		AttachCaptureMetadata(Payload, TEXT("unreal.editor_status"), *Args, 4096, 65536);
		TestEqual(TEXT("Payload capture schema version."), UnrealMcpCaptureRedactionTests::GetIntField(Payload, TEXT("captureSchemaVersion")), 1);
		TestEqual(TEXT("Payload capture status."), Payload->GetStringField(TEXT("captureStatus")), FString(TEXT("redacted")));
		TestTrue(TEXT("Payload has public redaction summary."), Payload->HasTypedField<EJson::Object>(TEXT("redactionSummaryPublic")));
		TestFalse(TEXT("Payload does not store sanitized args."), Payload->HasField(TEXT("sanitizedArguments")));
		TestFalse(TEXT("Payload does not store raw args."), Payload->HasField(TEXT("arguments")));
		const FString PayloadJson = UnrealMcpCaptureRedactionTests::SerializeObject(Payload);
		TestFalse(TEXT("Payload metadata excludes secret value."), PayloadJson.Contains(TEXT("secret-payload-value")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCaptureActivityLogEventIdTest,
	"UnrealMcp.Capture.ActivityLogEventId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCaptureActivityLogEventIdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SessionId = FString::Printf(TEXT("capture-eventid-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower());
	const FString ActivityLogPath = UnrealMcpCaptureRedactionTests::MakeActivityLogPath(SessionId);

	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*ActivityLogPath, false, true, true);
	};

	{
		UnrealMcp::FActivityLogEvent Event;
		Event.EventKind = TEXT("tool_call");
		Event.Summary = TEXT("Synthetic capture event.");
		Event.Payload = UnrealMcpCaptureRedactionTests::MakeObject();
		FString FailureReason;
		TestTrue(TEXT("ActivityLog writes generated eventId event."), UnrealMcp::TryWriteActivityEventForSession(SessionId, Event, FailureReason));
		if (!FailureReason.IsEmpty())
		{
			AddError(FailureReason);
		}
	}

	const FString ExplicitEventId = TEXT("capture-explicit-event-id");
	{
		UnrealMcp::FActivityLogEvent Event;
		Event.EventId = ExplicitEventId;
		Event.EventKind = TEXT("tool_call");
		Event.Summary = TEXT("Synthetic capture event with explicit eventId.");
		Event.Payload = UnrealMcpCaptureRedactionTests::MakeObject();
		FString FailureReason;
		TestTrue(TEXT("ActivityLog writes explicit eventId event."), UnrealMcp::TryWriteActivityEventForSession(SessionId, Event, FailureReason));
		if (!FailureReason.IsEmpty())
		{
			AddError(FailureReason);
		}
	}

	TArray<FString> Lines;
	TestTrue(TEXT("ActivityLog file loads."), FFileHelper::LoadFileToStringArray(Lines, *ActivityLogPath));
	TestEqual(TEXT("ActivityLog test wrote two lines."), Lines.Num(), 2);
	if (Lines.Num() < 2)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> GeneratedRecord = UnrealMcpCaptureRedactionTests::ParseJsonLine(Lines[0]);
	const TSharedPtr<FJsonObject> ExplicitRecord = UnrealMcpCaptureRedactionTests::ParseJsonLine(Lines[1]);
	FString GeneratedEventId;
	TestTrue(TEXT("Generated eventId exists."), GeneratedRecord.IsValid() && GeneratedRecord->TryGetStringField(TEXT("eventId"), GeneratedEventId));
	TestFalse(TEXT("Generated eventId is non-empty."), GeneratedEventId.IsEmpty());
	TestEqual(TEXT("Generated eventId has hyphenated GUID length."), GeneratedEventId.Len(), 36);
	TestEqual(TEXT("Explicit eventId is preserved."), ExplicitRecord.IsValid() ? ExplicitRecord->GetStringField(TEXT("eventId")) : FString(), ExplicitEventId);

	return true;
}

#endif
