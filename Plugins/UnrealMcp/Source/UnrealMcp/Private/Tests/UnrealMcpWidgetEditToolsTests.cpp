#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UnrealMcpWidgetEditTools.h"
#include "WidgetBlueprint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpWidgetEditBindingHelpersTest,
	"UnrealMcp.WidgetEdit.BindingHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpWidgetEditBindingHelpersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UWidgetBlueprint* WidgetBlueprint = NewObject<UWidgetBlueprint>();
	FDelegateEditorBinding ExistingBinding;
	ExistingBinding.ObjectName = TEXT("BoundButton");
	ExistingBinding.PropertyName = TEXT("OnClicked");
	WidgetBlueprint->Bindings.Add(ExistingBinding);

	TSet<FString> BoundWidgetNames;
	BoundWidgetNames.Add(TEXT("BoundButton"));
	TestEqual(
		TEXT("Binding reference count detects a known widget binding."),
		UnrealMcp::CountWidgetBindingReferences(WidgetBlueprint, BoundWidgetNames),
		1);

	const int32 RenamedBindings = UnrealMcp::RenameWidgetBindingObjectReferences(
		WidgetBlueprint,
		TEXT("BoundButton"),
		TEXT("RenamedButton"));
	TestEqual(TEXT("Rename updates one known binding object name."), RenamedBindings, 1);
	TestEqual(
		TEXT("Binding object name survives rename with the new widget name."),
		WidgetBlueprint->Bindings[0].ObjectName,
		FString(TEXT("RenamedButton")));

	TestTrue(
		TEXT("Delete refuses referenced widgets when force=false."),
		UnrealMcp::ShouldRefuseWidgetDelete(1, false));
	TestFalse(
		TEXT("Delete allows referenced widgets when force=true."),
		UnrealMcp::ShouldRefuseWidgetDelete(1, true));

	TSet<FString> ExistingNames;
	ExistingNames.Add(TEXT("BoundButton"));
	ExistingNames.Add(TEXT("BoundButton_Copy"));
	const FString UniqueName = UnrealMcp::MakeUniqueWidgetDuplicateName(TEXT("BoundButton"), FString(), ExistingNames);
	TestEqual(
		TEXT("Duplicate name generation avoids collisions."),
		UniqueName,
		FString(TEXT("BoundButton_Copy_1")));

	return true;
}

#endif
