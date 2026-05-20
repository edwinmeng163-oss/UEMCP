# Packaging And Build Agent Guide

Read this when work touches UBT builds, release zips, install/deployment,
Windows packaging, verifier failures, or cross-platform build pitfalls. Deep
references are [BuildAndPackagingPitfalls](../BuildAndPackagingPitfalls.md),
[WindowsPackaging](../WindowsPackaging.md),
[Stage2WindowsVerify](../Stage2WindowsVerify.md), and
[DeploymentTroubleshooting](../DeploymentTroubleshooting.md).

## Build Commands

macOS UE 5.7 build example:

```bash
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
  UEvolveEditor Mac Development \
  -Project="/Users/wmbt7052/Documents/Unreal Projects/MyProject/UEvolve.uproject" \
  -WaitMutex
```

Windows UE 5.7 build example:

```powershell
& "E:\3D_SOFTWARE\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
  UnrealEditor Win64 Development `
  "-Project=E:\UE5_P\EasyMapper5_7\EasyMapper5_7.uproject" `
  -WaitMutex
```

Before compiling, close Unreal Editor or disable Live Coding. If Editor is
open, macOS/Windows builds may fail due to locked binaries or Live Coding
locks.

## Pre-commit And CI Checks

```bash
python3 Tools/validate_tool_registry.py
python3 Tools/check_ue56_compat.py
python3 Tools/verify_package_integrity.py --root . --mode source --repo-root .
```

Run the RAG eval when docs, recommendation, or knowledge indexing changed:

```text
/tool unreal.knowledge_eval_run {"evalPath":"Tools/UnrealMcpKnowledge/Evals","includeDetails":false}
```

If ToolRegistry changes, also keep the plugin resource registry mirror
byte-for-byte identical.

## Install And Deployment

The plugin is normally installed into a target project as a project-level
plugin.

Automated installer:

```bash
python3 Tools/install_unrealmcp_to_project.py --project "/path/to/YourProject/YourProject.uproject" --dry-run
python3 Tools/install_unrealmcp_to_project.py --project "/path/to/YourProject/YourProject.uproject"
```

Manual install copies:

```text
Plugins/UnrealMcp -> <TargetProject>/Plugins/UnrealMcp
Tools             -> <TargetProject>/Tools
Schemas           -> <TargetProject>/Schemas
Docs              -> <TargetProject>/Docs
```

Then enable `UnrealMcp` and `PythonScriptPlugin` in the target `.uproject`,
close Unreal Editor, build the project, and open the project. The MCP endpoint
exists only while Unreal Editor is open and the plugin is loaded.

Avoid having both project-level and engine-level copies of `UnrealMcp`; stale
or locked binaries cause confusing Windows failures.

## Pitfalls Index

Read [BuildAndPackagingPitfalls](../BuildAndPackagingPitfalls.md) before
authoring a new release chunk. It indexes:

1. Unity-build symbol collisions.
2. UE 5.6 dev-host and UE 5.7 example-host verification.
3. Build target name traps.
4. Stale plugin dylib shadow.
5. Cross-platform zip path separator handling.
6. Scaffold source path fallback.
7. Codex dispatch CLI hygiene.
8. Hermes coordinator hygiene.
9. End-to-end verification at integration boundaries.
10. Codex spec deviation policy.
11. Editor-load warnings versus UBT errors.
12. Mac-only build verify missing MSVC-promoted-to-error warnings.

Unity-build hygiene rule: the UnrealMcp module has `bUseUnity = false` because
per-file anonymous namespaces with same-named helpers collided under UBT unity
build. Do not define generic cross-file helpers in anonymous namespaces. Put
cross-file helpers at `namespace UnrealMcp` scope in exactly one file and
forward-declare them at the same scope.

## Windows Packaging

Windows source-only zips are produced automatically by:

```text
.github/workflows/win-release-package.yml
```

