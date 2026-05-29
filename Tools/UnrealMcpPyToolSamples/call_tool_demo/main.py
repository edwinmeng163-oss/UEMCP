# Sample only: copy this directory to
# <ProjectDir>/Tools/UnrealMcpPyTools/call_tool_demo/ and run
# unreal.mcp_user_registry_reload before calling user.call_tool_demo.

def execute(args):
    status = call_tool("unreal.editor_status")
    actors = call_tool("unreal.list_level_actors")
    return {
        "isError": False,
        "text": "call_tool re-entry ok",
        "structuredContent": {
            "reentry": "ok",
            "editorStatusPolicy": status["meta"]["policyDecision"],
            "listActorsPolicy": actors["meta"]["policyDecision"],
        },
    }
