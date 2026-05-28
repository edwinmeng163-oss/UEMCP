#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpCodeTools.h"

namespace
{
	FString CodePathPolicyTestNormalize(FString Path)
	{
		Path.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
		while (Path.Contains(TEXT("//")))
		{
			Path.ReplaceInline(TEXT("//"), TEXT("/"), ESearchCase::CaseSensitive);
		}
		while (Path.Len() > 1 && Path.EndsWith(TEXT("/")))
		{
			Path.LeftChopInline(1);
		}
		return Path;
	}

	TFunction<bool(const FString&)> CodePathPolicyTestExists(const TArray<FString>& ExistingPaths)
	{
		TSet<FString> Existing;
		for (const FString& ExistingPath : ExistingPaths)
		{
			Existing.Add(CodePathPolicyTestNormalize(ExistingPath).ToLower());
		}
		return [Existing](const FString& Candidate)
		{
			return Existing.Contains(CodePathPolicyTestNormalize(Candidate).ToLower());
		};
	}

	TFunction<bool(const FString&, FString&)> CodePathPolicyTestSymlinkResolver(const TMap<FString, FString>& Targets)
	{
		return [Targets](const FString& Path, FString& OutTarget)
		{
			const FString CleanPath = CodePathPolicyTestNormalize(Path).ToLower();
			for (const TPair<FString, FString>& Pair : Targets)
			{
				if (CodePathPolicyTestNormalize(Pair.Key).ToLower() == CleanPath)
				{
					OutTarget = Pair.Value;
					return true;
				}
			}
			return false;
		};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodePathPolicyPureTest,
	"UnrealMcp.Code.PathPolicyPure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodePathPolicyPureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using EClass = UnrealMcp::ECodePathClassification;
	const FString ProjectDir = TEXT("/Repo");
	const FString PluginBaseDir = TEXT("/Repo/Plugins/UnrealMcp");
	const auto Exists = CodePathPolicyTestExists({
		TEXT("/Repo/Tools/UnrealMcpPyTools/link/main.py"),
		TEXT("/Repo/Source"),
		TEXT("/Repo/Plugins/GamePlugin/Source")
	});
	TMap<FString, FString> SymlinkTargets;
	SymlinkTargets.Add(TEXT("/Repo/Tools/UnrealMcpPyTools/link/main.py"), TEXT("/Repo/Plugins/UnrealMcp/Source/UnrealMcpModule.cpp"));
	const auto ResolveSymlink = CodePathPolicyTestSymlinkResolver(SymlinkTargets);

	auto Classify = [&](const FString& Path)
	{
		return UnrealMcp::ClassifyCodePath_Pure(ProjectDir, PluginBaseDir, Path, Exists, ResolveSymlink);
	};

	TestTrue(TEXT("default-writable hit"), Classify(TEXT("Tools/UnrealMcpPyTools/tool/main.py")).Classification == EClass::DefaultWritable);
	TestTrue(TEXT("default-writable samples hit"), Classify(TEXT("Tools/UnrealMcpPyToolSamples/sample/tool.json")).Classification == EClass::DefaultWritable);
	TestTrue(TEXT("high-risk Source hit"), Classify(TEXT("Source/MyGame/MyActor.cpp")).Classification == EClass::HighRisk);
	TestTrue(TEXT("high-risk plugin Source hit"), Classify(TEXT("Plugins/GamePlugin/Source/GamePlugin.cpp")).Classification == EClass::HighRisk);
	TestTrue(TEXT("forbidden UnrealMcp plugin"), Classify(TEXT("Plugins/UnrealMcp/Source/UnrealMcpModule.cpp")).Classification == EClass::Forbidden);
	TestTrue(TEXT("forbidden Binaries"), Classify(TEXT("Binaries/Mac/MyGame")).Classification == EClass::Forbidden);
	TestTrue(TEXT("forbidden generated header"), Classify(TEXT("Source/MyGame/Foo.generated.h")).Classification == EClass::Forbidden);
	TestTrue(TEXT("forbidden uasset"), Classify(TEXT("Tools/UnrealMcpPyTools/tool/Asset.uasset")).Classification == EClass::Forbidden);
	TestTrue(TEXT("forbidden Engine path"), Classify(TEXT("/UE/Engine/Source/Runtime/Core/Public/CoreMinimal.h")).Classification == EClass::Forbidden);
	TestTrue(TEXT("symlink target into UnrealMcp forbidden"), Classify(TEXT("Tools/UnrealMcpPyTools/link/main.py")).Classification == EClass::Forbidden);
	TestTrue(TEXT("traversal into UnrealMcp forbidden"), Classify(TEXT("Tools/UnrealMcpPyTools/tool/../../../Plugins/UnrealMcp/Source/X.cpp")).Classification == EClass::Forbidden);
	TestTrue(TEXT("absolute outside project"), Classify(TEXT("/Other/Source/Outside.cpp")).Classification == EClass::OutsideProject);
	TestTrue(TEXT("macOS casefold variant forbidden"), Classify(TEXT("plugins/unrealmcp/Source/X.cpp")).Classification == EClass::Forbidden);
	TestTrue(TEXT("extension allowlist rejects markdown writes"), Classify(TEXT("Tools/UnrealMcpPyTools/tool/README.md")).Classification == EClass::Forbidden);
	TestTrue(TEXT("create_file parent into high-risk remains high-risk"), Classify(TEXT("Source/NewFolder/NewActor.cpp")).Classification == EClass::HighRisk);
	TestTrue(TEXT("Saved/CodeChanges target rejected"), Classify(TEXT("Saved/UnrealMcp/CodeChanges/Previews/preview.json")).Classification == EClass::Forbidden);

	FString ComposedPath = TEXT("Tools/UnrealMcpPyTools/Caf");
	ComposedPath.AppendChar(static_cast<TCHAR>(0x00e9));
	ComposedPath += TEXT("/main.py");
	FString DecomposedPath = TEXT("Tools/UnrealMcpPyTools/Cafe");
	DecomposedPath.AppendChar(static_cast<TCHAR>(0x0301));
	DecomposedPath += TEXT("/main.py");
	const UnrealMcp::FCodePathPolicy Composed = Classify(ComposedPath);
	const UnrealMcp::FCodePathPolicy Decomposed = Classify(DecomposedPath);
	TestTrue(TEXT("NFC composed path is default writable"), Composed.Classification == EClass::DefaultWritable);
	TestTrue(TEXT("NFC decomposed equivalent is default writable"), Decomposed.Classification == EClass::DefaultWritable);
	TestEqual(TEXT("NFC-equivalent canonical paths match"), Decomposed.CanonicalPath, Composed.CanonicalPath);

	return true;
}

#endif
