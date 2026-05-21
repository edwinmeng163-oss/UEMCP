// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.
//
// =====================================================================
// UnrealMcpExtensionLifecycle.h — v0.26 Reform C Phase 0 contract header
// =====================================================================
//
// Single source of truth for the user-tool / self-extension lifecycle
// vocabulary used by ALL Reform C jobs (Wave 1 Job A/B, Wave 2 Job C/D,
// Wave 3 integration). Plain C++ types (NOT USTRUCT) because this is
// internal contract plumbing; reflection is not needed.
//
// JSON shape (additive to existing tool result structuredContent):
//
//   {
//     "lifecycle": {
//       "toolName": "unreal.example",
//       "extensionScope": "core" | "user",
//       "implementationTrack": "cpp" | "python",
//       "state": "<see ELifecycleState>",
//       "callableNow": false,
//       "nextRequiredAction": "unreal.mcp_user_registry_reload",
//       "sourceKind": "<see ESourceKind>",
//       "handlerKind": "cpp_dispatcher" | "python_bridge" | "none",
//       "paths": { "scaffoldDir": "...", "registryPath": "...",
//                  "pythonHandlerPath": "...", "manifestPath": "..." },
//       "derivedFlags": {        // optional; computed from state
//         "requiresApproval": false,
//         "requiresBuild": false,
//         "requiresRestart": false,
//         "requiresReload": false,
//         "requiresSmoke": true
//       }
//     }
//   }
//
// Rules (carried from R0.5 + R1 design lock):
//   - state is the primary signal; derivedFlags are computed
//   - callableNow remains explicit (prevents false-success behavior)
//   - handlerKind stays separate from sourceKind (audit/debug distinguish
//     registry state from execution mechanism)
//   - assistant may NEVER claim a tool is callable until callableNow=true
//     AND mcp_user_tool_smoke / mcp_tool_audit confirms
//
// Cross-references:
//   - Docs/AIProviderArchitecture.md Section H (added in Wave 3)
//   - Tools/UnrealMcpSkills/mcp-self-extension/SKILL.md (updated Wave 3)
//   - safety prompt 6 rules (built by UnrealMcpAssistantSystemPromptBuilder, Wave 3)
// =====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Templates/SharedPointer.h"

namespace UnrealMcp::Extension
{
	// =====================================================================
	// Enums
	// =====================================================================

	enum class EExtensionScope : uint8
	{
		Core,   // built-in plugin tools; canonical 160 + 2 v0.26 control tools = 162
		User,   // project-local Python user tools under <projDir>/Tools/UnrealMcpPyTools/
	};

	enum class EImplementationTrack : uint8
	{
		Cpp,    // built-in plugin handlers; AI default-prohibited (requires approval per Job A)
		Python, // project-local Python handler via UnrealMcpPythonToolBridge; AI default
	};

	enum class EHandlerKind : uint8
	{
		CppDispatcher, // routed through UnrealMcpToolDispatcher
		PythonBridge,  // routed through UnrealMcpPythonToolBridge::ExecutePythonRegisteredTool
		None,          // no handler resolvable (broken / missing / draft-only)
	};

	// 12 lifecycle states. Source of truth; derivedFlags are computed.
	// Order chosen to roughly match the forward lifecycle flow.
	enum class ELifecycleState : uint8
	{
		DraftScaffolded,                  // files written to scaffold dir; not yet registered
		DryRunReady,                      // dry-run plan generated; user/AI may inspect
		ApprovalRequired,                 // high-risk action awaiting user Approve/Reject (Job A)
		AppliedCoreCppBuildRequired,      // core C++ patch applied; needs UBT rebuild
		BuiltRestartRequired,             // dylib rebuilt; needs editor restart to load
		LoadedCoreCppAfterRestart,        // core C++ tool loaded into running editor
		AppliedUserPythonReloadRequired,  // user Python scaffold written; awaiting mcp_user_registry_reload
		LoadedUserPythonHot,              // user Python tool loaded via hot reload (no restart needed)
		SmokePassed,                      // mcp_user_tool_smoke succeeded; safe to claim callable
		SmokeFailed,                      // smoke check failed; tool NOT safe to claim callable
		Blocked,                          // invalid path, sha mismatch, policy reject, etc.
		RolledBack,                       // apply succeeded then auto-rolled back on build/test failure
	};

	// 7 source-kind values matching R1's 11 audit taxonomy codes (subset
	// representing where the tool's registry / handler entry actually lives).
	// The full 11 audit issue codes are listed in the audit tool's own header;
	// this enum is the per-tool reporting field used in lifecycle.sourceKind.
	enum class ESourceKind : uint8
	{
		CoreRegistry,         // canonical tools.json (160 + 2 = 162) with valid handler
		UserRegistry,         // user overlay registry (project-local Tools/UnrealMcpPyTools/)
		DescriptorOnly,       // tools/list descriptor present but no registry entry
		HandlerOnly,          // handler registered but no tools.json entry
		MissingHandler,       // tools.json entry exists, handler resolution fails
		PythonHandlerMissing, // user-track tool: tool.json present but main.py file absent
		PythonShaMismatch,    // user-track tool: main.py sha256 doesn't match tool.json declared
	};

	// =====================================================================
	// Lifecycle struct (plain C++; serialized to JSON via BuildLifecycleJson)
	// =====================================================================

