// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.
//
// Implementation of UnrealMcpExtensionLifecycle.h enum/JSON/derivation
// helpers. See header for contract + cross-references.

#include "UnrealMcpExtensionLifecycle.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Templates/SharedPointer.h"
#include "UnrealMcpModule.h"  // for LogUnrealMcp

namespace UnrealMcp::Extension
{
	namespace
	{
		// Helper: append "<key>: <reason>" line to inconsistencies report
		void AddInconsistency(FString& Out, const TCHAR* Key, const FString& Reason)
		{
			if (!Out.IsEmpty()) { Out.AppendChar(TEXT('\n')); }
			Out.Append(Key);
			Out.Append(TEXT(": "));
			Out.Append(Reason);
		}
	}

	// =====================================================================
	// Enum ↔ string converters
	// =====================================================================

	const TCHAR* ExtensionScopeToString(EExtensionScope Scope)
	{
		switch (Scope)
		{
		case EExtensionScope::Core: return TEXT("core");
		case EExtensionScope::User: return TEXT("user");
		}
		return TEXT("core");
	}

	EExtensionScope ExtensionScopeFromString(const FString& Value, EExtensionScope Fallback)
	{
		if (Value.Equals(TEXT("core"), ESearchCase::IgnoreCase)) { return EExtensionScope::Core; }
		if (Value.Equals(TEXT("user"), ESearchCase::IgnoreCase)) { return EExtensionScope::User; }
		return Fallback;
	}

	const TCHAR* ImplementationTrackToString(EImplementationTrack Track)
	{
		switch (Track)
		{
		case EImplementationTrack::Cpp: return TEXT("cpp");
		case EImplementationTrack::Python: return TEXT("python");
		}
		return TEXT("cpp");
	}

	EImplementationTrack ImplementationTrackFromString(const FString& Value, EImplementationTrack Fallback)
	{
		if (Value.Equals(TEXT("cpp"), ESearchCase::IgnoreCase)) { return EImplementationTrack::Cpp; }
		if (Value.Equals(TEXT("python"), ESearchCase::IgnoreCase)) { return EImplementationTrack::Python; }
		return Fallback;
	}

	const TCHAR* HandlerKindToString(EHandlerKind Kind)
	{
		switch (Kind)
		{
		case EHandlerKind::CppDispatcher: return TEXT("cpp_dispatcher");
		case EHandlerKind::PythonBridge:  return TEXT("python_bridge");
		case EHandlerKind::None:          return TEXT("none");
		}
		return TEXT("none");
	}

	EHandlerKind HandlerKindFromString(const FString& Value, EHandlerKind Fallback)
	{
		if (Value.Equals(TEXT("cpp_dispatcher"), ESearchCase::IgnoreCase)) { return EHandlerKind::CppDispatcher; }
		if (Value.Equals(TEXT("python_bridge"), ESearchCase::IgnoreCase))  { return EHandlerKind::PythonBridge; }
		if (Value.Equals(TEXT("none"), ESearchCase::IgnoreCase))           { return EHandlerKind::None; }
		return Fallback;
	}

	const TCHAR* LifecycleStateToString(ELifecycleState State)
	{
		switch (State)
		{
		case ELifecycleState::DraftScaffolded:                 return TEXT("draft_scaffolded");
		case ELifecycleState::DryRunReady:                     return TEXT("dry_run_ready");
		case ELifecycleState::ApprovalRequired:                return TEXT("approval_required");
		case ELifecycleState::AppliedCoreCppBuildRequired:     return TEXT("applied_core_cpp_build_required");
		case ELifecycleState::BuiltRestartRequired:            return TEXT("built_restart_required");
		case ELifecycleState::LoadedCoreCppAfterRestart:       return TEXT("loaded_core_cpp_after_restart");
		case ELifecycleState::AppliedUserPythonReloadRequired: return TEXT("applied_user_python_reload_required");
		case ELifecycleState::LoadedUserPythonHot:             return TEXT("loaded_user_python_hot");
		case ELifecycleState::SmokePassed:                     return TEXT("smoke_passed");
		case ELifecycleState::SmokeFailed:                     return TEXT("smoke_failed");
		case ELifecycleState::Blocked:                         return TEXT("blocked");
		case ELifecycleState::RolledBack:                      return TEXT("rolled_back");
		}
		return TEXT("draft_scaffolded");
	}

