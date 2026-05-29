#pragma once

#include "CoreMinimal.h"

namespace UnrealMcp
{
	enum class EToolRiskLevel : uint8;

	namespace Extension
	{
		enum class ESourceKind : uint8;
	}

	enum class ECallToolDecision : uint8
	{
		Allow,
		ForceDryRun,
		Deny
	};

	struct FCallToolPolicyResult
	{
		ECallToolDecision Decision = ECallToolDecision::Deny;
		bool bForcedDryRun = false;
		FString Reason;
	};

	struct FCallToolTargetFacts
	{
		UNREALMCP_API FCallToolTargetFacts();

		bool bVisible = false;
		Extension::ESourceKind SourceKind;
		EToolRiskLevel RiskLevel;
		bool bRequiresLock = false;
		bool bRequiresWrite = false;
		bool bRequiresRestart = false;
		bool bRequiresExternalProcess = false;
		bool bRequiresBuild = false;
		bool bDryRunSupport = false;
		bool bIsWorkflowRun = false;
		int32 Depth = 0;
	};

	UNREALMCP_API FCallToolPolicyResult ClassifyCallToolTarget_Pure(const FCallToolTargetFacts& F);
}
