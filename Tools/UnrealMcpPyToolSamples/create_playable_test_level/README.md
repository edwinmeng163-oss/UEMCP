# create_playable_test_level

Sample Python user tool for the UEAtelier project-local Python track. This is not a core MCP tool and is not part of the canonical ToolRegistry count.

## Location

This committed sample lives at:

```text
Tools/UnrealMcpPyToolSamples/create_playable_test_level/
```

To make it callable in an editor project, copy the directory to:

```text
<ProjectDir>/Tools/UnrealMcpPyTools/create_playable_test_level/
```

Then run `unreal.mcp_user_registry_reload` and `unreal.mcp_user_tool_smoke` before calling `user.create_playable_test_level`.

## Behavior

`dryRun` defaults to `true`. A real run creates its own map under `/Game/levels/<levelName>`, refuses the protected `Lvl_TopDown` name, and refuses to overwrite an existing map asset when the editor asset subsystem can detect it. The sample adds a cube ground surface, directional light, skylight, optional sky atmosphere, PlayerStart, and optionally a character set to Auto Possess Player 0.

The result reports the new map package, spawned actor labels, save attempts, actor-presence verification, and a map-check expectation for follow-up diagnostics or PIE smoke.
