// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.

#include "Providers/UnrealMcpApprovalPolicy.h"

#include "HAL/CriticalSection.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "UnrealMcpExtensionLifecycle.h"

#include <atomic>

namespace
{
	class FUnrealMcpPendingApprovalState final
	{
	public:
		explicit FUnrealMcpPendingApprovalState(const UnrealMcp::Approval::FApprovalRequest& InRequest)
			: Request(InRequest)
			, CompletionEvent(FPlatformProcess::GetSynchEventFromPool(false))
		{
			Decision.store(UnrealMcp::Approval::EUserDecision::Pending, std::memory_order_relaxed);
		}

		~FUnrealMcpPendingApprovalState()
		{
			if (CompletionEvent)
			{
				FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
				CompletionEvent = nullptr;
			}
		}

		UnrealMcp::Approval::FApprovalRequest Request;
		std::atomic<UnrealMcp::Approval::EUserDecision> Decision;
		FEvent* CompletionEvent = nullptr;
	};

	FCriticalSection& UnrealMcpApprovalRegistryMutex()
	{
		static FCriticalSection Mutex;
		return Mutex;
	}

	TMap<FGuid, TSharedPtr<FUnrealMcpPendingApprovalState>>& UnrealMcpApprovalRegistry()
	{
		static TMap<FGuid, TSharedPtr<FUnrealMcpPendingApprovalState>> Registry;
		return Registry;
	}

	uint32 UnrealMcpApprovalTimeoutToMilliseconds(double TimeoutSeconds)
	{
		if (TimeoutSeconds <= 0.0)
		{
			return 0;
		}

		const double TimeoutMs = FMath::Min(TimeoutSeconds * 1000.0, static_cast<double>(MAX_uint32));
		return static_cast<uint32>(TimeoutMs);
	}
}

