#include "STaskAtlasWindow.h"

#include "UnrealMcpCapturedArgsStore.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpUserToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TaskAtlasWindow"

namespace UnrealMcp::TaskAtlasComposite
{
	namespace
	{
		struct FCompositeStep
		{
			FString ToolName;
			int32 Ordinal = 0;
			FString EventId;
			FString CaptureRef;
			FString CaptureStatus;
			FString PolicyClassAtCapture;
			FString CaptureReadStatus = TEXT("not_applicable");
			FString CaptureReadError;
			TSharedPtr<FJsonObject> CapturedArgs;
			bool bFromStepRef = false;
			bool bHasCapturedArgs = false;
		};

		bool TaskAtlasCompositeIsValidToolId(const FString& ToolId, FString& OutReason)
		{
			if (ToolId.IsEmpty())
			{
				OutReason = TEXT("Tool id is required.");
				return false;
			}
			if (ToolId.Len() > 64)
			{
				OutReason = TEXT("Tool id must be 64 characters or fewer.");
				return false;
			}
			if (ToolId.Contains(TEXT("/"), ESearchCase::CaseSensitive)
				|| ToolId.Contains(TEXT("\\"), ESearchCase::CaseSensitive)
				|| ToolId.Contains(TEXT(".."), ESearchCase::CaseSensitive)
				|| ToolId.Contains(TEXT(":"), ESearchCase::CaseSensitive)
				|| ToolId.StartsWith(TEXT("//"), ESearchCase::CaseSensitive)
				|| ToolId.StartsWith(TEXT("\\\\"), ESearchCase::CaseSensitive)
				|| FPaths::IsRelative(ToolId) == false)
			{
				OutReason = TEXT("Tool id must be a safe single directory name without slashes, traversal, drives, or UNC paths.");
				return false;
			}
			for (const TCHAR Character : ToolId)
			{
				const bool bLowerAlpha = Character >= TEXT('a') && Character <= TEXT('z');
				const bool bUpperAlpha = Character >= TEXT('A') && Character <= TEXT('Z');
				const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
				if (!bLowerAlpha && !bUpperAlpha && !bDigit && Character != TEXT('_'))
				{
					OutReason = TEXT("Tool id may contain only alphanumeric characters and underscores.");
					return false;
				}
			}
			return true;
		}

		FString TaskAtlasCompositePythonQuote(const FString& Value)
		{
			FString Result = TEXT("\"");
			for (const TCHAR Character : Value)
			{
				if (Character == TEXT('\\'))
				{
					Result += TEXT("\\\\");
				}
				else if (Character == TEXT('"'))
				{
					Result += TEXT("\\\"");
				}
				else if (Character == TEXT('\n'))
				{
					Result += TEXT("\\n");
				}
				else if (Character == TEXT('\r'))
				{
					Result += TEXT("\\r");
				}
				else if (Character == TEXT('\t'))
				{
					Result += TEXT("\\t");
				}
				else
				{
					Result.AppendChar(Character);
				}
			}
			Result += TEXT("\"");
			return Result;
		}

		FString TaskAtlasCompositeJsonToString(const TSharedPtr<FJsonObject>& Object)
		{
			FString Output;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
			return Output;
		}

		bool TaskAtlasCompositeIsReplayableCaptureStatus(const FString& CaptureStatus)
		{
			const FString Normalized = CaptureStatus.TrimStartAndEnd().ToLower();
			return Normalized == TEXT("captured") || Normalized == TEXT("redacted");
		}

		void TaskAtlasCompositeReadCapturedArgs(FCompositeStep& Step)
		{
			if (Step.CaptureRef.TrimStartAndEnd().IsEmpty()
				|| !TaskAtlasCompositeIsReplayableCaptureStatus(Step.CaptureStatus))
			{
				Step.CaptureReadStatus = TEXT("placeholder");
				return;
			}

			TSharedPtr<FJsonObject> CapturedContent;
			FString ReadError;
			if (!UnrealMcp::CapturedArgsStore::ReadCapturedArgs(Step.CaptureRef, CapturedContent, ReadError))
			{
				Step.CaptureReadStatus = TEXT("read_failed");
				Step.CaptureReadError = ReadError.TrimStartAndEnd();
				return;
			}

			const TSharedPtr<FJsonObject>* SanitizedArgs = nullptr;
			if (!CapturedContent.IsValid()
				|| !CapturedContent->TryGetObjectField(TEXT("sanitizedArguments"), SanitizedArgs)
				|| !SanitizedArgs
				|| !(*SanitizedArgs).IsValid())
			{
				Step.CaptureReadStatus = TEXT("read_failed");
				Step.CaptureReadError = TEXT("Captured args file is missing object field 'sanitizedArguments'.");
				return;
			}

			Step.CapturedArgs = *SanitizedArgs;
			Step.bHasCapturedArgs = true;
			Step.CaptureReadStatus = TEXT("captured");
		}

		FString TaskAtlasCompositeGetStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
		{
			FString Value;
			if (Object.IsValid())
			{
				Object->TryGetStringField(FieldName, Value);
			}
			return Value.TrimStartAndEnd();
		}

		int32 TaskAtlasCompositeGetOrdinalField(const TSharedPtr<FJsonObject>& Object, int32 DefaultOrdinal)
		{
			double Ordinal = static_cast<double>(DefaultOrdinal);
			if (Object.IsValid())
			{
				Object->TryGetNumberField(TEXT("ordinal"), Ordinal);
			}
			return FMath::TruncToInt(Ordinal);
		}

		TSharedPtr<FJsonObject> TaskAtlasCompositeStepToJson(const FCompositeStep& Step)
		{
			TSharedPtr<FJsonObject> StepObject = MakeShared<FJsonObject>();
			StepObject->SetNumberField(TEXT("ordinal"), Step.Ordinal);
			StepObject->SetStringField(TEXT("tool"), Step.ToolName);
			if (!Step.EventId.IsEmpty())
			{
				StepObject->SetStringField(TEXT("eventId"), Step.EventId);
			}
			if (!Step.CaptureStatus.IsEmpty())
			{
				StepObject->SetStringField(TEXT("captureStatus"), Step.CaptureStatus);
			}
			if (!Step.CaptureRef.IsEmpty())
			{
				StepObject->SetStringField(TEXT("captureRef"), Step.CaptureRef);
			}
			if (!Step.PolicyClassAtCapture.IsEmpty())
			{
				StepObject->SetStringField(TEXT("policyClassAtCapture"), Step.PolicyClassAtCapture);
			}
			StepObject->SetBoolField(TEXT("hasCapturedArgs"), Step.bHasCapturedArgs);
			StepObject->SetStringField(TEXT("captureReadStatus"), Step.CaptureReadStatus);
			if (!Step.CaptureReadError.IsEmpty())
			{
				StepObject->SetStringField(TEXT("captureReadError"), Step.CaptureReadError);
			}
			return StepObject;
		}
		}

	FString NormalizeReplayEligibility(const FString& ReplayEligibility)
	{
		const FString Trimmed = ReplayEligibility.TrimStartAndEnd();
		if (Trimmed == TEXT("preview_ready")
			|| Trimmed == TEXT("partial")
			|| Trimmed == TEXT("blocked")
			|| Trimmed == TEXT("skeleton_pre_capture"))
		{
			return Trimmed;
		}
		return TEXT("skeleton_pre_capture");
	}

	FString ReplayStatusForReplayEligibility(const FString& ReplayEligibility)
	{
		const FString Normalized = NormalizeReplayEligibility(ReplayEligibility);
		return Normalized == TEXT("preview_ready") ? FString(TEXT("preview_only")) : Normalized;
	}

	FString ReplayStatusSummary(const FString& ReplayEligibility, const FString& ReplayUnavailableReason)
	{
		const FString Status = ReplayStatusForReplayEligibility(ReplayEligibility);
		const FString Reason = ReplayUnavailableReason.TrimStartAndEnd();
		if (Status == TEXT("preview_only"))
		{
			return TEXT("preview_only: generated a captured-argument preview composite; write-capable steps remain dry-run only");
		}
		if (Status == TEXT("partial"))
		{
			return Reason.IsEmpty()
				? FString(TEXT("partial: generated a preview composite with placeholders because at least one step lacks captured arguments"))
				: FString::Printf(TEXT("partial: generated a preview composite with placeholders because %s"), *Reason);
		}
		if (Status == TEXT("blocked"))
		{
			return Reason.IsEmpty()
				? FString(TEXT("blocked: generated a preview composite, but at least one step is denied by call_tool policy"))
				: FString::Printf(TEXT("blocked: generated a preview composite, but %s"), *Reason);
		}
		return Reason.IsEmpty()
			? FString(TEXT("skeleton_pre_capture: generated skeleton because no captured arguments are available"))
			: FString::Printf(TEXT("skeleton_pre_capture: generated skeleton because %s"), *Reason);
	}

	FString ComputePythonHandlerSha256(const FString& MainPy)
	{
		return UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(MainPy);
	}

