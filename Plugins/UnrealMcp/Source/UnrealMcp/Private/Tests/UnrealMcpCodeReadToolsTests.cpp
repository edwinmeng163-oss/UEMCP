#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealMcpCodeTools.h"
#include "UnrealMcpModule.h"

namespace
{
	FString CodeReadToolsTestRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/UnrealMcpPyToolSamples/__CodeReadToolsTest__")));
	}

	FString CodeReadToolsProjectPath(const FString& RelativePath)
	{
		return FPaths::Combine(TEXT("Tools/UnrealMcpPyToolSamples/__CodeReadToolsTest__"), RelativePath);
	}

	void CodeReadToolsDeleteTestRoot()
	{
		IFileManager::Get().DeleteDirectory(*CodeReadToolsTestRoot(), false, true);
	}

	bool CodeReadToolsWriteFile(const FString& RelativePath, const FString& Text)
	{
		const FString FullPath = FPaths::Combine(CodeReadToolsTestRoot(), RelativePath);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), true);
		return FFileHelper::SaveStringToFile(Text, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TSharedPtr<FJsonObject> CodeReadToolsMakeArgs()
	{
		return MakeShared<FJsonObject>();
	}

	FUnrealMcpExecutionResult CodeReadToolsExecute(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
	{
		FUnrealMcpExecutionResult Result;
		UnrealMcp::TryExecuteCodeTool(ToolName, *Arguments, Result);
		return Result;
	}

	bool CodeReadToolsArrayHasExcludedPath(const TArray<TSharedPtr<FJsonValue>>* Files)
	{
		if (!Files)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Files)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Object.IsValid())
			{
				continue;
			}
			FString ProjectRelativePath;
			Object->TryGetStringField(TEXT("projectRelativePath"), ProjectRelativePath);
			if (ProjectRelativePath.StartsWith(TEXT("Binaries/"), ESearchCase::IgnoreCase)
				|| ProjectRelativePath.StartsWith(TEXT("Intermediate/"), ESearchCase::IgnoreCase)
				|| ProjectRelativePath.StartsWith(TEXT("Saved/"), ESearchCase::IgnoreCase)
				|| ProjectRelativePath.StartsWith(TEXT("Content/"), ESearchCase::IgnoreCase)
				|| ProjectRelativePath.Contains(TEXT("/Binaries/"), ESearchCase::IgnoreCase)
				|| ProjectRelativePath.Contains(TEXT("/Intermediate/"), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodeReadToolsTest,
	"UnrealMcp.Code.ReadTools",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodeReadToolsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	CodeReadToolsDeleteTestRoot();

	TestTrue(TEXT("alpha fixture writes"), CodeReadToolsWriteFile(TEXT("alpha.py"), TEXT("first line\nNeedleLiteral here\nRegexValue42 here\nlast line\n")));
	TestTrue(TEXT("beta fixture writes"), CodeReadToolsWriteFile(TEXT("nested/beta.json"), TEXT("{\"name\":\"NeedleLiteral\",\"value\":\"RegexValue77\"}\n")));
	TestTrue(TEXT("cap a fixture writes"), CodeReadToolsWriteFile(TEXT("caps/a.py"), TEXT("CapNeedle one\n")));
	TestTrue(TEXT("cap b fixture writes"), CodeReadToolsWriteFile(TEXT("caps/b.py"), TEXT("CapNeedle two\n")));
	TestTrue(TEXT("cap c fixture writes"), CodeReadToolsWriteFile(TEXT("caps/c.py"), TEXT("CapNeedle three\n")));

	{
		TSharedPtr<FJsonObject> Arguments = CodeReadToolsMakeArgs();
		Arguments->SetStringField(TEXT("path"), CodeReadToolsProjectPath(TEXT("alpha.py")));
		const FUnrealMcpExecutionResult Result = CodeReadToolsExecute(TEXT("unreal.code_read_file"), Arguments);
		TestFalse(TEXT("read allowed succeeds"), Result.bIsError);
		TestTrue(TEXT("read structured content present"), Result.StructuredContent.IsValid());
		FString Text;
		FString Sha;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(TEXT("text"), Text);
			Result.StructuredContent->TryGetStringField(TEXT("sha256"), Sha);
		}
		TestTrue(TEXT("read text includes fixture line"), Text.Contains(TEXT("NeedleLiteral")));
		TestEqual(TEXT("read sha is sha256 length"), Sha.Len(), 64);
	}

	{
		TSharedPtr<FJsonObject> Arguments = CodeReadToolsMakeArgs();
		Arguments->SetStringField(TEXT("scope"), TEXT("project"));
		Arguments->SetNumberField(TEXT("maxResults"), 2000);
		const FUnrealMcpExecutionResult Result = CodeReadToolsExecute(TEXT("unreal.code_list_files"), Arguments);
		TestFalse(TEXT("list project succeeds"), Result.bIsError);
		const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
		TestTrue(TEXT("list returns files"), Result.StructuredContent.IsValid() && Result.StructuredContent->TryGetArrayField(TEXT("files"), Files) && Files);
		TestFalse(TEXT("list excludes generated/runtime roots"), CodeReadToolsArrayHasExcludedPath(Files));
	}

	{
		TSharedPtr<FJsonObject> Arguments = CodeReadToolsMakeArgs();
		Arguments->SetStringField(TEXT("query"), TEXT("NeedleLiteral"));
		Arguments->SetStringField(TEXT("scope"), TEXT("user_tools"));
		Arguments->SetStringField(TEXT("mode"), TEXT("literal"));
		const FUnrealMcpExecutionResult Result = CodeReadToolsExecute(TEXT("unreal.code_search"), Arguments);
		TestFalse(TEXT("literal search succeeds"), Result.bIsError);
		double MatchesReturned = 0.0;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetNumberField(TEXT("matchesReturned"), MatchesReturned);
		}
		TestTrue(TEXT("literal search returns a match"), MatchesReturned >= 1.0);
	}

	{
		TSharedPtr<FJsonObject> Arguments = CodeReadToolsMakeArgs();
		Arguments->SetStringField(TEXT("query"), TEXT("RegexValue[0-9]+"));
		Arguments->SetStringField(TEXT("scope"), TEXT("user_tools"));
		Arguments->SetStringField(TEXT("mode"), TEXT("regex"));
		const FUnrealMcpExecutionResult Result = CodeReadToolsExecute(TEXT("unreal.code_search"), Arguments);
		TestFalse(TEXT("regex search succeeds"), Result.bIsError);
		double MatchesReturned = 0.0;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetNumberField(TEXT("matchesReturned"), MatchesReturned);
		}
		TestTrue(TEXT("regex search returns a match"), MatchesReturned >= 1.0);
	}

	{
		TSharedPtr<FJsonObject> Arguments = CodeReadToolsMakeArgs();
		Arguments->SetStringField(TEXT("query"), TEXT("CapNeedle"));
		Arguments->SetStringField(TEXT("scope"), TEXT("user_tools"));
		Arguments->SetNumberField(TEXT("maxMatches"), 2);
		const FUnrealMcpExecutionResult Result = CodeReadToolsExecute(TEXT("unreal.code_search"), Arguments);
		TestFalse(TEXT("capped search succeeds"), Result.bIsError);
		bool bTruncated = false;
		double MatchesReturned = 0.0;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetBoolField(TEXT("truncated"), bTruncated);
			Result.StructuredContent->TryGetNumberField(TEXT("matchesReturned"), MatchesReturned);
		}
		TestTrue(TEXT("capped search reports truncation"), bTruncated);
		TestEqual(TEXT("capped search returns requested max"), static_cast<int32>(MatchesReturned), 2);
	}

	{
		TSharedPtr<FJsonObject> FullReadArgs = CodeReadToolsMakeArgs();
		FullReadArgs->SetStringField(TEXT("path"), CodeReadToolsProjectPath(TEXT("alpha.py")));
		const FUnrealMcpExecutionResult FullRead = CodeReadToolsExecute(TEXT("unreal.code_read_file"), FullReadArgs);

		TSharedPtr<FJsonObject> SliceReadArgs = CodeReadToolsMakeArgs();
		SliceReadArgs->SetStringField(TEXT("path"), CodeReadToolsProjectPath(TEXT("alpha.py")));
		SliceReadArgs->SetNumberField(TEXT("startLine"), 2);
		SliceReadArgs->SetNumberField(TEXT("lineCount"), 1);
		const FUnrealMcpExecutionResult SliceRead = CodeReadToolsExecute(TEXT("unreal.code_read_file"), SliceReadArgs);

		FString FullSha;
		FString SliceSha;
		FString SliceText;
		if (FullRead.StructuredContent.IsValid())
		{
			FullRead.StructuredContent->TryGetStringField(TEXT("sha256"), FullSha);
		}
		if (SliceRead.StructuredContent.IsValid())
		{
			SliceRead.StructuredContent->TryGetStringField(TEXT("sha256"), SliceSha);
			SliceRead.StructuredContent->TryGetStringField(TEXT("text"), SliceText);
		}
		TestEqual(TEXT("slice read returns whole-file sha"), SliceSha, FullSha);
		TestTrue(TEXT("slice read returns requested line"), SliceText.Contains(TEXT("NeedleLiteral")));
		TestFalse(TEXT("slice read omits first line"), SliceText.Contains(TEXT("first line")));
	}

	CodeReadToolsDeleteTestRoot();
	return true;
}

#endif
