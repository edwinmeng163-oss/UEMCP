void RegisterGeneratedPathTraversalFixtureDescriptor(FUnrealMcpToolRegistrar& Registrar)
{
	TSharedPtr<FJsonObject> Schema = MakeObjectSchema();
	FUnrealMcpToolDescriptor Descriptor = MakeDescriptor(
		TEXT("unreal.path_traversal_fixture"),
		TEXT("Path Traversal Fixture"),
		TEXT("Fixture used to verify that scaffold apply rejects path traversal in metadata."),
		TEXT("self-extension"),
		TEXT("UnrealMcpSelfExtensionTools.cpp"),
		EUnrealMcpToolRisk::Low);
	Descriptor.bRequiresWrite = false;
	Descriptor.bDryRunSupport = true;
	Registrar.Add(Descriptor, Schema);
}