	FString JsonObjectToCondensedString(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return TEXT("{}");
		}

		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	bool BuildCompositeUserToolFiles(
		const FString& ToolId,
		const FString& Title,
		const FString& Description,
		const FString& TaskId,
		const FString& ReplayEligibility,
		const FString& ReplayUnavailableReason,
		const TArray<FString>& CriticalPath,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs,
		const TSet<FString>& VisibleCoreToolNames,
		FString& OutToolName,
		FString& OutMainPy,
		FString& OutMainPySha256,
		FString& OutToolJson,
		TSharedPtr<FJsonObject>& OutSmokeArgs,
		TArray<FString>& OutStepTools,
		FString& OutFailureReason)
	{
		OutToolName.Reset();
		OutMainPy.Reset();
		OutMainPySha256.Reset();
		OutToolJson.Reset();
		OutSmokeArgs = MakeShared<FJsonObject>();
		OutStepTools.Reset();
		OutFailureReason.Reset();

		if (!TaskAtlasCompositeIsValidToolId(ToolId, OutFailureReason))
		{
			return false;
		}

		TArray<FCompositeStep> Steps;
		Steps.Reserve(StepRefs.Num() > 0 ? StepRefs.Num() : CriticalPath.Num());

		for (int32 Index = 0; Index < StepRefs.Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& StepValue = StepRefs[Index];
			const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
			if (!StepObject.IsValid())
			{
				continue;
			}

			FCompositeStep Step;
			Step.bFromStepRef = true;
			Step.Ordinal = TaskAtlasCompositeGetOrdinalField(StepObject, Index);
			Step.ToolName = TaskAtlasCompositeGetStringField(StepObject, TEXT("tool"));
			Step.EventId = TaskAtlasCompositeGetStringField(StepObject, TEXT("eventId"));
			Step.CaptureStatus = TaskAtlasCompositeGetStringField(StepObject, TEXT("captureStatus"));
			Step.CaptureRef = TaskAtlasCompositeGetStringField(StepObject, TEXT("captureRef"));
			Step.PolicyClassAtCapture = TaskAtlasCompositeGetStringField(StepObject, TEXT("policyClassAtCapture"));
			if (!Step.ToolName.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive)
				|| !VisibleCoreToolNames.Contains(Step.ToolName))
			{
				continue;
			}

			TaskAtlasCompositeReadCapturedArgs(Step);
			Steps.Add(Step);
		}

