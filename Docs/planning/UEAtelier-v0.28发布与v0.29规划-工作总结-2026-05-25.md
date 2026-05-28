# UEAtelier 工作总结 —— v0.28 发布闭环 + v0.29 规划

> 文档日期:2026-05-25
> 项目:UEAtelier(Unreal Editor MCP 自扩展工作台插件)
> 仓库:`github.com/edwinmeng163-oss/UEAtelier`
> 本地根:`/Users/wmbt7052/Documents/Unreal Projects/MyProject`
> 覆盖范围:本轮对话从「发布前审计」到「v0.28.0 发布闭环」再到「v0.29 方案敲定」的完整工作

---

## 0. 一句话总览

本轮把 **v0.28.0** 从「这个版本不能发布」一路推到 **正式发布闭环**:补齐了 AI 端到端造可玩第三人称角色所需的**核心蓝图创作层 + 运行时验证层**(工具数 **164 → 174**),实测 AI **仅用可见核心工具(无 `execute_python`)** 造出能走动的角色、`pie_input_probe` 量得前进约 **126cm**;双引擎 UE 5.6 + 5.7 验证通过;三语(中/EN/日)release notes + README 全量更新;一个无法稳定修复的历史测试失败被记为已知并开了 GitHub issue。发布后探索了 Task Atlas「Make Tool」的真实行为,敲定了 **v0.29「组合工具 / 流水线工具」** 的方向与用户的方案调整,并把所有上下文写进 Hermes 永久记忆,留给全新 session 接手。

---

## 1. 项目与协作角色背景

### 1.1 产品
- **UEAtelier** 是一个 Unreal Editor 的 MCP(Model Context Protocol)自扩展工作台插件,主交付物在 `Plugins/UnrealMcp`。
- 目标:让 AI 通过可见的核心 `unreal.*` 工具在 Unreal 编辑器里端到端完成真实工作(造角色、搭关卡、做 UI 等),并能自我扩展(Python user 工具轨道)。
- 双引擎:UE 5.6.1 + UE 5.7.4,单一源码树同时支持 macOS + Windows。

### 1.2 三角色协作模型
- **Claude(我,PM / Code Reviewer)** —— 定方向、写任务 prompt、审 diff、提交/打 tag/推送、对接 GitHub、与人沟通。**从不直接写代码,从不直接跑 `codex-agent` CLI。**
- **Hermes(协调者)** —— 接 PM 任务、跑 shell + 文件操作、派发 codex-agent、做 ≤5 行机械小修、跑 validators + 构建 + 冒烟,返回结构化 PM 报告。**从不 commit/push/tag。**
- **Codex(实现者,gpt-5.5 -r xhigh)** —— 重型代码生成、多文件实现。**从不 commit,只留工作区 diff 给 PM。**

### 1.3 硬约束(全程生效)
- 对话保持**中文**(用户强调:"和我对话保持中文,保持!!!!")。
- PM 独占 git stage/commit/tag/push/release + 破坏性操作(rm-rf / git restore);选择性 stage(绝不 `git add -A`);绝不跳过 hooks(`--no-verify`/`--no-gpg-sign`)除非明确要求;绝不改 git config;绝不 force-push main。
- 构建前编辑器必须**关闭**(验证 `pgrep -x UnrealEditor`);跨引擎切换前 `rm -rf Plugins/UnrealMcp/{Intermediate,Binaries}`(PM 做,Hermes 会拦)。
- 永不删除 FYPDN actor / `__ExternalActors__`/TopDown 目录;tracked `.umap` 用 `git restore`。
- `EAiProviderKind` append-only;引擎版本 shim 只放 `UnrealMcpEngineCompat.h`。
- v0.27 起:核心 C++ 工具面对 AI 隔离封闭(self-extension = Python user 轨道)。

---

## 2. 起点 —— 为什么「这个版本不能发布」

