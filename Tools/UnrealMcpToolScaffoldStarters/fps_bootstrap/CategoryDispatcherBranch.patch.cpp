// Apply near the top of the selected category TryExecute*Tool dispatcher.
if (ToolName == TEXT("unreal.fps.bootstrap"))
{
	OutResult = ExecuteGeneratedFpsBootstrapTool(ToolName, Arguments);
	return true;
}