		TSet<FString> SeenTools;
		if (Steps.Num() == 0)
		{
			for (const FString& RawToolName : CriticalPath)
			{
				const FString ToolName = RawToolName.TrimStartAndEnd();
				if (!ToolName.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive)
					|| !VisibleCoreToolNames.Contains(ToolName)
					|| SeenTools.Contains(ToolName))
				{
					continue;
				}

				SeenTools.Add(ToolName);
				FCompositeStep Step;
				Step.ToolName = ToolName;
				Step.Ordinal = Steps.Num();
				Step.CaptureReadStatus = TEXT("placeholder");
				Steps.Add(Step);
			}
		}

		bool bHasAnyCapturedArgs = false;
		for (const FCompositeStep& Step : Steps)
		{
			OutStepTools.Add(Step.ToolName);
			bHasAnyCapturedArgs = bHasAnyCapturedArgs || Step.bHasCapturedArgs;
		}

		if (OutStepTools.Num() == 0)
		{
			OutFailureReason = TEXT("Make Tool Set requires at least one visible core unreal.* tool in the selected workflow critical path.");
			return false;
		}

		OutToolName = FString::Printf(TEXT("user.%s"), *ToolId);
		const FString NormalizedReplayEligibility = NormalizeReplayEligibility(ReplayEligibility);
		const FString CompositeKind = bHasAnyCapturedArgs ? FString(TEXT("preview")) : FString(TEXT("skeleton"));
		const FString ReplayStatus = ReplayStatusForReplayEligibility(NormalizedReplayEligibility);
		const FString ReplaySummary = ReplayStatusSummary(NormalizedReplayEligibility, ReplayUnavailableReason);

		TSharedPtr<FJsonObject> CapturedDefaults = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> CapturedMeta = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> StepRefValues;
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FString StepKey = FString::Printf(TEXT("step%d"), Index);
			const FCompositeStep& Step = Steps[Index];
			if (Step.bHasCapturedArgs)
			{
				CapturedDefaults->SetObjectField(StepKey, Step.CapturedArgs);
			}
			CapturedMeta->SetObjectField(StepKey, TaskAtlasCompositeStepToJson(Step));
			StepRefValues.Add(MakeShared<FJsonValueObject>(TaskAtlasCompositeStepToJson(Step)));
		}

		const FString CapturedDefaultsJson = JsonObjectToCondensedString(CapturedDefaults);
		const FString CapturedMetaJson = JsonObjectToCondensedString(CapturedMeta);

		OutMainPy += TEXT("# Generated by Task Atlas as a preview composite user tool.\n");
		OutMainPy += FString::Printf(TEXT("# Replay status: %s.\n"), *ReplaySummary);
		OutMainPy += TEXT("# Preview only: read-only/dry-run steps execute, and write-capable steps are forced to dryRun by call_tool policy.\n");
		OutMainPy += TEXT("# This is not real replay; captured arguments are embedded only as JSON data defaults.\n\n");
		OutMainPy += TEXT("import json\n\n");
		OutMainPy += FString::Printf(TEXT("_CAPTURED_JSON = %s\n"), *TaskAtlasCompositePythonQuote(CapturedDefaultsJson));
		OutMainPy += FString::Printf(TEXT("_CAPTURED_META_JSON = %s\n"), *TaskAtlasCompositePythonQuote(CapturedMetaJson));
		OutMainPy += TEXT("_CAPTURED = json.loads(_CAPTURED_JSON)\n");
		OutMainPy += TEXT("_CAPTURED_META = json.loads(_CAPTURED_META_JSON)\n");
		OutMainPy += TEXT("_MISSING = object()\n\n");
		OutMainPy += TEXT("def _json_copy(value):\n");
		OutMainPy += TEXT("    return json.loads(json.dumps(value or {}))\n\n");
		OutMainPy += TEXT("def _step_args(args, override_key, captured_key):\n");
		OutMainPy += TEXT("    if override_key in args and args.get(override_key) is not None:\n");
		OutMainPy += TEXT("        value = args.get(override_key)\n");
		OutMainPy += TEXT("    else:\n");
		OutMainPy += TEXT("        value = _CAPTURED.get(captured_key, {})\n");
		OutMainPy += TEXT("    if not isinstance(value, dict):\n");
		OutMainPy += TEXT("        value = {}\n");
		OutMainPy += TEXT("    return _json_copy(value)\n\n");
		OutMainPy += TEXT("def _effective_args(requested, result):\n");
		OutMainPy += TEXT("    effective = _json_copy(requested)\n");
		OutMainPy += TEXT("    meta = result.get(\"meta\", {}) if isinstance(result, dict) else {}\n");
		OutMainPy += TEXT("    if meta.get(\"forcedDryRun\") and effective.get(\"dryRun\") is not True:\n");
		OutMainPy += TEXT("        effective[\"dryRun\"] = True\n");
		OutMainPy += TEXT("    return effective\n\n");
		OutMainPy += TEXT("def _args_diff(requested, effective):\n");
		OutMainPy += TEXT("    requested = requested or {}\n");
		OutMainPy += TEXT("    effective = effective or {}\n");
		OutMainPy += TEXT("    diff = {}\n");
		OutMainPy += TEXT("    for key in sorted(set(requested.keys()) | set(effective.keys())):\n");
		OutMainPy += TEXT("        requested_value = requested[key] if key in requested else _MISSING\n");
		OutMainPy += TEXT("        effective_value = effective[key] if key in effective else _MISSING\n");
		OutMainPy += TEXT("        if requested_value == effective_value:\n");
		OutMainPy += TEXT("            continue\n");
		OutMainPy += TEXT("        entry = {}\n");
		OutMainPy += TEXT("        if requested_value is _MISSING:\n");
		OutMainPy += TEXT("            entry[\"requestedMissing\"] = True\n");
		OutMainPy += TEXT("        else:\n");
		OutMainPy += TEXT("            entry[\"requested\"] = requested_value\n");
		OutMainPy += TEXT("        if effective_value is _MISSING:\n");
		OutMainPy += TEXT("            entry[\"effectiveMissing\"] = True\n");
		OutMainPy += TEXT("        else:\n");
		OutMainPy += TEXT("            entry[\"effective\"] = effective_value\n");
		OutMainPy += TEXT("        diff[key] = entry\n");
		OutMainPy += TEXT("    return diff\n\n");
		OutMainPy += TEXT("def _record_step(tool, captured_key, result, requested, effective):\n");
		OutMainPy += TEXT("    result = result or {}\n");
		OutMainPy += TEXT("    meta = result.get(\"meta\", {}) if isinstance(result, dict) else {}\n");
		OutMainPy += TEXT("    step = {\n");
		OutMainPy += TEXT("        \"tool\": tool,\n");
		OutMainPy += TEXT("        \"policyDecision\": meta.get(\"policyDecision\", \"unknown\"),\n");
		OutMainPy += TEXT("        \"isError\": bool(result.get(\"isError\", True)),\n");
		OutMainPy += TEXT("    }\n");
		OutMainPy += TEXT("    if meta.get(\"forcedDryRun\"):\n");
		OutMainPy += TEXT("        step[\"forcedDryRun\"] = True\n");
		OutMainPy += TEXT("    capture = _CAPTURED_META.get(captured_key)\n");
		OutMainPy += TEXT("    if capture:\n");
		OutMainPy += TEXT("        step[\"capture\"] = capture\n");
		OutMainPy += TEXT("    diff = _args_diff(requested, effective)\n");
		OutMainPy += TEXT("    if diff:\n");
		OutMainPy += TEXT("        step[\"effectiveArgsDiff\"] = diff\n");
		OutMainPy += TEXT("    return step\n\n");
		OutMainPy += TEXT("def execute(args):\n");
		OutMainPy += TEXT("    args = args or {}\n");
		OutMainPy += TEXT("    steps = []\n");
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FString StepName = FString::Printf(TEXT("step%d_args"), Index);
			const FString CapturedKey = FString::Printf(TEXT("step%d"), Index);
			const FString StepIndex = FString::FromInt(Index);
			const FString QuotedTool = TaskAtlasCompositePythonQuote(Steps[Index].ToolName);
			const FString QuotedStepName = TaskAtlasCompositePythonQuote(StepName);
			const FString QuotedCapturedKey = TaskAtlasCompositePythonQuote(CapturedKey);
			OutMainPy += FString::Printf(TEXT("    step%d_requested = _step_args(args, %s, %s)\n"), Index, *QuotedStepName, *QuotedCapturedKey);
			OutMainPy += FString::Printf(TEXT("    r%d = call_tool_raw(%s, step%d_requested)\n"), Index, *QuotedTool, Index);
			OutMainPy += FString::Printf(TEXT("    step%d_effective = _effective_args(step%d_requested, r%d)\n"), Index, Index, Index);
			OutMainPy += FString::Printf(TEXT("    steps.append(_record_step(%s, %s, r%d, step%d_requested, step%d_effective))\n"), *QuotedTool, *QuotedCapturedKey, Index, Index, Index);
			OutMainPy += FString::Printf(TEXT("    if r%d.get(\"isError\", False):\n"), Index);
			OutMainPy += FString::Printf(TEXT("        return {\"isError\": True, \"text\": \"composite stopped at step %s\", \"structuredContent\": {\"steps\": steps}}\n"), *StepIndex);
		}
		OutMainPy += FString::Printf(TEXT("    return {\"isError\": False, \"text\": \"preview composite executed %d steps\", \"structuredContent\": {\"steps\": steps}}\n"), Steps.Num());

		OutMainPySha256 = ComputePythonHandlerSha256(OutMainPy);

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FString StepName = FString::Printf(TEXT("step%d_args"), Index);
			TSharedPtr<FJsonObject> StepSchema = MakeShared<FJsonObject>();
			StepSchema->SetStringField(TEXT("type"), TEXT("object"));
			StepSchema->SetStringField(
				TEXT("description"),
				Steps[Index].bHasCapturedArgs
					? FString::Printf(TEXT("Optional override for %s. If omitted, the sanitized captured arguments for step %d are used."), *Steps[Index].ToolName, Index)
					: FString::Printf(TEXT("Arguments forwarded to %s. Placeholder step; fill reviewed args before preview."), *Steps[Index].ToolName));
			StepSchema->SetBoolField(TEXT("additionalProperties"), true);
			Properties->SetObjectField(StepName, StepSchema);
			if (!Steps[Index].bHasCapturedArgs)
			{
				OutSmokeArgs->SetObjectField(StepName, MakeShared<FJsonObject>());
			}
		}

		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		InputSchema->SetObjectField(TEXT("properties"), Properties);
		InputSchema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>());
		InputSchema->SetBoolField(TEXT("additionalProperties"), false);

		TArray<TSharedPtr<FJsonValue>> CriticalPathValues;
		for (const FString& ToolName : OutStepTools)
		{
			CriticalPathValues.Add(MakeShared<FJsonValueString>(ToolName));
		}

		TSharedPtr<FJsonObject> ToolJson = MakeShared<FJsonObject>();
		ToolJson->SetStringField(TEXT("name"), OutToolName);
		ToolJson->SetStringField(TEXT("title"), Title.TrimStartAndEnd().IsEmpty() ? ToolId : Title.TrimStartAndEnd());
		ToolJson->SetStringField(TEXT("description"), Description);
		ToolJson->SetStringField(TEXT("pythonHandlerSha256"), OutMainPySha256);
		ToolJson->SetArrayField(TEXT("importAllowlist"), TArray<TSharedPtr<FJsonValue>>());
		ToolJson->SetObjectField(TEXT("inputSchema"), InputSchema);
		ToolJson->SetObjectField(TEXT("smokeArgs"), OutSmokeArgs);
		ToolJson->SetStringField(TEXT("generator"), TEXT("task-atlas-composite"));
		ToolJson->SetStringField(TEXT("compositeKind"), CompositeKind);
		ToolJson->SetStringField(TEXT("replayStatus"), ReplayStatus);
		ToolJson->SetStringField(TEXT("replayEligibility"), NormalizedReplayEligibility);
		ToolJson->SetStringField(TEXT("replayUnavailableReason"), ReplayUnavailableReason.TrimStartAndEnd());
		ToolJson->SetStringField(TEXT("taskId"), TaskId.TrimStartAndEnd());
		ToolJson->SetArrayField(TEXT("criticalPath"), CriticalPathValues);
		ToolJson->SetArrayField(TEXT("stepRefs"), StepRefValues);

		OutToolJson = TaskAtlasCompositeJsonToString(ToolJson);
		return true;
	}

	bool WriteCompositeUserToolFiles(
		const FString& ToolId,
		const FString& MainPy,
		const FString& ToolJson,
		bool bOverwrite,
		FString& OutDirectory,
		FString& OutFailureReason)
	{
		OutDirectory.Reset();
		OutFailureReason.Reset();

		if (!TaskAtlasCompositeIsValidToolId(ToolId, OutFailureReason))
		{
			return false;
		}

		UserRegistry::InitializeUserToolRegistry();
		FString RootDir = UserRegistry::GetUserToolsRootDir();
		FPaths::NormalizeFilename(RootDir);
		RootDir.RemoveFromEnd(TEXT("/"));

		FString ToolDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(RootDir, ToolId));
		FPaths::NormalizeFilename(ToolDir);
		FPaths::CollapseRelativeDirectories(ToolDir);
		ToolDir.RemoveFromEnd(TEXT("/"));

		const FString RootPrefix = RootDir + TEXT("/");
		if (!ToolDir.StartsWith(RootPrefix, ESearchCase::IgnoreCase))
		{
			OutFailureReason = TEXT("Refused to write composite user tool outside the user-tool registry root.");
			return false;
		}

		if (IFileManager::Get().DirectoryExists(*ToolDir))
		{
			if (!bOverwrite)
			{
				OutFailureReason = FString::Printf(TEXT("Composite user tool directory already exists: %s"), *ToolDir);
				return false;
			}
			if (!IFileManager::Get().DeleteDirectory(*ToolDir, false, true))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to replace existing composite user tool directory: %s"), *ToolDir);
				return false;
			}
		}

		if (!IFileManager::Get().MakeDirectory(*ToolDir, true))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to create composite user tool directory: %s"), *ToolDir);
			return false;
		}

		const FString MainPyPath = FPaths::Combine(ToolDir, TEXT("main.py"));
		const FString ToolJsonPath = FPaths::Combine(ToolDir, TEXT("tool.json"));
		if (!FFileHelper::SaveStringToFile(MainPy, *MainPyPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to write composite main.py: %s"), *MainPyPath);
			return false;
		}
		if (!FFileHelper::SaveStringToFile(ToolJson, *ToolJsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to write composite tool.json: %s"), *ToolJsonPath);
			return false;
		}

		OutDirectory = ToolDir;
		return true;
	}
}

