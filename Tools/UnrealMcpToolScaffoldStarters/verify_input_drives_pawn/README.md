# verify_input_drives_pawn MCP Tool Scaffold Starter

## English

`unreal.simulation.verify_input_drives_pawn` starts or uses Play In Editor,
injects one axis input into the local player controller, ticks for a bounded
duration, and reports whether the possessed pawn moved or rotated enough to
meet the expected threshold.

It exists so Chat assistants can verify FPS bring-up functionally before
claiming success. A Blueprint graph that compiles can still fail if movement
input lacks a world direction vector, yaw is not controller-driven, or the
wrong pawn is possessed.

### Install

1. `git pull` to get this starter.
2. `cp -R Tools/UnrealMcpToolScaffoldStarters/verify_input_drives_pawn Tools/UnrealMcpToolScaffolds/`
3. Open Unreal Editor with the UnrealMcp plugin loaded.
4. In Chat panel, ask: `Apply scaffold verify_input_drives_pawn`.
5. After apply succeeds, call `unreal.simulation.verify_input_drives_pawn`.

### Arguments

| Argument | Required | Default | Notes |
| --- | --- | --- | --- |
| `pawnClass` | yes |  | Pawn class path, Blueprint class path, Blueprint asset path, or native class path. |
| `inputAxis` | yes | `MoveForward` | Axis to inject; prefixed axis names are accepted. |
| `axisValue` | yes | `1.0` | Axis value to inject. |
| `durationSeconds` | no | `0.5` | PIE tick duration after injection. |
| `expectedMotionDelta` | no | `0` | Minimum movement delta in cm; `<=0` disables motion threshold. |
| `expectedYawDelta` | no | `0` | Minimum yaw or pitch delta in degrees; `<=0` disables rotation threshold. |
| `startPieIfNeeded` | no | `true` | Start PIE if no session is active. |

### Known Limitations

- The verifier uses classic key/axis injection. Enhanced Input projects may
  require a future verifier variant.
- If the tool starts PIE itself, it requests PIE shutdown before returning.
- It validates the possessed pawn; it does not create fixtures or maps.

## 中文

`unreal.simulation.verify_input_drives_pawn` 会启动或复用 PIE，把一个 axis
input 注入本地 PlayerController，在限定时间内 tick，然后报告 possessed pawn
是否产生了足够的移动或旋转。

它的目标是让 Chat 在报告 FPS 已可玩之前进行功能验证。Blueprint 即使 compile
成功，也可能因为 movement input 缺少 world direction vector、yaw 没有由
controller 驱动，或 possessed pawn 不正确而失败。

安装步骤：

1. `git pull` 获取 starter。
2. `cp -R Tools/UnrealMcpToolScaffoldStarters/verify_input_drives_pawn Tools/UnrealMcpToolScaffolds/`
3. 打开已加载 UnrealMcp plugin 的 Unreal Editor。
4. 在 Chat panel 中请求：`Apply scaffold verify_input_drives_pawn`。
5. apply 成功后调用 `unreal.simulation.verify_input_drives_pawn`。

已知限制：当前 verifier 使用 classic key/axis injection；如果工具自行启动
PIE，会在返回前请求关闭 PIE；它只验证 possessed pawn，不创建测试地图或 fixture。

## 日本語

`unreal.simulation.verify_input_drives_pawn` は PIE を開始または再利用し、
local PlayerController に axis input を注入して、一定時間 tick した後、
possessed pawn が期待値以上に移動または回転したかを報告します。

Chat が FPS bring-up の成功を報告する前に、機能面の確認を行うための
starter です。Blueprint が compile できても、movement input に world
direction vector がない、yaw が controller-driven ではない、possessed pawn
が違う、といった理由で失敗することがあります。

インストール手順:

1. `git pull` で starter を取得します。
2. `cp -R Tools/UnrealMcpToolScaffoldStarters/verify_input_drives_pawn Tools/UnrealMcpToolScaffolds/`
3. UnrealMcp plugin をロードした Unreal Editor を開きます。
4. Chat panel で `Apply scaffold verify_input_drives_pawn` と依頼します。
5. apply 成功後、`unreal.simulation.verify_input_drives_pawn` を実行します。

既知の制限: 現在は classic key/axis injection を使います。ツール自身が PIE
を開始した場合、戻る前に PIE shutdown を要求します。possessed pawn を検証する
だけで、fixture や map は作成しません。
