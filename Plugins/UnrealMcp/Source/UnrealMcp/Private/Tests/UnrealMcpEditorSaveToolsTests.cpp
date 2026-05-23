#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"
#include "UnrealMcpModule.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace
{
	class FEditorSaveDirtyPackageStateScope
	{
	public:
		FEditorSaveDirtyPackageStateScope()
		{
			for (TObjectIterator<UPackage> PackageIt; PackageIt; ++PackageIt)
			{
				UPackage* Package = *PackageIt;
				if (Package && Package->IsDirty())
				{
					InitiallyDirtyPackages.Add(Package);
					Package->SetDirtyFlag(false);
				}
			}
		}

		~FEditorSaveDirtyPackageStateScope()
		{
			for (const TWeakObjectPtr<UPackage>& Package : InitiallyDirtyPackages)
			{
				if (Package.IsValid())
				{
					Package.Get()->SetDirtyFlag(true);
				}
			}
		}

	private:
		TArray<TWeakObjectPtr<UPackage>> InitiallyDirtyPackages;
	};

	FUnrealMcpExecutionResult ExecuteSaveDirtyPackagesForTest(const FJsonObject& Arguments)
	{
		FUnrealMcpModule& Module = FModuleManager::LoadModuleChecked<FUnrealMcpModule>(TEXT("UnrealMcp"));
		return Module.ExecuteToolFromEditorUI(TEXT("unreal.save_dirty_packages"), Arguments);
	}

	int32 GetStructuredInt(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		double Value = 0.0;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetNumberField(FieldName, Value);
		}
		return static_cast<int32>(Value);
	}

	FString GetStructuredString(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		FString Value;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	bool GetStructuredBool(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		bool bValue = false;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetBoolField(FieldName, bValue);
		}
		return bValue;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetStructuredArray(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetArrayField(FieldName, Values);
		}
		return Values;
	}

	bool StringArrayContains(const TArray<TSharedPtr<FJsonValue>>* Values, const FString& Expected)
	{
		if (!Values)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Actual;
			if (Value.IsValid() && Value->TryGetString(Actual) && Actual == Expected)
			{
				return true;
			}
		}
		return false;
	}

	bool SkippedPackagesContains(const TArray<TSharedPtr<FJsonValue>>* Values, const FString& ExpectedName, const FString& ExpectedReason)
	{
		if (!Values)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid())
			{
				continue;
			}

			FString Name;
			FString Reason;
			(*Object)->TryGetStringField(TEXT("name"), Name);
			(*Object)->TryGetStringField(TEXT("reason"), Reason);
			if (Name == ExpectedName && Reason == ExpectedReason)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpSaveDirtyPackagesNoDirtyDiagnosticsTest,
	"UnrealMcp.Editor.SaveDirtyPackages.NoDirtyDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpSaveDirtyPackagesNoDirtyDiagnosticsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEditorSaveDirtyPackageStateScope DirtyStateScope;

	FJsonObject Arguments;
	const FUnrealMcpExecutionResult Result = ExecuteSaveDirtyPackagesForTest(Arguments);

	TestFalse(TEXT("save_dirty_packages succeeds without dirty packages"), Result.bIsError);
	TestTrue(TEXT("structured content is present"), Result.StructuredContent.IsValid());
	if (!Result.StructuredContent.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("savedPackages"), GetStructuredInt(Result, TEXT("savedPackages")), 0);
	TestEqual(TEXT("reason"), GetStructuredString(Result, TEXT("reason")), TEXT("no_dirty_packages"));
	TestFalse(TEXT("isPlayInEditor"), GetStructuredBool(Result, TEXT("isPlayInEditor")));

	const TArray<TSharedPtr<FJsonValue>>* DirtyBefore = GetStructuredArray(Result, TEXT("dirtyPackagesBefore"));
	const TArray<TSharedPtr<FJsonValue>>* DirtyAfter = GetStructuredArray(Result, TEXT("dirtyPackagesAfter"));
	const TArray<TSharedPtr<FJsonValue>>* SkippedPackages = GetStructuredArray(Result, TEXT("skippedPackages"));
	TestTrue(TEXT("dirtyPackagesBefore is present and empty"), DirtyBefore && DirtyBefore->Num() == 0);
	TestTrue(TEXT("dirtyPackagesAfter is present and empty"), DirtyAfter && DirtyAfter->Num() == 0);
	TestTrue(TEXT("skippedPackages is present and empty"), SkippedPackages && SkippedPackages->Num() == 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpSaveDirtyPackagesSaveNotRequestedDiagnosticsTest,
	"UnrealMcp.Editor.SaveDirtyPackages.SaveNotRequestedDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpSaveDirtyPackagesSaveNotRequestedDiagnosticsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEditorSaveDirtyPackageStateScope DirtyStateScope;

	const FString PackageName = FString::Printf(TEXT("/Temp/UEAtelierSaveDirtyPackagesTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* Package = CreatePackage(*PackageName);
	TestNotNull(TEXT("test package created"), Package);
	if (!Package)
	{
		return false;
	}
	Package->SetDirtyFlag(true);

	FJsonObject Arguments;
	Arguments.SetBoolField(TEXT("saveMaps"), false);
	Arguments.SetBoolField(TEXT("saveAssets"), false);

	const FUnrealMcpExecutionResult Result = ExecuteSaveDirtyPackagesForTest(Arguments);
	Package->SetDirtyFlag(false);

	TestFalse(TEXT("save_dirty_packages reports diagnostics without saving when save flags are false"), Result.bIsError);
	TestTrue(TEXT("structured content is present"), Result.StructuredContent.IsValid());
	if (!Result.StructuredContent.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("savedPackages"), GetStructuredInt(Result, TEXT("savedPackages")), 0);
	TestEqual(TEXT("reason"), GetStructuredString(Result, TEXT("reason")), TEXT("save_not_requested"));
	TestFalse(TEXT("isPlayInEditor"), GetStructuredBool(Result, TEXT("isPlayInEditor")));

	const TArray<TSharedPtr<FJsonValue>>* DirtyBefore = GetStructuredArray(Result, TEXT("dirtyPackagesBefore"));
	const TArray<TSharedPtr<FJsonValue>>* DirtyAfter = GetStructuredArray(Result, TEXT("dirtyPackagesAfter"));
	const TArray<TSharedPtr<FJsonValue>>* SkippedPackages = GetStructuredArray(Result, TEXT("skippedPackages"));
	TestTrue(TEXT("dirtyPackagesBefore includes test package"), StringArrayContains(DirtyBefore, PackageName));
	TestTrue(TEXT("dirtyPackagesAfter includes test package"), StringArrayContains(DirtyAfter, PackageName));
	TestTrue(TEXT("skippedPackages includes save-not-requested test package"), SkippedPackagesContains(SkippedPackages, PackageName, TEXT("save_not_requested")));

	return true;
}

#endif