namespace TaskAtlasWindow
{
	FString JsonObjectToPrettyString(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return TEXT("{}");
		}

		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	FString GetStringField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, const FString& Fallback = FString())
	{
		FString Value;
		if (Object.IsValid() && Object->TryGetStringField(FieldName, Value))
		{
			return Value;
		}
		return Fallback;
	}

	bool GetBoolField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, bool bFallback = false)
	{
		bool bValue = bFallback;
		if (Object.IsValid())
		{
			Object->TryGetBoolField(FieldName, bValue);
		}
		return bValue;
	}

	TArray<FString> GetStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		TArray<FString> Values;
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, ArrayValues) || !ArrayValues)
		{
			return Values;
		}
		for (const TSharedPtr<FJsonValue>& Value : *ArrayValues)
		{
			if (Value.IsValid() && Value->Type == EJson::String)
			{
				Values.Add(Value->AsString());
			}
		}
		return Values;
	}

	int32 WorkflowSortRank(const STaskAtlasWindow::FWorkflowRow& Row)
	{
		if (Row.bPinned)
		{
			return 0;
		}
		if (Row.Rating == TEXT("success"))
		{
			return 1;
		}
		return 2;
	}

	FString JoinCriticalPath(const TArray<FString>& CriticalPath)
	{
		TArray<FString> DisplayTools;
		for (int32 Index = 0; Index < CriticalPath.Num() && Index < 10; ++Index)
		{
			DisplayTools.Add(CriticalPath[Index]);
		}
		FString Text = FString::Join(DisplayTools, TEXT(" -> "));
		if (CriticalPath.Num() > 10)
		{
			Text += TEXT(" -> ...");
		}
		return Text;
	}

	FString TrimDashes(FString Value)
	{
		while (Value.StartsWith(TEXT("-")))
		{
			Value.RightChopInline(1);
		}
		while (Value.EndsWith(TEXT("-")))
		{
			Value.LeftChopInline(1);
		}
		return Value;
	}

	void AppendNormalizedDash(FString& OutValue)
	{
		if (!OutValue.IsEmpty() && !OutValue.EndsWith(TEXT("-")))
		{
			OutValue.AppendChar('-');
		}
	}

	FString TrimUnderscores(FString Value)
	{
		while (Value.StartsWith(TEXT("_")))
		{
			Value.RightChopInline(1);
		}
		while (Value.EndsWith(TEXT("_")))
		{
			Value.LeftChopInline(1);
		}
		return Value;
	}

	void AppendNormalizedUnderscore(FString& OutValue)
	{
		if (!OutValue.IsEmpty() && !OutValue.EndsWith(TEXT("_")))
		{
			OutValue.AppendChar('_');
		}
	}

	FString SanitizeToolIdPart(const FString& Source)
	{
		FString Slug;
		for (const TCHAR Ch : Source)
		{
			if (Ch >= 'A' && Ch <= 'Z')
			{
				Slug.AppendChar(static_cast<TCHAR>(Ch + ('a' - 'A')));
			}
			else if ((Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9'))
			{
				Slug.AppendChar(Ch);
			}
			else
			{
				AppendNormalizedUnderscore(Slug);
			}
		}
		return TrimUnderscores(Slug);
	}

	FString MakeAtlasToolId(const FString& Label, const FString& TaskId)
	{
		FString ToolId = SanitizeToolIdPart(Label);
		if (ToolId.IsEmpty())
		{
			ToolId = SanitizeToolIdPart(TaskId);
		}
		if (ToolId.IsEmpty())
		{
			ToolId = TEXT("workflow");
		}
		ToolId = FString::Printf(TEXT("atlas_%s"), *ToolId);
		if (ToolId.Len() > 64)
		{
			ToolId = TrimUnderscores(ToolId.Left(64));
		}
		return ToolId.IsEmpty() ? FString(TEXT("atlas_workflow")) : ToolId;
	}

	FString SanitizeSkillSlugPart(const FString& Source)
	{
		FString Slug;
		for (const TCHAR Ch : Source)
		{
			if (Ch >= 'A' && Ch <= 'Z')
			{
				Slug.AppendChar(static_cast<TCHAR>(Ch + ('a' - 'A')));
			}
			else if ((Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9'))
			{
				Slug.AppendChar(Ch);
			}
			else if (FChar::IsWhitespace(Ch) || Ch == '_' || Ch == '-')
			{
				AppendNormalizedDash(Slug);
			}
		}

		Slug = TrimDashes(Slug);
		if (Slug.Len() > 64)
		{
			Slug = TrimDashes(Slug.Left(64));
		}
		return Slug;
	}

	FString MakeSkillSlug(const FString& Label, const FString& TaskId)
	{
		FString Slug = SanitizeSkillSlugPart(Label);
		if (Slug.IsEmpty() && !TaskId.TrimStartAndEnd().IsEmpty())
		{
			Slug = SanitizeSkillSlugPart(FString::Printf(TEXT("task-%s"), *TaskId));
		}
		if (Slug.IsEmpty())
		{
			Slug = TEXT("task-atlas-workflow");
		}
		return Slug;
	}

	FString SanitizeTaskFilename(const FString& TaskId)
	{
		FString SafeName;
		for (const TCHAR Ch : TaskId)
		{
			if ((Ch >= 'A' && Ch <= 'Z')
				|| (Ch >= 'a' && Ch <= 'z')
				|| (Ch >= '0' && Ch <= '9')
				|| Ch == '-'
				|| Ch == '_'
				|| Ch == '.')
			{
				SafeName.AppendChar(Ch);
			}
			else
			{
				AppendNormalizedDash(SafeName);
			}
		}

		SafeName = TrimDashes(SafeName);
		if (SafeName.Len() > 128)
		{
			SafeName = TrimDashes(SafeName.Left(128));
		}
		if (SafeName.IsEmpty() || SafeName == TEXT(".") || SafeName == TEXT(".."))
		{
			SafeName = TEXT("task-atlas-workflow");
		}
		return SafeName;
	}

	FString RedactHomePaths(FString Text)
	{
		const FString Replacement = TEXT("<HOME>/");
		auto RedactPrefix = [&Text, &Replacement](const FString& Prefix)
		{
			int32 SearchStart = 0;
			while (SearchStart < Text.Len())
			{
				const int32 PrefixIndex = Text.Find(Prefix, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
				if (PrefixIndex == INDEX_NONE)
				{
					break;
				}

				const int32 NameStart = PrefixIndex + Prefix.Len();
				const int32 SlashIndex = Text.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
				if (SlashIndex == INDEX_NONE)
				{
					break;
				}
				if (SlashIndex == NameStart)
				{
					SearchStart = NameStart + 1;
					continue;
				}

				Text = Text.Left(PrefixIndex) + Replacement + Text.Mid(SlashIndex + 1);
				SearchStart = PrefixIndex + Replacement.Len();
			}
		};

		RedactPrefix(TEXT("/Users/"));
		RedactPrefix(TEXT("/home/"));
		return Text;
	}

	FString MarkdownValue(const FString& Value, bool bRedact = true)
	{
		const FString CleanValue = (bRedact ? RedactHomePaths(Value) : Value).TrimStartAndEnd();
		return CleanValue.IsEmpty() ? FString(TEXT("(empty)")) : CleanValue;
	}

	FString BoolText(bool bValue)
	{
		return bValue ? FString(TEXT("true")) : FString(TEXT("false"));
	}

	void AppendMetadataLine(FString& OutMarkdown, const FString& FieldName, const FString& Value, bool bRedact = true)
	{
		OutMarkdown += FString::Printf(TEXT("- %s: %s\n"), *FieldName, *MarkdownValue(Value, bRedact));
	}

	void AppendTextSection(FString& OutMarkdown, const FString& Heading, const FString& Value)
	{
		OutMarkdown += FString::Printf(TEXT("## %s\n\n%s\n\n"), *Heading, *MarkdownValue(Value));
	}

	bool HasAdditionalEventRefFields(const TSharedPtr<FJsonObject>& EventObject)
	{
		if (!EventObject.IsValid())
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : EventObject->Values)
		{
			if (Pair.Key != TEXT("ts") && Pair.Key != TEXT("tool") && Pair.Key != TEXT("isError"))
			{
				return true;
			}
		}
		return false;
	}

	void AppendEventRefs(FString& OutMarkdown, const TSharedPtr<FJsonObject>& TaskJson)
	{
		OutMarkdown += TEXT("## Event Refs\n\n");

		const TArray<TSharedPtr<FJsonValue>>* EventRefs = nullptr;
		if (!TaskJson.IsValid() || !TaskJson->TryGetArrayField(TEXT("eventRefs"), EventRefs) || !EventRefs || EventRefs->Num() == 0)
		{
			OutMarkdown += TEXT("- (none)\n\n");
			return;
		}

		for (int32 Index = 0; Index < EventRefs->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& EventValue = (*EventRefs)[Index];
			if (!EventValue.IsValid() || EventValue->Type != EJson::Object || !EventValue->AsObject().IsValid())
			{
				OutMarkdown += FString::Printf(TEXT("- event %d: (non-object ref)\n"), Index + 1);
				continue;
			}

			const TSharedPtr<FJsonObject> EventObject = EventValue->AsObject();
			const FString Ts = RedactHomePaths(GetStringField(EventObject, TEXT("ts"), TEXT("(missing)")));
			const FString Tool = RedactHomePaths(GetStringField(EventObject, TEXT("tool"), TEXT("(missing)")));
			bool bEventIsError = false;
			const FString ErrorText = EventObject->TryGetBoolField(TEXT("isError"), bEventIsError)
				? BoolText(bEventIsError)
				: FString(TEXT("(missing)"));
			OutMarkdown += FString::Printf(TEXT("- event %d: ts=%s, tool=%s, isError=%s\n"), Index + 1, *Ts, *Tool, *ErrorText);

			if (HasAdditionalEventRefFields(EventObject))
			{
				const FString JsonText = RedactHomePaths(JsonObjectToPrettyString(EventObject)).TrimStartAndEnd();
				if (!JsonText.IsEmpty())
				{
					OutMarkdown += TEXT("  details:\n\n");
					OutMarkdown += TEXT("```json\n");
					OutMarkdown += JsonText;
					OutMarkdown += TEXT("\n```\n");
				}
			}
		}
		OutMarkdown += TEXT("\n");
	}

	FString BuildTaskKnowledgeMarkdown(const STaskAtlasWindow::FWorkflowRow& Row)
	{
		const TSharedPtr<FJsonObject>& TaskJson = Row.Json;
		const FString TaskId = GetStringField(TaskJson, TEXT("taskId"), Row.TaskId);
		const FString Label = GetStringField(TaskJson, TEXT("label"), Row.Label);
		const FString Rating = GetStringField(TaskJson, TEXT("rating"), Row.Rating.IsEmpty() ? TEXT("unrated") : Row.Rating);
		const bool bPinned = GetBoolField(TaskJson, TEXT("pinned"), Row.bPinned);
		const FString SessionId = GetStringField(TaskJson, TEXT("sessionId"));
		const FString TStartUtc = GetStringField(TaskJson, TEXT("tStartUtc"));
		const FString TEndUtc = GetStringField(TaskJson, TEXT("tEndUtc"), Row.TEndUtc);
		const FString UserIntent = GetStringField(TaskJson, TEXT("userIntentText"));
		const FString AiSummary = GetStringField(TaskJson, TEXT("aiSummaryText"));
		TArray<FString> CriticalPath = GetStringArrayField(TaskJson, TEXT("criticalPath"));
		if (CriticalPath.Num() == 0)
		{
			CriticalPath = Row.CriticalPath;
		}

		FString Markdown;
		Markdown += TEXT("# Task Atlas Workflow\n\n");
		Markdown += TEXT("## Metadata\n\n");
		AppendMetadataLine(Markdown, TEXT("taskId"), TaskId, false);
		AppendMetadataLine(Markdown, TEXT("label"), Label);
		AppendMetadataLine(Markdown, TEXT("rating"), Rating, false);
		AppendMetadataLine(Markdown, TEXT("pinned"), BoolText(bPinned), false);
		AppendMetadataLine(Markdown, TEXT("sessionId"), SessionId, false);
		AppendMetadataLine(Markdown, TEXT("tStartUtc"), TStartUtc, false);
		AppendMetadataLine(Markdown, TEXT("tEndUtc"), TEndUtc, false);
		Markdown += TEXT("\n");

		AppendTextSection(Markdown, TEXT("User Intent"), UserIntent);
		AppendTextSection(Markdown, TEXT("AI Summary"), AiSummary);

		Markdown += TEXT("## Critical Path Tools\n\n");
		if (CriticalPath.Num() == 0)
		{
			Markdown += TEXT("- (none)\n\n");
		}
		else
		{
			for (const FString& ToolName : CriticalPath)
			{
				Markdown += FString::Printf(TEXT("- %s\n"), *MarkdownValue(ToolName));
			}
			Markdown += TEXT("\n");
		}

		AppendEventRefs(Markdown, TaskJson);
		return Markdown;
	}

	bool BuildTaskAtlasKnowledgeSourcePath(const FString& TaskId, FString& OutPath, FString& OutFailureReason)
	{
		FString SourceRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("UnrealMcp/KnowledgeSources/TaskAtlas")));
		FPaths::NormalizeDirectoryName(SourceRoot);

		if (!IFileManager::Get().MakeDirectory(*SourceRoot, true))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to create Task Atlas knowledge source directory: %s"), *SourceRoot);
			return false;
		}

		const FString SafeName = SanitizeTaskFilename(TaskId);
		FString SourcePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SourceRoot, SafeName + TEXT(".md")));
		FPaths::NormalizeFilename(SourcePath);

		const FString RootPrefix = SourceRoot + TEXT("/");
		if (!SourcePath.StartsWith(RootPrefix, ESearchCase::IgnoreCase))
		{
			OutFailureReason = TEXT("Refused to write Task Atlas knowledge source outside Saved/UnrealMcp/KnowledgeSources/TaskAtlas.");
			return false;
		}

		OutPath = SourcePath;
		return true;
	}

	bool IsTaskAtlasCompositeUserTool(const UnrealMcp::UserRegistry::FUserToolEntry& Entry, FString& OutCompositeKind)
	{
		OutCompositeKind.Reset();
		if (!Entry.ToolJson.IsValid())
		{
			return false;
		}

		FString Generator;
		if (!Entry.ToolJson->TryGetStringField(TEXT("generator"), Generator)
			|| Generator != TEXT("task-atlas-composite"))
		{
			return false;
		}

		Entry.ToolJson->TryGetStringField(TEXT("compositeKind"), OutCompositeKind);
		OutCompositeKind = OutCompositeKind.TrimStartAndEnd();
		if (OutCompositeKind.IsEmpty())
		{
			OutCompositeKind = TEXT("unknown");
		}
		return true;
	}

	FString NormalizeAbsoluteDirectory(const FString& Directory)
	{
		if (Directory.TrimStartAndEnd().IsEmpty())
		{
			return FString();
		}

		FString Result = FPaths::ConvertRelativePathToFull(Directory);
		FPaths::NormalizeFilename(Result);
		FPaths::CollapseRelativeDirectories(Result);
		Result.RemoveFromEnd(TEXT("/"));
		return Result;
	}

	FString MakeRelativeDisplayPath(const FString& AbsolutePath, const FString& FallbackRoot)
	{
		FString ProjectDir = NormalizeAbsoluteDirectory(FPaths::ProjectDir());
		FString RelativePath = AbsolutePath;
		if (!ProjectDir.IsEmpty())
		{
			const FString ProjectPrefix = ProjectDir + TEXT("/");
			if (AbsolutePath.StartsWith(ProjectPrefix, ESearchCase::IgnoreCase))
			{
				const FString ProjectBase = ProjectDir + TEXT("/");
				if (FPaths::MakePathRelativeTo(RelativePath, *ProjectBase))
				{
					FPaths::NormalizeFilename(RelativePath);
					return RelativePath;
				}
			}
		}

		RelativePath = AbsolutePath;
		const FString FallbackBase = FallbackRoot + TEXT("/");
		if (FPaths::MakePathRelativeTo(RelativePath, *FallbackBase))
		{
			FPaths::NormalizeFilename(RelativePath);
			return RelativePath;
		}
		return AbsolutePath;
	}

	bool ResolveUserToolScaffoldDir(
		const FString& InputScaffoldDir,
		FString& OutScaffoldDir,
		FString& OutRelativeScaffoldDir,
		FString& OutFailureReason)
	{
		OutScaffoldDir.Reset();
		OutRelativeScaffoldDir.Reset();
		OutFailureReason.Reset();

		const FString RootDir = NormalizeAbsoluteDirectory(UnrealMcp::UserRegistry::GetUserToolsRootDir());
		if (RootDir.IsEmpty())
		{
			OutFailureReason = TEXT("User-tool registry root is empty.");
			return false;
		}

		const FString ScaffoldDir = NormalizeAbsoluteDirectory(InputScaffoldDir);
		if (ScaffoldDir.IsEmpty())
		{
			OutFailureReason = TEXT("User tool scaffold directory is empty.");
			return false;
		}

		const FString RootPrefix = RootDir + TEXT("/");
		if (ScaffoldDir.Equals(RootDir, ESearchCase::IgnoreCase)
			|| !ScaffoldDir.StartsWith(RootPrefix, ESearchCase::IgnoreCase))
		{
			OutFailureReason = FString::Printf(
				TEXT("Refused to delete user tool outside the registry root: %s"),
				*ScaffoldDir);
			return false;
		}

		OutScaffoldDir = ScaffoldDir;
		OutRelativeScaffoldDir = MakeRelativeDisplayPath(ScaffoldDir, RootDir);
		return true;
	}
}

