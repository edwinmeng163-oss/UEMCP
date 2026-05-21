#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpExtensionLifecycle.h"

namespace LE = UnrealMcp::Extension;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpLifecycle_StateToStringRoundTripTest,
	"UnrealMcp.Lifecycle.StateToStringRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpLifecycle_StateToStringRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<LE::ELifecycleState> States = {
		LE::ELifecycleState::DraftScaffolded,
		LE::ELifecycleState::DryRunReady,
		LE::ELifecycleState::ApprovalRequired,
		LE::ELifecycleState::AppliedCoreCppBuildRequired,
		LE::ELifecycleState::BuiltRestartRequired,
		LE::ELifecycleState::LoadedCoreCppAfterRestart,
		LE::ELifecycleState::AppliedUserPythonReloadRequired,
		LE::ELifecycleState::LoadedUserPythonHot,
		LE::ELifecycleState::SmokePassed,
		LE::ELifecycleState::SmokeFailed,
		LE::ELifecycleState::Blocked,
		LE::ELifecycleState::RolledBack
	};

	for (LE::ELifecycleState State : States)
	{
		const FString Encoded = LE::LifecycleStateToString(State);
		const LE::ELifecycleState Decoded = LE::LifecycleStateFromString(Encoded);
		TestEqual(*FString::Printf(TEXT("round-trip %s"), *Encoded), static_cast<uint8>(Decoded), static_cast<uint8>(State));
	}
	TestEqual(
		TEXT("unknown lifecycle state fallback"),
		static_cast<uint8>(LE::LifecycleStateFromString(TEXT("unknown_garbage"), LE::ELifecycleState::DraftScaffolded)),
		static_cast<uint8>(LE::ELifecycleState::DraftScaffolded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpLifecycle_SourceKindToStringRoundTripTest,
	"UnrealMcp.Lifecycle.SourceKindToStringRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpLifecycle_SourceKindToStringRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<LE::ESourceKind> Kinds = {
		LE::ESourceKind::CoreRegistry,
		LE::ESourceKind::UserRegistry,
		LE::ESourceKind::DescriptorOnly,
		LE::ESourceKind::HandlerOnly,
		LE::ESourceKind::MissingHandler,
		LE::ESourceKind::PythonHandlerMissing,
		LE::ESourceKind::PythonShaMismatch
	};

	for (LE::ESourceKind Kind : Kinds)
	{
		const FString Encoded = LE::SourceKindToString(Kind);
		const LE::ESourceKind Decoded = LE::SourceKindFromString(Encoded);
		TestEqual(*FString::Printf(TEXT("round-trip %s"), *Encoded), static_cast<uint8>(Decoded), static_cast<uint8>(Kind));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpLifecycle_HandlerKindToStringRoundTripTest,
	"UnrealMcp.Lifecycle.HandlerKindToStringRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpLifecycle_HandlerKindToStringRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<LE::EHandlerKind> Kinds = {
		LE::EHandlerKind::CppDispatcher,
		LE::EHandlerKind::PythonBridge,
		LE::EHandlerKind::None
	};

	for (LE::EHandlerKind Kind : Kinds)
	{
		const FString Encoded = LE::HandlerKindToString(Kind);
		const LE::EHandlerKind Decoded = LE::HandlerKindFromString(Encoded);
		TestEqual(*FString::Printf(TEXT("round-trip %s"), *Encoded), static_cast<uint8>(Decoded), static_cast<uint8>(Kind));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpLifecycle_DerivedFlagsTest,
	"UnrealMcp.Lifecycle.DerivedFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpLifecycle_DerivedFlagsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	auto ExpectFlags = [this](
		LE::ELifecycleState State,
		bool bApproval,
		bool bBuild,
		bool bRestart,
		bool bReload,
		bool bSmoke)
	{
		const LE::FDerivedLifecycleFlags Flags = LE::DeriveLifecycleFlags(State);
		const FString Prefix = LE::LifecycleStateToString(State);
		TestEqual(*FString::Printf(TEXT("%s requiresApproval"), *Prefix), Flags.bRequiresApproval, bApproval);
		TestEqual(*FString::Printf(TEXT("%s requiresBuild"), *Prefix), Flags.bRequiresBuild, bBuild);
		TestEqual(*FString::Printf(TEXT("%s requiresRestart"), *Prefix), Flags.bRequiresRestart, bRestart);
		TestEqual(*FString::Printf(TEXT("%s requiresReload"), *Prefix), Flags.bRequiresReload, bReload);
		TestEqual(*FString::Printf(TEXT("%s requiresSmoke"), *Prefix), Flags.bRequiresSmoke, bSmoke);
	};

	ExpectFlags(LE::ELifecycleState::DraftScaffolded, false, false, false, true, true);
	ExpectFlags(LE::ELifecycleState::DryRunReady, false, false, false, false, false);
	ExpectFlags(LE::ELifecycleState::ApprovalRequired, true, false, false, false, false);
	ExpectFlags(LE::ELifecycleState::AppliedCoreCppBuildRequired, false, true, true, false, true);
	ExpectFlags(LE::ELifecycleState::BuiltRestartRequired, false, false, true, false, true);
	ExpectFlags(LE::ELifecycleState::LoadedCoreCppAfterRestart, false, false, false, false, true);
	ExpectFlags(LE::ELifecycleState::AppliedUserPythonReloadRequired, false, false, false, true, true);
	ExpectFlags(LE::ELifecycleState::LoadedUserPythonHot, false, false, false, false, true);
	ExpectFlags(LE::ELifecycleState::SmokePassed, false, false, false, false, false);
	ExpectFlags(LE::ELifecycleState::SmokeFailed, false, false, false, false, false);
	ExpectFlags(LE::ELifecycleState::Blocked, false, false, false, false, false);
	ExpectFlags(LE::ELifecycleState::RolledBack, false, false, false, false, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpLifecycle_CallableNowTest,
	"UnrealMcp.Lifecycle.CallableNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpLifecycle_CallableNowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<LE::ELifecycleState> States = {
		LE::ELifecycleState::DraftScaffolded,
		LE::ELifecycleState::DryRunReady,
		LE::ELifecycleState::ApprovalRequired,
		LE::ELifecycleState::AppliedCoreCppBuildRequired,
		LE::ELifecycleState::BuiltRestartRequired,
		LE::ELifecycleState::LoadedCoreCppAfterRestart,
		LE::ELifecycleState::AppliedUserPythonReloadRequired,
		LE::ELifecycleState::LoadedUserPythonHot,
		LE::ELifecycleState::SmokePassed,
		LE::ELifecycleState::SmokeFailed,
		LE::ELifecycleState::Blocked,
		LE::ELifecycleState::RolledBack
	};

	for (LE::ELifecycleState State : States)
	{
		const bool bExpectedCallable = State == LE::ELifecycleState::SmokePassed;
		TestEqual(*FString::Printf(TEXT("%s callable"), LE::LifecycleStateToString(State)), LE::IsLifecycleStateCallable(State), bExpectedCallable);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpLifecycle_JsonBuildParseRoundTripTest,
	"UnrealMcp.Lifecycle.JsonBuildParseRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpLifecycle_JsonBuildParseRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	LE::FToolLifecycle Lifecycle;
	Lifecycle.ToolName = TEXT("user.x");
	Lifecycle.ExtensionScope = LE::EExtensionScope::User;
	Lifecycle.ImplementationTrack = LE::EImplementationTrack::Python;
	Lifecycle.HandlerKind = LE::EHandlerKind::PythonBridge;
	Lifecycle.SourceKind = LE::ESourceKind::UserRegistry;
	Lifecycle.State = LE::ELifecycleState::LoadedUserPythonHot;
	Lifecycle.bCallableNow = false;
	Lifecycle.NextRequiredAction = TEXT("smoke");
	Lifecycle.ScaffoldDir = TEXT("/tmp/user.x");
	Lifecycle.RegistryPath = TEXT("/tmp/user.x/tool.json");
	Lifecycle.PythonHandlerPath = TEXT("/tmp/user.x/main.py");
	Lifecycle.ManifestPath = TEXT("/tmp/Manifest.json");

	const TSharedPtr<FJsonObject> LifecycleJson = LE::BuildLifecycleJson(Lifecycle);
	TSharedPtr<FJsonObject> Wrapped = MakeShared<FJsonObject>();
	Wrapped->SetObjectField(TEXT("lifecycle"), LifecycleJson);

	LE::FToolLifecycle Parsed;
	TestTrue(TEXT("parse lifecycle"), LE::ParseLifecycleJson(Wrapped, Parsed));
	TestEqual(TEXT("toolName"), Parsed.ToolName, Lifecycle.ToolName);
	TestEqual(TEXT("state"), static_cast<uint8>(Parsed.State), static_cast<uint8>(Lifecycle.State));
	TestEqual(TEXT("callableNow"), Parsed.bCallableNow, Lifecycle.bCallableNow);
	TestEqual(TEXT("pythonHandlerPath"), Parsed.PythonHandlerPath, Lifecycle.PythonHandlerPath);

	const TSharedPtr<FJsonObject> WithFlags = LE::BuildLifecycleJson(Lifecycle, true);
	const TSharedPtr<FJsonObject>* DerivedFlags = nullptr;
	TestTrue(TEXT("derivedFlags included"), WithFlags->TryGetObjectField(TEXT("derivedFlags"), DerivedFlags) && DerivedFlags && (*DerivedFlags).IsValid());

	const TSharedPtr<FJsonObject> WithoutFlags = LE::BuildLifecycleJson(Lifecycle, false);
	TestFalse(TEXT("derivedFlags absent"), WithoutFlags->HasField(TEXT("derivedFlags")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpLifecycle_ConsistencyValidationTest,
	"UnrealMcp.Lifecycle.ConsistencyValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpLifecycle_ConsistencyValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FString Inconsistencies;

	LE::FToolLifecycle Consistent;
	Consistent.ToolName = TEXT("user.x");
	Consistent.State = LE::ELifecycleState::SmokePassed;
	Consistent.bCallableNow = true;
	Consistent.HandlerKind = LE::EHandlerKind::PythonBridge;
	Consistent.ImplementationTrack = LE::EImplementationTrack::Python;
	Consistent.ExtensionScope = LE::EExtensionScope::User;
	Consistent.SourceKind = LE::ESourceKind::UserRegistry;
	TestTrue(TEXT("consistent lifecycle"), LE::ValidateLifecycleConsistency(Consistent, Inconsistencies));
	TestTrue(TEXT("consistent has no inconsistencies"), Inconsistencies.IsEmpty());

	LE::FToolLifecycle BadCallable = Consistent;
	BadCallable.bCallableNow = false;
	TestFalse(TEXT("callableNow inconsistency"), LE::ValidateLifecycleConsistency(BadCallable, Inconsistencies));
	TestTrue(TEXT("callableNow reason"), Inconsistencies.Contains(TEXT("callableNow")));

	LE::FToolLifecycle BadHandler;
	BadHandler.ToolName = TEXT("unreal.x");
	BadHandler.State = LE::ELifecycleState::LoadedCoreCppAfterRestart;
	BadHandler.bCallableNow = false;
	BadHandler.HandlerKind = LE::EHandlerKind::PythonBridge;
	BadHandler.ImplementationTrack = LE::EImplementationTrack::Cpp;
	BadHandler.ExtensionScope = LE::EExtensionScope::Core;
	BadHandler.SourceKind = LE::ESourceKind::CoreRegistry;
	TestFalse(TEXT("handlerKind inconsistency"), LE::ValidateLifecycleConsistency(BadHandler, Inconsistencies));
	TestTrue(TEXT("handlerKind reason"), Inconsistencies.Contains(TEXT("handlerKind")));

	LE::FToolLifecycle BadSource;
	BadSource.ToolName = TEXT("unreal.x");
	BadSource.State = LE::ELifecycleState::Blocked;
	BadSource.bCallableNow = false;
	BadSource.ExtensionScope = LE::EExtensionScope::Core;
	BadSource.SourceKind = LE::ESourceKind::UserRegistry;
	TestFalse(TEXT("sourceKind inconsistency"), LE::ValidateLifecycleConsistency(BadSource, Inconsistencies));
	TestTrue(TEXT("sourceKind reason"), Inconsistencies.Contains(TEXT("sourceKind")));

	LE::FToolLifecycle BadScaffold;
	BadScaffold.ToolName = TEXT("unreal.x");
	BadScaffold.State = LE::ELifecycleState::DraftScaffolded;
	BadScaffold.bCallableNow = false;
	BadScaffold.ExtensionScope = LE::EExtensionScope::Core;
	BadScaffold.SourceKind = LE::ESourceKind::DescriptorOnly;
	TestFalse(TEXT("scaffoldDir inconsistency"), LE::ValidateLifecycleConsistency(BadScaffold, Inconsistencies));
	TestTrue(TEXT("scaffoldDir reason"), Inconsistencies.Contains(TEXT("paths.scaffoldDir")));

	LE::FToolLifecycle BadPythonPath;
	BadPythonPath.ToolName = TEXT("user.x");
	BadPythonPath.State = LE::ELifecycleState::LoadedUserPythonHot;
	BadPythonPath.bCallableNow = false;
	BadPythonPath.HandlerKind = LE::EHandlerKind::PythonBridge;
	BadPythonPath.ImplementationTrack = LE::EImplementationTrack::Python;
	BadPythonPath.ExtensionScope = LE::EExtensionScope::User;
	BadPythonPath.SourceKind = LE::ESourceKind::UserRegistry;
	TestFalse(TEXT("pythonHandlerPath inconsistency"), LE::ValidateLifecycleConsistency(BadPythonPath, Inconsistencies));
	TestTrue(TEXT("pythonHandlerPath reason"), Inconsistencies.Contains(TEXT("paths.pythonHandlerPath")));

	return true;
}

#endif
