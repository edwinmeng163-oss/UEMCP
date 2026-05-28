# UEAtelier v0.29 Code Tools 核心代码编辑工具工作规划

日期：2026-05-28

## 1. 背景判断

当前 UEAtelier 可以服务 C++ Unreal 项目，也可以构建项目、读取日志、编辑 Blueprint/Actor/Widget/Asset，并通过 Python user tool 做项目本地扩展。

但当前插件还没有一组面向 `.cpp`、`.h`、`.Build.cs`、`.uplugin` 等代码文件的核心读写工具。现有 C++ self-extension 路径只用于 UEAtelier 核心源码补丁，且是 hidden、manual、developer-only；它不应该被用来编辑宿主项目 C++ 类或用户隔离插件源码。

v0.29 首要目标应改为：先做核心 Code Tools，让 Hermes/Codex 能安全阅读、搜索、预览补丁、应用补丁、回滚代码文件；暂不做插件内 IDE，也不做 LSP/clangd/Copilot 级 UI。

## 2. 产品目标

做一组核心 MCP 工具，让 AI 可以在 Unreal Editor 内部完成代码文件工作闭环：

```text
inspect -> search -> read -> preview change -> apply change -> build -> fix compile errors -> rollback if needed
```

目标不是任意文件系统遥控器，而是可审计、可回滚、带路径边界的代码编辑能力。

## 3. 非目标

首版明确不做：

- 不做 IDE UI。
- 不做 LSP、clangd、符号索引、跳转定义。
- 不做任意 `write_file`。
- 不做删除文件、移动文件、批量重命名文件。
- 不编辑 Engine 安装目录。
- 不默认编辑 UEAtelier 核心插件源码。
- 不绕过现有 `mcp_apply_scaffold` 核心 self-extension 管线。

## 4. 首版工具边界

新增 7 个核心工具，统一使用 `unreal.code_*` 前缀：

```text
unreal.code_workspace_status
unreal.code_list_files
unreal.code_read_file
unreal.code_search
unreal.code_preview_change
unreal.code_apply_change
unreal.code_rollback_change
```

### 4.1 unreal.code_workspace_status

用途：返回当前代码工作区规则。

输出内容：

- projectDir
- allowedReadRoots
- defaultWritableRoots
- highRiskWritableRoots
- forbiddenRoots
- allowedExtensions
- latestCodeChangeManifest
- active extension lock status

风险：read_only。

### 4.2 unreal.code_list_files

用途：列出可读代码文件。

输入建议：

- `scope`: `project` / `source` / `plugins` / `user_tools` / `python_tools`
- `extensions`
- `glob`
- `maxResults`

默认排除：

```text
Binaries/**
Intermediate/**
DerivedDataCache/**
Saved/**
Content/**
Plugins/*/Binaries/**
Plugins/*/Intermediate/**
```

风险：read_only。

### 4.3 unreal.code_read_file

用途：读取单个代码文件。

输入建议：

- `path`
- `startLine`
- `lineCount`
- `maxChars`

输出必须包含：

- resolvedPath
- projectRelativePath
- lineCount
- sha256
- text

后续 apply 必须带 expected sha，防止旧上下文写入。

风险：read_only。

### 4.4 unreal.code_search

用途：搜索文件名或文件内容。

输入建议：

- `query`
- `mode`: `literal` / `regex` / `filename`
- `scope`
- `extensions`
- `contextLines`
- `maxMatches`

首版可用 C++ 文件扫描实现，不依赖外部 `rg`。后续如允许外部进程，可接 `rg`，但 v0.29 首版不需要。

风险：read_only。

### 4.5 unreal.code_preview_change

用途：预览代码变更，不写磁盘。

输入建议采用结构化 edits，不接受任意 shell patch：

```json
{
  "edits": [
    {
      "path": "Plugins/UEAtelierUserTools/MyTool/Source/MyTool/Private/MyTool.cpp",
      "expectedSha256": "...",
      "operation": "replace_exact",
      "oldText": "...",
      "newText": "..."
    }
  ]
}
```

首版支持操作：

- `replace_exact`
- `insert_before`
- `insert_after`
- `create_file`

输出：

- previewId
- unifiedDiff
- touchedFiles
- riskLevel
- requiresApproval
- wouldRequireBuild
- pathPolicyResult