void STaskAtlasWindow::Construct(const FArguments& InArgs, FUnrealMcpModule* InOwnerModule)
{
	OwnerModule = InOwnerModule;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Task Atlas"))
					.Font(FAppStyle::GetFontStyle("HeadingMedium"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.OnClicked(this, &STaskAtlasWindow::HandleRefreshClicked)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 8.0f)
			[
				SAssignNew(StatusText, STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("StatusInitial", "Loading Task Atlas."))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)
				+ SSplitter::Slot()
				.Value(0.64f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(0.58f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Workflows", "Workflows"))
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(WorkflowListBox, SVerticalBox)
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(0.42f)
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("UnusedTools", "Unused Tools"))
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(UnusedToolListBox, SVerticalBox)
								]
							]
						]
					]
				]
				+ SSplitter::Slot()
				.Value(0.36f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(0.62f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(10.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(ToolDetailTitle, STextBlock)
								.Text(LOCTEXT("ToolDetailTitle", "Tool Details"))
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SAssignNew(ToolDetailMeta, STextBlock)
								.AutoWrapText(true)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Text(LOCTEXT("ToolDetailMeta", "Select a tool name to inspect registry metadata."))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 10.0f, 0.0f, 0.0f)
							[
								SAssignNew(ToolDetailDescription, STextBlock)
								.AutoWrapText(true)
								.Text(FText::GetEmpty())
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 10.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(ToolDetailSchema, STextBlock)
									.AutoWrapText(true)
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.Text(FText::GetEmpty())
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(0.38f)
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("MadeTools", "Made Tools"))
									.Font(FAppStyle::GetFontStyle("NormalFontBold"))
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(8.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SButton)
									.Text(LOCTEXT("RefreshMadeTools", "Refresh"))
									.OnClicked(this, &STaskAtlasWindow::HandleRefreshClicked)
								]
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(MadeToolListBox, SVerticalBox)
								]
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("SearchHint", "Search workflows and tools"))
				.OnTextChanged(this, &STaskAtlasWindow::HandleSearchChanged)
			]
		]
	];

	RefreshData();
}

