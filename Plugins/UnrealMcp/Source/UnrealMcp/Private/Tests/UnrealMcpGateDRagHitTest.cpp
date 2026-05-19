#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "UnrealMcpSelfExtensionTools.h"

namespace UnrealMcpGateDRagHitTest
{
	const FString GateDSentinel = TEXT("UEVOLVE_GATE_D_SENTINEL_TASK_ATLAS_SMOKE");

	FString GateDGetKnowledgeSourcePath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("UnrealMcp/KnowledgeSources/TaskAtlas/gate_d_smoke.md")));
	}

	void GateDConfigureRefreshArgs(FJsonObject& Args)
	{
		Args.SetBoolField(TEXT("includeOfficialDocs"), true);
		Args.SetBoolField(TEXT("includeVersionedDocs"), false);
		Args.SetBoolField(TEXT("includeToolRegistry"), false);
		Args.SetBoolField(TEXT("includeActivityLog"), false);
		Args.SetBoolField(TEXT("includeSkills"), false);
		Args.SetNumberField(TEXT("maxCards"), 50.0);
	}

	bool GateDRefreshIndex(FUnrealMcpExecutionResult* OutResult = nullptr)
	{
		FJsonObject RefreshArgs;
		GateDConfigureRefreshArgs(RefreshArgs);
		const FUnrealMcpExecutionResult Result = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
		if (OutResult)
		{
			*OutResult = Result;
		}
		return !Result.bIsError;
	}

	bool GateDResultContainsSentinel(const FUnrealMcpExecutionResult& Result)
	{
		if (!Result.StructuredContent.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* KnowledgeCards = nullptr;
		if (!Result.StructuredContent->TryGetArrayField(TEXT("knowledgeCards"), KnowledgeCards) || !KnowledgeCards)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *KnowledgeCards)
		{
			const TSharedPtr<FJsonObject> Card = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Card.IsValid())
			{
				continue;
			}

			FString Title;
			FString SourcePath;
			FString Excerpt;
			Card->TryGetStringField(TEXT("title"), Title);
			Card->TryGetStringField(TEXT("sourcePath"), SourcePath);
			Card->TryGetStringField(TEXT("excerpt"), Excerpt);
			if (Title.Contains(TEXT("Gate D Task Atlas Smoke"), ESearchCase::CaseSensitive)
				|| SourcePath.Contains(TEXT("gate_d_smoke.md"), ESearchCase::CaseSensitive)
				|| Excerpt.Contains(GateDSentinel, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}

		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpGateDRagHitTest,
	"UnrealMcp.Verification.GateD.RagHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpGateDRagHitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString SourcePath = UnrealMcpGateDRagHitTest::GateDGetKnowledgeSourcePath();
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*SourcePath, false, true, true);
		UnrealMcpGateDRagHitTest::GateDRefreshIndex();
	};

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SourcePath), true);
	const FString Markdown = FString::Printf(
		TEXT("# Gate D Task Atlas Smoke\n\n")
		TEXT("This synthetic Task Atlas knowledge source exists only for the v0.20 Gate D RAG test.\n\n")
		TEXT("Sentinel: %s\n\n")
		TEXT("A tool recommendation query containing this sentinel should surface this card from gate_d_smoke.md.\n"),
		*UnrealMcpGateDRagHitTest::GateDSentinel);
	TestTrue(
		TEXT("Synthetic Task Atlas markdown source written."),
		FFileHelper::SaveStringToFile(Markdown, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FUnrealMcpExecutionResult RefreshResult;
	TestTrue(TEXT("KnowledgeIndexRefresh succeeds in-process."), UnrealMcpGateDRagHitTest::GateDRefreshIndex(&RefreshResult));
	TestFalse(TEXT("KnowledgeIndexRefresh did not report an error."), RefreshResult.bIsError);

	FJsonObject RecommendArgs;
	RecommendArgs.SetStringField(TEXT("task"), UnrealMcpGateDRagHitTest::GateDSentinel);
	RecommendArgs.SetBoolField(TEXT("includeKnowledge"), true);
	RecommendArgs.SetBoolField(TEXT("includeWorkflowDraft"), false);
	RecommendArgs.SetNumberField(TEXT("limit"), 5.0);

	TArray<TSharedPtr<FJsonValue>> EmptyTools;
	const FUnrealMcpExecutionResult RecommendResult = UnrealMcp::ToolRecommend(RecommendArgs, EmptyTools);
	TestFalse(TEXT("ToolRecommend succeeds in-process."), RecommendResult.bIsError);
	TestTrue(
		TEXT("ToolRecommend includes the synthetic Task Atlas KnowledgeCard."),
		UnrealMcpGateDRagHitTest::GateDResultContainsSentinel(RecommendResult));

	return true;
}

#endif
