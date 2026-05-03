#include "UnrealMcpEditorTools.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformOutputDevices.h"
#include "Modules/ModuleManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpSettings.h"

namespace UnrealMcp
{
	FString NormalizeEndpointPath(const FString& EndpointPath);
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	int32 GetPositiveIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue);
	TArray<FAssetData> GetSelectedAssets();
	TSharedPtr<FJsonObject> MakeAssetObject(const FAssetData& Asset);
	FString DescribeAsset(const FAssetData& Asset);

	namespace
	{
		static constexpr int32 EditorToolDefaultListLimit = 200;

		FUnrealMcpExecutionResult ExecuteEditorStatus()
		{
			UEditorActorSubsystem* EditorActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
			UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			const FString CurrentMap = EditorWorld ? EditorWorld->GetOutermost()->GetName() : TEXT("");
			const bool bIsPIE = GEditor && GEditor->PlayWorld != nullptr;
			const bool bIsSimulating = GEditor && GEditor->bIsSimulatingInEditor;
			const bool bPlayRequestPending = GEditor && GEditor->GetPlaySessionRequest().IsSet();
			const FString EngineVersion = FEngineVersion::Current().ToString();
			const TArray<FAssetData> SelectedAssets = GetSelectedAssets();
			const int32 SelectedActorCount = EditorActorSubsystem ? EditorActorSubsystem->GetSelectedLevelActors().Num() : 0;

			const UUnrealMcpSettings* Settings = GetDefault<UUnrealMcpSettings>();
			const FString EndpointUrl = FString::Printf(TEXT("http://127.0.0.1:%d%s"), Settings->Port, *NormalizeEndpointPath(Settings->EndpointPath));

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("projectName"), FApp::GetProjectName());
			StructuredContent->SetStringField(TEXT("projectDir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
			StructuredContent->SetStringField(TEXT("engineVersion"), EngineVersion);
			StructuredContent->SetStringField(TEXT("currentMap"), CurrentMap);
			StructuredContent->SetBoolField(TEXT("isPlayInEditor"), bIsPIE);
			StructuredContent->SetBoolField(TEXT("isSimulatingInEditor"), bIsSimulating);
			StructuredContent->SetBoolField(TEXT("playRequestPending"), bPlayRequestPending);
			StructuredContent->SetNumberField(TEXT("selectedAssetCount"), SelectedAssets.Num());
			StructuredContent->SetNumberField(TEXT("selectedActorCount"), SelectedActorCount);
			StructuredContent->SetStringField(TEXT("endpoint"), EndpointUrl);

			const FString Text = FString::Printf(
				TEXT("Project: %s\nEngine: %s\nMap: %s\nPIE: %s\nSimulating: %s\nPlay request pending: %s\nSelected assets: %d\nSelected actors: %d\nEndpoint: %s"),
				FApp::GetProjectName(),
				*EngineVersion,
				CurrentMap.IsEmpty() ? TEXT("<none>") : *CurrentMap,
				bIsPIE ? TEXT("true") : TEXT("false"),
				bIsSimulating ? TEXT("true") : TEXT("false"),
				bPlayRequestPending ? TEXT("true") : TEXT("false"),
				SelectedAssets.Num(),
				SelectedActorCount,
				*EndpointUrl);

			return MakeExecutionResult(Text, StructuredContent, false);
		}

		FUnrealMcpExecutionResult ExecuteTailLog(const FJsonObject& Arguments)
		{
			const int32 RequestedLines = FMath::Min(GetPositiveIntArgument(Arguments, TEXT("lines"), 120), 500);
			FString ContainsFilter;
			Arguments.TryGetStringField(TEXT("contains"), ContainsFilter);
			ContainsFilter = ContainsFilter.TrimStartAndEnd();

			const FString RawEditorLogPath = FGenericPlatformOutputDevices::GetAbsoluteLogFilename();
			FString EditorLogPath = FPaths::ConvertRelativePathToFull(RawEditorLogPath);
			FPaths::NormalizeFilename(EditorLogPath);
			FString FullLogText;
			if (!FFileHelper::LoadFileToString(FullLogText, *EditorLogPath))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to read editor log '%s' (raw path: '%s')."), *EditorLogPath, *RawEditorLogPath), nullptr, true);
			}

			TArray<FString> AllLines;
			FullLogText.ParseIntoArrayLines(AllLines);

			TArray<FString> MatchingLines;
			MatchingLines.Reserve(AllLines.Num());
			if (ContainsFilter.IsEmpty())
			{
				MatchingLines = AllLines;
			}
			else
			{
				for (const FString& Line : AllLines)
				{
					if (Line.Contains(ContainsFilter, ESearchCase::IgnoreCase))
					{
						MatchingLines.Add(Line);
					}
				}
			}

			const int32 StartIndex = FMath::Max(0, MatchingLines.Num() - RequestedLines);
			TArray<FString> ReturnedLines;
			for (int32 Index = StartIndex; Index < MatchingLines.Num(); ++Index)
			{
				ReturnedLines.Add(MatchingLines[Index]);
			}

			const FString TailText = ReturnedLines.Num() > 0
				? FString::Join(ReturnedLines, TEXT("\n"))
				: (ContainsFilter.IsEmpty()
					? TEXT("The editor log is empty.")
					: FString::Printf(TEXT("No log lines matched '%s'."), *ContainsFilter));

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("logPath"), EditorLogPath);
			StructuredContent->SetStringField(TEXT("rawLogPath"), RawEditorLogPath);
			StructuredContent->SetNumberField(TEXT("requestedLines"), RequestedLines);
			StructuredContent->SetNumberField(TEXT("matchedLineCount"), MatchingLines.Num());
			StructuredContent->SetNumberField(TEXT("returnedLineCount"), ReturnedLines.Num());
			StructuredContent->SetStringField(TEXT("text"), TailText);
			if (!ContainsFilter.IsEmpty())
			{
				StructuredContent->SetStringField(TEXT("contains"), ContainsFilter);
			}

			return MakeExecutionResult(TailText, StructuredContent, false);
		}

		FUnrealMcpExecutionResult ExecuteListMaps()
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

			FARFilter Filter;
			Filter.PackagePaths.Add(TEXT("/Game"));
			Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
			Filter.bRecursivePaths = true;

			TArray<FAssetData> AssetData;
			AssetRegistryModule.Get().GetAssets(Filter, AssetData);
			AssetData.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.PackageName.ToString() < B.PackageName.ToString();
			});

			TArray<TSharedPtr<FJsonValue>> MapsArray;
			TArray<FString> TextLines;
			for (const FAssetData& Asset : AssetData)
			{
				TSharedPtr<FJsonObject> AssetObject = MakeShared<FJsonObject>();
				AssetObject->SetStringField(TEXT("packageName"), Asset.PackageName.ToString());
				AssetObject->SetStringField(TEXT("assetName"), Asset.AssetName.ToString());
				AssetObject->SetStringField(TEXT("objectPath"), Asset.GetSoftObjectPath().ToString());
				MapsArray.Add(MakeShared<FJsonValueObject>(AssetObject));
				TextLines.Add(Asset.PackageName.ToString());
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetNumberField(TEXT("count"), AssetData.Num());
			StructuredContent->SetArrayField(TEXT("maps"), MapsArray);

			const FString Text = TextLines.Num() > 0
				? FString::Printf(TEXT("Found %d maps:\n%s"), TextLines.Num(), *FString::Join(TextLines, TEXT("\n")))
				: TEXT("Found 0 maps under /Game.");

			return MakeExecutionResult(Text, StructuredContent, false);
		}

		FUnrealMcpExecutionResult ExecuteListAssets(const FJsonObject& Arguments)
		{
			FString Path = TEXT("/Game");
			bool bRecursive = true;
			FString ClassPathFilter;
			Arguments.TryGetStringField(TEXT("path"), Path);
			Arguments.TryGetBoolField(TEXT("recursive"), bRecursive);
			Arguments.TryGetStringField(TEXT("classPath"), ClassPathFilter);
			const int32 Limit = GetPositiveIntArgument(Arguments, TEXT("limit"), EditorToolDefaultListLimit);

			if (Path.IsEmpty() || !Path.StartsWith(TEXT("/")))
			{
				return MakeExecutionResult(TEXT("The path argument must be a Content Browser path like /Game."), nullptr, true);
			}

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

			FARFilter Filter;
			Filter.PackagePaths.Add(*Path);
			Filter.bRecursivePaths = bRecursive;

			TArray<FAssetData> AssetData;
			AssetRegistryModule.Get().GetAssets(Filter, AssetData);
			AssetData.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
			});

			int32 TotalMatches = 0;
			bool bTruncated = false;
			TArray<TSharedPtr<FJsonValue>> AssetsArray;
			TArray<FString> TextLines;

			for (const FAssetData& Asset : AssetData)
			{
				if (!ClassPathFilter.IsEmpty())
				{
					const FString AssetClassPath = Asset.AssetClassPath.ToString();
					if (!AssetClassPath.Equals(ClassPathFilter, ESearchCase::IgnoreCase)
						&& !AssetClassPath.Contains(ClassPathFilter, ESearchCase::IgnoreCase))
					{
						continue;
					}
				}

				++TotalMatches;

				if (AssetsArray.Num() >= Limit)
				{
					bTruncated = true;
					continue;
				}

				AssetsArray.Add(MakeShared<FJsonValueObject>(MakeAssetObject(Asset)));
				TextLines.Add(DescribeAsset(Asset));
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("path"), Path);
			StructuredContent->SetBoolField(TEXT("recursive"), bRecursive);
			StructuredContent->SetStringField(TEXT("classPath"), ClassPathFilter);
			StructuredContent->SetNumberField(TEXT("count"), TotalMatches);
			StructuredContent->SetNumberField(TEXT("returnedCount"), AssetsArray.Num());
			StructuredContent->SetBoolField(TEXT("truncated"), bTruncated);
			StructuredContent->SetArrayField(TEXT("assets"), AssetsArray);

			FString Text;
			if (TextLines.Num() > 0)
			{
				Text = FString::Printf(TEXT("Found %d assets under %s"), TotalMatches, *Path);
				if (!ClassPathFilter.IsEmpty())
				{
					Text += FString::Printf(TEXT(" filtered by %s"), *ClassPathFilter);
				}
				if (bTruncated)
				{
					Text += FString::Printf(TEXT(" (showing first %d)"), AssetsArray.Num());
				}
				Text += TEXT(":\n") + FString::Join(TextLines, TEXT("\n"));
			}
			else
			{
				Text = FString::Printf(TEXT("Found 0 assets under %s."), *Path);
			}

			return MakeExecutionResult(Text, StructuredContent, false);
		}

		FUnrealMcpExecutionResult ExecuteListSelectedAssets()
		{
			const TArray<FAssetData> SelectedAssets = GetSelectedAssets();

			TArray<TSharedPtr<FJsonValue>> AssetsArray;
			TArray<FString> TextLines;
			for (const FAssetData& Asset : SelectedAssets)
			{
				AssetsArray.Add(MakeShared<FJsonValueObject>(MakeAssetObject(Asset)));
				TextLines.Add(DescribeAsset(Asset));
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetNumberField(TEXT("count"), SelectedAssets.Num());
			StructuredContent->SetArrayField(TEXT("assets"), AssetsArray);

			const FString Text = TextLines.Num() > 0
				? FString::Printf(TEXT("Selected assets (%d):\n%s"), TextLines.Num(), *FString::Join(TextLines, TEXT("\n")))
				: TEXT("No assets are currently selected in the Content Browser.");

			return MakeExecutionResult(Text, StructuredContent, false);
		}
	}

	bool TryExecuteEditorTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.editor_status"))
		{
			OutResult = ExecuteEditorStatus();
			return true;
		}

		if (ToolName == TEXT("unreal.tail_log"))
		{
			OutResult = ExecuteTailLog(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.list_maps"))
		{
			OutResult = ExecuteListMaps();
			return true;
		}

		if (ToolName == TEXT("unreal.list_assets"))
		{
			OutResult = ExecuteListAssets(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.list_selected_assets"))
		{
			OutResult = ExecuteListSelectedAssets();
			return true;
		}

		return false;
	}
}
