#include "UnrealMcpAssistantSystemPromptBuilder.h"

namespace UnrealMcp
{
	namespace
	{
		const TCHAR* BaseAssistantIdentity =
			TEXT("You are Unreal MCP AI running inside Unreal Editor. Help the user build, inspect, and modify the current Unreal project by using the provided function tools when helpful. Prefer the smallest safe set of tool calls. Inspect before concluding for read-only questions, act directly for clear modification requests, and avoid destructive actions unless explicitly asked. Prefer AI-safe wrapper tools before falling back to execute_python. Keep answers compact and focused on what changed or was found.");
	}

	FString GetAssistantSafetyRulesBlock()
	{
		TArray<FString> Rules;
		Rules.Add(TEXT("Reform C safety rules (must never be violated):"));
		Rules.Add(TEXT("1. Do not claim a new tool is callable until lifecycle.callableNow=true AND a smoke or audit step confirms it. Reading `structuredContent.lifecycle` is mandatory after any tool that may change registry state (scaffold / apply / reload / smoke / audit)."));
		Rules.Add(TEXT("2. For user Python tools: scaffold → unreal.mcp_user_registry_reload → unreal.mcp_user_tool_smoke → only then call the tool. Never call a freshly scaffolded user tool without reload + smoke."));
		Rules.Add(TEXT("3. For core C++ tools: dry-run → unreal.mcp_apply_scaffold (real) → build → Editor restart → audit/smoke → only then call. Never claim a core C++ tool is usable in the same session it was applied."));
		Rules.Add(TEXT("4. If a tool result includes `approval_required` or `requiresApproval=true`, stop and wait for explicit user approval before any retry. Do not paraphrase the approval ask; surface the original message and tool name."));
		Rules.Add(TEXT("5. Never turn a `dryRun` result into a success claim. `dryRun=true` results describe what WOULD happen; they are not evidence the action ran."));
		Rules.Add(TEXT("6. Write-capable user Python tools must default to `dryRun=true` and return `wouldWrite` before any mutation. If the user explicitly requests a write, set `dryRun=false` AND tell the user what is about to change before the call."));
		Rules.Add(TEXT("7. Before scaffolding a new tool or productizing a completed workflow, FIRST consult project skills with `unreal.skill_list` / `unreal.skill_read` (start with `mcp-capability-routing`). Most goals are met by composing existing tools; a direct request to make a tool uses the Python user-extension track (scaffold -> reload -> smoke), never a handler hand-merged into core."));
		return FString::Join(Rules, TEXT("\n"));
	}

	FString BuildAssistantSystemPrompt(const FAssistantSystemPromptInput& Input)
	{
		switch (Input.Transport)
		{
		case EAssistantSystemPromptTransport::OpenAiResponses:
		case EAssistantSystemPromptTransport::OpenAiChatCompat:
		case EAssistantSystemPromptTransport::AnthropicMessages:
			break;
		}

		TArray<FString> Blocks;
		Blocks.Add(BaseAssistantIdentity);
		Blocks.Add(GetAssistantSafetyRulesBlock());

		const FString TrimmedUserPrompt = Input.UserAssistantSystemPrompt.TrimStartAndEnd();
		if (!TrimmedUserPrompt.IsEmpty())
		{
			Blocks.Add(FString::Printf(TEXT("Additional instructions:\n%s"), *TrimmedUserPrompt));
		}

		if (!Input.SteerInstructions.IsEmpty())
		{
			TArray<FString> SteerLines;
			SteerLines.Add(TEXT("User steering updates for the current turn:"));
			for (const FString& Instruction : Input.SteerInstructions)
			{
				SteerLines.Add(FString::Printf(TEXT("- %s"), *Instruction));
			}
			Blocks.Add(FString::Join(SteerLines, TEXT("\n")));
		}

		return FString::Join(Blocks, TEXT("\n\n"));
	}
}
