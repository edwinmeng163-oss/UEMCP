# Unreal MCP Project

This repository is an Unreal Engine 5.7 project focused on editor automation, AI-assisted project inspection, Blueprint scaffolding, UMG setup, and local Model Context Protocol workflows.

It includes an in-project editor plugin, **Unreal MCP**, which exposes Unreal Editor operations through a localhost MCP endpoint and an in-editor chat panel.

## 中文概览

本项目当前定位为 **Unreal MCP 编辑器自动化与 AI 协作开发实验项目**。

当前重点：

- 在 Unreal Editor 内运行本地 MCP server。
- 通过 Chat 面板执行项目检查、地图/资产查询、PIE 控制、日志读取、Map Check 等操作。
- 通过 MCP 工具辅助创建 Blueprint、编辑 Blueprint 图、搭建 UMG Widget、生成玩法系统脚手架。
- 保留 UE 模板内容作为测试与原型资源。
- 使用 Git LFS 管理 Unreal 二进制资产，方便上传和协作。

## Current Status

The project currently contains:

- Unreal Engine 5.7 C++ project foundation.
- TopDown, Strategy, and TwinStick template content for local testing.
- `Plugins/UnrealMcp`, an editor plugin for local MCP and in-editor AI/chat workflows.
- Git LFS setup for Unreal binary assets.
- Project-level README and ignore rules suitable for public GitHub hosting.

## Unreal MCP Plugin

Plugin path:

```text
Plugins/UnrealMcp
```

Default MCP endpoint:

```text
http://127.0.0.1:8765/mcp
```

Editor chat panel:

```text
Window > Unreal MCP Chat
```

Full plugin documentation:

```text
Plugins/UnrealMcp/README.md
```

## Tool Coverage

Unreal MCP currently supports:

- Editor and project inspection.
- Log tailing and map checks.
- Map and asset listing.
- PIE start/stop.
- Actor selection, transforms, spawning, layout, and batch property edits.
- Python execution inside Unreal Editor.
- Blueprint class creation and compilation.
- Blueprint graph editing:
  - `unreal.bp_add_variable`
  - `unreal.bp_add_function`
  - `unreal.bp_add_event_node`
  - `unreal.bp_add_call_function_node`
  - `unreal.bp_add_branch_node`
  - `unreal.bp_add_for_each_node`
  - `unreal.bp_connect_pins`
  - `unreal.bp_set_pin_default`
  - `unreal.bp_arrange_graph`
  - `unreal.bp_compile_save`
- UMG Widget Blueprint editing:
  - `unreal.widget_add`
  - `unreal.widget_remove`
  - `unreal.widget_set_property`
  - `unreal.widget_set_slot_layout`
  - `unreal.widget_bind_event`
  - `unreal.widget_bind_blueprint_variable`
  - `unreal.widget_build_template`
- Gameplay scaffold helpers:
  - `unreal.scaffold_round_system`
  - `unreal.scaffold_shop_system`
  - `unreal.scaffold_economy_system`
  - `unreal.scaffold_autobattler_ai`
  - `unreal.scaffold_result_ui`
- Saving dirty packages.

## In-Editor Chat Usage

Open the chat panel from:

```text
Window > Unreal MCP Chat
```

Examples:

```text
/status
/maps
/assets /Game/TopDown
/map_check
/log 80
```

Direct MCP tool call example:

```text
/tool unreal.editor_status {}
```

AI-assisted request example:

```text
inspect the current project and summarize the maps, selected actors, and available Blueprint assets
```

## Opening The Project

Requirements:

- Unreal Engine 5.7.
- Git LFS.
- macOS is the most tested development path for this repository.

Clone and pull LFS assets:

```bash
git clone https://github.com/edwinmeng163-oss/UEMCP.git
cd UEMCP
git lfs install
git lfs pull
```

Open:

```text
MyProject.uproject
```

## Deployment Guide / 部署指南

This project is an Unreal Editor plugin workflow rather than a packaged game/server deployment. A normal deployment means cloning the repository, pulling LFS assets, opening the project in Unreal Engine, compiling the editor modules, and verifying that the local MCP endpoint is available.

