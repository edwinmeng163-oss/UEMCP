void RegisterGeneratedBadPatchFixtureDescriptor(FUnrealMcpToolRegistrar& Registrar)
{
	TSharedPtr<FJsonObject> Schema = MakeObjectSchema();
	FUnrealMcpToolDescriptor Descriptor = MakeDescriptor(
		TEXT("unreal.bad_patch_fixture"),
		TEXT("Bad Patch Fixture"),
		TEXT("Fixture used to verify that unsafe generated scaffold patches are rejected."),
		TEXT("self-extension"),
		TEXT("UnrealMcpSelfExtensionTools.cpp"),
		EUnrealMcpToolRisk::Low);
	Descriptor.bRequiresWrite = false;
	Descriptor.bDryRunSupport = true;
	Registrar.Add(Descriptor, Schema);
}
