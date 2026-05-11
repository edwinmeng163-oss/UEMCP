#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UnrealMcpEngineCompat.h"
#include "UObject/NameTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpEngineCompatFStringOutputDeviceTest,
	"UnrealMcp.EngineCompat.FStringOutputDevice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpEngineCompatFStringOutputDeviceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FStringOutputDevice Output(TEXT("prefix:"));
	Output.Serialize(TEXT("payload"), ELogVerbosity::Display, FName(TEXT("UnrealMcpEngineCompat")));

	TestEqual(
		TEXT("FStringOutputDevice preserves its initial text and appends serialized text"),
		static_cast<const FString&>(Output),
		FString(TEXT("prefix:payload")));

	return true;
}

#endif