进入本轮时,v0.28 的雏形已经在做,但用户给出明确判断:**在补齐核心工具(蓝图编辑 + UI)之前不能发布**。核心缺口是:AI 还不能纯用可见核心工具端到端造出一个**真正可玩**的第三人称角色 —— 缺输入事件节点、缺蓝图组件挂载、缺类默认/关卡 GameMode/possess 设置、缺 gameplay 图节点,更缺**运行时证明**(造完到底能不能走)。

---

## 3. 关键决策时间线(用户每一次拍板)

| # | 用户决策 | 含义 |
|---|---------|------|
| 1 | "之前的 `bp_add_event_node` 类工具去哪了" → 审计 | 触发 git 取证,发现历史工具被降级 + 一个未接线的潜伏库 |
| 2 | 新增独立输入工具 + 一个枚举 gameplay 工具 + **BP+UI 一次做完再发** | 定了 v0.28 的工具形态与发布门槛 |
| 3 | "补运行时验证层" | 加 `pie_input_probe` + `verify_viewport_widgets` |
| 4 | RunRecoversStale:"顺手修了再发" → 修两轮没绿 → **"记为已知 + 回退两轮改"** | 历史失败不阻塞发布,回退保持 v0.28 干净 |
| 5 | "记得把这个没法修的 bug 发 issue" | → GitHub issue #3 |
| 6 | 发布:"分阶段,先 commit + 双引擎 gate" → "现在全走阶段 2" | 先锁代码再走完整发布 |
| 7 | 三语 release notes + 完整 README 更新,"交给 Hermes 来做你来验收" | Hermes 执行,PM 验收 |
| 8 | "确认扩展工具是否还能写 C++ 工具,非核心" | 确认 self-extension 现状 |
| 9 | C++ scaffold pipeline "暂时保留,做 5.8 的时候可能会加回来" | 该轨道 parked,留给 UE 5.8 |
| 10 | Task Atlas Make Tool 行为确认 → "做真正的流水线式组合工具" | 定了 v0.29 方向 |
| 11(最新)| "fresh session + 方案调整:组合流程替换加入 skills 键;Make Tool 不变可做 C++/Python 通用工具但不进核心、保持隔离" | v0.29 的最终形态 + 留给新 session |

---

## 4. v0.28 技术实现

### 4.1 主题
**core Blueprint authoring + playable-character closure + runtime verification** —— AI 现在纯用可见核心 `unreal.*` 工具(**无 `execute_python`**)端到端造出可玩第三人称角色,并由运行时探针证明它真的能走。

### 4.2 一个关键的「复用」发现
审计旧工具时,通过 git 取证发现:
- 旧的 `unreal.bp_add_input_axis_event_node` 工具在 v0.11.x 被提升(commit `73a46a5`)后又降级(commit `d4962a9`)。
- 源码里**潜伏着一个完整但未接线的 `UUnrealMcpBlueprintGraphLibrary`**(BlueprintCallable,9 个 helper:AddCallFunctionNode、AddMovementInputCallNode、AddControllerYaw/PitchInputCallNode、AddInputAxisEventNode、AddGetActorForward/RightVectorNode、ConnectPinsByGuid、CompileBlueprint)。

→ v0.28 没有从零写,而是**把这个潜伏库包装成工具**,这是本版最大的省力点。

### 4.3 10 个新核心工具(164 → 174),按 5 类

**① 输入事件(Legacy K2Node)**
- `bp_add_input_axis_event_node`
- `bp_add_input_action_event_node`

**② 蓝图 SCS 组件**
- `bp_add_component`(SpringArm/Camera/任意 SceneComponent,走 USCS_Node)
- `bp_set_component_property`(带 property allowlist)

**③ 类默认 / 关卡 / possess**
- `bp_set_class_default`
- `editor_set_map_game_mode`
- `actor_set_auto_possess`

**④ gameplay 图节点(单一枚举工具)**
- `bp_add_gameplay_node`,枚举值:`AddMovementInput` / `AddControllerYawInput` / `AddControllerPitchInput` / `GetActorForwardVector` / `GetActorRightVector` / `GetControlRotation` / `Jump` / `FloatGreaterThan`

