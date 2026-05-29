#include "UnrealMcpCallToolLibrary.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpCallToolPolicy.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpToolRegistry.h"

namespace UnrealMcpCallToolLibraryLocal
{
	constexpr int32 MaxTextChars = 20000;
	static thread_local int32 CallToolDepthCounter = 0;

	struct FScopedCallToolDepth
	{
		FScopedCallToolDepth()
		{
			++CallToolDepthCounter;
		}

		~FScopedCallToolDepth()
		{
			--CallToolDepthCounter;
		}

		static int32 Current()
		{
			return CallToolDepthCounter;
		}
	};

	FString SerializePayload(const TSharedRef<FJsonObject>& Payload)
	{
		FString JsonString;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(Payload, Writer);
		return JsonString;
	}

	const TCHAR* DecisionToString(UnrealMcp::ECallToolDecision Decision)
	{
		switch (Decision)
		{
		case UnrealMcp::ECallToolDecision::Allow:
			return TEXT("allow");
		case UnrealMcp::ECallToolDecision::ForceDryRun:
			return TEXT("force_dry_run");
		case UnrealMcp::ECallToolDecision::Deny:
		default:
			return TEXT("deny");
		}
	}

	TSharedPtr<FJsonObject> MakeMetaPayload(
		UnrealMcp::ECallToolDecision Decision,
		bool bForcedDryRun,
		bool bTruncated,
		const FString& Reason)
	{
		TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
		Meta->SetStringField(TEXT("policyDecision"), DecisionToString(Decision));
		Meta->SetBoolField(TEXT("forcedDryRun"), bForcedDryRun);
		Meta->SetBoolField(TEXT("truncated"), bTruncated);
		Meta->SetStringField(TEXT("reason"), Reason);
		return Meta;
	}

	FString MakeErrorPayload(
		const FString& ToolName,
		const FString& Reason,
		UnrealMcp::ECallToolDecision Decision = UnrealMcp::ECallToolDecision::Deny,
		bool bForcedDryRun = false)
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("toolName"), ToolName);
		Payload->SetStringField(TEXT("text"), Reason);
		Payload->SetBoolField(TEXT("isError"), true);
		Payload->SetField(TEXT("structuredContent"), MakeShared<FJsonValueNull>());
		Payload->SetObjectField(TEXT("meta"), MakeMetaPayload(Decision, bForcedDryRun, false, Reason));
		return SerializePayload(Payload);
	}

	FString MakeResultPayload(
		const FString& ToolName,
		const FUnrealMcpExecutionResult& Result,
		UnrealMcp::ECallToolDecision Decision,
		bool bForcedDryRun)
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("toolName"), ToolName);

		bool bTruncated = false;
		FString Text = Result.Text;
		if (Text.Len() > MaxTextChars)
		{
			Text = Text.Left(MaxTextChars);
			bTruncated = true;
		}
		Payload->SetStringField(TEXT("text"), Text);
		Payload->SetBoolField(TEXT("isError"), Result.bIsError);
		if (Result.StructuredContent.IsValid())
		{
			Payload->SetObjectField(TEXT("structuredContent"), Result.StructuredContent);
		}
		else
		{
			Payload->SetField(TEXT("structuredContent"), MakeShared<FJsonValueNull>());
		}
		Payload->SetObjectField(TEXT("meta"), MakeMetaPayload(Decision, bForcedDryRun, bTruncated, FString()));
		return SerializePayload(Payload);
	}

	UnrealMcp::FCallToolTargetFacts GatherFacts(const FString& ToolName)
	{
		UnrealMcp::FCallToolTargetFacts Facts;
		if (const UnrealMcp::FToolRegistryEntry* Entry = UnrealMcp::FindToolRegistryEntry(ToolName))
		{
			Facts.bVisible = Entry->Exposure == UnrealMcp::EToolExposure::Visible;
		}

		Facts.SourceKind = UnrealMcp::ResolveToolSourceKind(ToolName);
		const UnrealMcp::FToolPolicy Policy = UnrealMcp::GetToolPolicy(ToolName);
		Facts.RiskLevel = Policy.RiskLevel;
		Facts.bRequiresLock = Policy.bRequiresLock;
		Facts.bRequiresWrite = Policy.bRequiresWrite;
		Facts.bRequiresRestart = Policy.bRequiresRestart;
		Facts.bRequiresExternalProcess = Policy.bRequiresExternalProcess;
		Facts.bRequiresBuild = Policy.bRequiresBuild;
		Facts.bDryRunSupport = Policy.bDryRunSupport;
		Facts.bIsWorkflowRun = ToolName == TEXT("unreal.workflow_run");
		Facts.Depth = FScopedCallToolDepth::Current();
		return Facts;
	}
}

FString UUnrealMcpCallToolLibrary::CallTool(const FString& ToolName, const FString& ArgumentsJson)
{
	TSharedPtr<FJsonObject> ParsedArguments;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
	if (!FJsonSerializer::Deserialize(Reader, ParsedArguments) || !ParsedArguments.IsValid())
	{
		return UnrealMcpCallToolLibraryLocal::MakeErrorPayload(ToolName, TEXT("invalid_arguments_json"));
	}

	const UnrealMcp::FCallToolTargetFacts Facts = UnrealMcpCallToolLibraryLocal::GatherFacts(ToolName);
	const UnrealMcp::FCallToolPolicyResult PolicyResult = UnrealMcp::ClassifyCallToolTarget_Pure(Facts);
	if (PolicyResult.Decision == UnrealMcp::ECallToolDecision::Deny)
	{
		return UnrealMcpCallToolLibraryLocal::MakeErrorPayload(
			ToolName,
			PolicyResult.Reason,
			PolicyResult.Decision,
			PolicyResult.bForcedDryRun);
	}

	if (PolicyResult.Decision == UnrealMcp::ECallToolDecision::ForceDryRun)
	{
		ParsedArguments->SetBoolField(TEXT("dryRun"), true);
	}

	UnrealMcpCallToolLibraryLocal::FScopedCallToolDepth Guard;
	FUnrealMcpModule* Module = FModuleManager::GetModulePtr<FUnrealMcpModule>(FName(TEXT("UnrealMcp")));
	if (!Module)
	{
		return UnrealMcpCallToolLibraryLocal::MakeErrorPayload(
			ToolName,
			TEXT("module_unavailable"),
			PolicyResult.Decision,
			PolicyResult.bForcedDryRun);
	}

	const FUnrealMcpExecutionResult Result = Module->ExecuteToolFromEditorUI(ToolName, *ParsedArguments);
	return UnrealMcpCallToolLibraryLocal::MakeResultPayload(
		ToolName,
		Result,
		PolicyResult.Decision,
		PolicyResult.bForcedDryRun);
}
