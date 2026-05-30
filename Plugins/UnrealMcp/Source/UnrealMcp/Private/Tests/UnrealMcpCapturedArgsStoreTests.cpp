#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
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
#include "UnrealMcpCapturedArgsStore.h"
#include "UnrealMcpCaptureRedaction.h"

namespace UnrealMcpCapturedArgsStoreTests
{
	TSharedPtr<FJsonObject> MakeObject()
	{
		return MakeShared<FJsonObject>();
	}

	FString MakeTestId(const FString& Prefix)
	{
		return FString::Printf(TEXT("%s-%s"), *Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToLower());
	}

	FString MakeCapturePathFromRef(const FString& CaptureRef)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp"), CaptureRef));
	}

	FString MakeCapturePath(const FString& SessionId, const FString& EventId)
	{
		return MakeCapturePathFromRef(FPaths::Combine(TEXT("CapturedToolArgs"), SessionId, EventId + TEXT(".json")));
	}

	FString MakeActivityLogPath(const FString& SessionId)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/ActivityLog"), SessionId + TEXT(".jsonl")));
	}

	void DeleteCaptureSession(const FString& SessionId)
	{
		const FString SessionRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			UnrealMcp::CapturedArgsStore::GetCapturedArgsRoot(),
			SessionId));
		IFileManager::Get().DeleteDirectory(*SessionRoot, false, true);
	}

	FString SerializeObject(const TSharedPtr<FJsonObject>& Object)
	{
		FString Json;
		if (!Object.IsValid())
		{
			return Json;
		}
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Json;
	}

	FString NormalizeForCompare(FString Path)
	{
		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Path);
		Path.RemoveFromEnd(TEXT("/"));
		return Path;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCapturedArgsStoreRoundtripTest,
	"UnrealMcp.Capture.StoreRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCapturedArgsStoreRoundtripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SessionId = UnrealMcpCapturedArgsStoreTests::MakeTestId(TEXT("capture-store"));
	const FString EventId = UnrealMcpCapturedArgsStoreTests::MakeTestId(TEXT("event"));
	ON_SCOPE_EXIT
	{
		UnrealMcpCapturedArgsStoreTests::DeleteCaptureSession(SessionId);
	};

	TSharedPtr<FJsonObject> Args = UnrealMcpCapturedArgsStoreTests::MakeObject();
	Args->SetStringField(TEXT("password"), TEXT("CANARY_SECRET_XYZ"));
	Args->SetStringField(TEXT("filter"), TEXT("StaticMesh"));
	const UnrealMcp::CaptureRedaction::FRedactionResult Redacted =
		UnrealMcp::CaptureRedaction::SanitizeToolArguments_Pure(TEXT("unreal.editor_status"), Args, 4096, 65536);

	FString CaptureRef;
	FString CaptureSha256;
	TestTrue(TEXT("Write captured args."), UnrealMcp::CapturedArgsStore::WriteCapturedArgs(
		SessionId,
		EventId,
		TEXT("unreal.editor_status"),
		TEXT("2026-05-31T00:00:00Z"),
		Redacted,
		CaptureRef,
		CaptureSha256));
	TestFalse(TEXT("captureRef is set."), CaptureRef.IsEmpty());
	TestEqual(TEXT("capture sha length."), CaptureSha256.Len(), 64);

	TSharedPtr<FJsonObject> ReadContent;
	FString ReadError;
	TestTrue(TEXT("Read captured args."), UnrealMcp::CapturedArgsStore::ReadCapturedArgs(CaptureRef, ReadContent, ReadError));
	if (!ReadError.IsEmpty())
	{
		AddError(ReadError);
	}
	TestTrue(TEXT("Captured content is valid."), ReadContent.IsValid());
	if (ReadContent.IsValid())
	{
		TestEqual(TEXT("eventId roundtrips."), ReadContent->GetStringField(TEXT("eventId")), EventId);
		TestEqual(TEXT("sessionId roundtrips."), ReadContent->GetStringField(TEXT("sessionId")), SessionId);
		TestEqual(TEXT("captureStatus roundtrips."), ReadContent->GetStringField(TEXT("captureStatus")), FString(TEXT("redacted")));
		const TSharedPtr<FJsonObject>* SanitizedArgs = nullptr;
		TestTrue(TEXT("sanitizedArguments is present."), ReadContent->TryGetObjectField(TEXT("sanitizedArguments"), SanitizedArgs) && SanitizedArgs && SanitizedArgs->IsValid());
		if (SanitizedArgs && SanitizedArgs->IsValid())
		{
			TestEqual(TEXT("password is redacted in store."), (*SanitizedArgs)->GetStringField(TEXT("password")), FString(TEXT("<redacted:secret>")));
			TestEqual(TEXT("non-secret arg is retained in private store."), (*SanitizedArgs)->GetStringField(TEXT("filter")), FString(TEXT("StaticMesh")));
		}
	}

	const FString JsonPath = UnrealMcpCapturedArgsStoreTests::MakeCapturePathFromRef(CaptureRef);
	TestTrue(TEXT("Tamper write succeeds."), FFileHelper::SaveStringToFile(TEXT("{\"tampered\":true}"), *JsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	ReadContent.Reset();
	ReadError.Reset();
	TestFalse(TEXT("Tampered captured args fail sha check."), UnrealMcp::CapturedArgsStore::ReadCapturedArgs(CaptureRef, ReadContent, ReadError));
	TestTrue(TEXT("Tamper error mentions sha."), ReadError.Contains(TEXT("sha"), ESearchCase::IgnoreCase));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCapturedArgsStoreSkipsNonCapturedTest,
	"UnrealMcp.Capture.StoreSkipsNonCaptured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCapturedArgsStoreSkipsNonCapturedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SessionId = UnrealMcpCapturedArgsStoreTests::MakeTestId(TEXT("capture-skip"));
	ON_SCOPE_EXIT
	{
		UnrealMcpCapturedArgsStoreTests::DeleteCaptureSession(SessionId);
	};

	const TArray<FString> Statuses = { TEXT("skipped"), TEXT("oversized") };
	for (const FString& Status : Statuses)
	{
		UnrealMcp::CaptureRedaction::FRedactionResult Redacted;
		Redacted.CaptureStatus = Status;
		Redacted.RedactionSummaryPublic = UnrealMcpCapturedArgsStoreTests::MakeObject();

		const FString EventId = UnrealMcpCapturedArgsStoreTests::MakeTestId(Status);
		FString CaptureRef;
		FString CaptureSha256;
		TestTrue(FString::Printf(TEXT("%s status returns success without write."), *Status), UnrealMcp::CapturedArgsStore::WriteCapturedArgs(
			SessionId,
			EventId,
			TEXT("unreal.editor_status"),
			TEXT("2026-05-31T00:00:00Z"),
			Redacted,
			CaptureRef,
			CaptureSha256));
		TestTrue(FString::Printf(TEXT("%s captureRef remains empty."), *Status), CaptureRef.IsEmpty());
		TestTrue(FString::Printf(TEXT("%s captureSha256 remains empty."), *Status), CaptureSha256.IsEmpty());
		TestFalse(FString::Printf(TEXT("%s status does not write a JSON file."), *Status), FPaths::FileExists(UnrealMcpCapturedArgsStoreTests::MakeCapturePath(SessionId, EventId)));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCapturedArgsStorePathOutsideRagTest,
	"UnrealMcp.Capture.StorePathOutsideRag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCapturedArgsStorePathOutsideRagTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString StoreRoot = UnrealMcpCapturedArgsStoreTests::NormalizeForCompare(UnrealMcp::CapturedArgsStore::GetCapturedArgsRoot());
	const FString ActivityRoot = UnrealMcpCapturedArgsStoreTests::NormalizeForCompare(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/ActivityLog")));
	const FString KnowledgeDocumentsRoot = UnrealMcpCapturedArgsStoreTests::NormalizeForCompare(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/KnowledgeSources")));

	TestFalse(TEXT("CapturedToolArgs is not the ActivityLog root."), StoreRoot.Equals(ActivityRoot, ESearchCase::IgnoreCase));
	TestFalse(TEXT("CapturedToolArgs is not under ActivityLog."), StoreRoot.StartsWith(ActivityRoot + TEXT("/"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("CapturedToolArgs is not under KnowledgeSources."), StoreRoot.StartsWith(KnowledgeDocumentsRoot + TEXT("/"), ESearchCase::IgnoreCase));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCapturedArgsRagCanaryTest,
	"UnrealMcp.Capture.RagCanary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCapturedArgsRagCanaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SessionId = UnrealMcpCapturedArgsStoreTests::MakeTestId(TEXT("capture-rag-canary"));
	const FString EventId = UnrealMcpCapturedArgsStoreTests::MakeTestId(TEXT("event"));
	const FString ActivityLogPath = UnrealMcpCapturedArgsStoreTests::MakeActivityLogPath(SessionId);
	ON_SCOPE_EXIT
	{
		UnrealMcpCapturedArgsStoreTests::DeleteCaptureSession(SessionId);
		IFileManager::Get().Delete(*ActivityLogPath, false, true, true);
	};

	const FString SecretCanary = TEXT("CANARY_SECRET_XYZ");
	const FString PrivateNonSecretCanary = TEXT("PRIVATE_CAPTURE_ONLY_VALUE_XYZ");
	TSharedPtr<FJsonObject> Args = UnrealMcpCapturedArgsStoreTests::MakeObject();
	Args->SetStringField(TEXT("password"), SecretCanary);
	Args->SetStringField(TEXT("note"), PrivateNonSecretCanary);
	const UnrealMcp::CaptureRedaction::FRedactionResult Redacted =
		UnrealMcp::CaptureRedaction::SanitizeToolArguments_Pure(TEXT("unreal.editor_status"), Args, 4096, 65536);
	TestEqual(TEXT("Canary args are redacted."), Redacted.CaptureStatus, FString(TEXT("redacted")));
	TestTrue(TEXT("Sanitized args retain private non-secret content only in private store."), Redacted.SanitizedArguments.IsValid() && Redacted.SanitizedArguments->GetStringField(TEXT("note")) == PrivateNonSecretCanary);
	TestEqual(TEXT("Secret canary is redacted before store."), Redacted.SanitizedArguments.IsValid() ? Redacted.SanitizedArguments->GetStringField(TEXT("password")) : FString(), FString(TEXT("<redacted:secret>")));

	FString CaptureRef;
	FString CaptureSha256;
	TestTrue(TEXT("Write canary captured args."), UnrealMcp::CapturedArgsStore::WriteCapturedArgs(
		SessionId,
		EventId,
		TEXT("unreal.editor_status"),
		TEXT("2026-05-31T00:00:00Z"),
		Redacted,
		CaptureRef,
		CaptureSha256));
	TestFalse(TEXT("Canary captureRef is set."), CaptureRef.IsEmpty());
	TestFalse(TEXT("Canary captureSha256 is set."), CaptureSha256.IsEmpty());

	TSharedPtr<FJsonObject> Payload = UnrealMcpCapturedArgsStoreTests::MakeObject();
	Payload->SetStringField(TEXT("toolName"), TEXT("unreal.editor_status"));
	Payload->SetNumberField(TEXT("captureSchemaVersion"), UnrealMcp::CaptureRedaction::kCaptureSchemaVersion);
	Payload->SetStringField(TEXT("captureStatus"), Redacted.CaptureStatus);
	Payload->SetObjectField(TEXT("redactionSummaryPublic"), Redacted.RedactionSummaryPublic);
	Payload->SetStringField(TEXT("captureRef"), CaptureRef);
	Payload->SetStringField(TEXT("captureSha256"), CaptureSha256);

	UnrealMcp::FActivityLogEvent Event;
	Event.EventId = EventId;
	Event.EventKind = TEXT("tool_call");
	Event.Summary = TEXT("RAG canary capture event.");
	Event.Payload = Payload;
	FString FailureReason;
	TestTrue(TEXT("Write canary ActivityLog event."), UnrealMcp::TryWriteActivityEventForSession(SessionId, Event, FailureReason));
	if (!FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}

	TArray<FString> ActivityLines;
	TestTrue(TEXT("Read canary ActivityLog."), FFileHelper::LoadFileToStringArray(ActivityLines, *ActivityLogPath));
	FString ActivityText = FString::Join(ActivityLines, TEXT("\n"));
	TestFalse(TEXT("ActivityLog does not contain secret canary."), ActivityText.Contains(SecretCanary));
	TestFalse(TEXT("ActivityLog does not contain private captured arg value."), ActivityText.Contains(PrivateNonSecretCanary));

	TSharedPtr<FJsonObject> StoredContent;
	FString ReadError;
	TestTrue(TEXT("Read canary private store."), UnrealMcp::CapturedArgsStore::ReadCapturedArgs(CaptureRef, StoredContent, ReadError));
	if (!ReadError.IsEmpty())
	{
		AddError(ReadError);
	}
	const FString StoredJson = UnrealMcpCapturedArgsStoreTests::SerializeObject(StoredContent);
	TestFalse(TEXT("Private store does not contain secret canary after redaction."), StoredJson.Contains(SecretCanary));
	TestTrue(TEXT("Private store contains non-secret canary outside RAG paths."), StoredJson.Contains(PrivateNonSecretCanary));

	const FString StoreJsonPath = UnrealMcpCapturedArgsStoreTests::NormalizeForCompare(UnrealMcpCapturedArgsStoreTests::MakeCapturePathFromRef(CaptureRef));
	const FString ActivityRoot = UnrealMcpCapturedArgsStoreTests::NormalizeForCompare(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/ActivityLog")));
	TArray<FString> RagActivityFiles;
	IFileManager::Get().FindFilesRecursive(RagActivityFiles, *ActivityRoot, TEXT("*.jsonl"), true, false);
	for (const FString& ActivityFile : RagActivityFiles)
	{
		TestFalse(TEXT("RAG ActivityLog enumeration does not include CapturedToolArgs file."), UnrealMcpCapturedArgsStoreTests::NormalizeForCompare(ActivityFile).Equals(StoreJsonPath, ESearchCase::IgnoreCase));
	}
	AddInfo(TEXT("RagCanary used the automation-safe fallback: ActivityLog enumeration excludes CapturedToolArgs and payload/store redaction excludes the secret canary."));

	return true;
}

#endif