	ELifecycleState LifecycleStateFromString(const FString& Value, ELifecycleState Fallback)
	{
		static const TMap<FString, ELifecycleState> Lookup = {
			{ TEXT("draft_scaffolded"),                  ELifecycleState::DraftScaffolded },
			{ TEXT("dry_run_ready"),                     ELifecycleState::DryRunReady },
			{ TEXT("approval_required"),                 ELifecycleState::ApprovalRequired },
			{ TEXT("applied_core_cpp_build_required"),   ELifecycleState::AppliedCoreCppBuildRequired },
			{ TEXT("built_restart_required"),            ELifecycleState::BuiltRestartRequired },
			{ TEXT("loaded_core_cpp_after_restart"),     ELifecycleState::LoadedCoreCppAfterRestart },
			{ TEXT("applied_user_python_reload_required"), ELifecycleState::AppliedUserPythonReloadRequired },
			{ TEXT("loaded_user_python_hot"),            ELifecycleState::LoadedUserPythonHot },
			{ TEXT("smoke_passed"),                      ELifecycleState::SmokePassed },
			{ TEXT("smoke_failed"),                      ELifecycleState::SmokeFailed },
			{ TEXT("blocked"),                           ELifecycleState::Blocked },
			{ TEXT("rolled_back"),                       ELifecycleState::RolledBack },
		};
		const ELifecycleState* Found = Lookup.Find(Value.ToLower());
		return Found ? *Found : Fallback;
	}

	const TCHAR* SourceKindToString(ESourceKind Kind)
	{
		switch (Kind)
		{
		case ESourceKind::CoreRegistry:         return TEXT("core_registry");
		case ESourceKind::UserRegistry:         return TEXT("user_registry");
		case ESourceKind::DescriptorOnly:       return TEXT("descriptor_only");
		case ESourceKind::HandlerOnly:          return TEXT("handler_only");
		case ESourceKind::MissingHandler:       return TEXT("missing_handler");
		case ESourceKind::PythonHandlerMissing: return TEXT("python_handler_missing");
		case ESourceKind::PythonShaMismatch:    return TEXT("python_sha_mismatch");
		}
		return TEXT("core_registry");
	}

	ESourceKind SourceKindFromString(const FString& Value, ESourceKind Fallback)
	{
		static const TMap<FString, ESourceKind> Lookup = {
			{ TEXT("core_registry"),          ESourceKind::CoreRegistry },
			{ TEXT("user_registry"),          ESourceKind::UserRegistry },
			{ TEXT("descriptor_only"),        ESourceKind::DescriptorOnly },
			{ TEXT("handler_only"),           ESourceKind::HandlerOnly },
			{ TEXT("missing_handler"),        ESourceKind::MissingHandler },
			{ TEXT("python_handler_missing"), ESourceKind::PythonHandlerMissing },
			{ TEXT("python_sha_mismatch"),    ESourceKind::PythonShaMismatch },
		};
		const ESourceKind* Found = Lookup.Find(Value.ToLower());
		return Found ? *Found : Fallback;
	}

	// =====================================================================
	// Derivation
	// =====================================================================

	FDerivedLifecycleFlags DeriveLifecycleFlags(ELifecycleState State)
	{
		FDerivedLifecycleFlags Flags;
		switch (State)
		{
		case ELifecycleState::DraftScaffolded:
			// User Python: needs reload+smoke. Core Cpp: needs apply+build+restart.
			// Default to user-track flags here; writer can override for core-cpp drafts.
			Flags.bRequiresReload = true;
			Flags.bRequiresSmoke = true;
			break;
		case ELifecycleState::DryRunReady:
			// Plan generated; nothing required to inspect; approval may be needed for next apply.
			break;
		case ELifecycleState::ApprovalRequired:
			Flags.bRequiresApproval = true;
			break;
		case ELifecycleState::AppliedCoreCppBuildRequired:
			Flags.bRequiresBuild = true;
			Flags.bRequiresRestart = true;
			Flags.bRequiresSmoke = true;
			break;
		case ELifecycleState::BuiltRestartRequired:
			Flags.bRequiresRestart = true;
			Flags.bRequiresSmoke = true;
			break;
		case ELifecycleState::LoadedCoreCppAfterRestart:
			Flags.bRequiresSmoke = true;
			break;
		case ELifecycleState::AppliedUserPythonReloadRequired:
			Flags.bRequiresReload = true;
			Flags.bRequiresSmoke = true;
			break;
		case ELifecycleState::LoadedUserPythonHot:
			Flags.bRequiresSmoke = true;
			break;
		case ELifecycleState::SmokePassed:
			// Fully ready; no further required action
			break;
		case ELifecycleState::SmokeFailed:
		case ELifecycleState::Blocked:
		case ELifecycleState::RolledBack:
			// Terminal failure states; no required action that will help
			break;
		}
		return Flags;
	}

	bool IsLifecycleStateCallable(ELifecycleState State)
	{
		// Only fully-loaded + smoke-confirmed states are callable. LoadedCoreCppAfterRestart
		// and LoadedUserPythonHot are LOADED but still recommend smoke before claiming
		// callable; SmokePassed is the only unconditional "yes you can call this".
		// However, in practice clients may treat "Loaded*" as callable-with-warning;
		// the strict definition here returns true only for SmokePassed.
		return State == ELifecycleState::SmokePassed;
	}

