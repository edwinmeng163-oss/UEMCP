// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"

namespace UnrealMcp::Approval
{
	// Risk level a tool call carries. Per-tool default set by registry policy,
	// per-call override possible (e.g. mcp_apply_scaffold dryRun=false has higher
	// risk than dryRun=true).
	enum class ERiskLevel : uint8
	{
		Low,           // read-only / inspection / smoke / dry-run plan
		Medium,        // user-scope writes (Python user tool exec, scaffold draft)
		High,          // core-scope writes (mcp_apply_scaffold real apply, build, restart, docs)
		Critical,      // destructive (delete, force, package, release)
	};

	// Policy decision returned by EvaluateApprovalPolicy.
	enum class EDecision : uint8
	{
		Allow,                 // proceed without approval
		RequireApproval,       // pause, surface inline card to user
		Block,                 // reject outright (e.g. forbidden tool in autonomous mode)
	};

	// One pending approval payload sent to ChatPanel for user decision.
	struct FApprovalRequest
	{
		FGuid ApprovalId;
		FString ToolName;
		FString RiskLevelLabel;             // "high" / "medium" / etc.
		FString ReasonHumanReadable;        // "this call writes to core plugin source"
		TSharedPtr<FJsonObject> ArgumentsPreview;  // truncated/redacted args for display
	};

	// User decision returned via the assistant handle API after card click.
	enum class EUserDecision : uint8
	{
		Pending,
		Approved,
		Rejected,
		TimedOut,
	};

	UNREALMCP_API EDecision EvaluateApprovalPolicy(
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments,
		bool bCallerIsAssistantAutonomous,
		ERiskLevel& OutRiskLevel,
		FString& OutReason);

	UNREALMCP_API ERiskLevel GetToolBaselineRisk(const FString& ToolName);

	UNREALMCP_API FGuid RegisterPendingApproval(const FApprovalRequest& Request);
	UNREALMCP_API bool ResolveApproval(const FGuid& ApprovalId, EUserDecision Decision);
	UNREALMCP_API EUserDecision WaitForApproval(const FGuid& ApprovalId, double TimeoutSeconds);
	UNREALMCP_API void CancelApproval(const FGuid& ApprovalId);

	UNREALMCP_API const TCHAR* RiskLevelToString(ERiskLevel Risk);
	UNREALMCP_API const TCHAR* UserDecisionToString(EUserDecision Decision);
}