FReply STaskAtlasWindow::HandleRefreshClicked()
{
	RefreshData();
	return FReply::Handled();
}

void STaskAtlasWindow::HandleSearchChanged(const FText& NewText)
{
	SearchText = NewText.ToString().TrimStartAndEnd().ToLower();
	RebuildLists();
}

FReply STaskAtlasWindow::HandlePinClicked(FString TaskId, bool bNewPinned)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("taskId"), TaskId);
	Arguments->SetBoolField(TEXT("pinned"), bNewPinned);
	const FUnrealMcpExecutionResult Result = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.task_pin"), *Arguments);
	SetStatus(Result.Text);
	if (!Result.bIsError)
	{
		RefreshData();
	}
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleMakeToolClicked(FWorkflowRow Row)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}

	const FString TaskLabel = Row.Label.TrimStartAndEnd();
	const FString ToolId = TaskAtlasWindow::MakeAtlasToolId(TaskLabel, Row.TaskId);
	const FString Title = TaskLabel.IsEmpty() ? ToolId : TaskLabel;
	const FString ReplayEligibility = UnrealMcp::TaskAtlasComposite::NormalizeReplayEligibility(Row.ReplayEligibility);
	const FString ReplaySummary = UnrealMcp::TaskAtlasComposite::ReplayStatusSummary(ReplayEligibility, Row.ReplayUnavailableReason);
	const FString CriticalPathText = TaskAtlasWindow::JoinCriticalPath(Row.CriticalPath);
	const FString CriticalPathDescription = CriticalPathText.IsEmpty()
		? FString(TEXT("No critical path tools were recorded."))
		: FString::Printf(TEXT("Critical path: %s."), *CriticalPathText);
	const FString Description = FString::Printf(
		TEXT("Task Atlas composite for workflow '%s'. %s Generated from the visible core-tool critical path. %s."),
		*Title,
		*CriticalPathDescription,
		*ReplaySummary);

	TSet<FString> VisibleCoreToolNames;
	for (const UnrealMcp::FToolRegistryEntry& Entry : UnrealMcp::GetToolRegistryEntries())
	{
		if (Entry.Exposure == UnrealMcp::EToolExposure::Visible && Entry.Name.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive))
		{
			VisibleCoreToolNames.Add(Entry.Name);
		}
	}

	FString ToolName;
	FString MainPy;
	FString MainPySha256;
	FString ToolJson;
	TSharedPtr<FJsonObject> SmokeArgs;
	TArray<FString> StepTools;
	FString FailureReason;
	TArray<TSharedPtr<FJsonValue>> StepRefs;
	const TArray<TSharedPtr<FJsonValue>>* StepRefArray = nullptr;
	if (Row.Json.IsValid() && Row.Json->TryGetArrayField(TEXT("stepRefs"), StepRefArray) && StepRefArray)
	{
		StepRefs = *StepRefArray;
	}
	if (!UnrealMcp::TaskAtlasComposite::BuildCompositeUserToolFiles(
		ToolId,
		Title,
		Description,
		Row.TaskId,
		ReplayEligibility,
		Row.ReplayUnavailableReason,
		Row.CriticalPath,
		StepRefs,
		VisibleCoreToolNames,
		ToolName,
		MainPy,
		MainPySha256,
		ToolJson,
		SmokeArgs,
		StepTools,
		FailureReason))
	{
		SetStatus(FailureReason.IsEmpty() ? FString(TEXT("Make Tool Set failed to generate a composite user tool.")) : FailureReason);
		return FReply::Handled();
	}

	FString Directory;
	if (!UnrealMcp::TaskAtlasComposite::WriteCompositeUserToolFiles(ToolId, MainPy, ToolJson, false, Directory, FailureReason))
	{
		SetStatus(FailureReason.IsEmpty() ? FString(TEXT("Make Tool Set failed to write the composite user tool.")) : FailureReason);
		return FReply::Handled();
	}

	TSharedPtr<FJsonObject> ReloadArguments = MakeShared<FJsonObject>();
	ReloadArguments->SetBoolField(TEXT("acceptChangedHashes"), true);
	const FUnrealMcpExecutionResult ReloadResult = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.mcp_user_registry_reload"), *ReloadArguments);
	if (ReloadResult.bIsError)
	{
		SetStatus(FString::Printf(
			TEXT("Composite user tool written at %s (%s), but registry reload failed: %s"),
			*Directory,
			*ReplaySummary,
			ReloadResult.Text.IsEmpty() ? TEXT("unreal.mcp_user_registry_reload failed.") : *ReloadResult.Text));
		return FReply::Handled();
	}

	TSharedPtr<FJsonObject> SmokeArguments = MakeShared<FJsonObject>();
	SmokeArguments->SetStringField(TEXT("toolName"), ToolName);
	SmokeArguments->SetStringField(TEXT("dryRunArgs"), UnrealMcp::TaskAtlasComposite::JsonObjectToCondensedString(SmokeArgs));
	SmokeArguments->SetNumberField(TEXT("timeoutSeconds"), 15.0);
	const FUnrealMcpExecutionResult SmokeResult = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.mcp_user_tool_smoke"), *SmokeArguments);
	if (SmokeResult.bIsError)
	{
		RefreshMadeTools();
		RebuildLists();
		SetStatus(FString::Printf(
			TEXT("Composite user tool %s written at %s and reloaded (%s), but smoke failed: %s"),
			*ToolName,
			*Directory,
			*ReplaySummary,
			SmokeResult.Text.IsEmpty() ? TEXT("unreal.mcp_user_tool_smoke failed.") : *SmokeResult.Text));
		return FReply::Handled();
	}

	RefreshMadeTools();
	RebuildLists();
	SetStatus(FString::Printf(
		TEXT("Composite user tool %s generated at %s, reloaded, and smoke-tested. %s."),
		*ToolName,
		*Directory,
		*ReplaySummary));
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandlePromoteToSkillsClicked(FWorkflowRow Row)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}
	if (Row.TaskId.TrimStartAndEnd().IsEmpty())
	{
		SetStatus(TEXT("Distill Skill requires a taskId on the selected workflow."));
		return FReply::Handled();
	}

	const FString SessionId = TaskAtlasWindow::GetStringField(Row.Json, TEXT("sessionId"));
	if (SessionId.TrimStartAndEnd().IsEmpty())
	{
		SetStatus(TEXT("Distill Skill requires a sessionId on the selected workflow."));
		return FReply::Handled();
	}

	const FString SkillName = TaskAtlasWindow::MakeSkillSlug(Row.Label, Row.TaskId);
	const FString UserIntent = TaskAtlasWindow::GetStringField(Row.Json, TEXT("userIntentText"));
	const FString Goal = UserIntent.TrimStartAndEnd().IsEmpty() ? Row.Label : UserIntent.TrimStartAndEnd();

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("sessionId"), SessionId);
	Arguments->SetStringField(TEXT("skillName"), SkillName);
	Arguments->SetStringField(TEXT("title"), Row.Label);
	Arguments->SetStringField(TEXT("goal"), Goal);
	Arguments->SetBoolField(TEXT("writeDraft"), true);
	Arguments->SetBoolField(TEXT("overwrite"), true);
	Arguments->SetNumberField(TEXT("maxEvents"), 200.0);
	Arguments->SetBoolField(TEXT("includeEvents"), false);

	const FUnrealMcpExecutionResult Result = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.skill_distill_from_activity"), *Arguments);
	if (Result.bIsError)
	{
		SetStatus(Result.Text.IsEmpty() ? FString(TEXT("Distill Skill failed.")) : Result.Text);
		return FReply::Handled();
	}

	const FString DraftPath = TaskAtlasWindow::GetStringField(Result.StructuredContent, TEXT("draftPath"));
	const FString ReturnedSkillName = TaskAtlasWindow::GetStringField(Result.StructuredContent, TEXT("skillName"), SkillName);
	if (!DraftPath.IsEmpty())
	{
		SetStatus(FString::Printf(TEXT("Skill draft '%s' written: %s"), *ReturnedSkillName, *DraftPath));
	}
	else
	{
		SetStatus(Result.Text.IsEmpty() ? FString::Printf(TEXT("Skill draft '%s' written."), *ReturnedSkillName) : Result.Text);
	}
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandlePromoteToRagClicked(FWorkflowRow Row)
{
	FString SourcePath;
	FString FailureReason;
	if (!TaskAtlasWindow::BuildTaskAtlasKnowledgeSourcePath(Row.TaskId, SourcePath, FailureReason))
	{
		SetStatus(FailureReason);
		return FReply::Handled();
	}

	const FString Markdown = TaskAtlasWindow::BuildTaskKnowledgeMarkdown(Row);
	if (!FFileHelper::SaveStringToFile(Markdown, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		SetStatus(FString::Printf(TEXT("Failed to write Task Atlas RAG source: %s"), *SourcePath));
		return FReply::Handled();
	}

	if (!OwnerModule)
	{
		SetStatus(FString::Printf(TEXT("RAG source written: %s. Refresh failed: Task Atlas is not connected to the module."), *SourcePath));
		return FReply::Handled();
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	const FUnrealMcpExecutionResult Result = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.knowledge_index_refresh"), *Arguments);
	if (Result.bIsError)
	{
		const FString ResultText = Result.Text.IsEmpty() ? FString(TEXT("unreal.knowledge_index_refresh failed.")) : Result.Text;
		SetStatus(FString::Printf(TEXT("RAG source written: %s. Refresh failed: %s"), *SourcePath, *ResultText));
		return FReply::Handled();
	}

	const FString ResultText = Result.Text.IsEmpty() ? FString(TEXT("knowledge_index_refresh completed.")) : Result.Text;
	SetStatus(FString::Printf(TEXT("RAG source written: %s. Refresh: %s"), *SourcePath, *ResultText));
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleToolClicked(FString ToolName)
{
	ShowToolDetails(ToolName);
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleDeleteMadeToolClicked(FString ToolName)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}

	UnrealMcp::UserRegistry::InitializeUserToolRegistry();
	const UnrealMcp::UserRegistry::FUserToolEntry* Entry = UnrealMcp::UserRegistry::FindUserTool(ToolName);
	if (!Entry)
	{
		SetStatus(FString::Printf(TEXT("Made tool %s was not found in the user registry."), *ToolName));
		return FReply::Handled();
	}

	FString CompositeKind;
	if (!TaskAtlasWindow::IsTaskAtlasCompositeUserTool(*Entry, CompositeKind))
	{
		SetStatus(FString::Printf(TEXT("Refused to delete %s because it is not a Task Atlas composite user tool."), *ToolName));
		return FReply::Handled();
	}

	FString ScaffoldDir;
	FString RelativeScaffoldDir;
	FString FailureReason;
	if (!TaskAtlasWindow::ResolveUserToolScaffoldDir(Entry->ScaffoldDir, ScaffoldDir, RelativeScaffoldDir, FailureReason))
	{
		SetStatus(FailureReason.IsEmpty() ? FString(TEXT("Refused to delete made tool because its scaffold directory is unsafe.")) : FailureReason);
		return FReply::Handled();
	}

	const EAppReturnType::Type Response = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::Format(
			LOCTEXT("ConfirmDeleteMadeTool", "Delete Task Atlas made tool '{0}'?\n\nDirectory: {1}"),
			FText::FromString(ToolName),
			FText::FromString(RelativeScaffoldDir)));
	if (Response != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const bool bDirectoryExisted = IFileManager::Get().DirectoryExists(*ScaffoldDir);
	if (bDirectoryExisted && !IFileManager::Get().DeleteDirectory(*ScaffoldDir, false, true))
	{
		SetStatus(FString::Printf(TEXT("Failed to delete made tool directory for %s: %s"), *ToolName, *ScaffoldDir));
		return FReply::Handled();
	}

	TSharedPtr<FJsonObject> ReloadArguments = MakeShared<FJsonObject>();
	ReloadArguments->SetBoolField(TEXT("acceptChangedHashes"), true);
	const FUnrealMcpExecutionResult ReloadResult = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.mcp_user_registry_reload"), *ReloadArguments);
	if (ReloadResult.bIsError)
	{
		SetStatus(FString::Printf(
			TEXT("Deleted made tool directory for %s, but registry reload failed: %s"),
			*ToolName,
			ReloadResult.Text.IsEmpty() ? TEXT("unreal.mcp_user_registry_reload failed.") : *ReloadResult.Text));
		return FReply::Handled();
	}

	RefreshMadeTools();
	RebuildLists();
	SetStatus(FString::Printf(
		TEXT("%s Task Atlas made tool %s (%s). Registry reload: %s"),
		bDirectoryExisted ? TEXT("Deleted") : TEXT("Removed stale registry entry for"),
		*ToolName,
		*RelativeScaffoldDir,
		ReloadResult.Text.IsEmpty() ? TEXT("ok") : *ReloadResult.Text));
	return FReply::Handled();
}

void STaskAtlasWindow::RefreshData()
{
	Workflows.Reset();
	Tools.Reset();
	MadeTools.Reset();
	ToolsByName.Reset();

	for (const UnrealMcp::FToolRegistryEntry& Entry : UnrealMcp::GetToolRegistryEntries())
	{
		if (Entry.Exposure != UnrealMcp::EToolExposure::Visible)
		{
			continue;
		}

		FToolRow Tool;
		Tool.Name = Entry.Name;
		Tool.Category = Entry.Category;
		Tool.RiskLevel = UnrealMcp::LexToString(Entry.Policy.RiskLevel);
		Tool.Description = Entry.Description;
		Tool.Owner = Entry.Policy.Owner;
		Tool.DocsPath = Entry.Policy.DocsPath;
		Tool.InputSchemaText = TaskAtlasWindow::JsonObjectToPrettyString(Entry.InputSchema);
		Tools.Add(Tool);
		ToolsByName.Add(Tool.Name, Tool);
	}
	Tools.Sort([](const FToolRow& Left, const FToolRow& Right)
	{
		return Left.Name < Right.Name;
	});
	RefreshMadeTools();

	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		RebuildLists();
		return;
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("filter"), TEXT("all"));
	Arguments->SetNumberField(TEXT("limit"), 200.0);
	const FUnrealMcpExecutionResult Result = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.task_list"), *Arguments);
	if (Result.bIsError || !Result.StructuredContent.IsValid())
	{
		SetStatus(Result.Text.IsEmpty() ? TEXT("Task list failed.") : Result.Text);
		RebuildLists();
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* TaskValues = nullptr;
	if (Result.StructuredContent->TryGetArrayField(TEXT("tasks"), TaskValues) && TaskValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *TaskValues)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object || !Value->AsObject().IsValid())
			{
				continue;
			}

			const TSharedPtr<FJsonObject> TaskObject = Value->AsObject();
			FWorkflowRow Row;
			Row.TaskId = TaskAtlasWindow::GetStringField(TaskObject, TEXT("taskId"));
			Row.Label = TaskAtlasWindow::GetStringField(TaskObject, TEXT("label"), Row.TaskId);
			Row.Rating = TaskAtlasWindow::GetStringField(TaskObject, TEXT("rating"), TEXT("unrated"));
			Row.TEndUtc = TaskAtlasWindow::GetStringField(TaskObject, TEXT("tEndUtc"));
			Row.ReplayEligibility = UnrealMcp::TaskAtlasComposite::NormalizeReplayEligibility(
				TaskAtlasWindow::GetStringField(TaskObject, TEXT("replayEligibility"), TEXT("skeleton_pre_capture")));
			Row.ReplayUnavailableReason = TaskAtlasWindow::GetStringField(TaskObject, TEXT("replayUnavailableReason"));
			Row.bPinned = TaskAtlasWindow::GetBoolField(TaskObject, TEXT("pinned"));
			Row.CriticalPath = TaskAtlasWindow::GetStringArrayField(TaskObject, TEXT("criticalPath"));
			Row.Json = TaskObject;
			Workflows.Add(Row);
		}
	}

	Workflows.Sort([](const FWorkflowRow& Left, const FWorkflowRow& Right)
	{
		const int32 LeftRank = TaskAtlasWindow::WorkflowSortRank(Left);
		const int32 RightRank = TaskAtlasWindow::WorkflowSortRank(Right);
		if (LeftRank != RightRank)
		{
			return LeftRank < RightRank;
		}
		return Left.TEndUtc > Right.TEndUtc;
	});

	SetStatus(FString::Printf(
		TEXT("Loaded %d workflow(s), %d visible tool(s), and %d made tool(s)."),
		Workflows.Num(),
		Tools.Num(),
		MadeTools.Num()));
	RebuildLists();
}

