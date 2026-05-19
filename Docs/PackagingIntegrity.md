# Packaging Integrity Verifier

`Tools/verify_package_integrity.py` is the C5 chunk 1a standalone verifier for
UEAtelier project-root packages. It checks the staged or zipped package before a
release handoff catches hidden install defects such as registry mirror drift,
Windows git-symlink stubs, missing install docs, stale generated state, and
missing full Windows runtime files.

The in-editor install doctor planned for C5 chunk 1b should reuse the check IDs
and severity vocabulary from this verifier.

## Usage

Validate the live canonical repo without failing on local generated state:

```bash
python3 Tools/verify_package_integrity.py --root . --mode source --repo-root .
```

Validate a staged source package root strictly:

```bash
python3 Tools/verify_package_integrity.py --root Saved/UnrealMcp/Packages/.stage-... --mode source --strict --repo-root .
```

Validate a staged full Windows package root strictly:

```bash
python3 Tools/verify_package_integrity.py --root Saved/UnrealMcp/Packages/.stage-... --mode full-win --strict --repo-root .
```

Validate a zip and its optional sibling `<zip>.sha256`:

```bash
python3 Tools/verify_package_integrity.py --zip Saved/UnrealMcp/Packages/UnrealMcp-v0.15.0.zip --mode source --strict --repo-root .
```

Emit machine-readable JSON:

```bash
python3 Tools/verify_package_integrity.py --root . --mode source --repo-root . --json
```

`--lenient` is the default. In lenient mode, warnings do not fail the process.
`--strict` returns a non-zero exit code when warnings are present. `--repo-root`
is optional and enables the deep `git ls-files -s` symlink scan; without it, that
check is reported as a non-blocking skip.

The JSON output keeps the required summary fields at the top level and includes
a `checks` array. Each check has this shape:

```json
{"id":"...","severity":"pass|warning|error","summary":"...","details":{},"recommendedFix":"..."}
```

`details` and `recommendedFix` are omitted when empty.

## Invariant Catalog

- `registry_mirror_equal`: `Plugins/UnrealMcp/Resources/ToolRegistry/tools.json`
  byte-equals `Tools/UnrealMcpToolRegistry/tools.json` when both exist.
- `schema_canonical_mirror_equal`: `Schemas/UnrealMcpToolRegistry.schema.json`
  byte-equals `Tools/UnrealMcpToolRegistry/schema.json` when both exist.
- `schema_canonical_alias_equal`: plugin schema aliases such as
  `Plugins/UnrealMcp/Resources/ToolRegistry/schema.json` byte-equal the
  canonical registry schema when present.
- `no_git_symlinks`: with `--repo-root`, `git ls-files -s` must report no
  tracked mode `120000` symlink entries.
- `required_files_present`: mode-specific package-critical files exist and are
  real files.
- `excluded_paths_absent`: package roots reject local/generated paths such as
  `Saved`, `Intermediate`, `DerivedDataCache`, `.DS_Store`, `__pycache__`, and
  `*.pyc`; source packages also reject `Binaries`, `node_modules`, and
  `runtime` except where full Windows packages explicitly require them.
- `win64_binary_present`: full Windows packages include
  `Plugins/UnrealMcp/Binaries/Win64/UnrealEditor-UnrealMcp.dll` and
  `UnrealEditor.modules`.
- `bridge_runtime_bundled`: full Windows packages include the Codex bridge
  runtime `bun.exe`, `node_modules/`, `package.json`, and Windows launchers.
- `install_md_present`: package install documentation is present in the expected
  root and/or plugin location for the package shape.
- `first_launch_md_present`: `Docs/FIRST_LAUNCH.md` is present where Docs are
  part of the package shape.
- `registry_schema_valid`: registry and schema JSON parse and expose the
  expected top-level shape.
- `sha256_sidecar_present`: zip validation checks sibling `<zip>.sha256` when it
  exists; absence is a strict-mode warning, not a blocking error.
- `schema_alias_not_stub`: package-critical registry, schema, descriptor,
  source, and required doc files are not Windows git-symlink stubs.

The symlink-stub heuristic flags files of 200 bytes or less that UTF-8 decode to
a single non-empty line matching a relative, absolute, or drive-rooted path. For
known formats such as JSON, Markdown, Unreal descriptor JSON, C++ source, and
INI files, the verifier also performs a minimal parse or syntax-shape check.

## Exit Codes

- `0`: all checks pass, or warnings only in lenient mode.
- `1`: warnings only and `--strict` was set.
- `2`: one or more blocking `error` checks.

Release and staging scripts should run the verifier with `--strict` before
creating the zip, then optionally run it again with `--zip` after the zip and
sidecar are written.
