# UEAtelier Release Notes & Setup Guide — 2026-05

> Trilingual: [中文](#中文) · [English](#english) · [日本語](#日本語)
> Target audience: new contributors cloning the repo, including Windows users.

---

## 中文

### 更新摘要

本次主线合并把两条 feature 分支整合进 `main`：

1. **多 Provider AI 体系（`feat/ai-multi-provider`）**
2. **RAG 闭环与 Activity 基础（`dev-b/rag-closed-loop`）**
3. **跨平台 Codex Desktop Bridge（P7.D）**

#### 1. 多 Provider AI 体系

`UUnrealMcpSettings::Providers[]` 取代旧的单 OpenAI 字段。已有 OpenAI 用户启动一次会**自动迁移**（旧字段保留并标 deprecated；新表内多一条 `openai-default` 条目）。新增 5 种 Provider Kind：

| Kind | 用途 | 鉴权 |
|---|---|---|
| `OpenAiResponses` | OpenAI Responses API（默认） | `Authorization: Bearer` |
| `OpenAiChatCompat` | OpenAI 兼容 chat/completions —— **覆盖 Kimi / Moonshot、GLM / 智谱、DeepSeek、Qwen、Ollama 等** | 同上 |
| `AnthropicMessages` | Anthropic Claude Messages API（含 tool_use / tool_result + thinking 字段） | `x-api-key` + `anthropic-version` |
| `Codex` | 本地 Codex CLI 子进程（macOS/Linux）—— 锁 `gpt-5.5 xhigh workspace-write` | 无（local） |
| `CodexAppServer` | 通过 Codex App Server bridge 走 Codex Desktop（**跨平台**） | 无（local） |

聊天面板顶部新增 **Provider/Model 选择器**；`Codex` CLI provider 显示 `gpt-5.5 (locked)` 不可改，`CodexAppServer` / Codex Desktop bridge provider 可编辑模型。

#### 2. RAG 闭环

- 新增 Activity Log 基础：`UnrealMcpActivityLog.{h,cpp}` + `Schemas/UnrealMcpActivityLogEntry.schema.json` + 滚动窗口持久化
- 工具调用与聊天面板事件 always-on 落 log
- `Tools/UnrealMcpKnowledge/build_index.py` 索引活动日志与 skill 文档，支持 kind-aware 检索 / 按 kind 过滤 / 分组
- `preview_change_plan` 与 `verify_task_outcome` 现在会注入并反写 `evidence[]` 字段（manifest schema 已扩展）

#### 3. Codex Desktop Bridge

`Tools/UnrealMcpCodexBridge/` —— Bun TypeScript daemon，把 Codex App Server 协议（POSIX 上 WebSocket-over-UDS、Windows 上 stdio 子进程 pipe —— 当前 Codex builds 拒绝 `--listen ws://...`，见 issue #2 comment 8）桥到一个简单的 `ws://127.0.0.1:8766/uevolve` 接口给 UE plugin 用。UE 侧入站监听始终是 WebSocket，与出站连 Codex 的传输无关。

**关键能力**：
- Codex 通过此桥能**直接调用 Unreal MCP 工具**（`unreal.spawn_actor` / `unreal.execute_python` / 全部可见工具），不再只是给文字方案
- 安全姿态：Codex 自身的 file edit / shell exec 全拒；只放行带 `codex_approval_kind: "mcp_tool_call"` 且 `serverName === "unrealmcp"` 的 MCP 工具调用 —— 由 UE MCP 自己的 audit/dry-run/safety 兜底
- 跨平台：macOS/Linux 用 Unix Domain Socket，Windows 自动切 `ws://127.0.0.1:<auto-port>`
- 模型与 reasoning effort 可由用户选择：Model 在 ChatPanel 顶部 combo 编辑，ReasoningEffort 在 `Project Settings > Plugins > Unreal MCP > AI > Providers` 设置；`Codex` CLI provider 仍保持 `gpt-5.5` + `xhigh` 硬锁

#### 4. UI 体验打磨

- 顶部进度 `SThrobber` + 计时
- 错误条目红色高亮 + ⚠ 前缀
- 工具调用卡片默认折叠，点击展开 args + result
- 每条消息📋复制按钮
- 智能自动滚动 —— 仅当用户在底部时才下拉，读历史不被打断

#### 5. Schema 与工具注册表

- `Schemas/UnrealMcpExtensionManifest.schema.json` 扩展 evidence / outcome 字段
- `Schemas/UnrealMcpActivityLogEntry.schema.json` 新增
- `Tools/UnrealMcpToolRegistry/tools.json` 新工具（chat_label、knowledge 检索增强等）

#### 6. 已知限制

- macOS 上"attach 现有 Codex Desktop 进程的 IPC socket"还没做（v2 增强）；当前 bridge 都是 spawn 一个独立 codex app-server 子进程
- `Codex` CLI provider（直 subprocess）目前只支持 macOS/Linux；Windows 用户请用 `CodexAppServer` provider
- 桥的 approval policy v1 不支持 Codex 在桥内执行 OS 级命令——这是有意为之

### Engine Compatibility / 引擎兼容性

UEAtelier plugin 当前支持 Unreal Engine 5.6 和 5.7。同一套 C++ 源码以 UE 5.6 作为最低支持版本；`UEvolve.uproject` 默认 `EngineAssociation` 设为 `5.6`，5.7 用户可以通过右键项目文件选择 **Switch Unreal Engine Version** 升级本地工程绑定。本次代码审计没有发现需要 5.7 专用 API shim 的 plugin 调用；请在 UE 5.6 环境中执行最终编译验证。

- **Pre-commit / linter**：提交前运行 `python3 Tools/check_ue56_compat.py`，检查已知 UE 5.7-only API，避免代码在 UE 5.7 机器上通过却在 UE 5.6 用户环境中失败。

---

### 安装指南（新用户从 Git 拉取到本地）

#### 共通先决条件

| 工具 | 用途 | 版本 |
|---|---|---|
| Unreal Engine | 编辑器宿主 | **5.7** |
| Git | 拉代码 | 任意现代版本 |
| Bun | 跑 Codex Bridge daemon（仅当用 CodexAppServer provider 时需要） | ≥ 1.1 |
| Codex CLI | Codex 系 provider 必备 | ≥ 0.130 |
| Codex Desktop | 可选，让 Codex 任务在 GUI 中可见 | 最新 |

#### macOS

```bash
# 1. 装依赖
brew install --cask unreal-engine                              # 或从 Epic Launcher 装 UE 5.7
curl -fsSL https://bun.sh/install | bash                       # Bun
npm install -g @openai/codex-cli                               # Codex CLI（或装 Codex Desktop App）

# 2. 拉代码
git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEAtelier

# 3. 生成 Xcode 工程并编译
/Users/Shared/Epic\ Games/UE_5.7/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh -project="$(pwd)/UEvolve.uproject" -game
/Users/Shared/Epic\ Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh UEvolveEditor Mac Development \
  -Project="$(pwd)/UEvolve.uproject" -waitmutex

# 4. 启动编辑器
"/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "$(pwd)/UEvolve.uproject"
```

#### Linux

```bash
# 1. UE 5.7 from Epic（按官方说明），Bun 与 Codex CLI 同 macOS
curl -fsSL https://bun.sh/install | bash
npm install -g @openai/codex-cli

# 2. 拉代码 + 编译
git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEAtelier
/path/to/UE_5.7/Engine/Build/BatchFiles/Linux/Build.sh UEvolveEditor Linux Development \
  -Project="$(pwd)/UEvolve.uproject" -waitmutex
```

#### Windows

```powershell
# 1. 装依赖
# - 通过 Epic Games Launcher 装 UE 5.7
# - https://bun.sh 下载 Windows 安装器
#   装完 Bun 后，打开新的终端运行 bun --version 确认可用
#   若提示 bun: command not found，把 %USERPROFILE%\.bun\bin 加到用户 PATH
#   （System Properties → Environment Variables → User PATH），或用 Scoop 安装：
#   scoop install bun（会自动设置 PATH）
#   改 PATH 后先关闭并重开终端 / VS Code / Explorer，再重试 start-bridge.cmd
# - npm install -g @openai/codex-cli   (或装 Codex Desktop 桌面应用)

# 2. 拉代码
git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEAtelier

# 3. 编译
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" UEvolveEditor Win64 Development `
  -Project="$PWD\UEvolve.uproject" -waitmutex

# 4. 启动编辑器
"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" `
  "$PWD\UEvolve.uproject"
```

#### 配置 AI Provider

1. 打开编辑器，菜单 `Edit > Project Settings > Plugins > Unreal MCP > AI`
2. 勾选 `bEnableAiAssistant`
3. 在 `Providers` 数组里添加条目（至少一条），按你选的服务填：

```toml
# 例：Kimi（OpenAI Chat 兼容）
Id              = kimi-default
DisplayName     = Kimi
Kind            = OpenAiChatCompat
BaseUrl         = https://api.moonshot.cn/v1/chat/completions
ApiKey          = <你的 moonshot api key>
Model           = moonshot-v1-8k

# 例：GLM
Id, DisplayName = glm, GLM 4
Kind            = OpenAiChatCompat
BaseUrl         = https://open.bigmodel.cn/api/paas/v4/chat/completions
Model           = glm-4

# 例：DeepSeek
BaseUrl         = https://api.deepseek.com/v1/chat/completions
Model           = deepseek-chat

# 例：Anthropic Claude
Kind            = AnthropicMessages
BaseUrl         = https://api.anthropic.com/v1/messages
Model           = claude-sonnet-4-6
```

#### Provider 预设

Phase 1 增加 provider preset 的数据和 Project Settings UI 支持。`PresetId` 记录选中的 preset；空 `PresetId` 或 `custom-openai-chat` 仍可用于手动 OpenAI-compatible provider。各 vendor 的 dispatcher 细节留到 Phase 2。

##### Phase 2: Kimi reasoning_content

Phase 2 先加入 Kimi / Moonshot chat-compatible `reasoning_content` handler. 它会累积 streamed `reasoning_content`, 并在 assistant tool-call continuation message 中回填同样文本; 空 `PresetId`, `custom-openai-chat` 和其他 OpenAI-compatible provider 保持不变. DeepSeek / GLM / Qwen / Ollama quirks 仍然 deferred; DeepSeek 不按同形处理, 因为 DeepSeek-reasoner 的 input contract 相反: input message 中包含 `reasoning_content` 会返回 400, 且不支持 function calling.

##### v0.24.1 hotfix: 嵌套 Provider preset UI 变更

v0.24.1 修复 Project Settings 中 Provider `PresetId` / `Kind` 自动填充。嵌套的
`Providers[]` 编辑会走 `PostEditChangeChainProperty`; hotfix 现在直接处理该
chain event, 确保 preset 选择拿到正确数组索引、填充对应 provider 行并写入
provider backup。新增 synthetic chain-event automation test 覆盖该回归。

##### v0.24.2 hotfix: Provider preset international endpoints

v0.24.2 隐藏四个 deprecated legacy `OpenAI*` Project Settings 字段, 但仍保留
`Config` 反序列化和 legacy INI migration. Kimi / GLM / Qwen preset 现在默认
使用 international endpoints: Kimi `https://api.moonshot.ai/v1/chat/completions`,
GLM/Zhipu `https://api.z.ai/api/paas/v4/chat/completions`, Qwen
`https://dashscope-intl.aliyuncs.com/compatible-mode/v1/chat/completions`.
`Z.ai` 是 Zhipu/GLM 的 international endpoint / rebrand, 所以 GLM 不再默认指向
`open.bigmodel.cn`. 升级时已有 provider 的 saved `BaseUrl` 会保留; 若要把
v0.24.0/v0.24.1 的 provider 切到 international default, 请清空并重新选择 preset,
或手动编辑 `BaseUrl`.

##### v0.24.3 hotfix: Codex CLI subprocess PATH

v0.24.3 修复 Unreal Editor 没有继承用户 interactive shell `PATH` 时, Codex CLI
provider 子进程出现 `bun: not found` 或类似工具解析失败的问题。Codex CLI 子进程现在会在
继承的 `PATH` 前追加这些常见 Mac developer-tool 目录:

- `$HOME/.bun/bin`
- `$HOME/.local/bin`
- `$HOME/.cargo/bin`
- `/opt/homebrew/bin`
- `/usr/local/bin`

chat exec 路径和 cleanup kill exec 路径都覆盖此 PATH 前缀。shell 会静默忽略不存在的
PATH 条目。Codex CLI provider 仍只支持 macOS/Linux; Windows 用户应继续使用 Codex
Desktop / CodexAppServer provider。此 hotfix 不涉及 INI/settings migration; 已有用户的
Codex Binary Path 设置会保留。

##### v0.24.4 hotfix: Codex CLI bash quoting

v0.24.4 修复 CodexProvider 子进程调用中的两个 bash quoting bug，这两个问题会阻止
Codex CLI 正确收到用户 prompt：

- **v0.24.3 regression**：v0.24.3 新增的 developer-tool PATH prefix 用 literal
  `"..."` 包住 `$PATH` assignment。UE Unix `FPlatformProcess::CreateProc` tokenizer
  (`UnixPlatformProcess.cpp` 中的 `ParseCmdLineToken`) 会把 `"` 当作 quote delimiter；
  bash `-c` payload 里的 literal `"` 会把命令解析成 `bash: -c: option requires an
  argument`。现在去掉了多余的 `"..."` 包装；POSIX bash 的 assignment RHS 本来就不会
  word-split。

- **Pre-v0.24.3 latent bug**：`CodexProvider.cpp:577` 的 prompt-pass 路径使用
  `"$(cat${IFS}<promptfile>)"` 通过 bash command substitution 注入用户 prompt。
  同一个 UE tokenizer quirk 会吃掉外层 literal `"`。该问题在 v0.24.3 之前被
  `bun: not found` 先行错误遮住；PATH prefix 生效后才暴露。现在改为把 composed
  prompt text 直接作为 `$'...'` wrapped argv word 传入，并通过现有
  `QuoteForBashWordNoSpaces` helper 保留 multi-turn chat 的 `ConversationContext`。

同时，`QuoteForBashWordNoSpaces` 会防御性地把 `"` 编码为 `\x22`，避免未来用户输入再次
引入同类问题；新增 `Docs/AIProviderArchitecture.md` 记录 shell-escaping invariants；
并新增两个 integration tests，通过 `/bin/bash -c` 运行实际 production command-builder
helpers，对 argc/argv-printing helper script 断言 argv boundary，覆盖 pure-string tests
抓不到的回归。

**已知限制（v0.24.4 不修复；v0.25 Reform B 追踪）**：即使这些修复已落地，通过 UE
Assistant panel 使用 Codex CLI chat 仍不会返回 codex 的回复。根因是 `codex-agent start -w`
等待 job `status != running`，但 codex CLI 是 interactive（首轮后保持 idle 等待下一次用户输入，
不会退出），而 codex 的实际回复在 tmux pane 中，parent process 不读取。v0.25 发布前请使用
Codex Desktop bridge（CodexAppServer provider）。详见 `Docs/AIProviderArchitecture.md`
Section G (G1)。

| Preset | Default Model |
|---|---|
| Custom OpenAI-compatible | empty/manual |
| OpenAI Responses | `gpt-5.1` |
| Anthropic Claude | `claude-sonnet-4-6` |
| Kimi (Moonshot) | `moonshot-v1-8k` |
| GLM (Zhipu) | `glm-4` |
| DeepSeek | `deepseek-chat` |
| Qwen (Tongyi) | `qwen-plus` |
| Ollama (local) | `llama3.1` |
| Codex CLI | Model empty; `CodexExtraArgs` defaults to `-m gpt-5.5 -r xhigh` |
| Codex Desktop / App Server | Model empty; bridge/default decides |

4. 设 `ActiveProviderId` 等于你想用的那条的 `Id`，保存（Ctrl+S）
5. 打开 `Window > UEAtelier Chat`，顶部 Provider 选择器应能切换、模型字段显示当前选择

#### Codex Desktop Bridge（可选）

让 Codex 真正驱动 Unreal 工具：

**1. 起 bridge daemon**（同一台机器上）

```bash
# macOS / Linux
Tools/UnrealMcpCodexBridge/start-bridge.sh
```

```cmd
:: Windows cmd.exe
Tools\UnrealMcpCodexBridge\start-bridge.cmd
```

```powershell
# Windows PowerShell
powershell -ExecutionPolicy Bypass -File Tools\UnrealMcpCodexBridge\start-bridge.ps1
```

启动后看到：

```text
Codex binary: /opt/homebrew/bin/codex
UEAtelier Codex Bridge listening at ws://127.0.0.1:8766/uevolve
Codex app-server transport=unix endpoint=/var/.../codex.sock
# Windows 上会显示 transport=ws endpoint=ws://127.0.0.1:<port>
```

**2. UE 里加 CodexAppServer provider**

```toml
Id              = codex-desktop
DisplayName     = Codex Desktop
Kind            = CodexAppServer
BaseUrl         = ws://127.0.0.1:8766/uevolve
```

**3. ChatPanel 顶部 Provider 切到 `Codex Desktop`，发 `/ask` 即可。** Model 可在聊天面板顶部 combo 编辑；ReasoningEffort 在 `Project Settings > Plugins > Unreal MCP > AI > Providers` 对应 provider 行里设置。

Bridge 已自动把编辑器的 MCP server（`http://127.0.0.1:8765/mcp`）注册给 Codex，所以 Codex 能直接调 Unreal 工具。

**环境变量（cross-platform）**

| 变量 | 默认值 | 作用 |
|---|---|---|
| `UEVOLVE_CODEX_BRIDGE_PORT` | 8766 | UE 朝桥的 WebSocket 端口 |
| `UEVOLVE_CODEX_MODEL` | `gpt-5.5` | bridge daemon 默认模型；UE provider / 每轮请求可覆盖 |
| `UEVOLVE_CODEX_EFFORT` | `xhigh` | bridge daemon 默认 reasoning effort（`low` / `medium` / `high` / `xhigh`） |
| `UEVOLVE_CODEX_TRANSPORT` | `unix`(POSIX) / `ws`(Windows) | 桥跟 codex 之间用 Unix socket 还是 WebSocket |
| `UEVOLVE_CODEX_APP_SERVER_PORT` | auto | 强制 codex app-server 绑定的端口（仅 ws 模式生效） |
| `UEVOLVE_CODEX_BIN` | auto (`where`/`which`) | Codex 可执行文件绝对路径；Windows 非 PATH 安装时设置 |
| `UEVOLVE_MCP_URL` | `http://127.0.0.1:8765/mcp` | UE MCP server 地址（编辑器没默认端口请改） |
| `UEVOLVE_CODEX_APPROVAL_POLICY` | `reject` | OS 级能力策略；`auto-approve` 仅本地开发用 |

#### 验证一切就绪

ChatPanel 里依次发：

1. `/ask 用 unreal.editor_status 报当前关卡` → 应看到工具调用卡片 + currentMap
2. `/ask 在原点生成一个 PointLight 起名 TestLight` → 关卡里真出现一盏灯
3. （Codex Desktop 路径）`/ask 用 execute_python 在原点画一个 3×3 立方体迷宫` → Codex 调用 MCP，关卡里出现迷宫

---

## English

### What's New

This release merges two feature branches into `main`:

1. **Multi-provider AI subsystem** (`feat/ai-multi-provider`)
2. **RAG closed loop + activity log foundation** (`dev-b/rag-closed-loop`)
3. **Cross-platform Codex Desktop Bridge** (P7.D)

#### 1. Multi-provider AI

`UUnrealMcpSettings::Providers[]` replaces the legacy single-OpenAI flat fields. Existing OpenAI users are **auto-migrated** on first launch (legacy fields kept and deprecated; the new array gets an `openai-default` entry). Five provider kinds:

| Kind | Purpose | Auth |
|---|---|---|
| `OpenAiResponses` | OpenAI Responses API (default) | `Authorization: Bearer` |
| `OpenAiChatCompat` | OpenAI-compatible chat/completions — **covers Kimi (Moonshot), GLM (Zhipu), DeepSeek, Qwen, Ollama** | same |
| `AnthropicMessages` | Anthropic Claude Messages API (tool_use / tool_result + optional thinking) | `x-api-key` + `anthropic-version` |
| `Codex` | Local Codex CLI subprocess (macOS/Linux only) — model locked to `gpt-5.5 xhigh workspace-write` | none |
| `CodexAppServer` | Via the Codex App Server bridge — **cross-platform** | none |

A Provider/Model selector at the top of the chat panel switches between configured entries; the `Codex` CLI provider displays `gpt-5.5 (locked)`, while the `CodexAppServer` / Codex Desktop bridge provider keeps the model editable.

#### 2. RAG Closed Loop

- New activity log foundation: `UnrealMcpActivityLog.{h,cpp}`, schema, rolling-window persistence.
- Always-on emitters from tool execution and chat panel.
- `Tools/UnrealMcpKnowledge/build_index.py` indexes activity log and skill docs with kind-aware search, grouping, and source-kind filters.
- `preview_change_plan` and `verify_task_outcome` inject and persist `evidence[]` against the extension manifest schema.

#### 3. Codex Desktop Bridge

`Tools/UnrealMcpCodexBridge/` is a Bun TypeScript daemon that bridges the Codex App Server protocol (WebSocket-over-UDS on POSIX, stdio child-process pipe on Windows — current Codex builds reject `--listen ws://...`; see issue #2 comment 8) to a simple `ws://127.0.0.1:8766/uevolve` endpoint consumed by the UE plugin. The UE-facing inbound listener is always WebSocket regardless of which outbound transport speaks to Codex.

**Capabilities**:
- Codex can **invoke Unreal MCP tools directly** (`unreal.spawn_actor`, `unreal.execute_python`, the visible tool set) instead of only emitting `/tool ...` text suggestions.
- Safety posture: Codex's built-in file edit and shell exec are universally rejected; only MCP tool-call approvals whose `serverName === "unrealmcp"` AND `_meta.codex_approval_kind === "mcp_tool_call"` are auto-accepted. UE MCP's own audit / dry-run / safety layer is trusted to gate destructive work.
- Cross-platform: macOS/Linux uses a Unix Domain Socket transport; Windows defaults to `ws://127.0.0.1:<auto-port>`.
- Model and reasoning effort are user-selectable: edit Model in the chat panel combo and ReasoningEffort in `Project Settings > Plugins > Unreal MCP > AI > Providers`. The separate `Codex` CLI provider remains locked to `gpt-5.5` + `xhigh`.

#### 4. UI Polish

- `SThrobber` + elapsed-time label while a request is in flight
- Error entries get a subtle red background and a ⚠ glyph
- Tool call cards default to collapsed `SExpandableArea`, expand to show args + result
- Per-entry 📋 copy button
- Smart auto-scroll — only follows the bottom when the user is already there

#### 5. Schemas & Tool Registry

- `Schemas/UnrealMcpExtensionManifest.schema.json` extended with evidence/outcome fields
- `Schemas/UnrealMcpActivityLogEntry.schema.json` added
- `Tools/UnrealMcpToolRegistry/tools.json` gains the new tools (chat_label, knowledge search enhancements)

#### 6. Known Limitations

- Attaching the bridge to an already-running Codex Desktop's IPC socket (`ipc-501.sock` on macOS) is a future enhancement; the current bridge always spawns its own codex app-server subprocess.
- The `Codex` CLI provider (subprocess) is macOS/Linux only. Windows users should use the `CodexAppServer` provider via the bridge.
- The bridge's V1 approval policy intentionally denies Codex's built-in OS-level capabilities. This is by design.

### Engine Compatibility

The UEAtelier plugin supports Unreal Engine 5.6 and 5.7 from the same source tree. UE 5.6 is the lower bound, so `UEvolve.uproject` defaults `EngineAssociation` to `5.6`; UE 5.7 users can switch the local project association through **Switch Unreal Engine Version**. This code-only audit found no plugin API calls that require UE 5.7-specific conditional shims; final compile validation still needs to run in a UE 5.6 installation.

- **Pre-commit / linter**: Before committing, run `python3 Tools/check_ue56_compat.py` to catch known UE 5.7-only APIs so code that passes on a UE 5.7 machine does not regress UE 5.6 users.

---

### Setup Guide (new contributor cloning the repo)

#### Common Prerequisites

| Tool | Why | Version |
|---|---|---|
| Unreal Engine | Editor host | **5.7** |
| Git | Source control | any modern |
| Bun | Codex bridge daemon (only needed for `CodexAppServer` provider) | ≥ 1.1 |
| Codex CLI | Required for any Codex-kind provider | ≥ 0.130 |
| Codex Desktop | Optional GUI companion | latest |

#### macOS

```bash
brew install --cask unreal-engine                              # or via Epic Launcher
curl -fsSL https://bun.sh/install | bash
npm install -g @openai/codex-cli

git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEAtelier

/Users/Shared/Epic\ Games/UE_5.7/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh \
  -project="$(pwd)/UEvolve.uproject" -game
/Users/Shared/Epic\ Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh \
  UEvolveEditor Mac Development -Project="$(pwd)/UEvolve.uproject" -waitmutex

"/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "$(pwd)/UEvolve.uproject"
```

#### Linux

```bash
curl -fsSL https://bun.sh/install | bash
npm install -g @openai/codex-cli

git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEAtelier
/path/to/UE_5.7/Engine/Build/BatchFiles/Linux/Build.sh \
  UEvolveEditor Linux Development -Project="$(pwd)/UEvolve.uproject" -waitmutex
```

#### Windows

```powershell
# Install UE 5.7 via Epic Games Launcher.
# Install Bun from https://bun.sh (Windows installer).
# After installing Bun, open a new terminal and verify bun --version works.
# If you see bun: command not found, add %USERPROFILE%\.bun\bin to User PATH
# (System Properties → Environment Variables → User PATH), or install via Scoop:
# scoop install bun, which sets PATH automatically.
# After changing PATH, close and reopen any terminal / VS Code / Explorer window before retrying start-bridge.cmd.
# npm install -g @openai/codex-cli   (or install Codex Desktop instead)

git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEAtelier

& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
  UEvolveEditor Win64 Development -Project="$PWD\UEvolve.uproject" -waitmutex

& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" `
  "$PWD\UEvolve.uproject"
```

#### Configure an AI Provider

1. `Edit > Project Settings > Plugins > Unreal MCP > AI`
2. Toggle `bEnableAiAssistant`
3. Add at least one row to `Providers`. Examples:

```toml
# Kimi (Moonshot)
Id          = kimi-default
DisplayName = Kimi
Kind        = OpenAiChatCompat
BaseUrl     = https://api.moonshot.cn/v1/chat/completions
ApiKey      = <your moonshot key>
Model       = moonshot-v1-8k

# GLM (Zhipu)
Kind        = OpenAiChatCompat
BaseUrl     = https://open.bigmodel.cn/api/paas/v4/chat/completions
Model       = glm-4

# DeepSeek
BaseUrl     = https://api.deepseek.com/v1/chat/completions
Model       = deepseek-chat

# Anthropic Claude
Kind        = AnthropicMessages
BaseUrl     = https://api.anthropic.com/v1/messages
Model       = claude-sonnet-4-6
```

#### Provider presets

Phase 1 adds data and Project Settings UI support for provider presets. `PresetId` records the selected preset; empty `PresetId` or `custom-openai-chat` remains valid for manual OpenAI-compatible providers. Vendor-specific dispatcher quirks are deferred to Phase 2.

##### Phase 2: Kimi reasoning_content

Phase 2 adds only the Kimi / Moonshot chat-compatible `reasoning_content` handler. It accumulates streamed `reasoning_content` and echoes it into the assistant tool-call continuation message; empty `PresetId`, `custom-openai-chat`, and other OpenAI-compatible providers remain unchanged. DeepSeek / GLM / Qwen / Ollama quirks remain deferred; DeepSeek is intentionally not treated as same-shape because DeepSeek-reasoner has the opposite input contract: `reasoning_content` in input messages returns 400, and function calling is unsupported.

##### v0.24.1 hotfix: nested Provider preset UI changes

v0.24.1 fixes Provider `PresetId` / `Kind` auto-fill in Project Settings. Nested
`Providers[]` edits route through `PostEditChangeChainProperty`; the hotfix now
handles that chain event directly so preset selection receives the correct array
index, fills the selected provider row, and writes the provider backup. A
synthetic chain-event automation test covers the regression.

##### v0.24.2 hotfix: Provider preset international endpoints

v0.24.2 hides the four deprecated legacy `OpenAI*` Project Settings fields from
the UI while preserving `Config` deserialization and legacy INI migration. Kimi
/ GLM / Qwen presets now default to international endpoints: Kimi
`https://api.moonshot.ai/v1/chat/completions`, GLM/Zhipu
`https://api.z.ai/api/paas/v4/chat/completions`, and Qwen
`https://dashscope-intl.aliyuncs.com/compatible-mode/v1/chat/completions`.
`Z.ai` is Zhipu/GLM's international endpoint and rebrand, so GLM no longer
defaults to `open.bigmodel.cn`. Existing saved provider `BaseUrl` values are
preserved on upgrade; to switch an existing v0.24.0/v0.24.1 provider to the
intl defaults, clear and re-pick the preset or manually edit `BaseUrl`.

##### v0.24.3 hotfix: Codex CLI subprocess PATH

v0.24.3 fixes Codex CLI provider subprocess `bun: not found` and similar tool
resolution failures when Unreal Editor is launched without inheriting the
user's interactive shell `PATH`. Codex CLI subprocesses now prepend these common
Mac developer-tool directories before the inherited `PATH`:

- `$HOME/.bun/bin`
- `$HOME/.local/bin`
- `$HOME/.cargo/bin`
- `/opt/homebrew/bin`
- `/usr/local/bin`

Both the chat exec path and the cleanup kill exec path use this PATH prefix.
Shells silently ignore non-existent PATH entries. The Codex CLI provider remains
macOS/Linux only; Windows users should continue using the Codex Desktop /
CodexAppServer provider. No INI/settings migration is involved; existing user
Codex Binary Path values are preserved.

##### v0.24.4 hotfix: Codex CLI bash quoting

v0.24.4 fixes two bash-quoting bugs in the CodexProvider subprocess invocation
that prevented Codex CLI from receiving the user prompt correctly:

- **v0.24.3 regression**: The dev-tool PATH prefix added in v0.24.3 wrapped
  `$PATH` assignment in literal `"..."`. The UE Unix
  `FPlatformProcess::CreateProc` tokenizer (`ParseCmdLineToken` in
  `UnixPlatformProcess.cpp`) treats `"` as a quote delimiter; the literal `"`
  chars in the bash `-c` payload mangled it into `bash: -c: option requires an
  argument`. Fixed by removing the redundant `"..."` (POSIX bash assignment-RHS
  is not word-split anyway).

- **Pre-v0.24.3 latent bug**: The prompt-pass path at `CodexProvider.cpp:577`
  used `"$(cat${IFS}<promptfile>)"` to inject the user prompt via bash command
  substitution. The same UE tokenizer quirk eats the literal `"` wrapping. Was
  masked by the `bun: not found` error before v0.24.3; surfaced once the PATH
  prefix worked. Fixed by passing the composed prompt text directly as a
  `$'...'`-wrapped argv word via the existing `QuoteForBashWordNoSpaces` helper,
  which handles `ConversationContext` correctly for multi-turn chat.

Also defensively encodes `"` chars in `QuoteForBashWordNoSpaces` (so future user
input cannot reintroduce the same bug class), introduces a new
`Docs/AIProviderArchitecture.md` documenting the shell-escaping invariants in
writing, and adds two integration tests that exec the actual production
command-builder helpers through `/bin/bash -c` against an argc/argv-printing
helper script - catching argv-boundary regressions that pure-string tests miss.

**Known limitation (NOT fixed in v0.24.4; tracked as v0.25 Reform B)**: Even
after these fixes, Codex CLI chat through the UE Assistant panel does not return
codex's reply. Root cause: `codex-agent start -w` waits for job
`status != running`, but codex CLI is interactive (stays idle after first turn,
never exits), and codex's reply lives in a tmux pane that the parent process
never reads. **Use the Codex Desktop bridge (CodexAppServer provider) instead
until v0.25 ships.** See `Docs/AIProviderArchitecture.md` Section G (G1) for
the planned rewrite.

| Preset | Default Model |
|---|---|
| Custom OpenAI-compatible | empty/manual |
| OpenAI Responses | `gpt-5.1` |
| Anthropic Claude | `claude-sonnet-4-6` |
| Kimi (Moonshot) | `moonshot-v1-8k` |
| GLM (Zhipu) | `glm-4` |
| DeepSeek | `deepseek-chat` |
| Qwen (Tongyi) | `qwen-plus` |
| Ollama (local) | `llama3.1` |
| Codex CLI | Model empty; `CodexExtraArgs` defaults to `-m gpt-5.5 -r xhigh` |
| Codex Desktop / App Server | Model empty; bridge/default decides |

4. Set `ActiveProviderId` to the row you want. Save.
5. Open `Window > UEAtelier Chat`. The top-bar Provider selector should list your entries; the model field reflects the active provider.

#### Codex Desktop Bridge (optional, recommended for "let Codex do work")

**1. Start the bridge**

```bash
# macOS / Linux
Tools/UnrealMcpCodexBridge/start-bridge.sh
```

```cmd
:: Windows cmd.exe
Tools\UnrealMcpCodexBridge\start-bridge.cmd
```

```powershell
# Windows PowerShell
powershell -ExecutionPolicy Bypass -File Tools\UnrealMcpCodexBridge\start-bridge.ps1
```

Expected startup:

```text
Codex binary: /opt/homebrew/bin/codex
UEAtelier Codex Bridge listening at ws://127.0.0.1:8766/uevolve
Codex app-server transport=unix endpoint=/var/.../codex.sock     # POSIX
# or:
Codex app-server transport=ws endpoint=ws://127.0.0.1:<port>     # Windows or override
```

**2. Add a `CodexAppServer` provider in UE**

```toml
Id          = codex-desktop
DisplayName = Codex Desktop
Kind        = CodexAppServer
BaseUrl     = ws://127.0.0.1:8766/uevolve
```

**3. Switch to it in the chat panel and ask `/ask`.** The bridge auto-registers the editor's MCP server with Codex, so Codex can call all Unreal tools. Edit Model in the chat panel combo; edit ReasoningEffort in `Project Settings > Plugins > Unreal MCP > AI > Providers`.

**Environment variables (cross-platform)**

| Var | Default | Purpose |
|---|---|---|
| `UEVOLVE_CODEX_BRIDGE_PORT` | 8766 | UE-facing WebSocket port |
| `UEVOLVE_CODEX_MODEL` | `gpt-5.5` | Bridge daemon default model; UE provider / per-turn requests can override it |
| `UEVOLVE_CODEX_EFFORT` | `xhigh` | Bridge daemon default reasoning effort (`low` / `medium` / `high` / `xhigh`) |
| `UEVOLVE_CODEX_TRANSPORT` | `unix` (POSIX) / `ws` (Windows) | Transport to Codex App Server |
| `UEVOLVE_CODEX_APP_SERVER_PORT` | auto | Pin Codex's listen port (ws mode) |
| `UEVOLVE_CODEX_BIN` | auto (`where`/`which`) | Absolute path to `codex.exe`, `codex.cmd`, or `codex` if auto-detection misses it |
| `UEVOLVE_MCP_URL` | `http://127.0.0.1:8765/mcp` | UE MCP server endpoint |
| `UEVOLVE_CODEX_APPROVAL_POLICY` | `reject` | Codex OS-level cap policy. `auto-approve` for local dev only. |

#### Verify

In the chat panel:

1. `/ask Use unreal.editor_status to report the current level` → tool card + currentMap
2. `/ask Spawn a PointLight named TestLight at origin` → a light appears in the level
3. (Codex Desktop path) `/ask Use execute_python to build a 3x3 cube maze at origin` → Codex calls MCP, maze appears

---

## 日本語

### アップデート概要

このリリースで 2 つの feature ブランチが `main` にマージされました：

1. **マルチプロバイダー AI 基盤**（`feat/ai-multi-provider`）
2. **RAG クローズドループとアクティビティ基盤**（`dev-b/rag-closed-loop`）
3. **クロスプラットフォーム Codex Desktop Bridge**（P7.D）

#### 1. マルチプロバイダー AI

`UUnrealMcpSettings::Providers[]` が旧来の OpenAI 単一フィールドを置き換えます。既存の OpenAI ユーザーは初回起動時に**自動マイグレーション**されます（旧フィールドは保持されたまま deprecated 化、新しい配列に `openai-default` エントリが追加されます）。5 種類の Provider Kind を追加：

| Kind | 用途 | 認証 |
|---|---|---|
| `OpenAiResponses` | OpenAI Responses API（デフォルト） | `Authorization: Bearer` |
| `OpenAiChatCompat` | OpenAI 互換 chat/completions — **Kimi（Moonshot）、GLM（Zhipu）、DeepSeek、Qwen、Ollama** をカバー | 同上 |
| `AnthropicMessages` | Anthropic Claude Messages API（tool_use / tool_result + thinking） | `x-api-key` + `anthropic-version` |
| `Codex` | ローカル Codex CLI サブプロセス（macOS/Linux のみ） — `gpt-5.5 xhigh workspace-write` 固定 | なし |
| `CodexAppServer` | Codex App Server bridge 経由 — **クロスプラットフォーム** | なし |

チャットパネル上部に **Provider/Model セレクター** が追加されました。`Codex` CLI provider は `gpt-5.5 (locked)` と表示され、`CodexAppServer` / Codex Desktop bridge provider ではモデルを編集できます。

#### 2. RAG クローズドループ

- Activity Log 基盤：`UnrealMcpActivityLog.{h,cpp}` + `Schemas/UnrealMcpActivityLogEntry.schema.json` + ローリングウィンドウ永続化
- ツール実行とチャットパネルからの always-on イベント送出
- `Tools/UnrealMcpKnowledge/build_index.py` がアクティビティログとスキルドキュメントをインデックス化、kind-aware 検索 / フィルタ / グルーピングをサポート
- `preview_change_plan` と `verify_task_outcome` が manifest schema の `evidence[]` フィールドへ証跡を注入・反映

#### 3. Codex Desktop Bridge

`Tools/UnrealMcpCodexBridge/` は Bun の TypeScript daemon で、Codex App Server プロトコル（POSIX では WebSocket-over-UDS、Windows では stdio child-process pipe — 現在の Codex ビルドは `--listen ws://...` を拒否します。issue #2 comment 8 参照）を UE plugin が消費するシンプルな `ws://127.0.0.1:8766/uevolve` エンドポイントに橋渡しします。UE 側 inbound listener は外向き transport にかかわらず常に WebSocket です。

**主要機能**：
- Codex が **Unreal MCP ツール（`unreal.spawn_actor`、`unreal.execute_python` 等、表示される全ツール）を直接呼び出せる**ようになります。これまでのように `/tool ...` 文字列を返すだけ、ではなく実行されます
- セーフティポスチャ：Codex 内蔵のファイル編集・シェル実行はすべて拒否。MCP ツール呼び出し承認のうち、`serverName === "unrealmcp"` かつ `_meta.codex_approval_kind === "mcp_tool_call"` のものだけが自動承認されます。破壊的変更は UE MCP 側の audit / dry-run / safety レイヤーが担保します
- クロスプラットフォーム：macOS/Linux は Unix Domain Socket、Windows は `ws://127.0.0.1:<auto-port>`
- モデルと reasoning effort はユーザーが選択可能です。Model はチャットパネル上部の combo で編集し、ReasoningEffort は `Project Settings > Plugins > Unreal MCP > AI > Providers` で設定します。別の `Codex` CLI provider は引き続き `gpt-5.5` + `xhigh` 固定です

#### 4. UI 改善

- 進捗 `SThrobber` + 経過時間ラベル
- エラーエントリの赤背景 + ⚠ 接頭辞
- ツールカードはデフォルト折りたたみ、クリックで展開して args + result を表示
- 各メッセージに 📋 コピーボタン
- スマートオートスクロール — ユーザーが下部にいる時のみ追従、履歴閲覧中は中断しない

#### 5. スキーマとツールレジストリ

- `Schemas/UnrealMcpExtensionManifest.schema.json` に evidence / outcome フィールド追加
- `Schemas/UnrealMcpActivityLogEntry.schema.json` 新規
- `Tools/UnrealMcpToolRegistry/tools.json` に新ツール（chat_label、knowledge 検索拡張等）

#### 6. 既知の制限

- macOS で「すでに起動中の Codex Desktop の IPC socket にアタッチする」機能は今後の拡張です。現状の bridge は常に独自の codex app-server サブプロセスを spawn します
- `Codex` CLI provider（直接サブプロセス）は macOS/Linux のみ対応。Windows ユーザーは `CodexAppServer` provider を使用してください
- Bridge V1 の承認ポリシーは Codex の OS レベル操作を意図的に拒否します（設計通り）

### Engine Compatibility / エンジン互換性

UEAtelier plugin は Unreal Engine 5.6 と 5.7 の両方を同じソースツリーでサポートします。最低対応バージョンは UE 5.6 のため、`UEvolve.uproject` の既定 `EngineAssociation` は `5.6` です。UE 5.7 のユーザーは **Switch Unreal Engine Version** でローカルプロジェクトの関連付けを更新できます。今回のコードのみの監査では UE 5.7 専用の conditional shim が必要な plugin API 呼び出しは見つかっていません。最終確認は UE 5.6 環境でのコンパイルで行ってください。

- **Pre-commit / linter**: コミット前に `python3 Tools/check_ue56_compat.py` を実行し、既知の UE 5.7-only API を検出してください。UE 5.7 環境では通る変更が UE 5.6 ユーザー環境で壊れることを防ぎます。

---

### セットアップ手順（Git から新規クローン）

#### 共通の前提条件

| ツール | 用途 | バージョン |
|---|---|---|
| Unreal Engine | エディタ | **5.7** |
| Git | ソース管理 | 任意 |
| Bun | Codex bridge daemon（`CodexAppServer` provider 使用時のみ必要） | ≥ 1.1 |
| Codex CLI | Codex 系 provider に必須 | ≥ 0.130 |
| Codex Desktop | 任意の GUI コンパニオン | 最新 |

#### macOS

```bash
brew install --cask unreal-engine                              # または Epic Launcher 経由
curl -fsSL https://bun.sh/install | bash
npm install -g @openai/codex-cli

git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEAtelier

/Users/Shared/Epic\ Games/UE_5.7/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh \
  -project="$(pwd)/UEvolve.uproject" -game
/Users/Shared/Epic\ Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh \
  UEvolveEditor Mac Development -Project="$(pwd)/UEvolve.uproject" -waitmutex

"/Users/Shared/Epic Games/UE_5.7/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "$(pwd)/UEvolve.uproject"
```

#### Linux

```bash
curl -fsSL https://bun.sh/install | bash
npm install -g @openai/codex-cli

git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEAtelier
/path/to/UE_5.7/Engine/Build/BatchFiles/Linux/Build.sh \
  UEvolveEditor Linux Development -Project="$(pwd)/UEvolve.uproject" -waitmutex
```

#### Windows

```powershell
# Epic Games Launcher で UE 5.7 をインストール
# https://bun.sh から Windows 用 Bun をインストール
# Bun のインストール後、新しいターミナルで bun --version が動くことを確認
# bun: command not found の場合は、%USERPROFILE%\.bun\bin をユーザー PATH に追加
# （System Properties → Environment Variables → User PATH）、または Scoop でインストール：
# scoop install bun（PATH は自動設定）
# PATH 変更後は、ターミナル / VS Code / Explorer を閉じて開き直してから start-bridge.cmd を再試行
# npm install -g @openai/codex-cli  （または Codex Desktop アプリを使用）

git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEAtelier

& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
  UEvolveEditor Win64 Development -Project="$PWD\UEvolve.uproject" -waitmutex

& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" `
  "$PWD\UEvolve.uproject"
```

#### AI Provider の設定

1. `Edit > Project Settings > Plugins > Unreal MCP > AI`
2. `bEnableAiAssistant` を ON
3. `Providers` 配列に最低 1 行追加。例：

```toml
# Kimi（Moonshot）
Id          = kimi-default
DisplayName = Kimi
Kind        = OpenAiChatCompat
BaseUrl     = https://api.moonshot.cn/v1/chat/completions
ApiKey      = <Moonshot の API キー>
Model       = moonshot-v1-8k

# GLM（Zhipu）
Kind        = OpenAiChatCompat
BaseUrl     = https://open.bigmodel.cn/api/paas/v4/chat/completions
Model       = glm-4

# DeepSeek
BaseUrl     = https://api.deepseek.com/v1/chat/completions
Model       = deepseek-chat

# Anthropic Claude
Kind        = AnthropicMessages
BaseUrl     = https://api.anthropic.com/v1/messages
Model       = claude-sonnet-4-6
```

#### Provider presets

Phase 1 では provider preset のデータと Project Settings UI を追加します。`PresetId` は選択した preset を記録します。空の `PresetId` または `custom-openai-chat` は、手動の OpenAI-compatible provider として引き続き有効です。vendor 固有の dispatcher quirks は Phase 2 で対応します。

##### Phase 2: Kimi reasoning_content

Phase 2 では Kimi / Moonshot chat-compatible `reasoning_content` handler のみを追加します. streamed `reasoning_content` を累積し, assistant tool-call continuation message に同じ text を戻します. 空の `PresetId`, `custom-openai-chat`, その他の OpenAI-compatible provider は変更しません. DeepSeek / GLM / Qwen / Ollama quirks は deferred のままです. DeepSeek は同形として扱いません. DeepSeek-reasoner は input contract が逆で, input message に `reasoning_content` があると 400 を返し, function calling も unsupported です.

##### v0.24.1 hotfix: ネストされた Provider preset UI 変更

v0.24.1 では Project Settings の Provider `PresetId` / `Kind` 自動入力を修正します。
ネストされた `Providers[]` 編集は `PostEditChangeChainProperty` 経由になるため、
hotfix はその chain event を直接処理し、preset 選択が正しい配列 index を受け取り、
選択した provider 行を埋めて provider backup を書きます。synthetic chain-event
automation test でこの回帰をカバーします。

##### v0.24.2 hotfix: Provider preset international endpoints

v0.24.2 では deprecated legacy `OpenAI*` Project Settings 4 項目を UI から隠し、
`Config` deserialization と legacy INI migration は維持します。Kimi / GLM / Qwen
preset は international endpoints を default にします: Kimi
`https://api.moonshot.ai/v1/chat/completions`, GLM/Zhipu
`https://api.z.ai/api/paas/v4/chat/completions`, Qwen
`https://dashscope-intl.aliyuncs.com/compatible-mode/v1/chat/completions`.
`Z.ai` は Zhipu/GLM の international endpoint / rebrand なので、GLM は
`open.bigmodel.cn` を default にしません。upgrade 時、既存 provider の saved
`BaseUrl` は保持されます。v0.24.0/v0.24.1 既存 provider を intl default に切り替えるには、
preset を clear して選び直すか、`BaseUrl` を手動編集してください。

##### v0.24.3 hotfix: Codex CLI subprocess PATH

v0.24.3 では、Unreal Editor がユーザーの interactive shell `PATH` を継承せずに起動した場合に、
Codex CLI provider の子プロセスで `bun: not found` などの tool 解決エラーが起きる問題を修正します。
Codex CLI 子プロセスは、継承した `PATH` の前に次の一般的な Mac developer-tool directory を追加します:

- `$HOME/.bun/bin`
- `$HOME/.local/bin`
- `$HOME/.cargo/bin`
- `/opt/homebrew/bin`
- `/usr/local/bin`

chat exec path と cleanup kill exec path の両方がこの PATH prefix を使います。存在しない
PATH entry は shell が静かに無視します。Codex CLI provider は引き続き macOS/Linux 専用です。
Windows ユーザーは Codex Desktop / CodexAppServer provider を使ってください。この hotfix では
INI/settings migration は行わず、既存ユーザーの Codex Binary Path 値は保持されます。

##### v0.24.4 hotfix: Codex CLI bash quoting

v0.24.4 では、CodexProvider の子プロセス起動で Codex CLI がユーザー prompt を正しく
受け取れなくなる 2 つの bash quoting bug を修正します。

- **v0.24.3 regression**：v0.24.3 で追加した developer-tool PATH prefix が `$PATH`
  assignment を literal `"..."` で包んでいました。UE Unix の
  `FPlatformProcess::CreateProc` tokenizer（`UnixPlatformProcess.cpp` の
  `ParseCmdLineToken`）は `"` を quote delimiter として扱うため、bash `-c` payload
  内の literal `"` がコマンドを壊し、`bash: -c: option requires an argument` になります。
  redundant な `"..."` を取り除いて修正しました。POSIX bash の assignment RHS はそもそも
  word-split されません。

- **Pre-v0.24.3 latent bug**：`CodexProvider.cpp:577` の prompt-pass path は
  `"$(cat${IFS}<promptfile>)"` で bash command substitution 経由の user prompt 注入を
  行っていました。同じ UE tokenizer quirk により外側の literal `"` が消費されます。この問題は
  v0.24.3 以前は `bun: not found` が先に出て masked されていましたが、PATH prefix が動くと
  表面化しました。composed prompt text を既存の `QuoteForBashWordNoSpaces` helper 経由で
  `$'...'` wrapped argv word として直接渡すことで修正し、multi-turn chat の
  `ConversationContext` も正しく維持します。

加えて、`QuoteForBashWordNoSpaces` は `"` 文字を防御的に `\x22` へエンコードします
（将来のユーザー入力で同じ bug class を再導入しないため）。新しい
`Docs/AIProviderArchitecture.md` に shell-escaping invariants を文書化し、実際の
production command-builder helpers を `/bin/bash -c` で argc/argv-printing helper script
に対して実行する integration tests を 2 本追加しました。pure-string tests では見逃す
argv boundary regression を検出します。

**既知の制限（v0.24.4 では修正しません。v0.25 Reform B で追跡）**：これらの修正後も、
UE Assistant panel から Codex CLI chat を使う場合、codex の reply は戻りません。根因は
`codex-agent start -w` が job `status != running` を待つ一方で、codex CLI は interactive
で、初回 turn 後も idle のまま次の入力を待ち、終了しないことです。codex の実際の reply は
parent process が読まない tmux pane にあります。v0.25 までは Codex Desktop bridge
（CodexAppServer provider）を使ってください。予定されている rewrite は
`Docs/AIProviderArchitecture.md` Section G (G1) を参照してください。

| Preset | Default Model |
|---|---|
| Custom OpenAI-compatible | empty/manual |
| OpenAI Responses | `gpt-5.1` |
| Anthropic Claude | `claude-sonnet-4-6` |
| Kimi (Moonshot) | `moonshot-v1-8k` |
| GLM (Zhipu) | `glm-4` |
| DeepSeek | `deepseek-chat` |
| Qwen (Tongyi) | `qwen-plus` |
| Ollama (local) | `llama3.1` |
| Codex CLI | Model empty; `CodexExtraArgs` defaults to `-m gpt-5.5 -r xhigh` |
| Codex Desktop / App Server | Model empty; bridge/default decides |

4. `ActiveProviderId` を選択した行の `Id` に設定して保存
5. `Window > UEAtelier Chat` を開き、上部の Provider セレクターが利用可能なら成功

#### Codex Desktop Bridge（任意、Codex に作業させたい場合に推奨）

**1. Bridge を起動**

```bash
# macOS / Linux
Tools/UnrealMcpCodexBridge/start-bridge.sh
```

```cmd
:: Windows cmd.exe
Tools\UnrealMcpCodexBridge\start-bridge.cmd
```

```powershell
# Windows PowerShell
powershell -ExecutionPolicy Bypass -File Tools\UnrealMcpCodexBridge\start-bridge.ps1
```

期待される起動ログ：

```text
Codex binary: /opt/homebrew/bin/codex
UEAtelier Codex Bridge listening at ws://127.0.0.1:8766/uevolve
Codex app-server transport=unix endpoint=/var/.../codex.sock     # POSIX
# あるいは：
Codex app-server transport=ws endpoint=ws://127.0.0.1:<port>     # Windows またはオーバーライド時
```

**2. UE に `CodexAppServer` provider を追加**

```toml
Id          = codex-desktop
DisplayName = Codex Desktop
Kind        = CodexAppServer
BaseUrl     = ws://127.0.0.1:8766/uevolve
```

**3. チャットパネルで切り替えて `/ask` を送信。** Bridge がエディタの MCP server（`http://127.0.0.1:8765/mcp`）を Codex に自動登録するので、Codex は Unreal ツールを直接呼び出せます。Model はチャットパネル上部の combo で編集し、ReasoningEffort は `Project Settings > Plugins > Unreal MCP > AI > Providers` で設定します。

**環境変数（クロスプラットフォーム）**

| 変数 | デフォルト | 用途 |
|---|---|---|
| `UEVOLVE_CODEX_BRIDGE_PORT` | 8766 | UE 側 WebSocket ポート |
| `UEVOLVE_CODEX_MODEL` | `gpt-5.5` | bridge daemon のデフォルトモデル。UE provider / 各 turn で上書き可能 |
| `UEVOLVE_CODEX_EFFORT` | `xhigh` | bridge daemon のデフォルト reasoning effort（`low` / `medium` / `high` / `xhigh`） |
| `UEVOLVE_CODEX_TRANSPORT` | `unix`(POSIX) / `ws`(Windows) | Bridge ⇔ Codex 間のトランスポート |
| `UEVOLVE_CODEX_APP_SERVER_PORT` | auto | Codex の listen ポートを固定（ws モード） |
| `UEVOLVE_CODEX_BIN` | auto (`where`/`which`) | 自動検出できない場合の `codex.exe`、`codex.cmd`、または `codex` の絶対パス |
| `UEVOLVE_MCP_URL` | `http://127.0.0.1:8765/mcp` | UE MCP server エンドポイント |
| `UEVOLVE_CODEX_APPROVAL_POLICY` | `reject` | Codex の OS 操作ポリシー。`auto-approve` はローカル開発専用 |

#### 動作確認

チャットパネルで順に：

1. `/ask unreal.editor_status で現在のレベルを報告して` → ツールカード + currentMap が表示
2. `/ask 原点に TestLight という名前の PointLight を生成して` → レベルに実際にライトが出現
3. （Codex Desktop ルート）`/ask execute_python で原点に 3x3 の立方体迷宫を作って` → Codex が MCP を呼び、迷宫が出現

---

## v0.25.0 — Reform B (Codex CLI exec rewrite)

### 中文

v0.25 重写 Codex CLI provider，改为直接调用 `codex exec --json --ephemeral`
的一次性非交互模式。这个版本解决 v0.24.4 Known Limitation G1（UE
Assistant panel 使用 Codex CLI chat 不返回回复）和 G4（多轮 composed prompt
进入 argv 后可能触发命令行长度限制）。`ConversationContext` 现在通过子进程 stdin
传入，最新用户消息仍然是最后一个 argv prompt。

#### Breaking change for Codex CLI provider users

1. 安装或定位 codex CLI binary：在终端运行 `which codex`。
2. 更新 Codex Binary Path 设置：旧值是 `.../codex-orchestrator/bin/codex-agent`，新值是 `codex` binary，例如 `/opt/homebrew/bin/codex`。
3. 从 Codex Extra Args 删除 `-m gpt-5.5 -r xhigh`，或改写为 `-c model="gpt-5.5" -c reasoning_effort="xhigh"`。
4. 第一次使用时先在终端运行 `codex login`。
5. 在 UEAtelier Chat 中发送一次 `/ask` 做 smoke test。

Codex CLI provider 不再需要 codex-agent、bun、tmux。Codex Desktop / App
Server provider 不受影响，WebSocket bridge 保持不变。Windows 用户继续使用
Codex Desktop bridge；CLI provider 仍然只支持 macOS/Linux。剩余已知限制：
G2（tilde path 不展开）、G3（hardcoded PATH list）、G5（provider
observability）、G6（无 token-level streaming），全部 deferred。

### EN

v0.25 rewrites the Codex CLI provider to invoke
`codex exec --json --ephemeral` directly in one-shot non-interactive mode. This
resolves v0.24.4 Known Limitation G1 (Codex CLI chat did not return replies to
the UE Assistant panel) and G4 (composed prompt argv length risk).
`ConversationContext` now goes through the child process stdin pipe; the latest
user message remains the final argv prompt.

#### Breaking change for Codex CLI provider users

1. Install or locate the codex CLI binary with `which codex`.
2. Update the Codex Binary Path setting: the old value was `.../codex-orchestrator/bin/codex-agent`; the new value is the `codex` binary, for example `/opt/homebrew/bin/codex`.
3. Remove `-m gpt-5.5 -r xhigh` from Codex Extra Args, or rewrite it as `-c model="gpt-5.5" -c reasoning_effort="xhigh"`.
4. Run `codex login` in a terminal if this is the first use.
5. Smoke-test UEAtelier Chat with one `/ask` request.

The Codex CLI provider no longer requires codex-agent, bun, or tmux. The Codex
Desktop / App Server provider is unaffected; the WebSocket bridge is unchanged.
Windows users should continue using the Codex Desktop bridge because the CLI
provider remains macOS/Linux only. Remaining known limitations are G2 (tilde
paths), G3 (hardcoded PATH list), G5 (provider observability), and G6 (no
token-level streaming); all are deferred.

### 日本語

v0.25 では Codex CLI provider を書き直し、`codex exec --json --ephemeral`
を直接呼ぶ one-shot / non-interactive mode に変更します。これにより v0.24.4
Known Limitation G1（Codex CLI chat の reply が UE Assistant panel に戻らない）
と G4（composed prompt を argv に入れることによる command-line length risk）を解決します。
`ConversationContext` は子プロセス stdin pipe 経由になり、最新の user message だけが
最後の argv prompt になります。

#### Breaking change for Codex CLI provider users

1. `which codex` を terminal で実行し、codex CLI binary を install / locate します。
2. Codex Binary Path を更新します。旧値は `.../codex-orchestrator/bin/codex-agent`、新値は `codex` binary（例: `/opt/homebrew/bin/codex`）です。
3. Codex Extra Args から `-m gpt-5.5 -r xhigh` を削除するか、`-c model="gpt-5.5" -c reasoning_effort="xhigh"` に書き換えます。
4. 初回利用時は terminal で `codex login` を実行します。
5. UEAtelier Chat で `/ask` を 1 回送り smoke test します。

Codex CLI provider では codex-agent、bun、tmux は不要になりました。Codex
Desktop / App Server provider は影響を受けず、WebSocket bridge は変更ありません。
Windows ユーザーは引き続き Codex Desktop bridge を使ってください。CLI provider は
macOS/Linux 専用のままです。残る known limitations は G2（tilde path）、
G3（hardcoded PATH list）、G5（provider observability）、G6（token-level
streaming なし）で、いずれも deferred です。

---

## v0.26.0 — 2026-05-22 — Reform C

### 中文

v0.26.0 完成 Reform C：三个 server-message provider 共享 `UnrealMcpAssistantSystemPromptBuilder`，内置 6 条安全规则，避免空 system prompt 与 provider prompt divergence；AssistantRun seam 接入 approval gate，遇到 `approval_required` / `requiresApproval=true` 必须等待用户批准；Python user-extension 成为一等默认路径，工具位于 `Tools/UnrealMcpPyTools/<tool_id>/main.py`，新增 `unreal.mcp_user_registry_reload` 与 `unreal.mcp_user_tool_smoke` 两个 control tools，配合 11-code audit taxonomy 与 scaffold/apply/audit/reload/smoke 全链路 lifecycle schema，只有 `lifecycle.callableNow=true` 且 smoke/audit 通过后才可声称新工具可调用。

### EN

v0.26.0 completes Reform C: the three server-message providers now share `UnrealMcpAssistantSystemPromptBuilder`, which always emits the six safety rules and prevents empty system prompts or provider prompt drift; the AssistantRun seam enforces the approval gate for `approval_required` / `requiresApproval=true`; Python user extensions are first-class by default at `Tools/UnrealMcpPyTools/<tool_id>/main.py`, backed by the new `unreal.mcp_user_registry_reload` and `unreal.mcp_user_tool_smoke` control tools, the 11-code audit taxonomy, and one lifecycle schema across scaffold/apply/audit/reload/smoke so a new tool is only callable after `lifecycle.callableNow=true` plus smoke or audit confirmation.

### 日本語

v0.26.0 では Reform C を完了しました。3 つの server-message provider は `UnrealMcpAssistantSystemPromptBuilder` を共有し、6 つの safety rules を常に含めることで空の system prompt と provider prompt divergence を防ぎます。AssistantRun seam では approval gate を適用し、`approval_required` / `requiresApproval=true` ではユーザー承認を待ちます。Python user-extension は `Tools/UnrealMcpPyTools/<tool_id>/main.py` を使う default path となり、新しい `unreal.mcp_user_registry_reload` と `unreal.mcp_user_tool_smoke` control tools、11-code audit taxonomy、scaffold/apply/audit/reload/smoke をまたぐ lifecycle schema により、`lifecycle.callableNow=true` と smoke/audit confirmation が揃うまで新しい tool を callable と主張できません。

---

## v0.26.1 — 2026-05-22 — 技能发现接线 / skills discovery wiring

### 中文

v0.26.1 给中央 system prompt 增加第 7 条安全规则：造新工具或把已完成的工作流产品化之前，必须先用 `unreal.skill_list` / `unreal.skill_read` 查项目技能（从 `mcp-capability-routing` 开始）。配套新增插件技能 `mcp-capability-routing`——一个"前门"路由器：先用 `tool_recommend` / `tool_gap_analyze` 找并组合现有的 162 个工具；当用户直接要求"做成工具"时，走安全的 Python user-extension 轨（scaffold → reload → smoke），绝不把 handler 手动合进 core C++。这样修正了 v0.26 事故的真实成因——用户直接要求造工具是合理的，错在构建路径，而非"该不该造"。无工具面变化（仍 162），仅新增提示规则与一个 skill。

### EN

v0.26.1 adds a seventh safety rule to the central system prompt: before scaffolding a new tool or productizing a completed workflow, the model must FIRST consult project skills via `unreal.skill_list` / `unreal.skill_read` (starting with `mcp-capability-routing`). It ships the companion `mcp-capability-routing` skill — a front-door router that finds and composes the existing 162 tools via `tool_recommend` / `tool_gap_analyze`, and, when the user directly asks to "make a tool", routes to the safe Python user-extension track (scaffold → reload → smoke) instead of hand-merging a handler into core C++. This corrects the real root cause of the v0.26 incident: a direct request to make a tool was legitimate; the fault was the build path, not the decision to build. No tool-surface change (still 162) — only the new prompt rule plus one skill.

### 日本語

v0.26.1 では中央 system prompt に 7 番目の safety rule を追加しました。新しい tool を scaffold したり完了済みワークフローを productize する前に、まず `unreal.skill_list` / `unreal.skill_read` でプロジェクトの skills を参照する必要があります（`mcp-capability-routing` から開始）。付随する `mcp-capability-routing` skill を同梱します。これは front-door router で、`tool_recommend` / `tool_gap_analyze` で既存の 162 tools を見つけて compose し、ユーザーが直接「tool を作って」と要求した場合は安全な Python user-extension track（scaffold → reload → smoke）に誘導し、handler を core C++ に手動マージしません。これにより v0.26 incident の真の根本原因を修正します。tool を作る直接要求は正当であり、誤りは build path であって作る判断ではありませんでした。tool-surface の変更なし（162 のまま）、新しい prompt rule と 1 つの skill のみです。

---

## v0.26.2 — 2026-05-23 — reliability patch

### 中文

v0.26.2 是紧急可靠性补丁。Provider preset 自动化测试不再改写用户的
per-user AI 配置：legacy OpenAI migration 覆盖改为测试纯 helper
`MakeProviderFromLegacyOpenAI`，避免 `GetMutableDefault<UUnrealMcpSettings>()`、
`SaveConfig()` 与 `providers.backup.json` 副作用。`unreal.mcp_user_tool_smoke`
的 `dryRunArgs` schema 从开放 object 改为 JSON string，同时 C++ 执行端继续兼容
legacy object 调用，并新增 `UnrealMcp.Audit.AllVisibleToolsSchemaCompatible`
回归测试，要求 visible core tools 的 `schemaIncompatibleCount=0`。v0.26.2
release checklist 还包含 reviewer 侧 dangling-OFPA test-level resave
（本 Codex 任务不改 `.umap`），并新增 manual editor-smoke checklist。无
tool-surface 变化（仍 162）。

### EN

v0.26.2 is an emergency reliability patch. Provider preset automation tests no
longer overwrite the user's per-user AI settings: legacy OpenAI migration
coverage now uses the pure `MakeProviderFromLegacyOpenAI` helper instead of
mutating `GetMutableDefault<UUnrealMcpSettings>()`, calling `SaveConfig()`, or
rewriting `providers.backup.json`. `unreal.mcp_user_tool_smoke` changes
`dryRunArgs` from an open object schema to a JSON string while the C++ execution
path remains tolerant of legacy object callers, and the new
`UnrealMcp.Audit.AllVisibleToolsSchemaCompatible` regression test requires
visible core tools to report `schemaIncompatibleCount=0`. The v0.26.2 release
checklist also includes the reviewer-side dangling-OFPA test-level resave (this
Codex task does not touch the `.umap`) and adds a manual editor-smoke checklist.
No tool-surface change (still 162).

### 日本語

v0.26.2 は緊急 reliability patch です。Provider preset の automation test は
ユーザーの per-user AI 設定を上書きしなくなりました。legacy OpenAI migration
の coverage は `GetMutableDefault<UUnrealMcpSettings>()` の mutation、
`SaveConfig()`、`providers.backup.json` の書き換えではなく、pure helper
`MakeProviderFromLegacyOpenAI` を検証します。`unreal.mcp_user_tool_smoke`
の `dryRunArgs` schema は open object から JSON string に変更し、C++ 実行側は
legacy object caller も引き続き受け付けます。さらに
`UnrealMcp.Audit.AllVisibleToolsSchemaCompatible` regression test を追加し、
visible core tools の `schemaIncompatibleCount=0` を要求します。v0.26.2
release checklist には reviewer 側の dangling-OFPA test-level resave も含めます
（この Codex task では `.umap` を変更しません）。manual editor-smoke checklist
も追加しました。tool-surface の変更はありません（162 のまま）。

---

## v0.26.3 — 2026-05-23 — ChatPanel UX

### 中文

v0.26.3 修复两个 ChatPanel 体验问题。模型输入框现在是直接可聚焦的编辑框，最近模型列表移到旁边的小 dropdown 按钮；Codex CLI provider 仍保持 gpt-5.5 / xhigh 锁定不变。工具包导出按钮在 `unreal.tools.export_package` 仍按原规则写入 `Saved/UnrealMcp/Packages` 后，会弹出文件夹选择器，把 zip 复制到用户选择的位置并 reveal；如果取消选择，则 reveal 原 Saved/Packages 位置。工具本身的 sandboxed export contract 不变。

### EN

v0.26.3 fixes two ChatPanel UX issues. The model field is now a directly focusable editable text box, with recent models moved to a small adjacent dropdown button; the Codex CLI provider remains locked to gpt-5.5 / xhigh. The export button still lets `unreal.tools.export_package` write the canonical zip under `Saved/UnrealMcp/Packages`, then offers a folder picker to copy the zip to a user-chosen location and reveal it; canceling the picker reveals the original Saved/Packages location. The tool export contract remains sandboxed and unchanged.

### 日本語

v0.26.3 では ChatPanel の UX 問題を 2 点修正しました。model field は直接 focus できる editable text box になり、recent models は隣の小さな dropdown button に移動しました。Codex CLI provider の gpt-5.5 / xhigh lock は従来どおりです。export button は `unreal.tools.export_package` が canonical zip を `Saved/UnrealMcp/Packages` に書き出した後、folder picker でユーザーが選んだ場所へ zip をコピーして reveal します。picker を cancel した場合は元の Saved/Packages を reveal します。tool export の sandboxed contract は変更していません。

---

## v0.27.0 — 2026-05-23 — wall off core from the AI

### 中文

v0.27.0 将 core source apply/pipeline 从 AI 工具面隐藏：`unreal.mcp_apply_scaffold`
与 `unreal.mcp_extension_pipeline` 变为 developer/manual-only，工具总数仍为
162，但 visible 从 152 降到 150。`unreal.scaffold_mcp_tool` 的 AI-facing
schema 现在只广告 Python track（`implementationTrack=["python"]`），AI
自扩展流程固定为 project-local Python user tool：scaffold -> reload -> smoke
-> `lifecycle.callableNow=true` 后可用。`unreal.workflow_run` 新增 hidden-tool
guard，不能作为 relay 调用 hidden core tools。中央 prompt 改写 core-flow 规则：
AI 不得调用/路由到 apply/pipeline，也不得用 `execute_python` /
`execute_python_file` 修改 `Plugins/UnrealMcp/Source` 或 ToolRegistry JSON；
硬 runtime sandbox 延后。三个 skills 合并为一个 `mcp-self-extension`，core
promotion 继续 deferred。

### EN

v0.27.0 hides core source apply/pipeline from the AI surface:
`unreal.mcp_apply_scaffold` and `unreal.mcp_extension_pipeline` are now
developer/manual-only. The registry still has 162 tools, while visible tools
drop from 152 to 150. `unreal.scaffold_mcp_tool` now advertises only the Python
track to AI callers (`implementationTrack=["python"]`), so AI self-extension is
the project-local Python user-tool flow: scaffold -> reload -> smoke -> usable
only after `lifecycle.callableNow=true`. `unreal.workflow_run` now rejects hidden
tool steps, preventing relay access to hidden core tools. The central prompt
rule now forbids calling/routing to apply/pipeline and forbids using
`execute_python` / `execute_python_file` to modify `Plugins/UnrealMcp/Source` or
ToolRegistry JSON; a hard runtime sandbox is deferred. The three project skills
are merged into `mcp-self-extension`, and core promotion remains deferred.

### 日本語

v0.27.0 では core source apply/pipeline を AI surface から隠しました。
`unreal.mcp_apply_scaffold` と `unreal.mcp_extension_pipeline` は
developer/manual-only です。registry は 162 tools のままですが、visible tools
は 152 から 150 に減ります。`unreal.scaffold_mcp_tool` の AI-facing schema は
Python track のみ（`implementationTrack=["python"]`）を広告し、AI
self-extension は project-local Python user-tool flow（scaffold -> reload ->
smoke -> `lifecycle.callableNow=true` 後に使用可）に固定されます。
`unreal.workflow_run` は hidden tool step を拒否し、hidden core tools への
relay access を防ぎます。central prompt rule は apply/pipeline の call/routing
を禁止し、`execute_python` / `execute_python_file` で
`Plugins/UnrealMcp/Source` や ToolRegistry JSON を変更することも禁止します
（hard runtime sandbox は deferred）。3 つの project skills は
`mcp-self-extension` に統合され、core promotion は引き続き deferred です。

---

## トラブルシューティング / Troubleshooting / 故障排查

| 症状 / Symptom | 原因 / Cause | 対処 / Fix |
|---|---|---|
| `127.0.0.1:8765 already in use` | 別のエディタが起動中 / Another editor is running | 全エディタを閉じてから再起動 / Close all editors |
| ChatPanel `No AI providers configured` | `Providers` 配列が空 / Empty providers list | Project Settings で 1 行追加 / Add a row |
| Bridge `Failed to connect` | Bridge daemon 未起動 / Daemon not running | `start-bridge` launcher で起動 / Start with a `start-bridge` launcher |
| `codex.cmd not found` / .cmd shim issue / .cmd シム問題 | Bun/Node spawn missed Windows shim | Set `UEVOLVE_CODEX_BIN` to full path |
| Codex returns `Provider not authorized` | API key 未設定 / Empty key | Provider 設定で `ApiKey` 設定 / Fill ApiKey |
| Anthropic 400 about `thinking` | モデルが thinking 非対応 / Model lacks thinking | `ReasoningEffort` を空に / Clear ReasoningEffort |
| Codex 「I cannot run commands」 | bridge approval policy が reject / Bridge rejecting | 期待通り（Codex MCP 経由で動作）/ Expected — Codex works via MCP |
| Windows で `unix://` エラー / unix:// error on Windows | デフォルトが unix だった / Default was unix | `UEVOLVE_CODEX_TRANSPORT=ws` を設定 / Set the env var |

---

## License & Contributing

See repo root `README.md` and `AGENTS.md`.