**⑤ 运行时验证**
- `pie_input_probe`(PIE 中异步注入输入 + 量测 pawn 位移,**FTSTicker 驱动、绝不从 handler 泵编辑器 tick**)
- `verify_viewport_widgets`(只读检查 viewport widget)

### 4.4 配套改动
- 扩展 `configure_player_input`:接受**任意 legacy 轴/动作映射名**(原来只支持固定的 MoveForward/MoveRight/LookYaw/LookPitch/Jump)。这是 dogfood 时发现 Turn/LookUp 不支持后补的(task #79)。
- 修复 `bp_set_pin_default` 对 object/class 默认引脚的 postcheck 误报。
- 新增 `UnrealMcpBlueprintComponentLibrary.{cpp,h}`(codex 实现):PIE-active 守卫、property allowlist(标量 + Vector/Rotator/Transform/Color)、仅直接属性、CPF_Edit 检查、FScopedTransaction、ImportText + 回读校验。
- 并入 **v0.27.1 的 `verify_player_controls` 崩溃修复**(不再从 handler 重入编辑器 tick —— 这正是 `pie_input_probe` 必须用 FTSTicker 的同一条教训)。

### 4.5 工程纪律
- pie_input_probe 的 `injectionMethod = input_key_event_args`(带 direct fallback)。
- 双引擎 UE 5.6 + 5.7 构建均通过;automation **82/84**(只剩两个已知:RunRecoversStale + MapValidation)。
- BP wave + runtime-verify wave 各为**一个内聚的 codex job、串行**——因为并行 codex 共享同一工作树,碰到 registrar/tools.json/dispatcher 等共享文件无法并行。

---

## 5. Dogfood 验证 —— 证明 AI 真能造出可玩角色

用户在插件里让 AI 实测(不是我写的代码,是 AI 用工具跑出来的):

- **Part A(造角色 + possession + 绑定图)**:character/possession/bindings 都建好。第一次跑在第 5 步「输入映射」卡住,因为 `configure_player_input` 当时只支持固定名 —— 修正 dogfood 用 LookYaw/LookPitch,并开 task #79 扩展到任意名(本版已做)。
- **Part B(HUD)**:HUD 建好,CreateWidget/AddToViewport 通过 `bp_add_call_function_node` 接线。
- **运行时探针结果**:
  - `moveForward`:`moved=true`,位移 **126.69cm**,`injectionMethod=input_key_event_args`(**输入层强证明**)。
  - `jump`:`direct_pawn_fallback`(InputAction 注入目前是**弱证明**,退回直接调用)。
  - `verify_viewport_widgets`:`count=0`。

→ 核心结论坐实:**AI 仅用可见核心工具(无 execute_python)造出可玩第三人称角色,并量化证明它能走。**

---

## 6. RunRecoversStale 的取舍 → GitHub issue #3

- `UnrealMcp.AutomationTools.RunRecoversStale` 自 v0.20(commit `39d21ca`)起**确定性失败**。
- 根因是测试隔离:`ResetAutomationToolStateForTests` 没删盘上的 run 目录 → 上一次 run 的文件污染 `TryLoadActiveRunFromDisk`。
- **两轮 codex 修复**(① 盘路径 continue;② reset 删 `GetAutomationRunRoot` 目录)看起来都对,但测试**仍然 FAIL**——典型的「静态看着过、运行时挂」悖论,根因是需要 UE_LOG 插桩才能钉死的微妙运行时/路径交互。
- 用户决策:**记为已知(和 MapValidation 同级)+ 回退两轮改**(`git restore` 保持 v0.28 干净)+ 开 issue。
- → **GitHub issue #3**:`https://github.com/edwinmeng163-oss/UEAtelier/issues/3`(插桩修复列为未来 follow-up)。

---

## 7. v0.28.0 发布闭环

| 项目 | 结果 |
|------|------|
| 发布 commit | `e46bbf2`(31 文件,+4117/−151),折叠了 v0.27.1 的 3 个 commit |
| Tag | `v0.28.0` → `e46bbf2` |
| README commit | `1fa4f74`(三语 README 更新,**当前 HEAD**) |
| GitHub release | latest、非 draft、三语 body |
| Assets(4 个) | Mac zip + sha(PM 做)、Win zip + sha(CI `win-release-package.yml` 自动做) |
| 插件版本 | `VersionName 0.27.1 → 0.28.0`,Version 41 → 42 |
| 工具数 | **174**(AGENTS.md / README 三处计数均同步) |
| 双引擎 | UE 5.6 + 5.7 |
| automation | 82/84(仅 RunRecoversStale + MapValidation 已知) |
| 本地 v0.27.1 tag | 已退役(未推送,内容随 v0.28 发布) |

**Win zip 的关键澄清**:用户纠正了我一度想 brew 装 pwsh 的计划 —— CI(`win-release-package.yml`)在 tag push 时于 windows-2022 runner 自动跑 `package_plugin.ps1` 并 `gh release upload --clobber`,**Win zip 是自动产出的**,Mac 这边不需要 pwsh。

---

## 8. 三语文档任务(Hermes 执行,PM 验收)

### 8.1 GitHub release notes(中 / EN / 日 三语对等)
- 我第一版只写了中 + EN,**漏了日语**;用户纠正后要求三语对等 + README 全量更新。
- Hermes 补全日语段,经 `gh release edit v0.28.0 --notes-file` 应用;`gh release view` 确认 body 三语齐 + 4 assets 仍在。
- 每段都涵盖:能力概述 + 10 新工具(5 类)+ `configure_player_input` 扩展 + `bp_set_pin_default` 修复 + 复用 GraphLibrary/新 ComponentLibrary + 并入 v0.27.1 崩溃修复 + 164→174 + 实测 126cm + 双引擎 + Install + Known limitations。

### 8.2 README.md 三语更新(commit `1fa4f74`)
- 三处计数确认 **174**(EN/中/日各一处)。
- 各语 capabilities 段加入 v0.28 能力:核心蓝图创作层、AI 端到端造可玩角色(无 execute_python)、运行时验证。
- 工具清单补上 10 个新工具名;latest-release 链接 0.26.0 → 0.28.0;VersionName 0.28.0。

---

## 9. 发布后探索 —— Task Atlas「Make Tool」的真实行为

用户问:"在 Task Atlas 里对任意流程点 Make Tool,是不是把整个工具和流程组合做成一个流水线式组合工具?"

我读 `STaskAtlasWindow.cpp:657-708`(`HandleMakeToolClicked`)确认:**当前不是**。现状是:
- 构造 `scaffold_mcp_tool` 参数(toolName / title / description 里塞 "Critical path: <tools>" / `argumentSchemaJson` 是空的 `{"properties":{},"required":[]}` / implementationNotes / category=self-extension),调 `unreal.scaffold_mcp_tool`。
- 提示文案还是 v0.27-stale 的 "Open it, edit the handler, then run mcp_extension_pipeline"。

→ 它只是「生成一个空骨架的 Python user 工具」,**不是真正的流水线组合工具**。用户因此决定:做一个真正的。

同时确认了两个可行性事实(2026-05-24 设计探针):
- **(a) 任务数据没有 per-step args** —— `criticalPath` 是去重的工具名列表(`AddUnique(Event.ToolName)`),`eventRefs` 只记 `{ts, tool, isError}`,ActivityLog 记 `eventKind/summary/payload` 但**不记每次工具调用的参数** → MVP 只能产出带 `argumentsJson` 占位符的 workflow steps **骨架**。
- **(b) Python user 工具当前不能调 MCP 工具** —— 样例只用 `unreal` Python API,user-tool runtime 没有 `call_tool`/`workflow_run` host callable。

并顺带确认:self-extension 现在**只能写 Python user 工具**;C++ scaffold pipeline(`mcp_apply_scaffold` + `mcp_extension_pipeline`)是 `legacy_hidden` + 开发者手动 + deferred(v0.27 的 wall-off),代码**完整保留未删**,用户决定 parked 到 UE 5.8 再看。

---

## 10. v0.29 方向 + 用户最终方案调整(本轮终点)

### 10.1 方向
**user-tool「call-tool」能力 → Task Atlas「流水线 / 组合工具」**。用户选了 **option B**:**先**加 user-tool call-tool 能力(host callable,让 Python user 工具能编排核心 MCP 工具 / `workflow_run`,带安全门 + 重入处理),**再**做命名 user 工具组合 + 升级 Make Tool。

### 10.2 用户最终方案调整(已写入 Hermes 记忆)
> 原话:"frensh session,同时设计方案调整,现在的制作工具组合流程替换原本的加入 skills 按键,make tools 功能不变可以制作 c++ 或者 python 通用工具,但是不能进入核心工具保持隔离使用"

拆成三条:
1. **新组合 / 流水线流程替换「加入 skills」键** —— 即 Task Atlas 里 `[→ Skills]` 按钮(现调 `skill_distill_from_activity`)改造成组合工具流程。**不新增按钮、不动 `[Make Tool]`。**
2. **`[Make Tool]` 功能不变** —— 但要能产出 **C++ 或 Python 通用工具**。
3. **隔离** —— Make Tool 产出的工具**不进核心** `unreal.*` 工具面,保持隔离(非核心)。

### 10.3 ⚠️ 一处架构张力(新 session R0 开场必须先跟用户对齐)
调整 (2)+(3)「Make Tool 可做 **C++** 通用工具、但不进核心、保持隔离」与项目既有铁律冲突:
- **C++ 一旦编译进 UnrealMcp 插件模块,它就是核心 `unreal.*`**;当前根本没有「运行时加载的非核心 C++ 工具」轨道(非核心 = Python `user.*`,运行时加载、不编译)。
- 所以「隔离的非核心 C++ 工具」目前在架构上**不成立**。

R0 第一件事请用户在三选一里拍板:
- **(a)** 生成 C++ **源码交付**,由用户自己编进**他自己的**模块/插件(进程外,永不注册进 UnrealMcp 工具面)—— 真隔离;
- **(b)** "C++" 是口语,隔离轨道其实只走 Python `user.*`;
- **(c)** 真做一条全新的非核心 C++ 加载机制(很大,等于第二套插件模块/热加载轨道)。

### 10.4 v0.29 已记录的 R0 开放问题
- `call_tool(name,argsJson)` vs `run_workflow(stepsJson)` —— 倾向走 `workflow_run` 现成的 dry-run/risk-gate/failure-pause,而非裸 dispatch。
- 哪些工具可被调用(**visible-only**,保住 v0.27 hidden-core 边界,防止 user 工具越权摸到 hidden apply/pipeline)。
- `UnrealMcpToolDispatcher` 的**重入**(user 工具跑在一次 tool dispatch 内部又去调另一个工具 = 重入 dispatch;参照 pie_input_probe 的 editor-tick 重入教训,但这里是 tool-dispatch 重入)。
- host callable 怎么注入 execute_python 沙箱(pyImportAllowList / builtin)。
- 顺手修 Make Tool 的 v0.27-stale 文案("run mcp_extension_pipeline" → 应改成 Python user 工具 reload/smoke 路径)。

---

## 11. 待办 / Roadmap

### v0.28.x / 近期
- 强化 `pie_input_probe` 对 InputAction(jump)的注入(目前是 direct fallback 弱证明)。
- GitHub issue #3:RunRecoversStale 的插桩修复(future follow-up)。

### v0.29(全新 session,多轮 R0)
- 开场顺序固定:**① 先解 §10.3 架构张力 → ② 再做 call-tool 能力(option B,安全门 + 重入)→ ③ 最后做组合工具 + 替换 To-Skills 键 + Make Tool 升级**。
- R0 走多轮 + Hermes 二次意见(比照 Reform C 的节奏)。

### UE 5.8
- 计划以 Epic **官方 MCP 工具**为基座;插件的 self-extension(Python user 轨道)成为增值层,用于(1)工具组合/编排脚本、(2)Epic 没提供的少数工具。
- 复活 C++ self-extension 轨道(`mcp_apply_scaffold` + `mcp_extension_pipeline`,代码已 parked 保留)作为开发者手动 C++ 扩展路径的候选。

---

## 12. 过程经验教训(本轮沉淀)

1. **大分析 brief 会淹死 Hermes** —— R1 schema-lock brief 太大,Hermes 反复 context-compaction 后只打印了 session_id 就死了。有效转向:**PM 直接读源码做精确 schema 设计,Hermes 只管 shell / 派发 / 构建 / automation**,不让它"读一切再叙述 7 段设计"。保持 Hermes turn 精简。
2. **并行 codex 共享一个工作树** —— 碰共享文件(registrar/tools.json/category dispatcher)的 chunk 不能并行。v0.28 的 BP wave 和 runtime-verify wave 各是**一个内聚 job、串行**。
3. **长 codex/构建 job 用 Monitor 模式** —— Hermes 派发(短、返回 jobId)后用 Monitor 盯 codex job 日志(log-size 稳定 + tmux 存活,60 分钟上限),熬过 ~20 分钟 background-bash 被杀窗口;`touch DONE` sentinel 无论成功/崩溃都触发(静默 ≠ 成功)。
4. **跨引擎 UHT 污染** —— 切引擎前必须 `rm -rf Plugins/UnrealMcp/{Intermediate,Binaries}`(PM 做)。
5. **陈旧 dylib 遮蔽新构建** —— 改插件代码后做 example-host 冒烟前先清 `Plugins/UnrealMcp/{Binaries,Intermediate}`,否则 mount 加载旧的 plugin-level 二进制。
6. **release notes 会和 filename 漂移** —— tag 移动后要同步刷新 SHA + filename。
7. **不要修一个在用户目标平台上没坏的东西** —— Win zip 由 CI 产出,别画蛇添足。
8. **核心 C++ = 进 UnrealMcp 模块 = 核心 `unreal.*`** —— 没有"非核心 C++ 工具"这种东西(非核心 = Python `user.*`)。这条要在 v0.29 R0 跟用户的方案调整里反复对齐。

---

## 13. 关键事实速查表

| 项 | 值 |
|----|----|
| 当前 HEAD | `1fa4f74`(README 三语更新) |
| 最新 tag | `v0.28.0` → `e46bbf2` |
| 插件版本 | `VersionName 0.28.0`,Version 42 |
| 工具总数 | **174**(164 → 174,本版 +10) |
| 双引擎 | UE 5.6.1 + UE 5.7.4(Mac + Win) |
| automation | 82/84(已知:RunRecoversStale + MapValidation) |
| 实测位移 | `pie_input_probe` 前进 **126.69cm**(input_key_event_args) |
| GitHub release | v0.28.0 latest,非 draft,三语 body,4 assets |
| 已知 issue | #3 RunRecoversStale(`edwinmeng163-oss/UEAtelier#3`) |
| 项目根 | `/Users/wmbt7052/Documents/Unreal Projects/MyProject` |
| Hermes 记忆 | `~/.hermes/memories/MEMORY.md`(v0.28 已落、v0.29 候选 + 方案调整已写) |
| 下一步 | v0.29 全新 session,R0 先解架构张力 |

---

*本总结由 Claude(PM/Reviewer)整理,反映本轮对话从发布前审计到 v0.28.0 发布闭环、再到 v0.29 方案敲定的完整工作。v0.28 这一程到此结束,v0.29 在全新 session 开始。*
