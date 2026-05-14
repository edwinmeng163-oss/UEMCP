# fps_bootstrap MCP Tool Scaffold Starter

## English

`unreal.fps.bootstrap` performs a complete FPS character bring-up. It creates or
updates classic axis mappings, configures the Pawn, Camera, CharacterMovement,
Capsule, GameMode, WorldSettings, and PlayerStart, then wires the Pawn Blueprint
EventGraph so input axes drive movement and look controls.

This exists because the removed `unreal.configure_fps_settings` tool only tuned
the chassis. It did not wire `AddMovementInput.WorldDirection` from
`GetActorForwardVector` / `GetActorRightVector`, did not guarantee
`bUseControllerRotationYaw`, and left Chat assistants to assemble fragile
Blueprint graphs from many low-level calls.

### Install

1. `git pull` to get this starter.
2. `cp -R Tools/UnrealMcpToolScaffoldStarters/fps_bootstrap Tools/UnrealMcpToolScaffolds/`
3. Open Unreal Editor with the UnrealMcp plugin loaded.
4. In Chat panel, ask: `Apply scaffold fps_bootstrap`.
5. After apply succeeds, call `unreal.fps.bootstrap` with
   `pawnBlueprintPath` and `gameModeBlueprintPath`.

### Arguments

| Argument | Required | Default | Notes |
| --- | --- | --- | --- |
| `pawnBlueprintPath` | yes |  | Pawn or Character Blueprint asset path. |
| `gameModeBlueprintPath` | yes |  | GameMode Blueprint asset path. |
| `playerStartLabel` | no | `FPS_PlayerStart` | PlayerStart actor label to locate or create. |
| `x`, `y`, `z` | no | `0`, `0`, `190` | PlayerStart location. |
| `pitch`, `yaw`, `roll` | no | `0`, `0`, `0` | PlayerStart rotation. |
| `cameraFov` | no | `90` | Camera field of view. |
| `cameraHeight` | no | `64` | Camera relative Z. |
| `walkSpeed` | no | `650` | Character movement speed. |
| `acceleration` | no | `4096` | Character acceleration. |
| `deceleration` | no | `4096` | Walking braking deceleration. |
| `capsuleRadius` | no | `42` | Character capsule radius. |
| `capsuleHalfHeight` | no | `96` | Character capsule half-height. |
| `axisPrefix` | no | empty | Prefix for axis names, e.g. `FPS_`. |
| `runVerifyAfter` | no | `true` | Invokes the verifier when installed. |
| `dryRun` | no | `false` | Preview without mutations. |
| `compileSave` | no | `true` | Compile and save touched Blueprints. |

### Known Limitations

- This starter targets classic input axis mappings, not Enhanced Input assets.
- Applying this scaffold still requires a C++ build and editor restart before
  the new tool is visible.
- Runtime verification depends on `verify_input_drives_pawn` being installed.

## 中文

`unreal.fps.bootstrap` 负责完整的 FPS 角色初始化。它会创建或更新 classic
axis mappings，配置 Pawn、Camera、CharacterMovement、Capsule、GameMode、
WorldSettings 和 PlayerStart，并把 Pawn Blueprint 的 EventGraph wiring 到
movement / look 输入。

这个 starter 用来替代已移除的 `unreal.configure_fps_settings`。旧工具只设置
外壳参数，没有把 `AddMovementInput.WorldDirection` 接到
`GetActorForwardVector` / `GetActorRightVector`，也没有保证
`bUseControllerRotationYaw`，导致 Chat 需要用大量低层 Blueprint 调用拼装图。

安装步骤：

1. `git pull` 获取 starter。
2. `cp -R Tools/UnrealMcpToolScaffoldStarters/fps_bootstrap Tools/UnrealMcpToolScaffolds/`
3. 打开已加载 UnrealMcp plugin 的 Unreal Editor。
4. 在 Chat panel 中请求：`Apply scaffold fps_bootstrap`。
5. apply 成功后，用 `pawnBlueprintPath` 和 `gameModeBlueprintPath` 调用
   `unreal.fps.bootstrap`。

已知限制：当前 starter 使用 classic input axis mappings，不创建 Enhanced
Input assets；apply 后仍需 C++ build 和 editor restart；runtime verification
需要先安装 `verify_input_drives_pawn`。

## 日本語

`unreal.fps.bootstrap` は FPS キャラクターの完全な bring-up を行います。
classic axis mappings を作成または更新し、Pawn、Camera、
CharacterMovement、Capsule、GameMode、WorldSettings、PlayerStart を設定し、
Pawn Blueprint の EventGraph を movement / look 入力へ配線します。

この starter は、削除された `unreal.configure_fps_settings` の置き換えです。
旧ツールは chassis 設定だけで、`AddMovementInput.WorldDirection` を
`GetActorForwardVector` / `GetActorRightVector` へ接続せず、
`bUseControllerRotationYaw` も保証しませんでした。

インストール手順:

1. `git pull` で starter を取得します。
2. `cp -R Tools/UnrealMcpToolScaffoldStarters/fps_bootstrap Tools/UnrealMcpToolScaffolds/`
3. UnrealMcp plugin をロードした Unreal Editor を開きます。
4. Chat panel で `Apply scaffold fps_bootstrap` と依頼します。
5. apply 成功後、`pawnBlueprintPath` と `gameModeBlueprintPath` を指定して
   `unreal.fps.bootstrap` を実行します。

既知の制限: classic input axis mappings を対象にしており Enhanced Input
assets は作成しません。apply 後は C++ build と editor restart が必要です。
runtime verification には `verify_input_drives_pawn` のインストールが必要です。
