# UEAtelier Release Notes — v0.32.0 (2026-06)

> Trilingual: [中文](#中文) · [English](#english) · [日本語](#日本語)
> Stable release: Task Atlas Make Tool Set rework + CLI ↔ Editor Chat sync
> Previous release: [`Docs/Release-2026-05.md`](Release-2026-05.md) (v0.31.0)

---

## 中文

### 更新摘要

本次发布是 **Task Atlas Make Tool Set 完整 rework** 与 **CLI ↔ Editor Chat 双向同步** 的稳定版。9 个 chunk 在 `experiment/post-4028be5` 分支上累积验证,经过 63/63 automation + 真实 dogfood + 端到端 GUI demo 通过后合入 `main`。

### 工具数

**181 → 190**(canonical 与 plugin 镜像 byte-equivalent)。

### 9 个 chunk 的核心交付

| Chunk | 内容 |
|---|---|
| 1 | 新 backend service `UnrealMcp::TaskAtlasService` namespace(skeleton) |
| 2 | `ClassifyTask`(纯函数 eligibility 判定)+ `ListMadeTools`(扫 PyTools generator filter) |
| 3 | `MakeComposite` + staging dir + atomic rename + reload-rejected rollback + diagnostic JSON |
| 4 | `DeleteMadeTool` / `SmokeMadeTool` / `PromoteToRag` / `IntrospectUserRegistry` |
| 5 | 6 个新 MCP tool 注册 + dispatcher + canonical/mirror tools.json(187 entries) |
| 6 | `STaskAtlasWindow` UI handler 改 thin invoker(净减 483 行) + Test Now / Debug 按钮 + helper consolidation |
| 7 | R+S 通知策略:`toolsListVersion` ETag + ChatPanel silent refresh hook |
| 8 | E2E happy-path + cross-wrapper + protocol-level tools/list 验证测试 |
| 9 | 3 个 chat sync MCP tool + Protocol B+C 硬化(190 entries) |

### 9 个新 MCP tool

**Task Atlas wrappers(chunk 5,6 个)**:
- `unreal.task_atlas_make_composite` — 从 task 生成 Python composite user tool(eligibility-aware)
- `unreal.task_atlas_delete_made_tool` — 删除已生成 user.atlas_*(`confirm:true` 必传)
- `unreal.task_atlas_list_made_tools` — 列已生成 composite tools
- `unreal.task_atlas_promote_to_rag` — task → KnowledgeSources markdown + 索引刷新
- `unreal.task_atlas_smoke_made_tool` — 显式 Test Now(可 dryRun)
- `unreal.user_registry_introspect` — 调试用 sanitized 列表

**Chat Sync(chunk 9,3 个)**:
- `unreal.chat_inject_user_input` — CLI 注入 prompt 到 editor Chat Panel,LLM 走正常 chat flow(需 AssistantRun 审批;`dryRun:true` 免审批)
- `unreal.chat_history_tail` — 读 `Saved/UnrealMcp/ChatHistory.json`(UTF-16 LE),sanitized 返回最近 N 条
- `unreal.chat_tool_log_tail` — 读当前 session ActivityLog 的 tool_call 事件(跟 editor chat 旁 tool log 同步)

### 产品 spec 关键决策(eligibility-based split)

- **`preview_ready`**:生成 Python composite,Test Now 显式 smoke(不再 auto smoke)
- **`partial` / `skeleton_pre_capture`**:markdown only 落 `Saved/UnrealMcp/TaskAtlasDrafts/`
- **`blocked`**:UI Make Tool Set 按钮 disabled + tooltip 显示 first blocked step;CLI `make_composite preferDocumentOnly:true` 返 `Blocked` outcome + markdown only

### Protocol hardening(B+C 修复 V2 暴露的 stale `task_list` 污染)

- **Fix B** `UnrealMcpProtocol.cpp::HandleToolsCall`:unknown tool call 不写 ActivityLog
- **Fix C** `UnrealMcpTaskAtlasService::ClassifyTask`:`DenyReason=="not_visible"` 不计入 `DenyCount`

→ 编辑器启动后第一个 task 不再被外部 stale probe 自动污染成 `blocked`。

### 兼容性

- 现有所有 chunk 1-8 之前的 MCP tool surface **不变**(read tools 行为兼容)
- `toolsListVersion` 是 `tools/list.result.structuredContent` 的扩展字段;旧 client 忽略它仍正常
- MCP `initialize.capabilities.tools.listChanged` 保持 `false`(Q server push 仍为 future epic;v0.32 采用 R+S 降级)
- UE 5.6 + 5.7 双引擎 build pass

### 验证

| 维度 | 结果 |
|---|---|
| UE 5.6 build | ✓ 60.52s |
| UE 5.7 build | ✓ 25.73s |
| Automation | ✓ 63/63 (49 baseline + 14 new) |
| validators | ✓ toolCount=190 / mirrorToolCount=190 / issueCount=0 |
| `check_ue56_compat.py` | ✓ 0 errors / 0 warnings |
| canonical vs mirror tools.json | byte-equivalent |
| 真实 dogfood | ✓ CLI inject chat → editor 实时显示 → make_composite → user.atlas_* replay 7 step → delete → RAG promote → knowledge_search 可搜 |

---

## English

### Summary

This stable release ships a **9-chunk rework of Task Atlas Make Tool Set** plus **bidirectional CLI ↔ editor chat sync**. Chunks accumulated on `experiment/post-4028be5`, verified by 63/63 automation, real dogfood, and end-to-end GUI demo before merging to `main`.

### Tool count

**181 → 190** (canonical and plugin mirror byte-equivalent).

### 9-chunk deliverables

| Chunk | Content |
|---|---|
| 1 | New backend service `UnrealMcp::TaskAtlasService` (skeleton) |
| 2 | `ClassifyTask` (pure eligibility) + `ListMadeTools` (PyTools generator filter) |
| 3 | `MakeComposite` + staging dir + atomic rename + reload-rejected rollback + diagnostic JSON |
| 4 | `DeleteMadeTool` / `SmokeMadeTool` / `PromoteToRag` / `IntrospectUserRegistry` |
| 5 | 6 new MCP tools + dispatcher + canonical/mirror tools.json (187) |
| 6 | `STaskAtlasWindow` UI handlers become thin invokers (net −483 LOC) + Test Now / Debug buttons + helper consolidation |
| 7 | R+S notification: `toolsListVersion` ETag + ChatPanel silent refresh hook |
| 8 | E2E happy-path + cross-wrapper + protocol-level tools/list verification tests |
| 9 | 3 chat-sync MCP tools + Protocol B+C hardening (190 entries) |

### 9 new MCP tools

**Task Atlas wrappers (chunk 5, 6 tools)**:
- `unreal.task_atlas_make_composite` — generate Python composite user tool from task (eligibility-aware)
- `unreal.task_atlas_delete_made_tool` — delete generated `user.atlas_*` (requires `confirm:true`)
- `unreal.task_atlas_list_made_tools` — list generated composite tools
- `unreal.task_atlas_promote_to_rag` — task → KnowledgeSources markdown + refresh
- `unreal.task_atlas_smoke_made_tool` — explicit Test Now (supports dryRun)
- `unreal.user_registry_introspect` — sanitized debug listing

**Chat Sync (chunk 9, 3 tools)**:
- `unreal.chat_inject_user_input` — CLI injects prompt into editor Chat Panel, LLM runs normal chat flow (requires AssistantRun approval; `dryRun:true` bypasses)
- `unreal.chat_history_tail` — reads `Saved/UnrealMcp/ChatHistory.json` (UTF-16 LE), returns sanitized last N
- `unreal.chat_tool_log_tail` — reads current session ActivityLog tool_call events (matches editor chat-side tool log)

### Product spec key decisions (eligibility-based split)

- **`preview_ready`**: generate Python composite, explicit Test Now smoke (no auto smoke)
- **`partial` / `skeleton_pre_capture`**: markdown only at `Saved/UnrealMcp/TaskAtlasDrafts/`
- **`blocked`**: UI Make Tool Set disabled + tooltip showing first blocked step; CLI `make_composite preferDocumentOnly:true` returns `Blocked` outcome with markdown-only output

### Protocol hardening (B+C, fixes V2-discovered stale `task_list` poisoning)

- **Fix B** `UnrealMcpProtocol.cpp::HandleToolsCall`: unknown tool calls no longer written to ActivityLog
- **Fix C** `UnrealMcpTaskAtlasService::ClassifyTask`: `DenyReason=="not_visible"` excluded from `DenyCount`

→ Fresh editor sessions no longer auto-poison the first Task Atlas task with external probe noise.

### Compatibility

- All pre-chunk-1 MCP tool surface behavior unchanged
- `toolsListVersion` is an extension field on `tools/list.result.structuredContent`; old clients ignore it gracefully
- MCP `initialize.capabilities.tools.listChanged` remains `false` (Q server push remains a future epic; v0.32 uses R+S degradation)
- UE 5.6 + 5.7 dual-engine build pass

### Verification

| Dimension | Result |
|---|---|
| UE 5.6 build | ✓ 60.52s |
| UE 5.7 build | ✓ 25.73s |
| Automation | ✓ 63/63 (49 baseline + 14 new) |
| validators | ✓ toolCount=190 / mirrorToolCount=190 / issueCount=0 |
| `check_ue56_compat.py` | ✓ 0 errors / 0 warnings |
| canonical vs mirror tools.json | byte-equivalent |
| Real dogfood | ✓ CLI inject chat → editor live display → make_composite → user.atlas_* replay 7 steps → delete → RAG promote → knowledge_search hit |

---

## 日本語

### 更新サマリ

本リリースは **Task Atlas Make Tool Set の 9 チャンク全面リワーク** と **CLI ↔ エディタ Chat の双方向同期** の安定版です。`experiment/post-4028be5` ブランチで 9 個のチャンクを積み上げ、63/63 automation + 実 dogfood + 全 GUI demo を通過した上で `main` にマージされました。

### ツール数

**181 → 190**(canonical と plugin mirror は byte-equivalent)。

### 9 チャンクの中核デリバラブル

| Chunk | 内容 |
|---|---|
| 1 | 新 backend service `UnrealMcp::TaskAtlasService` namespace(skeleton) |
| 2 | `ClassifyTask`(純粋関数 eligibility 判定)+ `ListMadeTools`(PyTools generator フィルタ) |
| 3 | `MakeComposite` + staging dir + atomic rename + reload 拒否ロールバック + diagnostic JSON |
| 4 | `DeleteMadeTool` / `SmokeMadeTool` / `PromoteToRag` / `IntrospectUserRegistry` |
| 5 | 6 新 MCP tool 登録 + dispatcher + canonical/mirror tools.json(187 entries) |
| 6 | `STaskAtlasWindow` UI handler を thin invoker 化(差し引き -483 LOC) + Test Now / Debug ボタン + helper 統合 |
| 7 | R+S 通知策:`toolsListVersion` ETag + ChatPanel silent refresh hook |
| 8 | E2E happy-path + cross-wrapper + protocol-level tools/list 検証テスト |
| 9 | 3 chat sync MCP tool + Protocol B+C 硬化(190 entries) |

### 9 個の新 MCP tool

**Task Atlas wrappers(chunk 5、6 個)**:
- `unreal.task_atlas_make_composite` — task から Python composite user tool 生成(eligibility-aware)
- `unreal.task_atlas_delete_made_tool` — 生成済み `user.atlas_*` 削除(`confirm:true` 必須)
- `unreal.task_atlas_list_made_tools` — 生成済み composite tool 一覧
- `unreal.task_atlas_promote_to_rag` — task → KnowledgeSources markdown + index refresh
- `unreal.task_atlas_smoke_made_tool` — 明示的 Test Now(dryRun 対応)
- `unreal.user_registry_introspect` — sanitized デバッグ一覧

**Chat Sync(chunk 9、3 個)**:
- `unreal.chat_inject_user_input` — CLI が editor Chat Panel にプロンプトを注入,LLM が通常 chat flow を実行(AssistantRun 承認必要;`dryRun:true` で免除)
- `unreal.chat_history_tail` — `Saved/UnrealMcp/ChatHistory.json`(UTF-16 LE)を読み、sanitized で直近 N 件を返却
- `unreal.chat_tool_log_tail` — 現セッション ActivityLog の tool_call イベントを読む(editor chat 側 tool log と同期)

### 製品仕様のキー決定(eligibility ベース分流)

- **`preview_ready`**:Python composite を生成,Test Now で明示的 smoke(自動 smoke なし)
- **`partial` / `skeleton_pre_capture`**:`Saved/UnrealMcp/TaskAtlasDrafts/` に markdown のみ
- **`blocked`**:UI Make Tool Set ボタン無効化 + tooltip で first blocked step を表示;CLI `make_composite preferDocumentOnly:true` は `Blocked` outcome + markdown のみ

### Protocol hardening(V2 で発見された stale `task_list` 汚染への B+C 修正)

- **Fix B** `UnrealMcpProtocol.cpp::HandleToolsCall`:不明 tool call を ActivityLog に書き込まない
- **Fix C** `UnrealMcpTaskAtlasService::ClassifyTask`:`DenyReason=="not_visible"` を `DenyCount` から除外

→ Editor 起動直後の最初の task が外部 stale probe ノイズで自動的に `blocked` 化されなくなった。

### 互換性

- chunk 1 以前の MCP tool surface の挙動はすべて不変
- `toolsListVersion` は `tools/list.result.structuredContent` の拡張フィールド;旧 client は無視して正常動作
- MCP `initialize.capabilities.tools.listChanged` は `false` のまま(Q server push は将来 epic;v0.32 は R+S 降格採用)
- UE 5.6 + 5.7 デュアルエンジン build pass

### 検証

| 観点 | 結果 |
|---|---|
| UE 5.6 build | ✓ 60.52s |
| UE 5.7 build | ✓ 25.73s |
| Automation | ✓ 63/63(baseline 49 + new 14) |
| validators | ✓ toolCount=190 / mirrorToolCount=190 / issueCount=0 |
| `check_ue56_compat.py` | ✓ 0 errors / 0 warnings |
| canonical vs mirror tools.json | byte-equivalent |
| 実 dogfood | ✓ CLI inject chat → editor リアルタイム表示 → make_composite → user.atlas_* replay 7 step → delete → RAG promote → knowledge_search ヒット |

---

## Branch / Tag History

Stable: `v0.32.0` (`main` after fast-forward from `experiment/post-4028be5`, 10 commits ahead of `4028be5`).

Previous stable: [`v0.31.0`](https://github.com/edwinmeng163-oss/UEAtelier/releases/tag/v0.31.0) (2026-05-31).
