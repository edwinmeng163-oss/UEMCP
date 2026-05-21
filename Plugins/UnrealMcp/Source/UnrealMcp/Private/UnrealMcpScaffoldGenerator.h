// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.
#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"
#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpModule.h"  // for FUnrealMcpExecutionResult

namespace UnrealMcp::Scaffold
{
	struct FPythonScaffoldResult
	{
		bool bSuccess = false;
		FString ToolId;
		FString ToolName;
		FString ScaffoldDir;
		FString MainPyPath;
		FString ToolJsonPath;
		FString ReadmePath;
		FString PythonHandlerSha256;
		FString Error;
		::UnrealMcp::Extension::FToolLifecycle Lifecycle;
	};

	// Generates a complete Python scaffold draft at
	//   <projDir>/Tools/UnrealMcpPyTools/<ToolId>/
	// with main.py / tool.json / README.md (no patch.cpp files).
	//
	// Validates ToolId per the single-file rule:
	//   alphanumeric + underscore only; reject paths / slashes / drives.
	//
	// Reads optional fields from Arguments:
	//   - "toolName" (string) - existing public input; python track derives ToolId by
	//     stripping a leading "user." or legacy "unreal." prefix
	//   - "description" (string) - populated into tool.json + README
	//   - "category" (string, default "user")
	//   - "exposure" (string, default "visible")
	//   - "wouldWriteHints" (array of strings) - for tool.json wouldWrite manifest
	//   - "importAllowlist" (array of strings, default empty)
	//
	// Aborts and returns bSuccess=false if:
	//   - ToolId invalid
	//   - target dir already exists (no overwrite; user must explicitly remove first)
	//   - file write fails
	UNREALMCP_API FPythonScaffoldResult GeneratePythonScaffoldFiles(const FString& ToolId, const FJsonObject& Arguments);

	// Returns the FUnrealMcpExecutionResult wrapping a FPythonScaffoldResult
	// with structuredContent.lifecycle populated per Phase 0 schema.
	UNREALMCP_API FUnrealMcpExecutionResult BuildScaffoldExecutionResult(const FPythonScaffoldResult& Scaffold);
}
