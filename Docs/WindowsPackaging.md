# Windows Packaging Guide for UnrealMcp Releases

This guide tells a Windows collaborator how to produce a `UnrealMcp-vX.Y.Z-win-*.zip` and attach it to an existing GitHub release. PM does the Mac zip and creates the release; this side packages the Windows companion.

> Audience: a Windows machine that has UE 5.6 and/or UE 5.7 installed, with the UEvolve repository cloned.
>
> Current backlog (as of v0.19.0): **v0.15.1, v0.15.2, v0.16.0, v0.17.0, v0.18.0, v0.19.0** all have Mac zips published but no Windows zip. Process each one with the steps below; you can batch all 6 into a single session.

---

## 1. One-time setup

### 1.1 Required software

| Tool | Version | Why |
|------|---------|-----|
| Unreal Engine | 5.6 and/or 5.7 (latest patch) | Build the plugin binaries |
| Visual Studio | 2022 with "Game development with C++" workload | UBT toolchain |
| Git for Windows | Latest | Clone + fetch tags |
| PowerShell | 7+ (`pwsh`) — Windows PowerShell 5.1 also works | Run the packaging script |
| Python | 3.10+ | Run validators (`validate_tool_registry.py`, `verify_package_integrity.py`) |
| GitHub CLI | Latest (`gh --version`) | Upload zip to existing release |

### 1.2 First-time auth

```powershell
gh auth login
# Choose GitHub.com -> HTTPS -> authenticate browser
# Pick scopes: repo (read + write release assets)
```

Verify you can see the project's releases:

```powershell
gh release list --repo edwinmeng163-oss/UEvolve --limit 5
```

### 1.3 Clone (only once)

```powershell
cd C:\src        # or wherever you keep code
git clone https://github.com/edwinmeng163-oss/UEvolve.git
cd UEvolve
```

If you already have it, just `git fetch --tags origin`.

---

## 2. Per-version workflow

For each version that needs a Win zip, repeat sections 2.1 → 2.5.

### 2.1 Sync to the version tag

```powershell
git fetch --tags origin
git checkout v0.19.0          # replace with the version you're packaging
```

Verify:

```powershell
# Should print "0.19.0"
Select-String -Path Plugins\UnrealMcp\UnrealMcp.uplugin -Pattern '"VersionName"'
```

### 2.2 Clean any stale build artifacts

```powershell
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Plugins\UnrealMcp\Binaries
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Plugins\UnrealMcp\Intermediate
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Examples\UEvolveExample57\Binaries
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Examples\UEvolveExample57\Intermediate
```

This prevents an older dylib from shadowing fresh UBT output (the Mac side has hit this exact trap multiple times).

### 2.3 Build the plugin against the example host

Use the example host that matches the engine you have installed. Replace `UE_5.7` with `UE_5.6` if you're packaging the UE 5.6 build.

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
    MyProjectEditor `
    Win64 `
    Development `
    -project="$(Resolve-Path .\Examples\UEvolveExample57\UEvolveExample57.uproject)" `
    -WaitMutex
```

Expected: `Build successful` at the end. Time: 3-10 minutes the first build, ~30s incremental.

**Important:** the build target is `MyProjectEditor`, NOT `UEvolveExample57Editor`. The folder name is `UEvolveExample57` but the internal module + target name is `MyProject*` (this trips up first-time packagers).

After build, verify the dll exists:

```powershell
Get-Item Examples\UEvolveExample57\Binaries\Win64\UnrealEditor-UnrealMcp.dll
Get-Item Examples\UEvolveExample57\Binaries\Win64\UnrealEditor.modules
```

If you're packaging the **full-experience** zip (with prebuilt binaries bundled), you need both files in a directory that the script can scan — see section 2.5.

### 2.4 Run validators (sanity check before packaging)

```powershell
python Tools\validate_tool_registry.py
python Tools\check_ue56_compat.py
```

Expected output:
- `validate_tool_registry.py` ends with `issueCount: 0` and `toolCount: <NNN>` matching the version (e.g. 149 for v0.16.0, 154 for v0.17.0/v0.18.0, 155 for v0.19.0)
- `check_ue56_compat.py` ends with `0 errors, 0 warnings`

If either fails, **stop** and report back — the tag should have been clean.

### 2.5 Package the zip

There are two flavors:

#### Flavor A: source-only (recommended for parity with Mac releases)

This matches the current Mac zip — pure source, the Win user builds locally on first launch. **This is what we ship now.**

```powershell
pwsh -File Tools\package_plugin.ps1 -Version 0.19.0
```

Output:
```
Zip path: Saved\UnrealMcp\Packages\UnrealMcp-v0.19.0-win-ue56-ue57-projectroot.zip
Zip size: ~1 MiB
SHA-256: <hash>
```

#### Flavor B: full-experience (with prebuilt Win64 binaries)

This bundles the freshly built `UnrealEditor-UnrealMcp.dll` so end-users don't need to compile. Use this only when explicitly requested — it makes the zip much larger and Mac users won't get a parallel artifact.

```powershell
pwsh -File Tools\package_plugin.ps1 `
    -Version 0.19.0 `
    -FullExperience `
    -PrebuiltBinariesPath (Resolve-Path .\Examples\UEvolveExample57) `
    -EngineTag ue57
```

For UE 5.6 binaries, change `-EngineTag ue56` and use a UE 5.6 build.

### 2.6 Verify the produced zip

```powershell
python Tools\verify_package_integrity.py `
    --zip Saved\UnrealMcp\Packages\UnrealMcp-v0.19.0-win-ue56-ue57-projectroot.zip `
    --mode source `
    --strict
```