	// =====================================================================
	// JSON build / parse
	// =====================================================================

	TSharedPtr<FJsonObject> BuildLifecycleJson(const FToolLifecycle& Lifecycle, bool bIncludeDerivedFlags)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("toolName"), Lifecycle.ToolName);
		Obj->SetStringField(TEXT("extensionScope"), ExtensionScopeToString(Lifecycle.ExtensionScope));
		Obj->SetStringField(TEXT("implementationTrack"), ImplementationTrackToString(Lifecycle.ImplementationTrack));
		Obj->SetStringField(TEXT("state"), LifecycleStateToString(Lifecycle.State));
		Obj->SetBoolField(TEXT("callableNow"), Lifecycle.bCallableNow);
		if (!Lifecycle.NextRequiredAction.IsEmpty())
		{
			Obj->SetStringField(TEXT("nextRequiredAction"), Lifecycle.NextRequiredAction);
		}
		Obj->SetStringField(TEXT("sourceKind"), SourceKindToString(Lifecycle.SourceKind));
		Obj->SetStringField(TEXT("handlerKind"), HandlerKindToString(Lifecycle.HandlerKind));

		// Paths object — only emit non-empty fields to keep payload lean
		TSharedPtr<FJsonObject> PathsObj = MakeShared<FJsonObject>();
		bool bAnyPath = false;
		if (!Lifecycle.ScaffoldDir.IsEmpty())        { PathsObj->SetStringField(TEXT("scaffoldDir"), Lifecycle.ScaffoldDir); bAnyPath = true; }
		if (!Lifecycle.RegistryPath.IsEmpty())       { PathsObj->SetStringField(TEXT("registryPath"), Lifecycle.RegistryPath); bAnyPath = true; }
		if (!Lifecycle.PythonHandlerPath.IsEmpty())  { PathsObj->SetStringField(TEXT("pythonHandlerPath"), Lifecycle.PythonHandlerPath); bAnyPath = true; }
		if (!Lifecycle.ManifestPath.IsEmpty())       { PathsObj->SetStringField(TEXT("manifestPath"), Lifecycle.ManifestPath); bAnyPath = true; }
		if (bAnyPath) { Obj->SetObjectField(TEXT("paths"), PathsObj); }

		if (bIncludeDerivedFlags)
		{
			const FDerivedLifecycleFlags Flags = DeriveLifecycleFlags(Lifecycle.State);
			TSharedPtr<FJsonObject> FlagsObj = MakeShared<FJsonObject>();
			FlagsObj->SetBoolField(TEXT("requiresApproval"), Flags.bRequiresApproval);
			FlagsObj->SetBoolField(TEXT("requiresBuild"),    Flags.bRequiresBuild);
			FlagsObj->SetBoolField(TEXT("requiresRestart"),  Flags.bRequiresRestart);
			FlagsObj->SetBoolField(TEXT("requiresReload"),   Flags.bRequiresReload);
			FlagsObj->SetBoolField(TEXT("requiresSmoke"),    Flags.bRequiresSmoke);
			Obj->SetObjectField(TEXT("derivedFlags"), FlagsObj);
		}

		return Obj;
	}

	bool ParseLifecycleJson(const TSharedPtr<FJsonObject>& StructuredContent, FToolLifecycle& OutLifecycle)
	{
		if (!StructuredContent.IsValid()) { return false; }
		const TSharedPtr<FJsonObject>* LifecycleObjPtr = nullptr;
		if (!StructuredContent->TryGetObjectField(TEXT("lifecycle"), LifecycleObjPtr) || !LifecycleObjPtr || !LifecycleObjPtr->IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>& Lifecycle = *LifecycleObjPtr;

		Lifecycle->TryGetStringField(TEXT("toolName"), OutLifecycle.ToolName);

		FString ScopeStr;
		Lifecycle->TryGetStringField(TEXT("extensionScope"), ScopeStr);
		OutLifecycle.ExtensionScope = ExtensionScopeFromString(ScopeStr);

		FString TrackStr;
		Lifecycle->TryGetStringField(TEXT("implementationTrack"), TrackStr);
		OutLifecycle.ImplementationTrack = ImplementationTrackFromString(TrackStr);

		FString StateStr;
		Lifecycle->TryGetStringField(TEXT("state"), StateStr);
		OutLifecycle.State = LifecycleStateFromString(StateStr);

		Lifecycle->TryGetBoolField(TEXT("callableNow"), OutLifecycle.bCallableNow);
		Lifecycle->TryGetStringField(TEXT("nextRequiredAction"), OutLifecycle.NextRequiredAction);

		FString SourceStr;
		Lifecycle->TryGetStringField(TEXT("sourceKind"), SourceStr);
		OutLifecycle.SourceKind = SourceKindFromString(SourceStr);

		FString HandlerStr;
		Lifecycle->TryGetStringField(TEXT("handlerKind"), HandlerStr);
		OutLifecycle.HandlerKind = HandlerKindFromString(HandlerStr);

		const TSharedPtr<FJsonObject>* PathsObjPtr = nullptr;
		if (Lifecycle->TryGetObjectField(TEXT("paths"), PathsObjPtr) && PathsObjPtr && PathsObjPtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& Paths = *PathsObjPtr;
			Paths->TryGetStringField(TEXT("scaffoldDir"), OutLifecycle.ScaffoldDir);
			Paths->TryGetStringField(TEXT("registryPath"), OutLifecycle.RegistryPath);
			Paths->TryGetStringField(TEXT("pythonHandlerPath"), OutLifecycle.PythonHandlerPath);
			Paths->TryGetStringField(TEXT("manifestPath"), OutLifecycle.ManifestPath);
		}

		return true;
	}

	// =====================================================================
	// Consistency validation
	// =====================================================================

	bool ValidateLifecycleConsistency(const FToolLifecycle& Lifecycle, FString& OutInconsistencies)
	{
		OutInconsistencies.Reset();

		// callableNow must agree with state-implied callability
		const bool bStateImpliesCallable = IsLifecycleStateCallable(Lifecycle.State);
		if (Lifecycle.bCallableNow != bStateImpliesCallable)
		{
			AddInconsistency(OutInconsistencies, TEXT("callableNow"),
				FString::Printf(TEXT("reported %s but state '%s' implies %s"),
					Lifecycle.bCallableNow ? TEXT("true") : TEXT("false"),
					LifecycleStateToString(Lifecycle.State),
					bStateImpliesCallable ? TEXT("true") : TEXT("false")));
		}

		// handlerKind must agree with implementationTrack (for loaded states)
		const bool bIsLoadedState = (Lifecycle.State == ELifecycleState::LoadedCoreCppAfterRestart
			|| Lifecycle.State == ELifecycleState::LoadedUserPythonHot
			|| Lifecycle.State == ELifecycleState::SmokePassed);
		if (bIsLoadedState)
		{
			const EHandlerKind ExpectedKind = (Lifecycle.ImplementationTrack == EImplementationTrack::Python)
				? EHandlerKind::PythonBridge
				: EHandlerKind::CppDispatcher;
			if (Lifecycle.HandlerKind != ExpectedKind)
			{
				AddInconsistency(OutInconsistencies, TEXT("handlerKind"),
					FString::Printf(TEXT("loaded state '%s' implies handler '%s' but reported '%s'"),
						LifecycleStateToString(Lifecycle.State),
						HandlerKindToString(ExpectedKind),
						HandlerKindToString(Lifecycle.HandlerKind)));
			}
		}

		// sourceKind broad consistency with extensionScope
		if (Lifecycle.ExtensionScope == EExtensionScope::Core
			&& Lifecycle.SourceKind == ESourceKind::UserRegistry)
		{
			AddInconsistency(OutInconsistencies, TEXT("sourceKind"),
				TEXT("extensionScope=core but sourceKind=user_registry"));
		}
		if (Lifecycle.ExtensionScope == EExtensionScope::User
			&& Lifecycle.SourceKind == ESourceKind::CoreRegistry)
		{
			AddInconsistency(OutInconsistencies, TEXT("sourceKind"),
				TEXT("extensionScope=user but sourceKind=core_registry"));
		}

		// path-set requirements per state
		switch (Lifecycle.State)
		{
		case ELifecycleState::DraftScaffolded:
			if (Lifecycle.ScaffoldDir.IsEmpty())
			{
				AddInconsistency(OutInconsistencies, TEXT("paths.scaffoldDir"),
					TEXT("state=draft_scaffolded requires scaffoldDir to be set"));
			}
			break;
		case ELifecycleState::LoadedUserPythonHot:
		case ELifecycleState::AppliedUserPythonReloadRequired:
			if (Lifecycle.PythonHandlerPath.IsEmpty())
			{
				AddInconsistency(OutInconsistencies, TEXT("paths.pythonHandlerPath"),
					FString::Printf(TEXT("state='%s' requires pythonHandlerPath to be set"),
						LifecycleStateToString(Lifecycle.State)));
			}
			break;
		case ELifecycleState::AppliedCoreCppBuildRequired:
		case ELifecycleState::BuiltRestartRequired:
			if (Lifecycle.ManifestPath.IsEmpty())
			{
				AddInconsistency(OutInconsistencies, TEXT("paths.manifestPath"),
					FString::Printf(TEXT("state='%s' should report manifestPath from apply"),
						LifecycleStateToString(Lifecycle.State)));
			}
			break;
		default:
			break;
		}

		return OutInconsistencies.IsEmpty();
	}
}
