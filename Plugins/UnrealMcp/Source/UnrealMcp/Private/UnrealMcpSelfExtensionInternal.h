#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpSharedPathResolver.h"
#include "Templates/Function.h"

class FJsonObject;
class FJsonValue;

namespace UnrealMcp
{
	int32 GetPositiveIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue);
	TSharedPtr<FJsonObject> MakeEmptyObject();
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	FUnrealMcpExecutionResult AuditMcpTools(const TArray<TSharedPtr<FJsonValue>>& ToolsArray);
	bool ResolveProjectPathInsideProject(const FString& RequestedPath, FString& OutPath, FString& OutFailureReason);
	bool ResolveProjectOutputDirectory(const FString& RequestedOutputRoot, FString& OutDirectory, FString& OutFailureReason);
	bool ResolveScaffoldReadDirectory(
		const FString& ToolId,
		FString& OutScaffoldDirectory,
		FString& OutFailureReason,
		FToolsReadResolution* OutResolution = nullptr);
	FToolsReadResolution ResolveScaffoldReadDirectory_Pure(
		const FString& ProjectDir,
		const FString& PluginBaseDir,
		const FString& ToolId,
		TFunctionRef<bool(const FString&)> FileOrDirExists);
	FString SanitizeMcpToolIdForPath(const FString& ToolName);
	FString GetMcpModuleSourcePath();
	FString GetMcpModuleHeaderPath();
	FString GetProjectReadmePath();
	FString GetPluginReadmePath();
	FString GetUnrealMcpSavedRoot();
	FString GetProjectMemoryFilePath();
	FString GetMcpExtensionBackupRoot();
	FString GetMcpBuildLogRoot();
	FString GetLatestMcpExtensionManifestPath();
	FString GetMcpExtensionLockPath();
	FString GetMcpProjectStateBackupRoot();
	FString HashTextForManifest(const FString& Text);
	struct FUserToolRollbackPlan
	{
		FString ToolId;
		FString ToolName;
		FString UserToolsRoot;
		FString ToolDirectory;
		TArray<FString> DeletionTargets;
	};
	struct FRollbackManifestSafety
	{
		FString ToolName;
		bool bIsCoreManifest = false;
		bool bTouchesCoreFiles = false;
		bool bManifestDriftDetected = false;
		bool bRefuseCoreRollback = false;
		FString RefusalReason;
		TArray<FString> SourcePaths;
	};
	bool BuildUserToolRollbackPlanForProject(
		const FString& ProjectDir,
		const FString& ToolNameOrPath,
		FUserToolRollbackPlan& OutPlan,
		FString& OutFailureReason);
	FUnrealMcpExecutionResult RollbackUserToolForProjectRoot(
		const FString& ProjectDir,
		const FString& ToolNameOrPath,
		bool bDryRun,
		TFunctionRef<FUnrealMcpExecutionResult()> ReloadRegistry);
	bool EvaluateRollbackManifestSafetyForProjectRoot(
		const FString& ProjectDir,
		const FString& PluginSourceRoot,
		const TSharedPtr<FJsonObject>& ManifestObject,
		bool bManifestPathExplicit,
		bool bForceExplicit,
		FRollbackManifestSafety& OutSafety,
		FString& OutFailureReason);
	FString MakePathRelativeToProject(const FString& Path);
	FString FileTimeToIsoString(const FDateTime& Time);
	bool IsPathInsideDirectory(const FString& Path, const FString& Directory);
	bool ResolvePathInsideTrustedSourceDomains(
		const FString& RequestedPath,
		FString& OutPath,
		FToolsReadResolution::ESource& OutSourceKind,
		FString& OutFailureReason);
	bool LoadJsonObject(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject);
	bool SaveJsonObjectToFile(const TSharedPtr<FJsonObject>& Object, const FString& FilePath, FString& OutFailureReason);
	TSharedPtr<FJsonObject> NormalizeOpenAiSchemaObject(const TSharedPtr<FJsonObject>& InputObject);
	bool IsOpenAiSchemaCompatibleObject(const TSharedPtr<FJsonObject>& InputObject, FString& OutReason);
	bool LoadProjectMemory(TSharedPtr<FJsonObject>& OutMemory, FString& OutFailureReason);
	bool LoadJsonObjectFromFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutObject, FString& OutFailureReason);
	void FindImmediateChildren(const FString& Directory, const FString& Pattern, bool bFiles, bool bDirectories, TArray<FString>& OutChildren);
	bool FindNewestFile(const FString& Directory, const FString& Pattern, FString& OutPath);
	TSharedPtr<FJsonObject> MakeFileInfoObject(const FString& Path);
	TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Strings);
	FString JsonObjectToString(const TSharedPtr<FJsonObject>& Object);
	TSharedPtr<FJsonObject> MakeMemoryEntrySummary(const TSharedPtr<FJsonObject>& EntryObject, bool bIncludeContent);
	TSharedPtr<FJsonObject> FindMemoryEntryByKey(const TSharedPtr<FJsonObject>& MemoryObject, const FString& Key);
	FString TailLines(const FString& Text, int32 MaxLines);
	FString RecommendPipelineNextStep(const TSharedPtr<FJsonObject>& MemoryEntry);
	void WriteBuildTestMemory(
		const FString& MemoryKey,
		const FString& Summary,
		const FString& Status,
		const FString& NextStep,
		const TSharedPtr<FJsonObject>& Content);
	TSharedPtr<FJsonObject> MakePipelineStepObject(
		const FString& StepName,
		const FString& Status,
		const FString& Message,
		const FUnrealMcpExecutionResult* Result = nullptr);
	void AddAuditIssue(
		TArray<TSharedPtr<FJsonValue>>& Issues,
		const FString& Severity,
		const FString& Location,
		const FString& Message);
	bool ResolveMcpScaffoldDirectory(
		const FJsonObject& Arguments,
		FString& OutDirectory,
		FString& OutToolName,
		FString& OutFailureReason,
		FToolsReadResolution* OutResolution = nullptr);
	TSharedPtr<FJsonObject> FindToolDefinitionByName(const TArray<TSharedPtr<FJsonValue>>& ToolsArray, const FString& ToolName);
	bool ExtractRequestedSchemaFromScaffoldReadme(const FString& ScaffoldDirectory, FString& OutSchemaJson);
	TSharedPtr<FJsonObject> ValidateCppSnippetText(
		const FString& SnippetText,
		const FString& SnippetName,
		const FString& ToolName);
	FUnrealMcpExecutionResult ValidateMcpToolSchema(const FJsonObject& Arguments, const TArray<TSharedPtr<FJsonValue>>& ToolsArray);
	FUnrealMcpExecutionResult ExportToolPackage(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult ListExportableToolPackages(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult ImportToolPackage(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult GenerateMcpTests(const FJsonObject& Arguments, const TArray<TSharedPtr<FJsonValue>>& ToolsArray);
	FUnrealMcpExecutionResult ApplyMcpScaffold(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult RollbackLastMcpExtension(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult LockExtensionSession(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult BackupProjectState(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult RollbackToManifest(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult BuildEditor(const FJsonObject& Arguments);
	bool TryExecuteSelfExtensionBuildTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);
	FUnrealMcpExecutionResult SupervisorInstall(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult ListMcpScaffolds(const FJsonObject& Arguments, const TArray<TSharedPtr<FJsonValue>>& ToolsArray);
	FUnrealMcpExecutionResult InspectMcpScaffold(const FJsonObject& Arguments, const TArray<TSharedPtr<FJsonValue>>& ToolsArray);
	FUnrealMcpExecutionResult ValidateCppSnippet(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult PatchScaffoldSnippet(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult CompileErrorFixPlan(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult DiffLastMcpApply(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult CleanMcpTestArtifacts(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult PreviewChangePlan(const FJsonObject& Arguments, const TArray<TSharedPtr<FJsonValue>>& ToolsArray);
	FUnrealMcpExecutionResult CaptureProjectSnapshot(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult DiffProjectSnapshot(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult VerifyTaskOutcome(const FJsonObject& Arguments, const TArray<TSharedPtr<FJsonValue>>& ToolsArray);
	FUnrealMcpExecutionResult ClassifyMcpError(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult PrepareTestSandbox(const FJsonObject& Arguments);
	bool IsEditorPlaying();
	FUnrealMcpExecutionResult MakePieBlockedResult(const FString& ToolName);
	bool TryAcquireExtensionSessionLock(
		const FString& Owner,
		const FString& Reason,
		int32 TtlSeconds,
		bool bForce,
		FString& OutSessionId,
		TSharedPtr<FJsonObject>& OutLockObject,
		FString& OutFailureReason);
	bool ReleaseExtensionSessionLock(const FString& SessionId, bool bForce, FString& OutFailureReason);
	bool ResolveMcpTestsDirectory(
		const FJsonObject& Arguments,
		FString& OutTestsDirectory,
		FString& OutScaffoldDirectory,
		FString& OutToolName,
		FString& OutFailureReason,
		TArray<FString>* OutCandidateRoots = nullptr);
	void FindMcpTestJsonFilesRecursive(const FString& Directory, TArray<FString>& OutFiles);
}