Expected: every check `[PASS]`, overall `Overall: pass (0 blocking, 0 warnings)`.

If you used `--full-experience`, change `--mode full-win`.

Note the sha256 — both the script output AND the `.sha256` sidecar should match.

---

## 3. Upload to the existing GitHub release

The Mac zip is already attached to each release. You add the Win zip + sha256 sidecar:

```powershell
gh release upload v0.19.0 `
    --repo edwinmeng163-oss/UEvolve `
    Saved\UnrealMcp\Packages\UnrealMcp-v0.19.0-win-ue56-ue57-projectroot.zip `
    Saved\UnrealMcp\Packages\UnrealMcp-v0.19.0-win-ue56-ue57-projectroot.zip.sha256
```

Verify online:

```powershell
gh release view v0.19.0 --repo edwinmeng163-oss/UEvolve
```

You should see 4 assets attached (2 Mac + 2 Win).

### 3.1 If the release already has a Win asset (re-upload)

`gh release upload` refuses to overwrite by default. If you need to replace:

```powershell
gh release upload v0.19.0 `
    --repo edwinmeng163-oss/UEvolve `
    --clobber `
    Saved\UnrealMcp\Packages\UnrealMcp-v0.19.0-win-ue56-ue57-projectroot.zip `
    Saved\UnrealMcp\Packages\UnrealMcp-v0.19.0-win-ue56-ue57-projectroot.zip.sha256
```

---

## 4. Batch the backlog

To do all 6 pending versions in one session:

```powershell
$versions = @("0.15.1", "0.15.2", "0.16.0", "0.17.0", "0.18.0", "0.19.0")

foreach ($v in $versions) {
    Write-Host "=== Packaging v$v ===" -ForegroundColor Cyan

    git checkout "v$v"

    # Clean
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Plugins\UnrealMcp\Binaries, Plugins\UnrealMcp\Intermediate
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Examples\UEvolveExample57\Binaries, Examples\UEvolveExample57\Intermediate

    # Build (only needed for full-experience; skip for source-only)
    # & "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" `
    #     MyProjectEditor Win64 Development `
    #     -project="$(Resolve-Path .\Examples\UEvolveExample57\UEvolveExample57.uproject)" `
    #     -WaitMutex
    # if ($LASTEXITCODE -ne 0) { throw "Build failed for v$v" }

    # Validators
    python Tools\validate_tool_registry.py
    if ($LASTEXITCODE -ne 0) { throw "validate_tool_registry failed for v$v" }

    # Package source-only
    pwsh -File Tools\package_plugin.ps1 -Version $v
    if ($LASTEXITCODE -ne 0) { throw "package_plugin failed for v$v" }

    # Verify
    $zip = "Saved\UnrealMcp\Packages\UnrealMcp-v$v-win-ue56-ue57-projectroot.zip"
    python Tools\verify_package_integrity.py --zip $zip --mode source --strict
    if ($LASTEXITCODE -ne 0) { throw "verify failed for v$v" }

    # Upload
    gh release upload "v$v" --repo edwinmeng163-oss/UEvolve $zip "$zip.sha256"

    Write-Host "=== v$v done ===" -ForegroundColor Green
}
```

Return to `main` when finished:

```powershell
git checkout main
```

---

## 5. Common pitfalls

### Git checkout from worktree paths
Don't run `git checkout` from inside `.claude\worktrees\` — that's a developer artifact, not the canonical project root. Always run from the repository root (`C:\src\UEvolve` or wherever you cloned).

### Long path issues
Windows default MAX_PATH is 260 characters. UE's Intermediate directory generates deep paths. If you hit `path too long` errors:

```powershell
# As Administrator, once per machine:
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -Value 1
# Then restart PowerShell.
```

Also ensure git long-paths support:
```powershell
git config --global core.longpaths true
```

### Encoding gotcha
The packaging script and validators expect UTF-8. If your PowerShell session has a non-UTF-8 default:

```powershell
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
```

(Add to your PowerShell profile for permanent.)

### Git symlinks
The repo uses a Windows-compatible "Schemas mirror" pattern where some files appear as small stubs on Windows by default. The verifier checks this:

```
[PASS] schema_alias_not_stub: Package-critical real files are not Windows git-symlink stubs.
```

If this fails, run:

```powershell
git config core.symlinks true
git checkout -- Schemas/  Plugins/UnrealMcp/Resources/ToolRegistry/
```

(Requires Developer Mode or running git from elevated shell.)

### Build target name mismatch
`MyProjectEditor` is the canonical target — the folder is `UEvolveExample57` but the internal UE module is `MyProject`. If you try `UEvolveExample57Editor`, UBT will helpfully create a `.Target.cs` to satisfy your bad command, which is then noise to delete.

---

## 6. Reporting back to PM (Claude)

After uploading, send PM a short status:

- Versions packaged: `v0.X.Y`, `v0.A.B`, ...
- All zips uploaded to GitHub releases: yes / no (which failed and why)
- Any validator or build warnings to flag
- SHA-256 of each zip (so PM can cross-check)

PM updates `~/.hermes/memories/MEMORY.md` and any "Windows release status" lines in the release notes if needed.

---

## 7. References

- Mac counterpart script: `Tools/package_plugin.sh`
- Verifier: `Tools/verify_package_integrity.py`
- Registry validator: `Tools/validate_tool_registry.py`
- Engine compat validator: `Tools/check_ue56_compat.py`
- Install guide (end-user facing, lives inside the zip): `Tools/PackagingResources/INSTALL.md`
- Project conventions: `AGENTS.md`, `Tools/codex-prompt-header.md`

For questions or odd errors, ping PM (Claude) with the command you ran and the full error output — do not improvise unknown fixes on release artifacts.