on every `v*.*.*` tag push and attached to the matching GitHub release. The
workflow runs on a `windows-2022` GitHub Actions runner so the zip retains
PowerShell `Compress-Archive` backslash entry paths. This is the canonical
Windows-tested artifact shape.

Manual fallback is documented in [WindowsPackaging](../WindowsPackaging.md).
The Windows collaborator should:

1. Sync to the release tag.
2. Clean stale plugin/example binaries and intermediates.
3. Build `MyProjectEditor` against the matching example host.
4. Run registry and UE 5.6 compatibility validators.
5. Run `Tools/package_plugin.ps1`.
6. Verify with `Tools/verify_package_integrity.py`.
7. Upload the zip and `.sha256` sidecar to the existing release.

Do not repackage a Windows-tested zip on macOS just to satisfy a local verifier.
Fix the verifier or verification context instead.

## Stage 2 Mac Projectroot Zip E2E

Every projectroot zip must be e2e-tested before tag-publish:

1. Create a fresh test project at `/tmp/UEvolveMacZipTest` from `TP_Blank`.
2. Extract the zip at project root, not under `Plugins/`.
3. UBT-build the editor module against the local UE 5.7 install.
4. Launch editor with `-nullrhi -unattended`; poll for `LogUnrealMcp:`
   listening and `Engine is initialized`.
5. Confirm port 8765 is bound.
6. Run smoke calls for `tools/list`,
   `unreal.editor.python_runtime_info`, and
   `unreal.mcp_apply_scaffold` dry run for `unreal.fps.bootstrap`.
7. Kill the editor, wait for port 8765 to free, and remove the temp project and
   log.

If the scaffold dry run fails on a missing scaffold file, suspect a packager
gap before opening an applier bug.

## Stale Plugin-level Binary Trap

After plugin code changes and before any example-host smoke, remove stale
plugin-level binaries:

```bash
rm -rf Plugins/UnrealMcp/Binaries Plugins/UnrealMcp/Intermediate
```

Then rebuild the example host. Fresh example-host binaries should live under
the example host `Binaries/` directory. If the plugin-level dylib/dll still
exists, repeat cleanup.

## Projectroot Zip Overlay Invariants

Every Mac source-only or Windows full-experience projectroot zip must ship:

```text
<UserProject>/Plugins/UnrealMcp/
<UserProject>/Tools/UnrealMcpToolRegistry/
<UserProject>/Tools/UnrealMcpPyTools/
<UserProject>/Tools/UnrealMcpToolScaffoldStarters/
<UserProject>/Tools/UnrealMcpToolScaffolds/
  fps_bootstrap/
  verify_input_drives_pawn/
<UserProject>/Tools/UnrealMcpSkills/
<UserProject>/Tools/UnrealMcpKnowledge/
<UserProject>/Tools/UnrealMcpTests/
<UserProject>/Tools/UnrealMcpCodexBridge/
<UserProject>/Docs/FIRST_LAUNCH.md
```

`UnrealMcpToolScaffoldStarters` and `UnrealMcpToolScaffolds` are not the same.
Starters are templates cloned by `unreal.scaffold_mcp_tool`; scaffolds are the
pre-staged working copies read by `unreal.mcp_apply_scaffold`. Both must
coexist.

`Tools/package_plugin.sh` and `Tools/package_plugin.ps1` enforce these
invariants. If a new top-level overlay subtree is added, add both the copy step
and the matching assertion in both packagers.

## Release Publish Flow

After a tag-moving fix:

```bash
git tag -d <tag> && git tag <tag> <newSha>
git push origin <branch>
git push --force origin <tag>
bash Tools/package_plugin.sh --version <ver> --engine-tag ue56-ue57
gh release delete-asset <tag> <old-asset-name> --yes --repo <repo>
gh release delete-asset <tag> <old-asset-name>.sha256 --yes --repo <repo>
gh release upload <tag> <new-zip> <new-zip>.sha256 --repo <repo>
gh release edit <tag> --notes-file <updated-body.md> --repo <repo>
```

Always update both the filename and SHA in release notes.
