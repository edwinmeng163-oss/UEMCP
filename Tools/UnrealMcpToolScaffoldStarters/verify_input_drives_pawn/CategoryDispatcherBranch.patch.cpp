// Apply near the top of the selected category TryExecute*Tool dispatcher.
if (ToolName == TEXT("unreal.simulation.verify_input_drives_pawn"))
{
	OutResult = ExecuteGeneratedVerifyInputDrivesPawnTool(ToolName, Arguments);
	return true;
}
