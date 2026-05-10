FUnrealMcpExecutionResult ExecuteGeneratedPathTraversalFixtureTool(const FString& ToolName, const FJsonObject& Arguments)
{
	FString Message;
	Arguments.TryGetStringField(TEXT("message"), Message);

	TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
	StructuredContent->SetStringField(TEXT("action"), TEXT("path_traversal_fixture"));
	StructuredContent->SetStringField(TEXT("toolName"), ToolName);
	StructuredContent->SetStringField(TEXT("message"), Message);
	return MakeExecutionResult(TEXT("Path traversal fixture completed."), StructuredContent, false);
}