	struct FToolLifecycle
	{
		FString ToolName;
		EExtensionScope ExtensionScope = EExtensionScope::Core;
		EImplementationTrack ImplementationTrack = EImplementationTrack::Cpp;
		ELifecycleState State = ELifecycleState::DraftScaffolded;
		bool bCallableNow = false;
		FString NextRequiredAction;        // e.g. "unreal.mcp_user_registry_reload"
		ESourceKind SourceKind = ESourceKind::CoreRegistry;
		EHandlerKind HandlerKind = EHandlerKind::None;

		// Optional path map. Empty values omitted from JSON output.
		FString ScaffoldDir;
		FString RegistryPath;
		FString PythonHandlerPath;
		FString ManifestPath;
	};

	struct FDerivedLifecycleFlags
	{
		bool bRequiresApproval = false;
		bool bRequiresBuild = false;
		bool bRequiresRestart = false;
		bool bRequiresReload = false;
		bool bRequiresSmoke = false;
	};

	// =====================================================================
	// Path constants
	// =====================================================================

	// Project-local user Python tools live at <projDir>/Tools/UnrealMcpPyTools/<tool_id>/
	// (forward-slash normalized; reject backslash, drive letter, UNC, absolute, ..)
	constexpr const TCHAR* UserPyToolsRelativeRoot = TEXT("Tools/UnrealMcpPyTools");

	// =====================================================================
	// Control tool name constants (Wave 1 Job B introduces these)
	// =====================================================================

	constexpr const TCHAR* ControlToolUserRegistryReload = TEXT("unreal.mcp_user_registry_reload");
	constexpr const TCHAR* ControlToolUserToolSmoke = TEXT("unreal.mcp_user_tool_smoke");

	// =====================================================================
	// Enum ↔ string converters (canonical lower-snake for JSON, identifier-safe)
	// =====================================================================

	UNREALMCP_API const TCHAR* ExtensionScopeToString(EExtensionScope Scope);
	UNREALMCP_API EExtensionScope ExtensionScopeFromString(const FString& Value, EExtensionScope Fallback = EExtensionScope::Core);

	UNREALMCP_API const TCHAR* ImplementationTrackToString(EImplementationTrack Track);
	UNREALMCP_API EImplementationTrack ImplementationTrackFromString(const FString& Value, EImplementationTrack Fallback = EImplementationTrack::Cpp);

	UNREALMCP_API const TCHAR* HandlerKindToString(EHandlerKind Kind);
	UNREALMCP_API EHandlerKind HandlerKindFromString(const FString& Value, EHandlerKind Fallback = EHandlerKind::None);

	UNREALMCP_API const TCHAR* LifecycleStateToString(ELifecycleState State);
	UNREALMCP_API ELifecycleState LifecycleStateFromString(const FString& Value, ELifecycleState Fallback = ELifecycleState::DraftScaffolded);

	UNREALMCP_API const TCHAR* SourceKindToString(ESourceKind Kind);
	UNREALMCP_API ESourceKind SourceKindFromString(const FString& Value, ESourceKind Fallback = ESourceKind::CoreRegistry);

	// =====================================================================
	// Derivation: state → derived flags
	// =====================================================================
	//
	// Centralized so all 4 Reform C jobs report the SAME flag matrix for a
	// given state. Tests in UnrealMcpLifecycleSchemaTests verify each state
	// maps to the expected flags + callableNow value.
	UNREALMCP_API FDerivedLifecycleFlags DeriveLifecycleFlags(ELifecycleState State);

	// Convenience: returns true iff the state implies callableNow=true.
	// Used as a defensive check; the FToolLifecycle.bCallableNow field is
	// the actual reported value (writer asserts these agree).
	UNREALMCP_API bool IsLifecycleStateCallable(ELifecycleState State);

	// =====================================================================
	// JSON construction (additive to existing tool result structuredContent)
	// =====================================================================
	//
	// BuildLifecycleJson produces the JSON object value that callers attach
	// as structuredContent->SetObjectField(TEXT("lifecycle"), result).
	// If bIncludeDerivedFlags=true, the derivedFlags sub-object is included.
	// Default false to keep payloads lean; opt-in for clients that prefer
	// flat boolean access.
	UNREALMCP_API TSharedPtr<FJsonObject> BuildLifecycleJson(const FToolLifecycle& Lifecycle, bool bIncludeDerivedFlags = false);

	// Reverse: parse lifecycle from JSON object. Returns false if "lifecycle"
	// key absent or malformed. Used by tests + cross-process clients.
	UNREALMCP_API bool ParseLifecycleJson(const TSharedPtr<FJsonObject>& StructuredContent, FToolLifecycle& OutLifecycle);

	// =====================================================================
	// Consistency validation (used in tests + assert in writer code)
	// =====================================================================
	//
	// Asserts that a constructed FToolLifecycle is internally consistent:
	//   - bCallableNow matches IsLifecycleStateCallable(State)
	//   - HandlerKind matches ImplementationTrack (Python → PythonBridge,
	//     Cpp → CppDispatcher, draft/blocked → None)
	//   - SourceKind matches ExtensionScope (Core → CoreRegistry, User →
	//     UserRegistry)
	//   - Required paths set for current state (e.g. DraftScaffolded must
	//     have ScaffoldDir; LoadedUserPythonHot must have PythonHandlerPath)
	//
	// Returns true if consistent. On false, OutInconsistencies lists each
	// violation (one per line). Used as a debug assert before writing JSON
	// in production code, and as a test assertion in
	// UnrealMcpLifecycleSchemaTests.
	UNREALMCP_API bool ValidateLifecycleConsistency(const FToolLifecycle& Lifecycle, FString& OutInconsistencies);
}
