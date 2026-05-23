#include "UnrealMcpAssistantSystemPromptBuilder.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAssistantSystemPromptBuilderSafetyRulesAlwaysPresentTest,
	"UnrealMcp.AssistantSystemPromptBuilder.SafetyRulesAlwaysPresent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAssistantSystemPromptBuilderSafetyRulesAlwaysPresentTest::RunTest(const FString& Parameters)
{
	UnrealMcp::FAssistantSystemPromptInput Input;
	const FString Output = UnrealMcp::BuildAssistantSystemPrompt(Input);

	TestTrue(TEXT("Safety header is present"), Output.Contains(TEXT("Reform C safety rules (must never be violated):")));
	for (int32 RuleIndex = 1; RuleIndex <= 7; ++RuleIndex)
	{
		const FString RulePrefix = FString::Printf(TEXT("%d. "), RuleIndex);
		TestTrue(
			FString::Printf(TEXT("Rule %d is present"), RuleIndex),
			Output.Contains(*RulePrefix));
	}

	TestTrue(
		TEXT("Rule 7 directs the model to consult skills first"),
		Output.Contains(TEXT("mcp-self-extension")) && Output.Contains(TEXT("unreal.skill_list")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAssistantSystemPromptBuilderUserPromptAppendedTest,
	"UnrealMcp.AssistantSystemPromptBuilder.UserPromptAppended",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAssistantSystemPromptBuilderUserPromptAppendedTest::RunTest(const FString& Parameters)
{
	UnrealMcp::FAssistantSystemPromptInput Input;
	Input.UserAssistantSystemPrompt = TEXT("Be terse.");

	const FString Output = UnrealMcp::BuildAssistantSystemPrompt(Input);
	const int32 SafetyIndex = Output.Find(TEXT("Reform C safety rules (must never be violated):"));
	const int32 AdditionalIndex = Output.Find(TEXT("Additional instructions:"));

	TestTrue(TEXT("Safety rules are present"), SafetyIndex != INDEX_NONE);
	TestTrue(TEXT("Additional instructions are present"), AdditionalIndex != INDEX_NONE);
	TestTrue(TEXT("Safety rules appear before additional instructions"), SafetyIndex < AdditionalIndex);
	TestTrue(TEXT("User prompt is appended"), Output.Contains(TEXT("Be terse.")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAssistantSystemPromptBuilderSteerInstructionsAppendedTest,
	"UnrealMcp.AssistantSystemPromptBuilder.SteerInstructionsAppended",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAssistantSystemPromptBuilderSteerInstructionsAppendedTest::RunTest(const FString& Parameters)
{
	UnrealMcp::FAssistantSystemPromptInput Input;
	Input.SteerInstructions.Add(TEXT("Keep the previous asset name."));
	Input.SteerInstructions.Add(TEXT("Do not rerun completed work."));

	const FString Output = UnrealMcp::BuildAssistantSystemPrompt(Input);
	const FString ExpectedSteerBlock =
		TEXT("User steering updates for the current turn:\n")
		TEXT("- Keep the previous asset name.\n")
		TEXT("- Do not rerun completed work.");

	TestTrue(TEXT("Output ends with steer block"), Output.EndsWith(*ExpectedSteerBlock));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAssistantSystemPromptBuilderTrimsUserPromptWhitespaceTest,
	"UnrealMcp.AssistantSystemPromptBuilder.TrimsUserPromptWhitespace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAssistantSystemPromptBuilderTrimsUserPromptWhitespaceTest::RunTest(const FString& Parameters)
{
	UnrealMcp::FAssistantSystemPromptInput Input;
	Input.UserAssistantSystemPrompt = TEXT("   Be terse.\n\n");

	const FString Output = UnrealMcp::BuildAssistantSystemPrompt(Input);
	TestTrue(TEXT("Trimmed prompt is present"), Output.Contains(TEXT("Additional instructions:\nBe terse.")));
	TestFalse(TEXT("Leading whitespace was trimmed"), Output.Contains(TEXT("   Be terse.")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpAssistantSystemPromptBuilderGetSafetyBlockMatchesEmbeddedTest,
	"UnrealMcp.AssistantSystemPromptBuilder.GetSafetyBlockMatchesEmbedded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpAssistantSystemPromptBuilderGetSafetyBlockMatchesEmbeddedTest::RunTest(const FString& Parameters)
{
	UnrealMcp::FAssistantSystemPromptInput Input;
	const FString SafetyBlock = UnrealMcp::GetAssistantSafetyRulesBlock();
	const FString Output = UnrealMcp::BuildAssistantSystemPrompt(Input);

	TestTrue(TEXT("Safety block is non-empty"), !SafetyBlock.IsEmpty());
	TestTrue(TEXT("Safety block appears contiguously in output"), Output.Contains(*SafetyBlock));

	return true;
}