void STaskAtlasWindow::RefreshMadeTools()
{
	MadeTools.Reset();

	UnrealMcp::UserRegistry::InitializeUserToolRegistry();
	for (const UnrealMcp::UserRegistry::FUserToolEntry* Entry : UnrealMcp::UserRegistry::GetAllUserTools())
	{
		if (!Entry)
		{
			continue;
		}

		FString CompositeKind;
		if (!TaskAtlasWindow::IsTaskAtlasCompositeUserTool(*Entry, CompositeKind))
		{
			continue;
		}

		FMadeToolRow Row;
		Row.ToolName = Entry->ToolName;
		Row.CompositeKind = CompositeKind;
		FString FailureReason;
		if (TaskAtlasWindow::ResolveUserToolScaffoldDir(Entry->ScaffoldDir, Row.ScaffoldDir, Row.RelativeScaffoldDir, FailureReason))
		{
			MadeTools.Add(Row);
			continue;
		}

		Row.ScaffoldDir = Entry->ScaffoldDir;
		Row.RelativeScaffoldDir = Entry->ScaffoldDir.IsEmpty()
			? FString(TEXT("<missing scaffold dir>"))
			: FString::Printf(TEXT("%s (outside user-tool root)"), *Entry->ScaffoldDir);
		MadeTools.Add(Row);
	}

	MadeTools.Sort([](const FMadeToolRow& Left, const FMadeToolRow& Right)
	{
		return Left.ToolName < Right.ToolName;
	});
}

