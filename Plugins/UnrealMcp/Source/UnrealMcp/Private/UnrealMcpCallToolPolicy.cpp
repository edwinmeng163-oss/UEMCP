#include "UnrealMcpCallToolPolicy.h"

#include "UnrealMcpToolRegistry.h"

namespace UnrealMcp
{
	FCallToolTargetFacts::FCallToolTargetFacts()
		: SourceKind(Extension::ESourceKind::DescriptorOnly)
		, RiskLevel(EToolRiskLevel::Critical)
	{
	}

	namespace
	{
		FCallToolPolicyResult CallToolPolicyDeny(const FString& Reason)
		{
			FCallToolPolicyResult Result;
			Result.Decision = ECallToolDecision::Deny;
			Result.Reason = Reason;
			return Result;
		}
	}

	FCallToolPolicyResult ClassifyCallToolTarget_Pure(const FCallToolTargetFacts& F)
	{
		if (!F.bVisible)
		{
			return CallToolPolicyDeny(TEXT("not_visible"));
		}

		if (F.SourceKind == Extension::ESourceKind::UserRegistry)
		{
			return CallToolPolicyDeny(TEXT("user_tool_forbidden"));
		}

		if (F.Depth >= 1)
		{
			return CallToolPolicyDeny(TEXT("call_tool_depth_exceeded"));
		}

		if (F.bIsWorkflowRun)
		{
			return CallToolPolicyDeny(TEXT("workflow_run_forbidden"));
		}

		const bool bDangerous = F.RiskLevel >= EToolRiskLevel::High
			|| F.bRequiresLock
			|| F.bRequiresWrite
			|| F.bRequiresRestart
			|| F.bRequiresExternalProcess
			|| F.bRequiresBuild;
		if (bDangerous)
		{
			if (F.bDryRunSupport)
			{
				FCallToolPolicyResult Result;
				Result.Decision = ECallToolDecision::ForceDryRun;
				Result.bForcedDryRun = true;
				Result.Reason = TEXT("force_dry_run");
				return Result;
			}

			return CallToolPolicyDeny(TEXT("dangerous_no_dryrun"));
		}

		FCallToolPolicyResult Result;
		Result.Decision = ECallToolDecision::Allow;
		return Result;
	}
}