namespace UnrealMcp::Approval
{
	ERiskLevel GetToolBaselineRisk(const FString& ToolName)
	{
		// CRITICAL: do NOT auto-approve any of these in autonomous mode.
		if (ToolName == TEXT("unreal.mcp_apply_scaffold")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.mcp_build_editor")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.mcp_clean_test_artifacts")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.code_apply_change")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.code_rollback_change")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.task_atlas_make_composite")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.task_atlas_delete_made_tool")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.task_atlas_smoke_made_tool")) { return ERiskLevel::High; }

		// HIGH: core docs / registry mutations
		if (ToolName.StartsWith(TEXT("unreal.docs_"))) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.write_text_file")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.delete_actor")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.execute_python")) { return ERiskLevel::High; }
		if (ToolName == TEXT("unreal.execute_python_file")) { return ERiskLevel::High; }

		// MEDIUM: user-scope mutations
		if (ToolName == TEXT("unreal.scaffold_mcp_tool")) { return ERiskLevel::Medium; }
		if (ToolName == ::UnrealMcp::Extension::ControlToolUserRegistryReload) { return ERiskLevel::Medium; }
		if (ToolName == ::UnrealMcp::Extension::ControlToolUserToolSmoke) { return ERiskLevel::Low; }
		if (ToolName == TEXT("unreal.task_atlas_promote_to_rag")) { return ERiskLevel::Medium; }
		if (ToolName == TEXT("unreal.chat_inject_user_input")) { return ERiskLevel::Medium; }
		if (ToolName.StartsWith(TEXT("unreal.spawn_"))) { return ERiskLevel::Medium; }
		if (ToolName.StartsWith(TEXT("unreal.set_"))) { return ERiskLevel::Medium; }
		if (ToolName.StartsWith(TEXT("unreal.create_"))) { return ERiskLevel::Medium; }

		// LOW: read-only / inspection
		if (ToolName == TEXT("unreal.mcp_tool_audit")) { return ERiskLevel::Low; }
		if (ToolName == TEXT("unreal.mcp_inspect_scaffold")) { return ERiskLevel::Low; }
		if (ToolName.StartsWith(TEXT("unreal.list_"))) { return ERiskLevel::Low; }
		if (ToolName.StartsWith(TEXT("unreal.get_"))) { return ERiskLevel::Low; }
		if (ToolName.StartsWith(TEXT("unreal.editor_status"))) { return ERiskLevel::Low; }

		return ERiskLevel::Low;
	}

	EDecision EvaluateApprovalPolicy(
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments,
		bool bCallerIsAssistantAutonomous,
		ERiskLevel& OutRiskLevel,
		FString& OutReason)
	{
		OutRiskLevel = GetToolBaselineRisk(ToolName);

		if (!bCallerIsAssistantAutonomous)
		{
			OutReason = TEXT("non-assistant caller; approval bypassed");
			return EDecision::Allow;
		}

		auto GetDryRunDefaultTrue = [&Arguments]()
		{
			bool bDryRun = true;
			if (Arguments.IsValid() && Arguments->HasField(TEXT("dryRun")))
			{
				bDryRun = Arguments->GetBoolField(TEXT("dryRun"));
			}
			return bDryRun;
		};

		auto GetDryRunDefaultFalse = [&Arguments]()
		{
			bool bDryRun = false;
			if (Arguments.IsValid() && Arguments->HasField(TEXT("dryRun")))
			{
				bDryRun = Arguments->GetBoolField(TEXT("dryRun"));
			}
			return bDryRun;
		};

		auto HasHighRiskCodePathHint = [&Arguments]()
		{
			if (!Arguments.IsValid())
			{
				return false;
			}
			bool bConfirmHighRisk = false;
			Arguments->TryGetBoolField(TEXT("confirmHighRisk"), bConfirmHighRisk);
			FString PathRisk;
			FString RiskLevel;
			Arguments->TryGetStringField(TEXT("pathRisk"), PathRisk);
			Arguments->TryGetStringField(TEXT("riskLevel"), RiskLevel);
			return bConfirmHighRisk
				|| PathRisk.Equals(TEXT("high"), ESearchCase::IgnoreCase)
				|| RiskLevel.Equals(TEXT("high"), ESearchCase::IgnoreCase);
		};

		if (ToolName == TEXT("unreal.code_apply_change"))
		{
			if (GetDryRunDefaultTrue())
			{
				OutRiskLevel = ERiskLevel::Low;
				OutReason = TEXT("code_apply_change dryRun=true is preview-only; allowed");
				return EDecision::Allow;
			}
			if (HasHighRiskCodePathHint())
			{
				OutRiskLevel = ERiskLevel::High;
				OutReason = TEXT("code_apply_change writes high-risk host path; requires approval");
				return EDecision::RequireApproval;
			}
			OutRiskLevel = ERiskLevel::High;
			OutReason = TEXT("code_apply_change dryRun=false writes user code files; requires approval");
			return EDecision::RequireApproval;
		}

		if (ToolName == TEXT("unreal.code_rollback_change"))
		{
			if (GetDryRunDefaultTrue())
			{
				OutRiskLevel = ERiskLevel::Low;
				OutReason = TEXT("code_rollback_change dryRun=true is preview-only; allowed");
				return EDecision::Allow;
			}
			if (HasHighRiskCodePathHint())
			{
				OutRiskLevel = ERiskLevel::High;
				OutReason = TEXT("code_rollback_change restores high-risk host path; requires approval");
				return EDecision::RequireApproval;
			}
			OutRiskLevel = ERiskLevel::High;
			OutReason = TEXT("code_rollback_change dryRun=false restores user code files; requires approval");
			return EDecision::RequireApproval;
		}

		if (ToolName == TEXT("unreal.task_atlas_make_composite"))
		{
			const bool bPreferDocumentOnly = Arguments.IsValid()
				&& Arguments->HasField(TEXT("preferDocumentOnly"))
				&& Arguments->GetBoolField(TEXT("preferDocumentOnly"));
			const bool bForceWriteEvenIfBlocked = Arguments.IsValid()
				&& Arguments->HasField(TEXT("forceWriteEvenIfBlocked"))
				&& Arguments->GetBoolField(TEXT("forceWriteEvenIfBlocked"));
			if (bPreferDocumentOnly && !bForceWriteEvenIfBlocked)
			{
				OutRiskLevel = ERiskLevel::Low;
				OutReason = TEXT("task_atlas_make_composite preferDocumentOnly=true is document-only; allowed");
				return EDecision::Allow;
			}
			OutRiskLevel = ERiskLevel::High;
			OutReason = TEXT("task_atlas_make_composite can write generated user tools; requires approval");
			return EDecision::RequireApproval;
		}

		if (ToolName == TEXT("unreal.task_atlas_delete_made_tool"))
		{
			if (GetDryRunDefaultFalse())
			{
				OutRiskLevel = ERiskLevel::Low;
				OutReason = TEXT("task_atlas_delete_made_tool dryRun=true is preview-only; allowed");
				return EDecision::Allow;
			}
			OutRiskLevel = ERiskLevel::High;
			OutReason = TEXT("task_atlas_delete_made_tool deletes generated user tools; requires approval");
			return EDecision::RequireApproval;
		}

		if (ToolName == TEXT("unreal.task_atlas_promote_to_rag"))
		{
			if (Arguments.IsValid()
				&& Arguments->HasField(TEXT("dryRun"))
				&& Arguments->GetBoolField(TEXT("dryRun")))
			{
				OutRiskLevel = ERiskLevel::Low;
				OutReason = TEXT("task_atlas_promote_to_rag dryRun=true is preview-only; allowed");
				return EDecision::Allow;
			}
			OutRiskLevel = ERiskLevel::Medium;
			OutReason = TEXT("task_atlas_promote_to_rag writes KnowledgeSources; requires approval");
			return EDecision::RequireApproval;
		}

		if (ToolName == TEXT("unreal.task_atlas_smoke_made_tool"))
		{
			if (GetDryRunDefaultTrue())
			{
				OutRiskLevel = ERiskLevel::Low;
				OutReason = TEXT("task_atlas_smoke_made_tool dryRun=true is preview-only; allowed");
				return EDecision::Allow;
			}
			OutRiskLevel = ERiskLevel::High;
			OutReason = TEXT("task_atlas_smoke_made_tool can execute user Python and write failure markers; requires approval");
			return EDecision::RequireApproval;
		}

		if (ToolName == TEXT("unreal.chat_inject_user_input"))
		{
			if (GetDryRunDefaultFalse())
			{
				OutRiskLevel = ERiskLevel::Low;
				OutReason = TEXT("chat_inject_user_input dryRun=true is preview-only; allowed");
				return EDecision::Allow;
			}
			OutRiskLevel = ERiskLevel::Medium;
			OutReason = TEXT("chat_inject_user_input writes chat state and can trigger an autonomous AI turn; requires approval");
			return EDecision::RequireApproval;
		}

		if (ToolName == TEXT("unreal.mcp_apply_scaffold")
			&& Arguments.IsValid()
			&& Arguments->HasField(TEXT("dryRun"))
			&& Arguments->GetBoolField(TEXT("dryRun")))
		{
			OutRiskLevel = ERiskLevel::Low;
			OutReason = TEXT("mcp_apply_scaffold dryRun=true is preview-only; allowed");
			return EDecision::Allow;
		}

		if (ToolName == TEXT("unreal.scaffold_mcp_tool")
			&& Arguments.IsValid()
			&& Arguments->HasField(TEXT("implementationTrack"))
			&& Arguments->GetStringField(TEXT("implementationTrack")).Equals(TEXT("cpp"), ESearchCase::IgnoreCase))
		{
			OutRiskLevel = ERiskLevel::High;
			OutReason = TEXT("scaffold_mcp_tool with implementationTrack=cpp writes core C++; requires approval");
			return EDecision::RequireApproval;
		}

		if (ToolName == TEXT("unreal.mcp_apply_scaffold"))
		{
			OutReason = TEXT("mcp_apply_scaffold without explicit dryRun=true merges to core plugin; requires approval");
			return EDecision::RequireApproval;
		}

		switch (OutRiskLevel)
		{
		case ERiskLevel::Low:
			OutReason = TEXT("low-risk tool; allowed");
			return EDecision::Allow;
		case ERiskLevel::Medium:
			OutReason = FString::Printf(TEXT("medium-risk tool '%s'; allowed (user-scope only)"), *ToolName);
			return EDecision::Allow;
		case ERiskLevel::High:
			OutReason = FString::Printf(TEXT("high-risk tool '%s'; requires user approval"), *ToolName);
			return EDecision::RequireApproval;
		case ERiskLevel::Critical:
			OutReason = FString::Printf(TEXT("critical-risk tool '%s'; blocked in autonomous mode"), *ToolName);
			return EDecision::Block;
		}

		OutReason = TEXT("policy fall-through; defaulting to allow");
		return EDecision::Allow;
	}

	FGuid RegisterPendingApproval(const FApprovalRequest& Request)
	{
		FApprovalRequest StoredRequest = Request;
		if (!StoredRequest.ApprovalId.IsValid())
		{
			StoredRequest.ApprovalId = FGuid::NewGuid();
		}

		TSharedPtr<FUnrealMcpPendingApprovalState> State = MakeShared<FUnrealMcpPendingApprovalState>(StoredRequest);
		{
			const FScopeLock Lock(&UnrealMcpApprovalRegistryMutex());
			UnrealMcpApprovalRegistry().Add(StoredRequest.ApprovalId, State);
		}
		return StoredRequest.ApprovalId;
	}

	bool ResolveApproval(const FGuid& ApprovalId, EUserDecision Decision)
	{
		TSharedPtr<FUnrealMcpPendingApprovalState> State;
		{
			const FScopeLock Lock(&UnrealMcpApprovalRegistryMutex());
			const TSharedPtr<FUnrealMcpPendingApprovalState>* FoundState = UnrealMcpApprovalRegistry().Find(ApprovalId);
			if (!FoundState || !FoundState->IsValid())
			{
				return false;
			}
			State = *FoundState;
		}

		State->Decision.store(Decision, std::memory_order_release);
		if (State->CompletionEvent)
		{
			State->CompletionEvent->Trigger();
		}
		return true;
	}

	EUserDecision WaitForApproval(const FGuid& ApprovalId, double TimeoutSeconds)
	{
		TSharedPtr<FUnrealMcpPendingApprovalState> State;
		{
			const FScopeLock Lock(&UnrealMcpApprovalRegistryMutex());
			const TSharedPtr<FUnrealMcpPendingApprovalState>* FoundState = UnrealMcpApprovalRegistry().Find(ApprovalId);
			if (!FoundState || !FoundState->IsValid())
			{
				return EUserDecision::TimedOut;
			}
			State = *FoundState;
		}

		EUserDecision Decision = State->Decision.load(std::memory_order_acquire);
		if (Decision == EUserDecision::Pending && State->CompletionEvent)
		{
			const bool bSignaled = State->CompletionEvent->Wait(UnrealMcpApprovalTimeoutToMilliseconds(TimeoutSeconds));
			Decision = State->Decision.load(std::memory_order_acquire);
			if (!bSignaled && Decision == EUserDecision::Pending)
			{
				State->Decision.store(EUserDecision::TimedOut, std::memory_order_release);
				Decision = EUserDecision::TimedOut;
			}
		}

		{
			const FScopeLock Lock(&UnrealMcpApprovalRegistryMutex());
			const TSharedPtr<FUnrealMcpPendingApprovalState>* FoundState = UnrealMcpApprovalRegistry().Find(ApprovalId);
			if (FoundState && *FoundState == State)
			{
				UnrealMcpApprovalRegistry().Remove(ApprovalId);
			}
		}

		return Decision == EUserDecision::Pending ? EUserDecision::TimedOut : Decision;
	}

	void CancelApproval(const FGuid& ApprovalId)
	{
		ResolveApproval(ApprovalId, EUserDecision::Rejected);
	}

	const TCHAR* RiskLevelToString(ERiskLevel Risk)
	{
		switch (Risk)
		{
		case ERiskLevel::Low:
			return TEXT("low");
		case ERiskLevel::Medium:
			return TEXT("medium");
		case ERiskLevel::High:
			return TEXT("high");
		case ERiskLevel::Critical:
			return TEXT("critical");
		}
		return TEXT("unknown");
	}

	const TCHAR* UserDecisionToString(EUserDecision Decision)
	{
		switch (Decision)
		{
		case EUserDecision::Pending:
			return TEXT("pending");
		case EUserDecision::Approved:
			return TEXT("approved");
		case EUserDecision::Rejected:
			return TEXT("rejected");
		case EUserDecision::TimedOut:
			return TEXT("timed_out");
		}
		return TEXT("unknown");
	}
}