void STaskAtlasWindow::RebuildLists()
{
	if (WorkflowListBox.IsValid())
	{
		WorkflowListBox->ClearChildren();
	}
	if (UnusedToolListBox.IsValid())
	{
		UnusedToolListBox->ClearChildren();
	}
	if (MadeToolListBox.IsValid())
	{
		MadeToolListBox->ClearChildren();
	}

	TSet<FString> UsedTools;
	int32 DisplayedWorkflowCount = 0;
	for (const FWorkflowRow& Row : Workflows)
	{
		if (Row.Rating == TEXT("failed") || !WorkflowMatchesSearch(Row))
		{
			continue;
		}

		for (const FString& ToolName : Row.CriticalPath)
		{
			UsedTools.Add(ToolName);
		}
		++DisplayedWorkflowCount;
		if (WorkflowListBox.IsValid())
		{
			WorkflowListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				BuildWorkflowRow(Row)
			];
		}
	}

	if (DisplayedWorkflowCount == 0 && WorkflowListBox.IsValid())
	{
		WorkflowListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoWorkflows", "No matching workflows."))
		];
	}

	int32 DisplayedUnusedCount = 0;
	for (const FToolRow& Tool : Tools)
	{
		if (UsedTools.Contains(Tool.Name) || !ToolMatchesSearch(Tool))
		{
			continue;
		}
		++DisplayedUnusedCount;
		if (UnusedToolListBox.IsValid())
		{
			UnusedToolListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				BuildUnusedToolRow(Tool)
			];
		}
	}

	if (DisplayedUnusedCount == 0 && UnusedToolListBox.IsValid())
	{
		UnusedToolListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoUnusedTools", "No matching unused tools."))
		];
	}

	int32 DisplayedMadeToolCount = 0;
	for (const FMadeToolRow& Row : MadeTools)
	{
		if (!MadeToolMatchesSearch(Row))
		{
			continue;
		}
		++DisplayedMadeToolCount;
		if (MadeToolListBox.IsValid())
		{
			MadeToolListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				BuildMadeToolRow(Row)
			];
		}
	}

	if (DisplayedMadeToolCount == 0 && MadeToolListBox.IsValid())
	{
		MadeToolListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoMadeTools", "No matching made tools."))
		];
	}
}

TSharedRef<SWidget> STaskAtlasWindow::BuildWorkflowRow(const FWorkflowRow& Row)
{
	const FString PinPrefix = Row.bPinned ? TEXT("[PINNED] ") : FString();
	const FString ReplayStatus = UnrealMcp::TaskAtlasComposite::ReplayStatusForReplayEligibility(Row.ReplayEligibility);
	const FString CriticalPathText = TaskAtlasWindow::JoinCriticalPath(Row.CriticalPath);
	TSharedRef<SWrapBox> CriticalPathWrap = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(4.0f, 4.0f));

	if (Row.CriticalPath.Num() == 0)
	{
		CriticalPathWrap->AddSlot()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoCriticalPath", "No tools recorded"))
		];
	}
	else
	{
		const int32 DisplayCount = FMath::Min(Row.CriticalPath.Num(), 10);
		for (int32 Index = 0; Index < DisplayCount; ++Index)
		{
			CriticalPathWrap->AddSlot()
			[
				BuildToolNameButton(Row.CriticalPath[Index])
			];
		}
		if (Row.CriticalPath.Num() > 10)
		{
			CriticalPathWrap->AddSlot()
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("MoreTools", "..."))
			];
		}
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.Text(FText::FromString(PinPrefix + Row.Label))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(FString::Printf(TEXT("%s | %s | %s"), *Row.Rating, *ReplayStatus, *CriticalPathText)))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					CriticalPathWrap
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SWrapBox)
				.InnerSlotPadding(FVector2D(4.0f, 4.0f))
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(FText::FromString(Row.bPinned ? TEXT("Unpin") : TEXT("Pin")))
					.OnClicked(this, &STaskAtlasWindow::HandlePinClicked, Row.TaskId, !Row.bPinned)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("MakeToolSet", "Make Tool Set"))
					.OnClicked(this, &STaskAtlasWindow::HandleMakeToolClicked, Row)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("DistillSkill", "Distill Skill"))
					.OnClicked(this, &STaskAtlasWindow::HandlePromoteToSkillsClicked, Row)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("ToRag", "To RAG"))
					.OnClicked(this, &STaskAtlasWindow::HandlePromoteToRagClicked, Row)
				]
			]
		];
}

TSharedRef<SWidget> STaskAtlasWindow::BuildUnusedToolRow(const FToolRow& Row)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				BuildToolNameButton(Row.Name)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(FString::Printf(TEXT("[%s] [%s]"), *Row.Category, *Row.RiskLevel)))
			]
	];
}

TSharedRef<SWidget> STaskAtlasWindow::BuildMadeToolRow(const FMadeToolRow& Row)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.Text(FText::FromString(Row.ToolName))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(FString::Printf(
						TEXT("%s | %s"),
						*Row.CompositeKind,
						*Row.RelativeScaffoldDir)))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("DeleteMadeTool", "Delete"))
				.OnClicked(this, &STaskAtlasWindow::HandleDeleteMadeToolClicked, Row.ToolName)
			]
		];
}

TSharedRef<SWidget> STaskAtlasWindow::BuildToolNameButton(const FString& ToolName)
{
	return SNew(SButton)
		.ContentPadding(FMargin(6.0f, 2.0f))
		.Text(FText::FromString(ToolName))
		.OnClicked(this, &STaskAtlasWindow::HandleToolClicked, ToolName);
}

void STaskAtlasWindow::ShowToolDetails(const FString& ToolName)
{
	const FToolRow* Tool = ToolsByName.Find(ToolName);
	if (!Tool)
	{
		SetStatus(FString::Printf(TEXT("Tool %s was not found in the registry."), *ToolName));
		return;
	}

	if (ToolDetailTitle.IsValid())
	{
		ToolDetailTitle->SetText(FText::FromString(Tool->Name));
	}
	if (ToolDetailMeta.IsValid())
	{
		ToolDetailMeta->SetText(FText::FromString(FString::Printf(
			TEXT("category: %s | risk: %s | owner: %s | docs: %s"),
			*Tool->Category,
			*Tool->RiskLevel,
			*Tool->Owner,
			*Tool->DocsPath)));
	}
	if (ToolDetailDescription.IsValid())
	{
		ToolDetailDescription->SetText(FText::FromString(Tool->Description));
	}
	if (ToolDetailSchema.IsValid())
	{
		ToolDetailSchema->SetText(FText::FromString(Tool->InputSchemaText));
	}
}

bool STaskAtlasWindow::WorkflowMatchesSearch(const FWorkflowRow& Row) const
{
	if (SearchText.IsEmpty())
	{
		return true;
	}

	if (Row.Label.ToLower().Contains(SearchText)
		|| Row.Rating.ToLower().Contains(SearchText)
		|| Row.ReplayEligibility.ToLower().Contains(SearchText)
		|| Row.ReplayUnavailableReason.ToLower().Contains(SearchText))
	{
		return true;
	}
	for (const FString& ToolName : Row.CriticalPath)
	{
		if (ToolName.ToLower().Contains(SearchText))
		{
			return true;
		}
	}
	return false;
}

bool STaskAtlasWindow::ToolMatchesSearch(const FToolRow& Row) const
{
	if (SearchText.IsEmpty())
	{
		return true;
	}

	return Row.Name.ToLower().Contains(SearchText)
		|| Row.Category.ToLower().Contains(SearchText)
		|| Row.RiskLevel.ToLower().Contains(SearchText)
		|| Row.Description.ToLower().Contains(SearchText);
}

bool STaskAtlasWindow::MadeToolMatchesSearch(const FMadeToolRow& Row) const
{
	if (SearchText.IsEmpty())
	{
		return true;
	}

	return Row.ToolName.ToLower().Contains(SearchText)
		|| Row.CompositeKind.ToLower().Contains(SearchText)
		|| Row.RelativeScaffoldDir.ToLower().Contains(SearchText);
}

void STaskAtlasWindow::SetStatus(const FString& Status)
{
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Status));
	}
}

#undef LOCTEXT_NAMESPACE