风险：low，preview-only。

### 4.6 unreal.code_apply_change

用途：应用已 preview 的变更。

必须要求：

- `previewId`
- expected sha
- `dryRun` 默认 true
- 高风险路径必须显式确认
- 真实写入前创建 backup manifest
- 真实写入后验证磁盘 sha

输出：

- editId
- manifestPath
- backupDirectory
- touchedFiles
- before/after sha
- buildRecommended
- restartRecommended

风险：high。

### 4.7 unreal.code_rollback_change

用途：按 code edit manifest 回滚。

输入建议：

- `editId` 或 `manifestPath`
- `dryRun` 默认 true
- `force` 默认 false

要求：

- 回滚前检测 manifest drift。
- dry run 返回 rollback diff。
- 真实回滚也写新的 rollback manifest。

风险：high。

## 5. 文件类型范围

首版读写支持：

```text
.h
.hpp
.cpp
.inl
.ipp
.Build.cs
.Target.cs
.uplugin
.uproject
.py
.json
.ini
```

首版只读或暂缓写入：

```text
.md
.cs
.sh
.command
.ps1
.bat
.usf
.ush
.hlsl
.yml
.yaml
.toml
```

明确禁止：

```text
*.generated.h
*.gen.cpp
*.uasset
*.umap
```

## 6. 路径安全策略

默认可写：

```text
Plugins/UEAtelierUserTools/**
Tools/UnrealMcpPyTools/**
```

高风险确认后可写：

```text
Source/**
Plugins/*/Source/**
Plugins/*.uplugin
*.uproject
Config/*.ini
```

默认禁止写：

```text
Plugins/UnrealMcp/**
Binaries/**
Intermediate/**
DerivedDataCache/**
Saved/**
Content/**
```

Engine 安装目录永远禁止。

说明：`Saved/UnrealMcp/CodeChanges/**` 是 Code Tools 自己的 manifest/backup 输出区，可以写，但不作为用户代码目标。

## 7. Manifest 与备份设计

不能复用当前 self-extension 的：

```text
Saved/UnrealMcp/LastExtensionApply.json
Saved/UnrealMcp/ExtensionBackups/**
```

新增独立路径：

```text
Saved/UnrealMcp/CodeChanges/LastCodeChange.json
Saved/UnrealMcp/CodeChanges/Backups/<editId>/
Saved/UnrealMcp/CodeChanges/Previews/<previewId>.json
```

manifest 必须包含：

- schemaVersion
- action: `code_apply_change` / `code_rollback_change`
- editId
- previewId
- timestampUtc
- sessionId
- touchedFiles
- sourcePath
- backupPath
- hashBefore
- hashAfter
- operation
- dryRun
- pathRisk
- requiresBuild
- requiresRestart

## 8. 与现有核心工具的冲突检查结论

已确认：

- 当前 174 个工具中没有 `unreal.code_*`。
- 没有 `unreal.file_*` / `unreal.source_*` 前缀工具。
- 候选 7 个名字全部空闲。
- 现有相近工具语义不重叠。

相近但不冲突的现有工具：

- `unreal.mcp_patch_scaffold_patch`：只编辑 scaffold patch fragment。
- `unreal.mcp_apply_scaffold`：只应用 UEAtelier 核心 self-extension 补丁。
- `unreal.mcp_rollback_last_extension`：只回滚 extension manifest。
- `unreal.project_version_migration`：只编辑 `.uproject` 的 `EngineAssociation`。
- `unreal.execute_python_file`：执行 Python 文件，不是代码编辑器。

## 9. 必须同步修改的硬编码点

如果新增独立 `code` category，必须同 patch 修改：

```text
Tools/UnrealMcpToolRegistry/schema.json
Schemas/UnrealMcpToolRegistry.schema.json
Plugins/UnrealMcp/Resources/ToolRegistry/schema.json
Tools/validate_tool_registry.py
Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolRegistry.cpp
Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolHandlerRegistry.cpp
Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolDispatcher.cpp
Plugins/UnrealMcp/Source/UnrealMcp/Private/Providers/UnrealMcpApprovalPolicy.cpp
Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolExecutionGuard.cpp
```

新增实现文件建议：

```text
Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCodeTools.h
Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCodeTools.cpp
```

## 10. 审批与锁策略

审批策略：

