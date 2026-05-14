# Unreal MCP Tool Scaffold Starters

## English

`Tools/UnrealMcpToolScaffoldStarters/` contains versioned starter scaffolds that
ship with the repository. They are reviewed source templates, not the live
self-extension workspace.

The local workspace remains `Tools/UnrealMcpToolScaffolds/`. That directory is
project-local and ignored so each editor session can iterate on drafts, apply
with backup manifests, and roll back without committing unfinished scaffold
state.

Use any starter with:

```bash
cp -R Tools/UnrealMcpToolScaffoldStarters/<toolId> Tools/UnrealMcpToolScaffolds/
```

Then in Unreal MCP Chat run:

```text
unreal.mcp_apply_scaffold(toolId="<toolId>")
```

Available starters:

- `fps_bootstrap`: complete FPS character bring-up.
- `verify_input_drives_pawn`: headless PIE input verification.

## 中文

`Tools/UnrealMcpToolScaffoldStarters/` 保存随仓库发布的版本化 scaffold
starter。它们是经过审阅的源码模板，不是正在运行的本地自扩展工作区。

本地工作区仍然是 `Tools/UnrealMcpToolScaffolds/`。该目录是项目本地并被
git 忽略的草稿区，用于每个 Editor session 独立迭代、通过 backup manifest
apply，并在需要时 rollback。

使用任意 starter：

```bash
cp -R Tools/UnrealMcpToolScaffoldStarters/<toolId> Tools/UnrealMcpToolScaffolds/
```

然后在 Unreal MCP Chat 中运行：

```text
unreal.mcp_apply_scaffold(toolId="<toolId>")
```

当前 starter：

- `fps_bootstrap`：完整 FPS 角色初始化。
- `verify_input_drives_pawn`：headless PIE 输入驱动验证。

## 日本語

`Tools/UnrealMcpToolScaffoldStarters/` は、リポジトリで管理する
versioned scaffold starter を置く場所です。これはレビュー済みのソース
テンプレートであり、実行中の self-extension workspace ではありません。

ローカル作業領域は引き続き `Tools/UnrealMcpToolScaffolds/` です。この
ディレクトリは project-local かつ git ignored で、各 Editor session が
draft を個別に反復し、backup manifest 付きで apply し、必要に応じて
rollback できるようにします。

starter の使い方:

```bash
cp -R Tools/UnrealMcpToolScaffoldStarters/<toolId> Tools/UnrealMcpToolScaffolds/
```

その後 Unreal MCP Chat で実行します:

```text
unreal.mcp_apply_scaffold(toolId="<toolId>")
```

利用可能な starter:

- `fps_bootstrap`: FPS キャラクターの完全な bring-up。
- `verify_input_drives_pawn`: headless PIE 入力検証。
