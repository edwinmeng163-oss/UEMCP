#!/usr/bin/env python3
"""Validate UEvolve project-root package integrity.

This verifier is intentionally stdlib-only so it can run in CI, from package
scripts, or through Unreal's embedded Python for smoke fixtures.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple


VALIDATOR_VERSION = "0.15.0-c5-1a"
SEVERITIES = ("pass", "warning", "error")
STUB_TARGET_RE = re.compile(r"^(\.{0,2}/|/|[A-Za-z]:[\\/])")


@dataclass(frozen=True)
class CheckResult:
    check_id: str
    severity: str
    summary: str
    details: Optional[Dict[str, Any]] = None
    recommended_fix: str = ""

    def to_json(self) -> Dict[str, Any]:
        result: Dict[str, Any] = {
            "id": self.check_id,
            "severity": self.severity,
            "summary": self.summary,
        }
        if self.details:
            result["details"] = self.details
        if self.recommended_fix:
            result["recommendedFix"] = self.recommended_fix
        return result


def make_check(
    check_id: str,
    severity: str,
    summary: str,
    details: Optional[Dict[str, Any]] = None,
    recommended_fix: str = "",
) -> CheckResult:
    if severity not in SEVERITIES:
        raise ValueError("Unknown severity: %s" % severity)
    return CheckResult(check_id, severity, summary, details, recommended_fix)


def compare_files(left: Path, right: Path) -> bool:
    return left.read_bytes() == right.read_bytes()


def rel_path(root: Path, path: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def is_real_file(path: Path) -> bool:
    return path.exists() and path.is_file() and not path.is_symlink()


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def infer_scope(root: Path, zip_input: bool) -> str:
    if zip_input:
        return "package"
    if (root / ".git").exists():
        return "repo"
    return "package"


def aggregate_summary(mode: str, scope: str, checks: Sequence[CheckResult]) -> Dict[str, Any]:
    blocking_count = sum(1 for check in checks if check.severity == "error")
    warning_count = sum(1 for check in checks if check.severity == "warning")
    if blocking_count:
        overall = "error"
    elif warning_count:
        overall = "warning"
    else:
        overall = "pass"
    return {
        "mode": mode,
        "scope": scope,
        "overallSeverity": overall,
        "blockingIssueCount": blocking_count,
        "warningCount": warning_count,
        "lastRunUtc": utc_now(),
        "validatorVersion": VALIDATOR_VERSION,
    }


def exit_code_for(checks: Sequence[CheckResult], strict: bool) -> int:
    if any(check.severity == "error" for check in checks):
        return 2
    if strict and any(check.severity == "warning" for check in checks):
        return 1
    return 0


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_extract_zip(zip_path: Path, destination: Path) -> None:
    """Extract zip with path-separator normalization.

    Zips produced by PowerShell's Compress-Archive on Windows use backslash
    entry names. The Python zipfile module on macOS/Linux treats those as
    literal characters in filenames rather than path separators, so a member
    named "Docs\\FIRST_LAUNCH.md" extracts as a single file with that literal
    name instead of a Docs/ directory with FIRST_LAUNCH.md inside.

    The Zip specification (APPNOTE.TXT 4.4.17.1) requires forward slashes,
    but Windows tools tolerate backslashes for reading and emit them on
    write. To accept both, we rewrite each member's filename to use forward
    slashes before extraction.
    """
    destination_resolved = destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as archive:
        for member in archive.infolist():
            normalized = member.filename.replace("\\", "/")
            target = (destination / normalized).resolve()
            if target != destination_resolved and destination_resolved not in target.parents:
                raise ValueError("Zip member escapes extraction root: %s" % normalized)
            # Manual extraction with the normalized name. archive.extractall()
            # looks members up by their original (possibly backslash) key, so
            # in-place renaming of member.filename does not redirect the write
            # path. Writing each file directly is the only reliable way to
            # honor the rewritten path.
            if normalized.endswith("/"):
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source, open(target, "wb") as dest:
                while True:
                    chunk = source.read(65536)
                    if not chunk:
                        break
                    dest.write(chunk)


def find_validation_root(extracted_root: Path) -> Path:
    if (extracted_root / "Plugins" / "UnrealMcp").exists():
        return extracted_root
    candidates = [
        child
        for child in extracted_root.iterdir()
        if child.is_dir()
        and child.name != "__MACOSX"
        and (child / "Plugins" / "UnrealMcp").exists()
    ]
    if len(candidates) == 1:
        return candidates[0]
    return extracted_root


def required_file_paths(mode: str, scope: str) -> List[str]:
    paths = [
        "Plugins/UnrealMcp/UnrealMcp.uplugin",
        "Plugins/UnrealMcp/README.md",
        "Plugins/UnrealMcp/Source/UnrealMcp/UnrealMcp.Build.cs",
        "Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpModule.cpp",
        "Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolRegistrar.cpp",
        "Plugins/UnrealMcp/Resources/ToolRegistry/tools.json",
        "Plugins/UnrealMcp/Resources/ToolRegistry/schema.json",
        "Tools/UnrealMcpToolRegistry/tools.json",
        "Tools/UnrealMcpToolRegistry/schema.json",
        "Tools/UnrealMcpPyTools/editor_python_runtime_info/main.py",
        "Tools/UnrealMcpCodexBridge/package.json",
        "Tools/UnrealMcpCodexBridge/start-bridge.cmd",
        "Tools/UnrealMcpCodexBridge/start-bridge.ps1",
        "Tools/UnrealMcpCodexBridge/start-bridge.sh",
        "Tools/UnrealMcpToolScaffoldStarters/README.md",
        "Tools/UnrealMcpToolScaffolds/fps_bootstrap/ScaffoldMetadata.json",
        "Tools/UnrealMcpToolScaffolds/verify_input_drives_pawn/ScaffoldMetadata.json",
    ]
    if scope == "repo" or mode == "full-win":
        paths.append("Schemas/UnrealMcpToolRegistry.schema.json")
    return paths


def critical_real_file_paths(mode: str, scope: str) -> List[str]:
    paths = list(required_file_paths(mode, scope))
    paths.extend(
        [
            "Docs/FIRST_LAUNCH.md",
            "INSTALL.md",
            "Plugins/UnrealMcp/INSTALL.md",
            "Tools/PackagingResources/INSTALL.md",
            "Schemas/UnrealMcpToolRegistry.schema.json",
        ]
    )
    if mode == "full-win":
        paths.extend(
            [
                "Plugins/UnrealMcp/Binaries/Win64/UnrealEditor-UnrealMcp.dll",
                "Plugins/UnrealMcp/Binaries/Win64/UnrealEditor.modules",
                "Tools/UnrealMcpCodexBridge/runtime/bun.exe",
            ]
        )
    return sorted(set(paths))


def compare_when_both_exist(
    root: Path,
    check_id: str,
    left_rel: str,
    right_rel: str,
    label: str,
) -> CheckResult:
    left = root / left_rel
    right = root / right_rel
    if not left.exists() or not right.exists():
        return make_check(
            check_id,
            "pass",
            "%s comparison skipped because one or both files are absent." % label,
            {"left": left_rel, "right": right_rel, "leftExists": left.exists(), "rightExists": right.exists()},
        )
    if not is_real_file(left) or not is_real_file(right):
        return make_check(
            check_id,
            "error",
            "%s comparison requires both paths to be real files." % label,
            {"left": left_rel, "right": right_rel},
            "Replace symlinks or non-files with real package files.",
        )
    if compare_files(left, right):
        return make_check(
            check_id,
            "pass",
            "%s files byte-match." % label,
            {"left": left_rel, "right": right_rel},
        )
    return make_check(
        check_id,
        "error",
        "%s files differ." % label,
        {"left": left_rel, "right": right_rel},
        "Regenerate or copy the canonical file so the package mirror is byte-identical.",
    )


def check_registry_mirror_equal(root: Path) -> CheckResult:
    return compare_when_both_exist(
        root,
        "registry_mirror_equal",
        "Plugins/UnrealMcp/Resources/ToolRegistry/tools.json",
        "Tools/UnrealMcpToolRegistry/tools.json",
        "Tool registry mirror",
    )


def check_schema_canonical_mirror_equal(root: Path) -> CheckResult:
    return compare_when_both_exist(
        root,
        "schema_canonical_mirror_equal",
        "Schemas/UnrealMcpToolRegistry.schema.json",
        "Tools/UnrealMcpToolRegistry/schema.json",
        "Canonical schema mirror",
    )


def check_schema_canonical_alias_equal(root: Path) -> CheckResult:
    canonical_candidates = [
        root / "Tools/UnrealMcpToolRegistry/schema.json",
        root / "Schemas/UnrealMcpToolRegistry.schema.json",
    ]
    canonical = next((path for path in canonical_candidates if path.exists()), None)
    alias_rels = [
        "Plugins/UnrealMcp/Resources/ToolRegistry/schema.json",
        "Plugins/UnrealMcp/Resources/Schemas/UnrealMcpToolRegistry.schema.json",
        "Plugins/UnrealMcp/Resources/ToolRegistry/UnrealMcpToolRegistry.schema.json",
    ]
    aliases = [root / rel for rel in alias_rels if (root / rel).exists()]
    if canonical is None:
        return make_check(
            "schema_canonical_alias_equal",
            "warning",
            "Plugin schema alias comparison skipped because no canonical schema was found.",
            {"canonicalCandidates": [rel_path(root, path) for path in canonical_candidates], "aliases": alias_rels},
            "Restore Tools/UnrealMcpToolRegistry/schema.json before packaging.",
        )
    if not aliases:
        return make_check(
            "schema_canonical_alias_equal",
            "warning",
            "No plugin schema alias was found to compare against the canonical schema.",
            {"canonical": rel_path(root, canonical), "aliasCandidates": alias_rels},
            "Package Plugins/UnrealMcp/Resources/ToolRegistry/schema.json when building current UEvolve packages.",
        )

    mismatches: List[str] = []
    non_files: List[str] = []
    for alias in aliases:
        if not is_real_file(alias):
            non_files.append(rel_path(root, alias))
            continue
        if not compare_files(canonical, alias):
            mismatches.append(rel_path(root, alias))
    if non_files:
        return make_check(
            "schema_canonical_alias_equal",
            "error",
            "One or more plugin schema aliases are not real files.",
            {"canonical": rel_path(root, canonical), "nonFiles": non_files},
            "Replace schema aliases with real files in the package.",
        )
    if mismatches:
        return make_check(
            "schema_canonical_alias_equal",
            "error",
            "One or more plugin schema aliases differ from the canonical schema.",
            {"canonical": rel_path(root, canonical), "mismatches": mismatches},
            "Copy Tools/UnrealMcpToolRegistry/schema.json into the plugin resource alias.",
        )
    return make_check(
        "schema_canonical_alias_equal",
        "pass",
        "Plugin schema alias files byte-match the canonical schema.",
        {"canonical": rel_path(root, canonical), "aliases": [rel_path(root, path) for path in aliases]},
    )


def check_no_git_symlinks(repo_root: Optional[Path]) -> CheckResult:
    if repo_root is None:
        return make_check(
            "no_git_symlinks",
            "pass",
            "Git symlink scan skipped because --repo-root was not provided.",
            {"skipped": True},
        )
    command = ["git", "-C", str(repo_root), "ls-files", "-s"]
    try:
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    except OSError as exc:
        return make_check(
            "no_git_symlinks",
            "warning",
            "Git symlink scan could not run.",
            {"repoRoot": str(repo_root), "error": str(exc)},
            "Install git or omit --repo-root when validating a package without repository context.",
        )
    if proc.returncode != 0:
        return make_check(
            "no_git_symlinks",
            "warning",
            "Git symlink scan failed for the provided --repo-root.",
            {"repoRoot": str(repo_root), "stderr": proc.stderr.strip()},
            "Pass a valid Git checkout as --repo-root, or omit --repo-root for package-only validation.",
        )
    symlinks: List[str] = []
    for line in proc.stdout.splitlines():
        parts = line.split(None, 3)
        if len(parts) >= 4 and parts[0] == "120000":
            symlinks.append(parts[3].split("\t", 1)[-1])
    if symlinks:
        return make_check(
            "no_git_symlinks",
            "error",
            "Tracked Git symlink entries were found.",
            {"repoRoot": str(repo_root), "symlinks": symlinks},
            "Replace tracked symlinks with real files before creating Windows-facing packages.",
        )
    return make_check(
        "no_git_symlinks",
        "pass",
        "No tracked Git symlink entries were found.",
        {"repoRoot": str(repo_root)},
    )


def check_required_files_present(root: Path, mode: str, scope: str) -> CheckResult:
    required = required_file_paths(mode, scope)
    missing: List[str] = []
    not_real_files: List[str] = []
    for rel in required:
        path = root / rel
        if not path.exists():
            missing.append(rel)
        elif not is_real_file(path):
            not_real_files.append(rel)
    if missing or not_real_files:
        return make_check(
            "required_files_present",
            "error",
            "Required package files are missing or are not real files.",
            {"missing": missing, "notRealFiles": not_real_files},
            "Restage the package from the canonical repo and ensure copied files are not symlinks or stubs.",
        )
    return make_check(
        "required_files_present",
        "pass",
        "Mode-specific required files are present as real files.",
        {"checkedCount": len(required), "mode": mode, "scope": scope},
    )


def is_allowed_full_win_generated_path(rel: str) -> bool:
    allowed_prefixes = (
        "Plugins/UnrealMcp/Binaries/",
        "Tools/UnrealMcpCodexBridge/node_modules/",
        "Tools/UnrealMcpCodexBridge/runtime/",
    )
    allowed_exact = (
        "Plugins/UnrealMcp/Binaries",
        "Tools/UnrealMcpCodexBridge/node_modules",
        "Tools/UnrealMcpCodexBridge/runtime",
    )
    return rel in allowed_exact or rel.startswith(allowed_prefixes)


def check_excluded_paths_absent(root: Path, mode: str, scope: str) -> CheckResult:
    if scope == "repo":
        return make_check(
            "excluded_paths_absent",
            "pass",
            "Package-only generated path rejection skipped for live repo scope.",
            {"scope": scope, "note": "Saved, Intermediate, Binaries, node_modules, and similar local state are ignored for repo validation."},
        )

    excluded_names = {"Saved", "Intermediate", "DerivedDataCache", ".DS_Store", "__pycache__"}
    source_only_names = {"Binaries", "node_modules", "runtime"}
    offenders: List[str] = []
    for path in root.rglob("*"):
        rel = rel_path(root, path)
        name = path.name
        if name in excluded_names or name.endswith(".pyc"):
            offenders.append(rel)
            continue
        if mode == "source" and name in source_only_names:
            offenders.append(rel)
            continue
        if mode == "full-win" and name in source_only_names and not is_allowed_full_win_generated_path(rel):
            offenders.append(rel)
    if offenders:
        return make_check(
            "excluded_paths_absent",
            "error",
            "Excluded generated or local-only paths are present in package scope.",
            {"offenders": offenders[:50], "offenderCount": len(offenders), "mode": mode},
            "Remove generated state from the staging tree and rerun the package script.",
        )
    return make_check(
        "excluded_paths_absent",
        "pass",
        "No excluded generated or local-only paths were found in package scope.",
        {"mode": mode, "scope": scope},
    )


def check_win64_binary_present(root: Path, mode: str) -> CheckResult:
    if mode != "full-win":
        return make_check(
            "win64_binary_present",
            "pass",
            "Win64 binary requirement is not applicable in source mode.",
            {"mode": mode},
        )
    required = [
        "Plugins/UnrealMcp/Binaries/Win64/UnrealEditor-UnrealMcp.dll",
        "Plugins/UnrealMcp/Binaries/Win64/UnrealEditor.modules",
    ]
    missing = [rel for rel in required if not is_real_file(root / rel)]
    if missing:
        return make_check(
            "win64_binary_present",
            "error",
            "Full Windows package is missing required Win64 plugin binaries.",
            {"missing": missing},
            "Bundle the prebuilt Win64 UnrealMcp DLL and UnrealEditor.modules before zipping.",
        )
    return make_check(
        "win64_binary_present",
        "pass",
        "Full Windows package contains required Win64 plugin binaries.",
        {"files": required},
    )


def check_bridge_runtime_bundled(root: Path, mode: str) -> CheckResult:
    if mode != "full-win":
        return make_check(
            "bridge_runtime_bundled",
            "pass",
            "Bridge runtime bundle requirement is not applicable in source mode.",
            {"mode": mode},
        )
    required_files = [
        "Tools/UnrealMcpCodexBridge/runtime/bun.exe",
        "Tools/UnrealMcpCodexBridge/package.json",
        "Tools/UnrealMcpCodexBridge/start-bridge.cmd",
        "Tools/UnrealMcpCodexBridge/start-bridge.ps1",
    ]
    node_modules = root / "Tools/UnrealMcpCodexBridge/node_modules"
    missing = [rel for rel in required_files if not is_real_file(root / rel)]
    if not node_modules.exists() or not node_modules.is_dir():
        missing.append("Tools/UnrealMcpCodexBridge/node_modules/")
    if missing:
        return make_check(
            "bridge_runtime_bundled",
            "error",
            "Full Windows package is missing required Codex bridge runtime files.",
            {"missing": missing},
            "Bundle the offline Codex bridge runtime, node_modules, package.json, and Windows launchers.",
        )
    return make_check(
        "bridge_runtime_bundled",
        "pass",
        "Full Windows package contains the required Codex bridge runtime files.",
        {"checkedFiles": required_files, "checkedDirectories": ["Tools/UnrealMcpCodexBridge/node_modules/"]},
    )


def check_install_md_present(root: Path, mode: str, scope: str) -> CheckResult:
    if scope == "repo":
        packaging_resource = root / "Tools/PackagingResources/INSTALL.md"
        if is_real_file(packaging_resource):
            return make_check(
                "install_md_present",
                "pass",
                "Repo install documentation resource is present for package staging.",
                {"file": "Tools/PackagingResources/INSTALL.md"},
            )
    root_install = root / "INSTALL.md"
    plugin_install = root / "Plugins/UnrealMcp/INSTALL.md"
    present = [rel for rel, path in (("INSTALL.md", root_install), ("Plugins/UnrealMcp/INSTALL.md", plugin_install)) if is_real_file(path)]
    if mode == "full-win":
        missing = [
            rel
            for rel, path in (("INSTALL.md", root_install), ("Plugins/UnrealMcp/INSTALL.md", plugin_install))
            if not is_real_file(path)
        ]
        if missing:
            return make_check(
                "install_md_present",
                "error",
                "Full Windows package is missing expected install documentation.",
                {"missing": missing, "present": present},
                "Copy Tools/PackagingResources/INSTALL.md to both root INSTALL.md and Plugins/UnrealMcp/INSTALL.md.",
            )
    elif not present:
        return make_check(
            "install_md_present",
            "error",
            "Package is missing install documentation.",
            {"expectedAnyOf": ["INSTALL.md", "Plugins/UnrealMcp/INSTALL.md"]},
            "Copy Tools/PackagingResources/INSTALL.md into the staged package.",
        )
    return make_check(
        "install_md_present",
        "pass",
        "Install documentation is present.",
        {"present": present, "mode": mode, "scope": scope},
    )


def check_first_launch_md_present(root: Path) -> CheckResult:
    rel = "Docs/FIRST_LAUNCH.md"
    if is_real_file(root / rel):
        return make_check(
            "first_launch_md_present",
            "pass",
            "First-launch documentation is present.",
            {"file": rel},
        )
    return make_check(
        "first_launch_md_present",
        "error",
        "First-launch documentation is missing.",
        {"missing": rel},
        "Copy Docs/FIRST_LAUNCH.md into the package Docs directory.",
    )


def load_json_file(path: Path) -> Tuple[Optional[Any], Optional[str]]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle), None
    except Exception as exc:  # noqa: BLE001 - exact parse failure belongs in details.
        return None, str(exc)


def check_registry_schema_valid(root: Path) -> CheckResult:
    errors: List[Dict[str, str]] = []
    details: Dict[str, Any] = {}
    registry_paths = [
        "Tools/UnrealMcpToolRegistry/tools.json",
        "Plugins/UnrealMcp/Resources/ToolRegistry/tools.json",
    ]
    schema_paths = [
        "Tools/UnrealMcpToolRegistry/schema.json",
        "Schemas/UnrealMcpToolRegistry.schema.json",
        "Plugins/UnrealMcp/Resources/ToolRegistry/schema.json",
    ]

    parsed_registries = 0
    for rel in registry_paths:
        path = root / rel
        if not path.exists():
            continue
        data, error = load_json_file(path)
        if error is not None:
            errors.append({"path": rel, "error": error})
            continue
        parsed_registries += 1
        if not isinstance(data, dict) or not isinstance(data.get("tools"), list):
            errors.append({"path": rel, "error": "Registry must be a JSON object with a top-level tools array."})
            continue
        if "schemaVersion" not in data or "registryVersion" not in data:
            errors.append({"path": rel, "error": "Registry is missing schemaVersion or registryVersion."})
        details.setdefault("registryToolCounts", {})[rel] = len(data.get("tools", []))

    parsed_schemas = 0
    for rel in schema_paths:
        path = root / rel
        if not path.exists():
            continue
        data, error = load_json_file(path)
        if error is not None:
            errors.append({"path": rel, "error": error})
            continue
        parsed_schemas += 1
        if not isinstance(data, dict) or data.get("type") != "object":
            errors.append({"path": rel, "error": "Schema must be a JSON object with type=object."})
            continue
        properties = data.get("properties")
        if not isinstance(properties, dict) or "tools" not in properties:
            errors.append({"path": rel, "error": "Schema is missing properties.tools."})

    details["parsedRegistries"] = parsed_registries
    details["parsedSchemas"] = parsed_schemas
    if errors:
        return make_check(
            "registry_schema_valid",
            "error",
            "Registry or schema JSON failed minimal validation.",
            {"errors": errors, **details},
            "Fix invalid JSON and restore the expected top-level registry/schema shape.",
        )
    if parsed_registries == 0 or parsed_schemas == 0:
        return make_check(
            "registry_schema_valid",
            "error",
            "Registry or schema JSON was not found for validation.",
            details,
            "Include Tools/UnrealMcpToolRegistry/tools.json and schema.json in the package.",
        )
    return make_check(
        "registry_schema_valid",
        "pass",
        "Registry and schema JSON passed minimal validation.",
        details,
    )


def check_sha256_sidecar_present(zip_path: Optional[Path], strict: bool) -> CheckResult:
    if zip_path is None:
        return make_check(
            "sha256_sidecar_present",
            "pass",
            "SHA-256 sidecar check is not applicable for --root validation.",
            {"zipInput": False},
        )
    sidecar = Path(str(zip_path) + ".sha256")
    if not sidecar.exists():
        severity = "warning" if strict else "pass"
        return make_check(
            "sha256_sidecar_present",
            severity,
            "SHA-256 sidecar is absent for the zip input.",
            {"sidecar": str(sidecar), "strict": strict},
            "Create a sibling <zip>.sha256 sidecar before strict release verification." if strict else "",
        )
    try:
        content = sidecar.read_text(encoding="utf-8").strip()
    except UnicodeDecodeError as exc:
        return make_check(
            "sha256_sidecar_present",
            "error",
            "SHA-256 sidecar is not UTF-8 text.",
            {"sidecar": str(sidecar), "error": str(exc)},
            "Regenerate the sidecar with a plain SHA-256 line.",
        )
    match = re.search(r"\b([0-9a-fA-F]{64})\b", content)
    if not match:
        return make_check(
            "sha256_sidecar_present",
            "error",
            "SHA-256 sidecar does not contain a 64-character hex digest.",
            {"sidecar": str(sidecar)},
            "Regenerate the sidecar with the package SHA-256 digest.",
        )
    expected = match.group(1).lower()
    actual = sha256_file(zip_path)
    if expected != actual:
        return make_check(
            "sha256_sidecar_present",
            "error",
            "SHA-256 sidecar digest does not match the zip.",
            {"sidecar": str(sidecar), "expected": expected, "actual": actual},
            "Regenerate the zip or its SHA-256 sidecar from the same package artifact.",
        )
    return make_check(
        "sha256_sidecar_present",
        "pass",
        "SHA-256 sidecar matches the zip input.",
        {"sidecar": str(sidecar), "sha256": actual},
    )


def is_symlink_stub_candidate(path: Path) -> Tuple[bool, Dict[str, Any]]:
    try:
        if path.stat().st_size > 200:
            return False, {}
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return False, {}
    non_empty_lines = [line.strip() for line in text.splitlines() if line.strip()]
    if len(non_empty_lines) != 1:
        return False, {}
    line = non_empty_lines[0]
    if STUB_TARGET_RE.match(line):
        return True, {"size": path.stat().st_size, "targetLine": line}
    return False, {}


def validate_known_format(path: Path) -> Tuple[bool, str]:
    suffix = path.suffix.lower()
    try:
        if suffix in {".json", ".uplugin", ".uproject"}:
            data = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(data, (dict, list)):
                return False, "JSON-like file must parse to an object or array."
            return True, ""
        if suffix == ".md":
            text = path.read_text(encoding="utf-8")
            if not text.strip():
                return False, "Markdown file is empty."
            return True, ""
        if suffix in {".yaml", ".yml"}:
            text = path.read_text(encoding="utf-8")
            if ":" not in text:
                return False, "YAML-like file has no key delimiter."
            return True, ""
        if suffix in {".cpp", ".h", ".ini"}:
            text = path.read_text(encoding="utf-8")
            if not text.strip():
                return False, "Source/config file is empty."
            if suffix in {".cpp", ".h"} and not any(token in text for token in ("#", "{", ";")):
                return False, "C++ file lacks expected source tokens."
            if suffix == ".ini" and not any(token in text for token in ("[", "=")):
                return False, "INI file lacks expected section or assignment syntax."
            return True, ""
    except Exception as exc:  # noqa: BLE001 - format failure is reported as validation detail.
        return False, str(exc)
    return True, ""


def check_schema_alias_not_stub(root: Path, mode: str, scope: str) -> CheckResult:
    stubbed: List[Dict[str, Any]] = []
    invalid_known_format: List[Dict[str, str]] = []
    checked = 0
    for rel in critical_real_file_paths(mode, scope):
        path = root / rel
        if not path.exists():
            continue
        checked += 1
        if path.is_symlink():
            stubbed.append({"path": rel, "reason": "filesystem symlink"})
            continue
        candidate, stub_details = is_symlink_stub_candidate(path)
        if candidate:
            stubbed.append({"path": rel, **stub_details})
            continue
        valid, reason = validate_known_format(path)
        if not valid:
            invalid_known_format.append({"path": rel, "reason": reason})
    if stubbed or invalid_known_format:
        return make_check(
            "schema_alias_not_stub",
            "error",
            "Package-critical files include symlinks, Windows symlink stubs, or invalid known-format files.",
            {"stubbed": stubbed, "invalidKnownFormat": invalid_known_format},
            "Replace package-critical stubs with real file contents and rerun the verifier.",
        )
    return make_check(
        "schema_alias_not_stub",
        "pass",
        "Package-critical real files are not Windows git-symlink stubs.",
        {"checkedCount": checked},
    )


def build_checks(
    root: Path,
    mode: str,
    scope: str,
    repo_root: Optional[Path],
    zip_path: Optional[Path],
    strict: bool,
) -> List[CheckResult]:
    return [
        check_registry_mirror_equal(root),
        check_schema_canonical_mirror_equal(root),
        check_schema_canonical_alias_equal(root),
        check_no_git_symlinks(repo_root),
        check_required_files_present(root, mode, scope),
        check_excluded_paths_absent(root, mode, scope),
        check_win64_binary_present(root, mode),
        check_bridge_runtime_bundled(root, mode),
        check_install_md_present(root, mode, scope),
        check_first_launch_md_present(root),
        check_registry_schema_valid(root),
        check_sha256_sidecar_present(zip_path, strict),
        check_schema_alias_not_stub(root, mode, scope),
    ]


def make_payload(summary: Dict[str, Any], checks: Sequence[CheckResult]) -> Dict[str, Any]:
    payload = dict(summary)
    payload["checks"] = [check.to_json() for check in checks]
    return payload


def print_pretty(summary: Dict[str, Any], checks: Sequence[CheckResult]) -> None:
    print("UEvolve package integrity verifier %s" % VALIDATOR_VERSION)
    print("Mode: %s" % summary["mode"])
    print("Scope: %s" % summary["scope"])
    print("Overall: %s (%s blocking, %s warnings)" % (
        summary["overallSeverity"],
        summary["blockingIssueCount"],
        summary["warningCount"],
    ))
    for check in checks:
        print("[%s] %s: %s" % (check.severity.upper(), check.check_id, check.summary))
        if check.details:
            print("  details: %s" % json.dumps(check.details, sort_keys=True))
        if check.recommended_fix:
            print("  recommendedFix: %s" % check.recommended_fix)


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate UEvolve package integrity.")
    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument("--root", help="Validate an extracted project-root package, repo root, or package root.")
    input_group.add_argument("--zip", dest="zip_path", help="Extract and validate a project-root package zip.")
    parser.add_argument("--mode", required=True, choices=("source", "full-win"), help="Package mode to validate.")
    strict_group = parser.add_mutually_exclusive_group()
    strict_group.add_argument("--lenient", action="store_true", help="Allow warnings without failing. This is the default.")
    strict_group.add_argument("--strict", action="store_true", help="Return exit code 1 for warnings.")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON.")
    parser.add_argument("--repo-root", help="Optional Git repo root for deep tracked symlink checks.")
    return parser.parse_args(argv)


def run_validation(args: argparse.Namespace) -> Tuple[Dict[str, Any], List[CheckResult]]:
    strict = bool(args.strict)
    repo_root = Path(args.repo_root).resolve() if args.repo_root else None
    zip_path = Path(args.zip_path).resolve() if args.zip_path else None

    if args.root:
        root = Path(args.root).resolve()
        scope = infer_scope(root, zip_input=False)
        checks = build_checks(root, args.mode, scope, repo_root, None, strict)
        summary = aggregate_summary(args.mode, scope, checks)
        return summary, checks

    assert zip_path is not None
    with tempfile.TemporaryDirectory(prefix="uevolve-package-") as temp_dir:
        temp_root = Path(temp_dir)
        safe_extract_zip(zip_path, temp_root)
        root = find_validation_root(temp_root)
        scope = infer_scope(root, zip_input=True)
        checks = build_checks(root, args.mode, scope, repo_root, zip_path, strict)
        summary = aggregate_summary(args.mode, scope, checks)
        return summary, checks


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    summary, checks = run_validation(args)
    if args.json:
        print(json.dumps(make_payload(summary, checks), indent=2, sort_keys=False))
    else:
        print_pretty(summary, checks)
    return exit_code_for(checks, strict=bool(args.strict))


if __name__ == "__main__":
    result = main()
    if "unreal" in sys.modules:
        if result != 0:
            raise RuntimeError("verify_package_integrity.py failed with exit code %s" % result)
    else:
        raise SystemExit(result)