- `code_workspace_status`：allow
- `code_list_files`：allow
- `code_read_file`：allow
- `code_search`：allow
- `code_preview_change` 且 preview-only：allow
- `code_apply_change` 且 dryRun=true：allow
- `code_apply_change` 且 dryRun=false：requires approval
- `code_rollback_change` 且 dryRun=true：allow
- `code_rollback_change` 且 dryRun=false：requires approval

锁策略：

真实 apply / rollback 必须使用现有 extension session lock，避免与以下工具并发：

```text
unreal.mcp_apply_scaffold
unreal.mcp_build_editor
unreal.mcp_rollback_last_extension
unreal.mcp_rollback_to_manifest
unreal.tools.import_package
unreal.skill_promote_draft
```

## 11. 实施阶段

### Phase 1：只读能力

目标：

- 加 `code` category。
- 实现 `workspace_status` / `list_files` / `read_file` / `search`。
- 更新 registry、schema、mirror、docs。
- 增加基础自动化测试。

验收：

```bash
python3 Tools/validate_tool_registry.py
```

并在 Editor 内确认 4 个只读工具可调用。

### Phase 2：preview 能力

目标：

- 实现 `code_preview_change`。
- 支持 `replace_exact`、`insert_before`、`insert_after`、`create_file`。
- 返回 unified diff 和 previewId。
- 不写目标文件。

验收：

- expected sha 不匹配时拒绝 preview。
- forbidden path 拒绝。
- high-risk path 标记 requiresApproval。

### Phase 3：apply 能力

目标：

- 实现 `code_apply_change`。
- dryRun 默认 true。
- dryRun=false 写 backup manifest。
- 写入后验证 after sha。
- 接现有 approval policy 和 extension lock。

验收：

- default writable root 可成功写入测试 fixture。
- high-risk root 需要显式确认。
- `Plugins/UnrealMcp/**` 被拒绝。
- `.generated.h` 被拒绝。

### Phase 4：rollback 能力

目标：

- 实现 `code_rollback_change`。
- 支持 dry run rollback diff。
- 支持 drift detection。
- 真实回滚写 rollback manifest。

验收：

- apply 后可回滚。
- drift 时默认拒绝，需要 force。
- 回滚后 hash 与 manifest 预期一致。

### Phase 5：接 build/error-fix 闭环

目标：

- 在 apply 输出里明确 `buildRecommended`。
- 文档指引接 `unreal.mcp_build_editor`。
- 编译失败后继续使用 `unreal.mcp_compile_error_fix_plan`。

不新增 build 工具。

## 12. 测试计划

基础测试：

- registry validation
- category validation
- read allowed file
- read denied generated file
- list excludes Binaries/Intermediate/Saved
- search literal
- search regex
- preview exact replace
- preview insert before/after
- preview create file
- apply dryRun no write
- apply real writes manifest
- rollback dryRun no write
- rollback real restores
- forbidden paths rejected
- stale expectedSha rejected

人工验证：

- 在 `Plugins/UEAtelierUserTools/TestCodeTool/**` 生成测试文件并编辑。
- 在 `Source/**` 上确认 high-risk approval 行为。
- 在 `Plugins/UnrealMcp/**` 上确认禁止写入。

## 13. 文档更新范围

有意义代码变更后必须更新：

```text
AGENTS.md
README.md
Plugins/UnrealMcp/README.md
Docs/SecurityModel.md
Docs/SelfExtensionPipeline.md
Docs/agents-guide/self-extension.md
```

新增建议：

```text
Docs/CodeTools.md
Docs/agents-guide/code-tools.md
```

## 14. 推荐最终决策

v0.29 首要目标定为：

```text
Core Code Tools: safe code file read/search/patch/apply/rollback for UE project and user extension plugin source.
```

落地原则：

- 新增 `code` category。
- 工具名使用 `unreal.code_*`。
- 默认服务隔离插件和 Python user tools。
- 高风险才允许编辑宿主 `Source/**`。
- 永远不默认编辑 UEAtelier 核心插件源码。
- 写入必须 preview -> apply -> manifest -> rollback。

这条路线与后续 `cpp_extension_tool` 是前后关系：先让 UEAtelier 能安全编辑代码文件，再用这组工具生成和维护 UE5.8 ToolsetRegistry 风格的隔离 C++ extension plugin。
