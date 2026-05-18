#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Math/Color.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpMaterialInstanceTools.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpMaterialInstanceStrictValueTest,
	"UnrealMcp.MaterialInstance.StrictValueTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpMaterialInstanceStrictValueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	{
		FJsonObject Arguments;
		Arguments.SetStringField(TEXT("value"), TEXT("0.5"));
		double ParsedValue = 0.0;
		FString FailureReason;
		TestFalse(
			TEXT("Scalar value refuses a string."),
			UnrealMcp::TryGetStrictJsonNumber(Arguments, TEXT("value"), ParsedValue, FailureReason));
		TestTrue(
			TEXT("Scalar type mismatch names the expected type."),
			FailureReason.Contains(TEXT("number")));
	}

	{
		FJsonObject Arguments;
		Arguments.SetNumberField(TEXT("value"), 0.5);
		double ParsedValue = 0.0;
		FString FailureReason;
		TestTrue(
			TEXT("Scalar value accepts a number."),
			UnrealMcp::TryGetStrictJsonNumber(Arguments, TEXT("value"), ParsedValue, FailureReason));
		TestEqual(
			TEXT("Scalar value is preserved."),
			ParsedValue,
			0.5);
	}

	{
		TSharedPtr<FJsonObject> ColorObject = MakeShared<FJsonObject>();
		ColorObject->SetNumberField(TEXT("r"), 1.0);
		ColorObject->SetNumberField(TEXT("g"), 0.25);
		ColorObject->SetNumberField(TEXT("b"), 0.2);
		ColorObject->SetNumberField(TEXT("a"), 1.0);
		FJsonObject Arguments;
		Arguments.SetObjectField(TEXT("value"), ColorObject);

		FLinearColor ParsedColor;
		FString FailureReason;
		TestTrue(
			TEXT("Vector value accepts an RGBA object."),
			UnrealMcp::TryGetStrictJsonLinearColor(Arguments, TEXT("value"), ParsedColor, FailureReason));
		TestEqual(TEXT("Vector red channel is preserved."), ParsedColor.R, 1.0f);
		TestEqual(TEXT("Vector green channel is preserved."), ParsedColor.G, 0.25f);
	}

	{
		FJsonObject Arguments;
		Arguments.SetStringField(TEXT("value"), TEXT("red"));
		FLinearColor ParsedColor;
		FString FailureReason;
		TestFalse(
			TEXT("Vector value refuses non-object input."),
			UnrealMcp::TryGetStrictJsonLinearColor(Arguments, TEXT("value"), ParsedColor, FailureReason));
		TestTrue(
			TEXT("Vector type mismatch names RGBA numeric object requirement."),
			FailureReason.Contains(TEXT("numeric r, g, b, and a")));
	}

	return true;
}

#endif