### 1. Prepare The Machine

Install:

- Unreal Engine 5.7.
- Xcode command line tools on macOS.
- Git.
- Git LFS.

Verify Git LFS:

```bash
git lfs install
git lfs version
```

### 2. Clone And Pull Assets

```bash
git clone https://github.com/edwinmeng163-oss/UEMCP.git
cd UEMCP
git lfs install
git lfs pull
```

If binary assets look very small or fail to load in Unreal, run `git lfs pull` again.

### 3. Open Or Build The Project

Open `MyProject.uproject` directly in Unreal Engine 5.7.

For a command-line editor build on macOS:

```bash
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
  MyProjectEditor Mac Development \
  -Project="$(pwd)/MyProject.uproject" \
  -WaitMutex
```

If the editor asks to rebuild modules on first open, allow it to rebuild.

### 4. Confirm Plugin Settings

In Unreal Editor, open:

```text
Edit > Project Settings > Plugins > Unreal MCP
```

Recommended defaults:

- MCP server enabled.
- Host: `127.0.0.1`
- Port: `8765`
- Endpoint path: `/mcp`
- Auth token: empty for local-only testing, or configured if another app will connect.

The plugin is editor-focused. Keep the endpoint bound to localhost unless you intentionally know how you want to secure remote access.

### 5. Verify The MCP Endpoint

With the Unreal Editor running, test from a terminal:

```bash
curl -s \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"curl","version":"1.0"}}}' \
  http://127.0.0.1:8765/mcp
```

List available tools:

```bash
curl -s \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' \
  http://127.0.0.1:8765/mcp
```

Call a basic status tool:

```bash
curl -s \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"unreal.editor_status","arguments":{}}}' \
  http://127.0.0.1:8765/mcp
```

### 6. Use The In-Editor Chat

Open:

```text
Window > Unreal MCP Chat
```

Try direct commands:

```text
/status
/maps
/assets /Game/TopDown
/map_check
```

Try a direct tool call:

```text
/tool unreal.editor_status {}
```

### 7. Optional AI Assistant Setup

To use natural-language AI requests from the chat panel, configure:

```text
Project Settings > Plugins > Unreal MCP > AI
```

Set:

- Enable AI Assistant.
- OpenAI Responses URL.
- OpenAI API Key.
- OpenAI Model.
- AI Max Tool Rounds.
- AI Request Timeout Seconds.

Keep API keys local. Do not commit editor user settings or key files.

### 8. Troubleshooting

- If `/status` works but AI requests fail, check the AI settings and API key.
- If `curl` cannot connect, confirm the Unreal Editor is open and the plugin is enabled.
- If tools time out during heavy editor work, increase the AI request/activity timeout settings.
- If assets are missing, run `git lfs pull`.
- If the plugin does not compile, rebuild `MyProjectEditor` from the command line and inspect the UnrealBuildTool log.

## Git / Repository Notes

This repository uses Git LFS for Unreal binary assets:

- `.uasset`
- `.umap`
- `.ubulk`
- `.uexp`

Generated Unreal folders are intentionally ignored:

- `Binaries/`
- `Intermediate/`
- `Saved/`
- `DerivedDataCache/`
- plugin build caches

Do not commit local API keys, editor user settings, logs, or generated local test assets.

## AI / API Key Safety

Unreal MCP can connect to the OpenAI Responses API from inside the editor. Configure API keys locally in:

```text
Project Settings > Plugins > Unreal MCP > AI
```

Do not commit API keys to Git. User-specific editor settings are ignored by `.gitignore`.

## License Notice

No standalone open-source license file has been added yet.

Important:

- Unreal Engine, Epic template assets, Starter Content, Mannequin assets, and other Epic-provided content remain governed by the Epic Unreal Engine EULA.
- Project-specific source code and original plugin code should receive a clear license before others rely on this repository for reuse.
- If this repository is intended to stay public, a common next step is to add an MIT license for original project/plugin code plus a notice excluding Epic/third-party content from that license.

## Repository

GitHub:

```text
https://github.com/edwinmeng163-oss/UEMCP
```
